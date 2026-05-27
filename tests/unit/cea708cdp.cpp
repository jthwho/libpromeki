/**
 * @file      cea708cdp.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
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

// ----------------------------------------------------------------------------
// F2 — ST 334-2:2015 §5.3 Table 4 wire-format correctness.
// ----------------------------------------------------------------------------

// Helper: locate the time-code section payload (5 bytes including section
// ID) inside a built CDP wire buffer.  Returns nullptr if absent.
static const uint8_t *findTimecodeSection(const Buffer &wire) {
        const uint8_t *p = static_cast<const uint8_t *>(wire.data());
        // The CDP header is 7 bytes; if time_code_present is set, the
        // time-code section starts at offset 7 (the time-code section
        // precedes cc_data / extras / footer).
        if (wire.size() < 7 + 5) return nullptr;
        if (p[7] != Cea708Cdp::TimeCodeSectionId) return nullptr;
        return p + 7;
}

TEST_CASE("Cea708Cdp: timecode section is emitted in H/M/S/F byte order (ST 334-2 §5.3)") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 5; // 30
        cdp.timeCodePresent = true;
        cdp.timeCode = Timecode(Timecode::Mode(Timecode::NDF30), 12, 34, 56, 7);

        Buffer wire = cdp.toBuffer();
        const uint8_t *tc = findTimecodeSection(wire);
        REQUIRE(tc != nullptr);

        // byte 1 = hours: reserved '11' in bits 7-6, tc_10hrs in bits 5-4
        // (= 1), tc_1hrs in bits 3-0 (= 2).  Expected: 0xC0 | 0x12 = 0xD2.
        CHECK(tc[1] == 0xD2);
        // byte 2 = minutes: reserved '1' in bit 7, tc_10min in bits 6-4
        // (= 3), tc_1min in bits 3-0 (= 4).  Expected: 0x80 | 0x34 = 0xB4.
        CHECK(tc[2] == 0xB4);
        // byte 3 = seconds: tc_field_flag in bit 7 (= 0), tc_10sec in
        // bits 6-4 (= 5), tc_1sec in bits 3-0 (= 6).  Expected: 0x56.
        CHECK(tc[3] == 0x56);
        // byte 4 = frames: drop_frame_flag in bit 7 (= 0), reserved '0'
        // in bit 6, tc_10fr in bits 5-4 (= 0), tc_1fr in bits 3-0 (= 7).
        // Expected: 0x07.
        CHECK(tc[4] == 0x07);
}

TEST_CASE("Cea708Cdp: timecode section reserved bits are '11' / '1' (ST 334-2 §5.3 Table 4)") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 5;
        cdp.timeCodePresent = true;
        // Pick digits whose BCD encoding has zero in the high bits of the
        // hours / minutes bytes, so the reserved-bit check isolates the
        // stamped '1' bits from the digit data.
        cdp.timeCode = Timecode(Timecode::Mode(Timecode::NDF30), 0, 0, 0, 0);

        Buffer wire = cdp.toBuffer();
        const uint8_t *tc = findTimecodeSection(wire);
        REQUIRE(tc != nullptr);

        // Hours byte bits 7-6 must be '11'.
        CHECK((tc[1] & 0xC0) == 0xC0);
        // Minutes byte bit 7 must be '1'.
        CHECK((tc[2] & 0x80) == 0x80);
        // Frames byte bit 6 must be '0' (reserved zero, not a flag).
        CHECK((tc[4] & 0x40) == 0x00);
}

TEST_CASE("Cea708Cdp: drop_frame_flag propagates from Timecode mode (ST 334-2 §5.3)") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 4; // 29.97
        cdp.timeCodePresent = true;
        cdp.timeCode = Timecode(Timecode::Mode(Timecode::DF30), 1, 0, 0, 0);

        Buffer wire = cdp.toBuffer();
        const uint8_t *tc = findTimecodeSection(wire);
        REQUIRE(tc != nullptr);
        // byte 4 bit 7 = drop_frame_flag; must be set for DF30 mode.
        CHECK((tc[4] & 0x80) == 0x80);

        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first().timeCode.isDropFrame());
}

TEST_CASE("Cea708Cdp: drop_frame_flag clear for NDF Timecode") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 5; // 30
        cdp.timeCodePresent = true;
        cdp.timeCode = Timecode(Timecode::Mode(Timecode::NDF30), 1, 0, 0, 0);

        Buffer wire = cdp.toBuffer();
        const uint8_t *tc = findTimecodeSection(wire);
        REQUIRE(tc != nullptr);
        CHECK((tc[4] & 0x80) == 0x00);

        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK_FALSE(r.first().timeCode.isDropFrame());
}

TEST_CASE("Cea708Cdp: tc_field_flag round-trips through wire") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 4;
        cdp.timeCodePresent = true;
        cdp.timeCode = Timecode(Timecode::Mode(Timecode::DF30), 1, 0, 0, 0);
        cdp.tcFieldFlag = true;

        Buffer wire = cdp.toBuffer();
        const uint8_t *tc = findTimecodeSection(wire);
        REQUIRE(tc != nullptr);
        // byte 3 bit 7 = tc_field_flag.
        CHECK((tc[3] & 0x80) == 0x80);

        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first().tcFieldFlag);
}

TEST_CASE("Cea708Cdp: tc_field_flag defaults to false and emits zero bit") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 5;
        cdp.timeCodePresent = true;
        cdp.timeCode = Timecode(Timecode::Mode(Timecode::NDF30), 0, 0, 0, 0);
        CHECK_FALSE(cdp.tcFieldFlag);

        Buffer wire = cdp.toBuffer();
        const uint8_t *tc = findTimecodeSection(wire);
        REQUIRE(tc != nullptr);
        CHECK((tc[3] & 0x80) == 0x00);
}

TEST_CASE("Cea708Cdp: parse resolves Timecode::Mode from frameRateCode (ST 334-2 Table 3)") {
        struct Case {
                uint8_t  code;
                bool     dropFrame;
                uint32_t expectedFps;
                bool     expectedDf;
        };
        const Case cases[] = {
                {1, false, 24, false}, // 23.976
                {2, false, 24, false},
                {3, false, 25, false},
                {4, true, 30, true}, // 29.97 DF
                {4, false, 30, false}, // 29.97 NDF
                {5, false, 30, false}, // 30 NDF
                {5, true, 30, true}, // 30 DF
                {6, false, 50, false},
                {7, true, 60, true}, // 59.94 DF
                {7, false, 60, false}, // 59.94 NDF
                {8, false, 60, false},
        };

        for (const Case &c : cases) {
                Cea708Cdp cdp;
                cdp.frameRateCode = c.code;
                cdp.timeCodePresent = true;
                // Drop the DF flag straight into the wire by hand —
                // building from a Timecode requires a libvtc format that
                // matches, but here we just want to feed parse the bit.
                Buffer wire = cdp.toBuffer();
                uint8_t *p = static_cast<uint8_t *>(wire.data());
                if (c.dropFrame) {
                        p[7 + 4] |= 0x80; // set drop_frame_flag bit
                        // Recompute the packet checksum.
                        size_t   sz = wire.size();
                        uint32_t sum = 0;
                        for (size_t i = 0; i < sz - 1; ++i) sum += p[i];
                        p[sz - 1] = static_cast<uint8_t>((0x100 - (sum & 0xFF)) & 0xFF);
                }
                Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
                REQUIRE(r.second().isOk());
                CHECK(r.first().timeCode.fps() == c.expectedFps);
                CHECK(r.first().timeCode.isDropFrame() == c.expectedDf);
        }
}

TEST_CASE("Cea708Cdp: parse marks Mode invalid for unknown frame rate code") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 0; // reserved / unknown
        cdp.timeCodePresent = true;
        Buffer            wire = cdp.toBuffer();
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK_FALSE(r.first().timeCode.isValid());
}

TEST_CASE("Cea708Cdp: full timecode round-trip preserves digits + mode + flags") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 4;
        cdp.timeCodePresent = true;
        cdp.timeCode = Timecode(Timecode::Mode(Timecode::DF30), 1, 2, 3, 4);
        cdp.tcFieldFlag = true;

        Buffer            wire = cdp.toBuffer();
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first() == cdp);
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
        CHECK(v.type() == DataTypeCea708Cdp);
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

TEST_CASE("Cea708Cdp: future_section bytes between cc_data and footer round-trip") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 4;
        // SMPTE 334-2 §5.7: future sections use IDs in 0x75..0xEF and
        // must be passed through by receivers that don't understand
        // them.  This codec preserves the bytes verbatim via extraBytes
        // so captured packets round-trip without losing fidelity, even
        // when the section ID is unknown to the library.
        Buffer  extras(8);
        extras.setSize(8);
        uint8_t bytes[8] = {0x80 /* future_section_id */, 0x06 /* length */, 0xAA, 0xBB,
                            0xCC, 0xDD, 0xEE, 0xFF};
        extras.copyFrom(bytes, sizeof(bytes), 0);
        cdp.extraBytes = extras;
        cdp.sequenceCounter = 0x99;

        Buffer            wire = cdp.toBuffer();
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const Cea708Cdp &out = r.first();
        CHECK(out.extraBytes.size() == 8);
        const uint8_t *p = static_cast<const uint8_t *>(out.extraBytes.data());
        for (size_t i = 0; i < 8; ++i) CHECK(p[i] == bytes[i]);
}

