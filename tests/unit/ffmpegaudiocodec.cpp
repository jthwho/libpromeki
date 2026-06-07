/**
 * @file      tests/ffmpegaudiocodec.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * Round-trip tests for the generic FFmpeg AudioEncoder / AudioDecoder
 * backend (AC-3 / MP3 / FLAC).  A 440 Hz sine wave is encoded, decoded,
 * and compared against the original.  AC-3 and MP3 are lossy (compared
 * with a generous RMS tolerance after aligning out the codec delay);
 * FLAC is lossless (compared near-bit-exact after alignment).
 */

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#include <promeki/audiocodec.h>
#include <promeki/audiodecoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioencoder.h>
#include <promeki/buffer.h>
#include <promeki/enums_audio.h>
#include <promeki/mediaconfig.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/pcmaudiopayload.h>
#include "codectesthelpers.h"

using namespace promeki;

namespace {

        // Interleaved-stereo PCMI_S16LE sine payload of @p samplesPerChannel.
        PcmAudioPayload::Ptr makeSineS16(size_t samplesPerChannel, float sampleRate, unsigned int channels,
                                         float freqHz, double phase) {
                AudioDesc    desc(AudioFormat::PCMI_S16LE, sampleRate, channels);
                const size_t bytes = desc.bufferSize(samplesPerChannel);
                auto         buf = Buffer(bytes);
                buf.setSize(bytes);
                auto *p = static_cast<int16_t *>(buf.data());
                for (size_t i = 0; i < samplesPerChannel; ++i) {
                        double  t = static_cast<double>(i) / sampleRate;
                        int16_t v = static_cast<int16_t>(std::sin(2.0 * M_PI * freqHz * t + phase) * 0.7 * 32767.0);
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

        // Encodes a 0.5 s tone through the codec, decodes it back, and returns
        // the best RMS error over a +/- search window that absorbs the codec's
        // algorithmic delay.  Returns -1.0 on a hard failure.
        double roundTripRms(AudioCodec::ID id, int bitrateKbps) {
                const float        sr = 48000.0f;
                const unsigned int ch = 2;
                const float        freq = 440.0f;
                const size_t       chunk = 2048;
                const size_t       chunks = 12; // ~0.5 s

                AudioCodec codec(id);
                if (!codec.canEncode() || !codec.canDecode()) return -1.0;
                auto encRes = codec.createEncoder();
                auto decRes = codec.createDecoder();
                if (!isOk(encRes) || !isOk(decRes)) return -1.0;
                AudioEncoder *enc = value(encRes);
                AudioDecoder *dec = value(decRes);

                MediaConfig encCfg;
                if (bitrateKbps > 0) encCfg.set(MediaConfig::BitrateKbps, int32_t(bitrateKbps));
                enc->configure(encCfg);
                MediaConfig decCfg;
                decCfg.set(MediaConfig::AudioRate, sr);
                decCfg.set(MediaConfig::AudioChannels, int32_t(ch));
                dec->configure(decCfg);

                std::vector<int16_t> original;
                std::vector<int16_t> decoded;
                auto drainDecoder = [&]() {
                        while (true) {
                                PcmAudioPayload::Ptr out = tests::firstPcmAudio(dec->receiveFrame());
                                if (!out.isValid()) break;
                                const auto *p = reinterpret_cast<const int16_t *>(out->plane(0).data());
                                decoded.insert(decoded.end(), p, p + out->sampleCount() * ch);
                        }
                };

                double phase = 0.0;
                for (size_t i = 0; i < chunks; ++i) {
                        auto pcm = makeSineS16(chunk, sr, ch, freq, phase);
                        phase += 2.0 * M_PI * freq * chunk / sr;
                        const auto *p = reinterpret_cast<const int16_t *>(pcm->plane(0).data());
                        original.insert(original.end(), p, p + chunk * ch);
                        if (enc->submitFrame(tests::frameWith(pcm)) != Error::Ok) { delete enc; delete dec; return -1.0; }
                        while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                                if (pkt->isEndOfStream()) break;
                                dec->submitFrame(tests::frameWith(pkt));
                                drainDecoder();
                        }
                }
                enc->flush();
                while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                        if (pkt->isEndOfStream()) break;
                        dec->submitFrame(tests::frameWith(pkt));
                        drainDecoder();
                }
                dec->flush();
                drainDecoder();
                delete enc;
                delete dec;

                if (decoded.size() < chunk * ch * 4 || original.size() < chunk * ch * 4) return -1.0;

                // Align out the codec delay: slide the decoded stream against the
                // original (skipping a lead-in) and take the best RMS.
                const size_t skip = 4096 * ch;          // step past encoder priming
                const size_t searchWin = 4096 * ch;     // up to ~85 ms of delay
                const size_t cmpLen = std::min({ static_cast<size_t>(24000 * ch),
                                                 original.size() - skip - searchWin,
                                                 decoded.size() - searchWin });
                double best = 1.0;
                for (size_t shift = 0; shift < searchWin; shift += ch) {
                        double err = rmsErrorS16(original.data() + skip, decoded.data() + shift, cmpLen);
                        if (err < best) best = err;
                }
                return best;
        }

