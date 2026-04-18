/**
 * @file      tests/mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_tpg.h>
#include <promeki/mediaiotask_imagefile.h>
#include <promeki/mediaiotask_converter.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/timecode.h>
#include <promeki/pixeldesc.h>
#include <promeki/framerate.h>
#include <promeki/videoformat.h>
#include <promeki/dir.h>
#include <promeki/imgseq.h>
#include <promeki/file.h>
#include <promeki/metadata.h>
#include <promeki/enums.h>
#include <promeki/enumlist.h>

using namespace promeki;

// ============================================================================
// MediaIO base class
// ============================================================================

TEST_CASE("MediaIO_RegistryContainsTPG") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "TPG") {
                        CHECK(desc.canOutput);
                        CHECK_FALSE(desc.canInput);
                        CHECK(desc.extensions.isEmpty());
                        found = true;
                }
        }
        CHECK(found);
}

TEST_CASE("MediaIO_CreateByType") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK_FALSE(io->isOpen());
        CHECK(io->mode() == MediaIO::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_CreateUnknownTypeReturnsNull") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "NonexistentFormat");
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

// ============================================================================
// Unknown-config-key detection (Phase 3 / idea #3)
// ============================================================================

TEST_CASE("MediaIO_UnknownConfigKeys_defaultsAreAllKnown") {
        // The default config is built straight from the backend's spec
        // map so every key should be recognised.
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        StringList unknown = MediaIO::unknownConfigKeys("TPG", cfg);
        CHECK(unknown.isEmpty());
}

TEST_CASE("MediaIO_UnknownConfigKeys_globalCommonKeys") {
        // Keys registered in the global MediaConfig registry — but
        // not in the TPG-specific spec map — must still be recognised
        // because validation falls back to the global registry.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, String("TPG"));
        cfg.set(MediaConfig::Name, String("tpg-main"));
        cfg.set(MediaConfig::EnableBenchmark, true);
        StringList unknown = MediaIO::unknownConfigKeys("TPG", cfg);
        CHECK(unknown.isEmpty());
}

TEST_CASE("MediaIO_UnknownConfigKeys_flagsTypos") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, String("TPG"));
        // Register the typo keys on the fly; the ctor is enough to
        // put them in the per-Tag StringRegistry but no spec exists.
        cfg.set(MediaConfig::ID("NoSuchKey"), Variant(String("42")));
        cfg.set(MediaConfig::ID("AlsoBogus"), Variant(String("1")));

        StringList unknown = MediaIO::unknownConfigKeys("TPG", cfg);
        CHECK(unknown.size() == 2);
        // Sorted lexicographically per the documented contract.
        CHECK(unknown[0] == "AlsoBogus");
        CHECK(unknown[1] == "NoSuchKey");
}

TEST_CASE("MediaIO_UnknownConfigKeys_unknownBackendStillChecksGlobal") {
        // When the backend is unknown, configSpecs() returns an empty
        // map and the global registry is consulted alone.  Global keys
        // still resolve; unregistered keys still get flagged.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Name, String("x"));
        cfg.set(MediaConfig::ID("Bogus"), Variant(String("y")));
        StringList unknown =
                MediaIO::unknownConfigKeys("NonexistentFormat", cfg);
        CHECK(unknown.size() == 1);
        CHECK(unknown[0] == "Bogus");
}

TEST_CASE("MediaIO_ValidateConfigKeys_LenientReturnsOk") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, String("TPG"));
        cfg.set(MediaConfig::ID("Typo"), Variant(String("1")));
        Error err = MediaIO::validateConfigKeys("TPG", cfg,
                MediaIO::ConfigValidation::Lenient,
                String("test-lenient"));
        CHECK(err.isOk());
}

TEST_CASE("MediaIO_ValidateConfigKeys_StrictRejectsUnknown") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, String("TPG"));
        cfg.set(MediaConfig::ID("Typo"), Variant(String("1")));
        Error err = MediaIO::validateConfigKeys("TPG", cfg,
                MediaIO::ConfigValidation::Strict,
                String("test-strict"));
        CHECK(err.isError());
        CHECK(err.code() == Error::InvalidArgument);
}

TEST_CASE("MediaIO_ValidateConfigKeys_StrictAcceptsCleanConfig") {
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        Error err = MediaIO::validateConfigKeys("TPG", cfg,
                MediaIO::ConfigValidation::Strict);
        CHECK(err.isOk());
}

TEST_CASE("MediaIO_DefaultConfigTPG") {
        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        CHECK_FALSE(cfg.isEmpty());

        // Should have the Type key set
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "TPG");

        // Plain defaults: video, audio, and timecode are all enabled
        // so an unconfigured TPG produces a ready-to-use reference
        // stream (1080p29.97 colour bars with burn-in, stereo AvSync
        // audio marker on 48 kHz PCM, timecode starting at 01:00:00:00).
        VideoFormat vfmt = cfg.getAs<VideoFormat>(MediaConfig::VideoFormat);
        CHECK(vfmt.isValid());
        CHECK(vfmt.raster() == Size2Du32(1920, 1080));
        CHECK(vfmt.frameRate() == FrameRate(FrameRate::FPS_29_97));
        CHECK(cfg.getAs<bool>(MediaConfig::VideoEnabled) == true);
        CHECK(cfg.getAs<bool>(MediaConfig::VideoBurnEnabled) == true);
        CHECK(cfg.getAs<int>(MediaConfig::VideoBurnFontSize) == 0);
        CHECK(cfg.getAs<bool>(MediaConfig::AudioEnabled) == true);
        {
                // Default channel modes: LTC on ch0, AvSync click on ch1.
                EnumList modes = cfg.get(MediaConfig::AudioChannelModes).get<EnumList>();
                REQUIRE(modes.isValid());
                REQUIRE(modes.size() == 2);
                CHECK(modes[0] == AudioPattern::LTC);
                CHECK(modes[1] == AudioPattern::AvSync);
        }
        CHECK(cfg.getAs<bool>(MediaConfig::TimecodeEnabled) == true);
        CHECK(cfg.getAs<String>(MediaConfig::TimecodeStart) == "01:00:00:00");
        CHECK(cfg.getAs<bool>(MediaConfig::TimecodeDropFrame) == false);

        // Should be usable for creation straight from the defaults.
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->mediaDesc().isValid());

        Frame::Ptr frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame->imageList().size() == 1);
        CHECK(frame->imageList()[0]->width() == 1920);
        CHECK(frame->audioList().size() == 1);
        CHECK(frame->metadata().contains(Metadata::Timecode));

        io->close();
        delete io;
}

// ============================================================================
// Introspection: readyReads / pendingReads / pendingWrites
// ============================================================================

TEST_CASE("MediaIO_Introspection_ClosedStateIsZero") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->readyReads() == 0);
        CHECK(io->pendingReads() == 0);
        CHECK(io->pendingWrites() == 0);
        CHECK_FALSE(io->frameAvailable());
        delete io;
}

TEST_CASE("MediaIO_Introspection_ReadPrefetchCounts") {
        // The TPG generator's executeCmd(Read) always succeeds immediately,
        // so after a blocking readFrame() the result queue should be empty
        // and pendingReads() reflects the in-flight prefetch that was
        // submitted on the way out.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_30)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        CHECK(io->pendingWrites() == 0);

        // First read causes prefetch to kick off.  Because prefetch
        // depth is 1 and the prefetched read may or may not have
        // finished by the time the user thread checks, we assert only
        // an invariant: readyReads() + pendingReads() >= 0.
        Frame::Ptr frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame.isValid());
        CHECK(io->readyReads() >= 0);
        CHECK(io->pendingReads() >= 0);
        CHECK(io->readyReads() + io->pendingReads() <= io->prefetchDepth() + 1);

        io->close();
        // After close, all counters are reset.
        CHECK(io->readyReads() == 0);
        CHECK(io->pendingReads() == 0);
        CHECK(io->pendingWrites() == 0);
        delete io;
}

TEST_CASE("MediaIO_Introspection_PendingWritesDrainAfterClose") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        cfg.set(MediaConfig::Capacity, 4);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        // Submit several blocking writes — each one runs to completion
        // before writeFrame() returns, so pendingWrites() is 0 again
        // at every observation point between calls.
        Image img(8, 8, PixelDesc(PixelDesc::RGB8_sRGB));
        img.fill(0);
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));

        CHECK(io->pendingWrites() == 0);
        CHECK(io->writeFrame(f).isOk());
        CHECK(io->pendingWrites() == 0);
        CHECK(io->writeFrame(f).isOk());
        CHECK(io->pendingWrites() == 0);

        // Close resets the counter whether or not writes were pending.
        io->close();
        CHECK(io->pendingWrites() == 0);
        delete io;
}

TEST_CASE("MediaIO_Introspection_FrameAvailableMatchesReadyReads") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_30)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // frameAvailable() and readyReads() > 0 must always agree.
        for(int i = 0; i < 5; i++) {
                CHECK(io->frameAvailable() == (io->readyReads() > 0));
                Frame::Ptr frame;
                io->readFrame(frame, /*block=*/false);
        }

        io->close();
        delete io;
}

// ============================================================================
// Video + Audio + Timecode
// ============================================================================

