/**
 * @file      cea708cdp.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/bufferiodevice.h>
#include <promeki/cea708cdp.h>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/result.h>
#include <promeki/variant.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("Cea708Cdp: default-constructed is empty and round-trippable") {
        Cea708Cdp cdp;
        CHECK(cdp.frameRateCode == 0);
        CHECK_FALSE(cdp.timeCodePresent);
        CHECK_FALSE(cdp.ccDataPresent);
        CHECK_FALSE(cdp.svcInfoPresent);
        CHECK_FALSE(cdp.captionServiceActive);
        CHECK(cdp.ccData.size() == 0);

        Buffer            wire = cdp.toBuffer();
        // Minimum CDP = 7-byte header + 4-byte footer = 11 bytes.
        CHECK(wire.size() == 11);

        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first() == cdp);
}

// ============================================================================
// cc_data round-trip
// ============================================================================

TEST_CASE("Cea708Cdp: round-trip a CDP carrying 4 cc_data triples") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 4; // 29.97
        cdp.ccDataPresent = true;
        cdp.captionServiceActive = true;
        cdp.sequenceCounter = 0x1234;

        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 0x94, 0x20});
        triples.pushToBack({true, 1, 0x91, 0x52});
        triples.pushToBack({false, 2, 0x00, 0x00});
        triples.pushToBack({true, 3, 0xC0, 0x80});
        cdp.ccData = triples;

        Buffer            wire = cdp.toBuffer();
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const Cea708Cdp &out = r.first();
        CHECK(out.frameRateCode == 4);
        CHECK(out.ccDataPresent);
        CHECK(out.captionServiceActive);
        CHECK(out.sequenceCounter == 0x1234);
        CHECK(out.ccData.size() == 4);
        CHECK(out.ccData[0] == Cea708Cdp::CcData{true, 0, 0x94, 0x20});
        CHECK(out.ccData[1] == Cea708Cdp::CcData{true, 1, 0x91, 0x52});
        CHECK(out.ccData[2] == Cea708Cdp::CcData{false, 2, 0x00, 0x00});
        CHECK(out.ccData[3] == Cea708Cdp::CcData{true, 3, 0xC0, 0x80});
}

TEST_CASE("Cea708Cdp: convenience constructor sets ccDataPresent + active") {
        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 'h' | 0x80, 'i' | 0x80});
        Cea708Cdp cdp(4, triples, 0x55);
        CHECK(cdp.frameRateCode == 4);
        CHECK(cdp.ccDataPresent);
        CHECK(cdp.captionServiceActive);
        CHECK(cdp.sequenceCounter == 0x55);
        CHECK(cdp.ccData.size() == 1);
}

// ============================================================================
// Timecode section
// ============================================================================

TEST_CASE("Cea708Cdp: round-trip CDP with timecode section") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 5; // 30
        cdp.timeCodePresent = true;
        cdp.timeCode = Timecode(Timecode::Mode(Timecode::NDF30), 12, 34, 56, 7);
        cdp.sequenceCounter = 0xABCD;

        Buffer            wire = cdp.toBuffer();
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const Cea708Cdp &out = r.first();
        CHECK(out.timeCodePresent);
        CHECK(out.timeCode.hour() == 12);
        CHECK(out.timeCode.min() == 34);
        CHECK(out.timeCode.sec() == 56);
        CHECK(out.timeCode.frame() == 7);
}

// ============================================================================
// Checksum / structural validation
// ============================================================================

TEST_CASE("Cea708Cdp: toBuffer stamps a packet_checksum that sums to zero") {
        Cea708Cdp cdp(4, {}, 0x42);
        Buffer    wire = cdp.toBuffer();
        // Every CDP byte sum mod 256 must be zero (CEA-708 §4.4.1).
        const uint8_t *p = static_cast<const uint8_t *>(wire.data());
        uint32_t sum = 0;
        for (size_t i = 0; i < wire.size(); ++i) sum += p[i];
        CHECK((sum & 0xFF) == 0);
}

TEST_CASE("Cea708Cdp: fromBuffer rejects bad identifier") {
        Cea708Cdp cdp;
        Buffer    wire = cdp.toBuffer();
        uint8_t  *p = static_cast<uint8_t *>(wire.data());
        p[0] = 0x00; // corrupt magic
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("Cea708Cdp: fromBuffer rejects mismatched length") {
        Cea708Cdp cdp;
        Buffer    wire = cdp.toBuffer();
        uint8_t  *p = static_cast<uint8_t *>(wire.data());
        p[2] = static_cast<uint8_t>(wire.size() + 1); // lie about length
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("Cea708Cdp: fromBuffer rejects bad checksum") {
        Cea708Cdp cdp;
        Buffer    wire = cdp.toBuffer();
        uint8_t  *p = static_cast<uint8_t *>(wire.data());
        // Flip a bit in the last byte to break the mod-256 zero invariant.
        p[wire.size() - 1] = static_cast<uint8_t>(p[wire.size() - 1] ^ 0x01);
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("Cea708Cdp: fromBuffer rejects mismatched footer sequence") {
        Cea708Cdp cdp;
        cdp.sequenceCounter = 0x1234;
        Buffer wire = cdp.toBuffer();
        // Find footer sequence at offset (size - 3, size - 2) and corrupt it.
        // The packet's checksum is also affected, so fix the checksum first
        // (we want to specifically test footer sequence mismatch).
        uint8_t *p = static_cast<uint8_t *>(wire.data());
        size_t   sz = wire.size();
        p[sz - 3] = 0xAA;
        p[sz - 2] = 0xBB;
        uint32_t sum = 0;
        for (size_t i = 0; i < sz - 1; ++i) sum += p[i];
        p[sz - 1] = static_cast<uint8_t>((0x100 - (sum & 0xFF)) & 0xFF);
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        CHECK(r.second().code() == Error::CorruptData);
}

// ============================================================================
// JSON / toString
// ============================================================================

TEST_CASE("Cea708Cdp: toJson produces structured output") {
        Cea708Cdp cdp(4, {}, 0x10);
        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 0x94, 0x20});
        cdp.ccData = triples;
        cdp.ccDataPresent = true;

        JsonObject obj = cdp.toJson();
        Error      err;
        CHECK(obj.getInt("frameRateCode", &err) == 4);
        CHECK(obj.getInt("sequenceCounter", &err) == 0x10);
        CHECK(obj.getBool("ccDataPresent", &err) == true);
        JsonArray arr = obj.getArray("ccData", &err);
        CHECK(err.isOk());
        CHECK(arr.size() == 1);
}

TEST_CASE("Cea708Cdp: toString includes seq + cc count") {
        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 0x94, 0x20});
        triples.pushToBack({true, 0, 'h' | 0x80, 'i' | 0x80});
        Cea708Cdp cdp(4, triples, 7);
        String    s = cdp.toString();
        CHECK(s.contains("seq=7"));
        CHECK(s.contains("cc=2"));
}

// ============================================================================
// Variant integration
// ============================================================================

TEST_CASE("Cea708Cdp: round-trips through Variant") {
        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 0x91, 0x20});
        Cea708Cdp original(4, triples, 0x77);
        original.ccData = triples;
        original.ccDataPresent = true;

        Variant   v;
        v.set(original);
        CHECK(v.type() == Variant::TypeCea708Cdp);
        Cea708Cdp out = v.get<Cea708Cdp>();
        CHECK(out == original);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("Cea708Cdp: DataStream operators round-trip") {
        Cea708Cdp::CcDataList triples;
        triples.pushToBack({true, 0, 0x94, 0x20});
        triples.pushToBack({true, 1, 0x91, 0x52});
        Cea708Cdp original(4, triples, 0xABCD);
        original.ccData = triples;
        original.ccDataPresent = true;

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        Cea708Cdp restored;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> restored;
        }
        CHECK(restored == original);
}

// ============================================================================
// Opaque extra bytes round-trip
// ============================================================================

TEST_CASE("Cea708Cdp: extra opaque bytes between cc_data and footer round-trip") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 4;
        cdp.svcInfoPresent = true; // signal that opaque section was present
        cdp.svcInfoStart = true;
        // Synthesise a fake svcinfo body the parser will treat as opaque
        // (this codec doesn't model svcinfo internals; it preserves bytes
        // verbatim so captured packets round-trip without losing fidelity).
        Buffer  extras(8);
        extras.setSize(8);
        uint8_t bytes[8] = {Cea708Cdp::CcSvcInfoSectionId, 0x01, 0x02, 0x03,
                            0x04, 0x05, 0x06, 0x07};
        extras.copyFrom(bytes, sizeof(bytes), 0);
        cdp.extraBytes = extras;
        cdp.sequenceCounter = 0x99;

        Buffer            wire = cdp.toBuffer();
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const Cea708Cdp &out = r.first();
        CHECK(out.svcInfoPresent);
        CHECK(out.extraBytes.size() == 8);
        const uint8_t *p = static_cast<const uint8_t *>(out.extraBytes.data());
        for (size_t i = 0; i < 8; ++i) CHECK(p[i] == bytes[i]);
}
