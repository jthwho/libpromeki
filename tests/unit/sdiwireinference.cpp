/**
 * @file      sdiwireinference.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enums_video.h>
#include <promeki/framerate.h>
#include <promeki/pixelformat.h>
#include <promeki/sdistandards.h>
#include <promeki/sdiwireinference.h>
#include <promeki/size2d.h>
#include <promeki/videoformat.h>

using namespace promeki;

// ============================================================================
// sdiWireFormatFor(PixelFormat)
// ============================================================================

TEST_CASE("sdiWireFormatFor: 8/10-bit YCbCr 4:2:2 framestores map to YCbCr_422_10") {
        // 8-bit UYVY zero-pads to 10-bit on the SDI wire.
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709))
              == SdiWireFormat::YCbCr_422_10);
        // 10-bit V210 maps directly to 10-bit wire.
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::YUV10_422_v210_Rec709))
              == SdiWireFormat::YCbCr_422_10);
}

TEST_CASE("sdiWireFormatFor: 8-bit RGB framestores map to RGB_444_10") {
        // SDI does not carry 8-bit RGB natively — the wire is 10-bit
        // minimum, so an 8-bit RGB framestore lands on RGB_444_10.
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::RGB8_sRGB))
              == SdiWireFormat::RGB_444_10);
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::BGR8_sRGB))
              == SdiWireFormat::RGB_444_10);
}

TEST_CASE("sdiWireFormatFor: 10-bit RGB DPX maps to RGB_444_10") {
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::RGB10_DPX_sRGB))
              == SdiWireFormat::RGB_444_10);
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::RGB10_DPX_LE_sRGB))
              == SdiWireFormat::RGB_444_10);
}

TEST_CASE("sdiWireFormatFor: 16-bit RGB collapses to RGB_444_12 (SDI ceiling)") {
        // 16-bit framestores still ride 12-bit wire payloads — SDI
        // does not define a higher single-cable precision.
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::RGB16_LE_sRGB))
              == SdiWireFormat::RGB_444_12);
}

TEST_CASE("sdiWireFormatFor: RGBA 8-bit framestores map to RGBA_444_10") {
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::RGBA8_sRGB))
              == SdiWireFormat::RGBA_444_10);
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::ARGB8_sRGB))
              == SdiWireFormat::RGBA_444_10);
        CHECK(sdiWireFormatFor(PixelFormat(PixelFormat::ABGR8_sRGB))
              == SdiWireFormat::RGBA_444_10);
}

TEST_CASE("sdiWireFormatFor: 4:2:0 / planar / float and other non-SDI formats return Auto") {
        // Invalid framestore.
        CHECK(sdiWireFormatFor(PixelFormat()) == SdiWireFormat::Auto);
}

// ============================================================================
// inferSdiLinkStandard
// ============================================================================
//
// Math: pixels/sec × sdiBitsPerPixel × 1.18 (overhead) ≤ aggregate Gbps.
// At YCbCr_422_10 (20 bpp), this lands every canonical SMPTE raster
// in the band the spec calls for:
//
// | Format              | Payload Gbps | Required (×1.18) | Band         |
// |---------------------|--------------|------------------|--------------|
// | 1080p29.97          | 1.244        | 1.468            | HD-SDI 1.485 |
// | 1080i59.94 (=p29.97)| 1.244        | 1.468            | HD-SDI 1.485 |
// | 1080p59.94          | 2.486        | 2.934            | 3G-SDI 2.97  |
// | UHD p29.97          | 4.976        | 5.872            | 6G-SDI 5.94  |
// | UHD p59.94          | 9.952        | 11.74            | 12G-SDI 11.88|
//

TEST_CASE("inferSdiLinkStandard: canonical YCbCr 4:2:2 10-bit lands in the right band") {
        const SdiWireFormat wf = SdiWireFormat::YCbCr_422_10;

        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte1080p29_97), wf, 1)
              == SdiLinkStandard::SL_HD);

        // Interlaced 59.94 = 29.97 frame rate payload → also HD-SDI.
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte1080i59_94), wf, 1)
              == SdiLinkStandard::SL_HD);

        // PsF is progressive payload in interlaced segments — same band.
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte1080psf29_97), wf, 1)
              == SdiLinkStandard::SL_HD);

        // 1080p59.94 → 3G-SDI single-link.
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte1080p59_94), wf, 1)
              == SdiLinkStandard::SL_3GA);

        // UHD p29.97 → 6G-SDI single-link.
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte2160p29_97), wf, 1)
              == SdiLinkStandard::SL_6G);

        // UHD p59.94 → 12G-SDI single-link.
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte2160p59_94), wf, 1)
              == SdiLinkStandard::SL_12G);

        // 720p59.94 fits in HD-SDI (smaller raster).
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte720p59_94), wf, 1)
              == SdiLinkStandard::SL_HD);

        // SD 525-line NTSC and 625-line PAL fit in SD-SDI (270 Mbps).
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte486i59_94), wf, 1)
              == SdiLinkStandard::SL_SD);
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte576i50), wf, 1)
              == SdiLinkStandard::SL_SD);
}

TEST_CASE("inferSdiLinkStandard: dual-cable picks the right dual-link standard") {
        const SdiWireFormat wf = SdiWireFormat::YCbCr_422_10;

        // 1080p25 → DL-HD (aggregate 2.97 Gbps).
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte1080p25), wf, 2)
              == SdiLinkStandard::DL_HD);

        // 1080p59.94 in 4:2:2 10-bit also fits DL-HD (2.93 < 2.97).
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte1080p59_94), wf, 2)
              == SdiLinkStandard::DL_HD);

        // UHD p29.97 → DL-3G (aggregate 5.94 Gbps).
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte2160p29_97), wf, 2)
              == SdiLinkStandard::DL_3G);
}

TEST_CASE("inferSdiLinkStandard: quad-cable prefers QL_3G_2SI") {
        const SdiWireFormat wf = SdiWireFormat::YCbCr_422_10;

        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte2160p59_94), wf, 4)
              == SdiLinkStandard::QL_3G_2SI);
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte2160p29_97), wf, 4)
              == SdiLinkStandard::QL_3G_2SI);
}

TEST_CASE("inferSdiLinkStandard: wire format drives the bandwidth — RGB at 30 bpp pushes higher") {
        // Same raster + rate, two wire formats:
        // - YCbCr 4:2:2 10-bit @ 20 bpp on 1080p59.94 → 3G-SDI
        // - RGB 4:4:4 10-bit @ 30 bpp on 1080p59.94 → 6G-SDI (3G can't carry it)
        const VideoFormat fmt(VideoFormat::Smpte1080p59_94);
        CHECK(inferSdiLinkStandard(fmt, SdiWireFormat::YCbCr_422_10, 1)
              == SdiLinkStandard::SL_3GA);
        CHECK(inferSdiLinkStandard(fmt, SdiWireFormat::RGB_444_10, 1)
              == SdiLinkStandard::SL_6G);
}

TEST_CASE("inferSdiLinkStandard: 12-bit wire formats push a band higher than 10-bit") {
        const VideoFormat fmt(VideoFormat::Smpte1080p59_94);
        // 4:2:2 10-bit @ 1080p59.94 fits 3G; 4:2:2 12-bit (24 bpp =
        // 2.98 × 1.18 = 3.52 Gbps) doesn't — pushes to 6G.
        CHECK(inferSdiLinkStandard(fmt, SdiWireFormat::YCbCr_422_10, 1)
              == SdiLinkStandard::SL_3GA);
        CHECK(inferSdiLinkStandard(fmt, SdiWireFormat::YCbCr_422_12, 1)
              == SdiLinkStandard::SL_6G);
}

TEST_CASE("inferSdiLinkStandard: returns Auto when bandwidth exceeds the cable count's ceiling") {
        // 8K p60 doesn't fit any plumbed standard.
        const SdiWireFormat wf = SdiWireFormat::YCbCr_422_10;
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte4320p60), wf, 1)
              == SdiLinkStandard::Auto);
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte4320p60), wf, 2)
              == SdiLinkStandard::Auto);
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte4320p60), wf, 4)
              == SdiLinkStandard::Auto);

        // 8K p60 in YCbCr 4:4:4 12-bit (36 bpp = 71.6 Gbps payload ×
        // 1.18 = 84.5 Gbps) — far past every standard at every cable
        // count.
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte4320p60),
                                    SdiWireFormat::YCbCr_444_12, 1)
              == SdiLinkStandard::Auto);
        CHECK(inferSdiLinkStandard(VideoFormat(VideoFormat::Smpte4320p60),
                                    SdiWireFormat::YCbCr_444_12, 4)
              == SdiLinkStandard::Auto);
}

TEST_CASE("inferSdiLinkStandard: returns Auto for malformed inputs") {
        const SdiWireFormat wf = SdiWireFormat::YCbCr_422_10;
        const VideoFormat valid(VideoFormat::Smpte1080p29_97);

        // Invalid VideoFormat.
        CHECK(inferSdiLinkStandard(VideoFormat(), wf, 1) == SdiLinkStandard::Auto);

        // Auto wire format.
        CHECK(inferSdiLinkStandard(valid, SdiWireFormat::Auto, 1) == SdiLinkStandard::Auto);

        // Unsupported cable count.
        CHECK(inferSdiLinkStandard(valid, wf, 0) == SdiLinkStandard::Auto);
        CHECK(inferSdiLinkStandard(valid, wf, 3) == SdiLinkStandard::Auto);
        CHECK(inferSdiLinkStandard(valid, wf, 8) == SdiLinkStandard::Auto);
        CHECK(inferSdiLinkStandard(valid, wf, -1) == SdiLinkStandard::Auto);
}
