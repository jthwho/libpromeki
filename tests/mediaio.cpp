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

TEST_CASE("MediaIO_DefaultConfigUnknownTypeReturnsEmpty") {
        MediaIO::Config cfg = MediaIO::defaultConfig("NonexistentFormat");
        CHECK(cfg.isEmpty());
}

TEST_CASE("MediaIO_DefaultConfigTPG") {
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        CHECK_FALSE(cfg.isEmpty());

        // Should have the Type key set
        CHECK(cfg.getAs<String>(MediaIO::ConfigType) == "TPG");

        // Check some defaults
        CHECK(cfg.getAs<FrameRate>(MediaIO_TPG::ConfigFrameRate).isValid());
        CHECK(cfg.getAs<bool>(MediaIO_TPG::ConfigVideoEnabled) == false);
        CHECK(cfg.getAs<int>(MediaIO_TPG::ConfigVideoWidth) == 1920);
        CHECK(cfg.getAs<int>(MediaIO_TPG::ConfigVideoHeight) == 1080);
        CHECK(cfg.getAs<bool>(MediaIO_TPG::ConfigAudioEnabled) == false);
        CHECK(cfg.getAs<String>(MediaIO_TPG::ConfigAudioMode) == "tone");
        CHECK(cfg.getAs<bool>(MediaIO_TPG::ConfigTimecodeEnabled) == false);

        // Should be usable for creation after enabling a component
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());
        CHECK(io->mediaDesc().isValid());

        Frame frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame.imageList().size() == 1);
        CHECK(frame.imageList()[0]->width() == 1920);

        io->close();
        delete io;
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

        // Check MediaDesc
        MediaDesc vd = io->mediaDesc();
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

        MediaDesc vd = io->mediaDesc();
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

        MediaDesc vd = io->mediaDesc();
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
        CHECK(io->frameCount() == MediaIO::FrameCountInfinite);

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

// ============================================================================
// MediaIO_ImageFile backend
// ============================================================================

#include <cstdio>
#include <cstring>
#include <promeki/mediaio_imagefile.h>
#include <promeki/file.h>
#include <promeki/imagefile.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>

TEST_CASE("MediaIO_ImageFile_RegistryContainsBackend") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "ImageFile") {
                        CHECK(desc.canRead);
                        CHECK(desc.canWrite);
                        CHECK_FALSE(desc.extensions.isEmpty());
                        bool hasDpx = false, hasTga = false, hasSgi = false;
                        for(const auto &e : desc.extensions) {
                                if(e == "dpx") hasDpx = true;
                                if(e == "tga") hasTga = true;
                                if(e == "sgi") hasSgi = true;
                        }
                        CHECK(hasDpx);
                        CHECK(hasTga);
                        CHECK(hasSgi);
                        found = true;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIO_ImageFile_CreateForFileReadByExtension") {
        MediaIO *io = MediaIO::createForFileRead("/tmp/promeki_nonexistent_test.dpx");
        CHECK(io != nullptr);
        if(io) {
                CHECK(io->open(MediaIO::Reader).isError());
                delete io;
        }
}

TEST_CASE("MediaIO_ImageFile_CreateForFileWriteByExtension") {
        MediaIO *io = MediaIO::createForFileWrite("/tmp/promeki_test_write.tga");
        CHECK(io != nullptr);
        delete io;
}

TEST_CASE("MediaIO_ImageFile_DefaultConfig") {
        MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
        CHECK_FALSE(cfg.isEmpty());
        CHECK(cfg.getAs<String>(MediaIO::ConfigType) == "ImageFile");
}

static void fillTestPattern(Image &img) {
        for(size_t p = 0; p < img.pixelDesc().planeCount(); ++p) {
                uint8_t *data = static_cast<uint8_t *>(img.data(p));
                size_t bytes = img.pixelDesc().pixelFormat().planeSize(p, img.width(), img.height());
                for(size_t i = 0; i < bytes; ++i) {
                        data[i] = static_cast<uint8_t>((i * 7 + p * 37) & 0xFF);
                }
        }
}

