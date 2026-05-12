/**
 * @file      ancpacket.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancpacket.h>
#include <promeki/ancmeta.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;

// Helper: build a small Buffer with deterministic content for round-trip
// tests.  copyFrom writes the bytes; setSize stamps the logical
// content length so downstream readers (Buffer::size, AncPacket
// diagnostics) see the right value.
static Buffer makeWireBytes(std::initializer_list<uint8_t> bytes) {
        Buffer buf(bytes.size());
        Error  err = buf.copyFrom(bytes.begin(), bytes.size());
        REQUIRE(err.isOk());
        buf.setSize(bytes.size());
        return buf;
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AncPacket: default constructor is invalid") {
        AncPacket pkt;
        CHECK_FALSE(pkt.isValid());
        CHECK(pkt.format().id() == AncFormat::Invalid);
        CHECK(pkt.transport() == AncTransport::Invalid);
        CHECK(pkt.data().size() == 0);
        CHECK(pkt.meta().isEmpty());
}

TEST_CASE("AncPacket: full-state constructor") {
        Buffer    bytes = makeWireBytes({0x61, 0x01, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE});
        Metadata  meta;
        meta.set(AncMeta::St291::Line, uint16_t(11));
        meta.set(AncMeta::St291::HOffset, uint16_t(0));
        AncPacket pkt(AncFormat(AncFormat::Cea708), AncTransport::St291, bytes, meta);

        CHECK(pkt.isValid());
        CHECK(pkt.format().id() == AncFormat::Cea708);
        CHECK(pkt.transport() == AncTransport::St291);
        CHECK(pkt.data().size() == 8);
        CHECK(pkt.meta().get(AncMeta::St291::Line).get<uint16_t>() == 11);
        CHECK(pkt.meta().get(AncMeta::St291::HOffset).get<uint16_t>() == 0);
}

TEST_CASE("AncPacket: full-state constructor with default meta") {
        Buffer    bytes = makeWireBytes({0x60, 0x60, 0x00});
        AncPacket pkt(AncFormat(AncFormat::AtcLtc), AncTransport::St291, bytes);
        CHECK(pkt.isValid());
        CHECK(pkt.meta().isEmpty());
}

// ============================================================================
// CoW semantics
// ============================================================================

TEST_CASE("AncPacket: copy is cheap and independent under mutation") {
        Buffer    bytes = makeWireBytes({0x61, 0x01, 0x02, 0xAB, 0xCD});
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, bytes);
        AncPacket b = a;
        CHECK(a == b);

        // Mutating b must not affect a.
        b.setTransport(AncTransport::NdiXml);
        CHECK(a.transport() == AncTransport::St291);
        CHECK(b.transport() == AncTransport::NdiXml);
        CHECK(a != b);
}

TEST_CASE("AncPacket: setFormat detaches CoW") {
        AncPacket a(AncFormat(AncFormat::Afd), AncTransport::St291, makeWireBytes({0x41, 0x05, 0x01, 0x00}));
        AncPacket b = a;
        b.setFormat(AncFormat(AncFormat::BarData));
        CHECK(a.format().id() == AncFormat::Afd);
        CHECK(b.format().id() == AncFormat::BarData);
}

TEST_CASE("AncPacket: setData detaches CoW") {
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x01, 0x02, 0x03}));
        AncPacket b = a;
        b.setData(makeWireBytes({0x04, 0x05}));
        CHECK(a.data().size() == 3);
        CHECK(b.data().size() == 2);
}

TEST_CASE("AncPacket: setMeta detaches CoW") {
        Metadata m1;
        m1.set(AncMeta::St291::Line, uint16_t(11));
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x01}), m1);
        AncPacket b = a;

        Metadata m2;
        m2.set(AncMeta::St291::Line, uint16_t(22));
        b.setMeta(m2);

        CHECK(a.meta().get(AncMeta::St291::Line).get<uint16_t>() == 11);
        CHECK(b.meta().get(AncMeta::St291::Line).get<uint16_t>() == 22);
}

TEST_CASE("AncPacket: metaMut detaches CoW") {
        Metadata m;
        m.set(AncMeta::St291::Line, uint16_t(11));
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x01}), m);
        AncPacket b = a;
        b.metaMut().set(AncMeta::St291::Line, uint16_t(99));
        CHECK(a.meta().get(AncMeta::St291::Line).get<uint16_t>() == 11);
        CHECK(b.meta().get(AncMeta::St291::Line).get<uint16_t>() == 99);
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("AncPacket: equality compares all four fields") {
        // Buffer equality is identity-based (operator== compares the
        // underlying impl pointer), so all "equal" cases must share the
        // same Buffer instance.  Buffer copies do that for free since
        // they refcount-share the same impl.
        Buffer    b1 = makeWireBytes({0x01, 0x02, 0x03});
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, b1);
        AncPacket b(AncFormat(AncFormat::Cea708), AncTransport::St291, b1);
        CHECK(a == b);

        AncPacket c(AncFormat(AncFormat::Cea608), AncTransport::St291, b1);
        CHECK(a != c);

        AncPacket d(AncFormat(AncFormat::Cea708), AncTransport::NdiXml, b1);
        CHECK(a != d);

        // Different Buffer (different impl) — packets compare unequal
        // even though the bytes happen to match.
        AncPacket e(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x01, 0x02, 0x03}));
        CHECK(a != e);

        Metadata withMeta;
        withMeta.set(AncMeta::St291::Line, uint16_t(11));
        AncPacket f(AncFormat(AncFormat::Cea708), AncTransport::St291, b1, withMeta);
        CHECK(a != f);
}

// ============================================================================
// toString
// ============================================================================

TEST_CASE("AncPacket: toString summarises format/transport/byte-count") {
        AncPacket pkt(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x01, 0x02, 0x03}));
        String    s = pkt.toString();
        CHECK(s.contains(String("Cea708")));
        CHECK(s.contains(String("St291")));
        CHECK(s.contains(String("3 bytes")));
}

TEST_CASE("AncPacket: toString invalid handle") {
        AncPacket pkt;
        String    s = pkt.toString();
        CHECK(s.contains(String("Invalid")));
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("AncPacket: DataStream round-trip preserves all four fields") {
        Buffer   bytes = makeWireBytes({0x61, 0x01, 0x05, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE});
        Metadata meta;
        meta.set(AncMeta::St291::Line, uint16_t(11));
        meta.set(AncMeta::St291::FieldB, true);
        AncPacket original(AncFormat(AncFormat::Cea708), AncTransport::St291, bytes, meta);

        Buffer         storage(8192);
        BufferIODevice dev(&storage);
        dev.open(IODevice::ReadWrite);

        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << original;
                REQUIRE(writer.status() == DataStream::Ok);
        }

        dev.seek(0);
        AncPacket round;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> round;
                REQUIRE(reader.status() == DataStream::Ok);
        }
        // Buffer equality is identity-based, so operator== on the
        // packets won't fire even on a byte-perfect round-trip.
        // Compare every field individually.
        CHECK(round.format() == original.format());
        CHECK(round.transport() == original.transport());
        CHECK(round.data().size() == original.data().size());
        const uint8_t *roundBytes = static_cast<const uint8_t *>(round.data().data());
        const uint8_t *origBytes = static_cast<const uint8_t *>(original.data().data());
        REQUIRE(roundBytes != nullptr);
        REQUIRE(origBytes != nullptr);
        for (size_t i = 0; i < original.data().size(); ++i) {
                CHECK(roundBytes[i] == origBytes[i]);
        }
        CHECK(round.meta() == original.meta());
}