TEST_CASE("Cea708Cdp: ccsvcinfo_section structured round-trip (SMPTE 334-2 §5.5)") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 4;
        cdp.svcInfoPresent = true;
        cdp.svcInfoStart = true;
        cdp.svcInfoComplete = true;
        // Two services: one CEA-608 line-21 (field 1), one CEA-708 DTVCC.
        Cea708Cdp::CcSvcInfoEntry s1;
        s1.csnSize5Bit = false;     // 6-bit csn → '0'
        s1.captionServiceNumber = 0; // line-21 has csn=0 per §5.5
        s1.languageCode[0] = 'e';
        s1.languageCode[1] = 'n';
        s1.languageCode[2] = 'g';
        s1.digitalCc = false;
        s1.line21Field = false;
        s1.easyReader = false;
        s1.wideAspect = true;
        Cea708Cdp::CcSvcInfoEntry s2;
        s2.csnSize5Bit = true;       // 5-bit csn → '1'
        s2.captionServiceNumber = 3; // DTVCC service 3
        s2.languageCode[0] = 's';
        s2.languageCode[1] = 'p';
        s2.languageCode[2] = 'a';
        s2.digitalCc = true;
        s2.line21Field = false;      // ignored for DTVCC services
        s2.easyReader = true;
        s2.wideAspect = true;
        cdp.ccSvcInfo.pushToBack(s1);
        cdp.ccSvcInfo.pushToBack(s2);
        cdp.sequenceCounter = 0xABCD;

        Buffer            wire = cdp.toBuffer();
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const Cea708Cdp &out = r.first();
        REQUIRE(out.svcInfoPresent);
        CHECK(out.svcInfoStart);
        CHECK(out.svcInfoComplete);
        CHECK_FALSE(out.svcInfoChange);
        REQUIRE(out.ccSvcInfo.size() == 2);
        CHECK(out.ccSvcInfo[0] == s1);
        CHECK(out.ccSvcInfo[1] == s2);
        // No future_section bytes left over.
        CHECK(out.extraBytes.size() == 0);
}

