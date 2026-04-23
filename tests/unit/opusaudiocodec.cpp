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
#include <vector>
#include <promeki/audio.h>
#include <promeki/audiocodec.h>
#include <promeki/audiodecoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioencoder.h>
#include <promeki/buffer.h>
#include <promeki/enums.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediapacket.h>

using namespace promeki;

namespace {

// Generates an interleaved-stereo PCMI_S16LE Audio frame containing
// a 440 Hz sine wave on both channels at unity-ish amplitude (~-3 dBFS).
Audio makeSineFrameS16(size_t samplesPerChannel,
                       float sampleRate, unsigned int channels,
                       float freqHz, double phase) {
        AudioDesc desc(AudioFormat::PCMI_S16LE, sampleRate, channels);
        const size_t bytes = desc.bufferSize(samplesPerChannel);
        auto buf = Buffer::Ptr::create(bytes);
        buf.modify()->setSize(bytes);
        auto *p = static_cast<int16_t *>(buf.modify()->data());
        for(size_t i = 0; i < samplesPerChannel; ++i) {
                double t = static_cast<double>(i) / sampleRate;
                double s = std::sin(2.0 * M_PI * freqHz * t + phase) * 0.7;
                int16_t v = static_cast<int16_t>(s * 32767.0);
                for(unsigned int c = 0; c < channels; ++c) {
                        *p++ = v;
                }
        }
        return Audio::fromBuffer(buf, desc);
}

// Computes RMS error between two interleaved s16 streams of equal
// length, normalised to the [-1, 1] range so the threshold is unit-
// independent.
double rmsErrorS16(const int16_t *a, const int16_t *b, size_t n) {
        double sumSq = 0.0;
        for(size_t i = 0; i < n; ++i) {
                double diff = (static_cast<double>(a[i]) - b[i]) / 32768.0;
                sumSq += diff * diff;
        }
        return std::sqrt(sumSq / static_cast<double>(n));
}

AudioEncoder *makeOpusEncoder(const MediaConfig *cfg = nullptr) {
        AudioCodec codec(AudioCodec::Opus);
        auto res = codec.createEncoder(cfg);
        return isOk(res) ? value(res) : nullptr;
}

AudioDecoder *makeOpusDecoder(const MediaConfig *cfg = nullptr) {
        AudioCodec codec(AudioCodec::Opus);
        auto res = codec.createDecoder(cfg);
        return isOk(res) ? value(res) : nullptr;
}

} // namespace

TEST_CASE("Opus: codec metadata advertises the libopus restrictions") {
        AudioCodec codec(AudioCodec::Opus);
        REQUIRE(codec.isValid());
        CHECK(codec.canEncode());   // Native backend registered at static init
        CHECK(codec.canDecode());

        // Sanity-check the metadata that opusaudiocodec.cpp does NOT
        // re-register — these come from the well-known Data record in
        // src/core/audiocodec.cpp and should match libopus's
        // documented restrictions.
        CHECK(codec.supportedSampleRates().contains(48000.0f));
        CHECK(codec.supportedChannelCounts().contains(2));
}

