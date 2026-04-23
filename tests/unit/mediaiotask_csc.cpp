/**
 * @file      tests/mediaiotask_csc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_csc.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/frame.h>
#include <promeki/pixelformat.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/metadata.h>

using namespace promeki;

namespace {

Frame::Ptr makeRgbFrame(size_t w, size_t h, PixelFormat::ID id, uint8_t fill) {
        Image img(w, h, PixelFormat(id));
        img.fill(static_cast<char>(fill));
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        return frame;
}

Frame::Ptr makeVideoAudioFrame(size_t w, size_t h, PixelFormat::ID id,
                               AudioFormat::ID dt, float rate,
                               unsigned int channels, size_t samples) {
        Image img(w, h, PixelFormat(id));
        img.fill(0);
        AudioDesc adesc(dt, rate, channels);
        Audio audio(adesc, samples);
        audio.zero();
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(std::move(audio)));
        return frame;
}

MediaIO::Config cscConfig(const PixelFormat &outputPd) {
        MediaIO::Config cfg = MediaIO::defaultConfig("CSC");
        cfg.set(MediaConfig::OutputPixelFormat, outputPd);
        return cfg;
}

} // namespace

TEST_CASE("MediaIOTask_CSC_Registry") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "CSC") {
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

TEST_CASE("MediaIOTask_CSC_DefaultConfig") {
        MediaIO::Config cfg = MediaIO::defaultConfig("CSC");
        CHECK_FALSE(cfg.isEmpty());
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "CSC");
        CHECK(cfg.getAs<int>(MediaConfig::Capacity) == 4);
}

TEST_CASE("MediaIOTask_CSC_RejectsReaderMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("CSC");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Source).isError());
        CHECK_FALSE(io->isOpen());
        delete io;
}

TEST_CASE("MediaIOTask_CSC_AcceptsReadWriteMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("CSC");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());
        CHECK(io->isOpen());
        CHECK(io->mode() == MediaIO::Transform);
        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_CSC_PassThroughVideo") {
        MediaIO::Config cfg = MediaIO::defaultConfig("CSC");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = makeRgbFrame(16, 16, PixelFormat::RGB8_sRGB, 0x42);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelFormat() == PixelFormat(PixelFormat::RGB8_sRGB));

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_CSC_Rgb8ToRgba8") {
        MediaIO::Config cfg = cscConfig(PixelFormat(PixelFormat::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = makeRgbFrame(32, 32, PixelFormat::RGB8_sRGB, 0x10);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelFormat() == PixelFormat(PixelFormat::RGBA8_sRGB));
        CHECK(out->imageList()[0]->width() == 32);
        CHECK(out->imageList()[0]->height() == 32);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_CSC_AudioPassesThrough") {
        MediaIO::Config cfg = cscConfig(PixelFormat(PixelFormat::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = makeVideoAudioFrame(16, 16, PixelFormat::RGB8_sRGB,
                                            AudioFormat::PCMI_Float32LE,
                                            48000.0f, 2, 1024);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelFormat() == PixelFormat(PixelFormat::RGBA8_sRGB));
        REQUIRE(out->audioList().size() == 1);
        CHECK(out->audioList()[0]->desc().format().id() == AudioFormat::PCMI_Float32LE);
        CHECK(out->audioList()[0]->samples() == 1024);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_CSC_OutputMediaDesc") {
        MediaIO::Config cfg = cscConfig(PixelFormat(PixelFormat::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_29_97));
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(640, 480), PixelFormat(PixelFormat::RGB8_sRGB)));
        io->setExpectedDesc(pending);

        REQUIRE(io->open(MediaIO::Transform).isOk());

        MediaDesc vd = io->mediaDesc();
        REQUIRE(vd.imageList().size() == 1);
        CHECK(vd.imageList()[0].pixelFormat() == PixelFormat(PixelFormat::RGBA8_sRGB));
        CHECK(vd.imageList()[0].size() == Size2Du32(640, 480));
        CHECK(vd.frameRate() == FrameRate(FrameRate::FPS_29_97));

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_CSC_RejectsCompressedTarget") {
        MediaIO::Config cfg = cscConfig(PixelFormat(PixelFormat::JPEG_RGB8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = makeRgbFrame(64, 48, PixelFormat::RGB8_sRGB, 0x80);
        io->writeFrame(in);
        Frame::Ptr out;
        Error rerr = io->readFrame(out, false);
        CHECK(rerr == Error::TryAgain);
        CHECK_FALSE(out.isValid());

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_CSC_Stats") {
        MediaIO::Config cfg = cscConfig(PixelFormat(PixelFormat::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr f = makeRgbFrame(16, 16, PixelFormat::RGB8_sRGB, 0x55);
        CHECK(io->writeFrame(f).isOk());
        CHECK(io->writeFrame(f).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<int64_t>(MediaIOTask_CSC::StatsFramesConverted) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::QueueDepth) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::QueueCapacity) == 4);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_CSC_ReadEmptyQueueTryAgain") {
        MediaIO::Config cfg = cscConfig(PixelFormat(PixelFormat::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out, false) == Error::TryAgain);

        io->close();
        delete io;
}
