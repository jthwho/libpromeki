/**
 * @file      videoformatdetails.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/videoformat.h>
#include <promeki/videoformatdetails.h>

using namespace promeki;

TEST_CASE("VideoFormatDetails: default construction is invalid") {
        VideoFormatDetails d;
        CHECK_FALSE(d.isValid());
        CHECK(d.totalLines() == 0);
        CHECK(d.activeLines() == 0);
}

TEST_CASE("VideoFormatDetails: invalid VideoFormat is invalid") {
        VideoFormatDetails d{VideoFormat()};
        CHECK_FALSE(d.isValid());
        CHECK(d.totalLines() == 0);
        CHECK(d.activeLines() == 0);
}

TEST_CASE("VideoFormatDetails: SMPTE 274M HD 1080-line is 1125 / 1080") {
        // Cover the entire 274M 1080-line family — every rate / scan
        // shares the same 1125 total lines per spec.
        const VideoFormat::WellKnownFormat formats[] = {
                VideoFormat::Smpte1080i50,     VideoFormat::Smpte1080i59_94,
                VideoFormat::Smpte1080i60,     VideoFormat::Smpte1080p23_98,
                VideoFormat::Smpte1080p24,     VideoFormat::Smpte1080p25,
                VideoFormat::Smpte1080p29_97,  VideoFormat::Smpte1080p30,
                VideoFormat::Smpte1080p50,     VideoFormat::Smpte1080p59_94,
                VideoFormat::Smpte1080p60,     VideoFormat::Smpte1080psf23_98,
                VideoFormat::Smpte1080psf24,   VideoFormat::Smpte1080psf25,
                VideoFormat::Smpte1080psf29_97,
                VideoFormat::Smpte1080psf30,
        };
        for (auto id : formats) {
                CAPTURE(static_cast<int>(id));
                VideoFormatDetails d{VideoFormat(id)};
                CHECK(d.isValid());
                CHECK(d.totalLines() == 1125);
                CHECK(d.activeLines() == 1080);
        }
}

TEST_CASE("VideoFormatDetails: SMPTE 296M HD 720-line is 750 / 720") {
        const VideoFormat::WellKnownFormat formats[] = {
                VideoFormat::Smpte720p50,
                VideoFormat::Smpte720p59_94,
                VideoFormat::Smpte720p60,
        };
        for (auto id : formats) {
                CAPTURE(static_cast<int>(id));
                VideoFormatDetails d{VideoFormat(id)};
                CHECK(d.isValid());
                CHECK(d.totalLines() == 750);
                CHECK(d.activeLines() == 720);
        }
}

TEST_CASE("VideoFormatDetails: SD 525-line NTSC is 525 / 486") {
        VideoFormatDetails d{VideoFormat(VideoFormat::Smpte486i59_94)};
        CHECK(d.isValid());
        CHECK(d.totalLines() == 525);
        CHECK(d.activeLines() == 486);
}

TEST_CASE("VideoFormatDetails: SD 625-line PAL is 625 / 576") {
        VideoFormatDetails d{VideoFormat(VideoFormat::Smpte576i50)};
        CHECK(d.isValid());
        CHECK(d.totalLines() == 625);
        CHECK(d.activeLines() == 576);
}

TEST_CASE("VideoFormatDetails: UHD 2160-line is 2250 / 2160") {
        const VideoFormat::WellKnownFormat formats[] = {
                VideoFormat::Smpte2160p23_98, VideoFormat::Smpte2160p24,
                VideoFormat::Smpte2160p25,    VideoFormat::Smpte2160p29_97,
                VideoFormat::Smpte2160p30,    VideoFormat::Smpte2160p50,
                VideoFormat::Smpte2160p59_94, VideoFormat::Smpte2160p60,
        };
        for (auto id : formats) {
                CAPTURE(static_cast<int>(id));
                VideoFormatDetails d{VideoFormat(id)};
                CHECK(d.isValid());
                CHECK(d.totalLines() == 2250);
                CHECK(d.activeLines() == 2160);
        }
}

TEST_CASE("VideoFormatDetails: UHD8K 4320-line is 4500 / 4320") {
        const VideoFormat::WellKnownFormat formats[] = {
                VideoFormat::Smpte4320p23_98, VideoFormat::Smpte4320p24,
                VideoFormat::Smpte4320p25,    VideoFormat::Smpte4320p29_97,
                VideoFormat::Smpte4320p30,    VideoFormat::Smpte4320p50,
                VideoFormat::Smpte4320p59_94, VideoFormat::Smpte4320p60,
        };
        for (auto id : formats) {
                CAPTURE(static_cast<int>(id));
                VideoFormatDetails d{VideoFormat(id)};
                CHECK(d.isValid());
                CHECK(d.totalLines() == 4500);
                CHECK(d.activeLines() == 4320);
        }
}

TEST_CASE("VideoFormatDetails: DCI 2K shares HD timing (1125 / 1080)") {
        VideoFormatDetails d{VideoFormat(VideoFormat::Dci2Kp24)};
        CHECK(d.isValid());
        CHECK(d.totalLines() == 1125);
        CHECK(d.activeLines() == 1080);
}

TEST_CASE("VideoFormatDetails: DCI 4K shares UHD timing (2250 / 2160)") {
        VideoFormatDetails d{VideoFormat(VideoFormat::Dci4Kp24)};
        CHECK(d.isValid());
        CHECK(d.totalLines() == 2250);
        CHECK(d.activeLines() == 2160);
}

TEST_CASE("VideoFormatDetails: QHD 1440 has no SMPTE timing entry") {
        // QHD is not SMPTE-standardised; the constructor returns an
        // invalid details object so callers know they need a fallback.
        VideoFormatDetails d{VideoFormatDetails(VideoFormat::Raster_QHD)};
        CHECK_FALSE(d.isValid());
        CHECK(d.totalLines() == 0);
        CHECK(d.activeLines() == 0);
}

TEST_CASE("VideoFormatDetails: totalLinesForRaster static accessor") {
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_HD) == 1125);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_UHD) == 2250);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_HD720) == 750);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_SD525) == 525);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_SD625) == 625);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_2K) == 1125);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_4K) == 2250);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_UHD8K) == 4500);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_8K) == 4500);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_QHD) == 0);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_Invalid) == 0);
        CHECK(VideoFormatDetails::totalLinesForRaster(VideoFormat::Raster_NotWellKnown) == 0);
}

TEST_CASE("VideoFormatDetails: format() returns the underlying VideoFormat") {
        VideoFormat        fmt(VideoFormat::Smpte1080p29_97);
        VideoFormatDetails d{fmt};
        CHECK(d.format() == fmt);
}
