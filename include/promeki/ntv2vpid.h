/**
 * @file      ntv2vpid.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/enums_color.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Pure mapping helpers between promeki's colour-description
 *        enums and AJA's VPID override values.
 * @ingroup proav
 *
 * SMPTE ST 352 VPID is a 4-byte payload identifier embedded on every
 * line of an SDI signal.  Byte 4 carries the HDR / WCG signalling that
 * downstream receivers (monitors, switchers, ASICs) inspect to pick
 * their colour processing.  AJA exposes four narrow knobs that
 * override the auto-derived VPID byte 4 fields:
 *
 *  - **Transfer characteristic** — SDR vs PQ vs HLG
 *    (@c NTV2VPIDTransferCharacteristics).
 *  - **Colorimetry** — Rec.709 vs UHDTV (Rec.2020) vs reserved /
 *    unknown (@c NTV2VPIDColorimetry).
 *  - **Luminance** — YCbCr vs ICtCp (@c NTV2VPIDLuminance).
 *  - **RGB range** — narrow (legal) vs full (extended)
 *    (@c NTV2VPIDRGBRange).
 *
 * The mappings live in their own header so other NTV2 files (the
 * MediaIO open path on both source and sink, plus the unit tests)
 * can share one canonical translation table.  All helpers are pure
 * (no state, no logging, no SDK calls) and return @c int instead of
 * the AJA enum types so callers in non-NTV2 TUs don't need the
 * libajantv2 headers — the @c .cpp casts at the boundary.  Sentinel
 * values mirror AJA's own "unspecified" / "unknown" SDK constants so
 * a caller's "I don't know" intent round-trips cleanly.
 *
 * @see Ntv2MediaIO open paths (`openSource` reads input VPID and
 *      stamps Frame metadata; `openSink` writes the per-channel
 *      VPID overrides from ImageDesc colorimetry + config).
 * @see thirdparty/libajantv2/ajantv2/includes/ntv2enums.h for the
 *      @c NTV2VPIDTransferCharacteristics / @c NTV2VPIDColorimetry /
 *      @c NTV2VPIDLuminance / @c NTV2VPIDRGBRange enum values.
 */
namespace Ntv2Vpid {

        // ---- Transfer characteristic ----

        /**
         * @brief Maps a promeki @ref TransferCharacteristics to an AJA
         *        @c NTV2VPIDTransferCharacteristics (returned as @c int).
         *
         * Only PQ (@c SMPTE2084) and HLG (@c ARIB_STD_B67) map to
         * dedicated VPID codes; every SDR variant (BT709, SRGB,
         * BT2020_10, BT2020_12, SMPTE170M, Gamma22, Gamma28, etc.)
         * collapses to @c NTV2_VPID_TC_SDR_TV per the SMPTE ST 352
         * convention.  @ref TransferCharacteristics::Unspecified maps
         * to @c NTV2_VPID_TC_Unspecified so a receiver sees "no claim
         * about transfer" rather than a misleading SDR claim.
         */
        int toVpidTransfer(const TransferCharacteristics &tc);

        /**
         * @brief Inverse of @ref toVpidTransfer.
         *
         * SDR collapses to @c TransferCharacteristics::BT709 (the
         * most common interpretation when a wire claims "SDR" without
         * further detail); unspecified stays unspecified.  Unknown
         * AJA codes return @c TransferCharacteristics::Unspecified.
         */
        TransferCharacteristics fromVpidTransfer(int vpidTransfer);

        // ---- Colorimetry ----

        /**
         * @brief Maps a promeki @ref ColorPrimaries to an AJA
         *        @c NTV2VPIDColorimetry (returned as @c int).
         *
         * @c BT709 → @c NTV2_VPID_Color_Rec709;
         * @c BT2020 → @c NTV2_VPID_Color_UHDTV;
         * everything else → @c NTV2_VPID_Color_Unknown.  Note that
         * SMPTE / DCI primaries (SMPTE428 / 431 / 432) don't have a
         * dedicated VPID code — they go through the @c Unknown path
         * since VPID was specified before those primaries became
         * common on the wire.
         */
        int toVpidColorimetry(const ColorPrimaries &cp);

        /**
         * @brief Inverse of @ref toVpidColorimetry.
         *
         * @c Rec709 → @c ColorPrimaries::BT709;
         * @c UHDTV → @c ColorPrimaries::BT2020;
         * @c Reserved / @c Unknown → @c ColorPrimaries::Unspecified.
         */
        ColorPrimaries fromVpidColorimetry(int vpidColorimetry);

        // ---- Luminance (YCbCr vs ICtCp) ----

        /**
         * @brief Maps a promeki @ref MatrixCoefficients value to the
         *        AJA @c NTV2VPIDLuminance flag (returned as @c int).
         *
         * Only the @c SMPTE2085 ICtCp matrix code triggers the ICtCp
         * VPID flag; everything else (including @c Unspecified) maps
         * to @c NTV2_VPID_Luminance_YCbCr.  Most pipelines never
         * touch this — YCbCr is the overwhelming default — but PQ /
         * BT.2100 ICtCp paths need the override so receivers parse
         * the wire correctly.
         */
        int toVpidLuminance(const MatrixCoefficients &mc);

        /**
         * @brief Inverse of @ref toVpidLuminance.
         *
         * @c NTV2_VPID_Luminance_ICtCp → @c MatrixCoefficients::SMPTE2085;
         * @c NTV2_VPID_Luminance_YCbCr → @c MatrixCoefficients::BT709
         * (the most common YCbCr matrix on SDI).
         */
        MatrixCoefficients fromVpidLuminance(int vpidLuminance);

        // ---- RGB range ----

        /**
         * @brief Maps a promeki @ref VideoRange to an AJA
         *        @c NTV2VPIDRGBRange (returned as @c int).
         *
         * @c Full → @c NTV2_VPID_Range_Full;
         * everything else (including @c Unknown) → @c NTV2_VPID_Range_Narrow.
         * "Unknown defaults to narrow" matches the SMPTE ST 352
         * convention for legacy SDR pipelines that don't carry an
         * explicit range claim.
         */
        int toVpidRgbRange(const VideoRange &vr);

        /**
         * @brief Inverse of @ref toVpidRgbRange.
         *
         * @c NTV2_VPID_Range_Full → @c VideoRange::Full;
         * @c NTV2_VPID_Range_Narrow → @c VideoRange::Limited.
         */
        VideoRange fromVpidRgbRange(int vpidRange);

} // namespace Ntv2Vpid

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
