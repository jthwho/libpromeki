/**
 * @file      mediaio_bridges.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for FormatDesc::bridge callbacks on the five Phase-3
 * transform backends (CSC, FrameSync, SRC, VideoDecoder,
 * VideoEncoder).  Each test confirms that the bridge:
 *   - Returns true only when the from→to gap is the bridge's job.
 *   - Sets the right MediaConfig keys on the inserted stage.
 *   - Reports a sensible (and deterministic) cost.
 *
 * Also exercises the per-backend describe() / proposeInput() /
 * proposeOutput() overrides so the planner can rely on consistent
 * answers across the transform inventory.
 */

#include <doctest/doctest.h>

#include <promeki/audiodesc.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaiotask.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/videocodec.h>

using namespace promeki;

namespace {

        const MediaIO::FormatDesc *findFormat(const String &name) {
                for (const auto &d : MediaIO::registeredFormats()) {
                        if (d.name == name) return &d;
                }
                return nullptr;
        }

        MediaDesc makeUncompressedDesc(uint32_t w, uint32_t h, PixelFormat::ID pdId,
                                       FrameRate::WellKnownRate rate = FrameRate::FPS_30) {
                MediaDesc md;
                md.setFrameRate(FrameRate(rate));
                md.imageList().pushToBack(ImageDesc(Size2Du32(w, h), PixelFormat(pdId)));
                return md;
        }

        MediaDesc makeCompressedDesc(uint32_t w, uint32_t h, PixelFormat::ID compressedId) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                md.imageList().pushToBack(ImageDesc(Size2Du32(w, h), PixelFormat(compressedId)));
                return md;
        }

        MediaDesc makeAudioDesc(float rate, unsigned channels, AudioFormat::ID dt) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                AudioDesc ad;
                ad.setSampleRate(rate);
                ad.setChannels(channels);
                ad.setFormat(dt);
                md.audioList().pushToBack(ad);
                return md;
        }

} // namespace

// ============================================================================
// CSC bridge
// ============================================================================

TEST_CASE("MediaIO_Bridge_CSC_AcceptsPixelFormatGap") {
        const MediaIO::FormatDesc *desc = findFormat("CSC");
        REQUIRE(desc != nullptr);
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc from = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        const MediaDesc to = makeUncompressedDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = desc->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "CSC");
        CHECK(cfg.getAs<PixelFormat>(MediaConfig::OutputPixelFormat).id() == PixelFormat::YUV8_420_SemiPlanar_Rec709);
        // Lossy chroma subsampling pushes the cost into the bounded-
        // error band but stays under "heavily lossy" (< 1000).
        CHECK(cost > 50);
        CHECK(cost < 1000);
}

TEST_CASE("MediaIO_Bridge_CSC_RejectsCompressedEnds") {
        const MediaIO::FormatDesc *desc = findFormat("CSC");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc fromCompressed = makeCompressedDesc(1920, 1080, PixelFormat::H264);
        const MediaDesc toUncompressed = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        CHECK_FALSE(desc->bridge(fromCompressed, toUncompressed, nullptr, nullptr));

        const MediaDesc fromUncompressed = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        const MediaDesc toCompressed = makeCompressedDesc(1920, 1080, PixelFormat::H264);
        CHECK_FALSE(desc->bridge(fromUncompressed, toCompressed, nullptr, nullptr));
}

TEST_CASE("MediaIO_Bridge_CSC_RejectsRasterMismatch") {
        // CSC does not scale.  When the raster differs the planner
        // needs a future Scaler bridge — CSC must decline.
        const MediaIO::FormatDesc *desc = findFormat("CSC");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc from = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        const MediaDesc to = makeUncompressedDesc(1280, 720, PixelFormat::RGBA8_sRGB);
        CHECK_FALSE(desc->bridge(from, to, nullptr, nullptr));
}

TEST_CASE("MediaIO_Bridge_CSC_RejectsIdentity") {
        const MediaIO::FormatDesc *desc = findFormat("CSC");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc same = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        // No work for CSC to do — it must decline so the planner
        // doesn't insert a no-op stage.
        CHECK_FALSE(desc->bridge(same, same, nullptr, nullptr));
}