TEST_CASE("MediaIO_ImageFile_DPXRoundTrip") {
        const char *fn = "/tmp/promeki_test_mediaio.dpx";
        const int w = 64, h = 48;

        {
                Image src(w, h, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                fillTestPattern(src);
                Frame writeFrame;
                writeFrame.imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                CHECK(io->writeFrame(writeFrame).isOk());
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Reader).isOk());
                MediaDesc vd = io->mediaDesc();
                CHECK(vd.imageList().size() == 1);
                CHECK(vd.imageList()[0].size().width() == w);
                CHECK(vd.imageList()[0].size().height() == h);
                CHECK(io->frameCount() == 1);
                CHECK(io->currentFrame() == 0);
                // Default step=0: re-reads the same frame indefinitely
                Frame readFrame;
                CHECK(io->readFrame(readFrame).isOk());
                CHECK(io->currentFrame() == 1);
                REQUIRE(readFrame.imageList().size() == 1);
                CHECK(readFrame.imageList()[0]->width() == w);
                CHECK(readFrame.imageList()[0]->height() == h);
                // Second read succeeds (step=0 re-delivers)
                Frame readFrame2;
                CHECK(io->readFrame(readFrame2).isOk());
                CHECK(io->currentFrame() == 2);
                // With step=1, next read returns EOF
                io->setStep(1);
                Frame emptyFrame;
                CHECK(io->readFrame(emptyFrame) == Error::EndOfFile);
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_ImageFile_TGARoundTrip") {
        const char *fn = "/tmp/promeki_test_mediaio.tga";
        {
                Image src(32, 32, PixelDesc::RGBA8_sRGB);
                REQUIRE(src.isValid());
                fillTestPattern(src);
                Frame wf;
                wf.imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                CHECK(io->writeFrame(wf).isOk());
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Reader).isOk());
                Frame rf;
                CHECK(io->readFrame(rf).isOk());
                REQUIRE(rf.imageList().size() == 1);
                CHECK(rf.imageList()[0]->width() == 32);
                CHECK(rf.imageList()[0]->height() == 32);
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_ImageFile_StepControl") {
        const char *fn = "/tmp/promeki_test_mediaio_step.dpx";
        {
                Image src(16, 16, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                fillTestPattern(src);
                Frame wf;
                wf.imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                io->writeFrame(wf);
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Reader).isOk());
                // Default step is 0 — no seeking, re-reads indefinitely
                CHECK(io->step() == 0);
                CHECK_FALSE(io->canSeek());
                Frame f1, f2;
                CHECK(io->readFrame(f1).isOk());
                CHECK(io->readFrame(f2).isOk());
                CHECK(f2.imageList().size() == 1);
                // Switch to step=1 — next read returns EOF
                io->setStep(1);
                Frame f3;
                CHECK(io->readFrame(f3) == Error::EndOfFile);
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_ImageFile_ReadBeforeOpenFails") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "ImageFile");
        cfg.set(MediaIO::ConfigFilename, "/tmp/dummy.dpx");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        Frame frame;
        CHECK(io->readFrame(frame) == Error::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_ImageFile_ProbeDetectsFormat") {
        const char *fn = "/tmp/promeki_test_probe.dpx";
        {
                Image src(8, 8, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                Frame wf;
                wf.imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                io->writeFrame(wf);
                io->close();
                delete io;
        }
        const auto &formats = MediaIO::registeredFormats();
        for(const auto &desc : formats) {
                if(desc.name == "ImageFile") {
                        REQUIRE(desc.canHandleDevice);
                        File probeFile(fn);
                        REQUIRE(probeFile.open(IODevice::ReadOnly).isOk());
                        CHECK(desc.canHandleDevice(&probeFile));
                        probeFile.close();
                        break;
                }
        }
        std::remove(fn);
}

// ============================================================================
// Base class accessor tests
// ============================================================================

TEST_CASE("MediaIO_BaseClass_FrameRateAccessor") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());
        CHECK(io->frameRate() == FrameRate(FrameRate::FPS_24));
        io->close();
        delete io;
}

