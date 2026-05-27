/**
 * @file      sdiwireinference.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdiwireinference.h>

#include <promeki/colormodel.h>
#include <promeki/framerate.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/size2d.h>
#include <promeki/videoformat.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// 18% SDI / TRS / ANC overhead; see header @par.
constexpr double kSdiOverheadFactor = 1.18;

// SDI's lowest payload precision is 10-bit.  Framebuffers carrying 8
// or 10-bit samples both land on 10-bit on the wire (8-bit is zero-
// padded on egress).  11- and 12-bit framebuffers land on 12-bit.
// Anything above 12 collapses back to 12 since the SDI spec family
// does not define a higher single-cable payload precision.
int wireBitTier(int compBits) {
        if (compBits <= 0)  return 0;
        if (compBits <= 10) return 10;
        return 12;
}

} // namespace

SdiWireFormat sdiWireFormatFor(const PixelFormat &pf) {
        if (!pf.isValid()) return SdiWireFormat::Auto;

        const PixelMemLayout         layout = pf.memLayout();
        const PixelMemLayout::Sampling samp = layout.sampling();
        const ColorModel::Type       cmType = pf.colorModel().type();
        if (layout.compCount() == 0)        return SdiWireFormat::Auto;
        const int   comp0Bits               = static_cast<int>(layout.compDesc(0).bits);
        const int   wireBits                = wireBitTier(comp0Bits);
        if (wireBits == 0)                  return SdiWireFormat::Auto;

        // Map the colour family + sampling + bit tier to a defined
        // SDI wire payload.  Sampling modes the SDI family does not
        // carry (4:2:0, 4:1:1) fall through to Auto so callers can
        // detect them and surface an explicit "not carriable" error
        // rather than silently picking a wrong standard.
        if (cmType == ColorModel::TypeYCbCr) {
                if (samp == PixelMemLayout::Sampling422) {
                        return wireBits == 10 ? SdiWireFormat::YCbCr_422_10
                                              : SdiWireFormat::YCbCr_422_12;
                }
                if (samp == PixelMemLayout::Sampling444) {
                        return wireBits == 10 ? SdiWireFormat::YCbCr_444_10
                                              : SdiWireFormat::YCbCr_444_12;
                }
                return SdiWireFormat::Auto;
        }

        if (cmType == ColorModel::TypeRGB) {
                // RGB framestores are always 4:4:4 (the @c Sampling
                // dimension only applies to YCbCr in practice).
                if (pf.hasAlpha() && wireBits == 10) {
                        return SdiWireFormat::RGBA_444_10;
                }
                return wireBits == 10 ? SdiWireFormat::RGB_444_10
                                      : SdiWireFormat::RGB_444_12;
        }

        // Other colour families (XYZ, Lab, HSV, HSL) — no SDI carrier
        // defined.
        return SdiWireFormat::Auto;
}

SdiLinkStandard inferSdiLinkStandard(const VideoFormat &fmt, const SdiWireFormat &wireFormat,
                                     int cableCount) {
        if (!fmt.isValid())                    return SdiLinkStandard::Auto;
        if (wireFormat == SdiWireFormat::Auto) return SdiLinkStandard::Auto;
        if (cableCount <= 0)                   return SdiLinkStandard::Auto;

        const int bpp = sdiBitsPerPixel(wireFormat);
        if (bpp <= 0) return SdiLinkStandard::Auto;

        const Size2Du32 &raster = fmt.raster();
        const double pixelsPerSec = static_cast<double>(raster.width())
                                    * static_cast<double>(raster.height())
                                    * fmt.frameRate().toDouble();
        const double payloadGbps  = pixelsPerSec * static_cast<double>(bpp) / 1.0e9;
        const double requiredGbps = payloadGbps * kSdiOverheadFactor;

        if (cableCount == 1) {
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::SL_SD))  return SdiLinkStandard::SL_SD;
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::SL_HD))  return SdiLinkStandard::SL_HD;
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::SL_3GA)) return SdiLinkStandard::SL_3GA;
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::SL_6G))  return SdiLinkStandard::SL_6G;
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::SL_12G)) return SdiLinkStandard::SL_12G;
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::SL_24G)) return SdiLinkStandard::SL_24G;
                return SdiLinkStandard::Auto;
        }
        if (cableCount == 2) {
                // DL standards report aggregate bandwidth (sum across
                // cables); compare requiredGbps against that aggregate.
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::DL_HD)) return SdiLinkStandard::DL_HD;
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::DL_3G)) return SdiLinkStandard::DL_3G;
                return SdiLinkStandard::Auto;
        }
        if (cableCount == 4) {
                if (requiredGbps <= sdiNominalDataRateGbps(SdiLinkStandard::QL_3G_2SI)) {
                        return SdiLinkStandard::QL_3G_2SI;
                }
                return SdiLinkStandard::Auto;
        }
        return SdiLinkStandard::Auto;
}

PROMEKI_NAMESPACE_END
