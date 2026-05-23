/**
 * @file      videoformatdetails.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/videoformatdetails.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// SMPTE 274M (HD 1080-line) → 1125 total lines.
// SMPTE 296M (HD 720-line) → 750 total lines.
// SMPTE 125M (NTSC SD 525-line) → 525 total lines.
// SMPTE 259M (PAL SD 625-line) → 625 total lines.
// SMPTE 2036 / 2110-20 (UHD 2160-line) → 2250 total lines.
// SMPTE 2036 / 2110-20 (UHD8K 4320-line) → 4500 total lines.
// DCI 2K / 4K / 8K share the matching SMPTE broadcast total-line
// count (DCI 2048×1080 ≡ HD timing, etc.).
int totalLinesForRasterImpl(VideoFormat::WellKnownRaster raster) {
        switch (raster) {
                case VideoFormat::Raster_SD525: return 525;
                case VideoFormat::Raster_SD625: return 625;
                case VideoFormat::Raster_HD720: return 750;
                case VideoFormat::Raster_HD: return 1125;
                case VideoFormat::Raster_2K: return 1125;
                case VideoFormat::Raster_UHD: return 2250;
                case VideoFormat::Raster_4K: return 2250;
                case VideoFormat::Raster_UHD8K: return 4500;
                case VideoFormat::Raster_8K: return 4500;
                case VideoFormat::Raster_QHD: // Not SMPTE-standardised.
                case VideoFormat::Raster_Invalid:
                case VideoFormat::Raster_NotWellKnown:
                default: return 0;
        }
}

int activeLinesForRasterImpl(VideoFormat::WellKnownRaster raster) {
        // Active lines for SMPTE-standardised rasters match the
        // raster height exactly.  Return 0 for any raster that
        // @ref totalLinesForRasterImpl rejects, so the two stay
        // in lockstep (callers treat @c activeLines == 0 as "no
        // SMPTE timing details available").
        switch (raster) {
                case VideoFormat::Raster_SD525: return 486;
                case VideoFormat::Raster_SD625: return 576;
                case VideoFormat::Raster_HD720: return 720;
                case VideoFormat::Raster_HD: return 1080;
                case VideoFormat::Raster_2K: return 1080;
                case VideoFormat::Raster_UHD: return 2160;
                case VideoFormat::Raster_4K: return 2160;
                case VideoFormat::Raster_UHD8K: return 4320;
                case VideoFormat::Raster_8K: return 4320;
                case VideoFormat::Raster_QHD:
                case VideoFormat::Raster_Invalid:
                case VideoFormat::Raster_NotWellKnown:
                default: return 0;
        }
}

} // namespace

VideoFormatDetails::VideoFormatDetails(const VideoFormat &fmt) : _format(fmt) {
        lookupFromRaster(fmt.wellKnownRaster());
}

VideoFormatDetails::VideoFormatDetails(VideoFormat::WellKnownRaster raster) {
        lookupFromRaster(raster);
}

void VideoFormatDetails::lookupFromRaster(VideoFormat::WellKnownRaster raster) {
        _totalLines = totalLinesForRasterImpl(raster);
        _activeLines = activeLinesForRasterImpl(raster);
}

int VideoFormatDetails::totalLinesForRaster(VideoFormat::WellKnownRaster raster) {
        return totalLinesForRasterImpl(raster);
}

PROMEKI_NAMESPACE_END
