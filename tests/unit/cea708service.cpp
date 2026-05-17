/**
 * @file      cea708service.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/bufferiodevice.h>
#include <promeki/buffer.h>
#include <promeki/cea708cdp.h>
#include <promeki/cea708service.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/string.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        /// @brief Builds a Buffer holding the given bytes.  Convenience
        ///        for constructing service-block data inline.
        Buffer bytesBuf(std::initializer_list<uint8_t> bytes) {
                Buffer b(bytes.size());
                b.setSize(bytes.size());
                if (bytes.size() > 0) {
                        std::vector<uint8_t> v(bytes);
                        b.copyFrom(v.data(), v.size(), 0);
                }
                return b;
        }

} // namespace

// ============================================================================
// Cea708Service
// ============================================================================

TEST_CASE("Cea708Service: default-constructed is the null block") {
        Cea708Service s;
        CHECK(s.serviceNumber() == 0);
        CHECK(s.isNull());
        CHECK_FALSE(s.isExtended());
        CHECK(s.data().size() == 0);
}

TEST_CASE("Cea708Service: isExtended depends on service number") {
        Cea708Service s1(1, Buffer());
        Cea708Service s6(6, Buffer());
        Cea708Service s7(7, Buffer());
        Cea708Service s63(63, Buffer());
        CHECK_FALSE(s1.isExtended());
        CHECK_FALSE(s6.isExtended());
        CHECK(s7.isExtended());
        CHECK(s63.isExtended());
}

TEST_CASE("Cea708Service::fromText: G0 chars pass through, others substituted with space") {
        Cea708Service s = Cea708Service::fromText(1, String("Hi\t!"));
        REQUIRE(s.data().size() == 4);
        const auto *p = static_cast<const uint8_t *>(s.data().data());
        CHECK(p[0] == 'H');
        CHECK(p[1] == 'i');
        // \t (0x09) is out of G0 range — substituted with 0x20.
        CHECK(p[2] == 0x20);
        CHECK(p[3] == '!');
}

TEST_CASE("Cea708Service::text: recovers G0 chars from data buffer") {
        Cea708Service s(1, bytesBuf({'A', 'B', 'C'}));
        CHECK(s.text() == "ABC");
}

TEST_CASE("Cea708Service::text: skips C0 / C1 control bytes") {
        // Mix of G0 + C0 (clear screen 0x03) + G0 + C1 (delete window 0x8C) + G0.
        Cea708Service s(1, bytesBuf({'A', 0x03, 'B', 0x8C, 'C'}));
        CHECK(s.text() == "ABC");
}

TEST_CASE("Cea708Service::fromText + text round-trip on printable ASCII") {
        Cea708Service s = Cea708Service::fromText(1, "Hello, world.");
        CHECK(s.text() == "Hello, world.");
}

// ============================================================================
// Cea708DtvccPacket — payload byte layout
// ============================================================================

TEST_CASE("Cea708DtvccPacket: empty packet emits an empty payload") {
        Cea708DtvccPacket pkt;
        CHECK(pkt.payloadByteCount() == 0);
        CHECK(pkt.toPayloadBytes().size() == 0);
}

TEST_CASE("Cea708DtvccPacket: standard service 1 block header layout") {
        // Service 1, 3 bytes of data ('A','B','C').
        //   header byte: (serviceNum<<5) | blockSize = (1<<5) | 3 = 0x23
        //   data: 0x41 0x42 0x43
        Cea708DtvccPacket pkt;
        pkt.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'A', 'B', 'C'})));
        Buffer payload = pkt.toPayloadBytes();
        REQUIRE(payload.size() == 4);
        const auto *p = static_cast<const uint8_t *>(payload.data());
        CHECK(p[0] == 0x23);
        CHECK(p[1] == 'A');
        CHECK(p[2] == 'B');
        CHECK(p[3] == 'C');
}

TEST_CASE("Cea708DtvccPacket: extended service 7 block uses 2-byte header") {
        // Service 7, 2 bytes of data ('X','Y').
        //   byte 0: (7<<5) | blockSize = 0xE0 | 2 = 0xE2
        //   byte 1: serviceNumberExt = 7  → 0x07
        //   data:   0x58 0x59
        Cea708DtvccPacket pkt;
        pkt.serviceBlocks().pushToBack(Cea708Service(7, bytesBuf({'X', 'Y'})));
        Buffer payload = pkt.toPayloadBytes();
        REQUIRE(payload.size() == 4);
        const auto *p = static_cast<const uint8_t *>(payload.data());
        CHECK(p[0] == 0xE2);
        CHECK(p[1] == 0x07);
        CHECK(p[2] == 'X');
        CHECK(p[3] == 'Y');
}

TEST_CASE("Cea708DtvccPacket: null block emits a single 0x00 header byte") {
        Cea708DtvccPacket pkt;
        pkt.serviceBlocks().pushToBack(Cea708Service()); // service=0
        Buffer payload = pkt.toPayloadBytes();
        REQUIRE(payload.size() == 1);
        const auto *p = static_cast<const uint8_t *>(payload.data());
        CHECK(p[0] == 0x00);
}

TEST_CASE("Cea708DtvccPacket: parsePayloadBytes round-trips one service block") {
        Cea708DtvccPacket orig;
        orig.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'H', 'i'})));
        Buffer payload = orig.toPayloadBytes();
        auto [blocks, err] = Cea708DtvccPacket::parsePayloadBytes(payload.data(), payload.size());
        REQUIRE(err.isOk());
        REQUIRE(blocks.size() == 1);
        CHECK(blocks[0].serviceNumber() == 1);
        CHECK(blocks[0].text() == "Hi");
}

TEST_CASE("Cea708DtvccPacket: parsePayloadBytes round-trips extended service block") {
        Cea708DtvccPacket orig;
        orig.serviceBlocks().pushToBack(Cea708Service(42, bytesBuf({'O', 'k'})));
        Buffer payload = orig.toPayloadBytes();
        auto [blocks, err] = Cea708DtvccPacket::parsePayloadBytes(payload.data(), payload.size());
        REQUIRE(err.isOk());
        REQUIRE(blocks.size() == 1);
        CHECK(blocks[0].serviceNumber() == 42);
        CHECK(blocks[0].isExtended());
        CHECK(blocks[0].text() == "Ok");
}

TEST_CASE("Cea708DtvccPacket: parsePayloadBytes stops at null block terminator") {
        // Two blocks then a null terminator.
        Cea708DtvccPacket orig;
        orig.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'A'})));
        orig.serviceBlocks().pushToBack(Cea708Service(2, bytesBuf({'B'})));
        orig.serviceBlocks().pushToBack(Cea708Service()); // null
        Buffer payload = orig.toPayloadBytes();
        auto [blocks, err] = Cea708DtvccPacket::parsePayloadBytes(payload.data(), payload.size());
        REQUIRE(err.isOk());
        REQUIRE(blocks.size() == 3);
        CHECK(blocks[0].serviceNumber() == 1);
        CHECK(blocks[1].serviceNumber() == 2);
        CHECK(blocks[2].isNull());
}

// ============================================================================
// Cea708DtvccPacket — CcData round-trip
// ============================================================================

TEST_CASE("Cea708DtvccPacket::toCcData: first triple has cc_type=2, rest cc_type=3") {
        Cea708DtvccPacket pkt(0, {});
        pkt.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'A', 'B', 'C', 'D'})));
        Cea708Cdp::CcDataList triples = pkt.toCcData();
        REQUIRE(triples.size() >= 1);
        CHECK(triples[0].type == 2); // packet start
        for (size_t i = 1; i < triples.size(); ++i) {
                CHECK(triples[i].type == 3); // packet data
        }
}

TEST_CASE("Cea708DtvccPacket::toCcData header byte carries sequence + packet_size_code") {
        Cea708DtvccPacket pkt(2, {});
        pkt.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'A'})));
        // Payload bytes: header (0x21) + 'A' = 2 bytes.
        // packet_size_code = payloadSize + 1 = 3.
        // header byte = (2 << 6) | 3 = 0x83.
        Cea708Cdp::CcDataList triples = pkt.toCcData();
        REQUIRE(triples.size() >= 1);
        CHECK(triples[0].b1 == 0x83);
}

TEST_CASE("Cea708DtvccPacket: full round-trip via cc_data") {
        Cea708DtvccPacket orig(1, {});
        orig.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'H', 'e', 'l', 'l', 'o'})));
        Cea708Cdp::CcDataList triples = orig.toCcData();
        auto [parsed, err] = Cea708DtvccPacket::fromCcData(triples);
        REQUIRE(err.isOk());
        CHECK(parsed.sequenceNumber() == 1);
        REQUIRE(parsed.serviceBlocks().size() == 1);
        CHECK(parsed.serviceBlocks()[0].serviceNumber() == 1);
        CHECK(parsed.serviceBlocks()[0].text() == "Hello");
}

TEST_CASE("Cea708DtvccPacket::fromCcData rejects a first triple with wrong cc_type") {
        Cea708Cdp::CcDataList triples;
        triples.pushToBack(Cea708Cdp::CcData{true, 3 /*data, expected start*/, 0x81, 0x00});
        auto [parsed, err] = Cea708DtvccPacket::fromCcData(triples);
        CHECK(err.code() == Error::ParseFailed);
}

