/**
 * @file      hdrstaticmetadata.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <doctest/doctest.h>
#include <promeki/bufferiodevice.h>
#include <promeki/contentlightlevel.h>
#include <promeki/datastream.h>
#include <promeki/hdrstaticmetadata.h>
#include <promeki/json.h>
#include <promeki/masteringdisplay.h>
#include <promeki/result.h>
#include <promeki/variant.h>

using namespace promeki;

namespace {

        // Canonical HDR10 mastering display: BT.2020 primaries + D65 +
        // 0.005..1000 cd/m² display range.
        MasteringDisplay hdr10MasteringDisplay() {
                return MasteringDisplay(CIEPoint(0.708, 0.292), CIEPoint(0.170, 0.797), CIEPoint(0.131, 0.046),
                                        CIEPoint(0.3127, 0.3290), 0.005, 1000.0);
        }

        HdrStaticMetadata hdr10Sample() {
                return HdrStaticMetadata(TransferCharacteristics::SMPTE2084, hdr10MasteringDisplay(),
                                         ContentLightLevel(1000, 400));
        }

} // namespace

// ============================================================================
// EOTF mapping (TransferCharacteristics <-> CTA-861.3 wire-EOTF)
// ============================================================================

TEST_CASE("HdrStaticMetadata: wireEotfFor maps PQ / HLG / SDR correctly") {
        CHECK(HdrStaticMetadata::wireEotfFor(TransferCharacteristics::SMPTE2084)
              == HdrStaticMetadata::EotfSmpte2084);
        CHECK(HdrStaticMetadata::wireEotfFor(TransferCharacteristics::ARIB_STD_B67)
              == HdrStaticMetadata::EotfHlg);
        CHECK(HdrStaticMetadata::wireEotfFor(TransferCharacteristics::BT709)
              == HdrStaticMetadata::EotfSdrGamma);
        CHECK(HdrStaticMetadata::wireEotfFor(TransferCharacteristics::Unspecified)
              == HdrStaticMetadata::EotfSdrGamma);
}

TEST_CASE("HdrStaticMetadata: transferFromWireEotf inverts the round-trippable subset") {
        CHECK(HdrStaticMetadata::transferFromWireEotf(HdrStaticMetadata::EotfSmpte2084)
              == TransferCharacteristics::SMPTE2084);
        CHECK(HdrStaticMetadata::transferFromWireEotf(HdrStaticMetadata::EotfHlg)
              == TransferCharacteristics::ARIB_STD_B67);
        // Both SDR-Gamma (0) and HDR-Gamma (1) collapse to Unspecified.
        CHECK(HdrStaticMetadata::transferFromWireEotf(HdrStaticMetadata::EotfSdrGamma)
              == TransferCharacteristics::Unspecified);
        CHECK(HdrStaticMetadata::transferFromWireEotf(HdrStaticMetadata::EotfHdrGamma)
              == TransferCharacteristics::Unspecified);
}

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("HdrStaticMetadata: default-constructed encodes to zero body + round-trips equal") {
        HdrStaticMetadata md;
        CHECK(md.eotf() == TransferCharacteristics::Unspecified);
        CHECK(md.contentLightLevel().maxCLL() == 0u);
        CHECK(md.contentLightLevel().maxFALL() == 0u);

        Buffer wire = md.toBuffer();
        CHECK(wire.size() == HdrStaticMetadata::Type1BodySize);
        const uint8_t *p = static_cast<const uint8_t *>(wire.data());
        for (size_t i = 0; i < wire.size(); ++i) CHECK(p[i] == 0u);

        // The default-constructed leaves are initialised to their
        // wire-zero form (CIE @c (0,0), zero luminance, zero
        // MaxCLL/MaxFALL) so a default Variant round-trips through
        // DataStream cleanly.
        Result<HdrStaticMetadata> r = HdrStaticMetadata::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first() == md);
}

// ============================================================================
// HDR10 sample round-trip (full mastering display + CLL)
// ============================================================================

TEST_CASE("HdrStaticMetadata: HDR10 sample round-trips through toBuffer/fromBuffer") {
        HdrStaticMetadata md = hdr10Sample();
        Buffer            wire = md.toBuffer();
        REQUIRE(wire.size() == HdrStaticMetadata::Type1BodySize);

        const uint8_t *p = static_cast<const uint8_t *>(wire.data());
        // Byte 0: EOTF == PQ (2).  Byte 1: Static_Metadata_Descriptor_ID = 0.
        CHECK(p[0] == HdrStaticMetadata::EotfSmpte2084);
        CHECK(p[1] == HdrStaticMetadata::DescriptorIdType1);

        Result<HdrStaticMetadata> r = HdrStaticMetadata::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        const HdrStaticMetadata &out = r.first();
        CHECK(out.eotf() == TransferCharacteristics::SMPTE2084);

        // Chromaticities round-trip with the CTA-861.3 quantisation
        // (chromaticity * 50000 → uint16).  At 50000-step resolution the
        // worst-case error is 1 / 50000 = 2.0e-5.
        const double tol = 3.0e-5;
        CHECK(std::fabs(out.masteringDisplay().red().x() - 0.708) < tol);
        CHECK(std::fabs(out.masteringDisplay().red().y() - 0.292) < tol);
        CHECK(std::fabs(out.masteringDisplay().green().x() - 0.170) < tol);
        CHECK(std::fabs(out.masteringDisplay().green().y() - 0.797) < tol);
        CHECK(std::fabs(out.masteringDisplay().blue().x() - 0.131) < tol);
        CHECK(std::fabs(out.masteringDisplay().blue().y() - 0.046) < tol);
        CHECK(std::fabs(out.masteringDisplay().whitePoint().x() - 0.3127) < tol);
        CHECK(std::fabs(out.masteringDisplay().whitePoint().y() - 0.3290) < tol);

        // Luminance: max is integer cd/m²; min is 0.0001 cd/m² steps (so
        // 0.005 → 50 wire units → 0.0050 cd/m² recovered exactly).
        CHECK(std::fabs(out.masteringDisplay().maxLuminance() - 1000.0) < 0.5);
        CHECK(std::fabs(out.masteringDisplay().minLuminance() - 0.005) < 1e-6);

        CHECK(out.contentLightLevel().maxCLL() == 1000u);
        CHECK(out.contentLightLevel().maxFALL() == 400u);
}

// ============================================================================
// HLG sample (EOTF only, no mastering display)
// ============================================================================

TEST_CASE("HdrStaticMetadata: HLG sample with no mastering display round-trips") {
        HdrStaticMetadata md(TransferCharacteristics::ARIB_STD_B67, MasteringDisplay(), ContentLightLevel());
        Buffer            wire = md.toBuffer();

        const uint8_t *p = static_cast<const uint8_t *>(wire.data());
        CHECK(p[0] == HdrStaticMetadata::EotfHlg);
        CHECK(p[1] == HdrStaticMetadata::DescriptorIdType1);
        // Bytes 2..25 are all zero when the mastering display + CLL are
        // unspecified (default-constructed CIE points + zero luminance).
        for (size_t i = 2; i < HdrStaticMetadata::Type1BodySize; ++i) CHECK(p[i] == 0u);

        Result<HdrStaticMetadata> r = HdrStaticMetadata::fromBuffer(wire);
        REQUIRE(r.second().isOk());
        CHECK(r.first().eotf() == TransferCharacteristics::ARIB_STD_B67);
        CHECK_FALSE(r.first().masteringDisplay().isValid());
        CHECK_FALSE(r.first().contentLightLevel().isValid());
}

// ============================================================================
// Failure modes
// ============================================================================

TEST_CASE("HdrStaticMetadata: fromBuffer rejects buffers smaller than Type1BodySize") {
        uint8_t                   bytes[10] = {0};
        Result<HdrStaticMetadata> r = HdrStaticMetadata::fromBuffer(bytes, sizeof(bytes));
        CHECK(r.second().code() == Error::CorruptData);
}

TEST_CASE("HdrStaticMetadata: fromBuffer rejects unknown Static_Metadata_Descriptor_ID") {
        uint8_t wire[HdrStaticMetadata::Type1BodySize] = {0};
        wire[0] = HdrStaticMetadata::EotfSmpte2084;
        wire[1] = 0x05; // not Type 1
        Result<HdrStaticMetadata> r = HdrStaticMetadata::fromBuffer(wire, sizeof(wire));
        CHECK(r.second().code() == Error::CorruptData);
}

// ============================================================================
// JSON / toString
// ============================================================================

TEST_CASE("HdrStaticMetadata: toJson produces structured output") {
        HdrStaticMetadata md = hdr10Sample();
        JsonObject        obj = md.toJson();

        Error err;
        CHECK(obj.getInt("wireEotf", &err) == HdrStaticMetadata::EotfSmpte2084);

        JsonObject mdObj = obj.getObject("masteringDisplay", &err);
        CHECK(err.isOk());
        CHECK(std::fabs(mdObj.getDouble("maxLuminance", &err) - 1000.0) < 1e-9);

        JsonObject cllObj = obj.getObject("contentLightLevel", &err);
        CHECK(err.isOk());
        CHECK(cllObj.getInt("maxCLL", &err) == 1000);
        CHECK(cllObj.getInt("maxFALL", &err) == 400);
}

TEST_CASE("HdrStaticMetadata: toString includes EOTF + key luminance fields") {
        HdrStaticMetadata md = hdr10Sample();
        String            s = md.toString();
        CHECK(s.contains("SMPTE2084"));
        CHECK(s.contains("maxCLL=1000"));
        CHECK(s.contains("maxFALL=400"));
}

// ============================================================================
// Variant integration
// ============================================================================

TEST_CASE("HdrStaticMetadata: round-trips through Variant") {
        HdrStaticMetadata original = hdr10Sample();
        Variant           v;
        v.set(original);
        CHECK(v.type() == DataTypeHdrStaticMetadata);
        HdrStaticMetadata out = v.get<HdrStaticMetadata>();
        // Equality across the chromaticity-quantisation step requires
        // either accepting the quantised round-trip or comparing via
        // toBuffer.  We constructed `original` with values that already
        // sit on integer wire steps, but the float comparison inside
        // MasteringDisplay is exact-double — so go through toBuffer for
        // identity.
        CHECK(out.toBuffer() == original.toBuffer());
}

// ============================================================================
// DataStream round-trip
// ============================================================================

TEST_CASE("HdrStaticMetadata: DataStream operators round-trip") {
        HdrStaticMetadata original = hdr10Sample();
        Buffer            buf(4096);
        BufferIODevice    dev(&buf);
        dev.open(IODevice::ReadWrite);
        {
                DataStream w = DataStream::createWriter(&dev);
                w << original;
        }
        dev.seek(0);
        HdrStaticMetadata restored;
        {
                DataStream r = DataStream::createReader(&dev);
                r >> restored;
        }
        CHECK(restored.toBuffer() == original.toBuffer());
}

// ============================================================================
// Equality / inequality
// ============================================================================

TEST_CASE("HdrStaticMetadata: operator== / operator!= field-wise") {
        HdrStaticMetadata a = hdr10Sample();
        HdrStaticMetadata b = a;
        CHECK(a == b);

        b.setContentLightLevel(ContentLightLevel(1000, 500));
        CHECK(a != b);
}
