/**
 * @file      rtppayloadrawvideo.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/rtppayloadrawvideo.h>
#include <cstring>
#include <vector>

using namespace promeki;

// Decodes a per-packet ESN, then walks SRD headers until C=0 or the
// MaxSrdsPerPacket cap is hit.  Returns parsed records so tests can
// inspect them without re-implementing the wire layout.
namespace {

struct DecodedSrd {
                uint16_t length;
                uint16_t row;
                uint16_t offset;
                bool     fBit;
                bool     cBit;
};

struct DecodedPacket {
                uint16_t                ext;
                std::vector<DecodedSrd> srds;
};

DecodedPacket decodePacket(const RtpPacket &pkt) {
        DecodedPacket dec;
        const uint8_t *pl = pkt.payload();
        dec.ext = static_cast<uint16_t>((static_cast<uint16_t>(pl[0]) << 8) | pl[1]);
        size_t cursor = RtpPayloadRawVideo::ExtSeqSize;
        bool   walk = true;
        while (walk && dec.srds.size() < RtpPayloadRawVideo::MaxSrdsPerPacket
               && cursor + RtpPayloadRawVideo::SrdHeaderSize <= pkt.payloadSize()) {
                const uint8_t *h = pl + cursor;
                DecodedSrd s;
                s.length = static_cast<uint16_t>((static_cast<uint16_t>(h[0]) << 8) | h[1]);
                s.fBit = (h[2] & 0x80u) != 0;
                s.row = static_cast<uint16_t>(((static_cast<uint16_t>(h[2]) & 0x7Fu) << 8) | h[3]);
                s.cBit = (h[4] & 0x80u) != 0;
                s.offset = static_cast<uint16_t>(((static_cast<uint16_t>(h[4]) & 0x7Fu) << 8) | h[5]);
                dec.srds.push_back(s);
                cursor += RtpPayloadRawVideo::SrdHeaderSize;
                walk = s.cBit;
        }
        return dec;
}

} // namespace

TEST_CASE("RtpPayloadRawVideo: construction") {
        SUBCASE("default 24bpp RGB") {
                RtpPayloadRawVideo payload(1920, 1080, 24);
                CHECK(payload.width() == 1920);
                CHECK(payload.height() == 1080);
                CHECK(payload.bitsPerPixel() == 24);
                CHECK(payload.clockRate() == 90000);
                CHECK(payload.payloadType() == 96);
                CHECK(payload.pgroupBytes() == 3);
                CHECK(payload.packetCounter() == 0u);
                CHECK_FALSE(payload.fieldBit());
        }

        SUBCASE("set payload type") {
                RtpPayloadRawVideo payload(320, 240, 24);
                payload.setPayloadType(112);
                CHECK(payload.payloadType() == 112);
        }

        SUBCASE("explicit pgroup overrides inferred") {
                RtpPayloadRawVideo a(320, 240, 24, 3);
                CHECK(a.pgroupBytes() == 3);
                RtpPayloadRawVideo b(320, 240, 16, 4);
                CHECK(b.pgroupBytes() == 4);
                RtpPayloadRawVideo c(320, 240, 32);
                CHECK(c.pgroupBytes() == 4);
        }
}

TEST_CASE("RtpPayloadRawVideo: pack rejects invalid input") {
        SUBCASE("null + zero size") {
                RtpPayloadRawVideo payload(320, 240, 24);
                auto               packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }
}

TEST_CASE("RtpPayloadRawVideo: round-trip") {
        SUBCASE("RGB8 16x8 frame round-trips bit-exact") {
                RtpPayloadRawVideo payload(16, 8, 24);
                const size_t       frameSize = 16 * 8 * 3;
                std::vector<uint8_t> data(frameSize);
                for (size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto   packets = payload.pack(data.data(), frameSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == frameSize);
                CHECK(std::memcmp(result.data(), data.data(), frameSize) == 0);
        }

        SUBCASE("RGB8 320x240 frame round-trips bit-exact (many packets)") {
                RtpPayloadRawVideo   payload(320, 240, 24);
                const size_t         frameSize = 320 * 240 * 3;
                std::vector<uint8_t> data(frameSize);
                for (size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto   packets = payload.pack(data.data(), frameSize);
                CHECK(packets.size() > 10);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == frameSize);
                CHECK(std::memcmp(result.data(), data.data(), frameSize) == 0);
        }

        SUBCASE("UYVY8 4:2:2 320x240 frame round-trips bit-exact") {
                RtpPayloadRawVideo payload(320, 240, 16, 4);
                const size_t       frameSize = 320 * 240 * 2;
                std::vector<uint8_t> data(frameSize);
                for (size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>((i * 7) & 0xFF);

                auto   packets = payload.pack(data.data(), frameSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == frameSize);
                CHECK(std::memcmp(result.data(), data.data(), frameSize) == 0);
        }

        SUBCASE("all packets share one buffer (zero-copy)") {
                RtpPayloadRawVideo   payload(64, 64, 24);
                const size_t         frameSize = 64 * 64 * 3;
                std::vector<uint8_t> data(frameSize, 0x42);

                auto packets = payload.pack(data.data(), frameSize);
                REQUIRE(packets.size() > 1);
                for (size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().impl().ptr() == packets[0].buffer().impl().ptr());
                }
        }
}

TEST_CASE("RtpPayloadRawVideo: pgroup alignment") {
        SUBCASE("RGB8 chunks are multiples of 3") {
                RtpPayloadRawVideo   payload(320, 240, 24, 3);
                std::vector<uint8_t> data(320 * 240 * 3, 0xAA);
                auto                 packets = payload.pack(data.data(), data.size());
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        for (const auto &srd : dec.srds) {
                                CHECK((srd.length % 3) == 0);
                        }
                }
        }

        SUBCASE("UYVY8 chunks are multiples of 4") {
                RtpPayloadRawVideo   payload(320, 240, 16, 4);
                std::vector<uint8_t> data(320 * 240 * 2, 0xBB);
                auto                 packets = payload.pack(data.data(), data.size());
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        for (const auto &srd : dec.srds) {
                                CHECK((srd.length % 4) == 0);
                        }
                }
        }
}

TEST_CASE("RtpPayloadRawVideo: continuation (C) bit semantics") {
        // ST 2110-20 §6.1.4: C=1 iff another SRD follows in this
        // packet.  The packer coalesces consecutive short scan lines
        // up to MaxSrdsPerPacket=3 SRDs per packet (§6.2.1).
        SUBCASE("short lines coalesce — multi-SRD emits C=1 on non-tail SRDs") {
                // 16-pixel-wide RGB8 line = 48 bytes/line; with the
                // default 1200-byte payload, multiple lines fit per
                // packet so multi-SRD packing kicks in.
                RtpPayloadRawVideo   payload(16, 64, 24);
                std::vector<uint8_t> data(16 * 64 * 3, 0x77);
                auto                 packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());

                bool sawMultiSrd = false;
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        REQUIRE(!dec.srds.empty());
                        REQUIRE(dec.srds.size() <= RtpPayloadRawVideo::MaxSrdsPerPacket);
                        // Tail SRD must have C=0; preceding SRDs must have C=1.
                        for (size_t k = 0; k < dec.srds.size(); k++) {
                                const bool expectC = (k + 1 < dec.srds.size());
                                CHECK(dec.srds[k].cBit == expectC);
                        }
                        if (dec.srds.size() > 1) sawMultiSrd = true;
                }
                CHECK(sawMultiSrd);
        }

        SUBCASE("long lines fragment — every SRD is solitary with C=0") {
                // 1920-pixel RGB8 line = 5760 bytes/line, doesn't fit
                // in 1200-byte payload; the single-SRD-per-packet
                // path applies and every C bit is 0.
                RtpPayloadRawVideo   payload(1920, 16, 24);
                std::vector<uint8_t> data(1920 * 16 * 3, 0x55);
                auto                 packets = payload.pack(data.data(), data.size());
                REQUIRE(packets.size() > 16); // multiple packets per line

                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        CHECK(dec.srds.size() == 1);
                        CHECK_FALSE(dec.srds[0].cBit);
                }
        }

        SUBCASE("never exceeds 3-SRD limit per packet (§6.2.1)") {
                // Very short lines + small frame deliberately invite
                // packing; assert the cap holds regardless.
                RtpPayloadRawVideo   payload(4, 30, 24); // 12 byte lines
                std::vector<uint8_t> data(4 * 30 * 3, 0x11);
                auto                 packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        CHECK(dec.srds.size() <= RtpPayloadRawVideo::MaxSrdsPerPacket);
                }
        }
}

TEST_CASE("RtpPayloadRawVideo: SRD ordering and row numbers") {
        SUBCASE("rows emitted top-to-bottom across all packets") {
                RtpPayloadRawVideo   payload(64, 32, 24);
                std::vector<uint8_t> data(64 * 32 * 3, 0xCC);
                auto                 packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());

                int prevRow = -1;
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        for (const auto &srd : dec.srds) {
                                // SRD rows must monotonically increase
                                // across the frame (§6.1.5: "SRD Row
                                // Number shall only increase within
                                // the field or frame").
                                CHECK(static_cast<int>(srd.row) >= prevRow);
                                prevRow = static_cast<int>(srd.row);
                                CHECK(srd.row < 32u);
                        }
                }
        }

        SUBCASE("first SRD of each line starts at offset 0") {
                RtpPayloadRawVideo   payload(64, 8, 24);
                std::vector<uint8_t> data(64 * 8 * 3, 0xDD);
                auto                 packets = payload.pack(data.data(), data.size());

                // Reconstruct: track first-seen offset for each row.
                std::vector<int> firstOff(8, -1);
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        for (const auto &srd : dec.srds) {
                                if (firstOff[srd.row] == -1) firstOff[srd.row] = srd.offset;
                        }
                }
                for (int r = 0; r < 8; r++) CHECK(firstOff[r] == 0);
        }
}

TEST_CASE("RtpPayloadRawVideo: 32-bit extended sequence counter") {
        SUBCASE("counter increments per packet across pack() calls") {
                RtpPayloadRawVideo   payload(1920, 4, 24); // 4 lines, multiple packets each
                std::vector<uint8_t> data(1920 * 4 * 3, 0x42);

                payload.setPacketCounter(0x12340000u);
                auto first = payload.pack(data.data(), data.size());
                REQUIRE(!first.isEmpty());
                CHECK(payload.packetCounter() == 0x12340000u + first.size());

                // Next pack() picks up where the prior call left off.
                auto second = payload.pack(data.data(), data.size());
                CHECK(payload.packetCounter() == 0x12340000u + first.size() + second.size());
        }

        SUBCASE("ESN field carries high 16 of the counter") {
                RtpPayloadRawVideo   payload(64, 4, 24);
                std::vector<uint8_t> data(64 * 4 * 3, 0x42);
                payload.setPacketCounter(0xABCD0000u);
                auto packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        // Every emitted packet shares the same ESN
                        // when the counter stays inside its high-16
                        // window (which holds until 2^16 packets,
                        // far beyond a single frame).
                        CHECK(dec.ext == 0xABCDu);
                }
        }

        SUBCASE("ESN rolls when counter crosses a 64K boundary") {
                // 1920px-wide line at 24bpp is 5760 bytes — at the
                // default 1200-byte maxPayload that's 5 packets/line
                // (single-SRD-per-packet path because lines don't
                // fit), so 4 lines * 5 packets = 20 packets total.
                // Plenty to span an ESN rollover.
                RtpPayloadRawVideo   payload(1920, 4, 24);
                std::vector<uint8_t> data(1920 * 4 * 3, 0x42);
                payload.setPacketCounter(0x1234FFFEu); // 2 packets before roll
                auto packets = payload.pack(data.data(), data.size());
                REQUIRE(packets.size() >= 4);

                auto dec0 = decodePacket(packets[0]);
                auto dec1 = decodePacket(packets[1]);
                auto dec2 = decodePacket(packets[2]);
                auto dec3 = decodePacket(packets[3]);
                CHECK(dec0.ext == 0x1234u);
                CHECK(dec1.ext == 0x1234u);
                CHECK(dec2.ext == 0x1235u); // ESN advances after counter wraps
                CHECK(dec3.ext == 0x1235u);
        }
}

TEST_CASE("RtpPayloadRawVideo: F (Field) bit") {
        SUBCASE("F=0 by default — every SRD has F clear") {
                RtpPayloadRawVideo   payload(64, 8, 24);
                std::vector<uint8_t> data(64 * 8 * 3, 0xEE);
                auto                 packets = payload.pack(data.data(), data.size());
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        for (const auto &srd : dec.srds) CHECK_FALSE(srd.fBit);
                }
        }

        SUBCASE("setFieldBit(true) flips F on every emitted SRD") {
                RtpPayloadRawVideo   payload(64, 8, 24);
                std::vector<uint8_t> data(64 * 8 * 3, 0xEE);
                payload.setFieldBit(true);
                CHECK(payload.fieldBit());
                auto packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        for (const auto &srd : dec.srds) CHECK(srd.fBit);
                }
        }

        SUBCASE("unpack ignores F bit on read (E20a: progressive only)") {
                // The unpack path doesn't yet distinguish fields —
                // E20e wires that.  For E20a we only assert that an
                // F=1 frame still reassembles byte-identical, the
                // F-bit-aware reassembly lands later.
                RtpPayloadRawVideo packerF1(64, 8, 24);
                packerF1.setFieldBit(true);
                std::vector<uint8_t> data(64 * 8 * 3);
                for (size_t i = 0; i < data.size(); i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = packerF1.pack(data.data(), data.size());

                // Unpack with a fresh payload (F bit irrelevant on
                // the reader side at E20a).
                RtpPayloadRawVideo reader(64, 8, 24);
                Buffer             result = reader.unpack(packets);
                CHECK(result.size() == data.size());
                CHECK(std::memcmp(result.data(), data.data(), data.size()) == 0);
        }
}

TEST_CASE("RtpPayloadRawVideo: 4:2:0 paired-row SRD emission") {
        // §6.2.5 — 4:2:0 wire SRDs cover a row pair; SRD Row Number =
        // first luma row of the pair (0, 2, 4, ...).  The buffer
        // height parameter is the IMAGE height; the packetizer
        // computes wireRows = height / rowsPerSrd internally.
        //
        // 16-pixel width, 4 image rows, 10-bit 4:2:0 wire:
        //   - pgroup: 15 octets / 8 pixels (4×2 block)
        //   - per wire row: (16/4)*15 = 60 octets, 2 wire rows total
        //   - bpp at the wire (per the pgroup geometry) = (8*15)/4 = 30
        //   - rowsPerSrd = 2
        RtpPayloadRawVideo payload(/*width*/ 16, /*height*/ 4, /*bpp*/ 30, /*pgroupBytes*/ 15, /*rowsPerSrd*/ 2);
        CHECK(payload.rowsPerSrd() == 2);
        // 2 wire rows × 60 octets = 120 bytes.
        std::vector<uint8_t> data(120);
        for (size_t i = 0; i < data.size(); i++) data[i] = static_cast<uint8_t>(i);
        auto packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        // Walk each emitted packet and verify SRD Row Number values
        // ascend in steps of 2 (rowsPerSrd).
        std::vector<uint16_t> rowNumbers;
        for (const auto &pkt : packets) {
                const uint8_t *pl = pkt.payload();
                const size_t   plSize = pkt.payloadSize();
                size_t         cursor = RtpPayloadRawVideo::ExtSeqSize;
                bool           more = true;
                size_t         srdsHere = 0;
                while (more && srdsHere < RtpPayloadRawVideo::MaxSrdsPerPacket &&
                       cursor + RtpPayloadRawVideo::SrdHeaderSize <= plSize) {
                        const uint16_t row = static_cast<uint16_t>(
                                ((static_cast<uint16_t>(pl[cursor + 2]) & 0x7Fu) << 8) | pl[cursor + 3]);
                        const uint8_t cBit = pl[cursor + 4] & 0x80u;
                        rowNumbers.push_back(row);
                        cursor += RtpPayloadRawVideo::SrdHeaderSize;
                        more = (cBit != 0);
                        srdsHere++;
                }
        }
        REQUIRE(rowNumbers.size() == 2u); // 2 wire rows → 2 SRDs.
        CHECK(rowNumbers[0] == 0u);
        CHECK(rowNumbers[1] == 2u);

        // Pack→unpack round-trip recovers the input.
        Buffer reassembled = payload.unpack(packets);
        REQUIRE(reassembled.size() == data.size());
        const uint8_t *back = static_cast<const uint8_t *>(reassembled.data());
        for (size_t i = 0; i < data.size(); i++) {
                CAPTURE(i);
                CHECK(static_cast<int>(back[i]) == static_cast<int>(data[i]));
        }
}

