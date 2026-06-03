/**
 * @file      pcapreader.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/pcapreader.h>

using namespace promeki;

namespace {

// Minimal capture-file byte builder.  Emits multi-byte fields in the
// requested endianness so the same construction code exercises both
// little- and big-endian readers.
struct Cap {
                bool be;
                std::vector<uint8_t> d;

                explicit Cap(bool bigEndian) : be(bigEndian) {}

                void u8(uint8_t v) { d.push_back(v); }
                void u16(uint16_t v) {
                        if(be) {
                                d.push_back(static_cast<uint8_t>(v >> 8));
                                d.push_back(static_cast<uint8_t>(v));
                        } else {
                                d.push_back(static_cast<uint8_t>(v));
                                d.push_back(static_cast<uint8_t>(v >> 8));
                        }
                }
                void u32(uint32_t v) {
                        if(be) {
                                d.push_back(static_cast<uint8_t>(v >> 24));
                                d.push_back(static_cast<uint8_t>(v >> 16));
                                d.push_back(static_cast<uint8_t>(v >> 8));
                                d.push_back(static_cast<uint8_t>(v));
                        } else {
                                d.push_back(static_cast<uint8_t>(v));
                                d.push_back(static_cast<uint8_t>(v >> 8));
                                d.push_back(static_cast<uint8_t>(v >> 16));
                                d.push_back(static_cast<uint8_t>(v >> 24));
                        }
                }
                void bytes(std::initializer_list<uint8_t> b) {
                        for(uint8_t x : b) d.push_back(x);
                }

                Buffer buffer() const {
                        Buffer buf(d.size() == 0 ? 1 : d.size());
                        std::memcpy(buf.data(), d.data(), d.size());
                        buf.setSize(d.size());
                        return buf;
                }
};

// Classic pcap global header (24 bytes).  network == link type.
void classicGlobalHeader(Cap &c, uint32_t magic, uint32_t snaplen, uint32_t network) {
        c.u32(magic);     // magic
        c.u16(2);         // version major
        c.u16(4);         // version minor
        c.u32(0);         // thiszone
        c.u32(0);         // sigfigs
        c.u32(snaplen);   // snaplen
        c.u32(network);   // network / link type
}

// Classic pcap record header + payload (16-byte header).
void classicRecord(Cap &c, uint32_t tsSec, uint32_t tsFrac, uint32_t origLen, std::initializer_list<uint8_t> data) {
        c.u32(tsSec);
        c.u32(tsFrac);
        c.u32(static_cast<uint32_t>(data.size())); // incl_len
        c.u32(origLen);
        c.bytes(data);
}

} // namespace

TEST_CASE("PcapReader: classic pcap, little-endian, microsecond timestamps") {
        Cap c(false);
        classicGlobalHeader(c, PcapReader::MagicMicros, 65535, PcapLinkType::Ethernet.value());
        classicRecord(c, 0x11223344u, 500000u /* 0.5s in us */, 4, {0xDE, 0xAD, 0xBE, 0xEF});

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        CHECK(r.format() == PcapFileFormat::ClassicPcap);
        CHECK(r.byteOrder() == PcapByteOrder::LittleEndian);
        CHECK(r.linkType() == PcapLinkType::Ethernet);
        CHECK(r.snapLength() == 65535u);

        auto [rec, err] = r.next();
        REQUIRE(err.isOk());
        CHECK(rec.linkType == PcapLinkType::Ethernet);
        CHECK(rec.capturedLength() == 4);
        CHECK(rec.originalLength == 4u);
        CHECK_FALSE(rec.snapTruncated);
        REQUIRE(rec.frame.size() == 4);
        const uint8_t *fp = rec.frame.data();
        CHECK(fp[0] == 0xDE);
        CHECK(fp[3] == 0xEF);
        REQUIRE(rec.captureTime.isValid());
        // 0x11223344 s + 0.5 s, expressed in ns since the Unix epoch.
        CHECK(rec.captureTime.nanoseconds() == static_cast<int64_t>(0x11223344) * 1000000000LL + 500000000LL);

        auto [rec2, err2] = r.next();
        CHECK(err2 == Error::EndOfFile);
}