TEST_CASE("Opus: encoder/decoder round-trip preserves a sine wave within tolerance") {
        const float        sr     = 48000.0f;
        const unsigned int ch     = 2;
        const float        freq   = 440.0f;
        const size_t       chunk  = 960;   // 20 ms @ 48 kHz
        const size_t       chunks = 25;    // 0.5 s of audio

        AudioEncoder *enc = makeOpusEncoder();
        AudioDecoder *dec = makeOpusDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        AudioCodec codec(AudioCodec::Opus);

        // Encoder: 64 kbit/s, audio mode, 20 ms frames.
        MediaConfig encCfg;
        encCfg.set(MediaConfig::BitrateKbps,    int32_t(64));
        encCfg.set(MediaConfig::OpusApplication, OpusApplication::Audio);
        encCfg.set(MediaConfig::OpusFrameSizeMs, 20.0f);
        enc->configure(encCfg);

        // Decoder: 48 kHz / 2 ch matches the encoder.
        MediaConfig decCfg;
        decCfg.set(MediaConfig::AudioRate,     sr);
        decCfg.set(MediaConfig::AudioChannels, int32_t(ch));
        dec->configure(decCfg);

        // Build the original signal as a single big buffer so we can
        // compare against the decoded output later.
        std::vector<int16_t> original;
        original.reserve(chunk * chunks * ch);

        for(size_t i = 0; i < chunks; ++i) {
                Audio::Ptr frame = Audio::Ptr::create(makeSineFrameS16(chunk, sr, ch, freq,
                                                                       i * chunk * 2.0 * M_PI * freq / sr));
                const auto *p = frame->data<int16_t>();
                original.insert(original.end(), p, p + chunk * ch);
                CHECK(enc->submitFrame(frame) == promeki::Error::Ok);
                while(auto pkt = enc->receivePacket()) {
                        CHECK(pkt->audioCodec().id() == codec.id());
                        CHECK(dec->submitPacket(pkt) == promeki::Error::Ok);
                }
        }

        // Drain the decoder and stitch the output together.
        std::vector<int16_t> decoded;
        while(true) {
                Audio::Ptr out = dec->receiveFrame();
                if(!out.isValid()) break;
                const auto *p = out->data<int16_t>();
                decoded.insert(decoded.end(), p, p + out->samples() * ch);
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
        const int16_t *origFromSkip = original.data() + skipFrames;
        // Find the best-matching alignment within +/-2880 samples
        // (60 ms) of the natural delay so the RMS comparison isn't
        // foiled by Opus's encoder-side look-ahead.  The test signal
        // is a steady tone so the cross-correlation peak is sharp.
        constexpr size_t searchWindow = 2880 * 2;
        size_t cmpLen = std::min({
                static_cast<size_t>(48000),                          // 1 second window
                cmp - skipFrames - searchWindow,
                decoded.size() - searchWindow,
        });
        double bestErr = 1.0;
        for(size_t shift = 0; shift < searchWindow; shift += 2) {
                double err = rmsErrorS16(
                        origFromSkip,
                        decoded.data() + shift,
                        cmpLen);
                if(err < bestErr) bestErr = err;
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
        AudioDesc badDesc(AudioFormat::PCMI_S16LE, 44100.0f, 2);
        Audio bad = Audio::fromBuffer(
                Buffer::Ptr::create(badDesc.bufferSize(960)),
                badDesc);
        bad.buffer().modify()->setSize(badDesc.bufferSize(960));

        // Some sanity: the Audio is otherwise valid; opus rejects on
        // descriptor mismatch.
        CHECK(bad.isValid());
        promeki::Error err = enc->submitFrame(Audio::Ptr::create(std::move(bad)));
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());

        delete enc;
}

TEST_CASE("Opus: encoder flush emits a final EndOfStream packet") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        // Submit a single 20 ms frame, drain, then flush.
        Audio::Ptr frame = Audio::Ptr::create(makeSineFrameS16(960, 48000.0f, 2, 440.0f, 0.0));
        REQUIRE(enc->submitFrame(frame) == promeki::Error::Ok);
        auto pkt = enc->receivePacket();
        REQUIRE(pkt);
        CHECK_FALSE(pkt->isEndOfStream());

        CHECK(enc->flush() == promeki::Error::Ok);
        auto eos = enc->receivePacket();
        REQUIRE(eos);
        CHECK(eos->isEndOfStream());
        CHECK_FALSE(enc->receivePacket());
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
        AudioDesc badDesc(AudioFormat::PCMI_S16LE, 48000.0f, 6);
        Audio bad = Audio::fromBuffer(
                Buffer::Ptr::create(badDesc.bufferSize(960)),
                badDesc);
        bad.buffer().modify()->setSize(badDesc.bufferSize(960));
        Error err = enc->submitFrame(Audio::Ptr::create(std::move(bad)));
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("Opus: encoder rejects null Audio frame") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);
        Error err = enc->submitFrame(Audio::Ptr());
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

        Audio::Ptr frame = Audio::Ptr::create(makeSineFrameS16(480, 48000.0f, 2, 440.0f, 0.0));
        Error err = enc->submitFrame(frame);
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("Opus: encoder reset clears state and the next frame is encodable again") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        Audio::Ptr frame = Audio::Ptr::create(makeSineFrameS16(960, 48000.0f, 2, 440.0f, 0.0));
        REQUIRE(enc->submitFrame(frame) == promeki::Error::Ok);
        REQUIRE(enc->receivePacket());
        // Flush + reset should leave the encoder ready to use with no
        // EOS in flight.
        REQUIRE(enc->flush() == promeki::Error::Ok);
        REQUIRE(enc->reset() == promeki::Error::Ok);

        // After reset, the next submit/receive cycle must succeed and
        // not be polluted by the prior flush state.
        REQUIRE(enc->submitFrame(frame) == promeki::Error::Ok);
        auto pkt = enc->receivePacket();
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

        Error err = dec->submitPacket(AudioPacket::Ptr());
        CHECK(err == promeki::Error::Invalid);

        // Valid Ptr but empty buffer — decoder ensureDecoder() runs first,
        // then bails on the empty buffer.
        Buffer::Ptr empty = Buffer::Ptr::create(0);
        AudioPacket::Ptr pkt = AudioPacket::Ptr::create(empty,
                AudioCodec(AudioCodec::Opus));
        Error err2 = dec->submitPacket(pkt);
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

        Buffer::Ptr buf = Buffer::Ptr::create(8);
        buf.modify()->fill(0x00);
        buf.modify()->setSize(8);
        AudioPacket::Ptr pkt = AudioPacket::Ptr::create(buf,
                AudioCodec(AudioCodec::Opus));
        Error err = dec->submitPacket(pkt);
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

        Buffer::Ptr buf = Buffer::Ptr::create(8);
        buf.modify()->fill(0x00);
        buf.modify()->setSize(8);
        AudioPacket::Ptr pkt = AudioPacket::Ptr::create(buf,
                AudioCodec(AudioCodec::Opus));
        Error err = dec->submitPacket(pkt);
        CHECK(err == promeki::Error::Invalid);
        CHECK_FALSE(dec->lastErrorMessage().isEmpty());
        delete dec;
}