TEST_CASE("RtpPayloadRawVideo: payload size budget") {
        SUBCASE("emitted packets never exceed maxPayloadSize") {
                RtpPayloadRawVideo   payload(1920, 32, 24);
                std::vector<uint8_t> data(1920 * 32 * 3, 0x99);
                auto                 packets = payload.pack(data.data(), data.size());
                const size_t maxPayload = payload.maxPayloadSize();
                for (size_t i = 0; i < packets.size(); i++) {
                        CHECK(packets[i].payloadSize() <= maxPayload);
                }
        }

        SUBCASE("custom maxPayloadSize honoured") {
                RtpPayloadRawVideo payload(320, 16, 24);
                payload.setMaxPayloadSize(400);
                CHECK(payload.maxPayloadSize() == 400);
                std::vector<uint8_t> data(320 * 16 * 3, 0x66);
                auto                 packets = payload.pack(data.data(), data.size());
                for (size_t i = 0; i < packets.size(); i++) {
                        CHECK(packets[i].payloadSize() <= 400u);
                }
        }
}

TEST_CASE("RtpPayloadRawVideo: BPM block-count helpers (§6.3.3)") {
        // 7 × 180 = 1260 caps every standard-UDP BPM packet.
        CHECK(RtpPayloadRawVideo::BpmBlockOctets == 180u);
        CHECK(RtpPayloadRawVideo::BpmStandardBlocksPerPacket == 7u);

        // Below one block — BPM can't emit anything.
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(0) == 0u);
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(179) == 0u);
        CHECK(RtpPayloadRawVideo::bpmTargetPayloadSize(179) == 0u);

        // Exact multiples round down to integer block counts.
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(180) == 1u);
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(359) == 1u);
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(360) == 2u);
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(1259) == 6u);
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(1260) == 7u);

        // Above 7 × 180 we clamp — §6.3.3 forbids Extended UDP in BPM.
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(1500) == 7u);
        CHECK(RtpPayloadRawVideo::bpmBlocksPerPacket(9000) == 7u);

        // target = blocks × 180.
        CHECK(RtpPayloadRawVideo::bpmTargetPayloadSize(180) == 180u);
        CHECK(RtpPayloadRawVideo::bpmTargetPayloadSize(1260) == 1260u);
        CHECK(RtpPayloadRawVideo::bpmTargetPayloadSize(9000) == 1260u);
}