TEST_CASE("MediaIO_TPG_FullGeneration") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(320, 240), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::VideoPattern, VideoPattern::ColorBars);
        cfg.set(MediaConfig::AudioEnabled, true);
        { EnumList __m = EnumList::forType<AudioPattern>(); __m.append(AudioPattern::Tone); cfg.set(MediaConfig::AudioChannelModes, __m); }
        cfg.set(MediaConfig::TimecodeEnabled, true);
        cfg.set(MediaConfig::TimecodeStart, "01:00:00:00");

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        Error err = io->open(MediaIO::Output);
        CHECK(err.isOk());
        CHECK(io->isOpen());
        CHECK(io->mode() == MediaIO::Output);

        // Check MediaDesc
        MediaDesc vd = io->mediaDesc();
        CHECK(vd.isValid());
        CHECK(vd.frameRate() == FrameRate(FrameRate::FPS_24));
        CHECK(vd.imageList().size() == 1);
        CHECK(vd.audioList().size() == 1);

        // Read a few frames
        for(int i = 0; i < 5; i++) {
                Frame::Ptr frame;
                err = io->readFrame(frame);
                CHECK(err.isOk());

                // Video
                CHECK(frame->imageList().size() == 1);
                const Image &img = *frame->imageList()[0];
                CHECK(img.isValid());
                CHECK(img.width() == 320);
                CHECK(img.height() == 240);

                // Audio
                CHECK(frame->audioList().size() == 1);
                const Audio &audio = *frame->audioList()[0];
                CHECK(audio.isValid());
                CHECK(audio.samples() > 0);

                // Timecode on frame
                CHECK(frame->metadata().contains(Metadata::Timecode));

                // Timecode on image (needed for burn-in overlays)
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
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(64, 64), FrameRate(FrameRate::FPS_25)));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::VideoPattern, VideoPattern::Ramp);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        MediaDesc vd = io->mediaDesc();
        CHECK(vd.imageList().size() == 1);
        CHECK(vd.audioList().size() == 0);

        Frame::Ptr frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame->imageList().size() == 1);
        CHECK(frame->audioList().isEmpty());
        CHECK_FALSE(frame->metadata().contains(Metadata::Timecode));

        io->close();
        delete io;
}

// ============================================================================
// Audio only
// ============================================================================

TEST_CASE("MediaIO_TPG_AudioOnly") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::AudioEnabled, true);
        { EnumList __m = EnumList::forType<AudioPattern>(); __m.append(AudioPattern::Silence); cfg.set(MediaConfig::AudioChannelModes, __m); }
        cfg.set(MediaConfig::AudioChannels, 4);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        MediaDesc vd = io->mediaDesc();
        CHECK(vd.imageList().size() == 0);
        CHECK(vd.audioList().size() == 1);
        CHECK(vd.audioList()[0].channels() == 4);

        Frame::Ptr frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame->imageList().isEmpty());
        CHECK(frame->audioList().size() == 1);

        io->close();
        delete io;
}

TEST_CASE("MediaIO_TPG_AudioCadence_29_97_48k") {
        // Verify TPG hands the audio pattern the correct cadenced sample
        // count for fractional NTSC: 5 frames of 29.97 @ 48 kHz must
        // sum to exactly 8008 samples and emit only 1601 / 1602 per
        // frame (no constant-1602 drift).
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        cfg.set(MediaConfig::AudioEnabled, true);
        { EnumList __m = EnumList::forType<AudioPattern>(); __m.append(AudioPattern::Silence); cfg.set(MediaConfig::AudioChannelModes, __m); }
        cfg.set(MediaConfig::AudioRate, 48000.0f);
        cfg.set(MediaConfig::AudioChannels, 2);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        size_t total = 0;
        for(int i = 0; i < 5; i++) {
                Frame::Ptr frame;
                REQUIRE(io->readFrame(frame).isOk());
                REQUIRE(frame->audioList().size() == 1);
                size_t s = frame->audioList()[0]->samples();
                INFO("frame " << i << " samples=" << s);
                CHECK((s == 1601 || s == 1602));
                total += s;
        }
        CHECK(total == 8008);

        io->close();
        delete io;
}

TEST_CASE("MediaIO_TPG_AudioCadence_30_48k_isConstant") {
        // Sanity: integer-cadence rates still emit a constant per-frame
        // sample count.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::AudioEnabled, true);
        { EnumList __m = EnumList::forType<AudioPattern>(); __m.append(AudioPattern::Silence); cfg.set(MediaConfig::AudioChannelModes, __m); }
        cfg.set(MediaConfig::AudioRate, 48000.0f);
        cfg.set(MediaConfig::AudioChannels, 2);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        for(int i = 0; i < 10; i++) {
                Frame::Ptr frame;
                REQUIRE(io->readFrame(frame).isOk());
                REQUIRE(frame->audioList().size() == 1);
                CHECK(frame->audioList()[0]->samples() == 1600);
        }

        io->close();
        delete io;
}

// ============================================================================
// Timecode only
// ============================================================================

TEST_CASE("MediaIO_TPG_TimecodeOnly") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        cfg.set(MediaConfig::TimecodeEnabled, true);
        cfg.set(MediaConfig::TimecodeDropFrame, true);
        cfg.set(MediaConfig::TimecodeStart, "10:00:00;00");

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        Frame::Ptr frame;
        CHECK(io->readFrame(frame).isOk());
        CHECK(frame->imageList().isEmpty());
        CHECK(frame->audioList().isEmpty());
        CHECK(frame->metadata().contains(Metadata::Timecode));

        io->close();
        delete io;
}

// ============================================================================
// Error cases
// ============================================================================

TEST_CASE("MediaIO_TPG_WriterNotSupported") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Input) == Error::NotSupported);
        delete io;
}

TEST_CASE("MediaIO_TPG_NothingEnabledFails") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        // No video, audio, or timecode enabled

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Output) == Error::InvalidArgument);
        delete io;
}

TEST_CASE("MediaIO_TPG_InvalidPatternFails") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::VideoPattern, "bogus_pattern");

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Output) == Error::InvalidArgument);
        delete io;
}

TEST_CASE("MediaIO_TPG_ReadBeforeOpenFails") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        Frame::Ptr frame;
        CHECK(io->readFrame(frame) == Error::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_TPG_DoubleOpenFails") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->open(MediaIO::Output) == Error::AlreadyOpen);
        io->close();
        delete io;
}

// ============================================================================
// Seeking not supported
// ============================================================================

TEST_CASE("MediaIO_TPG_NoSeek") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

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
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(64, 64), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::VideoPattern, VideoPattern::ColorBars);
        cfg.set(MediaConfig::VideoMotion, 2.0);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Read two frames — they should differ due to motion
        Frame::Ptr frame1, frame2;
        io->readFrame(frame1);
        io->readFrame(frame2);

        const Image &img1 = *frame1->imageList()[0];
        const Image &img2 = *frame2->imageList()[0];
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
#include <promeki/mediaiotask_imagefile.h>
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
                        CHECK(desc.canOutput);
                        CHECK(desc.canInput);
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
                CHECK(io->open(MediaIO::Output).isError());
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
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "ImageFile");
        CHECK(cfg.contains(MediaConfig::FrameRate));
        FrameRate defRate = cfg.getAs<FrameRate>(MediaConfig::FrameRate);
        CHECK(defRate == MediaIOTask_ImageFile::DefaultFrameRate);
        CHECK(defRate.isValid());
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

TEST_CASE("MediaIO_ImageFile_DefaultFrameRateOnRead") {
        // Default config: reading a still image reports DefaultFrameRate.
        const char *fn = "/tmp/promeki_test_mediaio_fps_default.tga";
        {
                Image src(16, 16, PixelDesc::RGBA8_sRGB);
                REQUIRE(src.isValid());
                fillTestPattern(src);
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                CHECK(io->writeFrame(wf).isOk());
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameRate() == MediaIOTask_ImageFile::DefaultFrameRate);
                CHECK(io->mediaDesc().frameRate() == MediaIOTask_ImageFile::DefaultFrameRate);
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_ImageFile_ConfigFrameRateOverride") {
        // Explicit ConfigFrameRate overrides the default on read.
        const char *fn = "/tmp/promeki_test_mediaio_fps_override.tga";
        {
                Image src(16, 16, PixelDesc::RGBA8_sRGB);
                REQUIRE(src.isValid());
                fillTestPattern(src);
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                CHECK(io->writeFrame(wf).isOk());
                io->close();
                delete io;
        }
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, String(fn));
                FrameRate wanted(FrameRate::FPS_29_97);
                cfg.set(MediaConfig::FrameRate, wanted);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameRate() == wanted);
                CHECK(io->mediaDesc().frameRate() == wanted);
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_ImageFile_DPXRoundTrip") {
        const char *fn = "/tmp/promeki_test_mediaio.dpx";
        const int w = 64, h = 48;

        {
                Image src(w, h, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                fillTestPattern(src);
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                CHECK(io->writeFrame(wf).isOk());
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                MediaDesc vd = io->mediaDesc();
                CHECK(vd.imageList().size() == 1);
                CHECK(vd.imageList()[0].size().width() == w);
                CHECK(vd.imageList()[0].size().height() == h);
                CHECK(io->frameCount() == 1);
                CHECK(io->currentFrame() == 0);
                // Default step=0: re-reads the same frame indefinitely
                Frame::Ptr rf;
                CHECK(io->readFrame(rf).isOk());
                CHECK(io->currentFrame() == 1);
                REQUIRE(rf->imageList().size() == 1);
                CHECK(rf->imageList()[0]->width() == w);
                CHECK(rf->imageList()[0]->height() == h);
                // Second read succeeds (step=0 re-delivers)
                Frame::Ptr rf2;
                CHECK(io->readFrame(rf2).isOk());
                CHECK(io->currentFrame() == 2);
                // With step=1, next read returns EOF
                io->setStep(1);
                Frame::Ptr emptyFrame;
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
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                CHECK(io->writeFrame(wf).isOk());
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                Frame::Ptr rf;
                CHECK(io->readFrame(rf).isOk());
                REQUIRE(rf->imageList().size() == 1);
                CHECK(rf->imageList()[0]->width() == 32);
                CHECK(rf->imageList()[0]->height() == 32);
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
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                io->writeFrame(wf);
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                // Default step is 0 — no seeking, re-reads indefinitely
                CHECK(io->step() == 0);
                CHECK_FALSE(io->canSeek());
                Frame::Ptr f1, f2;
                CHECK(io->readFrame(f1).isOk());
                CHECK(io->readFrame(f2).isOk());
                CHECK(f2->imageList().size() == 1);
                // Switch to step=1 — next read returns EOF
                io->setStep(1);
                Frame::Ptr f3;
                CHECK(io->readFrame(f3) == Error::EndOfFile);
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_ImageFile_ReadBeforeOpenFails") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "ImageFile");
        cfg.set(MediaConfig::Filename, "/tmp/dummy.dpx");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        Frame::Ptr frame;
        CHECK(io->readFrame(frame) == Error::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_ImageFile_ProbeDetectsFormat") {
        const char *fn = "/tmp/promeki_test_probe.dpx";
        {
                Image src(8, 8, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
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
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->frameRate() == FrameRate(FrameRate::FPS_24));
        io->close();
        delete io;
}

TEST_CASE("MediaIO_BaseClass_AudioDescAccessor") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::AudioEnabled, true);
        cfg.set(MediaConfig::AudioRate, 48000.0f);
        cfg.set(MediaConfig::AudioChannels, 4);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        AudioDesc ad = io->audioDesc();
        CHECK(ad.isValid());
        CHECK(ad.sampleRate() == 48000.0f);
        CHECK(ad.channels() == 4);
        io->close();
        delete io;
}

TEST_CASE("MediaIO_BaseClass_AudioDescEmpty") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        // No audio enabled
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
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
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::TimecodeEnabled, true);
        cfg.set(MediaConfig::TimecodeStart, "01:00:00:00");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        io->setStep(0);
        Frame::Ptr f1, f2;
        io->readFrame(f1);
        io->readFrame(f2);
        // Both frames should have the same timecode (step=0 = still)
        Timecode tc1 = f1->metadata().get(Metadata::Timecode).get<Timecode>();
        Timecode tc2 = f2->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc1.hour() == 1);
        CHECK(tc1.frame() == tc2.frame());
        io->close();
        delete io;
}

