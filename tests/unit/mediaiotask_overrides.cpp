/**
 * @file      mediaiotask_overrides.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests for MediaIOTask::applyOutputOverrides — the shared helper
 * every transform backend (CSC, FrameSync, SRC, VideoEncoder,
 * VideoDecoder) and the planner use to derive the produced
 * MediaDesc from an input MediaDesc + a MediaConfig.
 */

#include <doctest/doctest.h>

#include <promeki/audiodesc.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiotask.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>

using namespace promeki;

namespace {

MediaDesc makeVideoAudioDesc() {
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));

        ImageDesc img(Size2Du32(1920, 1080),
                      PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        md.imageList().pushToBack(img);

        AudioDesc ad;
        ad.setSampleRate(48000.0f);
        ad.setChannels(2);
        ad.setFormat(AudioFormat::PCMI_S16LE);
        md.audioList().pushToBack(ad);
        return md;
}

} // namespace

TEST_CASE("MediaIOTask_applyOutputOverrides_PassThroughOnEmptyConfig") {
        // An empty config means "inherit everything" — the helper
        // returns a copy of the input untouched.
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out == input);
}

TEST_CASE("MediaIOTask_applyOutputOverrides_OutputPixelFormatOverrides") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputPixelFormat,
                PixelFormat(PixelFormat::RGBA8_sRGB));

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        REQUIRE(out.imageList().size() == 1);
        CHECK(out.imageList()[0].pixelFormat().id() == PixelFormat::RGBA8_sRGB);

        // Other fields stay put.
        CHECK(out.imageList()[0].size() == input.imageList()[0].size());
        CHECK(out.frameRate() == input.frameRate());
        CHECK(out.audioList() == input.audioList());
}

TEST_CASE("MediaIOTask_applyOutputOverrides_InvalidPixelFormatIsPassThrough") {
        // An invalid PixelFormat means "inherit" — the helper must not
        // overwrite the input pixel format.
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputPixelFormat, PixelFormat());

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.imageList()[0].pixelFormat() == input.imageList()[0].pixelFormat());
}

TEST_CASE("MediaIOTask_applyOutputOverrides_OutputFrameRateOverrides") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputFrameRate, FrameRate(FrameRate::FPS_24));

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.frameRate() == FrameRate(FrameRate::FPS_24));
        // Pixel fields unaffected.
        CHECK(out.imageList()[0].pixelFormat() == input.imageList()[0].pixelFormat());
}

TEST_CASE("MediaIOTask_applyOutputOverrides_InvalidFrameRateIsPassThrough") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputFrameRate, FrameRate());

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.frameRate() == input.frameRate());
}

TEST_CASE("MediaIOTask_applyOutputOverrides_OutputAudioRateOverrides") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputAudioRate, 96000.0f);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        REQUIRE(out.audioList().size() == 1);
        CHECK(out.audioList()[0].sampleRate() == doctest::Approx(96000.0f));
        // Channels and dataType unchanged.
        CHECK(out.audioList()[0].channels() == input.audioList()[0].channels());
        CHECK(out.audioList()[0].format().id() == input.audioList()[0].format().id());
}

TEST_CASE("MediaIOTask_applyOutputOverrides_ZeroAudioRateIsPassThrough") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputAudioRate, 0.0f);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.audioList()[0].sampleRate() ==
              doctest::Approx(input.audioList()[0].sampleRate()));
}

TEST_CASE("MediaIOTask_applyOutputOverrides_OutputAudioChannelsOverrides") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputAudioChannels, 6);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.audioList()[0].channels() == 6);
}

TEST_CASE("MediaIOTask_applyOutputOverrides_ZeroAudioChannelsIsPassThrough") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputAudioChannels, 0);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.audioList()[0].channels() == input.audioList()[0].channels());
}

TEST_CASE("MediaIOTask_applyOutputOverrides_OutputAudioDataTypeOverrides") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputAudioDataType, AudioDataType::PCMI_S24LE);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.audioList()[0].format().id() == AudioFormat::PCMI_S24LE);
}

TEST_CASE("MediaIOTask_applyOutputOverrides_InvalidAudioDataTypeIsPassThrough") {
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputAudioDataType, AudioDataType::Invalid);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.audioList()[0].format().id() == input.audioList()[0].format().id());
}