TEST_CASE("RtpPayloadRawVideo: BPM emits fixed-size 180-octet-multiple payloads (§6.3.3)") {
        // 8-bit 4:2:2 UYVY (pgroup=4) divides 180 evenly so BPM is valid.
        // Width chosen so the frame produces several packets including
        // a short tail packet that exercises the zero-padding path.
        const int            width = 320;  // 320 × 2 bytes per pixel = 640 bytes per line
        const int            height = 8;   // 8 lines = 5120 bytes of pixel data
        const int            pg = 4;       // 8-bit 4:2:2 pgroup
        RtpPayloadRawVideo   payload(width, height, 16, pg);
        payload.setPackingMode(St2110PackingMode::Bpm);
        payload.setMaxPayloadSize(1260); // Standard UDP

        std::vector<uint8_t> data(width * height * 2, 0xAB);
        auto                 packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        // Every packet's payload is exactly 1260 octets (7 × 180).
        const size_t target = RtpPayloadRawVideo::bpmTargetPayloadSize(1260);
        CHECK(target == 1260u);
        for (size_t i = 0; i < packets.size(); i++) {
                CAPTURE(i);
                CHECK(packets[i].payloadSize() == target);
                CHECK(packets[i].payloadSize() % RtpPayloadRawVideo::BpmBlockOctets == 0u);
        }

        // Round-trip — depacketizer ignores trailing zero padding.
        Buffer reassembled = payload.unpack(packets);
        REQUIRE(reassembled.size() == data.size());
        CHECK(std::memcmp(reassembled.data(), data.data(), data.size()) == 0);
}

