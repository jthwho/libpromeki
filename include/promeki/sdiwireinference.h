/**
 * @file      sdiwireinference.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/enums_video.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

class VideoFormat;
class PixelFormat;

/**
 * @brief Returns the SDI wire-payload format that naturally carries
 *        a given framebuffer @ref PixelFormat after the on-board
 *        pack/unpack step.
 *
 * The framebuffer's bit depth and chroma subsampling drive the
 * choice — V210 / UYVY-10 / planar 10-bit 4:2:2 all map to
 * @ref SdiWireFormat::YCbCr_422_10 (the canonical SDI payload, with
 * SDI's 10-bit minimum absorbing any 8-bit framebuffer source by
 * zero-padding on egress).  RGB framebuffers map to the matching
 * RGB 4:4:4 wire format; an on-board CSC widget (when enabled)
 * later converts that to YCbCr on the routing fabric, but
 * @ref sdiWireFormatFor describes the natural / no-CSC path.
 *
 * Returns @ref SdiWireFormat::Auto when @p pf is invalid, when it's
 * a compressed format, or when it doesn't map onto a defined SDI
 * wire payload (e.g. half-float linear, YCbCr 4:2:0 — no SDI carrier
 * defined for those).
 *
 * @par Mapping table
 *
 * | Framebuffer category                       | Wire format       |
 * |--------------------------------------------|-------------------|
 * | 8/10-bit YCbCr 4:2:2 (interleaved / packed)| YCbCr_422_10      |
 * | 12-bit YCbCr 4:2:2                         | YCbCr_422_12      |
 * | 10-bit YCbCr 4:4:4                         | YCbCr_444_10      |
 * | 12-bit YCbCr 4:4:4                         | YCbCr_444_12      |
 * | 8/10-bit RGB 4:4:4                         | RGB_444_10        |
 * | 12-bit RGB 4:4:4                           | RGB_444_12        |
 * | 16-bit RGB 4:4:4                           | RGB_444_12        |
 * | 10-bit RGBA / ARGB                         | RGBA_444_10       |
 * | YCbCr 4:2:0 (planar / NV12), float, codecs | Auto              |
 *
 * @param pf  Framebuffer pixel format.
 * @return    The natural SDI wire-payload format, or @c Auto.
 */
SdiWireFormat sdiWireFormatFor(const PixelFormat &pf);

/**
 * @brief Picks the smallest SDI link standard that can carry a video
 *        signal of the given format and wire payload across the
 *        given number of physical cables.
 *
 * Intended for backends that need to resolve
 * @ref SdiLinkStandard::Auto on a sink: the wire standard is forced
 * by the offered raster + rate + cable count + wire payload, so the
 * standard is derivable.  Returns @ref SdiLinkStandard::Auto when
 * the bandwidth cannot be carried on the requested cable count
 * (caller should surface an explicit error), or when the inputs are
 * malformed.
 *
 * @par Bandwidth estimate
 *
 * Computes the active payload as
 * @c pixels-per-frame @c × @c frame-rate @c × @c sdiBitsPerPixel(wireFormat),
 * adds an 18% SDI / TRS / ANC overhead (the active-payload ratio is
 * ~84% — 1.243 Gbps / 1.485 Gbps for 1080i59.94 over HD-SDI), and
 * compares against the per-standard nominal wire rate from
 * @ref sdiNominalDataRateGbps.  The 1.18 multiplier leaves a small
 * safety margin so 1.001 timebase rounding doesn't push canonical
 * formats up a band.
 *
 * Interlaced and PsF formats are treated like progressive at the
 * same frame rate, since SDI delivers the same total payload per
 * frame regardless of scan ordering — the @ref FrameRate carried
 * inside @ref VideoFormat is always the frame rate (never the
 * field rate), so 1080i59.94 contributes a 29.97 fps payload.
 *
 * @par Quad-link disambiguation
 *
 * Two-Sample Interleave (@ref SdiLinkStandard::QL_3G_2SI) is
 * preferred over Square Division (@ref SdiLinkStandard::QL_3G_SQD)
 * when both fit, since 2SI is the modern default and what every
 * shipping 4K camera / monitor emits.
 *
 * @param fmt          Video format (raster + frame rate + scan).
 * @param wireFormat   SDI wire payload that will travel on the
 *                     cable — usually the result of
 *                     @ref sdiWireFormatFor on the framebuffer
 *                     @ref PixelFormat, or an explicit choice when
 *                     an on-board CSC reshapes the wire payload.
 * @param cableCount   Number of physical cables available
 *                     (1, 2, or 4).
 * @return The smallest fitting standard, or
 *         @ref SdiLinkStandard::Auto when inference fails.
 */
SdiLinkStandard inferSdiLinkStandard(const VideoFormat &fmt, const SdiWireFormat &wireFormat,
                                     int cableCount);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
