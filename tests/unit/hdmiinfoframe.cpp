/**
 * @file      hdmiinfoframe.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/hdmiinfoframe.h>
#include <promeki/ancmeta.h>

using namespace promeki;

// Helper: build a Buffer with the supplied bytes and a stamped logical
// content size.
static Buffer makeBytes(std::initializer_list<uint8_t> bytes) {
        Buffer b(bytes.size());
        Error  e = b.copyFrom(bytes.begin(), bytes.size());
        REQUIRE(e.isOk());
        b.setSize(bytes.size());
        return b;
}

// ============================================================================
// build()
// ============================================================================

TEST_CASE("HdmiInfoFrame: build AVI InfoFrame from AncFormat::AviInfoFrame") {
        // Synthetic AVI InfoFrame payload — 13 bytes is the standard
        // CEA-861 AVI body size; content here is arbitrary.
        Buffer body =
                makeBytes({0x10, 0x68, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::AviInfoFrame), /*version*/ 2, body);
        CHECK(f.isValid());
        CHECK(f.type() == 0x82);   // AVI InfoFrame type byte
        CHECK(f.version() == 2);
        CHECK(f.length() == 13);
        CHECK(f.checksumValid());
        CHECK(f.packet().format().id() == AncFormat::AviInfoFrame);
        CHECK(f.packet().transport() == AncTransport::HdmiInfoFrame);
}

TEST_CASE("HdmiInfoFrame: build DRM (HDR static) InfoFrame") {
        Buffer body = makeBytes({0x01, 0x00, 0x00, 0x00, 0x00});
        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::HdrStatic2086), 1, body);
        CHECK(f.type() == 0x87);
        CHECK(f.version() == 1);
        CHECK(f.length() == 5);
        CHECK(f.checksumValid());
}

TEST_CASE("HdmiInfoFrame: build Audio InfoFrame") {
        Buffer body = makeBytes({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A});
        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::AudioInfoFrame), 1, body);
        CHECK(f.type() == 0x84);
        CHECK(f.checksumValid());
}

TEST_CASE("HdmiInfoFrame: build SPD InfoFrame") {
        Buffer body = makeBytes({0x6D, 0x65, 0x6B, 0x69, 0x00}); // "meki\0"
        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::SpdInfoFrame), 1, body);
        CHECK(f.type() == 0x83);
        CHECK(f.checksumValid());
}

// ============================================================================
// buildRaw() — escape hatch
// ============================================================================

TEST_CASE("HdmiInfoFrame: buildRaw allows any type byte") {
        Buffer        body = makeBytes({0xDE, 0xAD, 0xBE, 0xEF});
        HdmiInfoFrame f = HdmiInfoFrame::buildRaw(0xA5, 7, body);
        CHECK(f.type() == 0xA5);
        CHECK(f.version() == 7);
        CHECK(f.length() == 4);
        CHECK(f.checksumValid());
        // No registered format for 0xA5 — packet wrapper format is Invalid.
        CHECK_FALSE(f.packet().format().isValid());
}

TEST_CASE("HdmiInfoFrame: buildRaw with type 0x81 resolves to VendorInfoFrame catch-all") {
        Buffer        body = makeBytes({0x90, 0x84, 0x00});
        HdmiInfoFrame f = HdmiInfoFrame::buildRaw(0x81, 1, body);
        CHECK(f.type() == 0x81);
        CHECK(f.packet().format().id() == AncFormat::VendorInfoFrame);
}

// ============================================================================
// from()
// ============================================================================

TEST_CASE("HdmiInfoFrame: from() succeeds for an HdmiInfoFrame transport packet") {
        Buffer        body = makeBytes({0x10, 0x68, 0x00});
        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::AviInfoFrame), 2, body);

        Result<HdmiInfoFrame> r = HdmiInfoFrame::from(f.packet());
        REQUIRE(isOk(r));
        CHECK(value(r).type() == 0x82);
}

TEST_CASE("HdmiInfoFrame: from() rejects wrong transport") {
        Buffer    pkt = makeBytes({0x82, 0x02, 0x01, 0x00, 0xAA});
        AncPacket p(AncFormat(AncFormat::AviInfoFrame), AncTransport::St291, pkt);
        Result<HdmiInfoFrame> r = HdmiInfoFrame::from(p);
        CHECK(isError(r));
        CHECK(error(r) == Error::InvalidArgument);
}

TEST_CASE("HdmiInfoFrame: from() rejects too-short data") {
        // 2 bytes < HeaderSize (4).
        Buffer    tiny = makeBytes({0x82, 0x02});
        AncPacket p(AncFormat(AncFormat::AviInfoFrame), AncTransport::HdmiInfoFrame, tiny);
        Result<HdmiInfoFrame> r = HdmiInfoFrame::from(p);
        CHECK(isError(r));
        CHECK(error(r) == Error::InvalidArgument);
}

// ============================================================================
// body() round-trip
// ============================================================================

TEST_CASE("HdmiInfoFrame: body() returns the bytes after the 4-byte header") {
        Buffer        original = makeBytes({0x01, 0x02, 0x03, 0x04, 0x05});
        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::SpdInfoFrame), 1, original);

        Buffer back = f.body();
        REQUIRE(back.size() == 5);
        const uint8_t *p = static_cast<const uint8_t *>(back.data());
        REQUIRE(p != nullptr);
        CHECK(p[0] == 0x01);
        CHECK(p[1] == 0x02);
        CHECK(p[2] == 0x03);
        CHECK(p[3] == 0x04);
        CHECK(p[4] == 0x05);
}

TEST_CASE("HdmiInfoFrame: body() is empty when length is zero") {
        Buffer        empty;
        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::SpdInfoFrame), 1, empty);
        CHECK(f.length() == 0);
        CHECK(f.body().size() == 0);
        CHECK(f.checksumValid());
}

// ============================================================================
// Checksum
// ============================================================================

TEST_CASE("HdmiInfoFrame: stored checksum makes mod-256 sum of all bytes zero") {
        Buffer        body = makeBytes({0xFF, 0xFE, 0xFD, 0xFC});
        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::AviInfoFrame), 2, body);

        // Manually sum every byte from the underlying packet's data.
        const uint8_t *raw = static_cast<const uint8_t *>(f.packet().data().data());
        REQUIRE(raw != nullptr);
        uint8_t sum = 0;
        for (size_t i = 0; i < f.packet().data().size(); ++i) sum = static_cast<uint8_t>(sum + raw[i]);
        CHECK(sum == 0);
}

// ============================================================================
// Implicit decay
// ============================================================================

TEST_CASE("HdmiInfoFrame: implicit decay to const AncPacket&") {
        Buffer        body = makeBytes({0x10, 0x20, 0x30});
        HdmiInfoFrame f = HdmiInfoFrame::build(AncFormat(AncFormat::AudioInfoFrame), 1, body);

        auto inspect = [](const AncPacket &p) { return p.transport() == AncTransport::HdmiInfoFrame; };
        CHECK(inspect(f));

        AncPacket::List list;
        list.pushToBack(f);
        CHECK(list.size() == 1);
}
