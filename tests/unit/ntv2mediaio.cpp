/**
 * @file      ntv2mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/ntv2mediaio.h>
#include <promeki/pixelformat.h>
#include <promeki/url.h>

using namespace promeki;

namespace {

        // Helper: build a single-image MediaDesc with the given pixel
        // format and a representative HD raster + frame rate so the
        // resolved descriptor is realistic.
        MediaDesc makeMediaDesc(PixelFormat::ID pf) {
                MediaDesc md;
                md.imageList().pushToBack(
                        ImageDesc(Size2Du32(1920, 1080), PixelFormat(pf)));
                md.setFrameRate(FrameRate(FrameRate::FPS_59_94));
                return md;
        }

} // namespace

TEST_CASE("Ntv2Factory: registry advertises both source and sink modes") {
        Ntv2Factory factory;
        CHECK(factory.canBeSource());
        CHECK(factory.canBeSink());
        CHECK(factory.schemes() == StringList({String("ntv2")}));
        CHECK(factory.canHandlePath(String("ntv2://0/1")));
        CHECK(factory.canHandlePath(String("ntv2:")));
        CHECK_FALSE(factory.canHandlePath(String("ndi://host/source")));
}

TEST_CASE("Ntv2Factory::urlToConfig parses every supported URL shape") {
        Ntv2Factory factory;

        // ntv2://0/1 — integer device index + channel.
        {
                MediaIO::Config cfg;
                Url             u(String("ntv2://0/1"));
                REQUIRE(factory.urlToConfig(u, &cfg).isOk());
                CHECK(cfg.getAs<int32_t>(MediaConfig::Ntv2DeviceIndex, int32_t(-99)) == 0);
                CHECK(cfg.getAs<int32_t>(MediaConfig::Ntv2Channel, int32_t(-99)) == 1);
        }

        // ntv2://kona5/2 — name shorthand routes through Ntv2DeviceName.
        {
                MediaIO::Config cfg;
                Url             u(String("ntv2://kona5/2"));
                REQUIRE(factory.urlToConfig(u, &cfg).isOk());
                CHECK(cfg.getAs<int32_t>(MediaConfig::Ntv2DeviceIndex, int32_t(0)) == -1);
                CHECK(cfg.getAs<String>(MediaConfig::Ntv2DeviceName, String()) == String("kona5"));
                CHECK(cfg.getAs<int32_t>(MediaConfig::Ntv2Channel, int32_t(-99)) == 2);
        }

        // ntv2:///3 — implicit device index 0 + channel.
        {
                MediaIO::Config cfg;
                Url             u(String("ntv2:///3"));
                REQUIRE(factory.urlToConfig(u, &cfg).isOk());
                CHECK(cfg.getAs<int32_t>(MediaConfig::Ntv2DeviceIndex, int32_t(-99)) == 0);
                CHECK(cfg.getAs<int32_t>(MediaConfig::Ntv2Channel, int32_t(-99)) == 3);
        }
}

TEST_CASE("Ntv2MediaIO::proposeInput passes through every supported NTV2 frame-buffer format") {
        Ntv2MediaIO io;
        const PixelFormat::ID supported[] = {
                PixelFormat::YUV8_422_UYVY_Rec709,
                PixelFormat::YUV8_422_Rec709,
                PixelFormat::RGB8_sRGB,
                PixelFormat::BGR8_sRGB,
                PixelFormat::ARGB8_sRGB,
                PixelFormat::ABGR8_sRGB,
                PixelFormat::RGBA8_sRGB,
        };
        for (PixelFormat::ID id : supported) {
                MediaDesc offered = makeMediaDesc(id);
                MediaDesc preferred;
                CHECK(io.proposeInput(offered, &preferred).isOk());
                REQUIRE(!preferred.imageList().isEmpty());
                CHECK(preferred.imageList()[0].pixelFormat() == PixelFormat(id));
        }
}

TEST_CASE("Ntv2MediaIO::proposeInput asks for UYVY when an unmapped YCbCr is offered") {
        Ntv2MediaIO io;

        // YUV10_422_SemiPlanar_LE_Rec709 has no NTV2 frame-buffer
        // equivalent in the Phase 2 table — proposeInput should ask
        // for UYVY so the planner can splice a CSC bridge.
        MediaDesc offered = makeMediaDesc(PixelFormat::YUV10_422_SemiPlanar_LE_Rec709);
        MediaDesc preferred;
        CHECK(io.proposeInput(offered, &preferred).isOk());
        REQUIRE(!preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat() ==
              PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
        // Rest of the descriptor (raster, frame rate) preserved.
        CHECK(preferred.imageList()[0].size() == Size2Du32(1920, 1080));
        CHECK(preferred.frameRate() == FrameRate(FrameRate::FPS_59_94));
}

TEST_CASE("Ntv2MediaIO::proposeInput asks for RGB8 when an unmapped non-YCbCr is offered") {
        Ntv2MediaIO io;

        // RGB10_DPX_sRGB doesn't currently map to an NTV2 frame-buffer
        // format (10-bit RGB lands in Phase 5).  proposeInput should
        // suggest packed RGB8 since the offered format's color model is
        // RGB, not YCbCr.
        MediaDesc offered = makeMediaDesc(PixelFormat::RGB10_DPX_sRGB);
        MediaDesc preferred;
        CHECK(io.proposeInput(offered, &preferred).isOk());
        REQUIRE(!preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat() == PixelFormat(PixelFormat::RGB8_sRGB));
}

TEST_CASE("Ntv2MediaIO::proposeInput leaves audio-only descriptors untouched") {
        Ntv2MediaIO io;
        MediaDesc   offered;
        offered.audioList().pushToBack(AudioDesc(AudioFormat(AudioFormat::PCMI_Float32LE),
                                                 48000.0f, 2));

        MediaDesc preferred;
        CHECK(io.proposeInput(offered, &preferred).isOk());
        CHECK(preferred.imageList().isEmpty());
        REQUIRE(!preferred.audioList().isEmpty());
        CHECK(preferred.audioList()[0].channels() == 2);
        CHECK(preferred.audioList()[0].sampleRate() == 48000.0f);
}

TEST_CASE("Ntv2MediaIO::proposeInput rejects a null output pointer") {
        Ntv2MediaIO io;
        MediaDesc   offered = makeMediaDesc(PixelFormat::YUV8_422_UYVY_Rec709);
        CHECK(io.proposeInput(offered, nullptr).isError());
}

#endif // PROMEKI_ENABLE_NTV2