TEST_CASE("MediaIO_BaseClass_AudioDescAccessor") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigAudioEnabled, true);
        cfg.set(MediaIO_TPG::ConfigAudioRate, 48000.0f);
        cfg.set(MediaIO_TPG::ConfigAudioChannels, 4);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());
        AudioDesc ad = io->audioDesc();
        CHECK(ad.isValid());
        CHECK(ad.sampleRate() == 48000.0f);
        CHECK(ad.channels() == 4);
        io->close();
        delete io;
}

TEST_CASE("MediaIO_BaseClass_AudioDescEmpty") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
        // No audio enabled
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());
        AudioDesc ad = io->audioDesc();
        CHECK_FALSE(ad.isValid());
        io->close();
        delete io;
}

// ============================================================================
// TPG step control
// ============================================================================

TEST_CASE("MediaIO_TPG_StepZeroHoldsTimecode") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigTimecodeEnabled, true);
        cfg.set(MediaIO_TPG::ConfigTimecodeStart, "01:00:00:00");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());

        io->setStep(0);
        Frame f1, f2;
        io->readFrame(f1);
        io->readFrame(f2);
        // Both frames should have the same timecode (step=0 = still)
        Timecode tc1 = f1.metadata().get(Metadata::Timecode).get<Timecode>();
        Timecode tc2 = f2.metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc1.hour() == 1);
        CHECK(tc1.frame() == tc2.frame());
        io->close();
        delete io;
}

TEST_CASE("MediaIO_TPG_StepForwardAdvancesTimecode") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "TPG");
        cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        cfg.set(MediaIO_TPG::ConfigTimecodeEnabled, true);
        cfg.set(MediaIO_TPG::ConfigTimecodeStart, "01:00:00:00");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Reader).isOk());

        // step=2: each read advances timecode by 2 frames
        io->setStep(2);
        Frame f1, f2;
        io->readFrame(f1);
        io->readFrame(f2);
        Timecode tc1 = f1.metadata().get(Metadata::Timecode).get<Timecode>();
        Timecode tc2 = f2.metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc1.frame() == 0);
        CHECK(tc2.frame() == 2);
        io->close();
        delete io;
}

// ============================================================================
// Post-close behavior
// ============================================================================

TEST_CASE("MediaIO_ImageFile_FrameCountAfterClose") {
        const char *fn = "/tmp/promeki_test_postclose.dpx";
        {
                Image src(8, 8, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                Frame wf;
                wf.imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                io->writeFrame(wf);
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Reader).isOk());
                CHECK(io->frameCount() == 1);
                io->close();
                CHECK(io->frameCount() == 0);
                CHECK(io->currentFrame() == 0);
                delete io;
        }
        std::remove(fn);
}

// ============================================================================
// MediaIO_AudioFile backend
// ============================================================================

#include <promeki/config.h>
#if PROMEKI_ENABLE_AUDIO

#include <promeki/mediaio_audiofile.h>
#include <promeki/audiotestpattern.h>
#include <promeki/audiolevel.h>