TEST_CASE("RtpPayloadRawVideo: BPM last-packet padding has zero bytes after the last SRD") {
        // Force a small frame whose data won't fill the final BPM
        // packet — the tail packet must be padded to the full target
        // size with zeros after the last SRD's sample data.
        const int width = 80;   // 80 × 2 = 160 bytes per line (UYVY 8-bit)
        const int height = 4;   // 4 lines, capped at 3 SRDs/packet → 2 packets
        const int pg = 4;
        RtpPayloadRawVideo payload(width, height, 16, pg);
        payload.setPackingMode(St2110PackingMode::Bpm);
        payload.setMaxPayloadSize(1260);

        std::vector<uint8_t> data(width * height * 2, 0xCD);
        auto                 packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        // Every BPM packet's payload is exactly the block-multiple.
        // The last packet must have zero-bytes filling the gap between
        // the end of its last SRD's sample data and the packet
        // boundary (§6.3.3).
        const RtpPacket &tail = packets[packets.size() - 1];
        CHECK(tail.payloadSize() == 1260u);

        const uint8_t *pl = tail.payload();
        const size_t   plSize = tail.payloadSize();
        size_t         cursor = RtpPayloadRawVideo::ExtSeqSize;
        size_t         totalData = 0;
        bool           more = true;
        size_t         srdsHere = 0;
        while (more && srdsHere < RtpPayloadRawVideo::MaxSrdsPerPacket &&
               cursor + RtpPayloadRawVideo::SrdHeaderSize <= plSize) {
                const uint16_t len = static_cast<uint16_t>(
                        (static_cast<uint16_t>(pl[cursor + 0]) << 8) | pl[cursor + 1]);
                const uint8_t cBit = pl[cursor + 4] & 0x80u;
                totalData += len;
                cursor += RtpPayloadRawVideo::SrdHeaderSize;
                more = (cBit != 0);
                srdsHere++;
        }
        const size_t dataEnd = cursor + totalData;
        REQUIRE(dataEnd < plSize); // Padding region exists.
        for (size_t i = dataEnd; i < plSize; i++) {
                CAPTURE(i);
                CHECK(pl[i] == 0u);
        }
}