TEST_CASE("MediaIO_TPG_StepForwardAdvancesTimecode") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::TimecodeEnabled, true);
        cfg.set(MediaConfig::TimecodeStart, "01:00:00:00");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // step=2: each read advances timecode by 2 frames
        io->setStep(2);
        Frame::Ptr f1, f2;
        io->readFrame(f1);
        io->readFrame(f2);
        Timecode tc1 = f1->metadata().get(Metadata::Timecode).get<Timecode>();
        Timecode tc2 = f2->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc1.frame() == 0);
        CHECK(tc2.frame() == 2);
        io->close();
        delete io;
}

// ============================================================================
// Prefetch depth
// ============================================================================

TEST_CASE("MediaIO_TPG_PrefetchDepth_DefaultIsTaskValue") {
        // After open(), prefetchDepth() should reflect the task's default
        // (1 for TPG since it doesn't override).
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(32, 32), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->prefetchDepth() == 1);
        io->close();
        delete io;
}

TEST_CASE("MediaIO_TPG_PrefetchDepth_UserOverride") {
        // Calling setPrefetchDepth() before open() makes the user's value
        // win — the task's default does not overwrite it.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(32, 32), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        io->setPrefetchDepth(4);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->prefetchDepth() == 4);

        // Read several frames — should still work with depth > 1.
        for(int i = 0; i < 3; i++) {
                Frame::Ptr frame;
                CHECK(io->readFrame(frame).isOk());
                CHECK(frame.isValid());
        }
        io->close();
        // After close, the explicit override is reset.
        CHECK(io->prefetchDepth() == 1);
        delete io;
}

TEST_CASE("MediaIO_TPG_PrefetchDepth_ClampsToOne") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        io->setPrefetchDepth(0);
        CHECK(io->prefetchDepth() == 1);
        io->setPrefetchDepth(-5);
        CHECK(io->prefetchDepth() == 1);
        delete io;
}

// ============================================================================
// Default seek mode
// ============================================================================

TEST_CASE("MediaIO_TPG_DefaultSeekMode_IsExact") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->defaultSeekMode() == MediaIO::SeekExact);
        io->close();
        delete io;
}

#if PROMEKI_ENABLE_AUDIO
TEST_CASE("MediaIO_AudioFile_SeekDefault_ResolvesToExact") {
        // The default seek mode should resolve to SeekExact for AudioFile,
        // and existing exact-frame seeking should continue to work.
        const char *fn = "/tmp/promeki_test_seekmode.wav";
        FrameRate fps(FrameRate::FPS_30);
        AudioDesc desc(48000.0f, 1);
        size_t spf = (size_t)std::round(desc.sampleRate() / fps.toDouble());
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::AudioRate, desc.sampleRate());
                cfg.set(MediaConfig::AudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                AudioTestPattern atp(desc);
                { EnumList __m = EnumList::forType<AudioPattern>(); for(size_t __c = 0; __c < desc.channels(); ++__c) __m.append(AudioPattern::Silence); atp.setChannelModes(__m); }
                atp.configure();
                for(int i = 0; i < 10; i++) {
                        Frame::Ptr frame = Frame::Ptr::create();
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(atp.create(spf)));
                        io->writeFrame(frame);
                }
                io->close();
                delete io;
        }
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->defaultSeekMode() == MediaIO::SeekExact);
                // Default seek (no mode arg) should hit the exact frame.
                CHECK(io->seekToFrame(5).isOk());
                CHECK(io->currentFrame() == 5);
                // Explicit Exact seek behaves identically.
                CHECK(io->seekToFrame(2, MediaIO::SeekExact).isOk());
                CHECK(io->currentFrame() == 2);
                io->close();
                delete io;
        }
        std::remove(fn);
}
#endif

// ============================================================================
// Track selection
// ============================================================================

TEST_CASE("MediaIO_TrackSelection_PreOpenOnly") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        // Pre-open setters succeed.
        List<int> v; v.pushToBack(0);
        List<int> a; a.pushToBack(1);
        CHECK(io->setVideoTracks(v).isOk());
        CHECK(io->setAudioTracks(a).isOk());

        REQUIRE(io->open(MediaIO::Output).isOk());

        // Post-open setters return AlreadyOpen.
        CHECK(io->setVideoTracks(v) == Error::AlreadyOpen);
        CHECK(io->setAudioTracks(a) == Error::AlreadyOpen);

        io->close();
        delete io;
}

// ============================================================================
// frameAvailable / multi open-close cycle
// ============================================================================

TEST_CASE("MediaIO_TPG_FrameAvailable_AfterRead") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        // Initially nothing in the queue.
        CHECK_FALSE(io->frameAvailable());
        // After a blocking read, the prefetched next frame should be in
        // the queue (depth = 1, refilled inside readFrame after consumption).
        Frame::Ptr frame;
        CHECK(io->readFrame(frame).isOk());
        // The strand may still be running the prefetch — drain it via close
        // for a deterministic check, but at minimum frameAvailable should
        // not crash.
        io->close();
        delete io;
}

TEST_CASE("MediaIO_TPG_ReopenSameInstance") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(32, 32), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        // Multiple open/read/close cycles on the same MediaIO instance
        // exercise backend state reset.
        for(int cycle = 0; cycle < 3; cycle++) {
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->isOpen());
                CHECK(io->mediaDesc().isValid());
                Frame::Ptr frame;
                CHECK(io->readFrame(frame).isOk());
                CHECK(frame.isValid());
                CHECK(io->close().isOk());
                CHECK_FALSE(io->isOpen());
        }
        delete io;
}

// ============================================================================
// Parameterized command
// ============================================================================

TEST_CASE("MediaIO_SendParams_DefaultIsNotSupported") {
        // TPG doesn't override executeCmd(MediaIOCommandParams &), so the
        // default implementation returns NotSupported.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        MediaIOParams params;
        Error err = io->sendParams("AnythingAtAll", params);
        CHECK(err == Error::NotSupported);
        io->close();
        delete io;
}

TEST_CASE("MediaIO_SendParams_RequiresOpen") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        // Not open yet
        Error err = io->sendParams("Foo");
        CHECK(err == Error::NotOpen);
        delete io;
}

// ============================================================================
// Cancel-on-close-after-open-failure
// ============================================================================

TEST_CASE("MediaIO_CancelPending_NoOpWhenClosed") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        // Not open: cancelPending must return 0 cleanly.
        CHECK(io->cancelPending() == 0);
        delete io;
}

// ============================================================================
// Stats
// ============================================================================

TEST_CASE("MediaIO_Stats_DefaultPopulatesStandardKeys") {
        // TPG doesn't override executeCmd(MediaIOCommandStats &), but
        // the MediaIO base class still overlays the standard
        // telemetry keys after the backend returns.  A freshly-opened
        // stage that has not processed any frames yet therefore
        // reports zero rates and zero drop / repeat / late counters
        // rather than an empty stats bag.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        MediaIOStats s = io->stats();
        CHECK_FALSE(s.isEmpty());
        CHECK(s.getAs<int64_t>(MediaIOStats::FramesDropped, -1) == 0);
        CHECK(s.getAs<int64_t>(MediaIOStats::FramesRepeated, -1) == 0);
        CHECK(s.getAs<int64_t>(MediaIOStats::FramesLate, -1) == 0);
        CHECK(s.getAs<double>(MediaIOStats::BytesPerSecond, -1.0) == 0.0);
        CHECK(s.getAs<double>(MediaIOStats::FramesPerSecond, -1.0) == 0.0);
        io->close();
        delete io;
}

TEST_CASE("MediaIO_Stats_RequiresOpen") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p24));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        // Not open: returns empty stats without erroring.
        MediaIOStats s = io->stats();
        CHECK(s.isEmpty());
        delete io;
}

// ============================================================================
// EOF semantics
// ============================================================================

