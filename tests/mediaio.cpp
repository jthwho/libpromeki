/**
 * @file      tests/mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaio_tpg.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/timecode.h>
#include <promeki/pixeldesc.h>
#include <promeki/framerate.h>

using namespace promeki;

// ============================================================================
// MediaIO base class
// ============================================================================

TEST_CASE("MediaIO_RegistryContainsTPG") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "TPG") {
                        CHECK(desc.canRead);
                        CHECK_FALSE(desc.canWrite);
                        CHECK(desc.extensions.isEmpty());
                        found = true;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIO_CreateByType") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK_FALSE(io->isOpen());
        CHECK(io->mode() == MediaIO::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_CreateUnknownTypeReturnsNull") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "NonexistentFormat");
        MediaIO *io = MediaIO::create(cfg);
        CHECK(io == nullptr);
}

TEST_CASE("MediaIO_CreateEmptyConfigReturnsNull") {
        MediaIO::Config cfg;
        MediaIO *io = MediaIO::create(cfg);
        CHECK(io == nullptr);
}

// ============================================================================
// Video + Audio + Timecode
// ============================================================================

TEST_CASE("MediaIO_TPG_FullGeneration") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
        cfg.set(MediaIO_TPG::ConfigVideoWidth, 320);
        cfg.set(MediaIO_TPG::ConfigVideoHeight, 240);
        cfg.set(MediaIO_TPG::ConfigVideoPattern, "colorbars");
        cfg.set(MediaIO_TPG::ConfigAudioEnabled, true);
        cfg.set(MediaIO_TPG::ConfigAudioMode, "tone");
        cfg.set(MediaIO_TPG::ConfigTimecodeEnabled, true);
        cfg.set(MediaIO_TPG::ConfigTimecodeStart, "01:00:00:00");

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        Error err = io->open(MediaIO::Reader);
        CHECK(err.isOk());
        CHECK(io->isOpen());
        CHECK(io->mode() == MediaIO::Reader);

        // Check VideoDesc
        VideoDesc vd = io->videoDesc();
        CHECK(vd.isValid());
        CHECK(vd.frameRate() == FrameRate(FrameRate::FPS_24));
        CHECK(vd.imageList().size() == 1);
        CHECK(vd.audioList().size() == 1);

        // Read a few frames
        for(int i = 0; i < 5; i++) {
                Frame frame;
                err = io->readFrame(frame);
                CHECK(err.isOk());

                // Video
                CHECK(frame.imageList().size() == 1);
                const Image &img = *frame.imageList()[0];
                CHECK(img.isValid());
                CHECK(img.width() == 320);
                CHECK(img.height() == 240);

                // Audio
                CHECK(frame.audioList().size() == 1);
                const Audio &audio = *frame.audioList()[0];
                CHECK(audio.isValid());
                CHECK(audio.samples() > 0);

                // Timecode on frame
                CHECK(frame.metadata().contains(Metadata::Timecode));

                // Timecode on image (required by TimecodeOverlayNode)
                CHECK(img.metadata().contains(Metadata::Timecode));
                Timecode tc = img.metadata().get(Metadata::Timecode).get<Timecode>();
                CHECK(tc.hour() == 1);
        }

        CHECK(io->currentFrame() == 5);

        err = io->close();
        CHECK(err.isOk());
        CHECK_FALSE(io->isOpen());
        delete io;
}

// ============================================================================
// Video only
// ============================================================================

TEST_CASE("MediaIO_TPG_VideoOnly") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_25));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
        cfg.set(MediaIO_TPG::ConfigVideoWidth, 64);
        cfg.set(MediaIO_TPG::ConfigVideoHeight, 64);
        cfg.set(MediaIO_TPG::ConfigVideoPattern, "ramp");

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());

        VideoDesc vd = io->videoDesc();
        CHECK(vd.imageList().size() == 1);
        CHECK(vd.audioList().size() == 0);

        Frame frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame.imageList().size() == 1);
        CHECK(frame.audioList().isEmpty());
        CHECK_FALSE(frame.metadata().contains(Metadata::Timecode));

        io->close();
        delete io;
}

// ============================================================================
// Audio only
// ============================================================================

TEST_CASE("MediaIO_TPG_AudioOnly") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_30));
        cfg.set(MediaIO_TPG::ConfigAudioEnabled, true);
        cfg.set(MediaIO_TPG::ConfigAudioMode, "silence");
        cfg.set(MediaIO_TPG::ConfigAudioChannels, 4);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());

        VideoDesc vd = io->videoDesc();
        CHECK(vd.imageList().size() == 0);
        CHECK(vd.audioList().size() == 1);
        CHECK(vd.audioList()[0].channels() == 4);

        Frame frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame.imageList().isEmpty());
        CHECK(frame.audioList().size() == 1);

        io->close();
        delete io;
}

// ============================================================================
// Timecode only
// ============================================================================

TEST_CASE("MediaIO_TPG_TimecodeOnly") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
        cfg.set(MediaIO_TPG::ConfigTimecodeEnabled, true);
        cfg.set(MediaIO_TPG::ConfigTimecodeDropFrame, true);
        cfg.set(MediaIO_TPG::ConfigTimecodeStart, "10:00:00;00");

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());

        Frame frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame.imageList().isEmpty());
        CHECK(frame.audioList().isEmpty());
        CHECK(frame.metadata().contains(Metadata::Timecode));

        io->close();
        delete io;
}

// ============================================================================
// Error cases
// ============================================================================

TEST_CASE("MediaIO_TPG_WriterNotSupported") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Writer) == Error::NotSupported);
        delete io;
}

TEST_CASE("MediaIO_TPG_NothingEnabledFails") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        // No video, audio, or timecode enabled

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Reader) == Error::InvalidArgument);
        delete io;
}

TEST_CASE("MediaIO_TPG_InvalidPatternFails") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
        cfg.set(MediaIO_TPG::ConfigVideoPattern, "bogus_pattern");

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Reader) == Error::InvalidArgument);
        delete io;
}

TEST_CASE("MediaIO_TPG_ReadBeforeOpenFails") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        Frame frame;
        CHECK(io->readFrame(frame) == Error::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_TPG_DoubleOpenFails") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());
        CHECK(io->open(MediaIO::Reader) == Error::AlreadyOpen);
        io->close();
        delete io;
}

// ============================================================================
// Seeking not supported
// ============================================================================

TEST_CASE("MediaIO_TPG_NoSeek") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());

        CHECK_FALSE(io->canSeek());
        CHECK(io->seekToFrame(0) == Error::IllegalSeek);
        CHECK(io->frameCount() == 0);

        io->close();
        delete io;
}

// ============================================================================
// Motion produces varying frames
// ============================================================================

TEST_CASE("MediaIO_TPG_Motion") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
        cfg.set(MediaIO_TPG::ConfigVideoWidth, 64);
        cfg.set(MediaIO_TPG::ConfigVideoHeight, 64);
        cfg.set(MediaIO_TPG::ConfigVideoPattern, "colorbars");
        cfg.set(MediaIO_TPG::ConfigVideoMotion, 2.0);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());

        // Read two frames — they should differ due to motion
        Frame frame1, frame2;
        io->readFrame(frame1);
        io->readFrame(frame2);

        const Image &img1 = *frame1.imageList()[0];
        const Image &img2 = *frame2.imageList()[0];
        const uint8_t *d1 = static_cast<const uint8_t *>(img1.data());
        const uint8_t *d2 = static_cast<const uint8_t *>(img2.data());
        bool differ = false;
        for(size_t i = 0; i < 64 * 3; i++) {
                if(d1[i] != d2[i]) { differ = true; break; }
        }
        CHECK(differ);

        io->close();
        delete io;
}
