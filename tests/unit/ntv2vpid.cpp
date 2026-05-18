/**
 * @file      ntv2vpid.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <doctest/doctest.h>
#include <promeki/enums.h>
#include <promeki/ntv2vpid.h>

#include <ntv2enums.h>

using namespace promeki;

TEST_CASE("Ntv2Vpid: TransferCharacteristics → VPID → TransferCharacteristics round-trip") {
        // PQ + HLG have dedicated VPID codes — those round-trip
        // exactly.
        CHECK(Ntv2Vpid::toVpidTransfer(TransferCharacteristics::SMPTE2084)
              == static_cast<int>(NTV2_VPID_TC_PQ));
        CHECK(Ntv2Vpid::fromVpidTransfer(static_cast<int>(NTV2_VPID_TC_PQ))
              == TransferCharacteristics::SMPTE2084);

        CHECK(Ntv2Vpid::toVpidTransfer(TransferCharacteristics::ARIB_STD_B67)
              == static_cast<int>(NTV2_VPID_TC_HLG));
        CHECK(Ntv2Vpid::fromVpidTransfer(static_cast<int>(NTV2_VPID_TC_HLG))
              == TransferCharacteristics::ARIB_STD_B67);

        // Unspecified is its own VPID code.
        CHECK(Ntv2Vpid::toVpidTransfer(TransferCharacteristics::Unspecified)
              == static_cast<int>(NTV2_VPID_TC_Unspecified));
        CHECK(Ntv2Vpid::fromVpidTransfer(static_cast<int>(NTV2_VPID_TC_Unspecified))
              == TransferCharacteristics::Unspecified);
}

TEST_CASE("Ntv2Vpid: SDR transfer characteristics collapse to NTV2_VPID_TC_SDR_TV") {
        // VPID byte 4 has no finer-grained transfer codes for SDR
        // variants — every BT709 / SRGB / BT2020_10 / BT2020_12 /
        // SMPTE170M / Gamma22 / Gamma28 / Linear path must land on
        // NTV2_VPID_TC_SDR_TV so receivers see "this is SDR" rather
        // than "no claim".
        const TransferCharacteristics sdrShapes[] = {
                TransferCharacteristics::BT709,    TransferCharacteristics::SRGB,
                TransferCharacteristics::BT2020_10, TransferCharacteristics::BT2020_12,
                TransferCharacteristics::SMPTE170M, TransferCharacteristics::Gamma22,
                TransferCharacteristics::Gamma28,   TransferCharacteristics::Linear,
        };
        for (const TransferCharacteristics &tc : sdrShapes) {
                CHECK(Ntv2Vpid::toVpidTransfer(tc) == static_cast<int>(NTV2_VPID_TC_SDR_TV));
        }
        // The reverse direction picks BT709 as the canonical SDR claim.
        CHECK(Ntv2Vpid::fromVpidTransfer(static_cast<int>(NTV2_VPID_TC_SDR_TV))
              == TransferCharacteristics::BT709);
}

TEST_CASE("Ntv2Vpid: Auto transfer maps to NTV2_VPID_TC_Unspecified") {
        // The Auto sentinel is a libpromeki-level "use the framestore's
        // own H.273 derivation" flag — the mapper has no extra context
        // here, so the safe wire-side claim is Unspecified.  The open
        // path resolves Auto into a concrete value before calling this
        // function in production.
        CHECK(Ntv2Vpid::toVpidTransfer(TransferCharacteristics::Auto)
              == static_cast<int>(NTV2_VPID_TC_Unspecified));
}

TEST_CASE("Ntv2Vpid: ColorPrimaries → VPID → ColorPrimaries round-trip") {
        // BT.709 and BT.2020 round-trip cleanly through the
        // Rec709 / UHDTV VPID codes.
        CHECK(Ntv2Vpid::toVpidColorimetry(ColorPrimaries::BT709)
              == static_cast<int>(NTV2_VPID_Color_Rec709));
        CHECK(Ntv2Vpid::fromVpidColorimetry(static_cast<int>(NTV2_VPID_Color_Rec709))
              == ColorPrimaries::BT709);

        CHECK(Ntv2Vpid::toVpidColorimetry(ColorPrimaries::BT2020)
              == static_cast<int>(NTV2_VPID_Color_UHDTV));
        CHECK(Ntv2Vpid::fromVpidColorimetry(static_cast<int>(NTV2_VPID_Color_UHDTV))
              == ColorPrimaries::BT2020);
}

TEST_CASE("Ntv2Vpid: unknown / SDR-era primaries collapse to NTV2_VPID_Color_Unknown") {
        // Every promeki primary outside {BT709, BT2020} lands on the
        // Unknown VPID code — VPID colorimetry only has two named
        // values and "everything else" must use Unknown rather than
        // misidentifying as Rec709 or UHDTV.
        const ColorPrimaries others[] = {
                ColorPrimaries::Unspecified, ColorPrimaries::BT470M,
                ColorPrimaries::BT470BG,    ColorPrimaries::SMPTE170M,
                ColorPrimaries::SMPTE240M,  ColorPrimaries::Film,
                ColorPrimaries::SMPTE428,   ColorPrimaries::SMPTE431,
                ColorPrimaries::SMPTE432,   ColorPrimaries::JEDEC_P22,
        };
        for (const ColorPrimaries &cp : others) {
                CHECK(Ntv2Vpid::toVpidColorimetry(cp) == static_cast<int>(NTV2_VPID_Color_Unknown));
        }

        // Reverse maps Unknown / Reserved to Unspecified.
        CHECK(Ntv2Vpid::fromVpidColorimetry(static_cast<int>(NTV2_VPID_Color_Unknown))
              == ColorPrimaries::Unspecified);
        CHECK(Ntv2Vpid::fromVpidColorimetry(static_cast<int>(NTV2_VPID_Color_Reserved))
              == ColorPrimaries::Unspecified);
}

TEST_CASE("Ntv2Vpid: MatrixCoefficients → Luminance round-trip") {
        // Only SMPTE2085 (ICtCp) triggers the ICtCp luminance flag.
        CHECK(Ntv2Vpid::toVpidLuminance(MatrixCoefficients::SMPTE2085)
              == static_cast<int>(NTV2_VPID_Luminance_ICtCp));
        CHECK(Ntv2Vpid::fromVpidLuminance(static_cast<int>(NTV2_VPID_Luminance_ICtCp))
              == MatrixCoefficients::SMPTE2085);

        // Every other matrix maps to YCbCr.
        const MatrixCoefficients ycbcrShapes[] = {
                MatrixCoefficients::BT709,      MatrixCoefficients::BT2020_NCL,
                MatrixCoefficients::BT2020_CL,  MatrixCoefficients::SMPTE170M,
                MatrixCoefficients::SMPTE240M,  MatrixCoefficients::FCC,
                MatrixCoefficients::RGB,        MatrixCoefficients::Unspecified,
        };
        for (const MatrixCoefficients &mc : ycbcrShapes) {
                CHECK(Ntv2Vpid::toVpidLuminance(mc) == static_cast<int>(NTV2_VPID_Luminance_YCbCr));
        }

        // Reverse YCbCr → BT709 (the canonical YCbCr matrix on SDI).
        CHECK(Ntv2Vpid::fromVpidLuminance(static_cast<int>(NTV2_VPID_Luminance_YCbCr))
              == MatrixCoefficients::BT709);
}

TEST_CASE("Ntv2Vpid: VideoRange → RGB range round-trip") {
        // Full → Full; Limited and Unknown both → Narrow per the
        // SMPTE ST 352 convention.
        CHECK(Ntv2Vpid::toVpidRgbRange(VideoRange::Full)
              == static_cast<int>(NTV2_VPID_Range_Full));
        CHECK(Ntv2Vpid::toVpidRgbRange(VideoRange::Limited)
              == static_cast<int>(NTV2_VPID_Range_Narrow));
        CHECK(Ntv2Vpid::toVpidRgbRange(VideoRange::Unknown)
              == static_cast<int>(NTV2_VPID_Range_Narrow));

        // Reverse: Full → Full; Narrow → Limited (no way to recover
        // the original Unknown — Limited is the safest concrete claim).
        CHECK(Ntv2Vpid::fromVpidRgbRange(static_cast<int>(NTV2_VPID_Range_Full))
              == VideoRange::Full);
        CHECK(Ntv2Vpid::fromVpidRgbRange(static_cast<int>(NTV2_VPID_Range_Narrow))
              == VideoRange::Limited);
}

TEST_CASE("Ntv2Vpid: out-of-range integers fall through to safe defaults") {
        // A garbage int (e.g. uninitialised register read) should
        // never assert or crash — the mappings clamp to the safest
        // "I don't know" claim.
        CHECK(Ntv2Vpid::fromVpidTransfer(99) == TransferCharacteristics::Unspecified);
        CHECK(Ntv2Vpid::fromVpidColorimetry(99) == ColorPrimaries::Unspecified);
        CHECK(Ntv2Vpid::fromVpidLuminance(99) == MatrixCoefficients::BT709);
        CHECK(Ntv2Vpid::fromVpidRgbRange(99) == VideoRange::Limited);
}

#endif // PROMEKI_ENABLE_NTV2