TEST_CASE("MediaIO_AudioFile_RegistryContainsBackend") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "AudioFile") {
                        CHECK(desc.canRead);
                        CHECK(desc.canWrite);
                        found = true;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIO_AudioFile_WAVRoundTrip") {
        const char *fn = "/tmp/promeki_test_mediaio.wav";
        const int numFrames = 10;
        FrameRate fps(FrameRate::FPS_24);
        AudioDesc desc(48000.0f, 2);
        size_t samplesPerFrame = (size_t)std::round(desc.sampleRate() / fps.toDouble());

        {
                MediaIO::Config cfg;
                cfg.set(MediaIO::ConfigType, "AudioFile");
                cfg.set(MediaIO::ConfigFilename, fn);
                cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
                cfg.set(MediaIO_AudioFile::ConfigAudioRate, desc.sampleRate());
                cfg.set(MediaIO_AudioFile::ConfigAudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                AudioTestPattern atp(desc);
                atp.setMode(AudioTestPattern::Tone);
                atp.setToneFrequency(1000.0);
                atp.setToneLevel(AudioLevel::fromDbfs(-20.0));
                atp.configure();
                for(int i = 0; i < numFrames; i++) {
                        Audio audio = atp.create(samplesPerFrame);
                        Frame frame;
                        frame.audioList().pushToBack(Audio::Ptr::create(audio));
                        io->writeFrame(frame);
                }
                io->close();
                delete io;
        }
        {
                MediaIO::Config cfg;
                cfg.set(MediaIO::ConfigType, "AudioFile");
                cfg.set(MediaIO::ConfigFilename, fn);
                cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Reader).isOk());
                MediaDesc vd = io->mediaDesc();
                CHECK(vd.imageList().isEmpty());
                CHECK(vd.audioList().size() == 1);
                CHECK(vd.frameRate() == fps);
                CHECK(io->frameCount() == (uint64_t)numFrames);
                int readCount = 0;
                Frame frame;
                while(io->readFrame(frame).isOk()) {
                        CHECK(frame.audioList().size() == 1);
                        readCount++;
                        frame = Frame();
                }
                CHECK(readCount == numFrames);
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_AudioFile_Seeking") {
        const char *fn = "/tmp/promeki_test_mediaio_seek.wav";
        FrameRate fps(FrameRate::FPS_30);
        AudioDesc desc(48000.0f, 1);
        size_t spf = (size_t)std::round(desc.sampleRate() / fps.toDouble());
        {
                MediaIO::Config cfg;
                cfg.set(MediaIO::ConfigType, "AudioFile");
                cfg.set(MediaIO::ConfigFilename, fn);
                cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
                cfg.set(MediaIO_AudioFile::ConfigAudioRate, desc.sampleRate());
                cfg.set(MediaIO_AudioFile::ConfigAudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                AudioTestPattern atp(desc);
                atp.setMode(AudioTestPattern::Silence);
                atp.configure();
                for(int i = 0; i < 20; i++) {
                        Frame frame;
                        frame.audioList().pushToBack(Audio::Ptr::create(atp.create(spf)));
                        io->writeFrame(frame);
                }
                io->close();
                delete io;
        }
        {
                MediaIO::Config cfg;
                cfg.set(MediaIO::ConfigType, "AudioFile");
                cfg.set(MediaIO::ConfigFilename, fn);
                cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Reader).isOk());
                CHECK(io->canSeek());
                CHECK(io->frameCount() == 20);
                CHECK(io->seekToFrame(10).isOk());
                CHECK(io->currentFrame() == 10);
                Frame frame;
                CHECK(io->readFrame(frame).isOk());
                CHECK(io->currentFrame() == 11);
                CHECK(io->seekToFrame(0).isOk());
                CHECK(io->currentFrame() == 0);
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_AudioFile_MissingFrameRateFails") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "AudioFile");
        cfg.set(MediaIO::ConfigFilename, "/tmp/dummy.wav");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Reader) == Error::InvalidArgument);
        delete io;
}

