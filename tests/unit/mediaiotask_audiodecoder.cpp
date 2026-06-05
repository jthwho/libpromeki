/**
 * @file      tests/mediaiotask_audiodecoder.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Exercises the @ref AudioDecoderMediaIO factory + bridge logic and
 * proposeInput / proposeOutput negotiation overrides.  Uses the
 * @c "PassthroughAudio" codec registered statically in
 * tests/unit/audioencoder.cpp.
 */

#include <doctest/doctest.h>

#include <promeki/audiocodec.h>
#include <promeki/audiodecodermediaio.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/buffer.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/enums_audio.h>
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

        CompressedAudioPayload::Ptr makeCompressedPayload(const AudioFormat &fmt, size_t samples) {
                AudioDesc    desc(fmt, 48000.0f, 2);
                const size_t bytes = samples * 2 * 2; // 2 bytes/sample * 2 channels — passthrough copies raw PCM
                auto         buf = Buffer(bytes);
                buf.fill(static_cast<char>(0x77));
                buf.setSize(bytes);
                return CompressedAudioPayload::Ptr::create(desc, buf, samples);
        }

} // namespace

TEST_CASE("AudioDecoderMediaIO: factory is registered") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioDecoder");
        REQUIRE(f != nullptr);
        CHECK(f->canBeTransform());
}

TEST_CASE("AudioDecoderMediaIO: bridge accepts compressed -> PCM") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioDecoder");
        REQUIRE(f != nullptr);

        const AudioCodec  passthrough = lookupPassthroughCodec();
        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(passthrough.isValid());
        REQUIRE(compressed.isValid());
        REQUIRE(passthrough.canDecode());

        const MediaDesc from = makeCompressedAudioDesc(48000.0f, 2, compressed);
        const MediaDesc to = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);

        MediaIO::Config cfg;
        int             cost = -1;
        const bool      applies = f->bridge(from, to, &cfg, &cost);
        CHECK(applies);
        CHECK(cfg.getAs<String>(MediaConfig::Type) == "AudioDecoder");
        CHECK(cfg.getAs<AudioCodec>(MediaConfig::AudioCodec) == passthrough);
        Error err;
        Enum  dtEnum = cfg.get(MediaConfig::OutputAudioDataType).asEnum(AudioDataType::Type, &err);
        CHECK(err.isOk());
        CHECK(static_cast<AudioFormat::ID>(dtEnum.value()) == AudioFormat::PCMI_S16LE);
        // Decoding is a precision-preserving hop.
        CHECK(cost < 100);
}

TEST_CASE("AudioDecoderMediaIO: bridge declines PCM source") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioDecoder");
        REQUIRE(f != nullptr);

        const MediaDesc from = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S24LE);
        const MediaDesc to = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        CHECK_FALSE(f->bridge(from, to, nullptr, nullptr));
}

TEST_CASE("AudioDecoderMediaIO: bridge declines compressed target") {
        const MediaIOFactory *f = MediaIOFactory::findByName("AudioDecoder");
        REQUIRE(f != nullptr);

        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(compressed.isValid());

        const MediaDesc from = makeCompressedAudioDesc(48000.0f, 2, compressed);
        const MediaDesc to = makeCompressedAudioDesc(48000.0f, 2, compressed);
        CHECK_FALSE(f->bridge(from, to, nullptr, nullptr));
}

TEST_CASE("AudioDecoderMediaIO: proposeInput rejects PCM source") {
        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioDecoder");
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc offered = makePcmDesc(48000.0f, 2, AudioFormat::PCMI_S16LE);
        MediaDesc       preferred;
        CHECK(io->proposeInput(offered, &preferred) == Error::NotSupported);

        delete io;
}

TEST_CASE("AudioDecoderMediaIO: proposeOutput projects to PCM AudioFormat") {
        const AudioCodec  passthrough = lookupPassthroughCodec();
        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(passthrough.isValid());
        REQUIRE(compressed.isValid());

        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioDecoder");
        cfg.set(MediaConfig::AudioCodec, passthrough);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        const MediaDesc requested = makeCompressedAudioDesc(48000.0f, 2, compressed);
        MediaDesc       achievable;
        CHECK(io->proposeOutput(requested, &achievable) == Error::Ok);
        REQUIRE(!achievable.audioList().isEmpty());
        // The default supported output of the Passthrough decoder is
        // PCMI_S16LE (see audioencoder.cpp test wiring).
        CHECK_FALSE(achievable.audioList()[0].format().isCompressed());

        delete io;
}