TEST_CASE("MediaIOTask_applyOutputOverrides_AppliesToAllVideoLayers") {
        // Multi-image MediaDescs (e.g. stereoscopic) must have the
        // override applied to every layer.
        MediaDesc input = makeVideoAudioDesc();
        input.imageList().pushToBack(
                ImageDesc(Size2Du32(1280, 720),
                          PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709)));

        MediaConfig cfg;
        cfg.set(MediaConfig::OutputPixelFormat,
                PixelFormat(PixelFormat::RGBA8_sRGB));

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        REQUIRE(out.imageList().size() == 2);
        CHECK(out.imageList()[0].pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        CHECK(out.imageList()[1].pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        // Per-layer raster preserved.
        CHECK(out.imageList()[0].size() == Size2Du32(1920, 1080));
        CHECK(out.imageList()[1].size() == Size2Du32(1280, 720));
}

TEST_CASE("MediaIOTask_applyOutputOverrides_AppliesToAllAudioLayers") {
        MediaDesc input = makeVideoAudioDesc();
        AudioDesc ad2;
        ad2.setSampleRate(44100.0f);
        ad2.setChannels(1);
        ad2.setFormat(AudioFormat::PCMI_Float32LE);
        input.audioList().pushToBack(ad2);

        MediaConfig cfg;
        cfg.set(MediaConfig::OutputAudioRate, 48000.0f);
        cfg.set(MediaConfig::OutputAudioChannels, 2);
        cfg.set(MediaConfig::OutputAudioDataType, AudioDataType::PCMI_S16LE);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        REQUIRE(out.audioList().size() == 2);
        for(size_t i = 0; i < out.audioList().size(); ++i) {
                CHECK(out.audioList()[i].sampleRate() == doctest::Approx(48000.0f));
                CHECK(out.audioList()[i].channels()   == 2);
                CHECK(out.audioList()[i].format().id()   == AudioFormat::PCMI_S16LE);
        }
}

TEST_CASE("MediaIOTask_applyOutputOverrides_CombinedOverrides") {
        // Setting every override at once should apply all of them
        // independently (no key shadows another).
        const MediaDesc input = makeVideoAudioDesc();
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
        cfg.set(MediaConfig::OutputFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaConfig::OutputAudioRate, 96000.0f);
        cfg.set(MediaConfig::OutputAudioChannels, 6);
        cfg.set(MediaConfig::OutputAudioDataType, AudioDataType::PCMI_Float32LE);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(input, cfg);
        CHECK(out.imageList()[0].pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        CHECK(out.frameRate() == FrameRate(FrameRate::FPS_24));
        CHECK(out.audioList()[0].sampleRate() == doctest::Approx(96000.0f));
        CHECK(out.audioList()[0].channels()   == 6);
        CHECK(out.audioList()[0].format().id()   == AudioFormat::PCMI_Float32LE);
}

TEST_CASE("MediaIOTask_applyOutputOverrides_EmptyInputStaysEmpty") {
        // Defensive: an invalid input MediaDesc should not crash the
        // helper.  An empty image / audio list means there is nothing
        // to override, so the result is also empty.
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
        cfg.set(MediaConfig::OutputAudioRate, 48000.0f);

        const MediaDesc out = MediaIOTask::applyOutputOverrides(MediaDesc(), cfg);
        CHECK(out.imageList().isEmpty());
        CHECK(out.audioList().isEmpty());
}

// ---------------------------------------------------------------
// MediaIOTask::defaultUncompressedPixelFormat
// ---------------------------------------------------------------
//
// This helper is the canonical fallback transform backends use
// when the pipeline hands them a PixelFormat they can't accept
// directly (compressed bitstream or uncompressed-but-no-paint-engine).
// The contract: YCbCr sources stay in the YCbCr family, everything
// else falls back to RGBA sRGB.  The branch is tiny, but missing
// coverage lets a future refactor silently swap the fallbacks.

TEST_CASE("MediaIOTask_defaultUncompressedPixelFormat_InvalidSourceFallsBackToRGBA") {
        // Default-constructed PixelFormat is invalid — isValid() gates
        // the YCbCr check to false, so the helper must return RGBA.
        const PixelFormat out =
                MediaIOTask::defaultUncompressedPixelFormat(PixelFormat());
        CHECK(out.id() == PixelFormat::RGBA8_sRGB);
}

TEST_CASE("MediaIOTask_defaultUncompressedPixelFormat_YUVSourceStaysYUV") {
        // Uncompressed YCbCr source — YCbCr type triggers the YUV
        // fallback so the planner picks a cheap CSC into YUV 4:2:2.
        const PixelFormat out = MediaIOTask::defaultUncompressedPixelFormat(
                PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        CHECK(out.id() == PixelFormat::YUV8_422_Rec709);
}

TEST_CASE("MediaIOTask_defaultUncompressedPixelFormat_CompressedYUVStaysYUV") {
        // Compressed H264 still reports ColorModel::TypeYCbCr, so the
        // helper treats the decode target as YCbCr too.  This matches
        // what the VideoEncoder / VideoDecoder bridges expect when they
        // splice a CSC between a YUV source and a RGB-only consumer.
        const PixelFormat out = MediaIOTask::defaultUncompressedPixelFormat(
                PixelFormat(PixelFormat::H264));
        CHECK(out.id() == PixelFormat::YUV8_422_Rec709);
}

TEST_CASE("MediaIOTask_defaultUncompressedPixelFormat_RGBSourceFallsBackToRGBA") {
        // RGB source — not YCbCr, so RGBA fallback wins.
        const PixelFormat out = MediaIOTask::defaultUncompressedPixelFormat(
                PixelFormat(PixelFormat::RGB8_sRGB));
        CHECK(out.id() == PixelFormat::RGBA8_sRGB);
}

TEST_CASE("MediaIOTask_defaultUncompressedPixelFormat_RGBACarriesThroughAsRGBA") {
        // Explicit RGBA sRGB source — the idempotent path.
        const PixelFormat out = MediaIOTask::defaultUncompressedPixelFormat(
                PixelFormat(PixelFormat::RGBA8_sRGB));
        CHECK(out.id() == PixelFormat::RGBA8_sRGB);
}
