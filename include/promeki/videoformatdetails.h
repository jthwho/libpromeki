/**
 * @file      videoformatdetails.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/videoformat.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief SMPTE timing details for a @ref VideoFormat.
 * @ingroup video
 *
 * @ref VideoFormat captures @c (raster, frame_rate, scan_mode) — the
 * identification triple for a video signal — but does not on its own
 * know how many physical lines per frame the SMPTE wire format puts on
 * the SDI grid.  @ref VideoFormatDetails extends that identification
 * with the @em physical timing facts SMPTE 274M / 296M / 2036 /
 * 2110-20 specify for each well-known raster:
 *
 * - @ref totalLines — total lines per frame in the SDI signal,
 *   including vertical blanking.  Drives the ST 2110-40 LLTM §6.4
 *   @c T_D computation (<tt>T_D = 8 / (FrameRate * TotalLines)</tt>),
 *   ST 2110-21 N-network egress timing, and ST 291 line-number range
 *   checks.
 * - @ref activeLines — visible lines per frame.  Equal to the raster
 *   height for SMPTE rasters.
 *
 * @par Lookup is by raster, not by full VideoFormat
 * SMPTE 274M (HD-SDI 1080-line) shares the same 1125 total-lines wire
 * format across all 1080-line frame rates (24, 23.98, 25, 29.97, 30,
 * 50, 59.94, 60) and across progressive / interlaced / PsF scan modes
 * — only the H-blanking varies.  SMPTE 2036 (UHD) does the same with
 * 2250 total lines for every 2160p rate.  The library encodes this by
 * keying details off the raster, not the full format.
 *
 * @par Non-SMPTE rasters
 * Rasters that are not SMPTE-standardised broadcast formats — for
 * example QHD (2560×1440) which is an IT-display resolution rather
 * than a broadcast one — have no canonical total-line count.
 * @ref isValid returns @c false for those; @ref totalLines /
 * @ref activeLines return @c 0.  Callers that need a default in those
 * cases (e.g. so the LLTM @c T_D math does not divide by zero) should
 * substitute a documented fallback before calling.
 *
 * @par Thread Safety
 * Immutable once constructed.  Concurrent reads are safe.
 */
class VideoFormatDetails {
        public:
                /// @brief Default-constructs an invalid details object.
                VideoFormatDetails() = default;

                /**
                 * @brief Constructs details from a @ref VideoFormat.
                 *
                 * The raster portion of @p fmt is looked up against the
                 * SMPTE-standardised raster registry.  Invalid or
                 * non-SMPTE rasters produce an invalid
                 * @ref VideoFormatDetails (@ref isValid @c == false).
                 *
                 * @param fmt The video format to look up.
                 */
                explicit VideoFormatDetails(const VideoFormat &fmt);

                /**
                 * @brief Constructs details from a raster id directly.
                 *
                 * Equivalent to constructing a @ref VideoFormat with a
                 * placeholder rate and looking up that.  Useful for
                 * callers that already know the raster id.
                 *
                 * @param raster Well-known raster id.
                 */
                explicit VideoFormatDetails(VideoFormat::WellKnownRaster raster);

                /**
                 * @brief @c true when the underlying raster is SMPTE-
                 *        standardised and has known total-line timing.
                 *
                 * Returns @c false for an invalid VideoFormat and for
                 * non-SMPTE rasters (e.g. QHD/1440p which is not in
                 * SMPTE 274M / 296M / 2036 / 2110-20).
                 */
                bool isValid() const { return _totalLines > 0; }

                /// @brief The underlying format the details describe.
                const VideoFormat &format() const { return _format; }

                /**
                 * @brief Total lines per frame in the SMPTE wire format,
                 *        including vertical blanking.
                 *
                 * Per-raster constants from SMPTE 274M (HD-SDI 1080-line
                 * → 1125), SMPTE 296M (HD-SDI 720-line → 750), SMPTE
                 * 125M (NTSC SD 525-line → 525), SMPTE 259M (PAL SD
                 * 625-line → 625), SMPTE 2036 / 2110-20 (UHD 2160-line
                 * → 2250, UHD8K 4320-line → 4500).  DCI rasters share
                 * timing with the matching broadcast height: DCI 2K
                 * (2048×1080) uses 1125, DCI 4K (4096×2160) uses 2250,
                 * DCI 8K (8192×4320) uses 4500.
                 *
                 * @return Total lines per frame, or 0 when the raster
                 *         is not SMPTE-standardised.
                 */
                int totalLines() const { return _totalLines; }

                /**
                 * @brief Active (visible) lines per frame.
                 *
                 * Equal to the raster height for SMPTE-standardised
                 * rasters.  For interlaced modes this is the total
                 * active line count across both fields.
                 *
                 * @return Active lines per frame, or 0 when the raster
                 *         is not SMPTE-standardised.
                 */
                int activeLines() const { return _activeLines; }

                /**
                 * @brief Convenience: look up @ref totalLines for a
                 *        raster without constructing the full details
                 *        object.
                 *
                 * @param raster Well-known raster id.
                 * @return Total lines per frame for @p raster, or 0
                 *         when @p raster has no SMPTE timing entry.
                 */
                static int totalLinesForRaster(VideoFormat::WellKnownRaster raster);

        private:
                void lookupFromRaster(VideoFormat::WellKnownRaster raster);

                VideoFormat _format;
                int         _totalLines = 0;
                int         _activeLines = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
