/**
 * @file      rtppacket.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/rtppacket.h>
#include <cstring>

using namespace promeki;

TEST_CASE("RtpPacket") {

        SUBCASE("default construction") {
                RtpPacket pkt;
                CHECK(pkt.isNull());
                CHECK_FALSE(pkt.isValid());
                CHECK(pkt.size() == 0);
        }

        SUBCASE("isNull and isValid") {
                // Null packet
                RtpPacket null;
                CHECK(null.isNull());
                CHECK_FALSE(null.isValid());

                // Has buffer but too small for header
                auto smallBuf = Buffer::Ptr::create(8);
                std::memset(smallBuf->data(), 0, 8);
                RtpPacket tooSmall(smallBuf, 0, 8);
                CHECK_FALSE(tooSmall.isNull());
                CHECK_FALSE(tooSmall.isValid());

                // Has buffer, large enough, but wrong version
                auto buf = Buffer::Ptr::create(64);
                std::memset(buf->data(), 0, 64);
                RtpPacket wrongVer(buf, 0, 64);
                // Version is 0 (zeroed buffer) — not valid
                CHECK_FALSE(wrongVer.isNull());
                CHECK_FALSE(wrongVer.isValid());

                // Set version to 2 — now valid
                wrongVer.setVersion(2);
                CHECK(wrongVer.isValid());

                // Convenience constructor always produces a valid packet
                RtpPacket good(1400);
                CHECK_FALSE(good.isNull());
                CHECK(good.isValid());

                // Truncated extension makes packet invalid
                RtpPacket truncExt(14);
                truncExt.setExtension(true);
                // Extension flag set but only 2 bytes after fixed header
                CHECK_FALSE(truncExt.isValid());
        }

        SUBCASE("inherits BufferView") {
                auto buf = Buffer::Ptr::create(1024);
                buf->setSize(1024);
                std::memset(buf->data(), 0, 1024);
                RtpPacket pkt(buf, 100, 500);
                pkt.setVersion(2);
                CHECK(pkt.isValid());
                CHECK(pkt.offset() == 100);
                CHECK(pkt.size() == 500);
        }

        SUBCASE("convenience constructor") {
                RtpPacket pkt(1400);
                CHECK(pkt.isValid());
                CHECK(pkt.size() == 1400);
                CHECK(pkt.version() == 2);
                CHECK(pkt.payloadType() == 0);
                CHECK(pkt.sequenceNumber() == 0);
                CHECK(pkt.timestamp() == 0);
                CHECK(pkt.ssrc() == 0);
                CHECK(pkt.marker() == false);
                CHECK(pkt.padding() == false);
                CHECK(pkt.extension() == false);
                CHECK(pkt.csrcCount() == 0);
                CHECK(pkt.payloadSize() == 1400 - RtpPacket::HeaderSize);
        }

        SUBCASE("header read/write round-trip") {
                auto buf = Buffer::Ptr::create(256);
                buf->setSize(256);
                std::memset(buf->data(), 0, 256);

                RtpPacket pkt(buf, 0, 256);
                pkt.setVersion(2);
                pkt.setMarker(true);
                pkt.setPayloadType(96);
                pkt.setSequenceNumber(12345);
                pkt.setTimestamp(90000);
                pkt.setSsrc(0xDEADBEEF);

                CHECK(pkt.version() == 2);
                CHECK(pkt.marker() == true);
                CHECK(pkt.payloadType() == 96);
                CHECK(pkt.sequenceNumber() == 12345);
                CHECK(pkt.timestamp() == 90000);
                CHECK(pkt.ssrc() == 0xDEADBEEF);
        }

        SUBCASE("fields are independent") {
                auto buf = Buffer::Ptr::create(64);
                buf->setSize(64);
                std::memset(buf->data(), 0, 64);

                RtpPacket pkt(buf, 0, 64);

                // Set marker without clobbering PT
                pkt.setPayloadType(97);
                pkt.setMarker(true);
                CHECK(pkt.payloadType() == 97);
                CHECK(pkt.marker() == true);

                // Clear marker without clobbering PT
                pkt.setMarker(false);
                CHECK(pkt.payloadType() == 97);
                CHECK(pkt.marker() == false);

                // Set version without clobbering padding/extension/CC
                pkt.setPadding(true);
                pkt.setVersion(2);
                CHECK(pkt.padding() == true);
                CHECK(pkt.version() == 2);
        }

        SUBCASE("payload accessors") {
                auto buf = Buffer::Ptr::create(100);
                buf->setSize(100);
                std::memset(buf->data(), 0, 100);

                RtpPacket pkt(buf, 0, 100);
                pkt.setVersion(2);
                // Write something to payload area
                pkt.payload()[0] = 0xAB;

                CHECK(pkt.payloadSize() == 100 - RtpPacket::HeaderSize);
                CHECK(pkt.payload()[0] == 0xAB);
                CHECK(pkt.data()[RtpPacket::HeaderSize] == 0xAB);
        }

        SUBCASE("payload on undersized packet") {
                auto buf = Buffer::Ptr::create(8);
                buf->setSize(8);
                RtpPacket pkt(buf, 0, 8);
                CHECK(pkt.payload() == nullptr);
                CHECK(pkt.payloadSize() == 0);
        }

        SUBCASE("headerSize with no CSRC or extension") {
                RtpPacket pkt(100);
                CHECK(pkt.headerSize() == 12);
        }

        SUBCASE("headerSize with extension") {
                // Build a packet with extension header
                RtpPacket pkt(100);
                pkt.setExtension(true);
                // Extension header at byte 12: profile (2 bytes) + length (2 bytes)
                uint8_t *ext = pkt.data() + 12;
                ext[0] = 0xBE;  // profile high byte
                ext[1] = 0xDE;  // profile low byte
                ext[2] = 0x00;  // length high byte
                ext[3] = 0x02;  // length = 2 (32-bit words = 8 bytes)

                CHECK(pkt.extension() == true);
                CHECK(pkt.extensionProfile() == 0xBEDE);
                CHECK(pkt.extensionLength() == 2);
                // 12 (fixed) + 4 (ext header) + 8 (ext data) = 24
                CHECK(pkt.headerSize() == 24);
                CHECK(pkt.payloadSize() == 100 - 24);
                CHECK(pkt.payload() == pkt.data() + 24);
        }

        SUBCASE("extensionProfile and extensionLength with no extension") {
                RtpPacket pkt(100);
                CHECK(pkt.extensionProfile() == 0);
                CHECK(pkt.extensionLength() == 0);
        }

        SUBCASE("headerSize returns 0 for truncated extension") {
                // Packet claims extension but is too small to hold it
                auto buf = Buffer::Ptr::create(14);
                std::memset(buf->data(), 0, 14);
                RtpPacket pkt(buf, 0, 14);
                pkt.setVersion(2);
                pkt.setExtension(true);
                // Only 2 bytes after fixed header, need 4 for ext header
                CHECK(pkt.headerSize() == 0);
                CHECK(pkt.payload() == nullptr);
                CHECK(pkt.payloadSize() == 0);
        }

        SUBCASE("reads from wire bytes") {
                auto buf = Buffer::Ptr::create(RtpPacket::HeaderSize);
                buf->setSize(RtpPacket::HeaderSize);
                uint8_t *d = static_cast<uint8_t *>(buf->data());

                // Manually construct: V=2, P=0, X=0, CC=0, M=1, PT=97
                d[0] = 0x80;           // V=2
                d[1] = 0x80 | 97;      // M=1, PT=97
                d[2] = 0x00; d[3] = 42; // seq=42
                d[4] = 0x00; d[5] = 0x01; d[6] = 0x51; d[7] = 0x80; // ts=86400
                d[8] = 0x11; d[9] = 0x22; d[10] = 0x33; d[11] = 0x44; // SSRC

                RtpPacket pkt(buf, 0, RtpPacket::HeaderSize);
                CHECK(pkt.version() == 2);
                CHECK(pkt.marker() == true);
                CHECK(pkt.payloadType() == 97);
                CHECK(pkt.sequenceNumber() == 42);
                CHECK(pkt.timestamp() == 86400);
                CHECK(pkt.ssrc() == 0x11223344);
        }

        SUBCASE("multiple packets sharing one buffer") {
                auto buf = Buffer::Ptr::create(4200);
                buf->setSize(4200);
                std::memset(buf->data(), 0, 4200);

                RtpPacket pkt1(buf, 0, 1400);
                RtpPacket pkt2(buf, 1400, 1400);
                RtpPacket pkt3(buf, 2800, 1400);

                pkt1.setVersion(2);
                pkt2.setVersion(2);
                pkt3.setVersion(2);
                pkt1.setSequenceNumber(0);
                pkt2.setSequenceNumber(1);
                pkt3.setSequenceNumber(2);
                pkt3.setMarker(true);

                CHECK(pkt1.buffer().ptr() == pkt3.buffer().ptr());
                CHECK(pkt1.sequenceNumber() == 0);
                CHECK(pkt2.sequenceNumber() == 1);
                CHECK(pkt3.sequenceNumber() == 2);
                CHECK(pkt3.marker() == true);
                CHECK(pkt1.marker() == false);
        }

        SUBCASE("createList") {
                auto pkts = RtpPacket::createList(10, 1400);
                CHECK(pkts.size() == 10);

                // All share the same buffer
                for(size_t i = 1; i < pkts.size(); ++i) {
                        CHECK(pkts[i].buffer().ptr() == pkts[0].buffer().ptr());
                }

                // Each packet is independent and pre-initialized
                for(size_t i = 0; i < pkts.size(); ++i) {
                        CHECK(pkts[i].size() == 1400);
                        CHECK(pkts[i].version() == 2);
                        CHECK(pkts[i].offset() == i * 1400);
                        pkts[i].setSequenceNumber(static_cast<uint16_t>(i));
                }

                // Verify writes didn't clobber neighbors
                for(size_t i = 0; i < pkts.size(); ++i) {
                        CHECK(pkts[i].sequenceNumber() == static_cast<uint16_t>(i));
                }
        }

        SUBCASE("createList with zero count") {
                auto pkts = RtpPacket::createList(0, 1400);
                CHECK(pkts.size() == 0);
        }

        SUBCASE("createList with varying sizes") {
                RtpPacket::SizeList sizes;
                sizes.pushToBack(100);
                sizes.pushToBack(200);
                sizes.pushToBack(1400);
                sizes.pushToBack(64);

                auto pkts = RtpPacket::createList(sizes);
                CHECK(pkts.size() == 4);

                // All share the same buffer
                for(size_t i = 1; i < pkts.size(); ++i) {
                        CHECK(pkts[i].buffer().ptr() == pkts[0].buffer().ptr());
                }

                // Each packet has the right size and offset
                CHECK(pkts[0].size() == 100);
                CHECK(pkts[0].offset() == 0);
                CHECK(pkts[1].size() == 200);
                CHECK(pkts[1].offset() == 100);
                CHECK(pkts[2].size() == 1400);
                CHECK(pkts[2].offset() == 300);
                CHECK(pkts[3].size() == 64);
                CHECK(pkts[3].offset() == 1700);

                // All are valid V=2 packets
                for(size_t i = 0; i < pkts.size(); ++i) {
                        CHECK(pkts[i].isValid());
                        CHECK(pkts[i].version() == 2);
                }

                // Writes to one packet don't clobber neighbors
                for(size_t i = 0; i < pkts.size(); ++i) {
                        pkts[i].setSequenceNumber(static_cast<uint16_t>(i * 100));
                }
                for(size_t i = 0; i < pkts.size(); ++i) {
                        CHECK(pkts[i].sequenceNumber() == static_cast<uint16_t>(i * 100));
                }
        }

        SUBCASE("createList with empty sizes list") {
                RtpPacket::SizeList sizes;
                auto pkts = RtpPacket::createList(sizes);
                CHECK(pkts.size() == 0);
        }

        SUBCASE("clear") {
                RtpPacket pkt(100);
                pkt.setPayloadType(96);
                pkt.setSequenceNumber(5000);
                pkt.setMarker(true);
                pkt.setSsrc(0xCAFEBABE);
                pkt.payload()[0] = 0xFF;

                pkt.clear();

                CHECK(pkt.version() == 2);
                CHECK(pkt.payloadType() == 0);
                CHECK(pkt.sequenceNumber() == 0);
                CHECK(pkt.marker() == false);
                CHECK(pkt.ssrc() == 0);
                CHECK(pkt.payload()[0] == 0);
        }

        SUBCASE("clear on null packet") {
                RtpPacket pkt;
                pkt.clear(); // Should not crash
                CHECK(pkt.isNull());
        }

        SUBCASE("createList with zero packet size") {
                auto pkts = RtpPacket::createList(5, 0);
                CHECK(pkts.size() == 0);
        }

        SUBCASE("padding flag round-trip") {
                RtpPacket pkt(100);
                CHECK(pkt.padding() == false);
                pkt.setPadding(true);
                CHECK(pkt.padding() == true);
                // Verify version and extension not clobbered
                CHECK(pkt.version() == 2);
                CHECK(pkt.extension() == false);
                pkt.setPadding(false);
                CHECK(pkt.padding() == false);
                CHECK(pkt.version() == 2);
        }

        SUBCASE("header-only packet has no payload") {
                RtpPacket pkt(RtpPacket::HeaderSize);
                CHECK(pkt.isValid());
                CHECK(pkt.payload() == nullptr);
                CHECK(pkt.payloadSize() == 0);
        }

        SUBCASE("createList varying sizes with undersized entry") {
                RtpPacket::SizeList sizes;
                sizes.pushToBack(8);   // Too small for RTP header
                sizes.pushToBack(100);
                auto pkts = RtpPacket::createList(sizes);
                CHECK(pkts.size() == 2);
                // First packet is too small — version not set since size < HeaderSize
                CHECK_FALSE(pkts[0].isValid());
                CHECK(pkts[1].isValid());
        }
}
