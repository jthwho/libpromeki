/**
 * @file      ntv2vpid.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/ntv2vpid.h>

#include <ntv2enums.h>

PROMEKI_NAMESPACE_BEGIN

namespace Ntv2Vpid {

        int toVpidTransfer(const TransferCharacteristics &tc) {
                if (tc == TransferCharacteristics::SMPTE2084) {
                        return static_cast<int>(NTV2_VPID_TC_PQ);
                }
                if (tc == TransferCharacteristics::ARIB_STD_B67) {
                        return static_cast<int>(NTV2_VPID_TC_HLG);
                }
                if (tc == TransferCharacteristics::Unspecified || tc == TransferCharacteristics::Auto) {
                        return static_cast<int>(NTV2_VPID_TC_Unspecified);
                }
                // Every other H.273 transfer (BT709, SRGB, BT2020_10,
                // BT2020_12, SMPTE170M, Gamma22/28, Linear, Log, etc.)
                // collapses to "SDR TV" on the wire — the VPID byte 4
                // transfer field has no finer granularity in SMPTE
                // ST 352 today.
                return static_cast<int>(NTV2_VPID_TC_SDR_TV);
        }

        TransferCharacteristics fromVpidTransfer(int vpidTransfer) {
                switch (static_cast<NTV2VPIDTransferCharacteristics>(vpidTransfer)) {
                        case NTV2_VPID_TC_PQ:
                                return TransferCharacteristics::SMPTE2084;
                        case NTV2_VPID_TC_HLG:
                                return TransferCharacteristics::ARIB_STD_B67;
                        case NTV2_VPID_TC_SDR_TV:
                                // VPID "SDR TV" is the SMPTE-default
                                // SDR — Rec.709 is the strongest claim
                                // we can derive from that single bit
                                // without inspecting the colorimetry
                                // field too.  Callers that need the
                                // refined claim (BT2020 SDR, etc.)
                                // also consult @ref fromVpidColorimetry.
                                return TransferCharacteristics::BT709;
                        case NTV2_VPID_TC_Unspecified:
                        default:
                                return TransferCharacteristics::Unspecified;
                }
        }

        int toVpidColorimetry(const ColorPrimaries &cp) {
                if (cp == ColorPrimaries::BT709) {
                        return static_cast<int>(NTV2_VPID_Color_Rec709);
                }
                if (cp == ColorPrimaries::BT2020) {
                        return static_cast<int>(NTV2_VPID_Color_UHDTV);
                }
                // Everything else (Unspecified, SMPTE170M, BT470*,
                // DCI primaries, etc.) maps to Unknown — VPID
                // colorimetry has no finer granularity.  A SMPTE170M
                // SD signal would still ride out with Rec.709 VPID by
                // convention since the receiver path is the same.
                return static_cast<int>(NTV2_VPID_Color_Unknown);
        }

        ColorPrimaries fromVpidColorimetry(int vpidColorimetry) {
                switch (static_cast<NTV2VPIDColorimetry>(vpidColorimetry)) {
                        case NTV2_VPID_Color_Rec709:
                                return ColorPrimaries::BT709;
                        case NTV2_VPID_Color_UHDTV:
                                return ColorPrimaries::BT2020;
                        case NTV2_VPID_Color_Reserved:
                        case NTV2_VPID_Color_Unknown:
                        default:
                                return ColorPrimaries::Unspecified;
                }
        }

        int toVpidLuminance(const MatrixCoefficients &mc) {
                if (mc == MatrixCoefficients::SMPTE2085) {
                        return static_cast<int>(NTV2_VPID_Luminance_ICtCp);
                }
                return static_cast<int>(NTV2_VPID_Luminance_YCbCr);
        }

        MatrixCoefficients fromVpidLuminance(int vpidLuminance) {
                switch (static_cast<NTV2VPIDLuminance>(vpidLuminance)) {
                        case NTV2_VPID_Luminance_ICtCp:
                                return MatrixCoefficients::SMPTE2085;
                        case NTV2_VPID_Luminance_YCbCr:
                        default:
                                // BT.709 is the most common YCbCr
                                // matrix on the wire; callers that
                                // want to refine to BT.2020 NCL etc.
                                // consult colorimetry + frame
                                // metadata too.
                                return MatrixCoefficients::BT709;
                }
        }

        int toVpidRgbRange(const VideoRange &vr) {
                if (vr == VideoRange::Full) {
                        return static_cast<int>(NTV2_VPID_Range_Full);
                }
                // Limited and Unknown both map to narrow per the
                // SMPTE ST 352 convention.
                return static_cast<int>(NTV2_VPID_Range_Narrow);
        }

        VideoRange fromVpidRgbRange(int vpidRange) {
                switch (static_cast<NTV2VPIDRGBRange>(vpidRange)) {
                        case NTV2_VPID_Range_Full:
                                return VideoRange::Full;
                        case NTV2_VPID_Range_Narrow:
                        default:
                                return VideoRange::Limited;
                }
        }

} // namespace Ntv2Vpid

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