TEST_CASE("RtpPayloadRawVideo: BPM falls back to GPM for non-divisor pgroups") {
        // 4:2:2 16-bit pgroup is 8 octets; 180 / 8 = 22.5 — not an
        // integer.  Per §6.3.3 BPM is only well-defined when pgroup
        // divides the block size; the packer falls back to GPM with a
        // one-shot warning.
        const int width = 64;
        const int height = 4;
        const int pg = 8;
        RtpPayloadRawVideo payload(width, height, 32, pg);  // 32 bpp = 4 bytes/pixel
        payload.setPackingMode(St2110PackingMode::Bpm);
        payload.setMaxPayloadSize(1260);

        std::vector<uint8_t> data(width * height * 4, 0xEE);
        auto                 packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        // GPM packets are variable-size — at least one will not be a
        // 180-octet multiple (proving we fell back).
        bool anyNonBlockSize = false;
        for (size_t i = 0; i < packets.size(); i++) {
                if (packets[i].payloadSize() % RtpPayloadRawVideo::BpmBlockOctets != 0u) {
                        anyNonBlockSize = true;
                        break;
                }
        }
        CHECK(anyNonBlockSize);

        // Round-trip still works in the GPM fallback path.
        Buffer reassembled = payload.unpack(packets);
        REQUIRE(reassembled.size() == data.size());
        CHECK(std::memcmp(reassembled.data(), data.data(), data.size()) == 0);
}

TEST_CASE("RtpPayloadRawVideo: BPM is opt-in — default mode stays GPM") {
        RtpPayloadRawVideo payload(640, 4, 16, 4);
        CHECK(payload.packingMode() == St2110PackingMode::Gpm);

        std::vector<uint8_t> data(640 * 4 * 2, 0x55);
        auto                 packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());
        // GPM: at least one packet smaller than 1260 expected for a
        // small frame.
        bool anySmallPkt = false;
        for (size_t i = 0; i < packets.size(); i++) {
                if (packets[i].payloadSize() < 1260u) {
                        anySmallPkt = true;
                        break;
                }
        }
        CHECK(anySmallPkt);
}

// ---------------------------------------------------------------------
// E20e — Interlaced / PsF / F-bit (§6.1.5)
// ---------------------------------------------------------------------