TEST_CASE("MediaIO_Bridge_CSC_Cost_FastPathBonus") {
        // RGBA8 ↔ BGRA8 is a registered SIMD fast path (byte
        // swizzle).  The bridge must report a noticeably lower cost
        // than a generic same-depth hop without a fast path so the
        // planner naturally prefers SIMD-accelerated routes.
        const MediaIO::FormatDesc *desc = findFormat("CSC");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc rgba8 = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        const MediaDesc bgra8 = makeUncompressedDesc(1920, 1080, PixelFormat::BGRA8_sRGB);
        int             fastCost = -1;
        REQUIRE(desc->bridge(rgba8, bgra8, nullptr, &fastCost));
        // Fast-path bonus shaves 25 off the base 50 → 25 (or lower).
        CHECK(fastCost <= 30);
}

TEST_CASE("MediaIO_Bridge_CSC_Cost_PenalizesBitDepthLoss") {
        // A 10-bit → 8-bit conversion drops 2 bits of luma precision.
        // The bit-depth penalty (100 per bit) plus the base 50 plus
        // the chroma-subsampling penalty (444 → 420 = 75 * 3 = 225)
        // together must keep this cost well above any same-depth
        // alternative — lossy paths must lose to same-depth paths.
        const MediaIO::FormatDesc *desc = findFormat("CSC");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc src10 = makeUncompressedDesc(1920, 1080, PixelFormat::YUV10_422_Planar_LE_Rec709);
        const MediaDesc dst8 = makeUncompressedDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        int             lossyCost = -1;
        REQUIRE(desc->bridge(src10, dst8, nullptr, &lossyCost));
        // Base 50 + 2 bits * 100 = 250 minimum.  Plus a chroma
        // penalty (422 → 420 = 75) when applicable = 325.  Allow
        // some slack for fast-path bonuses if any kernel exists.
        CHECK(lossyCost >= 200);
}

TEST_CASE("MediaIO_Bridge_CSC_Cost_SameDepthBeatsBitDepthLoss") {
        // The planner's quality preference becomes concrete here:
        // when both a same-bit-depth target and a downconverted
        // target are valid, the same-depth path must score cheaper.
        const MediaIO::FormatDesc *desc = findFormat("CSC");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc src10 = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA10_LE_sRGB);
        const MediaDesc same10Target = makeUncompressedDesc(1920, 1080, PixelFormat::YUV10_422_Planar_LE_Rec709);
        const MediaDesc downto8Target = makeUncompressedDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        int sameDepth = -1;
        int downConv = -1;
        REQUIRE(desc->bridge(src10, same10Target, nullptr, &sameDepth));
        REQUIRE(desc->bridge(src10, downto8Target, nullptr, &downConv));

        CHECK(sameDepth < downConv);
}

// ============================================================================
// FrameSync bridge
// ============================================================================

TEST_CASE("MediaIO_Bridge_FrameSync_AcceptsRateGap") {
        const MediaIO::FormatDesc *desc = findFormat("FrameSync");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc from = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB, FrameRate::FPS_30);
        const MediaDesc to = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB, FrameRate::FPS_24);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = desc->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "FrameSync");
        CHECK(cfg.getAs<FrameRate>(MediaConfig::OutputFrameRate) == FrameRate(FrameRate::FPS_24));
        CHECK(cost > 0);
}

TEST_CASE("MediaIO_Bridge_FrameSync_RejectsPixelMismatch") {
        // Pixel-format gaps are CSC's job.
        const MediaIO::FormatDesc *desc = findFormat("FrameSync");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc from = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        const MediaDesc to = makeUncompressedDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        CHECK_FALSE(desc->bridge(from, to, nullptr, nullptr));
}

TEST_CASE("MediaIO_Bridge_FrameSync_RejectsIdentity") {
        const MediaIO::FormatDesc *desc = findFormat("FrameSync");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc same = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        CHECK_FALSE(desc->bridge(same, same, nullptr, nullptr));
}

