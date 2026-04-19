/**
 * @file      tests/mediaiotask_framesync.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enums.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_framesync.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/mediatimestamp.h>
#include <promeki/timestamp.h>
#include <promeki/clockdomain.h>

#include <cstring>

using namespace promeki;

namespace {

MediaTimeStamp mts(int64_t ns, const ClockDomain &domain) {
        TimeStamp ts;
        ts.setValue(TimeStamp::Value(std::chrono::nanoseconds(ns)));
        return MediaTimeStamp(ts, domain);
}

Frame::Ptr makeTimestampedFrame(size_t w, size_t h, PixelDesc::ID id,
                                int64_t videoTsNs, int64_t audioTsNs,
                                float audioRate, unsigned int channels,
                                size_t samples) {
        ClockDomain dom(ClockDomain::Synthetic);

        Image::Ptr img = Image::Ptr::create(w, h, PixelDesc(id));
        img.modify()->metadata().set(Metadata::MediaTimeStamp,
                                     mts(videoTsNs, dom));

        AudioDesc adesc(AudioDesc::PCMI_Float32LE, audioRate, channels);
        Audio::Ptr audio = Audio::Ptr::create(adesc, samples);
        audio.modify()->resize(samples);
        std::memset(audio.modify()->data<float>(), 0,
                    samples * channels * sizeof(float));
        audio.modify()->metadata().set(Metadata::MediaTimeStamp,
                                       mts(audioTsNs, dom));

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(img);
        frame.modify()->audioList().pushToBack(audio);
        frame.modify()->metadata().set(Metadata::FrameRate,
                                       FrameRate(FrameRate::FPS_24));
        return frame;
}

} // namespace

TEST_CASE("MediaIOTask_FrameSync_Registry") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "FrameSync") {
                        CHECK_FALSE(desc.canBeSource);
                        CHECK_FALSE(desc.canBeSink);
                        CHECK(desc.canBeTransform);
                        CHECK(desc.extensions.isEmpty());
                        found = true;
                        break;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIOTask_FrameSync_DefaultConfig") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        CHECK_FALSE(cfg.isEmpty());
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "FrameSync");
        CHECK(cfg.getAs<int>(MediaConfig::InputQueueCapacity) == 8);
        CHECK_FALSE(cfg.getAs<FrameRate>(MediaConfig::OutputFrameRate).isValid());
}

TEST_CASE("MediaIOTask_FrameSync_RejectsReaderMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_24));
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setExpectedDesc(pending);

        CHECK(io->open(MediaIO::Source).isError());
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_RequiresFrameRate") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Transform).isError());
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_AcceptsReadWriteMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_24));
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());
        CHECK(io->isOpen());
        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_InheritsSourceFrameRate") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_24));
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());
        CHECK(io->mediaDesc().frameRate() == FrameRate(FrameRate::FPS_24));

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_OutputFrameRateOverride") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        cfg.set(MediaConfig::OutputFrameRate, FrameRate(FrameRate::FPS_60));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_24));
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());
        CHECK(io->mediaDesc().frameRate() == FrameRate(FrameRate::FPS_60));

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_InheritsSourceAudioDesc") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_24));
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        pending.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());
        REQUIRE(io->mediaDesc().audioList().size() == 1);
        CHECK(io->mediaDesc().audioList()[0].sampleRate() == 48000.0f);
        CHECK(io->mediaDesc().audioList()[0].channels() == 2);
        CHECK(io->mediaDesc().audioList()[0].dataType() == AudioDesc::PCMI_Float32LE);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_OutputAudioRateOverride") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        cfg.set(MediaConfig::OutputAudioRate, 96000.0f);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_24));
        pending.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());
        REQUIRE(io->mediaDesc().audioList().size() == 1);
        CHECK(io->mediaDesc().audioList()[0].sampleRate() == 96000.0f);
        CHECK(io->mediaDesc().audioList()[0].channels() == 2);
        CHECK(io->mediaDesc().audioList()[0].dataType() == AudioDesc::PCMI_Float32LE);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_OutputAudioChannelsOverride") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        cfg.set(MediaConfig::OutputAudioChannels, int32_t(8));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_24));
        pending.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());
        REQUIRE(io->mediaDesc().audioList().size() == 1);
        CHECK(io->mediaDesc().audioList()[0].sampleRate() == 48000.0f);
        CHECK(io->mediaDesc().audioList()[0].channels() == 8);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_OutputAudioDataTypeOverride") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        cfg.set(MediaConfig::OutputAudioDataType, AudioDataType::PCMI_S16LE);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_24));
        pending.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());
        REQUIRE(io->mediaDesc().audioList().size() == 1);
        CHECK(io->mediaDesc().audioList()[0].dataType() == AudioDesc::PCMI_S16LE);
        CHECK(io->mediaDesc().audioList()[0].sampleRate() == 48000.0f);
        CHECK(io->mediaDesc().audioList()[0].channels() == 2);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_SyntheticClockRoundTrip") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        FrameRate fps(FrameRate::FPS_24);
        int64_t framePeriodNs = static_cast<int64_t>(1e9 / fps.toDouble());

        MediaDesc pending;
        pending.setFrameRate(fps);
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        pending.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());

        for(int i = 0; i < 3; ++i) {
                int64_t tsNs = static_cast<int64_t>(i) * framePeriodNs;
                Frame::Ptr in = makeTimestampedFrame(
                        320, 240, PixelDesc::RGB8_sRGB,
                        tsNs, tsNs, 48000.0f, 2, 2000);
                CHECK(io->writeFrame(in).isOk());
        }

        for(int i = 0; i < 3; ++i) {
                Frame::Ptr out;
                CHECK(io->readFrame(out).isOk());
                REQUIRE(out.isValid());
                CHECK(out->imageList().size() == 1);
        }

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_FrameSync_Stats") {
        MediaIO::Config cfg = MediaIO::defaultConfig("FrameSync");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        FrameRate fps(FrameRate::FPS_24);
        int64_t framePeriodNs = static_cast<int64_t>(1e9 / fps.toDouble());

        MediaDesc pending;
        pending.setFrameRate(fps);
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(320, 240), PixelDesc(PixelDesc::RGB8_sRGB)));
        pending.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());

        for(int i = 0; i < 2; ++i) {
                int64_t tsNs = static_cast<int64_t>(i) * framePeriodNs;
                Frame::Ptr in = makeTimestampedFrame(
                        320, 240, PixelDesc::RGB8_sRGB,
                        tsNs, tsNs, 48000.0f, 2, 2000);
                CHECK(io->writeFrame(in).isOk());
        }

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<int64_t>(MediaIOTask_FrameSync::StatsFramesPushed) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOTask_FrameSync::StatsFramesPulled) == 1);

        io->close();
        delete io;
}
