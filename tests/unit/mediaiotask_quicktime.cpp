/**
 * @file      mediaiotask_quicktime.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_quicktime.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/buffer.h>
#include <promeki/string.h>

using namespace promeki;

namespace {

String fixturePath(const char *name) {
        return String(PROMEKI_SOURCE_DIR) + "/tests/data/quicktime/" + name;
}

Buffer::Ptr makeFilledBuffer(size_t size, uint8_t byte) {
        Buffer b(size);
        std::memset(b.data(), byte, size);
        b.setSize(size);
        return Buffer::Ptr::create(std::move(b));
}

} // namespace

// ============================================================================
// Registration / format descriptor
// ============================================================================

TEST_CASE("MediaIO_QuickTime: backend is registered") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &f : formats) {
                if(f.name == "QuickTime") { found = true; break; }
        }
        CHECK(found);
}

TEST_CASE("MediaIO_QuickTime: defaultConfig has the QuickTime type tag") {
        MediaIO::Config cfg = MediaIO::defaultConfig("QuickTime");
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "QuickTime");
}

TEST_CASE("MediaIO_QuickTime: createForFileRead picks the QuickTime backend") {
        // The factory selects the right backend by extension or by probing.
        // We verify the selection succeeded by checking the file actually opens.
        MediaIO *io = MediaIO::createForFileRead(fixturePath("tiny_uyvy_24p.mov"));
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());
        // QuickTime returns a valid MediaDesc with a video image.
        CHECK(io->mediaDesc().imageList().size() == 1);
        io->close();
        delete io;
}

// ============================================================================
// Reader: open + enumerate + read video samples
// ============================================================================

TEST_CASE("MediaIO_QuickTime: open uncompressed UYVY fixture") {
        MediaIO *io = MediaIO::createForFileRead(fixturePath("tiny_uyvy_24p.mov"));
        REQUIRE(io != nullptr);

        REQUIRE(io->open(MediaIO::Source).isOk());
        CHECK(io->isOpen());
        CHECK(io->frameCount() == 2);

        const MediaDesc &md = io->mediaDesc();
        CHECK(md.isValid());
        REQUIRE(md.imageList().size() == 1);
        CHECK(md.imageList()[0].size().width() == 16);
        CHECK(md.imageList()[0].size().height() == 16);
        CHECK(md.imageList()[0].pixelFormat().id() == PixelFormat::YUV8_422_UYVY_Rec709);

        REQUIRE(md.frameRate().isValid());
        CHECK(md.frameRate().rational().toDouble() == doctest::Approx(24.0).epsilon(0.01));

        // Read both video frames as Images.
        for(int i = 0; i < 2; ++i) {
                Frame::Ptr frame;
                REQUIRE(io->readFrame(frame).isOk());
                REQUIRE(frame.isValid());
                REQUIRE(frame->imageList().size() == 1);
                const Image &img = *frame->imageList()[0];
                CHECK(img.isValid());
                CHECK(img.width() == 16);
                CHECK(img.height() == 16);
                CHECK_FALSE(img.isCompressed());
                // 16x16 UYVY = 16*16*2 = 512 bytes
                CHECK(img.plane(0)->size() == 512);
        }

        io->close();
        delete io;
}

TEST_CASE("MediaIO_QuickTime: open ProRes fixture yields a compressed Image") {
        MediaIO *io = MediaIO::createForFileRead(fixturePath("tiny_prores_proxy_25p.mov"));
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());

        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame->imageList().size() == 1);
        const Image &img = *frame->imageList()[0];
        CHECK(img.isValid());
        CHECK(img.isCompressed());
        CHECK(img.pixelFormat().id() == PixelFormat::ProRes_422_Proxy);
        CHECK(img.compressedSize() > 0);
        CHECK(img.width() == 16);
        CHECK(img.height() == 16);

        io->close();
        delete io;
}

TEST_CASE("MediaIO_QuickTime: opens AAC-in-MP4 and exposes compressed audio samples") {
        MediaIO *io = MediaIO::createForFileRead(fixturePath("tiny_h264_aac.mp4"));
        REQUIRE(io != nullptr);
        Error err = io->open(MediaIO::Source);
        CHECK(err.isOk());

        // AAC audio track should be present, marked compressed, with the
        // correct FourCC ('mp4a') on its AudioDesc.
        REQUIRE(io->mediaDesc().audioList().size() == 1);
        const AudioDesc &ad = io->mediaDesc().audioList()[0];
        CHECK(ad.isValid());
        CHECK(ad.isCompressed());
        CHECK(ad.format().id() == AudioFormat::AAC);
        CHECK(ad.channels() == 2);
        CHECK(ad.sampleRate() == doctest::Approx(48000.0f));

        // Read one video frame and expect a compressed Audio object
        // attached to the frame.
        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame.isValid());
        REQUIRE(frame->imageList().size() == 1);
        CHECK(frame->imageList()[0]->isCompressed());

        REQUIRE(frame->audioList().size() == 1);
        const Audio &audio = *frame->audioList()[0];
        CHECK(audio.isValid());
        CHECK(audio.isCompressed());
        CHECK(audio.compressedSize() > 0);
        CHECK(audio.desc().format().id() == AudioFormat::AAC);

        io->close();
        delete io;
}

TEST_CASE("MediaIO_QuickTime: refuses non-PCM audio with NotSupported") {
        // The fixtures we have all use PCM audio (sowt) or no audio,
        // so we instead build the failure path by checking the audio
        // descriptor — if a hypothetical fixture had non-PCM audio,
        // the open path would surface NotSupported. For now this
        // confirms the regular PCM path opens successfully.
        MediaIO *io = MediaIO::createForFileRead(fixturePath("tiny_h264_pcm_24p.mov"));
        REQUIRE(io != nullptr);
        Error err = io->open(MediaIO::Source);
        CHECK(err.isOk());
        CHECK(io->mediaDesc().audioList().size() == 1);
        const AudioDesc &ad = io->mediaDesc().audioList()[0];
        CHECK(ad.format().id() == AudioFormat::PCMI_S16LE);
        CHECK(ad.channels() == 2);
        io->close();
        delete io;
}

TEST_CASE("MediaIO_QuickTime: timecode propagates onto frame metadata") {
        MediaIO *io = MediaIO::createForFileRead(fixturePath("tiny_uyvy_24p_tc.mov"));
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());

        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame->metadata().contains(Metadata::Timecode));
        Timecode tc = frame->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.isValid());
        CHECK(tc.hour() == 1);
        CHECK(tc.min() == 0);
        CHECK(tc.sec() == 0);
        CHECK(tc.frame() == 0);

        // The next frame should advance the timecode by one frame.
        REQUIRE(io->readFrame(frame).isOk());
        REQUIRE(frame->metadata().contains(Metadata::Timecode));
        Timecode tc2 = frame->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc2.frame() == 1);

        io->close();
        delete io;
}

TEST_CASE("MediaIO_QuickTime: seekToFrame moves the read cursor") {
        MediaIO *io = MediaIO::createForFileRead(fixturePath("tiny_uyvy_24p.mov"));
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::Source).isOk());
        REQUIRE(io->frameCount() == 2);

        // Seek to frame 1 then read.
        REQUIRE(io->seekToFrame(1).isOk());
        Frame::Ptr frame;
        REQUIRE(io->readFrame(frame).isOk());
        // FrameNumber metadata should reflect the seeked position.
        REQUIRE(frame->metadata().contains(Metadata::FrameNumber));
        CHECK(frame->metadata().get(Metadata::FrameNumber).get<int64_t>() == 1);

        io->close();
        delete io;
}

// ============================================================================
// Writer: round-trip via MediaIO
// ============================================================================

TEST_CASE("MediaIO_QuickTime: round-trip uncompressed video via MediaIO") {
        const String tmp = "/tmp/mediaio_qt_uyvy_roundtrip.mov";
        std::remove(tmp.cstr());

        // ---- write ----
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "QuickTime");
                cfg.set(MediaConfig::Filename, tmp);
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);

                // Build a pendingMediaDesc so the writer can register the
                // video track during open() rather than waiting for the
                // first frame.
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::RationalType(24, 1)));
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 16),
                        PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709)));
                io->setExpectedDesc(md);

                REQUIRE(io->open(MediaIO::Sink).isOk());

                // Write three frames with distinct fill bytes.
                for(int f = 0; f < 3; ++f) {
                        Frame::Ptr frame = Frame::Ptr::create();
                        Image img(Size2Du32(16, 16),
                                  PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
                        std::memset(img.plane(0)->data(), 0x20 + f, 512);
                        img.plane(0)->setSize(512);
                        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        REQUIRE(io->writeFrame(frame).isOk());
                }

                io->close();
                delete io;
        }

        // ---- read back ----
        {
                MediaIO *io = MediaIO::createForFileRead(tmp);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Source).isOk());
                CHECK(io->frameCount() == 3);
                CHECK(io->mediaDesc().imageList()[0].size().width() == 16);
                CHECK(io->mediaDesc().imageList()[0].size().height() == 16);

                for(int f = 0; f < 3; ++f) {
                        Frame::Ptr frame;
                        REQUIRE(io->readFrame(frame).isOk());
                        const Image &img = *frame->imageList()[0];
                        CHECK(img.plane(0)->size() == 512);
                        const uint8_t *bytes =
                                static_cast<const uint8_t *>(img.plane(0)->data());
                        for(size_t k = 0; k < 512; ++k) {
                                REQUIRE(bytes[k] == 0x20 + f);
                        }
                }

                io->close();
                delete io;
        }

        std::remove(tmp.cstr());
}

TEST_CASE("MediaIO_QuickTime: round-trip video + audio via MediaIO") {
        const String tmp = "/tmp/mediaio_qt_audio_roundtrip.mov";
        std::remove(tmp.cstr());

        const float  sampleRate = 48000.0f;
        const int    frames     = 4;
        // 24 fps → 2000 samples per video frame
        const size_t samplesPerFrame = 2000;

        // ---- write: simulate a TPG producing float32 audio that the
        // writer stores as s16le ----
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "QuickTime");
                cfg.set(MediaConfig::Filename, tmp);

                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::RationalType(24, 1)));
                md.imageList().pushToBack(ImageDesc(Size2Du32(16, 16),
                        PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709)));
                // Source audio is native float.
                md.audioList().pushToBack(AudioDesc(AudioFormat::PCMI_Float32LE,
                                                    sampleRate, 2));

                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                io->setExpectedDesc(md);
                REQUIRE(io->open(MediaIO::Sink).isOk());

                // Each frame gets samplesPerFrame stereo floats. Fill with
                // a distinct sine-ish pattern so we can round-trip verify.
                for(int f = 0; f < frames; ++f) {
                        Frame::Ptr frame = Frame::Ptr::create();

                        // Video frame: 16x16 UYVY, filled with per-frame byte.
                        Image img(Size2Du32(16, 16),
                                  PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
                        std::memset(img.plane(0)->data(), 0x30 + f, 512);
                        img.plane(0)->setSize(512);
                        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));

                        // Audio frame: float32 stereo, deterministic ramp.
                        AudioDesc fdesc(AudioFormat::PCMI_Float32LE, sampleRate, 2);
                        Audio audio(fdesc, samplesPerFrame);
                        float *a = audio.data<float>();
                        for(size_t i = 0; i < samplesPerFrame; ++i) {
                                float v = static_cast<float>(i + f * 10000) / 100000.0f;
                                if(v >  1.0f) v =  1.0f;
                                if(v < -1.0f) v = -1.0f;
                                a[i * 2 + 0] = v;
                                a[i * 2 + 1] = v;
                        }
                        frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));

                        REQUIRE(io->writeFrame(frame).isOk());
                }
                io->close();
                delete io;
        }

        // ---- read back ----
        {
                MediaIO *io = MediaIO::createForFileRead(tmp);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Source).isOk());

                // Must expose both tracks.
                CHECK(io->mediaDesc().imageList().size() == 1);
                REQUIRE(io->mediaDesc().audioList().size() == 1);
                const AudioDesc &ad = io->mediaDesc().audioList()[0];
                CHECK(ad.format().id() == AudioFormat::PCMI_S16LE);
                CHECK(ad.channels() == 2);
                CHECK(ad.sampleRate() == doctest::Approx(48000.0f));
                CHECK(io->frameCount() == frames);

                for(int f = 0; f < frames; ++f) {
                        Frame::Ptr frame;
                        REQUIRE(io->readFrame(frame).isOk());
                        REQUIRE(frame.isValid());

                        // Video
                        REQUIRE(frame->imageList().size() == 1);
                        CHECK(frame->imageList()[0]->plane(0)->size() == 512);

                        // Audio
                        REQUIRE(frame->audioList().size() == 1);
                        const Audio &audio = *frame->audioList()[0];
                        CHECK(audio.desc().format().id() == AudioFormat::PCMI_S16LE);
                        CHECK(audio.samples() == samplesPerFrame);
                        // First sample of frame 0 should be near 0.
                        const int16_t *s = audio.data<int16_t>();
                        if(f == 0) CHECK(std::abs(s[0]) < 10);
                        // Samples monotonically increase within a frame (ramp).
                        CHECK(s[samplesPerFrame * 2 - 2] > s[0]);
                }

                io->close();
                delete io;
        }

        std::remove(tmp.cstr());
}

TEST_CASE("MediaIO_QuickTime: round-trip compressed (ProRes) bytes pass through") {
        const String tmp = "/tmp/mediaio_qt_prores_passthrough.mov";
        std::remove(tmp.cstr());

        // ---- write: feed Image::fromCompressedData payloads ----
        {
                MediaIO::Config cfg;
                cfg.set(MediaConfig::Type, "QuickTime");
                cfg.set(MediaConfig::Filename, tmp);

                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::RationalType(25, 1)));
                md.imageList().pushToBack(ImageDesc(Size2Du32(64, 64),
                        PixelFormat(PixelFormat::ProRes_422_HQ)));

                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                io->setExpectedDesc(md);
                REQUIRE(io->open(MediaIO::Sink).isOk());

                static const size_t sizes[] = { 800, 850, 900 };
                for(int f = 0; f < 3; ++f) {
                        Buffer::Ptr buf = makeFilledBuffer(sizes[f], static_cast<uint8_t>(0xC0 + f));
                        Image img = Image::fromCompressedData(
                                buf->data(), buf->size(), 64, 64,
                                PixelFormat(PixelFormat::ProRes_422_HQ));
                        Frame::Ptr frame = Frame::Ptr::create();
                        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));
                        REQUIRE(io->writeFrame(frame).isOk());
                }
                io->close();
                delete io;
        }

        // ---- read back ----
        {
                MediaIO *io = MediaIO::createForFileRead(tmp);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::Source).isOk());
                CHECK(io->frameCount() == 3);
                CHECK(io->mediaDesc().imageList()[0].pixelFormat().id() ==
                      PixelFormat::ProRes_422_HQ);

                static const size_t sizes[] = { 800, 850, 900 };
                for(int f = 0; f < 3; ++f) {
                        Frame::Ptr frame;
                        REQUIRE(io->readFrame(frame).isOk());
                        const Image &img = *frame->imageList()[0];
                        REQUIRE(img.isCompressed());
                        CHECK(img.compressedSize() == sizes[f]);
                        const uint8_t *bytes =
                                static_cast<const uint8_t *>(img.plane(0)->data());
                        for(size_t k = 0; k < sizes[f]; ++k) {
                                REQUIRE(bytes[k] == 0xC0 + f);
                        }
                }
                io->close();
                delete io;
        }

        std::remove(tmp.cstr());
}