TEST_CASE("PcapReader: classic pcap, big-endian, nanosecond timestamps") {
        Cap c(true);
        classicGlobalHeader(c, PcapReader::MagicNanos, 2048, PcapLinkType::Ethernet.value());
        classicRecord(c, 7u, 123456789u /* ns */, 4, {0x01, 0x02, 0x03, 0x04});

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        CHECK(r.format() == PcapFileFormat::ClassicPcap);
        CHECK(r.byteOrder() == PcapByteOrder::BigEndian);

        auto [rec, err] = r.next();
        REQUIRE(err.isOk());
        REQUIRE(rec.captureTime.isValid());
        CHECK(rec.captureTime.nanoseconds() == 7LL * 1000000000LL + 123456789LL);
        CHECK(rec.capturedLength() == 4);
}

TEST_CASE("PcapReader: classic snaplen-truncated frame is flagged, not an error") {
        Cap c(false);
        classicGlobalHeader(c, PcapReader::MagicMicros, 4, PcapLinkType::Ethernet.value());
        // incl_len 4 but orig_len 100 -> the frame was snapped short.
        classicRecord(c, 1, 0, 100, {0xAA, 0xBB, 0xCC, 0xDD});

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        auto [rec, err] = r.next();
        REQUIRE(err.isOk());
        CHECK(rec.capturedLength() == 4);
        CHECK(rec.originalLength == 100u);
        CHECK(rec.snapTruncated);
}

TEST_CASE("PcapReader: a file cut off mid-record yields TruncatedData") {
        Cap c(false);
        classicGlobalHeader(c, PcapReader::MagicMicros, 65535, PcapLinkType::Ethernet.value());
        // Record header claims 4 bytes of payload but only 2 are present.
        c.u32(1);          // ts_sec
        c.u32(0);          // ts_usec
        c.u32(4);          // incl_len (claims 4)
        c.u32(4);          // orig_len
        c.bytes({0xAA, 0xBB}); // ...but only 2 bytes follow

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        auto [rec, err] = r.next();
        CHECK(err == Error::TruncatedData);
}

TEST_CASE("PcapReader: unrecognised magic is CorruptData") {
        Cap c(false);
        c.u32(0xCAFEBABEu);
        c.u32(0);
        PcapReader r;
        CHECK(r.openBuffer(c.buffer()) == Error::CorruptData);
}

// --- pcapng -----------------------------------------------------------

namespace {

// Append a complete pcapng Section Header Block.
void pcapngShb(Cap &c) {
        c.u32(PcapReader::PngBlockShb); // block type
        c.u32(28);                      // total length
        c.u32(PcapReader::PngByteOrderMagic);
        c.u16(1);                       // version major
        c.u16(0);                       // version minor
        c.u32(0xFFFFFFFFu);             // section length (-1, low)
        c.u32(0xFFFFFFFFu);             // section length (-1, high)
        c.u32(28);                      // trailing total length
}

// Append an Interface Description Block with an if_tsresol option.
void pcapngIdb(Cap &c, uint16_t linkType, uint32_t snaplen, uint8_t tsResol) {
        c.u32(PcapReader::PngBlockIdb);
        c.u32(32); // total length
        c.u16(linkType);
        c.u16(0);  // reserved
        c.u32(snaplen);
        // option if_tsresol (code 9, len 1) + 3 pad
        c.u16(9);
        c.u16(1);
        c.u8(tsResol);
        c.u8(0);
        c.u8(0);
        c.u8(0);
        // opt_endofopt
        c.u16(0);
        c.u16(0);
        c.u32(32); // trailing total length
}

// Append an Enhanced Packet Block carrying a 4-byte payload.
void pcapngEpb(Cap &c, uint32_t ifId, uint64_t ticks, uint32_t origLen, std::initializer_list<uint8_t> data) {
        c.u32(PcapReader::PngBlockEpb);
        c.u32(36); // total length (fixed 20 body + 4 data + 12 framing)
        c.u32(ifId);
        c.u32(static_cast<uint32_t>(ticks >> 32));
        c.u32(static_cast<uint32_t>(ticks & 0xFFFFFFFFu));
        c.u32(static_cast<uint32_t>(data.size())); // captured length
        c.u32(origLen);
        c.bytes(data);
        c.u32(36); // trailing total length
}

} // namespace