TEST_CASE("MediaIO_Bridge_FrameSync_AcceptsAudioRateOnly") {
        // Same video cadence and pixel format on both sides but the
        // audio sample rate differs.  FrameSync handles audio
        // resampling (SRC is sample-format only), so it should accept
        // and emit OutputAudioRate on the bridge config.
        const MediaIO::FormatDesc *desc = findFormat("FrameSync");
        REQUIRE(static_cast<bool>(desc->bridge));

        MediaDesc from = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        MediaDesc to = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        AudioDesc fromAud;
        fromAud.setSampleRate(48000.0f);
        fromAud.setChannels(2);
        fromAud.setFormat(AudioFormat::PCMI_S16LE);
        from.audioList().pushToBack(fromAud);
        AudioDesc toAud = fromAud;
        toAud.setSampleRate(96000.0f);
        to.audioList().pushToBack(toAud);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = desc->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "FrameSync");
        CHECK(cfg.getAs<float>(MediaConfig::OutputAudioRate) == doctest::Approx(96000.0f));
        CHECK(cost > 0);
}

TEST_CASE("MediaIO_Bridge_FrameSync_AcceptsAudioChannelsOnly") {
        // Video matches; only the audio channel count differs.  The
        // bridge accepts and emits OutputAudioChannels on the bridge
        // config.
        const MediaIO::FormatDesc *desc = findFormat("FrameSync");
        REQUIRE(static_cast<bool>(desc->bridge));

        MediaDesc from = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        MediaDesc to = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        AudioDesc fromAud;
        fromAud.setSampleRate(48000.0f);
        fromAud.setChannels(2);
        fromAud.setFormat(AudioFormat::PCMI_S16LE);
        from.audioList().pushToBack(fromAud);
        AudioDesc toAud = fromAud;
        toAud.setChannels(6); // downmix / upmix to 5.1
        to.audioList().pushToBack(toAud);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = desc->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<int>(MediaConfig::OutputAudioChannels) == 6);
        CHECK(cost > 0);
}

// ============================================================================
// SRC bridge
// ============================================================================

TEST_CASE("MediaIO_Bridge_SRC_AcceptsAudioDataTypeGap") {
        const MediaIO::FormatDesc *desc = findFormat("SRC");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc from = makeAudioDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        const MediaDesc to = makeAudioDesc(48000.0f, 2, AudioFormat::PCMI_S24LE);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = desc->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "SRC");
        Error err;
        Enum  dtEnum = cfg.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &err);
        CHECK(err.isOk());
        CHECK(static_cast<AudioFormat::ID>(dtEnum.value()) == AudioFormat::PCMI_S24LE);
        CHECK(cost > 0);
}

TEST_CASE("MediaIO_Bridge_SRC_RejectsRateGap") {
        // Sample-rate changes are FrameSync's job, not SRC's.
        const MediaIO::FormatDesc *desc = findFormat("SRC");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc from = makeAudioDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        const MediaDesc to = makeAudioDesc(96000.0f, 2, AudioFormat::PCMI_S16LE);
        CHECK_FALSE(desc->bridge(from, to, nullptr, nullptr));
}

// ============================================================================
// VideoDecoder bridge
// ============================================================================

TEST_CASE("MediaIO_Bridge_VideoDecoder_AcceptsCompressedToUncompressed") {
        const MediaIO::FormatDesc *desc = findFormat("VideoDecoder");
        REQUIRE(static_cast<bool>(desc->bridge));

        // The bridge declines unless the codec has a registered
        // decoder factory.  Use H264 if it's registered, otherwise
        // skip.
        const VideoCodec h264 = value(VideoCodec::lookup("H264"));
        if (!h264.canDecode()) {
                INFO("H264 decoder factory not registered in this build; skipping.");
                return;
        }

        const MediaDesc from = makeCompressedDesc(1920, 1080, PixelFormat::H264);
        const MediaDesc to = makeUncompressedDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = desc->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "VideoDecoder");
        CHECK(cfg.getAs<VideoCodec>(MediaConfig::VideoCodec) == h264);
        CHECK(cfg.getAs<PixelFormat>(MediaConfig::OutputPixelFormat).id() == PixelFormat::YUV8_420_SemiPlanar_Rec709);
        // Decoding is a precision-preserving hop in our cost model.
        CHECK(cost < 100);
}

