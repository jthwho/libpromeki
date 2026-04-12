/**
 * @file      tests/mediaiotask_converter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/enums.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_converter.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/audiodesc.h>
#include <promeki/frame.h>
#include <promeki/pixeldesc.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/framerate.h>
#include <promeki/size2d.h>
#include <promeki/metadata.h>

using namespace promeki;

namespace {

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------

/**
 * @brief Builds a synthetic uncompressed frame with a single RGB image.
 *
 * Fills the image with a fixed byte pattern so round-trip tests can
 * spot-check a few pixels.
 */
Frame::Ptr makeRgbFrame(size_t w, size_t h, PixelDesc::ID id, uint8_t fill) {
        Image img(w, h, PixelDesc(id));
        img.fill(static_cast<char>(fill));
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->imageList().pushToBack(Image::Ptr::create(std::move(img)));
        return frame;
}

/**
 * @brief Builds a synthetic audio-only frame with zeroed samples.
 */
Frame::Ptr makeAudioFrame(AudioDesc::DataType dt, float rate,
                          unsigned int channels, size_t samples) {
        AudioDesc desc(dt, rate, channels);
        Audio audio(desc, samples);
        audio.zero();
        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->audioList().pushToBack(Audio::Ptr::create(std::move(audio)));
        return frame;
}

/**
 * @brief Builds a config for a Converter with the given output pixel desc.
 */
MediaIO::Config converterConfig(const PixelDesc &outputPd) {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        cfg.set(MediaConfig::OutputPixelDesc, outputPd);
        return cfg;
}

} // namespace

// ============================================================================
// Registry / metadata
// ============================================================================

TEST_CASE("MediaIOTask_Converter_Registry") {
        const auto &formats = MediaIO::registeredFormats();
        bool found = false;
        for(const auto &desc : formats) {
                if(desc.name == "Converter") {
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

TEST_CASE("MediaIOTask_Converter_DefaultConfig") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        CHECK_FALSE(cfg.isEmpty());
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "Converter");
        CHECK(cfg.getAs<int>(MediaConfig::JpegQuality) == 85);

        // JPEG subsampling now uses the ChromaSubsampling Enum.
        Enum sub = cfg.get(MediaConfig::JpegSubsampling)
                      .asEnum(ChromaSubsampling::Type);
        CHECK(sub == ChromaSubsampling::YUV422);

        // Audio data type now uses the AudioDataType Enum, Invalid =
        // pass-through by default.
        Enum adt = cfg.get(MediaConfig::OutputAudioDataType)
                      .asEnum(AudioDataType::Type);
        CHECK(adt == AudioDataType::Invalid);

        CHECK(cfg.getAs<int>(MediaConfig::Capacity) == 4);
}

// ============================================================================
// Mode validation
// ============================================================================

TEST_CASE("MediaIOTask_Converter_RejectsReaderMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Output).isError());
        CHECK_FALSE(io->isOpen());
        delete io;
}

TEST_CASE("MediaIOTask_Converter_RejectsWriterMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::Input).isError());
        CHECK_FALSE(io->isOpen());
        delete io;
}

TEST_CASE("MediaIOTask_Converter_AcceptsReadWriteMode") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());
        CHECK(io->isOpen());
        CHECK(io->mode() == MediaIO::InputAndOutput);
        io->close();
        delete io;
}

// ============================================================================
// Pass-through (no config set)
// ============================================================================

TEST_CASE("MediaIOTask_Converter_PassThroughVideo") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        // Leave output pixel desc invalid -> pass-through
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr in = makeRgbFrame(16, 16, PixelDesc::RGB8_sRGB, 0x42);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelDesc() == PixelDesc(PixelDesc::RGB8_sRGB));
        CHECK(out->imageList()[0]->width() == 16);

        io->close();
        delete io;
}

// ============================================================================
// CSC (uncompressed -> uncompressed)
// ============================================================================

