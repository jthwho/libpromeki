/**
 * @file      ancformat.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
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
        CHECK(AncFormat(AncFormat::BarData).category() == AncCategory::Aspect);
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
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x06).id() == AncFormat::BarData);
        CHECK(AncFormat::fromSt291DidSdid(0x41, 0x07).id() == AncFormat::Scte104);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x60).id() == AncFormat::AtcLtc);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x61).id() == AncFormat::AtcVitc1);
        CHECK(AncFormat::fromSt291DidSdid(0x60, 0x62).id() == AncFormat::AtcVitc2);
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
        CHECK(v.type() == Variant::TypeAncFormat);
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
