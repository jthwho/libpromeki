/**
 * @file      ancformat.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ancformat.h>
#include <promeki/variant.h>
#include <promeki/datastream.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AncFormat: default constructor is Invalid") {
        AncFormat f;
        CHECK_FALSE(f.isValid());
        CHECK(f.id() == AncFormat::Invalid);
        CHECK(f.name() == "Invalid");
        CHECK(f.category() == AncCategory::Unknown);
        CHECK(f.canonicalTransport() == AncTransport::Invalid);
}

TEST_CASE("AncFormat: construction from well-known IDs") {
        AncFormat f(AncFormat::Cea708);
        CHECK(f.isValid());
        CHECK(f.id() == AncFormat::Cea708);
        CHECK(f.name() == "Cea708");
        CHECK(f.category() == AncCategory::Captions);
        CHECK(f.canonicalTransport() == AncTransport::St291);
        CHECK(f.st291Did() == 0x61);
        CHECK(f.st291Sdid() == 0x01);
}

TEST_CASE("AncFormat: construction from registered name") {
        AncFormat f("AtcLtc");
        CHECK(f.isValid());
        CHECK(f.id() == AncFormat::AtcLtc);
        CHECK(f.category() == AncCategory::Timecode);
        CHECK(f.st291Did() == 0x60);
        CHECK(f.st291Sdid() == 0x60);
}

TEST_CASE("AncFormat: construction from unknown name yields Invalid") {
        AncFormat f("NoSuchFormat");
        CHECK_FALSE(f.isValid());
}

TEST_CASE("AncFormat: toString matches name") {
        AncFormat f(AncFormat::Afd);
        CHECK(f.toString() == "Afd");
        CHECK(f.toString() == f.name());
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("AncFormat: equality by underlying Data pointer") {
        AncFormat a(AncFormat::Cea708);
        AncFormat b(AncFormat::Cea708);
        AncFormat c(AncFormat::Cea608);
        CHECK(a == b);
        CHECK(a != c);
}

// ============================================================================
// Categories and transports
// ============================================================================

TEST_CASE("AncFormat: categories of well-known formats") {
        CHECK(AncFormat(AncFormat::Cea708).category() == AncCategory::Captions);
        CHECK(AncFormat(AncFormat::Cea608).category() == AncCategory::Captions);
        CHECK(AncFormat(AncFormat::AtcLtc).category() == AncCategory::Timecode);
        CHECK(AncFormat(AncFormat::AtcVitc1).category() == AncCategory::Timecode);
        CHECK(AncFormat(AncFormat::AtcVitc2).category() == AncCategory::Timecode);
        CHECK(AncFormat(AncFormat::Scte104).category() == AncCategory::Splice);
        CHECK(AncFormat(AncFormat::Scte35).category() == AncCategory::Splice);
        CHECK(AncFormat(AncFormat::Afd).category() == AncCategory::Aspect);
        CHECK(AncFormat(AncFormat::PanScan).category() == AncCategory::Aspect);
        CHECK(AncFormat(AncFormat::HdrStatic2086).category() == AncCategory::Hdr);
        CHECK(AncFormat(AncFormat::HdrDynamic2094_40).category() == AncCategory::Hdr);
        CHECK(AncFormat(AncFormat::DvRpu).category() == AncCategory::Hdr);
        CHECK(AncFormat(AncFormat::Smpte2020Audio).category() == AncCategory::AudioMetadata);
        CHECK(AncFormat(AncFormat::AudioInfoFrame).category() == AncCategory::AudioMetadata);
        CHECK(AncFormat(AncFormat::AviInfoFrame).category() == AncCategory::Display);
        CHECK(AncFormat(AncFormat::SpdInfoFrame).category() == AncCategory::Display);
        CHECK(AncFormat(AncFormat::Klv0601).category() == AncCategory::Geolocation);
}

TEST_CASE("AncFormat: canonical transports") {
        CHECK(AncFormat(AncFormat::Cea708).canonicalTransport() == AncTransport::St291);
        CHECK(AncFormat(AncFormat::Scte35).canonicalTransport() == AncTransport::MpegTsPrivate);
        CHECK(AncFormat(AncFormat::HdrStatic2086).canonicalTransport() == AncTransport::HdmiInfoFrame);
        CHECK(AncFormat(AncFormat::AviInfoFrame).canonicalTransport() == AncTransport::HdmiInfoFrame);
}

// ============================================================================
// Per-transport identity lookups
// ============================================================================

TEST_CASE("AncFormat: fromSt291DidSdid for every St291-canonical format") {
        CHECK(AncFormat::fromSt291DidSdid(0x61, 0x01).id() == AncFormat::Cea708);
        CHECK(AncFormat::fromSt291DidSdid(0x61, 0x02).id() == AncFormat::Cea608);
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x05).id() == AncFormat::Afd);
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x06).id() == AncFormat::PanScan);
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x07).id() == AncFormat::Scte104);
        // ST 12-2:2014 §5: all ATC flavours share (DID=0x60, SDID=0x60);
        // the registry returns the lowest-ID match (AtcLtc).  See the
        // dedicated P2-1 test for the collapse rule.
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60).id() == AncFormat::AtcLtc);
        // ST 12-3:2016 §6 assigns (0x60, 0x61) to ATC_HFRTC (Phase 6).
        // (0x60, 0x62) is still unassigned — ST 12-2:2014 doesn't use it
        // and ST 12-3 only uses 0x61.
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x61).id() == AncFormat::AtcHfrtc);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x62).id() == AncFormat::Invalid);
        CHECK(AncFormat::fromSt291DidSdid(0x44, 0x04).id() == AncFormat::Klv0601);
}

TEST_CASE("AncFormat: fromSt291DidSdid returns Invalid for unregistered pair") {
        // DID 0x99 is not registered.
        CHECK(AncFormat::fromSt291DidSdid(0x99, 0x01).id() == AncFormat::Invalid);
        // (0, 0) means "no ST 291 carriage" — must not match any format.
        CHECK(AncFormat::fromSt291DidSdid(0, 0).id() == AncFormat::Invalid);
        // Wrong SDID under a registered DID also fails (no wildcard).
        CHECK(AncFormat::fromSt291DidSdid(0x61, 0x99).id() == AncFormat::Invalid);
}

TEST_CASE("AncFormat: Smpte2020Audio wildcard SDID match (0x45, 0x01..0x09)") {
        // Every SDID under DID 0x45 should resolve to Smpte2020Audio
        // because that format registers with st291Sdid=0 (wildcard).
        for (uint8_t sdid = 0x01; sdid <= 0x09; ++sdid) {
                AncFormat f = AncFormat::fromSt291DidSdid(0x45, sdid);
                CHECK(f.id() == AncFormat::Smpte2020Audio);
        }
}

TEST_CASE("AncFormat: P2-22 PacketForDeletion wildcard match (0x80, anyDBN)") {
        // ST 291-1 §6.3 Packet-Marked-for-Deletion (DID 0x80, Type-1).
        // Registered with wildcard SDID so any DBN value resolves
        // back to the format ID.  Test a representative set covering
        // the §6.4 DBN range (1..255 plus the "inactive" 0).
        for (uint8_t dbn : {uint8_t(0x00), uint8_t(0x01), uint8_t(0x42),
                            uint8_t(0x80), uint8_t(0xFE), uint8_t(0xFF)}) {
                INFO("dbn=0x", String::number(static_cast<int>(dbn), 16));
                AncFormat f = AncFormat::fromSt291DidSdid(0x80, dbn);
                CHECK(f.id() == AncFormat::PacketForDeletion);
        }
        // Sanity: a real Type-2 DID at the same second byte must NOT
        // resolve to PacketForDeletion.
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x05).id() == AncFormat::Afd);
}

TEST_CASE("AncFormat: P2-21 PacketForDeletion lands in the Control category") {
        AncFormat f(AncFormat::PacketForDeletion);
        CHECK(f.category() == AncCategory::Control);
        // Control should not appear under any of the content
        // categories — PacketForDeletion in particular must not pollute
        // a caller iterating Unknown.
        AncFormat::IDList unknownIds = AncFormat::registeredIDsForCategory(AncCategory::Unknown);
        for (auto id : unknownIds) {
                CHECK(id != AncFormat::PacketForDeletion);
        }
        AncFormat::IDList controlIds = AncFormat::registeredIDsForCategory(AncCategory::Control);
        bool found = false;
        for (auto id : controlIds) {
                if (id == AncFormat::PacketForDeletion) {
                        found = true;
                        break;
                }
        }
        CHECK(found);
}

TEST_CASE("AncFormat: P2-1 ATC family collapses onto (0x60, 0x60) per ST 12-2:2014 §5") {
        // All three ATC flavours register at DID=0x60/SDID=0x60.  The
        // DBB1 byte in the wire payload discriminates LTC vs VITC1 vs
        // VITC2; the (DID,SDID) → ID lookup picks the lowest-ID match
        // (AtcLtc) and the codec parser surfaces the actual flavour on
        // @c AncAtc::payloadType.
        CHECK(AncFormat(AncFormat::AtcLtc).st291Sdid() == 0x60);
        CHECK(AncFormat(AncFormat::AtcVitc1).st291Sdid() == 0x60);
        CHECK(AncFormat(AncFormat::AtcVitc2).st291Sdid() == 0x60);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60).id() == AncFormat::AtcLtc);
}

TEST_CASE("AncFormat: fromSt291DidSdid refines the ATC trio from the DBB1 byte") {
        // Build a 16-UDW ATC payload with the requested DBB1 byte encoded
        // LSB-first into bit 3 of UDWs 1..8 (ST 12-2 Table 2).
        auto makeAtcUdw = [](uint8_t dbb1) {
                List<uint16_t> udw;
                udw.resize(16);
                for (size_t i = 0; i < 8; ++i) {
                        if ((dbb1 >> i) & 1u) udw[i] = 0x08; // DBB bit = bit 3 of UDW i
                }
                return udw;
        };

        // No payload supplied → legacy behaviour (lowest-ID AtcLtc).
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60).id() == AncFormat::AtcLtc);

        // DBB1 0x00 / 0x01 / 0x02 → LTC / VITC1 / VITC2.
        List<uint16_t> ltc = makeAtcUdw(0x00);
        List<uint16_t> v1  = makeAtcUdw(0x01);
        List<uint16_t> v2  = makeAtcUdw(0x02);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60, &ltc).id() == AncFormat::AtcLtc);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60, &v1).id() == AncFormat::AtcVitc1);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60, &v2).id() == AncFormat::AtcVitc2);

        // Reserved DBB1 values fall back to LTC.
        List<uint16_t> reserved = makeAtcUdw(0x05);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60, &reserved).id() == AncFormat::AtcLtc);

        // The refinement is scoped to the (0x60,0x60) slot — a supplied
        // payload must not perturb any other format, including the
        // (0x60,0x61) ATC_HFRTC slot.
        CHECK(AncFormat::fromSt291DidSdid(0x61, 0x01, &v1).id() == AncFormat::Cea708);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x61, &v1).id() == AncFormat::AtcHfrtc);

        // A short UDW list (< 8 words) can't carry a full DBB1 — fall
        // back to LTC rather than reading out of bounds.
        List<uint16_t> shortUdw;
        shortUdw.resize(4);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60, &shortUdw).id() == AncFormat::AtcLtc);
}

TEST_CASE("AncFormat: fromHdmiInfoFrameType for every HDMI-canonical format") {
        CHECK(AncFormat::fromHdmiInfoFrameType(0x82).id() == AncFormat::AviInfoFrame);
        CHECK(AncFormat::fromHdmiInfoFrameType(0x83).id() == AncFormat::SpdInfoFrame);
        CHECK(AncFormat::fromHdmiInfoFrameType(0x84).id() == AncFormat::AudioInfoFrame);
        CHECK(AncFormat::fromHdmiInfoFrameType(0x87).id() == AncFormat::HdrStatic2086);
        // Type 0x81 is the OUI-agnostic Vendor-Specific catch-all.
        CHECK(AncFormat::fromHdmiInfoFrameType(0x81).id() == AncFormat::VendorInfoFrame);
}

TEST_CASE("AncFormat: fromHdmiInfoFrameType returns Invalid for unregistered type") {
        CHECK(AncFormat::fromHdmiInfoFrameType(0x90).id() == AncFormat::Invalid);
        CHECK(AncFormat::fromHdmiInfoFrameType(0x00).id() == AncFormat::Invalid);
}

TEST_CASE("AncFormat: P2-24 fromHdmiInfoFrame promotes type 0x81 by OUI") {
        // HDR10+ OUI (00:D0:46) → HdrDynamic2094_40.
        CHECK(AncFormat::fromHdmiInfoFrame(0x81, 0x00D046u).id() ==
              AncFormat::HdrDynamic2094_40);
        // Dolby OUI (00:90:3E) → DvRpu.
        CHECK(AncFormat::fromHdmiInfoFrame(0x81, 0x00903Eu).id() ==
              AncFormat::DvRpu);
        // Unknown OUI → VendorInfoFrame catch-all.
        CHECK(AncFormat::fromHdmiInfoFrame(0x81, 0xABCDEFu).id() ==
              AncFormat::VendorInfoFrame);
        // Zero OUI with non-vendor type — OUI ignored, type drives.
        CHECK(AncFormat::fromHdmiInfoFrame(0x82, 0u).id() ==
              AncFormat::AviInfoFrame);
        CHECK(AncFormat::fromHdmiInfoFrame(0x87, 0u).id() ==
              AncFormat::HdrStatic2086);
        // Unregistered non-vendor type → Invalid.
        CHECK(AncFormat::fromHdmiInfoFrame(0x90, 0u).id() ==
              AncFormat::Invalid);
}

TEST_CASE("AncFormat: fromMpegTsTableId for Scte35 and unregistered") {
        CHECK(AncFormat::fromMpegTsTableId(0xFC).id() == AncFormat::Scte35);
        CHECK(AncFormat::fromMpegTsTableId(0x00).id() == AncFormat::Invalid);
        CHECK(AncFormat::fromMpegTsTableId(0x42).id() == AncFormat::Invalid);
}

// ============================================================================
// Name-based lookup
// ============================================================================

TEST_CASE("AncFormat: fromName succeeds for well-known names") {
        Result<AncFormat> r = AncFormat::fromName("Cea708");
        REQUIRE(isOk(r));
        CHECK(value(r).id() == AncFormat::Cea708);
}

TEST_CASE("AncFormat: fromName returns IdNotFound for unknown") {
        Result<AncFormat> r = AncFormat::fromName("NoSuchFormat");
        CHECK(isError(r));
        CHECK(error(r) == Error::IdNotFound);
}

TEST_CASE("AncFormat: idFromName returns Invalid for unknown") {
        CHECK(AncFormat::idFromName("NoSuchFormat") == AncFormat::Invalid);
        CHECK(AncFormat::idFromName("Cea708") == AncFormat::Cea708);
}

// ============================================================================
// Enumeration
// ============================================================================

TEST_CASE("AncFormat: registeredIDs enumerates well-known formats") {
        AncFormat::IDList ids = AncFormat::registeredIDs();
        // The list must include every well-known well-known format.
        // We just probe for a representative subset.
        auto contains = [&](AncFormat::ID id) {
                for (auto x : ids)
                        if (x == id) return true;
                return false;
        };
        CHECK(contains(AncFormat::Cea708));
        CHECK(contains(AncFormat::AtcLtc));
        CHECK(contains(AncFormat::Afd));
        CHECK(contains(AncFormat::Scte35));
        CHECK(contains(AncFormat::HdrStatic2086));
        // Invalid must not appear in the registered list.
        CHECK_FALSE(contains(AncFormat::Invalid));
}

TEST_CASE("AncFormat: registeredIDsForCategory(Captions)") {
        AncFormat::IDList ids = AncFormat::registeredIDsForCategory(AncCategory::Captions);
        auto              contains = [&](AncFormat::ID id) {
                for (auto x : ids)
                        if (x == id) return true;
                return false;
        };
        CHECK(contains(AncFormat::Cea708));
        CHECK(contains(AncFormat::Cea608));
        CHECK_FALSE(contains(AncFormat::AtcLtc));
        CHECK_FALSE(contains(AncFormat::Afd));
}

TEST_CASE("AncFormat: registeredIDsForCategory(Hdr)") {
        AncFormat::IDList ids = AncFormat::registeredIDsForCategory(AncCategory::Hdr);
        auto              contains = [&](AncFormat::ID id) {
                for (auto x : ids)
                        if (x == id) return true;
                return false;
        };
        CHECK(contains(AncFormat::HdrStatic2086));
        CHECK(contains(AncFormat::HdrDynamic2094_40));
        CHECK(contains(AncFormat::DvRpu));
        CHECK_FALSE(contains(AncFormat::Cea708));
}

TEST_CASE("AncFormat: registeredIDsForTransport(St291)") {
        AncFormat::IDList ids = AncFormat::registeredIDsForTransport(AncTransport::St291);
        auto              contains = [&](AncFormat::ID id) {
                for (auto x : ids)
                        if (x == id) return true;
                return false;
        };
        CHECK(contains(AncFormat::Cea708));
        CHECK(contains(AncFormat::AtcLtc));
        CHECK(contains(AncFormat::Klv0601));
        CHECK(contains(AncFormat::Smpte2020Audio));
        // HDMI- / MpegTs-only formats should not appear.
        CHECK_FALSE(contains(AncFormat::AviInfoFrame));
        CHECK_FALSE(contains(AncFormat::Scte35));
}

TEST_CASE("AncFormat: registeredIDsForTransport(HdmiInfoFrame)") {
        AncFormat::IDList ids = AncFormat::registeredIDsForTransport(AncTransport::HdmiInfoFrame);
        auto              contains = [&](AncFormat::ID id) {
                for (auto x : ids)
                        if (x == id) return true;
                return false;
        };
        CHECK(contains(AncFormat::AviInfoFrame));
        CHECK(contains(AncFormat::SpdInfoFrame));
        CHECK(contains(AncFormat::AudioInfoFrame));
        CHECK(contains(AncFormat::HdrStatic2086));
        CHECK(contains(AncFormat::VendorInfoFrame));
        CHECK_FALSE(contains(AncFormat::Cea608));
}

TEST_CASE("AncFormat: registeredIDsForTransport(MpegTsPrivate)") {
        AncFormat::IDList ids = AncFormat::registeredIDsForTransport(AncTransport::MpegTsPrivate);
        auto              contains = [&](AncFormat::ID id) {
                for (auto x : ids)
                        if (x == id) return true;
                return false;
        };
        CHECK(contains(AncFormat::Scte35));
        CHECK_FALSE(contains(AncFormat::Cea708));
}

// ============================================================================
// F8 additions — new categories + new well-known formats
// ============================================================================

TEST_CASE("AncCategory: F8 additions are reachable by name and value") {
        // Round-trip through TypedEnum string mapping; these are the
        // four new categories landed in F8.A9.
        CHECK(AncCategory("Subtitles") == AncCategory::Subtitles);
        CHECK(AncCategory("Klv") == AncCategory::Klv);
        CHECK(AncCategory("Sei") == AncCategory::Sei);
        CHECK(AncCategory("Vbi") == AncCategory::Vbi);
        // Each must be distinct from the pre-existing buckets it
        // narrows on (Subtitles / Captions, Klv / Geolocation,
        // Sei / Captions, Vbi / Unknown).
        CHECK(AncCategory::Subtitles != AncCategory::Captions);
        CHECK(AncCategory::Klv != AncCategory::Geolocation);
        CHECK(AncCategory::Sei != AncCategory::Captions);
        CHECK(AncCategory::Vbi != AncCategory::Unknown);
}

TEST_CASE("AncFormat: F8 OP-47 SDP registration (DID 0x43 / SDID 0x02)") {
        AncFormat f(AncFormat::Op47Sdp);
        CHECK(f.isValid());
        CHECK(f.name() == "Op47Sdp");
        CHECK(f.category() == AncCategory::Subtitles);
        CHECK(f.canonicalTransport() == AncTransport::St291);
        CHECK(f.st291Did() == 0x43);
        CHECK(f.st291Sdid() == 0x02);
        CHECK(AncFormat::fromSt291DidSdid(0x43, 0x02).id() == AncFormat::Op47Sdp);
        CHECK(AncFormat::idFromName("Op47Sdp") == AncFormat::Op47Sdp);
}

TEST_CASE("AncFormat: P2 OP-47 Multipack registration (DID 0x43 / SDID 0x03)") {
        // RDD 8:2008 §4.2(iii): "DID and SDID values 143h and 203h,
        // respectively (includes parity)" → 8-bit DID/SDID 0x43/0x03.
        AncFormat f(AncFormat::Op47Multipack);
        CHECK(f.isValid());
        CHECK(f.name() == "Op47Multipack");
        CHECK(f.category() == AncCategory::Subtitles);
        CHECK(f.canonicalTransport() == AncTransport::St291);
        CHECK(f.st291Did() == 0x43);
        CHECK(f.st291Sdid() == 0x03);
        CHECK(AncFormat::fromSt291DidSdid(0x43, 0x03).id() == AncFormat::Op47Multipack);
}

TEST_CASE("AncFormat: P2 ST 2031 VBI registration (DID 0x41 / SDID 0x08)") {
        // ST 2031:2015 §5: "The DID word shall be set to the value 41h.
        // The SDID word shall be set to the value of 08h."
        AncFormat f(AncFormat::VbiSt2031);
        CHECK(f.isValid());
        CHECK(f.name() == "VbiSt2031");
        CHECK(f.category() == AncCategory::Vbi);
        CHECK(f.canonicalTransport() == AncTransport::St291);
        CHECK(f.st291Did() == 0x41);
        CHECK(f.st291Sdid() == 0x08);
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x08).id() == AncFormat::VbiSt2031);
        // ST 2031 lives on DID 0x41 alongside VPID (0x01), AFD (0x05),
        // PanScan (0x06), SCTE-104 (0x07), HdrStatic2086 (0x0C),
        // HdrDynamic2094_40 (0x0D); each SDID is uniquely assigned.
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x05).id() == AncFormat::Afd);
}

TEST_CASE("AncFormat: F8 HdrDynamic2094_10 is name-addressable but shares no DID lookup with HdrStatic") {
        AncFormat f(AncFormat::HdrDynamic2094_10);
        CHECK(f.isValid());
        CHECK(f.name() == "HdrDynamic2094_10");
        CHECK(f.category() == AncCategory::Hdr);
        CHECK(f.canonicalTransport() == AncTransport::St291);
        // ST 2108-1 multiplexes Frame Type 1 (HdrStatic2086) and
        // Frame Type 2 (HdrDynamic2094_10) under the same DID/SDID.
        // The codec dispatches on the Frame Type byte; the
        // (DID,SDID) lookup remains anchored to HdrStatic2086.
        CHECK(f.st291Did() == 0);
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x0C).id() == AncFormat::HdrStatic2086);
        CHECK(AncFormat::idFromName("HdrDynamic2094_10") == AncFormat::HdrDynamic2094_10);
}

TEST_CASE("AncFormat: F8 Subtitles category enumerates OP-47 SDP + multipack") {
        AncFormat::IDList ids = AncFormat::registeredIDsForCategory(AncCategory::Subtitles);
        auto              contains = [&](AncFormat::ID id) {
                for (auto x : ids)
                        if (x == id) return true;
                return false;
        };
        CHECK(contains(AncFormat::Op47Sdp));
        CHECK(contains(AncFormat::Op47Multipack));
        CHECK_FALSE(contains(AncFormat::Cea708));
}

TEST_CASE("AncFormat: F8 Vbi category enumerates ST 2031 only") {
        AncFormat::IDList ids = AncFormat::registeredIDsForCategory(AncCategory::Vbi);
        auto              contains = [&](AncFormat::ID id) {
                for (auto x : ids)
                        if (x == id) return true;
                return false;
        };
        CHECK(contains(AncFormat::VbiSt2031));
        CHECK_FALSE(contains(AncFormat::Cea608));
}

// ============================================================================
// User-defined registration
// ============================================================================

TEST_CASE("AncFormat: registerType allocates unique IDs at or above UserDefined") {
        AncFormat::ID a = AncFormat::registerType();
        AncFormat::ID b = AncFormat::registerType();
        CHECK(a >= AncFormat::UserDefined);
        CHECK(b >= AncFormat::UserDefined);
        CHECK(a != b);
}

TEST_CASE("AncFormat: registerData installs a custom format and is retrievable") {
        AncFormat::ID   newId = AncFormat::registerType();
        AncFormat::Data d;
        d.id = newId;
        d.name = "DoctestCustomAncFormat_Alpha";
        d.desc = "Doctest-only custom format";
        d.category = AncCategory::UserDefined;
        d.canonicalTransport = AncTransport::NdiXml;
        AncFormat::registerData(std::move(d));

        AncFormat f(newId);
        CHECK(f.isValid());
        CHECK(f.id() == newId);
        CHECK(f.name() == "DoctestCustomAncFormat_Alpha");
        CHECK(f.category() == AncCategory::UserDefined);
        CHECK(f.canonicalTransport() == AncTransport::NdiXml);
}

// ============================================================================
// Variant integration
// ============================================================================

TEST_CASE("AncFormat: Variant round-trip preserves identity") {
        AncFormat fmt(AncFormat::Cea708);
        Variant   v(fmt);
        CHECK(v.type() == DataTypeAncFormat);
        AncFormat back = v.get<AncFormat>();
        CHECK(back == fmt);
        CHECK(back.id() == AncFormat::Cea708);
}

// ============================================================================
// DataStream serialization
// ============================================================================

TEST_CASE("AncFormat: DataStream writes integer ID, reads back the registered format") {
        AncFormat orig(AncFormat::Afd);

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);

        int32_t encoded = static_cast<int32_t>(orig.id());
        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << encoded;
        }
        dev.seek(0);
        int32_t decoded = 0;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> decoded;
        }
        AncFormat round(static_cast<AncFormat::ID>(decoded));
        CHECK(round == orig);
}
