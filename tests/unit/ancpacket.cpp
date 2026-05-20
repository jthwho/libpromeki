/**
 * @file      ancpacket.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancpacket.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>
#include <promeki/metadata.h>

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
        AncPacket pkt(AncFormat(AncFormat::Cea708), AncTransport::St291, bytes);
        pkt.setSt291Line(11);
        pkt.setSt291HOffset(0);

        CHECK(pkt.isValid());
        CHECK(pkt.format().id() == AncFormat::Cea708);
        CHECK(pkt.transport() == AncTransport::St291);
        CHECK(pkt.data().size() == 8);
        CHECK(pkt.st291Line() == 11);
        CHECK(pkt.st291HOffset() == 0);
}

TEST_CASE("AncPacket: full-state constructor with default meta") {
        Buffer    bytes = makeWireBytes({0x60, 0x60, 0x00});
        AncPacket pkt(AncFormat(AncFormat::AtcLtc), AncTransport::St291, bytes);
        CHECK(pkt.isValid());
        CHECK(pkt.meta().isEmpty());
        // Default ST 291 framing sentinels per RFC 8331 §2.2.
        CHECK(pkt.st291Line() == 0x7FE);
        CHECK(pkt.st291HOffset() == 0xFFF);
        CHECK(pkt.st291FieldB() == false);
        CHECK(pkt.st291CBit() == false);
        CHECK(pkt.st291StreamNum() == 0);
}

TEST_CASE("AncPacket: ST 291 framing accessors round-trip") {
        AncPacket pkt(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x61, 0x01, 0x00}));
        pkt.setSt291Line(11);
        pkt.setSt291HOffset(0x42);
        pkt.setSt291FieldB(true);
        pkt.setSt291CBit(true);
        pkt.setSt291StreamNum(3);
        CHECK(pkt.st291Line() == 11);
        CHECK(pkt.st291HOffset() == 0x42);
        CHECK(pkt.st291FieldB() == true);
        CHECK(pkt.st291CBit() == true);
        CHECK(pkt.st291StreamNum() == 3);
}

TEST_CASE("AncPacket: ST 291 setters CoW-detach") {
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x61, 0x01, 0x00}));
        a.setSt291Line(11);
        AncPacket b = a;
        b.setSt291Line(99);
        CHECK(a.st291Line() == 11);
        CHECK(b.st291Line() == 99);
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
        b.setFormat(AncFormat(AncFormat::PanScan));
        CHECK(a.format().id() == AncFormat::Afd);
        CHECK(b.format().id() == AncFormat::PanScan);
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
        m1.set(Metadata::Title, String("alpha"));
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x01}), m1);
        AncPacket b = a;

        Metadata m2;
        m2.set(Metadata::Title, String("beta"));
        b.setMeta(m2);

        CHECK(a.meta().get(Metadata::Title).get<String>() == String("alpha"));
        CHECK(b.meta().get(Metadata::Title).get<String>() == String("beta"));
}

TEST_CASE("AncPacket: metaMut detaches CoW") {
        Metadata m;
        m.set(Metadata::Title, String("alpha"));
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x01}), m);
        AncPacket b = a;
        b.metaMut().set(Metadata::Title, String("beta"));
        CHECK(a.meta().get(Metadata::Title).get<String>() == String("alpha"));
        CHECK(b.meta().get(Metadata::Title).get<String>() == String("beta"));
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("AncPacket: equality compares all four fields") {
        // Buffer equality is value-based: identity short-circuits when
        // the impls coincide (refcount sharing — the common CoW case)
        // and byte-content compares otherwise.
        Buffer    b1 = makeWireBytes({0x01, 0x02, 0x03});
        AncPacket a(AncFormat(AncFormat::Cea708), AncTransport::St291, b1);
        AncPacket b(AncFormat(AncFormat::Cea708), AncTransport::St291, b1);
        CHECK(a == b);

        AncPacket c(AncFormat(AncFormat::Cea608), AncTransport::St291, b1);
        CHECK(a != c);

        AncPacket d(AncFormat(AncFormat::Cea708), AncTransport::NdiXml, b1);
        CHECK(a != d);

        // Different Buffer impl but identical bytes — packets compare
        // equal under Buffer's value-equality semantics.
        AncPacket e(AncFormat(AncFormat::Cea708), AncTransport::St291, makeWireBytes({0x01, 0x02, 0x03}));
        CHECK(a == e);

        // Different bytes — packets compare unequal.
        AncPacket eDiff(AncFormat(AncFormat::Cea708), AncTransport::St291,
                        makeWireBytes({0x01, 0x02, 0x04}));
        CHECK(a != eDiff);

        // Two packets that differ only on a direct ST 291 framing
        // field compare unequal (line is part of the equality contract
        // now that it lives on the Impl).
        AncPacket f(AncFormat(AncFormat::Cea708), AncTransport::St291, b1);
        f.setSt291Line(11);
        CHECK(a != f);

        // Differing application metadata also breaks equality.
        Metadata withMeta;
        withMeta.set(Metadata::Title, String("annotated"));
        AncPacket g(AncFormat(AncFormat::Cea708), AncTransport::St291, b1, withMeta);
        CHECK(a != g);
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
        meta.set(Metadata::Title, String("captured-anc"));
        AncPacket original(AncFormat(AncFormat::Cea708), AncTransport::St291, bytes, meta);
        original.setSt291Line(11);
        original.setSt291FieldB(true);
        original.setSt291StreamNum(2);

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
        // Buffer equality is now value-based, so operator== on the
        // packets compares byte-perfect.  Keep the per-field checks
        // below for richer diagnostic output on mismatch.
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
        CHECK(round.st291Line() == original.st291Line());
        CHECK(round.st291HOffset() == original.st291HOffset());
        CHECK(round.st291FieldB() == original.st291FieldB());
        CHECK(round.st291CBit() == original.st291CBit());
        CHECK(round.st291StreamNum() == original.st291StreamNum());
        CHECK(round == original);
}

