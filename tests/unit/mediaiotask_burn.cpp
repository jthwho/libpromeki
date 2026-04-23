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
#include <promeki/pixelformat.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/framerate.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>

using namespace promeki;

namespace {

Frame::Ptr makeRgbFrameWithTc(size_t w, size_t h, PixelFormat::ID id,
                               const Timecode &tc) {
        Image img(w, h, PixelFormat(id));
        img.fill(0);
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        frame.modify()->metadata().set(Metadata::Timecode, tc);
        frame.modify()->metadata().set(Metadata::FrameRate,
                                       FrameRate(FrameRate::FPS_24));
        return frame;
}

Frame::Ptr makeRgbFrame(size_t w, size_t h, PixelFormat::ID id) {
        Image img(w, h, PixelFormat(id));
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
        CHECK(io->open(MediaIO::Source).isError());
        delete io;
}

TEST_CASE("MediaIOTask_Burn_AcceptsReadWriteMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());
        CHECK(io->isOpen());
        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Burn_AppliesBurn") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        cfg.set(MediaConfig::VideoBurnText, String("{Timecode:smpte}"));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Timecode tc(Timecode::NDF24, 1, 0, 0, 0);
        Frame::Ptr in = makeRgbFrameWithTc(320, 240, PixelFormat::RGB8_sRGB, tc);
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
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = makeRgbFrame(64, 48, PixelFormat::RGB8_sRGB);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelFormat() == PixelFormat(PixelFormat::RGB8_sRGB));

        io->close();
        delete io;
}

TEST_CASE("MediaIOTask_Burn_AudioPassesThrough") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        cfg.set(MediaConfig::VideoBurnEnabled, false);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr in = Frame::Ptr::create();
        AudioDesc adesc(AudioFormat::PCMI_Float32LE, 48000.0f, 2);
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
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr f = makeRgbFrame(16, 16, PixelFormat::RGB8_sRGB);
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
        REQUIRE(io->open(MediaIO::Transform).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out, false) == Error::TryAgain);

        io->close();
        delete io;
}

// ----------------------------------------------------------------------------
// Introspection / negotiation
// ----------------------------------------------------------------------------
//
// Burn is a pure passthrough transform — the only negotiation
// constraint is that the video PixelFormat must have a paint engine (the
// overlay goes through VideoTestPattern::applyBurn, which needs one).
// These cases exercise proposeInput / proposeOutput against the three
// classes of offered MediaDesc: (a) paintable uncompressed, which
// passes through; (b) compressed or non-paintable, which is rewritten
// to a same-family paintable substitute so the planner splices in a
// CSC / decoder ahead of us; and (c) audio-only, which has nothing to
// burn but is still a valid passthrough.

namespace {

MediaDesc makeVideoDesc(uint32_t w, uint32_t h, PixelFormat::ID id) {
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::FPS_30));
        md.imageList().pushToBack(
                ImageDesc(Size2Du32(w, h), PixelFormat(id)));
        return md;
}

} // namespace

TEST_CASE("MediaIOTask_Burn_proposeInput_PaintableRgbPassesThrough") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        MediaDesc preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::Ok);
        CHECK(preferred == offered);

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeInput_PaintableYuvPassesThrough") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered =
                makeVideoDesc(1920, 1080, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        MediaDesc preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::Ok);
        CHECK(preferred == offered);

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeInput_CompressedYuvSubstitutesYuv") {
        // Compressed input has no paint engine; the backend must ask
        // for a paintable substitute in the same family so the planner
        // inserts a decoder + CSC ahead of us.  H.264 is a YCbCr codec,
        // so the substitute stays in the YUV family.
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::H264);
        MediaDesc preferred;
        REQUIRE(io->proposeInput(offered, &preferred) == Error::Ok);
        REQUIRE_FALSE(preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat().id() == PixelFormat::YUV8_422_Rec709);
        // Raster + frame rate are preserved — only the pixel format
        // changes.
        CHECK(preferred.imageList()[0].size() == offered.imageList()[0].size());
        CHECK(preferred.frameRate() == offered.frameRate());

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeInput_CompressedRgbSubstitutesRgba") {
        // Compressed RGB input (e.g. JPEG's RGB variant) substitutes
        // to an RGB paintable format so the planner's inserted
        // decoder + CSC stays inside the matching colour family.
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(1920, 1080, PixelFormat::JPEG_RGB8_sRGB);
        MediaDesc preferred;
        REQUIRE(io->proposeInput(offered, &preferred) == Error::Ok);
        REQUIRE_FALSE(preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat().id() == PixelFormat::RGBA8_sRGB);

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeInput_NonPaintableYuvSubstitutesYuv") {
        // BE-ordered YUV is uncompressed but has no registered paint
        // engine; the substitute must stay in the YUV family to keep
        // the required CSC cheap.
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered =
                makeVideoDesc(1920, 1080, PixelFormat::YUV10_422_Planar_BE_Rec709);
        MediaDesc preferred;
        REQUIRE(io->proposeInput(offered, &preferred) == Error::Ok);
        REQUIRE_FALSE(preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat().id() == PixelFormat::YUV8_422_Rec709);
        CHECK(preferred.imageList()[0].size() == offered.imageList()[0].size());

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeInput_NonPaintableRgbSubstitutesRgba") {
        // BE-ordered RGB is uncompressed but has no paint engine; the
        // substitute stays in the RGB family.
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered =
                makeVideoDesc(1920, 1080, PixelFormat::RGBA12_BE_sRGB);
        MediaDesc preferred;
        REQUIRE(io->proposeInput(offered, &preferred) == Error::Ok);
        REQUIRE_FALSE(preferred.imageList().isEmpty());
        CHECK(preferred.imageList()[0].pixelFormat().id() == PixelFormat::RGBA8_sRGB);

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeInput_AudioOnlyPassesThrough") {
        // An audio-only frame has nothing to draw on; Burn is still a
        // valid passthrough for audio.
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc offered;
        offered.setFrameRate(FrameRate(FrameRate::FPS_30));
        AudioDesc ad;
        ad.setSampleRate(48000.0f);
        ad.setChannels(2);
        ad.setFormat(AudioFormat::PCMI_Float32LE);
        offered.audioList().pushToBack(ad);

        MediaDesc preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::Ok);
        CHECK(preferred == offered);

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeInput_NullPreferredIsInvalid") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeVideoDesc(640, 480, PixelFormat::RGBA8_sRGB);
        CHECK(io->proposeInput(offered, nullptr) == Error::Invalid);

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeOutput_Passthrough") {
        // Burn is a pure passthrough transform — output shape equals
        // input shape.  proposeOutput must echo whatever is requested
        // so the planner treats Burn as a zero-cost passthrough.
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc requested = makeVideoDesc(1920, 1080, PixelFormat::RGBA8_sRGB);
        MediaDesc achievable;
        CHECK(io->proposeOutput(requested, &achievable) == Error::Ok);
        CHECK(achievable == requested);

        delete io;
}

TEST_CASE("MediaIOTask_Burn_proposeOutput_NullAchievableIsInvalid") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Burn");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc requested = makeVideoDesc(640, 480, PixelFormat::RGBA8_sRGB);
        CHECK(io->proposeOutput(requested, nullptr) == Error::Invalid);

        delete io;
}
