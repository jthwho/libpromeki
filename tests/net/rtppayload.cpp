/**
 * @file      tests/net/rtppayload.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/network/rtppayload.h>
#include <cstring>
#include <cstdlib>

using namespace promeki;

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
                auto packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("pack small audio") {
                RtpPayloadL24 payload(48000, 2);
                // 48 samples * 2 channels * 3 bytes = 288 bytes
                const size_t dataSize = 288;
                uint8_t data[288];
                for(size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, dataSize);
                CHECK(packets.size() == 1);
                CHECK(packets[0].size() == RtpPacket::HeaderSize + dataSize);
        }

        SUBCASE("pack large audio fragments correctly") {
                RtpPayloadL24 payload(48000, 2);
                // 3000 bytes of audio data — should split into multiple packets
                const size_t dataSize = 3000;
                uint8_t data[3000];
                for(size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, dataSize);
                CHECK(packets.size() > 1);

                // All packets should share the same buffer
                for(size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().ptr() == packets[0].buffer().ptr());
                }
        }

        SUBCASE("pack/unpack round-trip") {
                RtpPayloadL24 payload(48000, 2);
                const size_t dataSize = 576; // 96 samples * 2 ch * 3 bytes
                uint8_t data[576];
                for(size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, dataSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == dataSize);
                CHECK(std::memcmp(result.data(), data, dataSize) == 0);
        }

        SUBCASE("large round-trip") {
                RtpPayloadL24 payload(48000, 8);
                // 1000 samples * 8 channels * 3 bytes = 24000 bytes
                const size_t dataSize = 24000;
                std::vector<uint8_t> data(dataSize);
                for(size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

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
                auto packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("pack/unpack round-trip") {
                RtpPayloadL16 payload(48000, 2);
                const size_t dataSize = 384; // 96 samples * 2 ch * 2 bytes
                uint8_t data[384];
                for(size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, dataSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == dataSize);
                CHECK(std::memcmp(result.data(), data, dataSize) == 0);
        }

        SUBCASE("large round-trip") {
                RtpPayloadL16 payload(48000, 4);
                const size_t dataSize = 9600; // 1200 samples * 4 ch * 2 bytes
                std::vector<uint8_t> data(dataSize);
                for(size_t i = 0; i < dataSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

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
                auto packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("pack small frame") {
                // 8x4 pixels at 24bpp = 96 bytes total
                RtpPayloadRawVideo payload(8, 4, 24);
                const size_t frameSize = 8 * 4 * 3;
                uint8_t data[96];
                for(size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, frameSize);
                CHECK(packets.size() > 0);
        }

        SUBCASE("pack/unpack round-trip small") {
                RtpPayloadRawVideo payload(16, 8, 24);
                const size_t frameSize = 16 * 8 * 3; // 384 bytes
                uint8_t data[384];
                for(size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, frameSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == frameSize);
                CHECK(std::memcmp(result.data(), data, frameSize) == 0);
        }

        SUBCASE("pack/unpack round-trip large") {
                // 320x240 at 24bpp = 230400 bytes
                RtpPayloadRawVideo payload(320, 240, 24);
                const size_t frameSize = 320 * 240 * 3;
                std::vector<uint8_t> data(frameSize);
                for(size_t i = 0; i < frameSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data.data(), frameSize);
                CHECK(packets.size() > 100); // Should be many packets

                Buffer result = payload.unpack(packets);
                CHECK(result.size() == frameSize);
                CHECK(std::memcmp(result.data(), data.data(), frameSize) == 0);
        }

        SUBCASE("all packets share one buffer") {
                RtpPayloadRawVideo payload(64, 64, 24);
                const size_t frameSize = 64 * 64 * 3;
                std::vector<uint8_t> data(frameSize, 0x42);

                auto packets = payload.pack(data.data(), frameSize);
                for(size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().ptr() == packets[0].buffer().ptr());
                }
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
                auto packets = payload.pack(nullptr, 0);
                CHECK(packets.isEmpty());
        }

        SUBCASE("set quality") {
                RtpPayloadJpeg payload(640, 480);
                payload.setQuality(50);
                CHECK(payload.quality() == 50);
        }

        SUBCASE("pack small JPEG") {
                RtpPayloadJpeg payload(320, 240);
                // Simulate small JPEG data
                const size_t jpegSize = 500;
                uint8_t data[500];
                for(size_t i = 0; i < jpegSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, jpegSize);
                CHECK(packets.size() == 1);
        }

        SUBCASE("pack/unpack round-trip") {
                RtpPayloadJpeg payload(640, 480, 85);
                const size_t jpegSize = 1000;
                uint8_t data[1000];
                for(size_t i = 0; i < jpegSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data, jpegSize);
                Buffer result = payload.unpack(packets);
                CHECK(result.size() == jpegSize);
                CHECK(std::memcmp(result.data(), data, jpegSize) == 0);
        }

        SUBCASE("large JPEG round-trip") {
                RtpPayloadJpeg payload(1920, 1080, 85);
                // Simulate a larger JPEG (~50KB)
                const size_t jpegSize = 50000;
                std::vector<uint8_t> data(jpegSize);
                for(size_t i = 0; i < jpegSize; i++) data[i] = static_cast<uint8_t>(i & 0xFF);

                auto packets = payload.pack(data.data(), jpegSize);
                CHECK(packets.size() > 1);

                // All share one buffer
                for(size_t i = 1; i < packets.size(); i++) {
                        CHECK(packets[i].buffer().ptr() == packets[0].buffer().ptr());
                }

                Buffer result = payload.unpack(packets);
                CHECK(result.size() == jpegSize);
                CHECK(std::memcmp(result.data(), data.data(), jpegSize) == 0);
        }

        SUBCASE("JPEG header fields") {
                RtpPayloadJpeg payload(640, 480, 75);
                uint8_t data[100];
                std::memset(data, 0xAA, 100);

                auto packets = payload.pack(data, 100);
                REQUIRE(packets.size() == 1);

                const uint8_t *pkt = packets[0].data();
                // Check JPEG header at offset 12 (after RTP header)
                // Type-specific = 0
                CHECK(pkt[12] == 0);
                // Fragment offset = 0 for first packet
                CHECK(pkt[13] == 0);
                CHECK(pkt[14] == 0);
                CHECK(pkt[15] == 0);
                // Type = 1
                CHECK(pkt[16] == 1);
                // Quality = 75
                CHECK(pkt[17] == 75);
                // Width/8 = 80
                CHECK(pkt[18] == 80);
                // Height/8 = 60
                CHECK(pkt[19] == 60);
        }
}
