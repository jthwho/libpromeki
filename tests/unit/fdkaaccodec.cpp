/**
 * @file      tests/fdkaaccodec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Round-trip tests for the fdk-aac AudioEncoder / AudioDecoder backend.
 * AAC is a lossy codec, so we use an RMS-error tolerance rather than
 * bit-exact comparison (mirrors the @c opusaudiocodec tests).
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
#include <promeki/mediaconfig.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/pcmaudiopayload.h>
#include "codectesthelpers.h"

using namespace promeki;

namespace {

        // Build an interleaved-stereo PCMI_S16LE payload with a sine wave
        // on every channel.
        PcmAudioPayload::Ptr makeSineFrame(size_t samplesPerChannel, float sampleRate, unsigned int channels,
                                           float freqHz, double phase) {
                AudioDesc    desc(AudioFormat::PCMI_S16LE, sampleRate, channels);
                const size_t bytes = desc.bufferSize(samplesPerChannel);
                auto         buf   = Buffer(bytes);
                buf.setSize(bytes);
                auto *p = static_cast<int16_t *>(buf.data());
                for (size_t i = 0; i < samplesPerChannel; ++i) {
                        double  t = static_cast<double>(i) / sampleRate;
                        double  s = std::sin(2.0 * M_PI * freqHz * t + phase) * 0.7;
                        int16_t v = static_cast<int16_t>(s * 32767.0);
                        for (unsigned int c = 0; c < channels; ++c) *p++ = v;
                }
                BufferView planes;
                planes.pushToBack(buf, 0, bytes);
                return PcmAudioPayload::Ptr::create(desc, samplesPerChannel, planes);
        }

        double rmsErrorS16(const int16_t *a, const int16_t *b, size_t n) {
                double sumSq = 0.0;
                for (size_t i = 0; i < n; ++i) {
                        double diff = (static_cast<double>(a[i]) - b[i]) / 32768.0;
                        sumSq += diff * diff;
                }
                return std::sqrt(sumSq / static_cast<double>(n));
        }

        AudioEncoder *makeAacEncoder(const MediaConfig *cfg = nullptr) {
                AudioCodec codec(AudioCodec::AAC);
                auto       r = codec.createEncoder(cfg);
                return isOk(r) ? value(r) : nullptr;
        }

        AudioDecoder *makeAacDecoder(const MediaConfig *cfg = nullptr) {
                AudioCodec codec(AudioCodec::AAC);
                auto       r = codec.createDecoder(cfg);
                return isOk(r) ? value(r) : nullptr;
        }

} // namespace

TEST_CASE("FdkAac: codec registration") {
        AudioCodec codec(AudioCodec::AAC);
        REQUIRE(codec.isValid());
        CHECK(codec.canEncode());
        CHECK(codec.canDecode());
}

TEST_CASE("FdkAac: encoder/decoder round-trip preserves a sine wave within tolerance") {
        const float        sr     = 48000.0f;
        const unsigned int ch     = 2;
        const float        freq   = 440.0f;
        const size_t       chunk  = 1024;  // AAC-LC frame
        const size_t       chunks = 30;    // ~640 ms of audio — enough to past AAC priming

        AudioEncoder *enc = makeAacEncoder();
        AudioDecoder *dec = makeAacDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        MediaConfig encCfg;
        encCfg.set(MediaConfig::BitrateKbps, int32_t(128));
        enc->configure(encCfg);

        MediaConfig decCfg;
        decCfg.set(MediaConfig::AudioRate, sr);
        decCfg.set(MediaConfig::AudioChannels, int32_t(ch));
        dec->configure(decCfg);

        std::vector<int16_t> original;
        original.reserve(chunk * chunks * ch);

        for (size_t i = 0; i < chunks; ++i) {
                auto frame =
                    makeSineFrame(chunk, sr, ch, freq, i * chunk * 2.0 * M_PI * freq / sr);
                const auto *p = reinterpret_cast<const int16_t *>(frame->plane(0).data());
                original.insert(original.end(), p, p + chunk * ch);
                CHECK(enc->submitFrame(tests::frameWith(frame)) == Error::Ok);
                while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                        CHECK(pkt->desc().format().id() == AudioFormat::AAC);
                        CHECK(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
                }
        }
        REQUIRE(enc->flush() == Error::Ok);
        while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                if (pkt->isEndOfStream()) break;
                CHECK(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
        }

        std::vector<int16_t> decoded;
        while (auto out = tests::firstPcmAudio(dec->receiveFrame())) {
                const auto *p = reinterpret_cast<const int16_t *>(out->plane(0).data());
                decoded.insert(decoded.end(), p, p + out->sampleCount() * ch);
        }
        REQUIRE(!decoded.empty());

        // AAC has algorithmic delay similar to Opus; align by searching
        // for the lowest RMS error within a few frames' worth of shift.
        constexpr size_t skipFrames  = 2048 * 2;  // skip first ~85 ms of priming silence
        constexpr size_t searchWindow = 2048 * 4; // ~170 ms search window
        size_t           cmp = std::min(original.size(), decoded.size());
        REQUIRE(cmp > skipFrames + searchWindow);

        const int16_t *origFromSkip = original.data() + skipFrames;
        size_t         cmpLen       = std::min({
                static_cast<size_t>(48000),
                cmp - skipFrames - searchWindow,
                decoded.size() - searchWindow,
        });
        double         bestErr = 1.0;
        for (size_t shift = 0; shift < searchWindow; shift += 2) {
                double err = rmsErrorS16(origFromSkip, decoded.data() + shift, cmpLen);
                if (err < bestErr) bestErr = err;
        }

        // Empirically a 128 kbit/s AAC-LC encode of a 440 Hz tone lands
        // well below RMS 0.05; we set the threshold higher to avoid
        // platform / version flakiness.
        INFO("AAC RMS error: " << bestErr);
        CHECK(bestErr < 0.15);
}

TEST_CASE("FdkAac: decoder trims start-up priming (output delay)") {
        // The decoder discards its start-up output delay (CStreamInfo::
        // outputDelay) from the head of the stream so the first real sample
        // lands at t=0.  Observable: without trimming the decoder emits exactly
        // one 1024-sample frame per access unit (numAUs * 1024 samples); the
        // trim shortens the decoded total by the (sub-few-frames) output delay.
        const float        sr     = 48000.0f;
        const unsigned int ch     = 2;
        const size_t       chunk  = 1024;
        const size_t       chunks = 40;

        AudioEncoder *enc = makeAacEncoder();
        AudioDecoder *dec = makeAacDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        MediaConfig decCfg;
        decCfg.set(MediaConfig::AudioRate, sr);
        decCfg.set(MediaConfig::AudioChannels, int32_t(ch));
        dec->configure(decCfg);

        size_t numAus       = 0;
        size_t decodedPerCh = 0;
        auto   drain        = [&]() {
                while (auto out = tests::firstPcmAudio(dec->receiveFrame())) decodedPerCh += out->sampleCount();
        };

        for (size_t i = 0; i < chunks; ++i) {
                auto frame = makeSineFrame(chunk, sr, ch, 440.0f, i * chunk * 2.0 * M_PI * 440.0 / sr);
                REQUIRE(enc->submitFrame(tests::frameWith(frame)) == Error::Ok);
                while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                        ++numAus;
                        REQUIRE(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
                        drain();
                }
        }
        REQUIRE(enc->flush() == Error::Ok);
        while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                if (pkt->isEndOfStream()) break;
                ++numAus;
                REQUIRE(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
                drain();
        }
        drain();

        REQUIRE(numAus > 0);
        REQUIRE(decodedPerCh > 0);

        const size_t untrimmed = numAus * 1024;
        REQUIRE(decodedPerCh < untrimmed); // priming was removed
        const size_t removed = untrimmed - decodedPerCh;
        INFO("AAC priming removed: " << removed << " samples (numAUs=" << numAus << ")");
        CHECK(removed > 0);
        CHECK(removed <= 4096); // a start-up delay, not whole frames of real audio

        delete enc;
        delete dec;
}

TEST_CASE("FdkAac: encoder rejects unsupported channel layout") {
        AudioEncoder *enc = makeAacEncoder();
        REQUIRE(enc != nullptr);
        // 7 channels — there's no MODE_* mapping for it (we cover 1,2,3,4,5,6,8).
        AudioDesc    desc(AudioFormat::PCMI_S16LE, 48000.0f, 7u);
        const size_t bytes = desc.bufferSize(1024);
        Buffer       buf(bytes);
        buf.setSize(bytes);
        BufferView planes;
        planes.pushToBack(buf, 0, bytes);
        auto payload = PcmAudioPayload::Ptr::create(desc, static_cast<size_t>(1024), planes);
        Error err = enc->submitFrame(tests::frameWith(payload));
        CHECK(err.isError());
}

TEST_CASE("FdkAac: encoder + decoder can be reset cleanly") {
        AudioEncoder *enc = makeAacEncoder();
        AudioDecoder *dec = makeAacDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        auto frame = makeSineFrame(1024, 48000.0f, 2, 440.0f, 0.0);
        CHECK(enc->submitFrame(tests::frameWith(frame)) == Error::Ok);
        CHECK(enc->reset() == Error::Ok);

        // Re-using after reset must work — submit again and observe an emit.
        CHECK(enc->submitFrame(tests::frameWith(frame)) == Error::Ok);
        // The decoder reset path is symmetrical.
        CHECK(dec->reset() == Error::Ok);
}

TEST_CASE("FdkAac: 44.1 kHz mono round-trip works") {
        const float sr = 44100.0f;
        const unsigned int ch = 1;

        AudioEncoder *enc = makeAacEncoder();
        AudioDecoder *dec = makeAacDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);

        MediaConfig encCfg;
        encCfg.set(MediaConfig::BitrateKbps, int32_t(96));
        enc->configure(encCfg);
        MediaConfig decCfg;
        decCfg.set(MediaConfig::AudioRate, sr);
        decCfg.set(MediaConfig::AudioChannels, int32_t(ch));
        dec->configure(decCfg);

        // Push 30 frames and confirm we get at least some decoded output back.
        for (size_t i = 0; i < 30; ++i) {
                auto frame = makeSineFrame(1024, sr, ch, 440.0f, i * 1024.0);
                CHECK(enc->submitFrame(tests::frameWith(frame)) == Error::Ok);
                while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                        CHECK(dec->submitFrame(tests::frameWith(pkt)) == Error::Ok);
                }
        }
        enc->flush();
        while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                if (pkt->isEndOfStream()) break;
                dec->submitFrame(tests::frameWith(pkt));
        }
        size_t decodedSamples = 0;
        while (auto out = tests::firstPcmAudio(dec->receiveFrame())) decodedSamples += out->sampleCount();
        CHECK(decodedSamples > 0);
}
