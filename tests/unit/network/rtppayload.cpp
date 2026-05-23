/**
 * @file      rtppayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/base64.h>
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

// RtpPayloadRawVideo tests live in their own file (rtppayloadrawvideo.cpp)
// so the multi-SRD / 32-bit-extseq / F-bit coverage stays adjacent to
// the standalone wire-format class.

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

        SUBCASE("4:2:2 JPEG sets Type=0 (FFmpeg convention)") {
                // The minimal JPEG built by buildMinimalJpeg has no SOF0
                // marker, so jpegIs420() returns false → Type byte must be
                // 0 (FFmpeg convention for 4:2:2).
                RtpPayloadJpeg payload(320, 240);
                auto           jpeg = buildMinimalJpeg(100);
                auto           packets = payload.pack(jpeg.data(), jpeg.size());
                REQUIRE(packets.size() >= 1);
                // RTP/JPEG header starts at byte 12 (after 12-byte RTP header).
                // Type is at offset 4 within the JPEG header, i.e. byte 16.
                CHECK(packets[0].data()[16] == 0);
        }

        SUBCASE("4:2:0 JPEG (SOF0 Y sampling=0x22) sets Type=1 (FFmpeg convention)") {
                // Build a JPEG that has an SOF0 marker with Y sampling
                // factor = 0x22 (h=2, v=2) so jpegIs420() returns true.
                // RFC 2435 / FFmpeg convention: 4:2:0 → Type = 1.
                //
                // SOF0 layout (JFIF §B.2.2):
                //   FF C0 | Lf(2B) | P(1B) | Y(2B) | X(2B) | Nf(1B)
                //   per-component: Ci(1B) Hi/Vi(1B) Tqi(1B)
                // We only need the first component's Hi/Vi at byte 11
                // (0-indexed from the marker's first byte, inclusive).
                std::vector<uint8_t> jpeg;
                // SOI
                jpeg.push_back(0xFF); jpeg.push_back(0xD8);
                // SOF0: FF C0, length=17 (2+6+3*3), P=8, H=240, W=320, Nf=3
                jpeg.push_back(0xFF); jpeg.push_back(0xC0);
                jpeg.push_back(0x00); jpeg.push_back(0x11); // Lf = 17
                jpeg.push_back(0x08);                       // P = 8 bits
                jpeg.push_back(0x00); jpeg.push_back(0xF0); // Y = 240
                jpeg.push_back(0x01); jpeg.push_back(0x40); // X = 320
                jpeg.push_back(0x03);                       // Nf = 3 components
                // Component 1 (Y): Ci=1, Hi=2/Vi=2 (=0x22 → 4:2:0), Tq=0
                jpeg.push_back(0x01); jpeg.push_back(0x22); jpeg.push_back(0x00);
                // Component 2 (Cb): Ci=2, Hi=1/Vi=1 (=0x11), Tq=1
                jpeg.push_back(0x02); jpeg.push_back(0x11); jpeg.push_back(0x01);
                // Component 3 (Cr): same
                jpeg.push_back(0x03); jpeg.push_back(0x11); jpeg.push_back(0x01);
                // DQT marker
                jpeg.push_back(0xFF); jpeg.push_back(0xDB);
                jpeg.push_back(0x00); jpeg.push_back(0x43); // length = 67
                jpeg.push_back(0x00);                       // Pq/Tq
                for (int i = 0; i < 64; i++) jpeg.push_back(static_cast<uint8_t>(i + 1));
                // SOS marker (minimal 1-component)
                jpeg.push_back(0xFF); jpeg.push_back(0xDA);
                jpeg.push_back(0x00); jpeg.push_back(0x08);
                jpeg.push_back(0x01); // Ns=1
                jpeg.push_back(0x01); jpeg.push_back(0x00);
                jpeg.push_back(0x00); jpeg.push_back(0x3F); jpeg.push_back(0x00);
                // Entropy-coded data
                for (int i = 0; i < 50; i++) jpeg.push_back(static_cast<uint8_t>((i & 0x7F) + 1));

                RtpPayloadJpeg payload(320, 240);
                auto           packets = payload.pack(jpeg.data(), jpeg.size());
                REQUIRE(packets.size() >= 1);
                // Type byte is at offset 16 in the packet (byte 4 of JPEG header).
                CHECK(packets[0].data()[16] == 1);
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

        SUBCASE("setSliceMode default is off (K=0)") {
                RtpPayloadJpegXs payload(320, 240);
                CHECK_FALSE(payload.isSliceMode());
                payload.setSliceMode(true);
                CHECK(payload.isSliceMode());
                payload.setSliceMode(false);
                CHECK_FALSE(payload.isSliceMode());
        }

        SUBCASE("slice mode falls back to codestream mode when input has no SLH markers") {
                // buildJxsBytes() produces a raw byte stream with no
                // JPEG XS marker structure; slice-mode packing
                // returns empty, and the pack() dispatcher falls
                // back to codestream mode so the caller still gets
                // wire bytes.
                RtpPayloadJpegXs payload(320, 240);
                payload.setSliceMode(true);
                auto data = buildJxsBytes(500);
                auto packets = payload.pack(data.data(), data.size());
                REQUIRE(!packets.isEmpty());
                // Decode the K bit of the first packet's header
                // (byte 1 after the RTP header).  K=0 confirms the
                // fallback to codestream mode.
                const uint8_t kBit = (packets[0].payload()[0] >> 6) & 0x01;
                CHECK(kBit == 0);
        }

        SUBCASE("slice mode produces K=1 packets on a well-formed codestream") {
                // Build a synthetic 3-slice codestream mirroring the
                // jxsmarker.cpp fixture: SOC + CAP(8) + PIH(16) +
                // 3×(SLH(4) + 100 bytes coeffs) + EOC.
                std::vector<uint8_t> bytes;
                auto                 pushMarker = [&](uint16_t code) {
                        bytes.push_back(static_cast<uint8_t>(code >> 8));
                        bytes.push_back(static_cast<uint8_t>(code & 0xFF));
                };
                auto pushBe16 = [&](uint16_t v) {
                        bytes.push_back(static_cast<uint8_t>(v >> 8));
                        bytes.push_back(static_cast<uint8_t>(v & 0xFF));
                };

                pushMarker(0xFF10); // SOC
                pushMarker(0xFF50); // CAP
                pushBe16(10);       // len = 2 (self) + 8 payload
                for (int i = 0; i < 8; i++) bytes.push_back(static_cast<uint8_t>(0x20 + i));
                pushMarker(0xFF12); // PIH
                pushBe16(18);       // len = 2 + 16
                for (int i = 0; i < 16; i++) bytes.push_back(static_cast<uint8_t>(0x40 + i));
                for (int s = 0; s < 3; s++) {
                        pushMarker(0xFF20); // SLH
                        pushBe16(6);        // len = 2 + 4
                        for (int i = 0; i < 4; i++)
                                bytes.push_back(static_cast<uint8_t>(s));
                        for (int i = 0; i < 100; i++)
                                bytes.push_back(static_cast<uint8_t>((s * 11 + i) & 0x7F));
                }
                pushMarker(0xFF11); // EOC

                RtpPayloadJpegXs payload(320, 240);
                payload.setSliceMode(true);
                auto packets = payload.pack(bytes.data(), bytes.size());
                REQUIRE(!packets.isEmpty());
                // First packet should carry K=1.  The K bit lives at
                // bit 6 of the first JXS header byte (byte 0 of the
                // RTP payload).
                const uint8_t kBit = (packets[0].payload()[0] >> 6) & 0x01;
                CHECK(kBit == 1);
                // The L bit (last-of-frame) on the final packet
                // should be set.
                const uint8_t lBitLast = (packets[packets.size() - 1].payload()[0] >> 5) & 0x01;
                CHECK(lBitLast == 1);
                // Earlier packets should have L=0.
                for (size_t i = 0; i + 1 < packets.size(); i++) {
                        const uint8_t lBit = (packets[i].payload()[0] >> 5) & 0x01;
                        CHECK(lBit == 0);
                }
        }

        SUBCASE("slice mode falls back when a single slice exceeds the MTU") {
                // One huge slice (~3000 bytes coeffs).  Default MTU
                // is 1200; one slice can't fit so the packetizer
                // falls back to codestream mode (K=0).
                std::vector<uint8_t> bytes;
                auto                 pushMarker = [&](uint16_t code) {
                        bytes.push_back(static_cast<uint8_t>(code >> 8));
                        bytes.push_back(static_cast<uint8_t>(code & 0xFF));
                };
                auto pushBe16 = [&](uint16_t v) {
                        bytes.push_back(static_cast<uint8_t>(v >> 8));
                        bytes.push_back(static_cast<uint8_t>(v & 0xFF));
                };
                pushMarker(0xFF10);
                pushMarker(0xFF12);
                pushBe16(4); // empty PIH
                pushMarker(0xFF20);
                pushBe16(4); // empty SLH
                for (int i = 0; i < 3000; i++) bytes.push_back(static_cast<uint8_t>(i & 0x7F));
                pushMarker(0xFF11);

                RtpPayloadJpegXs payload(320, 240);
                payload.setSliceMode(true);
                auto packets = payload.pack(bytes.data(), bytes.size());
                REQUIRE(!packets.isEmpty());
                // Fallback path: K=0 on the first packet.
                const uint8_t kBit = (packets[0].payload()[0] >> 6) & 0x01;
                CHECK(kBit == 0);
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

// ============================================================================
// RtpPayloadH264 / RtpPayloadH265 helpers
// ============================================================================

// Build an Annex-B access unit from a list of NAL payloads.  Each NAL
// is preceded by a 4-byte start code.  Used by both H.264 and H.265
// tests since the framing layer is shared.
static std::vector<uint8_t> buildAnnexB(const std::vector<std::vector<uint8_t>> &nals) {
        std::vector<uint8_t> out;
        for (const auto &nal : nals) {
                out.push_back(0x00);
                out.push_back(0x00);
                out.push_back(0x00);
                out.push_back(0x01);
                for (uint8_t b : nal) out.push_back(b);
        }
        return out;
}

// Walk a reassembled Annex-B byte stream and split it back into NAL
// payloads (no start codes).  Used by tests to compare round-trip
// output without depending on the specific start-code length the
// implementation emits.
static std::vector<std::vector<uint8_t>> splitAnnexB(const uint8_t *data, size_t size) {
        std::vector<std::vector<uint8_t>> out;
        size_t                            i = 0;
        // Skip leading bytes until first start code.
        while (i + 3 < size &&
               !(data[i] == 0x00 && data[i + 1] == 0x00 &&
                 (data[i + 2] == 0x01 || (data[i + 2] == 0x00 && data[i + 3] == 0x01)))) {
                i++;
        }
        while (i < size) {
                // Skip start code.
                size_t scLen = 0;
                if (i + 3 < size && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                        scLen = 4;
                } else if (i + 2 < size && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
                        scLen = 3;
                } else {
                        break;
                }
                i += scLen;
                size_t start = i;
                while (i + 2 < size) {
                        if (data[i] == 0x00 && data[i + 1] == 0x00 &&
                            (data[i + 2] == 0x01 ||
                             (data[i + 2] == 0x00 && i + 3 < size && data[i + 3] == 0x01))) {
                                break;
                        }
                        i++;
                }
                size_t end = (i + 2 < size) ? i : size;
                if (end > start) {
                        out.emplace_back(data + start, data + end);
                }
                if (i + 2 >= size) break;
        }
        return out;
}

TEST_CASE("RtpPayloadH264") {

        SUBCASE("construction defaults") {
                RtpPayloadH264 payload;
                CHECK(payload.payloadType() == 96);
                CHECK(payload.clockRate() == 90000);
                CHECK(payload.maxPayloadSize() == 1200);
        }

        SUBCASE("setPayloadType") {
                RtpPayloadH264 payload;
                payload.setPayloadType(100);
                CHECK(payload.payloadType() == 100);
        }

        SUBCASE("pack empty data") {
                RtpPayloadH264 payload;
                auto           packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("single small NAL becomes one packet") {
                RtpPayloadH264 payload;
                // SPS NAL (type 7, NRI 3) — short, fits in one packet.
                std::vector<uint8_t> sps{0x67, 0x42, 0x00, 0x1f, 0x96, 0x54};
                auto                 input = buildAnnexB({sps});
                auto                 packets = payload.pack(input.data(), input.size());
                REQUIRE(packets.size() == 1);
                CHECK(packets[0].payloadSize() == sps.size());
                CHECK(std::memcmp(packets[0].payload(), sps.data(), sps.size()) == 0);
        }

        SUBCASE("multiple small NALs each get their own packet") {
                RtpPayloadH264 payload;
                std::vector<uint8_t> sps{0x67, 0x42, 0x00, 0x1f, 0x96, 0x54};
                std::vector<uint8_t> pps{0x68, 0xce, 0x06, 0xe2};
                std::vector<uint8_t> idr;
                idr.push_back(0x65);                     // IDR slice header
                for (int i = 0; i < 50; i++) idr.push_back(static_cast<uint8_t>(i + 1));
                auto input = buildAnnexB({sps, pps, idr});
                auto packets = payload.pack(input.data(), input.size());
                REQUIRE(packets.size() == 3);
                CHECK(packets[0].payloadSize() == sps.size());
                CHECK(packets[1].payloadSize() == pps.size());
                CHECK(packets[2].payloadSize() == idr.size());
        }

        SUBCASE("oversize NAL gets fragmented as FU-A") {
                RtpPayloadH264 payload;
                payload.setMaxPayloadSize(100); // tight MTU forces FU-A
                // Build a NAL larger than 100 bytes; first byte is the
                // H.264 NAL header (type 5 = IDR slice, NRI = 3).
                std::vector<uint8_t> idr;
                idr.push_back(0x65);
                for (int i = 0; i < 250; i++) idr.push_back(static_cast<uint8_t>((i * 7 + 3) & 0xFF));
                auto input = buildAnnexB({idr});
                auto packets = payload.pack(input.data(), input.size());
                REQUIRE(packets.size() >= 3);

                for (size_t i = 0; i < packets.size(); i++) {
                        const uint8_t *pl = packets[i].payload();
                        REQUIRE(pl != nullptr);
                        // Every packet has FU indicator type = 28
                        CHECK((pl[0] & 0x1F) == 28);
                        // F | NRI bits should match the source NAL header.
                        CHECK((pl[0] & 0xE0) == 0x60);
                        const uint8_t fuHdr = pl[1];
                        CHECK((fuHdr & 0x1F) == 5); // original NAL type carried in FU header
                        const bool s = (fuHdr & 0x80) != 0;
                        const bool e = (fuHdr & 0x40) != 0;
                        if (i == 0) {
                                CHECK(s);
                                CHECK_FALSE(e);
                        } else if (i == packets.size() - 1) {
                                CHECK_FALSE(s);
                                CHECK(e);
                        } else {
                                CHECK_FALSE(s);
                                CHECK_FALSE(e);
                        }
                }
        }

        SUBCASE("round-trip: SPS + PPS + small IDR") {
                RtpPayloadH264       payload;
                std::vector<uint8_t> sps{0x67, 0x42, 0x00, 0x1f, 0x96, 0x54};
                std::vector<uint8_t> pps{0x68, 0xce, 0x06, 0xe2};
                std::vector<uint8_t> idr{0x65, 0x88, 0x84, 0x00, 0x10, 0xff, 0x00, 0x12};
                auto                 input = buildAnnexB({sps, pps, idr});

                auto   packets = payload.pack(input.data(), input.size());
                Buffer out = payload.unpack(packets);
                auto   reNals = splitAnnexB(static_cast<const uint8_t *>(out.data()), out.size());
                REQUIRE(reNals.size() == 3);
                CHECK(reNals[0] == sps);
                CHECK(reNals[1] == pps);
                CHECK(reNals[2] == idr);
        }

        SUBCASE("round-trip: oversize NAL via FU-A") {
                RtpPayloadH264 payload;
                payload.setMaxPayloadSize(200);
                std::vector<uint8_t> idr;
                idr.push_back(0x65);
                for (int i = 0; i < 1000; i++) idr.push_back(static_cast<uint8_t>((i * 13 + 7) & 0xFF));
                auto input = buildAnnexB({idr});

                auto   packets = payload.pack(input.data(), input.size());
                REQUIRE(packets.size() >= 5);
                Buffer out = payload.unpack(packets);
                auto   reNals = splitAnnexB(static_cast<const uint8_t *>(out.data()), out.size());
                REQUIRE(reNals.size() == 1);
                CHECK(reNals[0] == idr);
        }

        SUBCASE("round-trip: mixed single + FU-A NALs") {
                RtpPayloadH264 payload;
                payload.setMaxPayloadSize(150);
                std::vector<uint8_t> sps{0x67, 0x42, 0x00, 0x1f, 0x96, 0x54};
                std::vector<uint8_t> pps{0x68, 0xce, 0x06, 0xe2};
                std::vector<uint8_t> idr;
                idr.push_back(0x65);
                for (int i = 0; i < 600; i++) idr.push_back(static_cast<uint8_t>((i * 11 + 5) & 0xFF));
                auto input = buildAnnexB({sps, pps, idr});

                auto   packets = payload.pack(input.data(), input.size());
                Buffer out = payload.unpack(packets);
                auto   reNals = splitAnnexB(static_cast<const uint8_t *>(out.data()), out.size());
                REQUIRE(reNals.size() == 3);
                CHECK(reNals[0] == sps);
                CHECK(reNals[1] == pps);
                CHECK(reNals[2] == idr);
        }

        SUBCASE("unpack handles STAP-A aggregation") {
                // Hand-crafted STAP-A: type 24, NRI 3, then two
                // length-prefixed NALs.
                std::vector<uint8_t> sps{0x67, 0x42, 0x00, 0x1f, 0x96, 0x54};
                std::vector<uint8_t> pps{0x68, 0xce, 0x06, 0xe2};
                std::vector<uint8_t> stap;
                stap.push_back(0x78); // F=0, NRI=3, type=24 (STAP-A)
                stap.push_back(static_cast<uint8_t>(sps.size() >> 8));
                stap.push_back(static_cast<uint8_t>(sps.size() & 0xFF));
                for (auto b : sps) stap.push_back(b);
                stap.push_back(static_cast<uint8_t>(pps.size() >> 8));
                stap.push_back(static_cast<uint8_t>(pps.size() & 0xFF));
                for (auto b : pps) stap.push_back(b);

                RtpPacket pkt(RtpPacket::HeaderSize + stap.size());
                pkt.setPayloadType(96);
                std::memcpy(pkt.payload(), stap.data(), stap.size());
                RtpPacket::List list;
                list.pushToBack(pkt);

                RtpPayloadH264 payload;
                Buffer         out = payload.unpack(list);
                auto           reNals = splitAnnexB(static_cast<const uint8_t *>(out.data()), out.size());
                REQUIRE(reNals.size() == 2);
                CHECK(reNals[0] == sps);
                CHECK(reNals[1] == pps);
        }

        SUBCASE("packets share a single backing buffer") {
                RtpPayloadH264 payload;
                payload.setMaxPayloadSize(200);
                std::vector<uint8_t> idr;
                idr.push_back(0x65);
                for (int i = 0; i < 1500; i++) idr.push_back(static_cast<uint8_t>(i & 0xFF));
                auto input = buildAnnexB({idr});
                auto packets = payload.pack(input.data(), input.size());
                REQUIRE(packets.size() > 1);
                for (size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().impl().ptr() == packets[0].buffer().impl().ptr());
                }
        }
}

TEST_CASE("RtpPayloadH265") {

        SUBCASE("construction defaults") {
                RtpPayloadH265 payload;
                CHECK(payload.payloadType() == 96);
                CHECK(payload.clockRate() == 90000);
                CHECK(payload.maxPayloadSize() == 1200);
        }

        SUBCASE("single small NAL becomes one packet") {
                RtpPayloadH265 payload;
                // VPS NAL: type 32, layerId=0, TID=1 → byte0 = 0x40, byte1 = 0x01.
                std::vector<uint8_t> vps{0x40, 0x01, 0x0c, 0x01, 0xff, 0xff};
                auto                 input = buildAnnexB({vps});
                auto                 packets = payload.pack(input.data(), input.size());
                REQUIRE(packets.size() == 1);
                CHECK(packets[0].payloadSize() == vps.size());
                CHECK(std::memcmp(packets[0].payload(), vps.data(), vps.size()) == 0);
        }

        SUBCASE("oversize NAL gets fragmented as FU") {
                RtpPayloadH265 payload;
                payload.setMaxPayloadSize(100);
                // IDR_W_RADL: type 19, layerId=0, TID=1 → byte0 = (19<<1)|0 = 0x26, byte1 = 0x01
                std::vector<uint8_t> idr;
                idr.push_back(0x26);
                idr.push_back(0x01);
                for (int i = 0; i < 250; i++) idr.push_back(static_cast<uint8_t>((i * 5 + 11) & 0xFF));
                auto input = buildAnnexB({idr});
                auto packets = payload.pack(input.data(), input.size());
                REQUIRE(packets.size() >= 3);

                for (size_t i = 0; i < packets.size(); i++) {
                        const uint8_t *pl = packets[i].payload();
                        REQUIRE(pl != nullptr);
                        // type = 49 (FU)
                        const uint8_t type = (pl[0] >> 1) & 0x3F;
                        CHECK(type == 49);
                        // PHdr1 should match source NAL byte 1 (layerId/TID).
                        CHECK(pl[1] == 0x01);
                        const uint8_t fuHdr = pl[2];
                        CHECK((fuHdr & 0x3F) == 19); // original type
                        const bool s = (fuHdr & 0x80) != 0;
                        const bool e = (fuHdr & 0x40) != 0;
                        if (i == 0) {
                                CHECK(s);
                                CHECK_FALSE(e);
                        } else if (i == packets.size() - 1) {
                                CHECK_FALSE(s);
                                CHECK(e);
                        } else {
                                CHECK_FALSE(s);
                                CHECK_FALSE(e);
                        }
                }
        }

        SUBCASE("round-trip: VPS + SPS + PPS + small IDR") {
                RtpPayloadH265       payload;
                std::vector<uint8_t> vps{0x40, 0x01, 0x0c, 0x01, 0xff, 0xff};
                std::vector<uint8_t> sps{0x42, 0x01, 0x01, 0x01, 0x60, 0x00};
                std::vector<uint8_t> pps{0x44, 0x01, 0xc1, 0x73};
                std::vector<uint8_t> idr{0x26, 0x01, 0xaf, 0x01, 0x07, 0xa6, 0x42, 0x9c};
                auto                 input = buildAnnexB({vps, sps, pps, idr});

                auto   packets = payload.pack(input.data(), input.size());
                Buffer out = payload.unpack(packets);
                auto   reNals = splitAnnexB(static_cast<const uint8_t *>(out.data()), out.size());
                REQUIRE(reNals.size() == 4);
                CHECK(reNals[0] == vps);
                CHECK(reNals[1] == sps);
                CHECK(reNals[2] == pps);
                CHECK(reNals[3] == idr);
        }

        SUBCASE("round-trip: oversize NAL via FU") {
                RtpPayloadH265 payload;
                payload.setMaxPayloadSize(200);
                std::vector<uint8_t> idr;
                idr.push_back(0x26);
                idr.push_back(0x01);
                for (int i = 0; i < 1000; i++) idr.push_back(static_cast<uint8_t>((i * 17 + 9) & 0xFF));
                auto input = buildAnnexB({idr});

                auto   packets = payload.pack(input.data(), input.size());
                REQUIRE(packets.size() >= 5);
                Buffer out = payload.unpack(packets);
                auto   reNals = splitAnnexB(static_cast<const uint8_t *>(out.data()), out.size());
                REQUIRE(reNals.size() == 1);
                CHECK(reNals[0] == idr);
        }

        SUBCASE("unpack handles AP aggregation") {
                std::vector<uint8_t> vps{0x40, 0x01, 0x0c, 0x01};
                std::vector<uint8_t> sps{0x42, 0x01, 0x01, 0x01, 0x60};
                // AP NAL header: type=48, layerId=0, TID=1
                // byte0 = (48<<1)|0 = 0x60, byte1 = 0x01
                std::vector<uint8_t> ap;
                ap.push_back(0x60);
                ap.push_back(0x01);
                ap.push_back(static_cast<uint8_t>(vps.size() >> 8));
                ap.push_back(static_cast<uint8_t>(vps.size() & 0xFF));
                for (auto b : vps) ap.push_back(b);
                ap.push_back(static_cast<uint8_t>(sps.size() >> 8));
                ap.push_back(static_cast<uint8_t>(sps.size() & 0xFF));
                for (auto b : sps) ap.push_back(b);

                RtpPacket pkt(RtpPacket::HeaderSize + ap.size());
                pkt.setPayloadType(96);
                std::memcpy(pkt.payload(), ap.data(), ap.size());
                RtpPacket::List list;
                list.pushToBack(pkt);

                RtpPayloadH265 payload;
                Buffer         out = payload.unpack(list);
                auto           reNals = splitAnnexB(static_cast<const uint8_t *>(out.data()), out.size());
                REQUIRE(reNals.size() == 2);
                CHECK(reNals[0] == vps);
                CHECK(reNals[1] == sps);
        }
}

// ============================================================================
// Validate-gate state machines (Phase 4 — codec-aware mid-stream-join)
// ============================================================================

namespace {

        // Pack a vector of NAL bytes into a Buffer suitable for
        // passing to validate().  The validate() implementation
        // walks Annex-B start codes, so we build that framing
        // here.
        Buffer makeAuBuffer(const std::vector<std::vector<uint8_t>> &nals) {
                std::vector<uint8_t> bytes = buildAnnexB(nals);
                Buffer               buf(bytes.size());
                buf.setSize(bytes.size());
                if (!bytes.empty()) std::memcpy(buf.data(), bytes.data(), bytes.size());
                return buf;
        }

        // H.264 NAL type bytes: F=0, NRI=3 → high bits 0x60.
        std::vector<uint8_t> h264NalSps() { return {0x67, 0x42, 0x00, 0x1f, 0x96, 0x54}; }
        std::vector<uint8_t> h264NalPps() { return {0x68, 0xce, 0x06, 0xe2}; }
        std::vector<uint8_t> h264NalIdr() { return {0x65, 0x88, 0x84, 0x00, 0x10}; }
        std::vector<uint8_t> h264NalNonIdr() { return {0x61, 0x9a, 0x12, 0x00}; }

        // H.265 NAL bytes: byte0 = (type<<1) | layerIdHi, byte1 = layerIdLo|TID.
        // For type=N and layerId=0 / TID=1: byte0 = (N<<1), byte1 = 0x01.
        std::vector<uint8_t> h265NalVps() { return {0x40, 0x01, 0x0c, 0x01, 0xff, 0xff}; }
        std::vector<uint8_t> h265NalSps() { return {0x42, 0x01, 0x01, 0x01, 0x60, 0x00}; }
        std::vector<uint8_t> h265NalPps() { return {0x44, 0x01, 0xc1, 0x73}; }
        // IDR_W_RADL = type 19 → byte0 = 0x26.
        std::vector<uint8_t> h265NalIdr() { return {0x26, 0x01, 0xaf, 0x01, 0x07, 0xa6}; }
        // CRA = type 21 → byte0 = 0x2a.  Different IRAP variant.
        std::vector<uint8_t> h265NalCra() { return {0x2a, 0x01, 0xaf, 0x01, 0x07}; }
        // TRAIL_R = type 1 → byte0 = 0x02.  Non-IRAP, non-paramSet.
        std::vector<uint8_t> h265NalTrail() { return {0x02, 0x01, 0xff, 0x00}; }

} // namespace

TEST_CASE("RtpPayloadH264::validate") {
        SUBCASE("empty buffer drops silently") {
                RtpPayloadH264 p;
                Buffer         empty;
                CHECK(p.validate(empty) == RtpPayload::ValidateResult::DropSilently);
        }

        SUBCASE("paramSets missing → Wait, then catch up in-band") {
                RtpPayloadH264 p;
                CHECK_FALSE(p.spsObserved());
                CHECK_FALSE(p.ppsObserved());

                // No SPS / PPS yet — first AU is a non-IDR slice.
                Buffer au0 = makeAuBuffer({h264NalNonIdr()});
                CHECK(p.validate(au0) == RtpPayload::ValidateResult::Wait);

                // SPS arrives in-band — still missing PPS, still Wait.
                Buffer au1 = makeAuBuffer({h264NalSps()});
                CHECK(p.validate(au1) == RtpPayload::ValidateResult::Wait);
                CHECK(p.spsObserved());
                CHECK_FALSE(p.ppsObserved());

                // PPS observed — paramSets complete, but no IDR yet.
                Buffer au2 = makeAuBuffer({h264NalPps()});
                CHECK(p.validate(au2) == RtpPayload::ValidateResult::DropSilently);
                CHECK(p.ppsObserved());
                CHECK_FALSE(p.idrLatched());

                // First IDR — flips Accept and latches.
                Buffer au3 = makeAuBuffer({h264NalIdr()});
                CHECK(p.validate(au3) == RtpPayload::ValidateResult::Accept);
                CHECK(p.idrLatched());

                // Subsequent non-IDR AUs accepted.
                Buffer au4 = makeAuBuffer({h264NalNonIdr()});
                CHECK(p.validate(au4) == RtpPayload::ValidateResult::Accept);
        }

        SUBCASE("paramSets+IDR in same AU → Accept on first call") {
                RtpPayloadH264 p;
                Buffer         au = makeAuBuffer({h264NalSps(), h264NalPps(), h264NalIdr()});
                CHECK(p.validate(au) == RtpPayload::ValidateResult::Accept);
                CHECK(p.idrLatched());
        }

        SUBCASE("seeded paramSets via setSpropParameterSets") {
                RtpPayloadH264 p;
                // Build the canonical RFC 6184 sprop-parameter-sets
                // value: base64(SPS) "," base64(PPS).
                auto                 spsNal = h264NalSps();
                auto                 ppsNal = h264NalPps();
                String               sprop = Base64::encode(spsNal.data(), spsNal.size()) + String(",") +
                               Base64::encode(ppsNal.data(), ppsNal.size());
                p.setSpropParameterSets(sprop);
                CHECK(p.spsObserved());
                CHECK(p.ppsObserved());

                // No in-band IDR yet — pre-IDR AU drops silently.
                Buffer pre = makeAuBuffer({h264NalNonIdr()});
                CHECK(p.validate(pre) == RtpPayload::ValidateResult::DropSilently);

                // First IDR gates open.
                Buffer idr = makeAuBuffer({h264NalIdr()});
                CHECK(p.validate(idr) == RtpPayload::ValidateResult::Accept);
        }

        SUBCASE("malformed sprop ignored gracefully") {
                RtpPayloadH264 p;
                p.setSpropParameterSets(String("***not-base64***"));
                CHECK_FALSE(p.spsObserved());
                CHECK_FALSE(p.ppsObserved());
                p.setSpropParameterSets(String());
                CHECK_FALSE(p.spsObserved());
        }

        SUBCASE("clearParamSets re-arms latch on SSRC reset") {
                RtpPayloadH264 p;
                Buffer         au = makeAuBuffer({h264NalSps(), h264NalPps(), h264NalIdr()});
                CHECK(p.validate(au) == RtpPayload::ValidateResult::Accept);
                CHECK(p.idrLatched());

                p.clearParamSets();
                CHECK_FALSE(p.spsObserved());
                CHECK_FALSE(p.ppsObserved());
                CHECK_FALSE(p.idrLatched());

                // After reset, an immediate non-IDR AU returns Wait
                // (no paramSets yet) — same gate as a fresh open.
                Buffer post = makeAuBuffer({h264NalNonIdr()});
                CHECK(p.validate(post) == RtpPayload::ValidateResult::Wait);
        }
}

TEST_CASE("RtpPayloadH265::validate") {
        SUBCASE("empty buffer drops silently") {
                RtpPayloadH265 p;
                Buffer         empty;
                CHECK(p.validate(empty) == RtpPayload::ValidateResult::DropSilently);
        }

        SUBCASE("paramSets missing → Wait, in-band catch-up") {
                RtpPayloadH265 p;
                Buffer         pre = makeAuBuffer({h265NalTrail()});
                CHECK(p.validate(pre) == RtpPayload::ValidateResult::Wait);

                // VPS arrives — SPS / PPS still missing.
                Buffer vpsOnly = makeAuBuffer({h265NalVps()});
                CHECK(p.validate(vpsOnly) == RtpPayload::ValidateResult::Wait);
                CHECK(p.vpsObserved());

                Buffer spsOnly = makeAuBuffer({h265NalSps()});
                CHECK(p.validate(spsOnly) == RtpPayload::ValidateResult::Wait);
                CHECK(p.spsObserved());

                Buffer ppsOnly = makeAuBuffer({h265NalPps()});
                // All paramSets in — but no IRAP yet.
                CHECK(p.validate(ppsOnly) == RtpPayload::ValidateResult::DropSilently);
                CHECK(p.ppsObserved());
                CHECK_FALSE(p.irapLatched());

                Buffer idr = makeAuBuffer({h265NalIdr()});
                CHECK(p.validate(idr) == RtpPayload::ValidateResult::Accept);
                CHECK(p.irapLatched());

                Buffer trail = makeAuBuffer({h265NalTrail()});
                CHECK(p.validate(trail) == RtpPayload::ValidateResult::Accept);
        }

        SUBCASE("CRA also opens the IRAP gate") {
                RtpPayloadH265 p;
                Buffer         au = makeAuBuffer({h265NalVps(), h265NalSps(), h265NalPps(), h265NalCra()});
                CHECK(p.validate(au) == RtpPayload::ValidateResult::Accept);
                CHECK(p.irapLatched());
        }

        SUBCASE("seeded sprops via setSpropVps/Sps/Pps") {
                RtpPayloadH265 p;
                auto           vps = h265NalVps();
                auto           sps = h265NalSps();
                auto           pps = h265NalPps();
                p.setSpropVps(Base64::encode(vps.data(), vps.size()));
                p.setSpropSps(Base64::encode(sps.data(), sps.size()));
                p.setSpropPps(Base64::encode(pps.data(), pps.size()));
                CHECK(p.vpsObserved());
                CHECK(p.spsObserved());
                CHECK(p.ppsObserved());

                Buffer pre = makeAuBuffer({h265NalTrail()});
                CHECK(p.validate(pre) == RtpPayload::ValidateResult::DropSilently);

                Buffer idr = makeAuBuffer({h265NalIdr()});
                CHECK(p.validate(idr) == RtpPayload::ValidateResult::Accept);
        }

        SUBCASE("setSprop ignores type-mismatched payloads") {
                RtpPayloadH265 p;
                // Feed a SPS into setSpropVps — type mismatch, no flag set.
                auto           sps = h265NalSps();
                p.setSpropVps(Base64::encode(sps.data(), sps.size()));
                CHECK_FALSE(p.vpsObserved());
                CHECK_FALSE(p.spsObserved()); // setSpropVps only flips _vpsObserved
        }

        SUBCASE("clearParamSets re-arms HEVC latch") {
                RtpPayloadH265 p;
                Buffer         au = makeAuBuffer({h265NalVps(), h265NalSps(), h265NalPps(), h265NalIdr()});
                CHECK(p.validate(au) == RtpPayload::ValidateResult::Accept);
                CHECK(p.irapLatched());

                p.clearParamSets();
                CHECK_FALSE(p.vpsObserved());
                CHECK_FALSE(p.spsObserved());
                CHECK_FALSE(p.ppsObserved());
                CHECK_FALSE(p.irapLatched());

                Buffer post = makeAuBuffer({h265NalTrail()});
                CHECK(p.validate(post) == RtpPayload::ValidateResult::Wait);
        }
}