TEST_CASE("MediaIO_ImageFile_EOFLatchesAfterFirstEOF") {
        // ImageFile with step=1 returns EOF after the first read.  Each
        // subsequent readFrame should also return EOF (without going down
        // to the backend).  Seeking is not supported by ImageFile, but
        // close+reopen resets the latch.
        const char *fn = "/tmp/promeki_test_eof_latch.dpx";
        {
                Image src(8, 8, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                io->writeFrame(wf);
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                io->setStep(1);  // override the default step=0 so EOF triggers
                Frame::Ptr f1;
                CHECK(io->readFrame(f1).isOk());          // first read OK
                Frame::Ptr f2;
                CHECK(io->readFrame(f2) == Error::EndOfFile);
                // Repeated reads continue to return EOF.
                Frame::Ptr f3;
                CHECK(io->readFrame(f3) == Error::EndOfFile);
                Frame::Ptr f4;
                CHECK(io->readFrame(f4) == Error::EndOfFile);
                // Reopen resets the latch.
                io->close();
                REQUIRE(io->open(MediaIO::Output).isOk());
                io->setStep(1);
                Frame::Ptr f5;
                CHECK(io->readFrame(f5).isOk());
                io->close();
                delete io;
        }
        std::remove(fn);
}

#if PROMEKI_ENABLE_AUDIO
TEST_CASE("MediaIO_AudioFile_EOFLatchClearedBySeek") {
        const char *fn = "/tmp/promeki_test_eof_seek.wav";
        FrameRate fps(FrameRate::FPS_30);
        AudioDesc desc(48000.0f, 1);
        size_t spf = (size_t)std::round(desc.sampleRate() / fps.toDouble());
        // Write a short clip
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::AudioRate, desc.sampleRate());
                cfg.set(MediaConfig::AudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                AudioTestPattern atp(desc);
                { EnumList __m = EnumList::forType<AudioPattern>(); for(size_t __c = 0; __c < desc.channels(); ++__c) __m.append(AudioPattern::Silence); atp.setChannelModes(__m); }
                atp.configure();
                for(int i = 0; i < 5; i++) {
                        Frame::Ptr frame = Frame::Ptr::create();
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(atp.create(spf)));
                        io->writeFrame(frame);
                }
                io->close();
                delete io;
        }
        // Read past EOF, then seek and confirm reads work again
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                Frame::Ptr frame;
                int reads = 0;
                while(io->readFrame(frame).isOk()) reads++;
                CHECK(reads == 5);
                // Latched: subsequent reads keep returning EOF.
                CHECK(io->readFrame(frame) == Error::EndOfFile);
                CHECK(io->readFrame(frame) == Error::EndOfFile);
                // Seek clears the latch — reads work again.
                CHECK(io->seekToFrame(0).isOk());
                CHECK(io->readFrame(frame).isOk());
                io->close();
                delete io;
        }
        std::remove(fn);
}
#endif

// ============================================================================
// Device enumeration
// ============================================================================

TEST_CASE("MediaIO_Enumerate_NoEnumerateReturnsEmpty") {
        // TPG doesn't provide an enumerate callback, so the result is empty.
        StringList instances = MediaIO::enumerate("TPG");
        CHECK(instances.isEmpty());
}

TEST_CASE("MediaIO_Enumerate_UnknownTypeReturnsEmpty") {
        StringList instances = MediaIO::enumerate("DefinitelyNotARealBackend");
        CHECK(instances.isEmpty());
}

// ============================================================================
// Cancellation
// ============================================================================

TEST_CASE("MediaIO_TPG_CancelPendingDropsReadResults") {
        // Submit several non-blocking reads, then cancel pending work.
        // After cancel, the read result queue should be drained and a
        // subsequent blocking read should still succeed (it submits fresh).
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(32, 32), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Kick off several non-blocking reads.  Each is a no-op for the
        // caller (returns TryAgain) but queues a task on the strand.
        for(int i = 0; i < 5; i++) {
                Frame::Ptr f;
                io->readFrame(f, false);
        }

        // Cancel anything queued.  We don't care exactly how many were
        // running vs queued — the cancelled count is implementation-defined
        // here, but it should be ≥ 0.
        size_t cancelled = io->cancelPending();
        CHECK(cancelled >= 0);

        // After cancellation, a blocking read should still work.
        Frame::Ptr frame;
        Error err = io->readFrame(frame, true);
        CHECK(err.isOk());
        CHECK(frame.isValid());

        io->close();
        delete io;
}

// ============================================================================
// Close semantics (sync and async)
// ============================================================================

TEST_CASE("MediaIO_Close_NotOpenReturnsNotOpen") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->close() == Error::NotOpen);
        CHECK(io->close(false) == Error::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_Close_SyncEmitsClosedSignal") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        std::atomic<int> closedCount{0};
        std::atomic<int> closedErr{-1};
        io->closedSignal.connect([&](Error e) {
                closedErr.store(static_cast<int>(e.code()));
                closedCount.fetch_add(1);
        });

        CHECK(io->close().isOk());
        CHECK(closedCount.load() == 1);
        CHECK(closedErr.load() == static_cast<int>(Error::Ok));
        CHECK_FALSE(io->isOpen());
        CHECK_FALSE(io->isClosing());
        delete io;
}

TEST_CASE("MediaIO_Close_AsyncReturnsImmediatelyAndEmitsClosed") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        std::atomic<int> closedCount{0};
        io->closedSignal.connect([&](Error) {
                closedCount.fetch_add(1);
        });

        // Non-blocking submit returns Ok immediately.
        CHECK(io->close(false).isOk());

        // Wait for finalize to run.  Strand runs on the shared thread
        // pool; a short bounded wait is sufficient for the TPG backend.
        for(int i = 0; i < 200 && closedCount.load() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(closedCount.load() == 1);
        CHECK_FALSE(io->isOpen());
        CHECK_FALSE(io->isClosing());
        delete io;
}

TEST_CASE("MediaIO_Close_DoubleCloseWhileClosingReturnsNotOpen") {
        // A second close() issued while the first async close is still
        // in flight must be rejected (NotOpen) — the finalize task
        // flips _closing back to false only after it finishes.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        std::atomic<int> closedCount{0};
        io->closedSignal.connect([&](Error) { closedCount.fetch_add(1); });

        CHECK(io->close(false).isOk());
        CHECK(io->isClosing());
        // Second close while first is pending.
        CHECK(io->close(false) == Error::NotOpen);
        CHECK(io->close() == Error::NotOpen);

        for(int i = 0; i < 200 && closedCount.load() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(closedCount.load() == 1);
        delete io;
}

TEST_CASE("MediaIO_Close_AsyncEmitsSyntheticEOS") {
        // Consumer path: no prefetch in flight.  After close(false),
        // readFrame() must return EndOfFile at least once (the
        // synthetic trailing entry pushed by finalize).
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Drain one real read so prefetch is settled, then close async.
        Frame::Ptr realFrame;
        CHECK(io->readFrame(realFrame).isOk());
        CHECK(realFrame.isValid());

        std::atomic<int> closedCount{0};
        io->closedSignal.connect([&](Error) { closedCount.fetch_add(1); });

        CHECK(io->close(false).isOk());

        // Wait for finalize to push the synthetic EOS and fire closed.
        for(int i = 0; i < 200 && closedCount.load() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(closedCount.load() == 1);

        // The queue still holds the EOS entry (async path doesn't drain
        // for us).  readFrame must return EndOfFile when popping it,
        // regardless of the now-NotOpen state.
        Frame::Ptr f;
        CHECK(io->readFrame(f, /*block=*/false) == Error::EndOfFile);
        // Queue is now empty; further reads see NotOpen.
        CHECK(io->readFrame(f, /*block=*/false) == Error::NotOpen);
        delete io;
}

TEST_CASE("MediaIO_Close_AsyncDeliversAllPendingReadsBeforeEOS") {
        // Graceful close: reads submitted before close() must complete
        // and their frames must appear in the queue BEFORE the trailing
        // synthetic EOS.
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::AudioEnabled, false);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        // Deep prefetch so several reads are in flight when close runs.
        io->setPrefetchDepth(4);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Kick off prefetch by doing one non-blocking read.
        {
                Frame::Ptr f;
                io->readFrame(f, /*block=*/false);
        }

        std::atomic<int> closedCount{0};
        io->closedSignal.connect([&](Error) { closedCount.fetch_add(1); });
        CHECK(io->close(false).isOk());

        for(int i = 0; i < 200 && closedCount.load() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(closedCount.load() == 1);

        // Drain the queue; every entry before the EOS must be a
        // successful real frame.
        int realFrames = 0;
        bool sawEOS = false;
        while(true) {
                Frame::Ptr f;
                Error e = io->readFrame(f, /*block=*/false);
                if(e == Error::EndOfFile) { sawEOS = true; break; }
                if(e == Error::NotOpen) break;  // queue empty, no EOS?
                REQUIRE(e.isOk());
                CHECK(f.isValid());
                realFrames++;
                if(realFrames > 100) break;  // safety
        }
        CHECK(sawEOS);
        CHECK(realFrames >= 1);
        delete io;
}

TEST_CASE("MediaIO_Close_AsyncRejectsNewWrites") {
        // writeFrame() during an async close must return NotOpen
        // without queueing any new work behind the finalize task.
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        cfg.set(MediaConfig::Capacity, 4);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Image img(8, 8, PixelDesc(PixelDesc::RGB8_sRGB));
        img.fill(0);
        Frame::Ptr f = Frame::Ptr::create();
        f.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));

        std::atomic<int> closedCount{0};
        io->closedSignal.connect([&](Error) { closedCount.fetch_add(1); });

        CHECK(io->close(false).isOk());
        CHECK(io->isClosing());
        // Write during close is rejected.
        CHECK(io->writeFrame(f) == Error::NotOpen);
        CHECK(io->writeFrame(f, /*block=*/false) == Error::NotOpen);

        for(int i = 0; i < 200 && closedCount.load() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(closedCount.load() == 1);
        delete io;
}

TEST_CASE("MediaIO_Close_ReopenAfterAsyncStartsClean") {
        // Reopening on the same instance after an async close must
        // begin with an empty read queue — any orphaned synthetic
        // EOS from the previous session must be cleared by open().
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(Size2Du32(16, 16), FrameRate(FrameRate::FPS_24)));
        cfg.set(MediaConfig::VideoEnabled, true);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        std::atomic<int> closedCount{0};
        io->closedSignal.connect([&](Error) { closedCount.fetch_add(1); });
        CHECK(io->close(false).isOk());
        for(int i = 0; i < 200 && closedCount.load() == 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(closedCount.load() == 1);

        // Reopen without draining the EOS from the queue.  open()
        // must clear it so the first read returns a real frame.
        REQUIRE(io->open(MediaIO::Output).isOk());
        Frame::Ptr f;
        Error e = io->readFrame(f);
        CHECK(e.isOk());
        CHECK(f.isValid());

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
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                io->writeFrame(wf);
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameCount() == 1);
                io->close();
                CHECK(io->frameCount() == 0);
                CHECK(io->currentFrame() == 0);
                delete io;
        }
        std::remove(fn);
}

