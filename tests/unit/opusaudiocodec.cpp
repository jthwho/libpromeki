/**
 * @file      tests/opusaudiocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Round-trip tests for the native libopus AudioEncoder / AudioDecoder
 * backend.  Uses a low-frequency sine wave so we can compare the
 * decoded samples against the original signal — Opus is a lossy
 * codec, so we use a generous RMS-error tolerance rather than
 * bit-exact comparison.
 */

#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <vector>
#include <promeki/audiocodec.h>
#include <promeki/audiodecoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioencoder.h>
#include <promeki/buffer.h>
#include <promeki/enums.h>
#include <promeki/mediaconfig.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/pcmaudiopayload.h>

using namespace promeki;

namespace {

        // Generates an interleaved-stereo PCMI_S16LE PcmAudioPayload
        // containing a 440 Hz sine wave on both channels at unity-ish amplitude
        // (~-3 dBFS).
        PcmAudioPayload::Ptr makeSineFramePayloadS16(size_t samplesPerChannel, float sampleRate, unsigned int channels,
                                                     float freqHz, double phase) {
                AudioDesc    desc(AudioFormat::PCMI_S16LE, sampleRate, channels);
                const size_t bytes = desc.bufferSize(samplesPerChannel);
                auto         buf = Buffer::Ptr::create(bytes);
                buf.modify()->setSize(bytes);
                auto *p = static_cast<int16_t *>(buf.modify()->data());
                for (size_t i = 0; i < samplesPerChannel; ++i) {
                        double  t = static_cast<double>(i) / sampleRate;
                        double  s = std::sin(2.0 * M_PI * freqHz * t + phase) * 0.7;
                        int16_t v = static_cast<int16_t>(s * 32767.0);
                        for (unsigned int c = 0; c < channels; ++c) {
                                *p++ = v;
                        }
                }
                BufferView planes;
                planes.pushToBack(buf, 0, bytes);
                return PcmAudioPayload::Ptr::create(desc, samplesPerChannel, planes);
        }

        // Computes RMS error between two interleaved s16 streams of equal
        // length, normalised to the [-1, 1] range so the threshold is unit-
        // independent.
        double rmsErrorS16(const int16_t *a, const int16_t *b, size_t n) {
                double sumSq = 0.0;
                for (size_t i = 0; i < n; ++i) {
                        double diff = (static_cast<double>(a[i]) - b[i]) / 32768.0;
                        sumSq += diff * diff;
                }
                return std::sqrt(sumSq / static_cast<double>(n));
        }

        AudioEncoder *makeOpusEncoder(const MediaConfig *cfg = nullptr) {
                AudioCodec codec(AudioCodec::Opus);
                auto       res = codec.createEncoder(cfg);
                return isOk(res) ? value(res) : nullptr;
        }

        AudioDecoder *makeOpusDecoder(const MediaConfig *cfg = nullptr) {
                AudioCodec codec(AudioCodec::Opus);
                auto       res = codec.createDecoder(cfg);
                return isOk(res) ? value(res) : nullptr;
        }

        // Finds the AudioFormat ID for the compressed Opus format (registered
        // by the Opus backend at static-init time) so test callsites can build
        // CompressedAudioPayloads with the right descriptor.
        AudioFormat opusCompressedFormat() {
                // The Opus backend registers a compressed AudioFormat named
                // "Opus" whose audioCodec() is AudioCodec::Opus.
                for (AudioFormat::ID id : AudioFormat::registeredIDs()) {
                        AudioFormat f(id);
                        if (f.isCompressed() && f.audioCodec().id() == AudioCodec::Opus) return f;
                }
                return AudioFormat();
        }

} // namespace

TEST_CASE("Opus: codec metadata advertises the libopus restrictions") {
        AudioCodec codec(AudioCodec::Opus);
        REQUIRE(codec.isValid());
        CHECK(codec.canEncode()); // Native backend registered at static init
        CHECK(codec.canDecode());

        // Sanity-check the metadata that opusaudiocodec.cpp does NOT
        // re-register — these come from the well-known Data record in
        // src/core/audiocodec.cpp and should match libopus's
        // documented restrictions.
        CHECK(codec.supportedSampleRates().contains(48000.0f));
        CHECK(codec.supportedChannelCounts().contains(2));
}

