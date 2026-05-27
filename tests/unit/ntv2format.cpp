/**
 * @file      ntv2format.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <doctest/doctest.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/ntv2format.h>
#include <promeki/pixelformat.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/videoportref.h>
#include <promeki/videoreferenceconfig.h>

using namespace promeki;

// Tests live in the unit-test target which does not have the
// libajantv2 headers on its include path (those are private to
// the promeki shared library).  Instead of asserting on raw NTV2
// enum values we round-trip every mapping through the helpers and
// verify the inverse path returns the original promeki value.
// Round-trip equivalence is a stronger property anyway: it catches
// a typo in either direction.

TEST_CASE("Ntv2Format::pixel-format round-trip covers the supported table") {
        const PixelFormat::ID supported[] = {
                // 8-bit YCbCr + RGB
                PixelFormat::YUV8_422_UYVY_Rec709,
                PixelFormat::YUV8_422_Rec709,
                PixelFormat::RGB8_sRGB,
                PixelFormat::BGR8_sRGB,
                PixelFormat::ARGB8_sRGB,
                PixelFormat::ABGR8_sRGB,
                PixelFormat::RGBA8_sRGB,
                // Phase-5+ high-bit-depth additions
                PixelFormat::YUV10_422_v210_Rec709,
                PixelFormat::RGB10_DPX_sRGB,
                PixelFormat::RGB10_DPX_LE_sRGB,
                PixelFormat::RGB16_LE_sRGB,
        };
        const int invalidSentinel = Ntv2Format::toNtv2PixelFormat(PixelFormat::Invalid);
        for (PixelFormat::ID id : supported) {
                const int ntv2 = Ntv2Format::toNtv2PixelFormat(id);
                CHECK(ntv2 != invalidSentinel);
                CHECK(Ntv2Format::fromNtv2PixelFormat(ntv2) == id);
        }

        // The invalid sentinel round-trips back to Invalid.  We don't
        // assert the literal value because the SDK reserves a
        // platform-specific sentinel (currently NTV2_FBF_INVALID = the
        // NUMFRAMEBUFFERFORMATS count, which is SDK-version dependent).
        CHECK(Ntv2Format::fromNtv2PixelFormat(invalidSentinel) == PixelFormat::Invalid);

        // A 10-bit YUV packing we deliberately don't map (semi-planar
        // has no NTV2 frame-buffer equivalent — V210 is the AJA-native
        // 10-bit YCbCr packing) rounds back to Invalid via the SDK
        // sentinel.
        CHECK(Ntv2Format::toNtv2PixelFormat(PixelFormat::YUV10_422_SemiPlanar_LE_Rec709) ==
              invalidSentinel);
}

TEST_CASE("Ntv2Format::toNtv2PixelFormat accepts HDR variants on the same FBF as the SDR sibling") {
        // The NTV2 frame-buffer formats describe wire / byte layout
        // only — colorimetry is signalled out-of-band on VPID, derived
        // by applySinkVpid → ColorModel::toH273.  Each HDR PixelFormat
        // must therefore map to the same NTV2_FBF_* as its SDR sibling.
        const int v210Fbf = Ntv2Format::toNtv2PixelFormat(PixelFormat::YUV10_422_v210_Rec709);
        const int rgb48Fbf = Ntv2Format::toNtv2PixelFormat(PixelFormat::RGB16_LE_sRGB);

        // 10-bit UYVY HDR variants share the V210 FBF.
        CHECK(Ntv2Format::toNtv2PixelFormat(PixelFormat::YUV10_422_UYVY_LE_Rec2020_PQ) == v210Fbf);
        CHECK(Ntv2Format::toNtv2PixelFormat(PixelFormat::YUV10_422_UYVY_LE_Rec2020_HLG) == v210Fbf);

        // 16-bit RGB HDR variants share NTV2_FBF_48BIT_RGB.
        CHECK(Ntv2Format::toNtv2PixelFormat(PixelFormat::RGB16_LE_Rec2020_PQ) == rgb48Fbf);
        CHECK(Ntv2Format::toNtv2PixelFormat(PixelFormat::RGB16_LE_Rec2020_HLG) == rgb48Fbf);
        CHECK(Ntv2Format::toNtv2PixelFormat(PixelFormat::RGB16_LE_DCI_P3_PQ) == rgb48Fbf);
}

TEST_CASE("Ntv2Format::video-format round-trip covers common broadcast rasters") {
        struct Case {
                        uint32_t      width;
                        uint32_t      height;
                        VideoScanMode scan;
                        FrameRate     rate;
        };
        const Case cases[] = {
                {1920, 1080, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_59_94)},
                {1920, 1080, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_60)},
                {1920, 1080, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_50)},
                {1920, 1080, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_30)},
                {1920, 1080, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_29_97)},
                {1920, 1080, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_25)},
                {1920, 1080, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_24)},
                {1920, 1080, VideoScanMode::Interlaced, FrameRate(FrameRate::FPS_29_97)},
                {1920, 1080, VideoScanMode::Interlaced, FrameRate(FrameRate::FPS_30)},
                {1920, 1080, VideoScanMode::Interlaced, FrameRate(FrameRate::FPS_25)},
                {1280, 720, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_59_94)},
                {1280, 720, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_60)},
                {1280, 720, VideoScanMode::Progressive, FrameRate(FrameRate::FPS_50)},
        };
        for (const Case &c : cases) {
                ImageDesc img(Size2Du32(c.width, c.height),
                              PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
                img.setVideoScanMode(c.scan);
                const int fmt = Ntv2Format::toNtv2VideoFormat(img, c.rate);
                CHECK(fmt != 0);

                Size2Du32     outSize;
                FrameRate     outRate;
                VideoScanMode outScan;
                CHECK(Ntv2Format::fromNtv2VideoFormat(fmt, &outSize, &outRate, &outScan).isOk());
                CHECK(outSize == img.size());
                CHECK(outRate == c.rate);
                // Round-trip narrows generic "Interlaced" → "Interlaced"
                // (we don't track field dominance through the table),
                // so compare via isInterlaced rather than ==.
                CHECK(outScan.isInterlaced() == c.scan.isInterlaced());
        }

        // Unsupported raster fails fast — the unknown sentinel
        // round-trips back through fromNtv2VideoFormat as an error.
        ImageDesc weird(Size2Du32(640, 480), PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
        weird.setVideoScanMode(VideoScanMode::Progressive);
        const int unknown = Ntv2Format::toNtv2VideoFormat(weird, FrameRate(FrameRate::FPS_30));
        CHECK(Ntv2Format::fromNtv2VideoFormat(unknown, nullptr, nullptr, nullptr).isError());
}

TEST_CASE("Ntv2Format::channel round-trip covers 1..8") {
        for (int ch = 1; ch <= 8; ++ch) {
                int ntv2 = Ntv2Format::toNtv2Channel(ch);
                // Valid channel values are >= 0; the SDK's
                // NTV2_CHANNEL1 is 0 and NTV2_CHANNEL_INVALID is 8.
                CHECK(ntv2 >= 0);
                CHECK(Ntv2Format::fromNtv2Channel(ntv2) == ch);
        }
        // Out-of-range produces the SDK's invalid sentinel.
        CHECK(Ntv2Format::fromNtv2Channel(Ntv2Format::toNtv2Channel(0)) == 0);
        CHECK(Ntv2Format::fromNtv2Channel(Ntv2Format::toNtv2Channel(9)) == 0);
}

TEST_CASE("Ntv2Format::portToInputSource translates SDI / HDMI connectors") {
        // SDI ports.
        const int sdi1 = Ntv2Format::portToInputSource(VideoPortRef(VideoConnectorKind::Sdi, 1));
        const int sdi8 = Ntv2Format::portToInputSource(VideoPortRef(VideoConnectorKind::Sdi, 8));
        CHECK(sdi1 >= 0);
        CHECK(sdi8 >= 0);
        CHECK(sdi8 != sdi1); // sequential, must differ

        // HDMI ports.
        const int hdmi1 = Ntv2Format::portToInputSource(VideoPortRef(VideoConnectorKind::Hdmi, 1));
        const int hdmi4 = Ntv2Format::portToInputSource(VideoPortRef(VideoConnectorKind::Hdmi, 4));
        CHECK(hdmi1 >= 0);
        CHECK(hdmi4 >= 0);

        // Out-of-range SDI / HDMI return the SDK's invalid sentinel.
        const int invalidPort = Ntv2Format::portToInputSource(
                VideoPortRef(VideoConnectorKind::Sdi, 9));
        // Sanity: invalid sentinel must not coincide with a valid SDI
        // value emitted above.
        CHECK(invalidPort != sdi1);
        CHECK(invalidPort != sdi8);
        CHECK(invalidPort == Ntv2Format::portToInputSource(VideoPortRef()));
        CHECK(invalidPort ==
              Ntv2Format::portToInputSource(VideoPortRef(VideoConnectorKind::Composite, 1)));
}

TEST_CASE("Ntv2Format::referenceFor maps generic sources to NTV2 reference enums") {
        const int freerun = Ntv2Format::referenceFor(VideoReferenceConfig(VideoReferenceSource::FreeRun));
        const int genlock = Ntv2Format::referenceFor(VideoReferenceConfig(VideoReferenceSource::Genlock));
        const int external = Ntv2Format::referenceFor(VideoReferenceConfig(VideoReferenceSource::External));
        // Genlock and External both alias to the SDK's external
        // reference; FreeRun is distinct.
        CHECK(genlock == external);
        CHECK(freerun != genlock);

        VideoReferenceConfig fromSdi3(VideoReferenceSource::FromSignal);
        fromSdi3.setSignalPort(VideoPortRef(VideoConnectorKind::Sdi, 3));
        const int sdi3Ref = Ntv2Format::referenceFor(fromSdi3);
        CHECK(sdi3Ref != freerun);
        CHECK(sdi3Ref != genlock);

        // Unset signal port falls back to FreeRun rather than crashing.
        VideoReferenceConfig fromSignalNoPort(VideoReferenceSource::FromSignal);
        CHECK(Ntv2Format::referenceFor(fromSignalNoPort) == freerun);
}

TEST_CASE("Ntv2Format::standardFitsCableCount honours per-standard cable counts") {
        // Single-link standards fit any non-zero cable budget.
        CHECK(Ntv2Format::standardFitsCableCount(SdiLinkStandard::SL_3GA, 1));
        CHECK(Ntv2Format::standardFitsCableCount(SdiLinkStandard::SL_3GA, 8));
        CHECK_FALSE(Ntv2Format::standardFitsCableCount(SdiLinkStandard::SL_3GA, 0));

        // Quad-link standards need at least 4 cables.
        CHECK_FALSE(Ntv2Format::standardFitsCableCount(SdiLinkStandard::QL_3G_2SI, 1));
        CHECK_FALSE(Ntv2Format::standardFitsCableCount(SdiLinkStandard::QL_3G_2SI, 3));
        CHECK(Ntv2Format::standardFitsCableCount(SdiLinkStandard::QL_3G_2SI, 4));
        CHECK(Ntv2Format::standardFitsCableCount(SdiLinkStandard::QL_3G_2SI, 8));

        // Auto is always accepted (the cable count is unconstrained).
        CHECK(Ntv2Format::standardFitsCableCount(SdiLinkStandard::Auto, 0));
        CHECK(Ntv2Format::standardFitsCableCount(SdiLinkStandard::Auto, 4));
}

#endif // PROMEKI_ENABLE_NTV2