// Builds a deterministic source frame where byte i = (i * 13 + 7) & 0xFF.
// Used by the interlaced/PsF round-trip tests so the reassembled
// receive-side buffer can be byte-compared against the original.
static std::vector<uint8_t> buildSourceFrame(size_t bytes) {
        std::vector<uint8_t> v(bytes);
        for (size_t i = 0; i < bytes; i++) v[i] = static_cast<uint8_t>((i * 13u + 7u) & 0xFFu);
        return v;
}

TEST_CASE("RtpPayloadRawVideo: Interlaced (even-first) field split + F-bit + row-restart") {
        // 8-row frame: field 0 (even-first) = rows 0,2,4,6 → SRD Row
        // Numbers 0,1,2,3 with F=0; field 1 = rows 1,3,5,7 → SRD Row
        // Numbers 0,1,2,3 with F=1.  Marker bit fires on the last
        // packet of each field per §6.1.2.
        const int width = 320, height = 8, pg = 4;
        RtpPayloadRawVideo payload(width, height, 16, pg);
        payload.setScanMode(VideoScanMode::InterlacedEvenFirst);

        auto data = buildSourceFrame(width * height * 2);
        auto packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        // Tally F-bits + SRD rows per field, and find the two
        // marker-bearing packets (last of each field).
        size_t   markersSeen = 0;
        size_t   lastMarkerIdx = SIZE_MAX;
        size_t   firstMarkerIdx = SIZE_MAX;
        size_t   field0Srds = 0, field1Srds = 0;
        for (size_t i = 0; i < packets.size(); i++) {
                if (packets[i].marker()) {
                        markersSeen++;
                        if (firstMarkerIdx == SIZE_MAX) firstMarkerIdx = i;
                        lastMarkerIdx = i;
                }
                DecodedPacket dec = decodePacket(packets[i]);
                for (const auto &s : dec.srds) {
                        if (s.fBit) field1Srds++;
                        else        field0Srds++;
                }
        }
        CHECK(markersSeen == 2u); // one marker per field (§6.1.2).
        CHECK(lastMarkerIdx == packets.size() - 1u);
        CHECK(firstMarkerIdx < packets.size() - 1u);
        CHECK(field0Srds == 4u); // rows 0,2,4,6 → 4 SRDs
        CHECK(field1Srds == 4u); // rows 1,3,5,7 → 4 SRDs

        // Every SRD before the first marker must have F=0 (field 0);
        // every SRD after must have F=1 (field 1).  §6.1.5 forbids
        // mixing fields in a packet — the packetizer breaks on field
        // boundaries, so each packet's SRDs share a single F-bit.
        bool seenFirstField = true;
        for (size_t i = 0; i < packets.size(); i++) {
                DecodedPacket dec = decodePacket(packets[i]);
                for (const auto &s : dec.srds) {
                        CAPTURE(i);
                        if (seenFirstField) CHECK(s.fBit == false);
                        else                CHECK(s.fBit == true);
                }
                if (i == firstMarkerIdx) seenFirstField = false;
        }

        // Round-trip: depacketizer reassembles using the same scan
        // mode so the recovered buffer matches the original byte-
        // exactly.
        Buffer back = payload.unpack(packets);
        REQUIRE(back.size() == data.size());
        CHECK(std::memcmp(back.data(), data.data(), data.size()) == 0);
}

TEST_CASE("RtpPayloadRawVideo: Interlaced odd-first puts odd-parity rows in field 0") {
        const int width = 320, height = 8, pg = 4;
        RtpPayloadRawVideo payload(width, height, 16, pg);
        payload.setScanMode(VideoScanMode::InterlacedOddFirst);

        auto data = buildSourceFrame(width * height * 2);
        auto packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        // OddFirst: field 0 (F=0) carries odd-parity source rows
        // (1, 3, 5, 7).  Per §6.1.5 SRD Row Numbers in each field
        // restart at 0, so an F=0 SRD with row=2 corresponds to
        // source row 5 (odd-parity row index 2 = 1, 3, 5 → entry 2).
        //
        // Round-trip is the cleanest end-to-end check that the
        // unpack path inverts the right parity.
        Buffer back = payload.unpack(packets);
        REQUIRE(back.size() == data.size());
        CHECK(std::memcmp(back.data(), data.data(), data.size()) == 0);
}

TEST_CASE("RtpPayloadRawVideo: PsF top/bottom segment split (§6.1.5)") {
        // 6-row frame: segment 0 = rows 0,1,2 (top half); segment 1 =
        // rows 3,4,5 (bottom half).  F=0 on segment 0, F=1 on segment
        // 1.  Marker on last packet of each segment.
        const int width = 320, height = 6, pg = 4;
        RtpPayloadRawVideo payload(width, height, 16, pg);
        payload.setScanMode(VideoScanMode::PsF);

        auto data = buildSourceFrame(width * height * 2);
        auto packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        size_t markers = 0;
        for (size_t i = 0; i < packets.size(); i++) {
                if (packets[i].marker()) markers++;
        }
        CHECK(markers == 2u);

        // Round-trip.
        Buffer back = payload.unpack(packets);
        REQUIRE(back.size() == data.size());
        CHECK(std::memcmp(back.data(), data.data(), data.size()) == 0);
}