TEST_CASE("AudioDecoderMediaIO: end-to-end decode round-trip") {
        const AudioCodec  passthrough = lookupPassthroughCodec();
        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(passthrough.isValid());
        REQUIRE(compressed.isValid());

        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioDecoder");
        cfg.set(MediaConfig::AudioCodec, passthrough);
        cfg.set(MediaConfig::OutputAudioDataType, AudioDataType(AudioDataType::PCMI_S16LE));
        cfg.set(MediaConfig::AudioRate, 48000.0f);
        cfg.set(MediaConfig::AudioChannels, int32_t(2));
        MediaIO *dec = MediaIO::create(cfg);
        REQUIRE(dec != nullptr);

        const MediaDesc upstream = makeCompressedAudioDesc(48000.0f, 2, compressed);
        REQUIRE(dec->setPendingMediaDesc(upstream).isOk());
        REQUIRE(dec->open().wait().isOk());
        REQUIRE(dec->sink(0) != nullptr);
        REQUIRE(dec->source(0) != nullptr);

        // Feed a compressed packet through.
        Frame in;
        in.addPayload(makeCompressedPayload(compressed, 256));
        REQUIRE(dec->sink(0)->writeFrame(in).wait().isOk());

        MediaIORequest readReq = dec->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());
        const auto *cr = readReq.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        REQUIRE(cr->frame.isValid());

        auto auds = cr->frame.audioPayloads();
        REQUIRE_FALSE(auds.isEmpty());
        REQUIRE(auds[0].isValid());
        CHECK_FALSE(auds[0]->isCompressed());

        REQUIRE(dec->close().wait().isOk());
        delete dec;
}

TEST_CASE("AudioDecoderMediaIO: coerces decoded PCM to advertised OutputAudioDataType") {
        // The PassthroughAudio decoder always emits PCMI_S16LE (its native
        // format), like fdk-aac.  When the caller asks for a different output
        // type the wrapper must convert so the frame it hands downstream
        // matches what its source desc advertised — otherwise strict sinks
        // (e.g. AudioFile) reject the format mismatch.
        const AudioCodec  passthrough = lookupPassthroughCodec();
        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(passthrough.isValid());
        REQUIRE(compressed.isValid());

        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioDecoder");
        cfg.set(MediaConfig::AudioCodec, passthrough);
        // Ask for S32LE even though the decoder natively emits S16LE.
        cfg.set(MediaConfig::OutputAudioDataType, AudioDataType(AudioDataType::PCMI_S32LE));
        cfg.set(MediaConfig::AudioRate, 48000.0f);
        cfg.set(MediaConfig::AudioChannels, int32_t(2));
        MediaIO *dec = MediaIO::create(cfg);
        REQUIRE(dec != nullptr);

        const MediaDesc upstream = makeCompressedAudioDesc(48000.0f, 2, compressed);
        REQUIRE(dec->setPendingMediaDesc(upstream).isOk());
        REQUIRE(dec->open().wait().isOk());

        // The advertised source desc must promise S32LE.
        REQUIRE(dec->source(0) != nullptr);
        const MediaDesc &srcDesc = dec->source(0)->mediaDesc();
        REQUIRE_FALSE(srcDesc.audioList().isEmpty());
        CHECK(srcDesc.audioList()[0].format().id() == AudioFormat::PCMI_S32LE);

        Frame in;
        in.addPayload(makeCompressedPayload(compressed, 256));
        REQUIRE(dec->sink(0)->writeFrame(in).wait().isOk());

        MediaIORequest readReq = dec->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());
        const auto *cr = readReq.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        REQUIRE(cr->frame.isValid());

        auto auds = cr->frame.audioPayloads();
        REQUIRE_FALSE(auds.isEmpty());
        REQUIRE(auds[0].isValid());
        // The delivered payload must be S32LE, not the decoder's native S16LE.
        CHECK(auds[0]->desc().format().id() == AudioFormat::PCMI_S32LE);

        REQUIRE(dec->close().wait().isOk());
        delete dec;
}

TEST_CASE("AudioDecoderMediaIO: auto-detects codec from first packet") {
        const AudioCodec  passthrough = lookupPassthroughCodec();
        const AudioFormat compressed = lookupPassthroughCompressedFormat();
        REQUIRE(passthrough.isValid());
        REQUIRE(compressed.isValid());

        MediaIO::Config cfg;
        cfg.set(MediaConfig::Type, "AudioDecoder");
        // Intentionally omit AudioCodec — should auto-detect.
        cfg.set(MediaConfig::OutputAudioDataType, AudioDataType(AudioDataType::PCMI_S16LE));
        cfg.set(MediaConfig::AudioRate, 48000.0f);
        cfg.set(MediaConfig::AudioChannels, int32_t(2));
        MediaIO *dec = MediaIO::create(cfg);
        REQUIRE(dec != nullptr);

        const MediaDesc upstream = makeCompressedAudioDesc(48000.0f, 2, compressed);
        REQUIRE(dec->setPendingMediaDesc(upstream).isOk());
        REQUIRE(dec->open().wait().isOk());

        Frame in;
        in.addPayload(makeCompressedPayload(compressed, 128));
        REQUIRE(dec->sink(0)->writeFrame(in).wait().isOk());

        MediaIORequest readReq = dec->source(0)->readFrame();
        REQUIRE(readReq.wait().isOk());
        const auto *cr = readReq.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        auto auds = cr->frame.audioPayloads();
        REQUIRE_FALSE(auds.isEmpty());
        CHECK_FALSE(auds[0]->isCompressed());

        REQUIRE(dec->close().wait().isOk());
        delete dec;
}