TEST_CASE("Cea708DtvccPacket::toPayloadBytes: oversized block data is truncated to 31 bytes") {
        // The wire @c block_size field is 5 bits — anything > 31 must
        // be truncated by toPayloadBytes so the parser can't be
        // tricked into reading wrapped sizes and misinterpreting the
        // overflow bytes as a fresh block header.
        std::initializer_list<uint8_t> oversize = {
                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
                'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
                'U', 'V', 'W', 'X', 'Y', 'Z', '0', '1', '2', '3',
                '4', '5', '6', '7', '8'  // 35 bytes total
        };
        Cea708DtvccPacket pkt;
        pkt.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf(oversize)));
        Buffer payload = pkt.toPayloadBytes();
        // Header byte (1) + truncated data (31) = 32 bytes.
        REQUIRE(payload.size() == 32);
        const auto *p = static_cast<const uint8_t *>(payload.data());
        // Header carries block_size = 31 (0x1F), service 1 → 0x3F.
        CHECK(p[0] == ((1u << 5) | 31u));
        // First 31 data bytes preserved verbatim.
        for (size_t i = 0; i < 31; ++i) CHECK(p[1 + i] == *(oversize.begin() + i));

        // And it must round-trip as exactly one block of 31 bytes.
        auto [blocks, err] = Cea708DtvccPacket::parsePayloadBytes(payload.data(), payload.size());
        REQUIRE(err.isOk());
        REQUIRE(blocks.size() == 1);
        CHECK(blocks[0].serviceNumber() == 1);
        CHECK(blocks[0].data().size() == 31);
}