        AudioFormat compressedFormatFor(AudioCodec::ID id) {
                for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                        AudioFormat f(fid);
                        if (f.isCompressed() && f.audioCodec().id() == id) return f;
                }
                return AudioFormat();
        }

} // namespace

TEST_CASE("FFmpeg: backend registered for AC-3 / MP3 / FLAC") {
        auto bk = AudioCodec::lookupBackend("FFmpeg");
        REQUIRE(isOk(bk));
        const AudioCodec::Backend ffmpeg = value(bk);
        for (AudioCodec::ID id : {AudioCodec::AC3, AudioCodec::MP3, AudioCodec::FLAC}) {
                AudioCodec codec(id);
                CHECK(codec.availableEncoderBackends().contains(ffmpeg));
                CHECK(codec.availableDecoderBackends().contains(ffmpeg));
                CHECK(codec.canEncode());
                CHECK(codec.canDecode());
        }
}

TEST_CASE("FFmpeg: AC-3 encode/decode round-trips a sine wave (lossy)") {
        double rms = roundTripRms(AudioCodec::AC3, 192);
        REQUIRE(rms >= 0.0);
        CHECK(rms < 0.10);
}

TEST_CASE("FFmpeg: MP3 encode/decode round-trips a sine wave (lossy)") {
        double rms = roundTripRms(AudioCodec::MP3, 192);
        REQUIRE(rms >= 0.0);
        CHECK(rms < 0.10);
}

TEST_CASE("FFmpeg: FLAC encode/decode round-trips a sine wave (lossless)") {
        double rms = roundTripRms(AudioCodec::FLAC, 0);
        REQUIRE(rms >= 0.0);
        // Lossless: after alignment the overlapping region is bit-exact.
        CHECK(rms < 1.0e-4);
}

TEST_CASE("FFmpeg: emitted compressed payloads carry the right codec identity") {
        AudioCodec codec(AudioCodec::AC3);
        auto       encRes = codec.createEncoder();
        REQUIRE(isOk(encRes));
        AudioEncoder *enc = value(encRes);

        // Feed enough samples to force at least one AC-3 frame (1536/frame).
        for (int i = 0; i < 4; ++i) {
                auto pcm = makeSineS16(1536, 48000.0f, 2, 440.0f, i * 0.1);
                CHECK(enc->submitFrame(tests::frameWith(pcm)) == Error::Ok);
        }
        bool sawPacket = false;
        while (auto pkt = tests::firstCompressedAudio(enc->receiveFrame())) {
                if (pkt->isEndOfStream()) break;
                CHECK(pkt->desc().format().audioCodec().id() == AudioCodec::AC3);
                CHECK(pkt->sampleCount() > 0);
                sawPacket = true;
        }
        CHECK(sawPacket);
        delete enc;
}

TEST_CASE("FFmpeg: encoder rejects a frame with no PCM payload") {
        AudioCodec codec(AudioCodec::AC3);
        auto       encRes = codec.createEncoder();
        REQUIRE(isOk(encRes));
        AudioEncoder *enc = value(encRes);
        CHECK(enc->submitFrame(Frame()) == Error::Invalid);
        CHECK_FALSE(enc->lastErrorMessage().isEmpty());
        delete enc;
}

TEST_CASE("FFmpeg: decoder rejects a frame with no compressed payload") {
        AudioCodec codec(AudioCodec::MP3);
        auto       decRes = codec.createDecoder();
        REQUIRE(isOk(decRes));
        AudioDecoder *dec = value(decRes);
        CHECK(dec->submitFrame(Frame()) == Error::Invalid);
        delete dec;
}

TEST_CASE("FFmpeg: each serviced codec exposes a compressed AudioFormat") {
        CHECK(compressedFormatFor(AudioCodec::AC3).isValid());
        CHECK(compressedFormatFor(AudioCodec::MP3).isValid());
        CHECK(compressedFormatFor(AudioCodec::FLAC).isValid());
}