TEST_CASE("Cea708Cdp: ccsvcinfo + future_section coexist in same CDP") {
        Cea708Cdp cdp;
        cdp.frameRateCode = 5;
        cdp.svcInfoPresent = true;
        cdp.svcInfoComplete = true;
        Cea708Cdp::CcSvcInfoEntry e;
        e.csnSize5Bit = true;
        e.captionServiceNumber = 1;
        e.languageCode[0] = 'e';
        e.languageCode[1] = 'n';
        e.languageCode[2] = 'g';
        e.digitalCc = true;
        e.easyReader = false;
        e.wideAspect = false;
        cdp.ccSvcInfo.pushToBack(e);
        // Add a future_section with a private ID 0x90 + 3 bytes.
        Buffer  fut(5);
        fut.setSize(5);
        uint8_t bytes[5] = {0x90 /* future_section_id */, 0x03 /* length */, 0x11, 0x22, 0x33};
        fut.copyFrom(bytes, sizeof(bytes), 0);
        cdp.extraBytes = fut;
        cdp.sequenceCounter = 1;

        Buffer            wire = cdp.toBuffer();
        Result<Cea708Cdp> r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const Cea708Cdp &out = r.first();
        REQUIRE(out.ccSvcInfo.size() == 1);
        CHECK(out.ccSvcInfo[0].languageCode[0] == 'e');
        CHECK(out.ccSvcInfo[0].digitalCc);
        REQUIRE(out.extraBytes.size() == 5);
        const uint8_t *p = static_cast<const uint8_t *>(out.extraBytes.data());
        for (size_t i = 0; i < 5; ++i) CHECK(p[i] == bytes[i]);
}

