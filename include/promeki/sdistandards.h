/**
 * @file      sdistandards.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/enums.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @file
 *
 * @brief Lightweight enum-only helpers for the SDI carrier and wire
 *        format enums.
 *
 * These free functions are pure functions of @ref SdiLinkStandard and
 * @ref SdiWireFormat — no @c SdiSignalConfig / @c VideoPortRef / list
 * machinery is dragged in.  Backends that only need to ask "how many
 * cables does this standard occupy?" or "how many bits per pixel does
 * this wire payload carry?" should include this header rather than the
 * heavier @c sdisignalconfig.h.
 *
 * All names are @c sdi -prefixed so they read unambiguously next to
 * other domain helpers (HDMI, NDI) and do not collide with the more
 * generic @c PixelFormat::bitsPerPixel.
 */

// ============================================================================
// SdiLinkStandard helpers
// ============================================================================

/**
 * @brief Returns the number of physical cables a given SDI link
 *        standard requires.
 *
 * Returns @c 0 for @ref SdiLinkStandard::Auto, which is treated as
 * "unspecified" (the cable count is supplied by the offered
 * @ref MediaDesc on a source, or by the backend's defaults on a sink).
 * Returns @c 1 for any single-link standard, @c 2 for the dual-link
 * variants, and @c 4 for the quad-link variants.
 */
inline int sdiCableCount(const SdiLinkStandard &standard) {
        if (standard == SdiLinkStandard::Auto)      return 0;
        if (standard == SdiLinkStandard::DL_HD)     return 2;
        if (standard == SdiLinkStandard::DL_3GB)    return 2;
        if (standard == SdiLinkStandard::DL_3G)     return 2;
        if (standard == SdiLinkStandard::QL_3G_SQD) return 4;
        if (standard == SdiLinkStandard::QL_3G_2SI) return 4;
        return 1;
}

/**
 * @brief Returns @c true for the dual-link variants
 *        (HD-SDI dual-link, 3G Level B dual-link, dual-link 3G 425-2).
 */
inline bool sdiIsDualLink(const SdiLinkStandard &standard) {
        return standard == SdiLinkStandard::DL_HD ||
               standard == SdiLinkStandard::DL_3GB ||
               standard == SdiLinkStandard::DL_3G;
}

/**
 * @brief Returns @c true for the quad-link variants
 *        (3G Square-Division and 3G 2-Sample Interleave).
 */
inline bool sdiIsQuadLink(const SdiLinkStandard &standard) {
        return standard == SdiLinkStandard::QL_3G_SQD ||
               standard == SdiLinkStandard::QL_3G_2SI;
}

/**
 * @brief Returns the nominal aggregate wire bandwidth, in Gbps,
 *        across every cable a given standard occupies.
 *
 * Multi-link variants return the sum of the per-cable rates so the
 * value reflects total physical bandwidth, not per-link bandwidth.
 * For example dual-link 3G (425-2) returns @c 5.94 (2 × 2.97), and
 * quad-link 3G returns @c 11.88 (4 × 2.97).  Returns @c 0.0 for
 * @ref SdiLinkStandard::Auto.
 *
 * Values reflect the canonical "fractional" payload rates used in
 * production specs; the underlying NRZ symbol rates (e.g. 2.9700 vs.
 * 2.9700/1.001) are not material at this descriptor level.
 */
inline double sdiNominalDataRateGbps(const SdiLinkStandard &standard) {
        if (standard == SdiLinkStandard::Auto)      return 0.0;
        if (standard == SdiLinkStandard::SL_SD)     return 0.270;
        if (standard == SdiLinkStandard::SL_HD)     return 1.485;
        if (standard == SdiLinkStandard::DL_HD)     return 2.970;  // 2 × 1.485
        if (standard == SdiLinkStandard::SL_3GA)    return 2.970;
        if (standard == SdiLinkStandard::SL_3GB)    return 2.970;
        if (standard == SdiLinkStandard::DL_3GB)    return 2.970;  // 2 × 1.485
        if (standard == SdiLinkStandard::DL_3G)     return 5.940;  // 2 × 2.97
        if (standard == SdiLinkStandard::QL_3G_SQD) return 11.880; // 4 × 2.97
        if (standard == SdiLinkStandard::QL_3G_2SI) return 11.880; // 4 × 2.97
        if (standard == SdiLinkStandard::SL_6G)     return 5.940;
        if (standard == SdiLinkStandard::SL_12G)    return 11.880;
        if (standard == SdiLinkStandard::SL_24G)    return 23.760;
        return 0.0;
}

// ============================================================================
// SdiWireFormat helpers
// ============================================================================

/**
 * @brief Returns the intrinsic per-pixel bit count for a given SDI
 *        wire payload format.
 *
 * Used for wire-bandwidth math (@c sdiBitsPerPixel × @c pixels/sec
 * gives the active payload rate that the SDI link standard's
 * @ref sdiNominalDataRateGbps must accommodate, plus SDI framing
 * overhead).  Returns @c 0 for @ref SdiWireFormat::Auto.
 *
 * | Wire format       | bpp |
 * |-------------------|-----|
 * | YCbCr_422_10      |  20 |
 * | YCbCr_422_12      |  24 |
 * | YCbCr_444_10      |  30 |
 * | YCbCr_444_12      |  36 |
 * | RGB_444_10        |  30 |
 * | RGB_444_12        |  36 |
 * | RGBA_444_10       |  40 |
 */
inline int sdiBitsPerPixel(const SdiWireFormat &wf) {
        if (wf == SdiWireFormat::YCbCr_422_10) return 20;
        if (wf == SdiWireFormat::YCbCr_422_12) return 24;
        if (wf == SdiWireFormat::YCbCr_444_10) return 30;
        if (wf == SdiWireFormat::YCbCr_444_12) return 36;
        if (wf == SdiWireFormat::RGB_444_10)   return 30;
        if (wf == SdiWireFormat::RGB_444_12)   return 36;
        if (wf == SdiWireFormat::RGBA_444_10)  return 40;
        return 0;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