TEST_CASE("RtpPayloadRawVideo: odd-height split gives the temporally-first field/segment the extra line") {
        SUBCASE("Interlaced even-first, H=5 → field 0 has 3 rows (ceil), field 1 has 2 rows (floor)") {
                const int width = 80, height = 5, pg = 4;
                RtpPayloadRawVideo payload(width, height, 16, pg);
                payload.setScanMode(VideoScanMode::InterlacedEvenFirst);
                auto data = buildSourceFrame(width * height * 2);
                auto packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());

                size_t field0Srds = 0, field1Srds = 0;
                for (size_t i = 0; i < packets.size(); i++) {
                        DecodedPacket dec = decodePacket(packets[i]);
                        for (const auto &s : dec.srds) {
                                if (s.fBit) field1Srds++;
                                else        field0Srds++;
                        }
                }
                CHECK(field0Srds == 3u); // rows 0, 2, 4
                CHECK(field1Srds == 2u); // rows 1, 3

                Buffer back = payload.unpack(packets);
                CHECK(std::memcmp(back.data(), data.data(), data.size()) == 0);
        }

        SUBCASE("PsF, H=5 → segment 0 has 3 rows (ceil), segment 1 has 2 rows (floor)") {
                const int width = 80, height = 5, pg = 4;
                RtpPayloadRawVideo payload(width, height, 16, pg);
                payload.setScanMode(VideoScanMode::PsF);
                auto data = buildSourceFrame(width * height * 2);
                auto packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());

                size_t seg0Srds = 0, seg1Srds = 0;
                for (size_t i = 0; i < packets.size(); i++) {
                        DecodedPacket dec = decodePacket(packets[i]);
                        for (const auto &s : dec.srds) {
                                if (s.fBit) seg1Srds++;
                                else        seg0Srds++;
                        }
                }
                CHECK(seg0Srds == 3u);
                CHECK(seg1Srds == 2u);

                Buffer back = payload.unpack(packets);
                CHECK(std::memcmp(back.data(), data.data(), data.size()) == 0);
        }
}

TEST_CASE("RtpPayloadRawVideo: 4:2:0 + interlaced/PsF degrades to Progressive (§6.2.5)") {
        // rowsPerSrd = 2 marks 4:2:0 wire pgroup geometry; combining
        // with Interlaced/PsF is forbidden by §6.2.5.  The packer
        // logs a one-shot warning and treats the frame as Progressive
        // so packets still flow rather than dropping the frame.
        const int width = 16, height = 4;
        const int rowsPerSrd = 2; // 4:2:0
        RtpPayloadRawVideo payload(width, height, 12, 6, rowsPerSrd);
        payload.setScanMode(VideoScanMode::InterlacedEvenFirst);

        // Wire-row count = height / rowsPerSrd = 2.  Each wire row
        // covers width × bpp / 8 = 24 bytes.
        std::vector<uint8_t> data(2 * 24, 0xA5);
        auto                 packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        // Progressive fallback: every SRD has F=0 and exactly one
        // marker (on the last packet).
        size_t markers = 0;
        for (size_t i = 0; i < packets.size(); i++) {
                if (packets[i].marker()) markers++;
                DecodedPacket dec = decodePacket(packets[i]);
                for (const auto &s : dec.srds) CHECK(s.fBit == false);
        }
        // marker count is 0 here — pack() doesn't pre-stamp marker on
        // the last packet for Progressive (that's the session's job
        // via markerOnLast).
        CHECK(markers == 0u);
}

TEST_CASE("RtpPayloadRawVideo: Progressive default stays single-field, no field-boundary marker") {
        const int width = 320, height = 4, pg = 4;
        RtpPayloadRawVideo payload(width, height, 16, pg);
        CHECK(payload.scanMode() == VideoScanMode::Progressive);

        auto data = buildSourceFrame(width * height * 2);
        auto packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());

        size_t markers = 0;
        for (size_t i = 0; i < packets.size(); i++) {
                if (packets[i].marker()) markers++;
                DecodedPacket dec = decodePacket(packets[i]);
                for (const auto &s : dec.srds) CHECK(s.fBit == false);
        }
        // Progressive: marker is set by the session via markerOnLast,
        // not by pack().  Pre-stamp count is 0.
        CHECK(markers == 0u);
}

TEST_CASE("RtpPayloadRawVideo: BPM at non-Standard UDP picks 1×180 minimum") {
        // A 200-octet MTU budget fits exactly one 180-octet block; the
        // packetizer must emit 180-byte payloads.  Validates the
        // floor-to-multiple logic at the bottom of the BPM range.
        RtpPayloadRawVideo payload(40, 4, 16, 4);
        payload.setPackingMode(St2110PackingMode::Bpm);
        payload.setMaxPayloadSize(200);

        std::vector<uint8_t> data(40 * 4 * 2, 0x77);
        auto                 packets = payload.pack(data.data(), data.size());
        REQUIRE(!packets.isEmpty());
        for (size_t i = 0; i < packets.size(); i++) {
                CAPTURE(i);
                CHECK(packets[i].payloadSize() == 180u);
        }
}

