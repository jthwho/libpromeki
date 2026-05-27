/**
 * @file      ntv2mediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NTV2

#include <doctest/doctest.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
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
                // Phase-5+ additions: V210, 10-bit DPX (BE + LE),
                // and 16-bit RGB.  All map directly to NTV2 frame
                // buffer formats.
                PixelFormat::YUV10_422_v210_Rec709,
                PixelFormat::RGB10_DPX_sRGB,
                PixelFormat::RGB10_DPX_LE_sRGB,
                PixelFormat::RGB16_LE_sRGB,
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

        // RGB16_BE_sRGB has no NTV2 frame-buffer mapping today (the
        // LE variant does).  Default config leaves on-board CSC
        // enabled, so the target stays in the offered RGB family —
        // the planner inserts a software packing CSC upstream, and
        // the on-board CSC bridges the RGB ↔ YCbCr wire mismatch
        // inside the routing fabric.
        MediaDesc offered = makeMediaDesc(PixelFormat::RGB16_BE_sRGB);
        MediaDesc preferred;
        CHECK(io.proposeInput(offered, &preferred).isOk());
        REQUIRE(!preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat() == PixelFormat(PixelFormat::RGB8_sRGB));
}

TEST_CASE("Ntv2MediaIO::proposeInput rewrites RGB to wire family when on-board CSC is disabled") {
        Ntv2MediaIO io;
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Ntv2DisableOnBoardCsc, true);
        io.setConfig(cfg);

        // RGB8 maps directly to an NTV2 frame-buffer format, but
        // the wire is YCbCr — using it would force an on-board CSC
        // in routing.  With Ntv2DisableOnBoardCsc=true, the
        // negotiator instead proposes the wire's family (UYVY) so
        // the planner either gets a native-YCbCr producer or
        // splices a software CSC upstream.
        MediaDesc offered = makeMediaDesc(PixelFormat::RGB8_sRGB);
        MediaDesc preferred;
        CHECK(io.proposeInput(offered, &preferred).isOk());
        REQUIRE(!preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat() ==
              PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
}

TEST_CASE("Ntv2MediaIO::proposeInput keeps a same-family format when on-board CSC is disabled") {
        Ntv2MediaIO io;
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Ntv2DisableOnBoardCsc, true);
        io.setConfig(cfg);

        // Same-family (YUV in, YUV wire) needs no CSC of any kind,
        // so it passes through even with on-board CSC disabled.
        MediaDesc offered = makeMediaDesc(PixelFormat::YUV8_422_UYVY_Rec709);
        MediaDesc preferred;
        CHECK(io.proposeInput(offered, &preferred).isOk());
        REQUIRE(!preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat() ==
              PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
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

TEST_CASE("Ntv2Factory configSpecs exposes Phase 6 keys with documented defaults") {
        Ntv2Factory                          factory;
        const MediaIOFactory::Config::SpecMap specs = factory.configSpecs();

        // Signal-loss polling defaults to 15 VBIs — visible at the
        // factory layer because the open path reads it as a config
        // override, but the spec ships its default for tooling /
        // probe output.
        auto pollIt = specs.find(MediaConfig::Ntv2SignalPollIntervalVbi);
        REQUIRE(pollIt != specs.end());
        CHECK(pollIt->second.defaultValue().get<int32_t>() == 15);

        // External-pacing thresholds default to 0 → "use one frame
        // interval" / "use 8 × frame interval" at gate-arm time
        // (resolved inside executeCmd(SetClock)).
        auto skipIt = specs.find(MediaConfig::Ntv2PaceSkipThresholdMs);
        REQUIRE(skipIt != specs.end());
        CHECK(skipIt->second.defaultValue().get<int32_t>() == 0);

        auto reanchorIt = specs.find(MediaConfig::Ntv2PaceReanchorThresholdMs);
        REQUIRE(reanchorIt != specs.end());
        CHECK(reanchorIt->second.defaultValue().get<int32_t>() == 0);
}

TEST_CASE("Error::SignalLoss is registered and distinct from NotReady") {
        // NotReady = "never came up"; SignalLoss = "was up, lost it."
        // The capture worker emits SignalLoss via errorOccurredSignal
        // on every present→absent transition, so the discrete code
        // must exist and not alias to anything else in the table.
        const Error loss(Error::SignalLoss);
        CHECK(loss.isError());
        CHECK(loss.code() == Error::SignalLoss);
        CHECK(loss != Error::NotReady);
        CHECK(loss != Error::DeviceError);
        CHECK(!loss.name().isEmpty());
        CHECK(!loss.desc().isEmpty());
}

TEST_CASE("Ntv2MediaIO: StatsDeviceLost is a registered MediaIOStats ID") {
        // Driver-restart / hot-unplug detection ticks this counter
        // and emits errorOccurredSignal(DeviceError).  The ID itself
        // must be exposed so dashboards can wire it up without
        // hardcoding the name.
        const MediaIOStats::ID id = Ntv2MediaIO::StatsDeviceLost;
        CHECK(!id.name().isEmpty());
        CHECK(id.name() == String("Ntv2DeviceLost"));
        // And distinct from the signal-loss IDs.
        CHECK(id != Ntv2MediaIO::StatsSignalLoss);
        CHECK(id != Ntv2MediaIO::StatsSignalReacquired);
}

#endif // PROMEKI_ENABLE_NTV2