TEST_CASE("Opus: encoder/decoder round-trip preserves a sine wave within tolerance") {
        const float        sr = 48000.0f;
        const unsigned int ch = 2;
        const float        freq = 440.0f;
        const size_t       chunk = 960; // 20 ms @ 48 kHz
        const size_t       chunks = 25; // 0.5 s of audio

        AudioEncoder *enc = makeOpusEncoder();
        AudioDecoder *dec = makeOpusDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        AudioCodec codec(AudioCodec::Opus);

        // Encoder: 64 kbit/s, audio mode, 20 ms frames.
        MediaConfig encCfg;
        encCfg.set(MediaConfig::BitrateKbps, int32_t(64));
        encCfg.set(MediaConfig::OpusApplication, OpusApplication::Audio);
        encCfg.set(MediaConfig::OpusFrameSizeMs, 20.0f);
        enc->configure(encCfg);

        // Decoder: 48 kHz / 2 ch matches the encoder.
        MediaConfig decCfg;
        decCfg.set(MediaConfig::AudioRate, sr);
        decCfg.set(MediaConfig::AudioChannels, int32_t(ch));
        dec->configure(decCfg);

        // Build the original signal as a single big buffer so we can
        // compare against the decoded output later.
        std::vector<int16_t> original;
        original.reserve(chunk * chunks * ch);

        for (size_t i = 0; i < chunks; ++i) {
                auto        frame = makeSineFramePayloadS16(chunk, sr, ch, freq, i * chunk * 2.0 * M_PI * freq / sr);
                const auto *p = reinterpret_cast<const int16_t *>(frame->plane(0).data());
                original.insert(original.end(), p, p + chunk * ch);
                CHECK(enc->submitPayload(frame) == promeki::Error::Ok);
                while (auto pkt = enc->receiveCompressedPayload()) {
                        CHECK(pkt->desc().format().audioCodec().id() == codec.id());
                        CHECK(dec->submitPayload(pkt) == promeki::Error::Ok);
                }
        }

        // Drain the decoder and stitch the output together.
        std::vector<int16_t> decoded;
        while (true) {
                PcmAudioPayload::Ptr out = dec->receiveAudioPayload();
                if (!out.isValid()) break;
                const auto *p = reinterpret_cast<const int16_t *>(out->plane(0).data());
                decoded.insert(decoded.end(), p, p + out->sampleCount() * ch);
        }

        REQUIRE(!decoded.empty());

        // Opus has a small algorithmic delay (a few hundred samples
        // depending on application mode); compare the overlapping
        // region rather than expecting bit-exact frame counts.
        size_t cmp = std::min(original.size(), decoded.size());
        REQUIRE(cmp >= chunk * ch * 5);
        // Skip the first 20 ms to step past Opus's initial silence
        // priming so we're comparing real signal.
        constexpr size_t skipFrames = 960 * 2;
        const int16_t   *origFromSkip = original.data() + skipFrames;
        // Find the best-matching alignment within +/-2880 samples
        // (60 ms) of the natural delay so the RMS comparison isn't
        // foiled by Opus's encoder-side look-ahead.  The test signal
        // is a steady tone so the cross-correlation peak is sharp.
        constexpr size_t searchWindow = 2880 * 2;
        size_t           cmpLen = std::min({
                static_cast<size_t>(48000), // 1 second window
                cmp - skipFrames - searchWindow,
                decoded.size() - searchWindow,
        });
        double           bestErr = 1.0;
        for (size_t shift = 0; shift < searchWindow; shift += 2) {
                double err = rmsErrorS16(origFromSkip, decoded.data() + shift, cmpLen);
                if (err < bestErr) bestErr = err;
        }

        // Empirically a 64 kbit/s Opus encode of a 440 Hz tone with
        // proper alignment lands well below RMS 0.05; we set the
        // threshold higher to avoid platform / version flakiness.
        CHECK(bestErr < 0.10);

        delete enc;
        delete dec;
}

TEST_CASE("Opus: encoder rejects unsupported sample rates") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        // 44.1 kHz is not in libopus's allowed set (8/12/16/24/48 kHz).
        AudioDesc    badDesc(AudioFormat::PCMI_S16LE, 44100.0f, 2);
        const size_t bytes = badDesc.bufferSize(960);
        auto         buf = Buffer::Ptr::create(bytes);
        buf.modify()->setSize(bytes);
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        auto bad = PcmAudioPayload::Ptr::create(badDesc, 960, planes);

        CHECK(bad->isValid());
        promeki::Error err = enc->submitPayload(bad);
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());

        delete enc;
}