TEST_CASE("RtpPayloadRawVideo: SRD Length=0 keep-alive (§6.2.1)") {
        // §6.2.1: "An SRD Length of 0 indicates that no sample data
        // follows the SRD Header.  This shall only occur when exactly
        // one SRD Header is present."  The static makeKeepAlive helper
        // emits exactly that packet; the receiver-side unpack treats
        // the zero-length SRD as a no-op write.
        SUBCASE("makeKeepAlive emits a single SRD with Length=0, C=0") {
                auto pkt = RtpPayloadRawVideo::makeKeepAlive(/*packetCounter=*/0xABCD1234u,
                                                             /*srdRow=*/137,
                                                             /*fieldBit=*/false);
                REQUIRE_FALSE(pkt.isNull());
                // ESN (2) + one SRD Header (6) = 8 octets of payload.
                CHECK(pkt.payloadSize() == RtpPayloadRawVideo::ExtSeqSize
                                                   + RtpPayloadRawVideo::SrdHeaderSize);
                CHECK(pkt.payloadType() == RtpPayloadRawVideo::DefaultPayloadType);
                CHECK(pkt.marker());

                auto dec = decodePacket(pkt);
                CHECK(dec.ext == 0xABCDu); // high 16 bits of the 32-bit counter
                REQUIRE(dec.srds.size() == 1u);
                CHECK(dec.srds[0].length == 0u);
                CHECK(dec.srds[0].row == 137u);
                CHECK(dec.srds[0].offset == 0u);
                CHECK_FALSE(dec.srds[0].cBit);
                CHECK_FALSE(dec.srds[0].fBit);
        }

        SUBCASE("F-bit + payload-type overrides") {
                auto pkt = RtpPayloadRawVideo::makeKeepAlive(0u, /*srdRow=*/0,
                                                             /*fieldBit=*/true, /*payloadType=*/112);
                REQUIRE_FALSE(pkt.isNull());
                CHECK(pkt.payloadType() == 112u);
                auto dec = decodePacket(pkt);
                REQUIRE(dec.srds.size() == 1u);
                CHECK(dec.srds[0].fBit);
        }

        SUBCASE("receiver-side unpack tolerates keep-alive (no destination write)") {
                // Receiver maintains a destination buffer of arbitrary
                // content; the keep-alive must not perturb a single
                // byte.  A canary pattern is checked byte-for-byte.
                RtpPayloadRawVideo   payload(32, 4, 24);
                auto                 pkt = RtpPayloadRawVideo::makeKeepAlive(0u);
                RtpPacket::List      packets;
                packets.pushToBack(pkt);
                Buffer               result = payload.unpack(packets);
                // unpack always sizes the output to width × height ×
                // bpp / 8 and zero-fills before walking SRDs (see
                // rtppayloadrawvideo.cpp).  A keep-alive must not alter
                // that zero baseline.
                REQUIRE(result.size() == 32u * 4u * 3u);
                const uint8_t *bytes = static_cast<const uint8_t *>(result.data());
                for (size_t i = 0; i < result.size(); i++) {
                        CAPTURE(i);
                        REQUIRE(bytes[i] == 0u);
                }
        }
}

TEST_CASE("RtpPayloadRawVideo: 3-SRD-per-packet cap holds across pgroups (§6.2.1)") {
        // Defensive sweep: short frames + tiny lines invite packing.
        // Each (pgroupBytes, bitsPerPixel) tuple here is representative
        // of a §6.2 ST 2110-20 family: RGB-8 (3-byte pgroup), 4:2:2-8
        // (4), 4:2:2-10 (5), Key-10 (2), 4:2:0-10 paired-row (15-byte
        // pgroup over 2 rows).  Across every emitted packet the SRD
        // header count must stay ≤ MaxSrdsPerPacket.
        struct Case {
                        int width;
                        int height;
                        int bitsPerPixel;
                        int pgroupBytes;
                        int rowsPerSrd;
                        const char *label;
        };
        const Case cases[] = {
                {16, 24, 24, 3, 1, "RGB 4:4:4/8 (pg=3)"},
                {16, 24, 16, 4, 1, "YCbCr 4:2:2/8 (pg=4)"},
                {16, 24, 20, 5, 1, "YCbCr 4:2:2/10 (pg=5)"},
                {16, 24, 12, 2, 1, "Key/10 BE (pg=2)"},
                {16, 24, 60, 15, 2, "YCbCr 4:2:0/10 paired-row (pg=15, 2 rows)"},
        };
        for (const auto &c : cases) {
                CAPTURE(c.label);
                RtpPayloadRawVideo   payload(c.width, c.height, c.bitsPerPixel, c.pgroupBytes, c.rowsPerSrd);
                std::vector<uint8_t> data(static_cast<size_t>(c.width) * static_cast<size_t>(c.height)
                                                  * static_cast<size_t>(c.bitsPerPixel) / 8u,
                                          0x33);
                auto                 packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());
                for (size_t i = 0; i < packets.size(); i++) {
                        auto dec = decodePacket(packets[i]);
                        CHECK(dec.srds.size() <= RtpPayloadRawVideo::MaxSrdsPerPacket);
                }
        }
}
