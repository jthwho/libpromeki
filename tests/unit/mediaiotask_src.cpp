/**
 * @file      tests/mediaiotask_src.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enums.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_src.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>

using namespace promeki;

namespace {

Frame::Ptr makeAudioFrame(AudioDesc::DataType dt, float rate,
                          unsigned int channels, size_t samples) {
        AudioDesc desc(dt, rate, channels);
        Audio audio(desc, samples);
        audio.zero();
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(std::move(audio)));
        return frame;
}

Frame::Ptr makeVideoAudioFrame(size_t w, size_t h, PixelDesc::ID id,
                               AudioDesc::DataType dt, float rate,
                               unsigned int channels, size_t samples) {
        Image img(w, h, PixelDesc(id));
        img.fill(0);
        AudioDesc adesc(dt, rate, channels);
        Audio audio(adesc, samples);
        audio.zero();
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(std::move(audio)));
        return frame;
}

} // namespace

TEST_CASE("MediaIOTask_SRC_Registry") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "SRC") {
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

TEST_CASE("MediaIOTask_SRC_DefaultConfig") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        CHECK_FALSE(cfg.isEmpty());
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "SRC");

        Enum adt = cfg.get(MediaConfig::OutputAudioDataType)
                      .asEnum(AudioDataType::Type);
        CHECK(adt == AudioDataType::Invalid);
        CHECK(cfg.getAs<int>(MediaConfig::Capacity) == 4);
}

TEST_CASE("MediaIOTask_SRC_RejectsReaderMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Source).isError());
        delete io;
}

TEST_CASE("MediaIOTask_SRC_AcceptsReadWriteMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());
        CHECK(io->isOpen());
        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_SRC_AudioPassThrough") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = makeAudioFrame(AudioDesc::PCMI_Float32LE, 48000.0f, 2, 1024);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->audioList().size() == 1);
        CHECK(out->audioList()[0]->desc().dataType() == AudioDesc::PCMI_Float32LE);
        CHECK(out->audioList()[0]->samples() == 1024);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_SRC_AudioFormatConversion") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        cfg.set(MediaConfig::OutputAudioDataType,
                AudioDataType::PCMI_S16LE);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = makeAudioFrame(AudioDesc::PCMI_Float32LE, 48000.0f, 2, 1024);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->audioList().size() == 1);
        const Audio &a = *out->audioList()[0];
        CHECK(a.isValid());
        CHECK(a.desc().dataType() == AudioDesc::PCMI_S16LE);
        CHECK(a.samples() == 1024);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_SRC_VideoPassesThrough") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        cfg.set(MediaConfig::OutputAudioDataType,
                AudioDataType::PCMI_S16LE);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = makeVideoAudioFrame(16, 16, PixelDesc::RGB8_sRGB,
                                            AudioDesc::PCMI_Float32LE,
                                            48000.0f, 2, 1024);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelDesc() == PixelDesc(PixelDesc::RGB8_sRGB));
        REQUIRE(out->audioList().size() == 1);
        CHECK(out->audioList()[0]->desc().dataType() == AudioDesc::PCMI_S16LE);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_SRC_UnknownAudioDataTypeRejected") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        cfg.set(MediaConfig::OutputAudioDataType,
                String("NotARealFormat"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Transform).isError());
        delete io;
}

TEST_CASE("MediaIOTask_SRC_OutputMediaDesc") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        cfg.set(MediaConfig::OutputAudioDataType,
                AudioDataType::PCMI_S16LE);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_29_97));
        pending.audioList().pushToBack(
                AudioDesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());

        MediaDesc vd = io->mediaDesc();
        REQUIRE(vd.audioList().size() == 1);
        CHECK(vd.audioList()[0].dataType() == AudioDesc::PCMI_S16LE);
        CHECK(vd.audioList()[0].sampleRate() == 48000.0f);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_SRC_Stats") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        cfg.set(MediaConfig::OutputAudioDataType,
                AudioDataType::PCMI_S16LE);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr f = makeAudioFrame(AudioDesc::PCMI_Float32LE, 48000.0f, 2, 1024);
        CHECK(io->writeFrame(f).isOk());
        CHECK(io->writeFrame(f).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<int64_t>(MediaIOTask_SRC::StatsFramesConverted) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::QueueDepth) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::QueueCapacity) == 4);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_SRC_ReadEmptyQueueTryAgain") {
        MediaIO::Config cfg = MediaIO::defaultConfig("SRC");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out, false) == Error::TryAgain);

        io->close();
        delete io;
}