TEST_CASE("Opus: encoder flush emits a final EndOfStream packet") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        // Submit a single 20 ms frame, drain, then flush.
        auto frame = makeSineFramePayloadS16(960, 48000.0f, 2, 440.0f, 0.0);
        REQUIRE(enc->submitPayload(frame) == promeki::Error::Ok);
        auto pkt = enc->receiveCompressedPayload();
        REQUIRE(pkt);
        CHECK_FALSE(pkt->isEndOfStream());

        CHECK(enc->flush() == promeki::Error::Ok);
        auto eos = enc->receiveCompressedPayload();
        REQUIRE(eos);
        CHECK(eos->isEndOfStream());
        CHECK_FALSE(enc->receiveCompressedPayload());
        delete enc;
}

TEST_CASE("Opus: the Native backend is reachable through AudioCodec") {
        // The Native backend must be registered for both encoder and
        // decoder sides.  A CodecBackend override that pins "Native"
        // must resolve to the same backend the default selector would
        // choose, and the encoder/decoder both carry that pinned
        // backend through their codec() wrapper.
        auto bk = AudioCodec::lookupBackend("Native");
        REQUIRE(isOk(bk));
        const AudioCodec::Backend nativeBackend = value(bk);

        AudioCodec codec(AudioCodec::Opus);
        CHECK(codec.availableEncoderBackends().contains(nativeBackend));
        CHECK(codec.availableDecoderBackends().contains(nativeBackend));

        MediaConfig cfg;
        cfg.set(MediaConfig::CodecBackend, String("Native"));

        AudioEncoder *encA = makeOpusEncoder(&cfg);
        AudioEncoder *encB = makeOpusEncoder();
        REQUIRE(encA != nullptr);
        REQUIRE(encB != nullptr);
        CHECK(encA->codec().backend() == nativeBackend);
        CHECK(encA->codec().name() == "Opus");
        CHECK(encA->codec().name() == encB->codec().name());
        delete encA;
        delete encB;

        AudioDecoder *decA = makeOpusDecoder(&cfg);
        REQUIRE(decA != nullptr);
        CHECK(decA->codec().backend() == nativeBackend);
        CHECK(decA->codec().name() == "Opus");
        delete decA;
}

TEST_CASE("Opus: AudioCodec::fromString parses Opus:Native") {
        auto res = AudioCodec::fromString("Opus:Native");
        REQUIRE(isOk(res));
        AudioCodec codec = value(res);
        CHECK(codec.id() == AudioCodec::Opus);
        CHECK(codec.backend().isValid());
        CHECK(codec.toString() == "Opus:Native");
}

TEST_CASE("Opus: encoder rejects unsupported channel count") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        // Opus encoder accepts 1 or 2 channels only — feed it 6.
        AudioDesc    badDesc(AudioFormat::PCMI_S16LE, 48000.0f, 6);
        const size_t bytes = badDesc.bufferSize(960);
        auto         buf = Buffer::Ptr::create(bytes);
        buf.modify()->setSize(bytes);
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        auto  bad = PcmAudioPayload::Ptr::create(badDesc, 960, planes);
        Error err = enc->submitPayload(bad);
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("Opus: encoder rejects null payload") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);
        Error err = enc->submitPayload(PcmAudioPayload::Ptr());
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("Opus: encoder rejects unsupported frame size") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        // 7 ms is not in libopus's allowed set.
        MediaConfig cfg;
        cfg.set(MediaConfig::OpusFrameSizeMs, 7.0f);
        enc->configure(cfg);

        auto  frame = makeSineFramePayloadS16(480, 48000.0f, 2, 440.0f, 0.0);
        Error err = enc->submitPayload(frame);
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("Opus: encoder reset clears state and the next frame is encodable again") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        auto frame = makeSineFramePayloadS16(960, 48000.0f, 2, 440.0f, 0.0);
        REQUIRE(enc->submitPayload(frame) == promeki::Error::Ok);
        REQUIRE(enc->receiveCompressedPayload());
        // Flush + reset should leave the encoder ready to use with no
        // EOS in flight.
        REQUIRE(enc->flush() == promeki::Error::Ok);
        REQUIRE(enc->reset() == promeki::Error::Ok);

        // After reset, the next submit/receive cycle must succeed and
        // not be polluted by the prior flush state.
        REQUIRE(enc->submitPayload(frame) == promeki::Error::Ok);
        auto pkt = enc->receiveCompressedPayload();
        REQUIRE(pkt);
        CHECK_FALSE(pkt->isEndOfStream());
        delete enc;
}