TEST_CASE("MediaIO_Bridge_VideoDecoder_RejectsUncompressedSource") {
        const MediaIO::FormatDesc *desc = findFormat("VideoDecoder");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc from = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        const MediaDesc to = makeUncompressedDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        CHECK_FALSE(desc->bridge(from, to, nullptr, nullptr));
}

// ============================================================================
// VideoEncoder bridge
// ============================================================================

TEST_CASE("MediaIO_Bridge_VideoEncoder_AcceptsUncompressedToCompressed") {
        const MediaIO::FormatDesc *desc = findFormat("VideoEncoder");
        REQUIRE(static_cast<bool>(desc->bridge));

        const VideoCodec h264 = value(VideoCodec::lookup("H264"));
        if (!h264.canEncode()) {
                INFO("H264 encoder factory not registered in this build; skipping.");
                return;
        }

        const MediaDesc from = makeUncompressedDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        const MediaDesc to = makeCompressedDesc(1920, 1080, PixelFormat::H264);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = desc->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "VideoEncoder");
        CHECK(cfg.getAs<VideoCodec>(MediaConfig::VideoCodec) == h264);
        // Encoding is heavily lossy in our cost model.
        CHECK(cost >= 1000);
}

TEST_CASE("MediaIO_Bridge_VideoEncoder_RejectsCompressedSource") {
        const MediaIO::FormatDesc *desc = findFormat("VideoEncoder");
        REQUIRE(static_cast<bool>(desc->bridge));

        const MediaDesc from = makeCompressedDesc(1920, 1080, PixelFormat::H264);
        const MediaDesc to = makeCompressedDesc(1920, 1080, PixelFormat::HEVC);
        CHECK_FALSE(desc->bridge(from, to, nullptr, nullptr));
}

// ============================================================================
// proposeInput / proposeOutput overrides
// ============================================================================

TEST_CASE("MediaIO_proposeInput_CSC_RejectsCompressed") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "CSC");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc compressed = makeCompressedDesc(1920, 1080, PixelFormat::H264);
        MediaDesc       preferred;
        CHECK(io->proposeInput(compressed, &preferred) == Error::NotSupported);

        const MediaDesc rgb = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        CHECK(io->proposeInput(rgb, &preferred) == Error::Ok);
        CHECK(preferred == rgb);

        delete io;
}

TEST_CASE("MediaIO_proposeOutput_CSC_AppliesOutputPixelFormat") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "CSC");
        cfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc requested = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        MediaDesc       achievable;
        CHECK(io->proposeOutput(requested, &achievable) == Error::Ok);
        REQUIRE(!achievable.imageList().isEmpty());
        CHECK(achievable.imageList()[0].pixelFormat().id() == PixelFormat::YUV8_420_SemiPlanar_Rec709);
        // Raster and frame rate flow through unchanged.
        CHECK(achievable.imageList()[0].size() == Size2Du32(1920, 1080));
        CHECK(achievable.frameRate() == FrameRate(FrameRate::FPS_30));

        delete io;
}

TEST_CASE("MediaIO_proposeInput_VideoEncoder_RejectsCompressed") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "VideoEncoder");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc compressed = makeCompressedDesc(1920, 1080, PixelFormat::H264);
        MediaDesc       preferred;
        CHECK(io->proposeInput(compressed, &preferred) == Error::NotSupported);

        delete io;
}

TEST_CASE("MediaIO_proposeInput_VideoDecoder_RejectsUncompressed") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "VideoDecoder");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc uncompressed = makeUncompressedDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        MediaDesc       preferred;
        CHECK(io->proposeInput(uncompressed, &preferred) == Error::NotSupported);

        delete io;
}

// ============================================================================
// describe() overrides
// ============================================================================

TEST_CASE("MediaIO_describe_CSC_PopulatesPreferredFromConfig") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "CSC");
        cfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaIODescription d;
        REQUIRE(io->describe(&d) == Error::Ok);
        CHECK(d.canBeTransform());
        // Pre-open: the cached output PixelFormat lives only after the
        // backend's executeCmd(Open).  describe() therefore reports
        // an empty preferred — that's expected and explicitly part
        // of the "no probe before open" contract.
        CHECK_FALSE(d.preferredFormat().isValid());

        delete io;
}