TEST_CASE("MediaIOTask_Converter_CSC_Rgb8ToRgba8") {
        MediaIO::Config cfg = converterConfig(PixelDesc(PixelDesc::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        // Verify computed output mediaDesc reflects the conversion
        // (Converter was opened with an empty pendingMediaDesc, so the
        // list will be empty; feed a frame and verify the output image.)
        Frame::Ptr in = makeRgbFrame(32, 32, PixelDesc::RGB8_sRGB, 0x10);
        CHECK(io->writeFrame(in).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelDesc() == PixelDesc(PixelDesc::RGBA8_sRGB));
        CHECK(out->imageList()[0]->width() == 32);
        CHECK(out->imageList()[0]->height() == 32);

        io->close();
        delete io;
}

// ============================================================================
// Output mediaDesc is computed from pendingMediaDesc at Open time
// ============================================================================

TEST_CASE("MediaIOTask_Converter_OutputMediaDesc") {
        MediaIO::Config cfg = converterConfig(PixelDesc(PixelDesc::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        // Describe the upstream stage's output so the Converter can
        // compute its own output descriptor.
        MediaDesc pending;
        pending.setFrameRate(FrameRate(FrameRate::FPS_2997));
        pending.imageList().pushToBack(
                ImageDesc(Size2Du32(640, 480), PixelDesc(PixelDesc::RGB8_sRGB)));
        io->setMediaDesc(pending);

        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        MediaDesc vd = io->mediaDesc();
        REQUIRE(vd.imageList().size() == 1);
        CHECK(vd.imageList()[0].pixelDesc() == PixelDesc(PixelDesc::RGBA8_sRGB));
        CHECK(vd.imageList()[0].size() == Size2Du32(640, 480));
        CHECK(vd.frameRate() == FrameRate(FrameRate::FPS_2997));
        CHECK_FALSE(io->canSeek());

        io->close();
        delete io;
}

// ============================================================================
// JPEG encode / decode
// ============================================================================

TEST_CASE("MediaIOTask_Converter_JpegEncodeDecode") {
        const size_t W = 64;
        const size_t H = 48;

        // Encode: RGB8 -> JPEG_RGB8_sRGB
        {
                MediaIO::Config cfg =
                        converterConfig(PixelDesc(PixelDesc::JPEG_RGB8_sRGB));
                MediaIO *io = MediaIO::create(cfg);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

                Frame::Ptr in = makeRgbFrame(W, H, PixelDesc::RGB8_sRGB, 0x80);
                CHECK(io->writeFrame(in).isOk());

                Frame::Ptr out;
                CHECK(io->readFrame(out).isOk());
                REQUIRE(out.isValid());
                REQUIRE(out->imageList().size() == 1);
                const Image &compressed = *out->imageList()[0];
                CHECK(compressed.isCompressed());
                CHECK(compressed.compressedSize() > 0);

                io->close();
                delete io;
        }

        // Decode: JPEG_RGB8_sRGB -> RGB8
        {
                // Start by producing a JPEG via the encoder path again
                Frame::Ptr jpegFrame;
                {
                        MediaIO::Config enc =
                                converterConfig(PixelDesc(PixelDesc::JPEG_RGB8_sRGB));
                        MediaIO *io = MediaIO::create(enc);
                        REQUIRE(io != nullptr);
                        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());
                        Frame::Ptr in = makeRgbFrame(W, H, PixelDesc::RGB8_sRGB, 0x80);
                        REQUIRE(io->writeFrame(in).isOk());
                        REQUIRE(io->readFrame(jpegFrame).isOk());
                        io->close();
                        delete io;
                }
                REQUIRE(jpegFrame.isValid());
                REQUIRE(jpegFrame->imageList().size() == 1);
                CHECK(jpegFrame->imageList()[0]->isCompressed());

                MediaIO::Config dec =
                        converterConfig(PixelDesc(PixelDesc::RGB8_sRGB));
                MediaIO *io = MediaIO::create(dec);
                REQUIRE(io != nullptr);
                REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

                CHECK(io->writeFrame(jpegFrame).isOk());

                Frame::Ptr out;
                CHECK(io->readFrame(out).isOk());
                REQUIRE(out.isValid());
                REQUIRE(out->imageList().size() == 1);
                const Image &decoded = *out->imageList()[0];
                CHECK_FALSE(decoded.isCompressed());
                CHECK(decoded.pixelDesc() == PixelDesc(PixelDesc::RGB8_sRGB));
                CHECK(decoded.width() == W);
                CHECK(decoded.height() == H);

                io->close();
                delete io;
        }
}

// ============================================================================
// Audio sample format conversion
// ============================================================================

TEST_CASE("MediaIOTask_Converter_AudioFormatConversion") {
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        cfg.set(MediaConfig::OutputAudioDataType,
                AudioDataType::PCMI_S16LE);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

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

TEST_CASE("MediaIOTask_Converter_UnknownAudioDataTypeRejected") {
        // An unknown value for the AudioDataType key — sent as a
        // String so the Variant::asEnum path rejects it just like an
        // out-of-list integer would.
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        cfg.set(MediaConfig::OutputAudioDataType,
                String("NotARealFormat"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::InputAndOutput).isError());
        delete io;
}

TEST_CASE("MediaIOTask_Converter_UnknownJpegSubsamplingRejected") {
        // Same idea: an unknown ChromaSubsampling name should fail
        // when asEnum() cannot find a match.
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        cfg.set(MediaConfig::JpegSubsampling, String("YUV777"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        CHECK(io->open(MediaIO::InputAndOutput).isError());
        delete io;
}

// ============================================================================
// Back-pressure: write while queue is full returns TryAgain
// ============================================================================

TEST_CASE("MediaIOTask_Converter_WriteBeyondCapacity") {
        MediaIO::Config cfg = converterConfig(PixelDesc(PixelDesc::RGBA8_sRGB));
        cfg.set(MediaConfig::Capacity, 2);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr f = makeRgbFrame(8, 8, PixelDesc::RGB8_sRGB, 0x22);
        CHECK(io->writeFrame(f).isOk());   // queue depth 1
        CHECK(io->writeFrame(f).isOk());   // queue depth 2 — at capacity
        // Third write exceeds capacity — succeeds (with a warning) so
        // frames are never silently dropped by the converter.  Back-
        // pressure is the caller's responsibility.
        CHECK(io->writeFrame(f).isOk());   // queue depth 3 — beyond capacity

        // All three frames should be readable.
        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        CHECK(io->readFrame(out).isOk());
        CHECK(io->readFrame(out).isOk());

        io->close();
        delete io;
}

// ============================================================================
// writesAccepted / writeDepth / pendingOutput
// ============================================================================

TEST_CASE("MediaIOTask_Converter_WritesAccepted") {
        MediaIO::Config cfg = converterConfig(PixelDesc(PixelDesc::RGBA8_sRGB));
        cfg.set(MediaConfig::Capacity, 3);

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        // writeDepth reflects the configured capacity.
        CHECK(io->writeDepth() == 3);

        // Empty converter — full capacity available.
        CHECK(io->writesAccepted() == 3);

        // Each blocking write lands immediately; pendingWrites goes
        // back to 0, but the frame sits in the output queue, so
        // writesAccepted decreases by one per write.
        Frame::Ptr f = makeRgbFrame(8, 8, PixelDesc::RGB8_sRGB, 0x33);
        CHECK(io->writeFrame(f).isOk());
        CHECK(io->writesAccepted() == 2);

        CHECK(io->writeFrame(f).isOk());
        CHECK(io->writesAccepted() == 1);

        CHECK(io->writeFrame(f).isOk());
        CHECK(io->writesAccepted() == 0);   // at capacity

        // Reading a frame frees a slot.
        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        CHECK(io->writesAccepted() == 1);

        CHECK(io->readFrame(out).isOk());
        CHECK(io->writesAccepted() == 2);

        CHECK(io->readFrame(out).isOk());
        CHECK(io->writesAccepted() == 3);   // fully drained

        io->close();
        delete io;
}

// ============================================================================
// Back-pressure: read while queue empty returns TryAgain
// ============================================================================

TEST_CASE("MediaIOTask_Converter_ReadEmptyQueueTryAgain") {
        MediaIO::Config cfg = converterConfig(PixelDesc(PixelDesc::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr out;
        CHECK(io->readFrame(out, /*block=*/false) == Error::TryAgain);

        io->close();
        delete io;
}

// ============================================================================
// Stats
// ============================================================================

TEST_CASE("MediaIOTask_Converter_Stats") {
        MediaIO::Config cfg = converterConfig(PixelDesc(PixelDesc::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr f = makeRgbFrame(16, 16, PixelDesc::RGB8_sRGB, 0x55);
        CHECK(io->writeFrame(f).isOk());
        CHECK(io->writeFrame(f).isOk());

        MediaIOStats stats = io->stats();
        CHECK(stats.getAs<int64_t>(MediaIOTask_Converter::StatsFramesConverted) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::QueueDepth) == 2);
        CHECK(stats.getAs<int64_t>(MediaIOStats::QueueCapacity) == 4);
        // Standard base-class keys are now authoritative for drops
        // and rate; they must be present even without a running
        // telemetry pump.
        CHECK(stats.getAs<int64_t>(MediaIOStats::FramesDropped) == 0);

        io->close();
        delete io;
}

// ============================================================================
// Reopen after close
// ============================================================================

TEST_CASE("MediaIOTask_Converter_ReopenAfterClose") {
        MediaIO::Config cfg = converterConfig(PixelDesc(PixelDesc::RGBA8_sRGB));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        // Write a frame into the first session and leave it pending in
        // the Converter's output FIFO.
        Frame::Ptr f = makeRgbFrame(8, 8, PixelDesc::RGB8_sRGB, 0xAA);
        CHECK(io->writeFrame(f).isOk());
        io->close();
        CHECK_FALSE(io->isOpen());
        // After close, all framework counters are zero.
        CHECK(io->readyReads() == 0);
        CHECK(io->pendingReads() == 0);
        CHECK(io->pendingWrites() == 0);

        // The backend must fully reset its output queue on close — a
        // fresh open must not surface the frame that was queued during
        // the previous session.
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());
        CHECK(io->writeFrame(f).isOk());
        Frame::Ptr out;
        CHECK(io->readFrame(out).isOk());
        REQUIRE(out.isValid());
        REQUIRE(out->imageList().size() == 1);
        CHECK(out->imageList()[0]->pixelDesc() == PixelDesc(PixelDesc::RGBA8_sRGB));

        io->close();
        delete io;
}

// ============================================================================
// pendingWrites() tracks in-flight non-blocking writes
// ============================================================================

TEST_CASE("MediaIOTask_Converter_PendingWritesNonBlocking") {
        MediaIO::Config cfg = converterConfig(PixelDesc(PixelDesc::RGBA8_sRGB));
        cfg.set(MediaConfig::Capacity, 8);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open(MediaIO::InputAndOutput).isOk());

        Frame::Ptr f = makeRgbFrame(16, 16, PixelDesc::RGB8_sRGB, 0x33);

        // A blocking write returns only after the strand task has
        // finished, so pendingWrites() is 0 at the observation point.
        CHECK(io->pendingWrites() == 0);
        CHECK(io->writeFrame(f, /*block=*/true).isOk());
        CHECK(io->pendingWrites() == 0);

        io->close();
        delete io;
}