TEST_CASE("Opus: requestKeyframe is a safe no-op for the audio encoder") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);
        // The Opus encoder doesn't override requestKeyframe(), so this
        // hits the AudioEncoder base default.  Probe that it doesn't
        // crash and doesn't latch an error.
        enc->requestKeyframe();
        CHECK(enc->lastError() == promeki::Error::Ok);
        delete enc;
}

TEST_CASE("Opus: decoder rejects null and empty packets") {
        AudioDecoder *dec = makeOpusDecoder();
        REQUIRE(dec != nullptr);

        Error err = dec->submitPayload(CompressedAudioPayload::Ptr());
        CHECK(err == promeki::Error::Invalid);

        // Valid Ptr but empty buffer — decoder ensureDecoder() runs first,
        // then bails on the empty buffer.
        AudioFormat opusFmt = opusCompressedFormat();
        REQUIRE(opusFmt.isValid());
        AudioDesc   cdesc(opusFmt, 48000.0f, 2);
        Buffer::Ptr empty = Buffer::Ptr::create(0);
        auto        pkt = CompressedAudioPayload::Ptr::create(cdesc, BufferView(empty, 0, 0));
        Error       err2 = dec->submitPayload(pkt);
        CHECK(err2 == promeki::Error::Invalid);
        CHECK_FALSE(dec->lastErrorMessage().isEmpty());
        delete dec;
}

TEST_CASE("Opus: decoder rejects unsupported sample rate") {
        AudioDecoder *dec = makeOpusDecoder();
        REQUIRE(dec != nullptr);

        // 44.1 kHz is not in libopus's allowed output set.
        MediaConfig cfg;
        cfg.set(MediaConfig::AudioRate, 44100.0f);
        cfg.set(MediaConfig::AudioChannels, int32_t(2));
        dec->configure(cfg);

        AudioFormat opusFmt = opusCompressedFormat();
        REQUIRE(opusFmt.isValid());
        AudioDesc   cdesc(opusFmt, 44100.0f, 2);
        Buffer::Ptr buf = Buffer::Ptr::create(8);
        buf.modify()->fill(0x00);
        buf.modify()->setSize(8);
        auto  pkt = CompressedAudioPayload::Ptr::create(cdesc, BufferView(buf, 0, 8));
        Error err = dec->submitPayload(pkt);
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(dec->lastErrorMessage().isEmpty());
        delete dec;
}

TEST_CASE("Opus: decoder rejects unsupported channel count") {
        AudioDecoder *dec = makeOpusDecoder();
        REQUIRE(dec != nullptr);

        // Opus decoder supports 1 or 2 channels; ask for 6.
        MediaConfig cfg;
        cfg.set(MediaConfig::AudioRate, 48000.0f);
        cfg.set(MediaConfig::AudioChannels, int32_t(6));
        dec->configure(cfg);

        AudioFormat opusFmt = opusCompressedFormat();
        REQUIRE(opusFmt.isValid());
        AudioDesc   cdesc(opusFmt, 48000.0f, 6);
        Buffer::Ptr buf = Buffer::Ptr::create(8);
        buf.modify()->fill(0x00);
        buf.modify()->setSize(8);
        auto  pkt = CompressedAudioPayload::Ptr::create(cdesc, BufferView(buf, 0, 8));
        Error err = dec->submitPayload(pkt);
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(dec->lastErrorMessage().isEmpty());
        delete dec;
}

TEST_CASE("Opus: decoder reset is a no-op when no decoder is initialised yet") {
        AudioDecoder *dec = makeOpusDecoder();
        REQUIRE(dec != nullptr);
        // Neither configure() nor a successful submitPayload() has run,
        // so the lazy decoder hasn't been created yet — reset() should
        // still succeed cleanly.
        CHECK(dec->reset() == promeki::Error::Ok);
        // Same goes for flush().
        CHECK(dec->flush() == promeki::Error::Ok);
        delete dec;
}