TEST_CASE("Cea708DtvccPacket: triple count matches the spec formula") {
        // Payload size = 1 (header byte) + service block bytes.
        // Triple count = ceil((1 + payload) / 2) = ceil(packet_size / 2).
        Cea708DtvccPacket pkt(0, {});
        pkt.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'A', 'B', 'C'})));
        // payload = 1 (header) + 1 (block hdr) + 3 (chars) = wait, the
        // "packet payload" for the wire is the *post-header* data.
        // packet_size in our header = payloadSize + 1.  Wire bytes
        // emitted across triples = packet_size = payloadSize + 1.
        // payloadSize = 4 (block hdr 0x23 + 'A' + 'B' + 'C'),
        // packet_size = 5, triple count = ceil(5/2) = 3.
        Cea708Cdp::CcDataList triples = pkt.toCcData();
        CHECK(triples.size() == 3);
}

// ============================================================================
// JSON / toString
// ============================================================================

TEST_CASE("Cea708Service::toJson reports service number, byte count, and hex dump") {
        Cea708Service svc(2, bytesBuf({0x91, 0x20, 0xAB}));
        JsonObject    obj = svc.toJson();
        CHECK(obj.getInt("serviceNumber") == 2);
        CHECK(obj.getInt("dataSize") == 3);
        CHECK(obj.getString("dataHex") == "91 20 ab");
}