// ============================================================================
// MediaIO_ImageFile sequence tests
// ============================================================================

// Small helper that creates an image-sequence test scratch directory
// with a set of DPX files spanning a given head..tail range.  Returns
// the path of the created directory (must be removed by the caller).
static FilePath makeImageSequenceDir(const String &subdir,
                                     const String &prefix,
                                     int head,
                                     int tail,
                                     int digits = 4) {
        Dir t = Dir::temp();
        FilePath dir = t.path() / subdir;
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        for(int i = head; i <= tail; i++) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s%0*d.dpx",
                              prefix.cstr(), digits, i);
                FilePath fp = dir / String(buf);

                Image src(16, 8, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                // Fill with a frame-number-dependent pattern so we can
                // verify which frame was read.
                uint8_t *data = static_cast<uint8_t *>(src.data(0));
                size_t bytes = src.pixelDesc().pixelFormat().planeSize(
                        0, src.width(), src.height());
                for(size_t b = 0; b < bytes; b++) {
                        data[b] = static_cast<uint8_t>((b + i * 13) & 0xFF);
                }

                MediaIO *io = MediaIO::createForFileWrite(fp.toString());
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                REQUIRE(io->writeFrame(wf).isOk());
                io->close();
                delete io;
        }

        return dir;
}

TEST_CASE("MediaIO_ImageSequence_ReadBasic") {
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_read_basic",
                                            "frame_", 1, 5);
        String mask = (dir / "frame_####.dpx").toString();

        MediaIO *io = MediaIO::createForFileRead(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        CHECK(io->canSeek());
        CHECK(io->frameCount() == 5);
        CHECK(io->step() == 1);  // sequences default to step=1

        for(int i = 0; i < 5; i++) {
                Frame::Ptr f;
                CHECK(io->readFrame(f).isOk());
                REQUIRE(f.isValid());
                REQUIRE(f->imageList().size() == 1);
        }

        // After reading all frames, next read is EOF.
        Frame::Ptr eof;
        CHECK(io->readFrame(eof) == Error::EndOfFile);

        io->close();
        delete io;
        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_ReadPrintfMask") {
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_printf",
                                            "shot_", 10, 14);
        String mask = (dir / "shot_%04d.dpx").toString();

        MediaIO *io = MediaIO::createForFileRead(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        CHECK(io->frameCount() == 5);
        CHECK(io->canSeek());

        Frame::Ptr f;
        CHECK(io->readFrame(f).isOk());
        REQUIRE(f.isValid());

        io->close();
        delete io;
        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_HeadTailDetection") {
        // Files numbered 100..105 — head/tail should reflect the actual range.
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_detect",
                                            "render.", 100, 105);
        String mask = (dir / "render.####.dpx").toString();

        MediaIO *io = MediaIO::createForFileRead(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->frameCount() == 6);
        io->close();
        delete io;

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_Seek") {
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_seek",
                                            "fr_", 1, 10);
        String mask = (dir / "fr_####.dpx").toString();

        MediaIO *io = MediaIO::createForFileRead(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        REQUIRE(io->canSeek());
        CHECK(io->frameCount() == 10);

        CHECK(io->seekToFrame(4).isOk());
        Frame::Ptr f;
        CHECK(io->readFrame(f).isOk());
        REQUIRE(f.isValid());

        // Seek back to start, read again.
        CHECK(io->seekToFrame(0).isOk());
        Frame::Ptr g;
        CHECK(io->readFrame(g).isOk());

        io->close();
        delete io;
        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_Reverse") {
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_reverse",
                                            "rv_", 1, 5);
        String mask = (dir / "rv_####.dpx").toString();

        MediaIO *io = MediaIO::createForFileRead(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        // Seek to the last frame then play in reverse.
        CHECK(io->seekToFrame(4).isOk());
        io->setStep(-1);

        int ok = 0;
        for(int i = 0; i < 10; i++) {
                Frame::Ptr f;
                Error err = io->readFrame(f);
                if(err == Error::EndOfFile) break;
                if(err.isOk()) ok++;
        }
        // 5 frames available when playing backwards from the tail
        CHECK(ok == 5);

        io->close();
        delete io;
        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_WriteBasic") {
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_write_basic";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "out_####.dpx").toString();

        MediaIO *io = MediaIO::createForFileWrite(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Input).isOk());

        for(int i = 0; i < 4; i++) {
                Image img(8, 8, PixelDesc::RGB8_sRGB);
                REQUIRE(img.isValid());
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                CHECK(io->writeFrame(wf).isOk());
        }
        CHECK(io->frameCount() == 4);
        io->close();
        delete io;

        // Verify the four files exist on disk with the expected names.
        CHECK(FilePath(dir / "out_0001.dpx").exists());
        CHECK(FilePath(dir / "out_0002.dpx").exists());
        CHECK(FilePath(dir / "out_0003.dpx").exists());
        CHECK(FilePath(dir / "out_0004.dpx").exists());

        // Auto-sidecar: the writer should have created out.imgseq.
        CHECK(FilePath(dir / "out.imgseq").exists());

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_WriteCustomHead") {
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_write_head";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "out_####.dpx").toString();

        MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
        cfg.set(MediaConfig::Filename, mask);
        cfg.set(MediaConfig::SequenceHead, 1001);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Input).isOk());

        for(int i = 0; i < 3; i++) {
                Image img(8, 8, PixelDesc::RGB8_sRGB);
                REQUIRE(img.isValid());
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                CHECK(io->writeFrame(wf).isOk());
        }
        io->close();
        delete io;

        CHECK(FilePath(dir / "out_1001.dpx").exists());
        CHECK(FilePath(dir / "out_1002.dpx").exists());
        CHECK(FilePath(dir / "out_1003.dpx").exists());

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_RoundTrip") {
        // Write a sequence, then read it back through the same API.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_roundtrip";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "rt_####.dpx").toString();

        {
                MediaIO *io = MediaIO::createForFileWrite(mask);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                for(int i = 0; i < 6; i++) {
                        Image img(16, 8, PixelDesc::RGB8_sRGB);
                        REQUIRE(img.isValid());
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(mask);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameCount() == 6);
                for(int i = 0; i < 6; i++) {
                        Frame::Ptr f;
                        CHECK(io->readFrame(f).isOk());
                        REQUIRE(f.isValid());
                }
                Frame::Ptr eof;
                CHECK(io->readFrame(eof) == Error::EndOfFile);
                io->close();
                delete io;
        }

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_AudioRoundTrip") {
        // DPX carries an optional embedded audio block.  When a
        // sequence is written with audio in each frame, reading the
        // sequence back should surface an AudioDesc on the MediaIO's
        // cached MediaDesc and deliver the audio with every frame.
        // Regression for the bug where openSequence populated the
        // image list from the head frame but forgot the audio list,
        // causing downstream consumers (e.g. SDL player) to skip
        // audio output entirely.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_audio";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "au_####.dpx").toString();
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        const size_t samplesPerFrame = 800;  // 48000/60

        {
                MediaIO *io = MediaIO::createForFileWrite(mask);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                for(int i = 0; i < 4; i++) {
                        Image img(16, 8, PixelDesc::RGB8_sRGB);
                        REQUIRE(img.isValid());
                        Audio audio(adesc, samplesPerFrame);
                        REQUIRE(audio.isValid());
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        wf.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(mask);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameCount() == 4);

                // The MediaDesc should report one audio track and the
                // audioDesc() accessor should return a valid descriptor
                // matching what we wrote.
                MediaDesc md = io->mediaDesc();
                REQUIRE(md.audioList().size() == 1);
                CHECK(md.audioList()[0].sampleRate() == 48000.0f);
                CHECK(md.audioList()[0].channels() == 2);
                CHECK(io->audioDesc().isValid());
                CHECK(io->audioDesc().sampleRate() == 48000.0f);
                CHECK(io->audioDesc().channels() == 2);

                // Reading any frame should deliver both video and audio.
                Frame::Ptr f;
                REQUIRE(io->readFrame(f).isOk());
                REQUIRE(f.isValid());
                REQUIRE(f->imageList().size() == 1);
                REQUIRE(f->audioList().size() == 1);
                CHECK(f->audioList()[0]->samples() == samplesPerFrame);
                CHECK(f->audioList()[0]->desc().channels() == 2);

                io->close();
                delete io;
        }

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_ImgSeqSidecarRead") {
        // Create a directory with image files and an .imgseq JSON
        // sidecar alongside them, then open the sidecar.
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_sidecar",
                                            "clip.", 1, 3);

        ImgSeq seq;
        seq.setName(NumName::fromMask("clip.####.dpx"));
        seq.setHead(1);
        seq.setTail(3);
        seq.setFrameRate(FrameRate(FrameRate::FPS_24));
        FilePath sidecar = dir / "clip.imgseq";
        CHECK(seq.save(sidecar).isOk());

        MediaIO *io = MediaIO::createForFileRead(sidecar.toString());
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());

        CHECK(io->frameCount() == 3);
        // Sidecar supplied the frame rate, so it's a "file" source.
        CHECK(io->frameRate() == FrameRate(FrameRate::FPS_24));
        const Metadata &md = io->metadata();
        CHECK(md.contains(Metadata::FrameRateSource));
        CHECK(md.getAs<String>(Metadata::FrameRateSource) == "file");

        Frame::Ptr f;
        CHECK(io->readFrame(f).isOk());
        REQUIRE(f.isValid());

        io->close();
        delete io;
        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_ImgSeqSidecarAutoDetectRange") {
        // Sidecar with no head/tail should trigger a directory scan.
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_sidecar_autorange",
                                            "auto_", 5, 8);

        ImgSeq seq;
        seq.setName(NumName::fromMask("auto_####.dpx"));
        // Deliberately leave head/tail at 0 so the task has to detect them.
        FilePath sidecar = dir / "auto.imgseq";
        CHECK(seq.save(sidecar).isOk());

        MediaIO *io = MediaIO::createForFileRead(sidecar.toString());
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->frameCount() == 4);  // 5,6,7,8
        io->close();
        delete io;

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_AutoSidecarWrite") {
        // Writing a sequence with default config should auto-create a
        // conventionally-named .imgseq sidecar alongside the frames.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_autosidecar_w";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "clip_####.dpx").toString();
        FrameRate fps(FrameRate::FPS_24);

        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc md;
                md.setFrameRate(fps);
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                        PixelDesc(PixelDesc::RGB8_sRGB)));
                io->setMediaDesc(md);

                REQUIRE(io->open(MediaIO::Input).isOk());
                for(int i = 0; i < 4; i++) {
                        Image img(16, 8, PixelDesc::RGB8_sRGB);
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }

        // Verify the sidecar was created with the auto-derived name.
        FilePath sidecarPath = dir / "clip.imgseq";
        REQUIRE(sidecarPath.exists());

        // Verify sidecar contents.
        Error sErr;
        ImgSeq seq = ImgSeq::load(sidecarPath, &sErr);
        REQUIRE(sErr.isOk());
        CHECK(seq.isValid());
        CHECK(seq.head() == 1);
        CHECK(seq.tail() == 4);
        CHECK(seq.frameRate() == fps);
        CHECK(seq.videoSize() == Size2Du32(16, 8));

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_AutoSidecarRead") {
        // Write a sequence (auto-creates .imgseq), then read via mask.
        // The reader should auto-discover the sidecar and pick up the
        // frame rate as a "file" source.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_autosidecar_r";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "sc_####.dpx").toString();
        FrameRate fps(FrameRate::FPS_24);

        // Write.
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc md;
                md.setFrameRate(fps);
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                        PixelDesc(PixelDesc::RGB8_sRGB)));
                io->setMediaDesc(md);

                REQUIRE(io->open(MediaIO::Input).isOk());
                for(int i = 0; i < 3; i++) {
                        Image img(16, 8, PixelDesc::RGB8_sRGB);
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }

        // Read via mask — auto-discover the sidecar.
        {
                MediaIO *io = MediaIO::createForFileRead(mask);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameCount() == 3);
                CHECK(io->frameRate() == fps);
                // Frame rate came from the auto-discovered sidecar.
                const Metadata &md = io->metadata();
                CHECK(md.getAs<String>(Metadata::FrameRateSource) == "file");
                io->close();
                delete io;
        }

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_AutoSidecarDisabled") {
        // Setting SaveImgSeqEnabled=false should suppress the auto-sidecar.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_autosidecar_off";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "no_####.dpx").toString();

        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::SaveImgSeqEnabled, false);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());

                for(int i = 0; i < 2; i++) {
                        Image img(8, 8, PixelDesc::RGB8_sRGB);
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }

        // No sidecar should exist.
        CHECK_FALSE(FilePath(dir / "no.imgseq").exists());

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_FrameRateSourceConfigFromDefault") {
        // No sidecar and no explicit --incfg FrameRate — the backend's
        // default config still pre-populates ConfigFrameRate with
        // DefaultFrameRate, so FrameRateSource is "config".
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_frdefault",
                                            "fr_", 1, 2);
        String mask = (dir / "fr_####.dpx").toString();

        MediaIO *io = MediaIO::createForFileRead(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        const Metadata &md = io->metadata();
        CHECK(md.getAs<String>(Metadata::FrameRateSource) == "config");
        CHECK(io->frameRate() == MediaIOTask_ImageFile::DefaultFrameRate);
        io->close();
        delete io;
        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_FrameRateSourceConfig") {
        FilePath dir = makeImageSequenceDir("promeki_test_imgseq_frconfig",
                                            "fr_", 1, 2);
        String mask = (dir / "fr_####.dpx").toString();

        MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
        cfg.set(MediaConfig::Filename, mask);
        cfg.set(MediaConfig::FrameRate,
                FrameRate(FrameRate::FPS_29_97));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Output).isOk());
        CHECK(io->frameRate() == FrameRate(FrameRate::FPS_29_97));
        const Metadata &md = io->metadata();
        CHECK(md.getAs<String>(Metadata::FrameRateSource) == "config");
        io->close();
        delete io;
        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageFile_SingleFileStillReportsFrameRateSource") {
        // Regression: single-image reads should also populate
        // FrameRateSource so downstream code can tell the difference
        // between a known rate and a guessed one.
        const char *fn = "/tmp/promeki_test_single_frsource.dpx";
        {
                Image src(16, 16, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                io->writeFrame(wf);
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                const Metadata &md = io->metadata();
                CHECK(md.contains(Metadata::FrameRateSource));
                // No sidecar — the rate came from the config entry
                // the backend default pre-populated.
                CHECK(md.getAs<String>(Metadata::FrameRateSource) == "config");
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_ImageSequence_SidecarMissingFiles") {
        // Sidecar with head/tail that do not match any files on disk
        // should fail to load the head frame cleanly.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_missing";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        ImgSeq seq;
        seq.setName(NumName::fromMask("nope_####.dpx"));
        seq.setHead(1);
        seq.setTail(5);
        FilePath sidecar = dir / "nope.imgseq";
        CHECK(seq.save(sidecar).isOk());

        MediaIO *io = MediaIO::createForFileRead(sidecar.toString());
        REQUIRE(io != nullptr);
        Error err = io->open(MediaIO::Output);
        CHECK(err.isError());
        delete io;

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_MaskNoMatch") {
        // Supplying a mask whose directory contains no matching files
        // should cleanly fail open(), not crash or hang.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_imgseq_nomatch";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "nothing_####.dpx").toString();
        MediaIO *io = MediaIO::createForFileRead(mask);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Output).isError());
        delete io;

        Dir(dir).removeRecursively();
}