TEST_CASE("Cea708Cdp: svcInfoMismatches counts entry-flag vs svc_data_byte_4 disagreement") {
        // Build a CDP whose svcinfo entry's entry-flag says service 3
        // but whose svc_data_byte_4 says service 5 — a non-compliant
        // encoder that our parser must tolerate but tally.  We assemble
        // the bytes manually to bypass our own encoder (which always
        // writes both fields from the same source).
        Cea708Cdp cdp;
        cdp.frameRateCode = 4;     // 29.97
        cdp.sequenceCounter = 0;
        cdp.svcInfoPresent = true;
        cdp.svcInfoStart = true;
        cdp.svcInfoComplete = true;
        Cea708Cdp::CcSvcInfoEntry e;
        e.csnSize5Bit = false;
        e.captionServiceNumber = 3; // claimed in entry-flag
        e.languageCode[0] = 'e';
        e.languageCode[1] = 'n';
        e.languageCode[2] = 'g';
        e.digitalCc = true;
        cdp.ccSvcInfo.pushToBack(e);
        // Serialize the compliant form first…
        Buffer wire = cdp.toBuffer();
        // …then patch svc_data_byte_4 to disagree.  Locate it via
        // structure: svc info section starts at offset = header (7) +
        // ccData section (if present — not in this CDP) + 2 (svcinfo
        // header), then svcPos+4 is byte 4 of entry 0.
        uint8_t *bytes = static_cast<uint8_t *>(wire.data());
        // Walk to find the svcinfo section (id 0x73).
        size_t svcOffset = 0;
        for (size_t i = 7; i + 2 < wire.size(); ++i) {
                if (bytes[i] == 0x73) { svcOffset = i; break; }
        }
        REQUIRE(svcOffset != 0);
        const size_t svcDataByte4 = svcOffset + 2 + 4; // section hdr + entry byte 4
        // svc_data_byte_4 layout when digital_cc=1: 0x80 | 0x40 | (svc & 0x3F)
        bytes[svcDataByte4] = static_cast<uint8_t>(0x80 | 0x40 | 5); // claim svc=5
        // Re-stamp checksum since we mutated a byte.
        uint8_t sum = 0;
        for (size_t i = 0; i < wire.size() - 1; ++i) sum += bytes[i];
        bytes[wire.size() - 1] = static_cast<uint8_t>(0x100 - sum);

        auto r = Cea708Cdp::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const Cea708Cdp &parsed = r.first();
        CHECK(parsed.svcInfoMismatches == 1);
        // The entry-flag value (3) wins, per §5.5 / parser policy.
        REQUIRE(parsed.ccSvcInfo.size() == 1);
        CHECK(parsed.ccSvcInfo[0].captionServiceNumber == 3);
}