TEST_CASE("PcapReader: pcapng little-endian, SHB + IDB(ns) + EPB") {
        Cap c(false);
        pcapngShb(c);
        pcapngIdb(c, PcapLinkType::Ethernet.value(), 65535, 9 /* ns resolution */);
        const uint64_t ticks = 1500000000000000000ull; // 1.5e9 s expressed in ns ticks
        pcapngEpb(c, 0, ticks, 4, {0xDE, 0xAD, 0xBE, 0xEF});

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        CHECK(r.format() == PcapFileFormat::Pcapng);
        CHECK(r.byteOrder() == PcapByteOrder::LittleEndian);
        // Interfaces are pre-scanned so metadata is available up front.
        CHECK(r.interfaceCount() == 1);
        CHECK(r.linkType() == PcapLinkType::Ethernet);
        CHECK(r.snapLength() == 65535u);

        auto [rec, err] = r.next();
        REQUIRE(err.isOk());
        CHECK(rec.linkType == PcapLinkType::Ethernet);
        CHECK(rec.capturedLength() == 4);
        REQUIRE(rec.captureTime.isValid());
        CHECK(rec.captureTime.nanoseconds() == static_cast<int64_t>(ticks));

        auto [rec2, err2] = r.next();
        CHECK(err2 == Error::EndOfFile);
}

TEST_CASE("PcapReader: pcapng big-endian round-trips identically") {
        Cap c(true);
        pcapngShb(c);
        pcapngIdb(c, PcapLinkType::Ethernet.value(), 1024, 6 /* us resolution */);
        // 6 = microseconds: ticks are us, so 2 ticks == 2000 ns.
        pcapngEpb(c, 0, 2ull, 4, {0x11, 0x22, 0x33, 0x44});

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        CHECK(r.byteOrder() == PcapByteOrder::BigEndian);
        auto [rec, err] = r.next();
        REQUIRE(err.isOk());
        CHECK(rec.captureTime.nanoseconds() == 2000LL);
}

TEST_CASE("PcapReader: pcapng Simple Packet Block carries no timestamp") {
        Cap c(false);
        pcapngShb(c);
        pcapngIdb(c, PcapLinkType::Ethernet.value(), 65535, 6);
        // SPB: type(4) + total_len(4) + orig_len(4) + data(4) + trailing len(4) = 20.
        c.u32(PcapReader::PngBlockSpb);
        c.u32(20);
        c.u32(4); // original length
        c.bytes({0xAB, 0xCD, 0xEF, 0x01});
        c.u32(20);

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        auto [rec, err] = r.next();
        REQUIRE(err.isOk());
        CHECK(rec.linkType == PcapLinkType::Ethernet);
        CHECK(rec.capturedLength() == 4);
        CHECK_FALSE(rec.captureTime.isValid()); // SPB has no timestamp
}

TEST_CASE("PcapReader: pcapng per-interface link type is authoritative") {
        Cap c(false);
        pcapngShb(c);
        pcapngIdb(c, PcapLinkType::Ethernet.value(), 65535, 6);   // interface 0
        pcapngIdb(c, PcapLinkType::LinuxSll2.value(), 65535, 6);  // interface 1
        pcapngEpb(c, 1, 0, 4, {0x00, 0x11, 0x22, 0x33});         // captured on interface 1

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        CHECK(r.interfaceCount() == 2);
        auto [rec, err] = r.next();
        REQUIRE(err.isOk());
        CHECK(rec.linkType == PcapLinkType::LinuxSll2);
}

TEST_CASE("PcapReader: pcapng skips unrecognised block types") {
        Cap c(false);
        pcapngShb(c);
        pcapngIdb(c, PcapLinkType::Ethernet.value(), 65535, 6);
        // An unknown block type (0x00000005, ISB) the reader must skip.
        c.u32(0x00000005u);
        c.u32(16);
        c.u32(0xDEADBEEFu);
        c.u32(16);
        pcapngEpb(c, 0, 0, 4, {0x09, 0x08, 0x07, 0x06});

        PcapReader r;
        REQUIRE(r.openBuffer(c.buffer()).isOk());
        auto [rec, err] = r.next();
        REQUIRE(err.isOk());
        CHECK(rec.capturedLength() == 4);
        const uint8_t *fp = rec.frame.data();
        CHECK(fp[0] == 0x09);
}

TEST_CASE("PcapReader: next() on an unopened reader reports NotOpen") {
        PcapReader r;
        auto [rec, err] = r.next();
        CHECK(err == Error::NotOpen);
}

TEST_CASE("PcapLinkType: unknown wire value round-trips as a valid Enum") {
        PcapLinkType lt(12345);
        CHECK(lt.value() == 12345);
}