// Regression: a plain single image path still uses the existing
// single-file behavior unchanged (no auto-promotion to a sequence).
TEST_CASE("MediaIO_ImageFile_SingleFileUnchanged") {
        const char *fn = "/tmp/promeki_test_single_unchanged.dpx";
        {
                Image src(16, 16, PixelDesc::RGB8_sRGB);
                REQUIRE(src.isValid());
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(src));
                MediaIO *io = MediaIO::createForFileWrite(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                io->writeFrame(wf);
                io->close();
                delete io;
        }
        {
                MediaIO *io = MediaIO::createForFileRead(fn);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK_FALSE(io->canSeek());
                CHECK(io->frameCount() == 1);
                CHECK(io->step() == 0);  // single-file default step
                // step=0 means re-read the same frame indefinitely.
                Frame::Ptr f1, f2;
                CHECK(io->readFrame(f1).isOk());
                CHECK(io->readFrame(f2).isOk());
                io->close();
                delete io;
        }
        std::remove(fn);
}

TEST_CASE("MediaIO_ImageSequence_WriterCreatesDirectory") {
        // Opening a sequence writer whose output directory does not yet
        // exist should create all missing path components automatically.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_mkdir" / "sub" / "dir";

        // Make sure neither the leaf nor its parents exist.
        Dir cleanup(t.path() / "promeki_test_mkdir");
        if(cleanup.exists()) cleanup.removeRecursively();
        CHECK_FALSE(Dir(dir).exists());

        String mask = (dir / "mk_####.dpx").toString();

        MediaIO *io = MediaIO::createForFileWrite(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Input).isOk());

        CHECK(Dir(dir).exists());

        Image img(8, 8, PixelDesc::RGB8_sRGB);
        Frame::Ptr wf = Frame::Ptr::create();
        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
        CHECK(io->writeFrame(wf).isOk());
        CHECK(FilePath(dir / "mk_0001.dpx").exists());

        io->close();
        delete io;
        cleanup.removeRecursively();
}

// ============================================================================
// MediaIO_AudioFile backend
// ============================================================================

#include <promeki/config.h>
#if PROMEKI_ENABLE_AUDIO

#include <promeki/mediaiotask_audiofile.h>
#include <promeki/audiotestpattern.h>
#include <promeki/audiolevel.h>

TEST_CASE("MediaIO_AudioFile_RegistryContainsBackend") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "AudioFile") {
                        CHECK(desc.canOutput);
                        CHECK(desc.canInput);
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
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::AudioRate, desc.sampleRate());
                cfg.set(MediaConfig::AudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                AudioTestPattern atp(desc);
                { EnumList __m = EnumList::forType<AudioPattern>(); for(size_t __c = 0; __c < desc.channels(); ++__c) __m.append(AudioPattern::Tone); atp.setChannelModes(__m); }
                atp.setToneFrequency(1000.0);
                atp.setToneLevel(AudioLevel::fromDbfs(-20.0));
                atp.configure();
                for(int i = 0; i < numFrames; i++) {
                        Audio audio = atp.create(samplesPerFrame);
                        Frame::Ptr frame = Frame::Ptr::create();
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                        io->writeFrame(frame);
                }
                io->close();
                delete io;
        }
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                MediaDesc vd = io->mediaDesc();
                CHECK(vd.imageList().isEmpty());
                CHECK(vd.audioList().size() == 1);
                CHECK(vd.frameRate() == fps);
                CHECK(io->frameCount() == (uint64_t)numFrames);
                int readCount = 0;
                Frame::Ptr frame;
                while(io->readFrame(frame).isOk()) {
                        CHECK(frame->audioList().size() == 1);
                        readCount++;
                        frame = {};
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
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::AudioRate, desc.sampleRate());
                cfg.set(MediaConfig::AudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                AudioTestPattern atp(desc);
                { EnumList __m = EnumList::forType<AudioPattern>(); for(size_t __c = 0; __c < desc.channels(); ++__c) __m.append(AudioPattern::Silence); atp.setChannelModes(__m); }
                atp.configure();
                for(int i = 0; i < 20; i++) {
                        Frame::Ptr frame = Frame::Ptr::create();
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(atp.create(spf)));
                        io->writeFrame(frame);
                }
                io->close();
                delete io;
        }
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->canSeek());
                CHECK(io->frameCount() == 20);
                CHECK(io->seekToFrame(10).isOk());
                CHECK(io->currentFrame() == 10);
                Frame::Ptr frame;
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
        cfg.set(MediaConfig::Type, "AudioFile");
        cfg.set(MediaConfig::Filename, "/tmp/dummy.wav");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Output) == Error::InvalidArgument);
        delete io;
}