TEST_CASE("Cea708Service::toString includes service number and byte count") {
        Cea708Service svc(7, bytesBuf({1, 2, 3, 4}));
        CHECK(svc.toString() == "Cea708Service(svc=7, bytes=4)");
}

TEST_CASE("Cea708DtvccPacket::toJson nests service-block JSON") {
        Cea708DtvccPacket pkt(2, {});
        pkt.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'A', 'B'})));
        pkt.serviceBlocks().pushToBack(Cea708Service(7, bytesBuf({0x10, 0x11})));
        JsonObject obj = pkt.toJson();
        CHECK(obj.getInt("sequenceNumber") == 2);
        CHECK(obj.getInt("payloadByteCount") == static_cast<int64_t>(pkt.payloadByteCount()));
        JsonArray arr = obj.getArray("serviceBlocks");
        REQUIRE(arr.size() == 2);
        CHECK(arr.getObject(0).getInt("serviceNumber") == 1);
        CHECK(arr.getObject(1).getInt("serviceNumber") == 7);
}

TEST_CASE("Cea708DtvccPacket::toString summarises sequence + block + payload counts") {
        Cea708DtvccPacket pkt(3, {});
        pkt.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'X'})));
        String s = pkt.toString();
        CHECK(s == "Cea708DtvccPacket(seq=3, blocks=1, payload=2B)");
}

// ============================================================================
// Variant integration
// ============================================================================

TEST_CASE("Cea708Service: round-trips through Variant") {
        Cea708Service original(5, bytesBuf({0x91, 0x52, 0x20, 0x41}));
        Variant       v;
        v.set(original);
        CHECK(v.type() == DataTypeCea708Service);
        Cea708Service out = v.get<Cea708Service>();
        CHECK(out == original);
}

TEST_CASE("Cea708DtvccPacket: round-trips through Variant") {
        Cea708DtvccPacket original(2, {});
        original.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'h', 'i'})));
        original.serviceBlocks().pushToBack(Cea708Service(7, bytesBuf({0x91, 0x20})));
        Variant v;
        v.set(original);
        CHECK(v.type() == DataTypeCea708DtvccPacket);
        Cea708DtvccPacket out = v.get<Cea708DtvccPacket>();
        CHECK(out == original);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("Cea708Service: DataStream operators round-trip") {
        Cea708Service  original(7, bytesBuf({0x10, 0x11, 0x20, 0x41, 0x42}));
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        Cea708Service restored;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> restored;
        }
        CHECK(restored == original);
}

TEST_CASE("Cea708DtvccPacket: DataStream operators round-trip a multi-block packet") {
        Cea708DtvccPacket original(1, {});
        original.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({'h', 'e', 'l', 'l', 'o'})));
        original.serviceBlocks().pushToBack(Cea708Service(7, bytesBuf({0x91, 0x20})));
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        Cea708DtvccPacket restored;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> restored;
        }
        CHECK(restored == original);
}

TEST_CASE("Cea708DtvccPacket: DataStream round-trip via tagged Variant") {
        Cea708DtvccPacket original(2, {});
        original.serviceBlocks().pushToBack(Cea708Service(1, bytesBuf({0x20, 0x21, 0x22})));
        Variant         v;
        v.set(original);
        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << v;
        }
        dev.seek(0);
        Variant restored;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> restored;
        }
        REQUIRE(restored.type() == DataTypeCea708DtvccPacket);
        Cea708DtvccPacket out = restored.get<Cea708DtvccPacket>();
        CHECK(out == original);
}
