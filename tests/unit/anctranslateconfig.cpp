/**
 * @file      anctranslateconfig.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/anctranslateconfig.h>
#include <promeki/bufferiodevice.h>
#include <promeki/datastream.h>
#include <promeki/enums.h>

using namespace promeki;

// ============================================================================
// Defaults
// ============================================================================

TEST_CASE("AncTranslateConfig: defaults via spec lookup") {
        AncTranslateConfig cfg;
        CHECK(cfg.isEmpty());

        // Default-construction stores nothing; the spec defaults are what
        // consumers see when they pull a spec-default fallback.  Enum-typed
        // values come out as a generic Enum and are compared via integer
        // value (TypedEnum subclasses are not Variant payload types of
        // their own).
        const VariantSpec *fid = AncTranslateConfig::spec(AncTranslateConfig::Fidelity);
        REQUIRE(fid != nullptr);
        CHECK(fid->defaultValue().get<Enum>().value() == AncFidelity::Default.value());

        const VariantSpec *cks = AncTranslateConfig::spec(AncTranslateConfig::Checksum);
        REQUIRE(cks != nullptr);
        CHECK(cks->defaultValue().get<Enum>().value() == AncChecksumPolicy::PreserveOrRecompute.value());

        const VariantSpec *onu = AncTranslateConfig::spec(AncTranslateConfig::OnUnsupported);
        REQUIRE(onu != nullptr);
        CHECK(onu->defaultValue().get<Enum>().value() == AncOnUnsupported::BestEffort.value());

        const VariantSpec *lossy = AncTranslateConfig::spec(AncTranslateConfig::AllowLossy);
        REQUIRE(lossy != nullptr);
        CHECK(lossy->defaultValue().get<bool>() == false);
}

// ============================================================================
// Set / get round-trip
// ============================================================================

TEST_CASE("AncTranslateConfig: set/get round-trip for universal knobs") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::Fidelity, AncFidelity(AncFidelity::Full));
        cfg.set(AncTranslateConfig::Checksum, AncChecksumPolicy(AncChecksumPolicy::AlwaysRecompute));
        cfg.set(AncTranslateConfig::OnUnsupported, AncOnUnsupported(AncOnUnsupported::Fail));
        cfg.set(AncTranslateConfig::AllowLossy, true);

        CHECK(cfg.get(AncTranslateConfig::Fidelity).asEnum(AncFidelity::Type).value() == AncFidelity::Full.value());
        CHECK(cfg.get(AncTranslateConfig::Checksum).asEnum(AncChecksumPolicy::Type).value() ==
              AncChecksumPolicy::AlwaysRecompute.value());
        CHECK(cfg.get(AncTranslateConfig::OnUnsupported).asEnum(AncOnUnsupported::Type).value() ==
              AncOnUnsupported::Fail.value());
        CHECK(cfg.getAs<bool>(AncTranslateConfig::AllowLossy) == true);

        CHECK(cfg.contains(AncTranslateConfig::Fidelity));
        CHECK(cfg.contains(AncTranslateConfig::Checksum));
        CHECK(cfg.contains(AncTranslateConfig::OnUnsupported));
        CHECK(cfg.contains(AncTranslateConfig::AllowLossy));
}

TEST_CASE("AncTranslateConfig: set/get round-trip for per-transport keys") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
        cfg.set(AncTranslateConfig::St291FieldB, true);
        cfg.set(AncTranslateConfig::NdiXmlNamespace, String("ndi"));
        cfg.set(AncTranslateConfig::HdmiInfoFrameOui, uint32_t(0x90848B));
        cfg.set(AncTranslateConfig::RtmpAmfObjectName, String("onMyTag"));

        CHECK(cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine) == 11);
        CHECK(cfg.getAs<bool>(AncTranslateConfig::St291FieldB) == true);
        CHECK(cfg.getAs<String>(AncTranslateConfig::NdiXmlNamespace) == "ndi");
        CHECK(cfg.getAs<uint32_t>(AncTranslateConfig::HdmiInfoFrameOui) == 0x90848B);
        CHECK(cfg.getAs<String>(AncTranslateConfig::RtmpAmfObjectName) == "onMyTag");
}

TEST_CASE("AncTranslateConfig: getAs falls back to defaultValue when missing") {
        AncTranslateConfig cfg;
        // No keys set — getAs returns the caller-supplied default, which the
        // helper specs document as the same value the spec advertises.
        CHECK(cfg.getAs<uint16_t>(AncTranslateConfig::St291BuildLine, 99) == 99);
        CHECK(cfg.getAs<bool>(AncTranslateConfig::AllowLossy, true) == true);
}

// ============================================================================
// merge
// ============================================================================

TEST_CASE("AncTranslateConfig: merge overlays — later wins, untouched preserved") {
        AncTranslateConfig base;
        base.set(AncTranslateConfig::Fidelity, AncFidelity(AncFidelity::Strict));
        base.set(AncTranslateConfig::AllowLossy, false);
        base.set(AncTranslateConfig::St291BuildLine, uint16_t(9));

        AncTranslateConfig overlay;
        overlay.set(AncTranslateConfig::Fidelity, AncFidelity(AncFidelity::Full));
        overlay.set(AncTranslateConfig::OnUnsupported, AncOnUnsupported(AncOnUnsupported::Skip));

        base.merge(overlay);

        // Overridden by overlay.
        CHECK(base.get(AncTranslateConfig::Fidelity).asEnum(AncFidelity::Type).value() == AncFidelity::Full.value());
        // Added by overlay.
        CHECK(base.get(AncTranslateConfig::OnUnsupported).asEnum(AncOnUnsupported::Type).value() ==
              AncOnUnsupported::Skip.value());
        // Untouched by overlay.
        CHECK(base.getAs<bool>(AncTranslateConfig::AllowLossy) == false);
        CHECK(base.getAs<uint16_t>(AncTranslateConfig::St291BuildLine) == 9);
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("AncTranslateConfig: DataStream round-trip preserves entries") {
        AncTranslateConfig original;
        original.set(AncTranslateConfig::Fidelity, AncFidelity(AncFidelity::Full));
        original.set(AncTranslateConfig::Checksum, AncChecksumPolicy(AncChecksumPolicy::StrictValidate));
        original.set(AncTranslateConfig::AllowLossy, true);
        original.set(AncTranslateConfig::St291BuildLine, uint16_t(13));
        original.set(AncTranslateConfig::RtmpAmfObjectName, String("onCueA"));

        Buffer         buf(4096);
        BufferIODevice dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream writer = DataStream::createWriter(&dev);
                writer << original;
        }
        dev.seek(0);
        AncTranslateConfig restored;
        {
                DataStream reader = DataStream::createReader(&dev);
                reader >> restored;
        }
        CHECK(restored == original);
}

// ============================================================================
// JSON string round-trip
// ============================================================================

TEST_CASE("AncTranslateConfig: toString produces non-empty JSON for populated config") {
        AncTranslateConfig cfg;
        cfg.set(AncTranslateConfig::AllowLossy, true);
        cfg.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
        String s = cfg.toString();
        CHECK_FALSE(s.isEmpty());
        // Compact (default) form should at least include the key names.
        CHECK(s.contains("AllowLossy"));
        CHECK(s.contains("St291BuildLine"));
}

TEST_CASE("AncTranslateConfig: toString/fromString round-trip preserves entries") {
        AncTranslateConfig original;
        original.set(AncTranslateConfig::Fidelity, AncFidelity(AncFidelity::Full));
        original.set(AncTranslateConfig::Checksum, AncChecksumPolicy(AncChecksumPolicy::AlwaysRecompute));
        original.set(AncTranslateConfig::OnUnsupported, AncOnUnsupported(AncOnUnsupported::Skip));
        original.set(AncTranslateConfig::AllowLossy, true);
        original.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
        original.set(AncTranslateConfig::NdiXmlNamespace, String("ndi"));
        original.set(AncTranslateConfig::RtmpAmfObjectName, String("onCaptionInfo"));

        Error              err;
        String             s = original.toString();
        AncTranslateConfig parsed = AncTranslateConfig::fromString(s, &err);
        CHECK(err.isOk());
        CHECK(parsed == original);
}

TEST_CASE("AncTranslateConfig: indented toString is also round-trippable") {
        AncTranslateConfig original;
        original.set(AncTranslateConfig::St291FieldB, true);
        original.set(AncTranslateConfig::HdmiInfoFrameOui, uint32_t(0x90848B));
        Error              err;
        AncTranslateConfig parsed = AncTranslateConfig::fromString(original.toString(2), &err);
        CHECK(err.isOk());
        CHECK(parsed == original);
}

TEST_CASE("AncTranslateConfig: fromString on malformed JSON sets err and returns empty") {
        Error              err;
        AncTranslateConfig parsed = AncTranslateConfig::fromString(String("{not valid json"), &err);
        CHECK(err.isError());
        CHECK(parsed.isEmpty());
}

TEST_CASE("AncTranslateConfig: empty config toString / fromString round-trip") {
        AncTranslateConfig empty;
        Error              err;
        AncTranslateConfig parsed = AncTranslateConfig::fromString(empty.toString(), &err);
        CHECK(err.isOk());
        CHECK(parsed == empty);
        CHECK(parsed.isEmpty());
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("AncTranslateConfig: equality compares stored entries") {
        AncTranslateConfig a;
        AncTranslateConfig b;
        CHECK(a == b);
        a.set(AncTranslateConfig::AllowLossy, true);
        CHECK(a != b);
        b.set(AncTranslateConfig::AllowLossy, true);
        CHECK(a == b);
        a.set(AncTranslateConfig::St291BuildLine, uint16_t(11));
        b.set(AncTranslateConfig::St291BuildLine, uint16_t(12));
        CHECK(a != b);
}