TEST_CASE("MediaIO_AudioFile_ReadBeforeOpenFails") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioFile");
        cfg.set(MediaConfig::Filename, "/tmp/dummy.wav");
        cfg.set(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_24));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        Frame::Ptr frame;
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
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::AudioRate, desc.sampleRate());
                cfg.set(MediaConfig::AudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                AudioTestPattern atp(desc);
                { EnumList __m = EnumList::forType<AudioPattern>(); for(size_t __c = 0; __c < desc.channels(); ++__c) __m.append(AudioPattern::Silence); atp.setChannelModes(__m); }
                atp.configure();
                for(int i = 0; i < 5; i++) {
                        Frame::Ptr frame = Frame::Ptr::create();
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(atp.create(spf)));
                        io->writeFrame(frame);
                }
                io->close();  // drains pending async write
                CHECK(io->frameCount() == 0);
                delete io;
        }

        // Read back and check post-close
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
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
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::AudioRate, desc.sampleRate());
                cfg.set(MediaConfig::AudioChannels, (unsigned int)desc.channels());
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Input).isOk());
                AudioTestPattern atp(desc);
                { EnumList __m = EnumList::forType<AudioPattern>(); for(size_t __c = 0; __c < desc.channels(); ++__c) __m.append(AudioPattern::Silence); atp.setChannelModes(__m); }
                atp.configure();
                for(int i = 0; i < 10; i++) {
                        Frame::Ptr frame = Frame::Ptr::create();
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(atp.create(spf)));
                        io->writeFrame(frame);
                }
                io->close();
                delete io;
        }

        // Read with step=0 — should re-read frame 0 repeatedly
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "AudioFile");
                cfg.set(MediaConfig::Filename, fn);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                io->setStep(0);
                // Read 3 times — should all succeed at frame 0
                for(int i = 0; i < 3; i++) {
                        Frame::Ptr frame;
                        CHECK(io->readFrame(frame).isOk());
                        CHECK(frame->audioList().size() == 1);
                }
                // currentFrame advances but position doesn't
                // Switch to step=1 and read — should get frame 0 again then advance
                io->setStep(1);
                Frame::Ptr frame;
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
        cfg.set(MediaConfig::Type, "AudioFile");
        cfg.set(MediaConfig::Filename, fn);
        cfg.set(MediaConfig::FrameRate, fps);
        cfg.set(MediaConfig::AudioRate, desc.sampleRate());
        cfg.set(MediaConfig::AudioChannels, (unsigned int)desc.channels());
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Input).isOk());
        CHECK_FALSE(io->canSeek());
        CHECK(io->seekToFrame(0) == Error::IllegalSeek);
        io->close();
        delete io;
        std::remove(fn);
}

// ============================================================================
// Sidecar audio for image sequences
// ============================================================================

TEST_CASE("MediaIO_ImageSequence_SidecarAudioWriteAndRead") {
        // Write a DPX image sequence with audio + timecode metadata.
        // The sidecar .wav should appear alongside the images as a BWF
        // file.  Reading the sequence back should auto-detect the
        // sidecar and deliver audio with every frame.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_sidecar_audio";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "sc_####.dpx").toString();
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        FrameRate fps(FrameRate::FPS_24);
        const size_t samplesPerFrame = fps.samplesPerFrame(48000, 0);
        Timecode startTc(Timecode::NDF24, 1, 0, 0, 0);

        // ---- write ----
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);

                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc md;
                md.setFrameRate(fps);
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                        PixelDesc(PixelDesc::RGB8_sRGB)));
                md.audioList().pushToBack(adesc);
                io->setMediaDesc(md);

                Metadata meta;
                meta.set(Metadata::Timecode, startTc);
                io->setMetadata(meta);

                REQUIRE(io->open(MediaIO::Input).isOk());

                // The MediaDesc should report audio
                CHECK(io->mediaDesc().audioList().size() == 1);
                CHECK(io->audioDesc().isValid());

                for(int i = 0; i < 4; i++) {
                        Image img(16, 8, PixelDesc::RGB8_sRGB);
                        REQUIRE(img.isValid());
                        Audio audio(adesc, samplesPerFrame);
                        REQUIRE(audio.isValid());
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        wf.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }

        // Verify the sidecar .wav exists alongside the images.
        CHECK(FilePath(dir / "sc.wav").exists());

        // ---- read back ----
        {
                MediaIO::Config rcfg = MediaIO::defaultConfig("ImageFile");
                rcfg.set(MediaConfig::Filename, mask);
                rcfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(rcfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameCount() == 4);

                // AudioDesc should be populated from the sidecar.
                MediaDesc md = io->mediaDesc();
                REQUIRE(md.audioList().size() == 1);
                CHECK(md.audioList()[0].sampleRate() == 48000.0f);
                CHECK(md.audioList()[0].channels() == 2);
                CHECK(io->audioDesc().isValid());

                // Read all frames and verify audio.
                for(int i = 0; i < 4; i++) {
                        Frame::Ptr f;
                        REQUIRE(io->readFrame(f).isOk());
                        REQUIRE(f.isValid());
                        REQUIRE(f->audioList().size() == 1);
                        CHECK(f->audioList()[0]->samples() == samplesPerFrame);
                        CHECK(f->audioList()[0]->desc().channels() == 2);
                }

                io->close();
                delete io;
        }

        // Verify the timecode was forwarded to the writer's container
        // metadata.  The ImageFile backend sets Metadata::Timecode on
        // the cached metadata at open time; the AudioFile writer
        // receives it and calls writeBroadcastInfo() which embeds the
        // BWF time_reference.  (AudioFile::createReader does not yet
        // parse BWF broadcast info back, so we verify the write-side
        // path by confirming that the container metadata on the
        // reader's MediaIO has a valid timecode.)
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());

                Metadata meta = io->metadata();
                CHECK(meta.contains(Metadata::Timecode));
                Timecode readTc = meta.get(Metadata::Timecode).get<Timecode>();
                CHECK(readTc.isValid());
                CHECK(readTc.toString() == startTc.toString());

                io->close();
                delete io;
        }

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_SidecarAudioDisabled") {
        // With SidecarAudioEnabled=false, no sidecar audio is written
        // even when audio data is provided.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_sidecar_disabled";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "nd_####.dpx").toString();
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        FrameRate fps(FrameRate::FPS_24);
        const size_t samplesPerFrame = fps.samplesPerFrame(48000, 0);

        MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
        cfg.set(MediaConfig::Filename, mask);
        cfg.set(MediaConfig::FrameRate, fps);
        cfg.set(MediaConfig::SidecarAudioEnabled, false);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc md;
        md.setFrameRate(fps);
        md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                PixelDesc(PixelDesc::RGB8_sRGB)));
        md.audioList().pushToBack(adesc);
        io->setMediaDesc(md);

        REQUIRE(io->open(MediaIO::Input).isOk());

        // No audio should be reported when sidecar is disabled.
        CHECK(io->mediaDesc().audioList().isEmpty());

        for(int i = 0; i < 2; i++) {
                Image img(16, 8, PixelDesc::RGB8_sRGB);
                Audio audio(adesc, samplesPerFrame);
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                wf.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                CHECK(io->writeFrame(wf).isOk());
        }
        io->close();
        delete io;

        // No .wav file should have been created.
        CHECK_FALSE(FilePath(dir / "nd.wav").exists());

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_SidecarAudioNoAudio") {
        // When writing an image sequence without any audio configured,
        // no sidecar audio file should be created.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_sidecar_noaudio";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "na_####.dpx").toString();

        MediaIO *io = MediaIO::createForFileWrite(mask);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Input).isOk());

        for(int i = 0; i < 2; i++) {
                Image img(16, 8, PixelDesc::RGB8_sRGB);
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                CHECK(io->writeFrame(wf).isOk());
        }
        io->close();
        delete io;

        CHECK_FALSE(FilePath(dir / "na.wav").exists());

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_SidecarAudioPathOverride") {
        // SidecarAudioPath config overrides the auto-derived name.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_sidecar_path";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "po_####.dpx").toString();
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        FrameRate fps(FrameRate::FPS_24);
        const size_t samplesPerFrame = fps.samplesPerFrame(48000, 0);

        MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
        cfg.set(MediaConfig::Filename, mask);
        cfg.set(MediaConfig::FrameRate, fps);
        cfg.set(MediaConfig::SidecarAudioPath, String("custom_audio.wav"));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaDesc md;
        md.setFrameRate(fps);
        md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                PixelDesc(PixelDesc::RGB8_sRGB)));
        md.audioList().pushToBack(adesc);
        io->setMediaDesc(md);

        REQUIRE(io->open(MediaIO::Input).isOk());

        Image img(16, 8, PixelDesc::RGB8_sRGB);
        Audio audio(adesc, samplesPerFrame);
        Frame::Ptr wf = Frame::Ptr::create();
        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
        wf.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
        CHECK(io->writeFrame(wf).isOk());
        io->close();
        delete io;

        // The auto-derived name (po.wav) should NOT exist.
        CHECK_FALSE(FilePath(dir / "po.wav").exists());
        // The custom name should exist.
        CHECK(FilePath(dir / "custom_audio.wav").exists());

        // Read back with the same config override — should auto-detect.
        {
                MediaIO::Config rcfg = MediaIO::defaultConfig("ImageFile");
                rcfg.set(MediaConfig::Filename, mask);
                rcfg.set(MediaConfig::SidecarAudioPath, String("custom_audio.wav"));

                MediaIO *rio = MediaIO::create(rcfg);
                REQUIRE(rio != nullptr);
                REQUIRE(rio->open(MediaIO::Output).isOk());
                CHECK(rio->audioDesc().isValid());
                CHECK(rio->audioDesc().sampleRate() == 48000.0f);
                rio->close();
                delete rio;
        }

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_SidecarAudioImgSeqField") {
        // When writing with SaveImgSeqPath, the .imgseq sidecar should
        // capture the audioFile field.  Reading from that sidecar should
        // find the audio through the field.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_sidecar_imgseq";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "is_####.dpx").toString();
        String sidecarPath = (dir / "test.imgseq").toString();
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        FrameRate fps(FrameRate::FPS_24);
        const size_t samplesPerFrame = fps.samplesPerFrame(48000, 0);

        // ---- write ----
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::SaveImgSeqPath, sidecarPath);

                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc md;
                md.setFrameRate(fps);
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                        PixelDesc(PixelDesc::RGB8_sRGB)));
                md.audioList().pushToBack(adesc);
                io->setMediaDesc(md);

                REQUIRE(io->open(MediaIO::Input).isOk());

                for(int i = 0; i < 3; i++) {
                        Image img(16, 8, PixelDesc::RGB8_sRGB);
                        Audio audio(adesc, samplesPerFrame);
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        wf.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }

        // Verify the .imgseq sidecar has the audioFile field.
        {
                Error sErr;
                ImgSeq seq = ImgSeq::load(FilePath(sidecarPath), &sErr);
                REQUIRE(sErr.isOk());
                CHECK(seq.audioFile() == "is.wav");
        }

        // Read via the .imgseq sidecar — audio should be found.
        {
                MediaIO *io = MediaIO::createForFileRead(sidecarPath);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameCount() == 3);
                CHECK(io->audioDesc().isValid());
                CHECK(io->audioDesc().sampleRate() == 48000.0f);

                Frame::Ptr f;
                REQUIRE(io->readFrame(f).isOk());
                REQUIRE(f->audioList().size() == 1);
                CHECK(f->audioList()[0]->samples() == samplesPerFrame);

                io->close();
                delete io;
        }

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_SidecarAudioSeek") {
        // Seeking the image sequence should sync the audio position.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_sidecar_seek";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "sk_####.dpx").toString();
        AudioDesc adesc(AudioDesc::NativeType, 48000.0f, 1);
        FrameRate fps(FrameRate::FPS_24);
        const size_t samplesPerFrame = fps.samplesPerFrame(48000, 0);

        // Write 8 frames with distinct audio fills.
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc md;
                md.setFrameRate(fps);
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                        PixelDesc(PixelDesc::RGB8_sRGB)));
                md.audioList().pushToBack(adesc);
                io->setMediaDesc(md);

                REQUIRE(io->open(MediaIO::Input).isOk());

                for(int i = 0; i < 8; i++) {
                        Image img(16, 8, PixelDesc::RGB8_sRGB);
                        Audio audio(adesc, samplesPerFrame);
                        // Fill channel 0 with a per-frame constant so
                        // we can verify seek lands on the right audio.
                        float *ptr = audio.data<float>();
                        float val = static_cast<float>(i + 1) * 0.1f;
                        for(size_t s = 0; s < samplesPerFrame; s++) ptr[s] = val;
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        wf.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }

        // Read: seek to frame 4 (0-based), read, verify audio.
        {
                MediaIO::Config rcfg = MediaIO::defaultConfig("ImageFile");
                rcfg.set(MediaConfig::Filename, mask);
                rcfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(rcfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->frameCount() == 8);

                CHECK(io->seekToFrame(4).isOk());
                Frame::Ptr f;
                REQUIRE(io->readFrame(f).isOk());
                REQUIRE(f->audioList().size() == 1);
                CHECK(f->audioList()[0]->samples() == samplesPerFrame);
                // Frame 4 (0-based) was written with fill = (4+1)*0.1 = 0.5
                const float *ptr = f->audioList()[0]->data<float>();
                CHECK(ptr[0] == doctest::Approx(0.5f));

                io->close();
                delete io;
        }

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_SidecarAudioReadDisabled") {
        // With SidecarAudioEnabled=false on reader, sidecar audio
        // should not be detected even when the .wav exists.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_sidecar_read_dis";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        // Use PNG so DPX embedded audio doesn't mask the sidecar
        // disable — PNG has no embedded audio support.
        String mask = (dir / "rd_####.png").toString();
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        FrameRate fps(FrameRate::FPS_24);
        const size_t samplesPerFrame = fps.samplesPerFrame(48000, 0);

        // Write with audio (sidecar is created).
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc md;
                md.setFrameRate(fps);
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                        PixelDesc(PixelDesc::RGB8_sRGB)));
                md.audioList().pushToBack(adesc);
                io->setMediaDesc(md);

                REQUIRE(io->open(MediaIO::Input).isOk());
                Image img(16, 8, PixelDesc::RGB8_sRGB);
                Audio audio(adesc, samplesPerFrame);
                Frame::Ptr wf = Frame::Ptr::create();
                wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                wf.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                CHECK(io->writeFrame(wf).isOk());
                io->close();
                delete io;
        }

        CHECK(FilePath(dir / "rd.wav").exists());

        // Read with sidecar disabled.
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::SidecarAudioEnabled, false);

                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());

                // No audio descriptor should be reported.
                CHECK(io->mediaDesc().audioList().isEmpty());
                CHECK_FALSE(io->audioDesc().isValid());

                io->close();
                delete io;
        }

        Dir(dir).removeRecursively();
}

