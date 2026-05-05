/**
 * @file      rtppayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/rtppayload.h>
#include <cstring>
#include <cstdlib>
#include <vector>

using namespace promeki;

// Build a minimal valid JPEG byte stream with the given entropy-coded data size.
// Returns a vector containing: SOI + DQT(64-byte table) + SOS header + entropy data.
static std::vector<uint8_t> buildMinimalJpeg(size_t entropySize) {
        std::vector<uint8_t> jpeg;
        // SOI
        jpeg.push_back(0xFF);
        jpeg.push_back(0xD8);
        // DQT marker (length=67: 2 length + 1 Pq/Tq + 64 table bytes)
        jpeg.push_back(0xFF);
        jpeg.push_back(0xDB);
        jpeg.push_back(0x00);
        jpeg.push_back(0x43);
        jpeg.push_back(0x00); // Pq=0, Tq=0
        for (int i = 0; i < 64; i++) jpeg.push_back(static_cast<uint8_t>(i + 1));
        // SOS marker (length=8: minimal 1-component)
        jpeg.push_back(0xFF);
        jpeg.push_back(0xDA);
        jpeg.push_back(0x00);
        jpeg.push_back(0x08);
        jpeg.push_back(0x01);
        jpeg.push_back(0x01);
        jpeg.push_back(0x00);
        jpeg.push_back(0x00);
        jpeg.push_back(0x3F);
        jpeg.push_back(0x00);
        // Entropy-coded data (avoid 0xFF bytes to prevent marker confusion)
        for (size_t i = 0; i < entropySize; i++) {
                jpeg.push_back(static_cast<uint8_t>((i & 0x7F) + 1));
        }
        return jpeg;
}

TEST_CASE("RtpPayloadL24") {

        SUBCASE("construction") {
                RtpPayloadL24 payload(48000, 2);
                CHECK(payload.clockRate() == 48000);
                CHECK(payload.channels() == 2);
                CHECK(payload.payloadType() == 97);
                CHECK(payload.maxPayloadSize() == 1200);
        }

        SUBCASE("set payload type") {
                RtpPayloadL24 payload;
                payload.setPayloadType(111);
                CHECK(payload.payloadType() == 111);
        }

        SUBCASE("pack empty data") {
                RtpPayloadL24 payload;
                auto          packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("pack small audio") {
                RtpPayloadL24 payload(48000, 2);
                // 48 samples * 2 channels * 3 bytes = 288 bytes
                const size_t dataSize = 288;
                uint8_t      data[288];
                for (size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, dataSize);
                CHECK(packets.size() == 1);
                CHECK(packets[0].size() == RtpPacket::HeaderSize + dataSize);
        }

        SUBCASE("pack large audio fragments correctly") {
                RtpPayloadL24 payload(48000, 2);
                // 3000 bytes of audio data — should split into multiple packets
                const size_t dataSize = 3000;
                uint8_t      data[3000];
                for (size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, dataSize);
                CHECK(packets.size() > 1);

                // All packets should share the same buffer
                for (size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().impl().ptr() == packets[0].buffer().impl().ptr());
                }
        }

        SUBCASE("pack/unpack round-trip") {
                RtpPayloadL24 payload(48000, 2);
                const size_t  dataSize = 576; // 96 samples * 2 ch * 3 bytes
                uint8_t       data[576];
                for (size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto   packets = payload.pack(data, dataSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == dataSize);
                CHECK(std::memcmp(result.data(), data, dataSize) == 0);
        }

        SUBCASE("large round-trip") {
                RtpPayloadL24 payload(48000, 8);
                // 1000 samples * 8 channels * 3 bytes = 24000 bytes
                const size_t         dataSize = 24000;
                std::vector<uint8_t> data(dataSize);
                for (size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data.data(), dataSize);
                CHECK(packets.size() > 1);

                Buffer result = payload.unpack(packets);
                CHECK(result.size() == dataSize);
                CHECK(std::memcmp(result.data(), data.data(), dataSize) == 0);
        }
}

TEST_CASE("RtpPayloadL16") {

        SUBCASE("construction") {
                RtpPayloadL16 payload(48000, 2);
                CHECK(payload.clockRate() == 48000);
                CHECK(payload.channels() == 2);
                CHECK(payload.payloadType() == 96);
                CHECK(payload.sampleRate() == 48000);
        }

        SUBCASE("set payload type") {
                RtpPayloadL16 payload;
                payload.setPayloadType(110);
                CHECK(payload.payloadType() == 110);
        }

        SUBCASE("pack empty data") {
                RtpPayloadL16 payload;
                auto          packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("pack/unpack round-trip") {
                RtpPayloadL16 payload(48000, 2);
                const size_t  dataSize = 384; // 96 samples * 2 ch * 2 bytes
                uint8_t       data[384];
                for (size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto   packets = payload.pack(data, dataSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == dataSize);
                CHECK(std::memcmp(result.data(), data, dataSize) == 0);
        }

        SUBCASE("large round-trip") {
                RtpPayloadL16        payload(48000, 4);
                const size_t         dataSize = 9600; // 1200 samples * 4 ch * 2 bytes
                std::vector<uint8_t> data(dataSize);
                for (size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data.data(), dataSize);
                CHECK(packets.size() > 1);

                Buffer result = payload.unpack(packets);
                CHECK(result.size() == dataSize);
                CHECK(std::memcmp(result.data(), data.data(), dataSize) == 0);
        }
}

TEST_CASE("RtpPayloadRawVideo") {

        SUBCASE("construction") {
                RtpPayloadRawVideo payload(1920, 1080, 24);
                CHECK(payload.width() == 1920);
                CHECK(payload.height() == 1080);
                CHECK(payload.bitsPerPixel() == 24);
                CHECK(payload.clockRate() == 90000);
                CHECK(payload.payloadType() == 96);
        }

        SUBCASE("set payload type") {
                RtpPayloadRawVideo payload(320, 240, 24);
                payload.setPayloadType(112);
                CHECK(payload.payloadType() == 112);
        }

        SUBCASE("pack empty data") {
                RtpPayloadRawVideo payload(320, 240, 24);
                auto               packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("pack small frame") {
                // 8x4 pixels at 24bpp = 96 bytes total
                RtpPayloadRawVideo payload(8, 4, 24);
                const size_t       frameSize = 8 * 4 * 3;
                uint8_t            data[96];
                for (size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, frameSize);
                CHECK(packets.size() > 0);
        }

        SUBCASE("pack/unpack round-trip small") {
                RtpPayloadRawVideo payload(16, 8, 24);
                const size_t       frameSize = 16 * 8 * 3; // 384 bytes
                uint8_t            data[384];
                for (size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto   packets = payload.pack(data, frameSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == frameSize);
                CHECK(std::memcmp(result.data(), data, frameSize) == 0);
        }

        SUBCASE("pack/unpack round-trip large") {
                // 320x240 at 24bpp = 230400 bytes
                RtpPayloadRawVideo   payload(320, 240, 24);
                const size_t         frameSize = 320 * 240 * 3;
                std::vector<uint8_t> data(frameSize);
                for (size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data.data(), frameSize);
                CHECK(packets.size() > 100); // Should be many packets

                Buffer result = payload.unpack(packets);
                CHECK(result.size() == frameSize);
                CHECK(std::memcmp(result.data(), data.data(), frameSize) == 0);
        }

        SUBCASE("all packets share one buffer") {
                RtpPayloadRawVideo   payload(64, 64, 24);
                const size_t         frameSize = 64 * 64 * 3;
                std::vector<uint8_t> data(frameSize, 0x42);

                auto packets = payload.pack(data.data(), frameSize);
                for (size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().impl().ptr() == packets[0].buffer().impl().ptr());
                }
        }

        SUBCASE("continuation bit is always zero") {
                // RFC 4175 §4: C is set only when another scan line
                // header follows in the SAME packet.  We emit one
                // line header per packet, so C must always be 0.
                RtpPayloadRawVideo   payload(320, 240, 24, 3);
                const size_t         frameSize = 320 * 240 * 3;
                std::vector<uint8_t> data(frameSize, 0x55);
                auto                 packets = payload.pack(data.data(), frameSize);
                REQUIRE(packets.size() > 10);

                // Per-line header starts at payload offset 2 (ext seq).
                // C bit is MSB of the offset field at byte 4 of the
                // line header (payload bytes 6-7).
                for (size_t i = 0; i < packets.size(); i++) {
                        const uint8_t *pl = packets[i].payload();
                        uint8_t        offsetHi = pl[2 + 4]; // ext_seq(2) + line_hdr byte 4
                        CHECK((offsetHi & 0x80) == 0);
                }
        }

        SUBCASE("pgroup alignment for RGB8") {
                // RGB8 has pgroup = 3 bytes.  Every chunk's byte count
                // (the Length field in the per-line header) must be a
                // multiple of 3 so that no pixel is split across packets.
                RtpPayloadRawVideo   payload(320, 240, 24, 3);
                const size_t         frameSize = 320 * 240 * 3;
                std::vector<uint8_t> data(frameSize, 0xAA);
                auto                 packets = payload.pack(data.data(), frameSize);

                for (size_t i = 0; i < packets.size(); i++) {
                        const uint8_t *pl = packets[i].payload();
                        uint16_t       dataLen = (static_cast<uint16_t>(pl[2]) << 8) | pl[3];
                        CHECK((dataLen % 3) == 0);
                }
        }

        SUBCASE("pgroup alignment for UYVY8") {
                // UYVY8 422 has pgroup = 4 bytes (2 pixels per 4 bytes).
                RtpPayloadRawVideo   payload(320, 240, 16, 4);
                const size_t         frameSize = 320 * 240 * 2;
                std::vector<uint8_t> data(frameSize, 0xBB);
                auto                 packets = payload.pack(data.data(), frameSize);

                for (size_t i = 0; i < packets.size(); i++) {
                        const uint8_t *pl = packets[i].payload();
                        uint16_t       dataLen = (static_cast<uint16_t>(pl[2]) << 8) | pl[3];
                        CHECK((dataLen % 4) == 0);
                }
        }

        SUBCASE("pgroupBytes accessor") {
                RtpPayloadRawVideo a(320, 240, 24, 3);
                CHECK(a.pgroupBytes() == 3);
                RtpPayloadRawVideo b(320, 240, 16, 4);
                CHECK(b.pgroupBytes() == 4);
                // Default: inferred from bpp
                RtpPayloadRawVideo c(320, 240, 32);
                CHECK(c.pgroupBytes() == 4);
        }
}

TEST_CASE("RtpPayloadJpeg") {

        SUBCASE("construction") {
                RtpPayloadJpeg payload(1920, 1080, 85);
                CHECK(payload.width() == 1920);
                CHECK(payload.height() == 1080);
                CHECK(payload.quality() == 85);
                CHECK(payload.clockRate() == 90000);
                CHECK(payload.payloadType() == 26);
        }

        SUBCASE("pack empty data") {
                RtpPayloadJpeg payload(640, 480);
                auto           packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("set quality") {
                RtpPayloadJpeg payload(640, 480);
                payload.setQuality(50);
                CHECK(payload.quality() == 50);
        }

        SUBCASE("pack small JPEG") {
                RtpPayloadJpeg payload(320, 240);
                auto           jpeg = buildMinimalJpeg(200);

                auto packets = payload.pack(jpeg.data(), jpeg.size());
                CHECK(packets.size() >= 1);
        }

        SUBCASE("large JPEG fragmentation") {
                RtpPayloadJpeg payload(1920, 1080, 85);
                // Build a JPEG with enough entropy data to require multiple packets
                auto jpeg = buildMinimalJpeg(5000);

                auto packets = payload.pack(jpeg.data(), jpeg.size());
                CHECK(packets.size() > 1);

                // All packets share one backing buffer
                for (size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().impl().ptr() == packets[0].buffer().impl().ptr());
                }
        }

        SUBCASE("JPEG header fields") {
                RtpPayloadJpeg payload(640, 480, 75);

                // Build minimal JPEG: SOI + DQT + SOS header + entropy data
                uint8_t data[128];
                size_t  pos = 0;
                // SOI
                data[pos++] = 0xFF;
                data[pos++] = 0xD8;
                // DQT marker with a 64-byte table (length = 67: 2 for length + 1 Pq/Tq + 64 table)
                data[pos++] = 0xFF;
                data[pos++] = 0xDB;
                data[pos++] = 0x00;
                data[pos++] = 0x43; // length = 67
                data[pos++] = 0x00; // Pq=0 (8-bit), Tq=0
                for (int i = 0; i < 64; i++) data[pos++] = static_cast<uint8_t>(i + 1);
                // SOS marker (minimal: length=8, 1 component)
                data[pos++] = 0xFF;
                data[pos++] = 0xDA;
                data[pos++] = 0x00;
                data[pos++] = 0x08; // length = 8
                data[pos++] = 0x01; // Ns = 1 component
                data[pos++] = 0x01;
                data[pos++] = 0x00; // Cs=1, Td=0/Ta=0
                data[pos++] = 0x00;
                data[pos++] = 0x3F;
                data[pos++] = 0x00; // Ss, Se, Ah/Al
                // Entropy-coded data
                for (int i = 0; i < 20; i++) data[pos++] = static_cast<uint8_t>(0xA0 + i);

                auto packets = payload.pack(data, pos);
                REQUIRE(packets.size() == 1);

                const uint8_t *pkt = packets[0].data();
                // Check RFC 2435 JPEG header at offset 12 (after RTP header)
                // Type-specific = 0
                CHECK(pkt[12] == 0);
                // Fragment offset = 0 for first packet
                CHECK(pkt[13] == 0);
                CHECK(pkt[14] == 0);
                CHECK(pkt[15] == 0);
                // Type = 0 (FFmpeg compat: 4:2:2)
                CHECK(pkt[16] == 0);
                // Quality = 255 (explicit tables)
                CHECK(pkt[17] == 255);
                // Width/8 = 80
                CHECK(pkt[18] == 80);
                // Height/8 = 60
                CHECK(pkt[19] == 60);

                // Quantization Table Header at offset 20
                CHECK(pkt[20] == 0); // MBZ
                CHECK(pkt[21] == 0); // Precision (0 = 8-bit)
                // Length = 64 (one 64-byte table, no Pq/Tq byte)
                uint16_t qtLen = (static_cast<uint16_t>(pkt[22]) << 8) | pkt[23];
                CHECK(qtLen == 64);
                // First byte of table data should be 1 (not 0x00 Pq/Tq)
                CHECK(pkt[24] == 1);
        }

        SUBCASE("unpack reassembles fragment data") {
                RtpPayloadJpeg payload(320, 240);
                auto           jpeg = buildMinimalJpeg(200);

                auto packets = payload.pack(jpeg.data(), jpeg.size());
                REQUIRE(packets.size() >= 1);

                Buffer result = payload.unpack(packets);
                // unpack should produce non-empty data
                CHECK(result.size() > 0);
        }

        SUBCASE("unpack multi-packet reassembly") {
                RtpPayloadJpeg payload(1920, 1080, 85);
                auto           jpeg = buildMinimalJpeg(5000);

                auto packets = payload.pack(jpeg.data(), jpeg.size());
                REQUIRE(packets.size() > 1);

                Buffer result = payload.unpack(packets);
                // Result should contain QT header + entropy data
                CHECK(result.size() > 5000);
        }

        SUBCASE("unpack empty packet list") {
                RtpPayloadJpeg  payload(320, 240);
                RtpPacket::List empty;
                Buffer          result = payload.unpack(empty);
                CHECK(result.size() == 0);
        }
}

// ============================================================================
// RtpPayloadJpegXs (RFC 9134)
// ============================================================================
//
// JPEG XS is an opaque byte stream at the RTP layer — pack() just
// splits it into MTU-sized fragments and prepends the 4-byte
// RFC 9134 header.  These tests don't care about real JPEG XS
// bitstreams; they use synthetic data with a unique byte at every
// position so the round-trip can verify order and completeness.

static std::vector<uint8_t> buildJxsBytes(size_t size) {
        std::vector<uint8_t> v(size);
        for (size_t i = 0; i < size; i++) {
                // Spread a full-range 256-period pattern so any
                // off-by-one in a fragment shows up visibly.
                v[i] = static_cast<uint8_t>(i & 0xFF);
        }
        return v;
}

// Mirror of the private writeJxsHeader in rtppayload.cpp — used by
// the bit-layout tests to decode the header back out of a packet.
// Keeping a local copy avoids exposing the helper in the public API.
static void decodeJxsHeader(const uint8_t *hdr, bool &T, bool &K, bool &L, uint8_t &I, uint8_t &F, uint16_t &SEP,
                            uint16_t &P) {
        T = ((hdr[0] >> 7) & 1) != 0;
        K = ((hdr[0] >> 6) & 1) != 0;
        L = ((hdr[0] >> 5) & 1) != 0;
        I = static_cast<uint8_t>((hdr[0] >> 3) & 0x03);
        F = static_cast<uint8_t>(((hdr[0] & 0x07) << 2) | ((hdr[1] >> 6) & 0x03));
        SEP = static_cast<uint16_t>(((hdr[1] & 0x3F) << 5) | ((hdr[2] >> 3) & 0x1F));
        P = static_cast<uint16_t>(((hdr[2] & 0x07) << 8) | hdr[3]);
}

TEST_CASE("RtpPayloadJpegXs") {

        SUBCASE("construction") {
                RtpPayloadJpegXs payload(1920, 1080);
                CHECK(payload.width() == 1920);
                CHECK(payload.height() == 1080);
                CHECK(payload.clockRate() == 90000);
                CHECK(payload.payloadType() == 96);
                CHECK(payload.frameCounter() == 0);
                CHECK(RtpPayloadJpegXs::HeaderSize == 4);
        }

        SUBCASE("custom payload type") {
                RtpPayloadJpegXs payload(1920, 1080, 112);
                CHECK(payload.payloadType() == 112);
                payload.setPayloadType(100);
                CHECK(payload.payloadType() == 100);
        }

        SUBCASE("pack empty / null data") {
                RtpPayloadJpegXs payload(640, 480);
                CHECK(payload.pack(nullptr, 0).isEmpty());
                CHECK(payload.pack(nullptr, 100).isEmpty());
                // Frame counter should NOT advance on a rejected call
                CHECK(payload.frameCounter() == 0);
        }

        SUBCASE("single-packet frame") {
                RtpPayloadJpegXs payload(320, 240);
                auto             data = buildJxsBytes(500); // comfortably under the 1200-byte MTU
                auto             packets = payload.pack(data.data(), data.size());
                REQUIRE(packets.size() == 1);
                // The one-and-only packet is the last packet → L=1
                const uint8_t *pl = packets[0].payload();
                bool           T, K, L;
                uint8_t        I, F;
                uint16_t       SEP, P;
                decodeJxsHeader(pl, T, K, L, I, F, SEP, P);
                CHECK(T == true);  // sequential
                CHECK(K == false); // codestream mode
                CHECK(L == true);  // last (and only) packet
                CHECK(I == 0);     // progressive
                CHECK(F == 0);     // first frame
                CHECK(SEP == 0);
                CHECK(P == 0); // first packet in frame
        }

        SUBCASE("multi-packet fragmentation") {
                RtpPayloadJpegXs payload(1920, 1080);
                auto             data = buildJxsBytes(5000); // forces ~5 packets at default 1200 MTU
                auto             packets = payload.pack(data.data(), data.size());
                REQUIRE(packets.size() > 1);

                // Shared buffer (same pattern as JPEG / raw paths).
                for (size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().impl().ptr() == packets[0].buffer().impl().ptr());
                }

                // Non-last packets must have L=0 and strictly-increasing
                // P counters starting at 0.  The last packet flips L=1.
                for (size_t i = 0; i < packets.size(); i++) {
                        const uint8_t *pl = packets[i].payload();
                        bool           T, K, L;
                        uint8_t        I, F;
                        uint16_t       SEP, P;
                        decodeJxsHeader(pl, T, K, L, I, F, SEP, P);
                        CHECK(T == true);
                        CHECK(K == false);
                        CHECK(I == 0);
                        CHECK(F == 0); // same frame across all packets
                        CHECK(P == (uint16_t)i);
                        CHECK(SEP == 0); // no P wrap at this size
                        if (i + 1 == packets.size()) {
                                CHECK(L == true);
                        } else {
                                CHECK(L == false);
                        }
                }
        }

        SUBCASE("frame counter advances across pack() calls") {
                RtpPayloadJpegXs payload(320, 240);
                auto             data = buildJxsBytes(500);

                for (int frame = 0; frame < 35; frame++) {
                        auto packets = payload.pack(data.data(), data.size());
                        REQUIRE(packets.size() >= 1);
                        const uint8_t *pl = packets[0].payload();
                        bool           T, K, L;
                        uint8_t        I, F;
                        uint16_t       SEP, P;
                        decodeJxsHeader(pl, T, K, L, I, F, SEP, P);
                        // F counter is mod 32, so frame 32 wraps back to 0.
                        CHECK(F == (uint8_t)(frame & 0x1F));
                }
                // After 35 calls the stored counter is (35 & 0x1F) = 3.
                CHECK(payload.frameCounter() == 3);
        }

        SUBCASE("resetFrameCounter clears state") {
                RtpPayloadJpegXs payload(320, 240);
                auto             data = buildJxsBytes(500);
                payload.pack(data.data(), data.size());
                payload.pack(data.data(), data.size());
                CHECK(payload.frameCounter() == 2);
                payload.resetFrameCounter();
                CHECK(payload.frameCounter() == 0);
        }

        SUBCASE("round-trip single frame") {
                RtpPayloadJpegXs payload(640, 480);
                auto             src = buildJxsBytes(900);
                auto             packets = payload.pack(src.data(), src.size());
                REQUIRE(!packets.isEmpty());

                Buffer out = payload.unpack(packets);
                REQUIRE(out.size() == src.size());
                CHECK(std::memcmp(out.data(), src.data(), src.size()) == 0);
        }

        SUBCASE("round-trip multi-packet frame") {
                RtpPayloadJpegXs payload(1920, 1080);
                auto             src = buildJxsBytes(12345); // ~11 packets at 1200 MTU
                auto             packets = payload.pack(src.data(), src.size());
                REQUIRE(packets.size() > 1);

                Buffer out = payload.unpack(packets);
                REQUIRE(out.size() == src.size());
                CHECK(std::memcmp(out.data(), src.data(), src.size()) == 0);
        }

        SUBCASE("unpack empty packet list") {
                RtpPayloadJpegXs payload(320, 240);
                RtpPacket::List  empty;
                Buffer           result = payload.unpack(empty);
                CHECK(result.size() == 0);
        }

        SUBCASE("custom MTU changes fragmentation") {
                // A very small MTU forces lots of small fragments; a
                // very large MTU collapses the frame to one packet.
                auto data = buildJxsBytes(1000);

                RtpPayloadJpegXs tiny(320, 240);
                tiny.setMaxPayloadSize(200); // 200 - 4 = 196 bytes/pkt → ~6 packets
                auto tinyPackets = tiny.pack(data.data(), data.size());
                CHECK(tinyPackets.size() > 1);

                RtpPayloadJpegXs jumbo(320, 240);
                jumbo.setMaxPayloadSize(8000); // one-shot
                auto jumboPackets = jumbo.pack(data.data(), data.size());
                CHECK(jumboPackets.size() == 1);
        }
}

TEST_CASE("RtpPayloadJson") {

        SUBCASE("construction defaults") {
                RtpPayloadJson payload;
                CHECK(payload.payloadType() == 98);
                CHECK(payload.clockRate() == 90000);
                CHECK(payload.maxPayloadSize() == 1200);
        }

        SUBCASE("custom payload type and clock rate") {
                RtpPayloadJson payload(100, 48000);
                CHECK(payload.payloadType() == 100);
                CHECK(payload.clockRate() == 48000);
        }

        SUBCASE("setPayloadType / setClockRate") {
                RtpPayloadJson payload;
                payload.setPayloadType(110);
                payload.setClockRate(44100);
                CHECK(payload.payloadType() == 110);
                CHECK(payload.clockRate() == 44100);
        }

        SUBCASE("pack empty data") {
                RtpPayloadJson payload;
                auto           packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("pack small JSON fits in one packet") {
                RtpPayloadJson payload;
                const char    *json = "{\"ts\":1234,\"fn\":42}";
                size_t         len = std::strlen(json);

                auto packets = payload.pack(json, len);
                REQUIRE(packets.size() == 1);
                CHECK(packets[0].payloadSize() == len);
                CHECK(std::memcmp(packets[0].payload(), json, len) == 0);
        }

        SUBCASE("pack large JSON fragments") {
                RtpPayloadJson payload;
                // 3500 bytes of synthetic JSON — exceeds the default
                // 1200-byte payload size so fragmentation kicks in.
                std::string bigJson = "{\"data\":\"";
                while (bigJson.size() < 3500) bigJson += "abcdefghij";
                bigJson += "\"}";

                auto packets = payload.pack(bigJson.data(), bigJson.size());
                REQUIRE(packets.size() > 1);

                // Every packet but the last should be full; the last
                // holds the remainder.
                size_t total = 0;
                for (const auto &pkt : packets) total += pkt.payloadSize();
                CHECK(total == bigJson.size());
        }

        SUBCASE("pack/unpack round-trip small") {
                RtpPayloadJson payload;
                const char    *json = "{\"timecode\":\"01:00:00:00\",\"dropFrame\":false}";
                size_t         len = std::strlen(json);

                auto   packets = payload.pack(json, len);
                Buffer out = payload.unpack(packets);
                CHECK(out.size() == len);
                CHECK(std::memcmp(out.data(), json, len) == 0);
        }

        SUBCASE("pack/unpack round-trip large") {
                RtpPayloadJson payload;
                std::string    bigJson;
                bigJson.reserve(5000);
                bigJson += "{\"lut\":[";
                for (int i = 0; i < 500; i++) {
                        if (i > 0) bigJson += ",";
                        bigJson += "0.";
                        bigJson += std::to_string(i);
                }
                bigJson += "]}";

                auto packets = payload.pack(bigJson.data(), bigJson.size());
                REQUIRE(packets.size() > 1);

                Buffer out = payload.unpack(packets);
                CHECK(out.size() == bigJson.size());
                CHECK(std::memcmp(out.data(), bigJson.data(), bigJson.size()) == 0);
        }

        SUBCASE("unpack empty packet list") {
                RtpPayloadJson  payload;
                RtpPacket::List empty;
                Buffer          result = payload.unpack(empty);
                CHECK(result.size() == 0);
        }

        SUBCASE("packets share a single backing buffer") {
                RtpPayloadJson payload;
                std::string    bigJson(3000, 'x');
                auto           packets = payload.pack(bigJson.data(), bigJson.size());
                REQUIRE(packets.size() > 1);
                // All packets allocated in one shared Buffer — the
                // first packet's data pointer is the base, subsequent
                // ones point into the same arena at growing offsets.
                for (size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].data() > packets[0].data());
                }
        }
}
