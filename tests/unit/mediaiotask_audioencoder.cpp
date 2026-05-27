/**
 * @file      tests/mediaiotask_audioencoder.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the @ref AudioEncoderMediaIO factory + bridge logic and
 * proposeInput / proposeOutput negotiation overrides.  Uses the
 * @c "PassthroughAudio" codec registered statically in
 * tests/unit/audioencoder.cpp — no real audio codec backend is
 * required for these tests to run.
 */

#include <doctest/doctest.h>

#include <promeki/audiocodec.h>
#include <promeki/audiodesc.h>
#include <promeki/audioencodermediaio.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/pcmaudiopayload.h>

using namespace promeki;

namespace {

        AudioCodec lookupPassthroughCodec() {
                auto r = AudioCodec::lookup("PassthroughAudio");
                return isOk(r) ? value(r) : AudioCodec();
        }

        AudioFormat lookupPassthroughCompressedFormat() {
                auto r = AudioFormat::lookup("PassthroughAudioCompressed");
                return isOk(r) ? value(r) : AudioFormat();
        }

        MediaDesc makePcmDesc(float rate, unsigned channels, AudioFormat::ID dt) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                AudioDesc ad(AudioFormat(dt), rate, channels);
                md.audioList().pushToBack(ad);
                return md;
        }

        MediaDesc makeCompressedAudioDesc(float rate, unsigned channels, const AudioFormat &fmt) {
                MediaDesc md;
                md.setFrameRate(FrameRate(FrameRate::FPS_30));
                AudioDesc ad(fmt, rate, channels);
                md.audioList().pushToBack(ad);
                return md;
        }

        PcmAudioPayload::Ptr makePcmPayload(size_t samples,
                                            const AudioDesc &desc = AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2)) {
                const size_t bytes = desc.bufferSize(samples);
                auto         buf = Buffer(bytes);
                buf.fill(static_cast<char>(0x55));
                buf.setSize(bytes);
                BufferView planes;
                planes.pushToBack(buf, 0, bytes);
                return PcmAudioPayload::Ptr::create(desc, samples, planes);
        }

} // namespace

TEST_CASE("AudioEncoderMediaIO: factory is registered") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioEncoder");
        REQUIRE(f != nullptr);
        CHECK(f->canBeTransform());
        CHECK_FALSE(f->canBeSource());
        CHECK_FALSE(f->canBeSink());
}

TEST_CASE("AudioEncoderMediaIO: bridge accepts PCM -> compressed audio") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioEncoder");
        REQUIRE(f != nullptr);

        const AudioCodec  passthrough = lookupPassthroughCodec();
        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(passthrough.isValid());
        REQUIRE(compressed.isValid());
        REQUIRE(passthrough.canEncode());

        const MediaDesc from = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        const MediaDesc to = makeCompressedAudioDesc(48000.0f, 2, compressed);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = f->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "AudioEncoder");
        CHECK(cfg.getAs<AudioCodec>(MediaConfig::AudioCodec) == passthrough);
        // Audio encoding is heavily lossy in our cost model.
        CHECK(cost >= 1000);
}

TEST_CASE("AudioEncoderMediaIO: bridge declines compressed source") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioEncoder");
        REQUIRE(f != nullptr);

        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(compressed.isValid());

        const MediaDesc from = makeCompressedAudioDesc(48000.0f, 2, compressed);
        const MediaDesc to = makeCompressedAudioDesc(48000.0f, 2, compressed);
        CHECK_FALSE(f->bridge(from, to, nullptr, nullptr));
}

TEST_CASE("AudioEncoderMediaIO: bridge declines PCM target") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioEncoder");
        REQUIRE(f != nullptr);

        const MediaDesc from = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        const MediaDesc to = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S24LE);
        CHECK_FALSE(f->bridge(from, to, nullptr, nullptr));
}

TEST_CASE("AudioEncoderMediaIO: bridge declines when video parts differ") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioEncoder");
        REQUIRE(f != nullptr);

        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(compressed.isValid());

        MediaDesc from = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        from.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::RGBA8_sRGB)));

        MediaDesc to = makeCompressedAudioDesc(48000.0f, 2, compressed);
        to.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080), PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709)));

        CHECK_FALSE(f->bridge(from, to, nullptr, nullptr));
}

TEST_CASE("AudioEncoderMediaIO: proposeInput rejects compressed source") {
        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(compressed.isValid());

        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioEncoder");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered = makeCompressedAudioDesc(48000.0f, 2, compressed);
        MediaDesc       preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::NotSupported);

        delete io;
}

TEST_CASE("AudioEncoderMediaIO: proposeOutput emits compressed AudioFormat") {
        const AudioCodec  passthrough = lookupPassthroughCodec();
        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(passthrough.isValid());
        REQUIRE(compressed.isValid());

        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioEncoder");
        cfg.set(MediaConfig::AudioCodec, passthrough);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc requested = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        MediaDesc       achievable;
        CHECK(io->proposeOutput(requested, &achievable) == Error::Ok);
        REQUIRE(!achievable.audioList().isEmpty());
        CHECK(achievable.audioList()[0].format() == compressed);
        // Sample rate and channels flow through unchanged.
        CHECK(achievable.audioList()[0].sampleRate() == 48000.0f);
        CHECK(achievable.audioList()[0].channels() == 2u);

        delete io;
}

TEST_CASE("AudioEncoderMediaIO: end-to-end encode round-trip") {
        const AudioCodec passthrough = lookupPassthroughCodec();
        REQUIRE(passthrough.isValid());

        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioEncoder");
        cfg.set(MediaConfig::AudioCodec, passthrough);
        MediaIO *enc = MediaIO::create(cfg);
        REQUIRE(enc != nullptr);

        // Pre-open hint so the backend's open() builds the right port
        // shapes for both the sink (PCM) and the source (compressed).
        const MediaDesc upstream = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        REQUIRE(enc->setPendingMediaDesc(upstream).isOk());
        REQUIRE(enc->open().wait().isOk());
        REQUIRE(enc->sink(0) != nullptr);
        REQUIRE(enc->source(0) != nullptr);

        // Build a PCM Frame and push it through.
        Frame pcmFrame;
        pcmFrame.addPayload(makePcmPayload(256));
        REQUIRE(enc->sink(0)->writeFrame(pcmFrame).wait().isOk());

        // Drain the encoded Frame.
        MediaIORequest readReq = enc->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());
        const auto *cr = readReq.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        REQUIRE(cr->frame.isValid());

        // The output Frame should carry a compressed audio payload.
        auto auds = cr->frame.audioPayloads();
        REQUIRE_FALSE(auds.isEmpty());
        REQUIRE(auds[0].isValid());
        CHECK(auds[0]->isCompressed());
        CHECK(auds[0]->desc().format().audioCodec() == passthrough);

        REQUIRE(enc->close().wait().isOk());
        delete enc;
}

TEST_CASE("AudioEncoderMediaIO: open without AudioCodec fails") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioEncoder");
        MediaIO *enc = MediaIO::create(cfg);
        REQUIRE(enc != nullptr);

        const MediaDesc upstream = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        REQUIRE(enc->setPendingMediaDesc(upstream).isOk());
        CHECK(enc->open().wait() == Error::InvalidArgument);

        delete enc;
}