TEST_CASE("Opus: encoder Float32LE input path") {
        AudioEncoder *enc = makeOpusEncoder();
        AudioDecoder *dec = makeOpusDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        const float        sr = 48000.0f;
        const unsigned int ch = 2;
        const size_t       chunk = 960;

        MediaConfig encCfg;
        encCfg.set(MediaConfig::OpusFrameSizeMs, 20.0f);
        enc->configure(encCfg);

        MediaConfig decCfg;
        decCfg.set(MediaConfig::AudioRate, sr);
        decCfg.set(MediaConfig::AudioChannels, int32_t(ch));
        dec->configure(decCfg);

        // Build a Float32LE payload of zeros (silence).
        AudioDesc    desc(AudioFormat::PCMI_Float32LE, sr, ch);
        const size_t bytes = desc.bufferSize(chunk);
        auto         buf = Buffer::Ptr::create(bytes);
        buf.modify()->setSize(bytes);
        std::memset(buf.modify()->data(), 0, bytes);
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        auto frame = PcmAudioPayload::Ptr::create(desc, chunk, planes);
        REQUIRE(frame->isValid());

        Error err = enc->submitPayload(frame);
        CHECK(err == promeki::Error::Ok);
        // The encoder must produce at least one packet for the full
        // 20ms / 960-sample frame.
        auto pkt = enc->receiveCompressedPayload();
        REQUIRE(pkt);
        CHECK(pkt->plane(0).size() > 0);
        // Decode round-trip should succeed.
        CHECK(dec->submitPayload(pkt) == promeki::Error::Ok);
        PcmAudioPayload::Ptr out = dec->receiveAudioPayload();
        REQUIRE(out.isValid());
        delete enc;
        delete dec;
}

TEST_CASE("Opus: configure with OpusApplication::Voip is accepted") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);
        MediaConfig cfg;
        cfg.set(MediaConfig::OpusApplication, OpusApplication::Voip);
        cfg.set(MediaConfig::OpusFrameSizeMs, 20.0f);
        enc->configure(cfg);

        // Submit one frame to force ensureEncoder() (which reads
        // applicationFromEnum) to actually run — mono silence payload.
        AudioDesc    monoDesc(AudioFormat::PCMI_S16LE, 48000.0f, 1);
        const size_t bytes = monoDesc.bufferSize(960);
        auto         buf = Buffer::Ptr::create(bytes);
        buf.modify()->setSize(bytes);
        std::memset(buf.modify()->data(), 0, bytes);
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        auto  mono = PcmAudioPayload::Ptr::create(monoDesc, 960, planes);
        Error err = enc->submitPayload(mono);
        CHECK(err == promeki::Error::Ok);
        delete enc;
}

TEST_CASE("Opus: configure with OpusApplication::LowDelay is accepted") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);
        MediaConfig cfg;
        cfg.set(MediaConfig::OpusApplication, OpusApplication::LowDelay);
        cfg.set(MediaConfig::OpusFrameSizeMs, 5.0f);
        enc->configure(cfg);

        // A 5 ms frame at 48 kHz is 240 samples.
        AudioDesc    desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        const size_t bytes = desc.bufferSize(240);
        auto         buf = Buffer::Ptr::create(bytes);
        buf.modify()->setSize(bytes);
        std::memset(buf.modify()->data(), 0, bytes);
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        auto  frame = PcmAudioPayload::Ptr::create(desc, 240, planes);
        Error err = enc->submitPayload(frame);
        CHECK(err == promeki::Error::Ok);
        delete enc;
}

TEST_CASE("Opus: encoder rejects sample-rate change after first frame") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        // First frame at 48 kHz initialises the encoder.
        auto first = makeSineFramePayloadS16(960, 48000.0f, 2, 440.0f, 0.0);
        REQUIRE(enc->submitPayload(first) == promeki::Error::Ok);

        // Second frame at 24 kHz must be rejected (encoder is locked
        // to its initial descriptor; mismatches surface Error::Invalid).
        auto  second = makeSineFramePayloadS16(480, 24000.0f, 2, 440.0f, 0.0);
        Error err = enc->submitPayload(second);
        CHECK(err == promeki::Error::Invalid);
        delete enc;
}