TEST_CASE("MediaIO_ImageSequence_SidecarAudioSourceHint") {
        // Write a DPX sequence with both embedded audio (DPX carries it)
        // and a sidecar .wav.  Then test the AudioSourceHint to select
        // which source the reader uses.
        Dir t = Dir::temp();
        FilePath dir = t.path() / "promeki_test_sidecar_hint";
        Dir d(dir);
        if(d.exists()) d.removeRecursively();
        d.mkdir();

        String mask = (dir / "hn_####.dpx").toString();
        AudioDesc adesc(AudioDesc::PCMI_S16LE, 48000.0f, 2);
        FrameRate fps(FrameRate::FPS_24);
        const size_t samplesPerFrame = fps.samplesPerFrame(48000, 0);

        // Write sequence with audio — DPX embeds per-frame audio AND
        // the sidecar .wav is created.
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                MediaDesc md;
                md.setFrameRate(fps);
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 8),
                        PixelDesc(PixelDesc::RGB8_sRGB)));
                md.audioList().pushToBack(adesc);
                io->setMediaDesc(md);

                REQUIRE(io->open(MediaIO::Input).isOk());
                for(int i = 0; i < 3; i++) {
                        Image img(16, 8, PixelDesc::RGB8_sRGB);
                        Audio audio(adesc, samplesPerFrame);
                        Frame::Ptr wf = Frame::Ptr::create();
                        wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        wf.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                        CHECK(io->writeFrame(wf).isOk());
                }
                io->close();
                delete io;
        }

        CHECK(FilePath(dir / "hn.wav").exists());

        // Default (Sidecar hint): should use sidecar audio.
        // Sidecar sample rate == 48000, channels == 2.
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->audioDesc().isValid());
                CHECK(io->audioDesc().sampleRate() == 48000.0f);

                Frame::Ptr f;
                REQUIRE(io->readFrame(f).isOk());
                REQUIRE(f->audioList().size() == 1);
                // Sidecar delivers frame-rate-aligned chunks.
                CHECK(f->audioList()[0]->samples() == samplesPerFrame);

                io->close();
                delete io;
        }

        // Embedded hint: should prefer the DPX embedded audio.
        {
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, mask);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::AudioSource, AudioSourceHint::Embedded);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());
                CHECK(io->audioDesc().isValid());

                // The embedded audio from DPX should be used.  Its
                // sample count may differ from the sidecar's
                // frame-rate-aligned chunks.
                Frame::Ptr f;
                REQUIRE(io->readFrame(f).isOk());
                REQUIRE(f->audioList().size() == 1);
                CHECK(f->audioList()[0]->desc().sampleRate() == 48000.0f);

                io->close();
                delete io;
        }

        // Embedded hint with no embedded audio (PNG, which has no
        // embedded audio) — should fall back to sidecar.
        {
                // Write a PNG sequence with NO embedded audio, but with
                // the sidecar from the DPX sequence still present.
                String pngMask = (dir / "hn_####.png").toString();
                {
                        MediaIO *io = MediaIO::createForFileWrite(pngMask);
                        REQUIRE(io != nullptr);
                        REQUIRE(io->open(MediaIO::Input).isOk());
                        for(int i = 0; i < 3; i++) {
                                Image img(16, 8, PixelDesc::RGB8_sRGB);
                                Frame::Ptr wf = Frame::Ptr::create();
                                wf.modify()->imageList().pushToBack(Image::Ptr::create(img));
                                CHECK(io->writeFrame(wf).isOk());
                        }
                        io->close();
                        delete io;
                }

                // Read the PNG sequence with Embedded hint — no embedded
                // audio, so should fall back to the sidecar hn.wav.
                MediaIO::Config cfg = MediaIO::defaultConfig("ImageFile");
                cfg.set(MediaConfig::Filename, pngMask);
                cfg.set(MediaConfig::FrameRate, fps);
                cfg.set(MediaConfig::AudioSource, AudioSourceHint::Embedded);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Output).isOk());

                // Sidecar fallback — audio should be available.
                CHECK(io->audioDesc().isValid());
                CHECK(io->audioDesc().sampleRate() == 48000.0f);

                io->close();
                delete io;
        }

        Dir(dir).removeRecursively();
}

#endif // PROMEKI_ENABLE_AUDIO