TEST_CASE("MediaIO_AudioFile_ReadBeforeOpenFails") {
        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "AudioFile");
        cfg.set(MediaIO::ConfigFilename, "/tmp/dummy.wav");
        cfg.set(MediaIO_AudioFile::ConfigFrameRate, FrameRate(FrameRate::FPS_24));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        Frame frame;
        CHECK(io->readFrame(frame) == Error::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_AudioFile_FrameCountAfterClose") {
        const char *fn = "/tmp/promeki_test_af_postclose.wav";
        FrameRate fps(FrameRate::FPS_24);
        AudioDesc desc(48000.0f, 1);
        size_t spf = (size_t)std::round(desc.sampleRate() / fps.toDouble());

        // Write 5 frames
        {
                MediaIO::Config cfg;
                cfg.set(MediaIO::ConfigType, "AudioFile");
                cfg.set(MediaIO::ConfigFilename, fn);
                cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
                cfg.set(MediaIO_AudioFile::ConfigAudioRate, desc.sampleRate());
                cfg.set(MediaIO_AudioFile::ConfigAudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                AudioTestPattern atp(desc);
                atp.setMode(AudioTestPattern::Silence);
                atp.configure();
                for(int i = 0; i < 5; i++) {
                        Frame frame;
                        frame.audioList().pushToBack(Audio::Ptr::create(atp.create(spf)));
                        io->writeFrame(frame);
                }
                CHECK(io->frameCount() == 5);
                io->close();
                CHECK(io->frameCount() == 0);
                delete io;
        }

        // Read back and check post-close
        {
                MediaIO::Config cfg;
                cfg.set(MediaIO::ConfigType, "AudioFile");
                cfg.set(MediaIO::ConfigFilename, fn);
                cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Reader).isOk());
                CHECK(io->frameCount() == 5);
                io->close();
                CHECK(io->frameCount() == 0);
                CHECK(io->currentFrame() == 0);
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_AudioFile_StepZeroReReads") {
        const char *fn = "/tmp/promeki_test_af_step.wav";
        FrameRate fps(FrameRate::FPS_30);
        AudioDesc desc(48000.0f, 1);
        size_t spf = (size_t)std::round(desc.sampleRate() / fps.toDouble());

        // Write 10 frames
        {
                MediaIO::Config cfg;
                cfg.set(MediaIO::ConfigType, "AudioFile");
                cfg.set(MediaIO::ConfigFilename, fn);
                cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
                cfg.set(MediaIO_AudioFile::ConfigAudioRate, desc.sampleRate());
                cfg.set(MediaIO_AudioFile::ConfigAudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Writer).isOk());
                AudioTestPattern atp(desc);
                atp.setMode(AudioTestPattern::Silence);
                atp.configure();
                for(int i = 0; i < 10; i++) {
                        Frame frame;
                        frame.audioList().pushToBack(Audio::Ptr::create(atp.create(spf)));
                        io->writeFrame(frame);
                }
                io->close();
                delete io;
        }

        // Read with step=0 — should re-read frame 0 repeatedly
        {
                MediaIO::Config cfg;
                cfg.set(MediaIO::ConfigType, "AudioFile");
                cfg.set(MediaIO::ConfigFilename, fn);
                cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Reader).isOk());
                io->setStep(0);
                // Read 3 times — should all succeed at frame 0
                for(int i = 0; i < 3; i++) {
                        Frame frame;
                        CHECK(io->readFrame(frame).isOk());
                        CHECK(frame.audioList().size() == 1);
                }
                // currentFrame advances but position doesn't
                // Switch to step=1 and read — should get frame 0 again then advance
                io->setStep(1);
                Frame frame;
                CHECK(io->readFrame(frame).isOk());
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_AudioFile_WriterCannotSeek") {
        const char *fn = "/tmp/promeki_test_af_wseek.wav";
        FrameRate fps(FrameRate::FPS_24);
        AudioDesc desc(48000.0f, 1);

        MediaIO::Config cfg;
        cfg.set(MediaIO::ConfigType, "AudioFile");
        cfg.set(MediaIO::ConfigFilename, fn);
        cfg.set(MediaIO_AudioFile::ConfigFrameRate, fps);
        cfg.set(MediaIO_AudioFile::ConfigAudioRate, desc.sampleRate());
        cfg.set(MediaIO_AudioFile::ConfigAudioChannels, (unsigned int)desc.channels());
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Writer).isOk());
        CHECK_FALSE(io->canSeek());
        CHECK(io->seekToFrame(0) == Error::IllegalSeek);
        io->close();
        delete io;
        std::remove(fn);
}

#endif // PROMEKI_ENABLE_AUDIO