TEST_CASE("Opus: decoder reset is a no-op when no decoder is initialised yet") {
        AudioDecoder *dec = makeOpusDecoder();
        REQUIRE(dec != nullptr);
        // Neither configure() nor a successful submitPacket() has run,
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

        const float        sr     = 48000.0f;
        const unsigned int ch     = 2;
        const size_t       chunk  = 960;

        MediaConfig encCfg;
        encCfg.set(MediaConfig::OpusFrameSizeMs, 20.0f);
        enc->configure(encCfg);

        MediaConfig decCfg;
        decCfg.set(MediaConfig::AudioRate, sr);
        decCfg.set(MediaConfig::AudioChannels, int32_t(ch));
        dec->configure(decCfg);

        // Build a Float32LE frame of zeros (silence).
        AudioDesc desc(AudioFormat::PCMI_Float32LE, sr, ch);
        const size_t bytes = desc.bufferSize(chunk);
        auto buf = Buffer::Ptr::create(bytes);
        buf.modify()->setSize(bytes);
        std::memset(buf.modify()->data(), 0, bytes);
        Audio frame = Audio::fromBuffer(buf, desc);
        REQUIRE(frame.isValid());

        Error err = enc->submitFrame(Audio::Ptr::create(std::move(frame)));
        CHECK(err == promeki::Error::Ok);
        // The encoder must produce at least one packet for the full
        // 20ms / 960-sample frame.
        auto pkt = enc->receivePacket();
        REQUIRE(pkt);
        CHECK(pkt->size() > 0);
        // Decode round-trip should succeed.
        CHECK(dec->submitPacket(pkt) == promeki::Error::Ok);
        Audio::Ptr out = dec->receiveFrame();
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
        // applicationFromEnum) to actually run.
        Audio::Ptr frame = Audio::Ptr::create(
                makeSineFrameS16(960, 48000.0f, 1, 440.0f, 0.0));
        // Above sine helper writes both channels; build a mono variant
        // by halving the Audio's channel count and trimming.  The
        // existing helper unconditionally writes ch interleaved
        // copies — for mono just request channels=1 explicitly.
        AudioDesc monoDesc(AudioFormat::PCMI_S16LE, 48000.0f, 1);
        auto buf = Buffer::Ptr::create(monoDesc.bufferSize(960));
        buf.modify()->setSize(monoDesc.bufferSize(960));
        std::memset(buf.modify()->data(), 0, buf->size());
        Audio mono = Audio::fromBuffer(buf, monoDesc);
        Error err = enc->submitFrame(Audio::Ptr::create(std::move(mono)));
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
        AudioDesc desc(AudioFormat::PCMI_S16LE, 48000.0f, 2);
        auto buf = Buffer::Ptr::create(desc.bufferSize(240));
        buf.modify()->setSize(desc.bufferSize(240));
        std::memset(buf.modify()->data(), 0, buf->size());
        Audio frame = Audio::fromBuffer(buf, desc);
        Error err = enc->submitFrame(Audio::Ptr::create(std::move(frame)));
        CHECK(err == promeki::Error::Ok);
        delete enc;
}

TEST_CASE("Opus: encoder rejects sample-rate change after first frame") {
        AudioEncoder *enc = makeOpusEncoder();
        REQUIRE(enc != nullptr);

        // First frame at 48 kHz initialises the encoder.
        Audio::Ptr first = Audio::Ptr::create(
                makeSineFrameS16(960, 48000.0f, 2, 440.0f, 0.0));
        REQUIRE(enc->submitFrame(first) == promeki::Error::Ok);

        // Second frame at 24 kHz must be rejected (encoder is locked
        // to its initial descriptor; mismatches surface Error::Invalid).
        Audio::Ptr second = Audio::Ptr::create(
                makeSineFrameS16(480, 24000.0f, 2, 440.0f, 0.0));
        Error err = enc->submitFrame(second);
        CHECK(err == promeki::Error::Invalid);
        delete enc;
}
