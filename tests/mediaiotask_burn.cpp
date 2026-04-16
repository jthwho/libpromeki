/**
 * @file      tests/mediaiotask_burn.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enums.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_burn.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>

using namespace promeki;

namespace {

Frame::Ptr makeRgbFrameWithTc(size_t w, size_t h, PixelDesc::ID id,
                               const Timecode &tc) {
        Image img(w, h, PixelDesc(id));
        img.fill(0);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        frame.modify()->metadata().set(Metadata::Timecode, tc);
        frame.modify()->metadata().set(Metadata::FrameRate,
                                       FrameRate(FrameRate::FPS_24));
        return frame;
}

Frame::Ptr makeRgbFrame(size_t w, size_t h, PixelDesc::ID id) {
        Image img(w, h, PixelDesc(id));
        img.fill(0);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        return frame;
}

} // namespace

TEST_CASE("MediaIOTask_Burn_Registry") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "Burn") {
                        CHECK_FALSE(desc.canOutput);
                        CHECK_FALSE(desc.canInput);
                        CHECK(desc.canInputAndOutput);
                        CHECK(desc.extensions.isEmpty());
                        found = true;
                        break;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIOTask_Burn_DefaultConfig") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        CHECK_FALSE(cfg.isEmpty());
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "Burn");
        CHECK(cfg.getAs<bool>(MediaConfig::VideoBurnEnabled) == true);
        CHECK(cfg.getAs<String>(MediaConfig::VideoBurnText) == "{Timecode:smpte}");
        CHECK(cfg.getAs<int>(MediaConfig::Capacity) == 4);
}

TEST_CASE("MediaIOTask_Burn_RejectsReaderMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Output).isError());
        delete io;
}

TEST_CASE("MediaIOTask_Burn_AcceptsReadWriteMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());
        CHECK(io->isOpen());
        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Burn_AppliesBurn") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        cfg.set(MediaConfig::VideoBurnText, String("{Timecode:smpte}"));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
        Frame::Ptr in = makeRgbFrameWithTc(320, 240, PixelDesc::RGB8_sRGB, tc);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->isValid());
        CHECK(out->imageList()[0]->width() == 320);
        CHECK(out->imageList()[0]->height() == 240);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Burn_PassThroughWhenDisabled") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        cfg.set(MediaConfig::VideoBurnEnabled, false);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr in = makeRgbFrame(64, 48, PixelDesc::RGB8_sRGB);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelDesc() == PixelDesc(PixelDesc::RGB8_sRGB));

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Burn_AudioPassesThrough") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        cfg.set(MediaConfig::VideoBurnEnabled, false);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr in = Frame::Ptr::create();
        AudioDesc adesc(AudioDesc::PCMI_Float32LE, 48000.0f, 2);
        Audio audio(adesc, 1024);
        audio.zero();
        in.modify()->audioList().pushToBack(Audio::Ptr::create(std::move(audio)));
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->audioList().size() == 1);
        CHECK(out->audioList()[0]->samples() == 1024);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Burn_Stats") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        cfg.set(MediaConfig::VideoBurnEnabled, false);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr f = makeRgbFrame(16, 16, PixelDesc::RGB8_sRGB);
        CHECK(io->writeFrame(f).isOk());
        CHECK(io->writeFrame(f).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<int64_t>(MediaIOTask_Burn::StatsFramesBurned) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::QueueDepth) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::QueueCapacity) == 4);

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Burn_ReadEmptyQueueTryAgain") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out, false) == Error::TryAgain);

        io->close();
        delete io;
}
