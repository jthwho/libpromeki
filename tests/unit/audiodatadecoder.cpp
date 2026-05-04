/**
 * @file      tests/audiodatadecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <promeki/audiodatadecoder.h>
#include <promeki/audiodataencoder.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audioresampler.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/pcmaudiopayload.h>

using namespace promeki;

namespace {

        PcmAudioPayload::Ptr makePayload(const AudioDesc &desc, size_t samples) {
                const size_t bytes = desc.bufferSize(samples);
                Buffer::Ptr  buf = Buffer::Ptr::create(bytes);
                buf.modify()->setSize(bytes);
                std::memset(buf.modify()->data(), 0, bytes);
                BufferView view(buf, 0, bytes);
                return PcmAudioPayload::Ptr::create(desc, samples, view);
        }

        // Apply a simple boxcar low-pass to mimic the smoothing an SRC's
        // anti-aliasing filter would introduce.  Not a real polyphase
        // resampler — the goal is just to soften the encoder's hard step
        // edges so we can exercise the decoder's integrate-and-compare
        // path under realistic conditions without pulling in libsoxr.
        void boxcarLowPass(std::vector<float> &samples, size_t window) {
                if (window <= 1 || samples.size() < window) return;
                std::vector<float> out(samples.size(), 0.0f);
                const size_t       half = window / 2;
                for (size_t i = 0; i < samples.size(); ++i) {
                        size_t a = (i >= half) ? (i - half) : 0;
                        size_t b = std::min(samples.size() - 1, i + half);
                        double sum = 0.0;
                        for (size_t k = a; k <= b; ++k) sum += samples[k];
                        out[i] = static_cast<float>(sum / static_cast<double>(b - a + 1));
                }
                samples.swap(out);
        }

} // namespace

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("AudioDataDecoder constructs with defaults") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataDecoder dec(desc);
        CHECK(dec.isValid());
        CHECK(dec.expectedSamplesPerBit() == AudioDataEncoder::DefaultSamplesPerBit);
}

TEST_CASE("AudioDataDecoder rejects compressed formats") {
        AudioDesc desc(AudioFormat(AudioFormat::Opus), 48000.0f, 2);
        CHECK_FALSE(AudioDataDecoder(desc).isValid());
}

// ============================================================================
// Round-trip — clean encoder output, no SRC
// ============================================================================

TEST_CASE("AudioDataDecoder round-trips against the encoder, native float") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        AudioDataDecoder dec(desc);
        REQUIRE(enc.isValid());
        REQUIRE(dec.isValid());

        const uint64_t payloads[] = {0x0000000000000000ULL, 0xffffffffffffffffULL, 0x0123456789abcdefULL,
                                     0xdeadbeefcafebabeULL, 0xa5a5a5a5a5a5a5a5ULL};

        auto payload = makePayload(desc, 1024);
        for (uint64_t p : payloads) {
                AudioDataEncoder::Item item{0, 1024, 0, p};
                REQUIRE(enc.encode(*payload.modify(), item).isOk());

                AudioDataDecoder::Band band{0, 1024, 0};
                auto                   r = dec.decode(*payload, band);
                INFO("payload=" << std::hex << p);
                CHECK(r.error.isOk());
                CHECK(r.decodedSync == AudioDataEncoder::SyncNibble);
                CHECK(r.payload == p);
                CHECK(r.decodedCrc == r.expectedCrc);
                CHECK(r.samplesPerBit == doctest::Approx(static_cast<double>(enc.samplesPerBit())).epsilon(0.05));
        }
}

TEST_CASE("AudioDataDecoder round-trips across PCM formats") {
        const AudioFormat::ID formats[] = {AudioFormat::PCMI_Float32LE, AudioFormat::PCMI_S16LE,
                                           AudioFormat::PCMI_S24LE,    AudioFormat::PCMI_S32LE,
                                           AudioFormat::PCMP_Float32LE, AudioFormat::PCMP_S16LE,
                                           AudioFormat::PCMP_S32LE};
        const uint64_t        payload = 0x0123456789abcdefULL;

        for (AudioFormat::ID fid : formats) {
                AudioFormat fmt(fid);
                if (!fmt.isValid()) continue;

                INFO("format=" << std::string(fmt.name().cstr()));
                AudioDesc        desc(fmt, 48000.0f, 2);
                AudioDataEncoder enc(desc);
                AudioDataDecoder dec(desc);
                REQUIRE(enc.isValid());
                REQUIRE(dec.isValid());

                auto                   pp = makePayload(desc, 1024);
                AudioDataEncoder::Item item{0, 1024, 0, payload};
                REQUIRE(enc.encode(*pp.modify(), item).isOk());

                auto r = dec.decode(*pp, AudioDataDecoder::Band{0, 1024, 0});
                CHECK(r.error.isOk());
                CHECK(r.payload == payload);
                CHECK(r.decodedSync == AudioDataEncoder::SyncNibble);
        }
}

TEST_CASE("AudioDataDecoder identifies independent payloads on multiple channels") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 4);
        AudioDataEncoder enc(desc);
        AudioDataDecoder dec(desc);
        REQUIRE(enc.isValid());

        auto             pp = makePayload(desc, 1024);
        const uint64_t   payloads[4] = {0x1111111111111111ULL, 0x2222222222222222ULL, 0x3333333333333333ULL,
                                        0x4444444444444444ULL};
        for (uint32_t ch = 0; ch < 4; ++ch) {
                AudioDataEncoder::Item item{0, 1024, ch, payloads[ch]};
                REQUIRE(enc.encode(*pp.modify(), item).isOk());
        }
        for (uint32_t ch = 0; ch < 4; ++ch) {
                auto r = dec.decode(*pp, AudioDataDecoder::Band{0, 1024, ch});
                INFO("channel=" << ch);
                CHECK(r.error.isOk());
                CHECK(r.payload == payloads[ch]);
        }
}

// ============================================================================
// Failure paths
// ============================================================================

TEST_CASE("AudioDataDecoder reports CorruptData on a silent band") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataDecoder dec(desc);
        auto             pp = makePayload(desc, 1024);

        auto r = dec.decode(*pp, AudioDataDecoder::Band{0, 1024, 0});
        CHECK(r.error == Error::CorruptData);
}

TEST_CASE("AudioDataDecoder rejects flipped-bit corruption with CRC mismatch") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        AudioDataDecoder dec(desc);

        auto                   pp = makePayload(desc, 1024);
        AudioDataEncoder::Item item{0, 1024, 0, 0x0123456789abcdefULL};
        REQUIRE(enc.encode(*pp.modify(), item).isOk());

        // Pick a payload bit known to be '1' for this payload so a
        // strong negative override of its first half flips the bit
        // unambiguously.  Payload 0x0123456789abcdef → bit 56 = '1';
        // the encoder writes payload bits MSB-first, so payload bit 56
        // lands at transmit index @c SyncBits + (63 - 56) = 11.
        float       *data = reinterpret_cast<float *>(pp.modify()->data()[0].data());
        const size_t H = enc.samplesPerBit() / 2;
        const size_t cellStart = (AudioDataEncoder::SyncBits + 7) * enc.samplesPerBit();
        const size_t channels = desc.channels();
        const size_t channel = item.channel;
        for (size_t k = 0; k < H; ++k) {
                data[(cellStart + k) * channels + channel] = -1.0f;
        }

        auto r = dec.decode(*pp, AudioDataDecoder::Band{0, 1024, 0});
        CHECK(r.error == Error::CorruptData);
        // CRC mismatch fired: we recovered some payload but the CRC
        // didn't match our recomputation.
        CHECK(r.decodedCrc != r.expectedCrc);
}

TEST_CASE("AudioDataDecoder rejects bands too short to fit a codeword") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataDecoder dec(desc);
        auto             pp = makePayload(desc, 1024);
        AudioDataDecoder::Band band{0, 100, 0}; // way below packet length
        auto                   r = dec.decode(*pp, band);
        CHECK(r.error == Error::OutOfRange);
}

// ============================================================================
// SRC-like robustness — boxcar low-pass
// ============================================================================

// ============================================================================
// decodeAll — streaming multi-packet decoder
// ============================================================================

namespace {

        // Build a single-channel codeword in float form by encoding to a
        // 1-channel native-float payload and pulling the channel out.
        // The streaming decoder operates on raw single-channel float
        // buffers, so the test wraps several of these end-to-end into a
        // long input vector.
        std::vector<float> encodeCodewordFloats(uint64_t payloadVal, uint32_t samplesPerBit = 8,
                                                float amplitude = 0.1f, size_t leadSilence = 0,
                                                size_t trailSilence = 0) {
                AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
                AudioDataEncoder enc(desc, samplesPerBit, amplitude);
                REQUIRE(enc.isValid());
                const size_t packetSamples = enc.packetSamples();
                auto         payload = makePayload(desc, packetSamples);
                REQUIRE(payload.isValid());
                AudioDataEncoder::Item item{0, packetSamples, 0, payloadVal};
                REQUIRE(enc.encode(*payload.modify(), item).isOk());

                std::vector<float> out(leadSilence, 0.0f);
                const float       *src = reinterpret_cast<const float *>(payload->plane(0).data());
                out.insert(out.end(), src, src + packetSamples);
                out.insert(out.end(), trailSilence, 0.0f);
                return out;
        }

} // namespace

TEST_CASE("AudioDataDecoder reports packetSampleCount on successful decode") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataEncoder enc(desc);
        AudioDataDecoder dec(desc);
        REQUIRE(enc.isValid());
        REQUIRE(dec.isValid());

        auto pp = makePayload(desc, 1024);
        REQUIRE(enc.encode(*pp.modify(),
                           AudioDataEncoder::Item{0, 1024, 0, 0xdeadbeefULL})
                        .isOk());

        auto r = dec.decode(*pp, AudioDataDecoder::Band{0, 1024, 0});
        REQUIRE(r.error.isOk());
        // Native rate (no resampling): packet length should equal
        // BitsPerPacket * samplesPerBit exactly.
        CHECK(r.packetSampleCount == static_cast<int64_t>(AudioDataDecoder::BitsPerPacket *
                                                          AudioDataEncoder::DefaultSamplesPerBit));
}

TEST_CASE("AudioDataDecoder::decodeAll decodes back-to-back codewords in one call") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        std::vector<float> stream;
        const uint64_t     payloads[3] = {0x1111111111111111ULL, 0x2222222222222222ULL, 0x3333333333333333ULL};
        for (uint64_t p : payloads) {
                auto chunk = encodeCodewordFloats(p, 8, 0.1f, 0, 0);
                stream.insert(stream.end(), chunk.begin(), chunk.end());
        }

        AudioDataDecoder::StreamState state;
        AudioDataDecoder::DecodedList items;
        dec.decodeAll(state, stream.data(), stream.size(), items);

        REQUIRE(items.size() == 3);
        for (size_t i = 0; i < 3; ++i) {
                CAPTURE(i);
                CHECK(items[i].error.isOk());
                CHECK(items[i].payload == payloads[i]);
                // Stream sample positions are non-decreasing and
                // separated by ~packetSamples.
                CHECK(items[i].streamSampleStart >= 0);
                if (i > 0) CHECK(items[i].streamSampleStart > items[i - 1].streamSampleStart);
        }
}

TEST_CASE("AudioDataDecoder::decodeAll holds partial codewords across calls") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        const uint64_t                payloadVal = 0xa5a5a5a5a5a5a5a5ULL;
        auto                          full = encodeCodewordFloats(payloadVal);
        AudioDataDecoder::StreamState state;
        AudioDataDecoder::DecodedList items;

        // First call: only the first half of the codeword.
        const size_t half = full.size() / 2;
        dec.decodeAll(state, full.data(), half, items);
        CHECK(items.isEmpty());

        // Second call: deliver the remainder.  The decoder should now
        // emit one item.
        dec.decodeAll(state, full.data() + half, full.size() - half, items);
        REQUIRE(items.size() == 1);
        CHECK(items[0].error.isOk());
        CHECK(items[0].payload == payloadVal);
        CHECK(items[0].streamSampleStart == 0); // codeword starts at sample 0
}

TEST_CASE("AudioDataDecoder::decodeAll emits a CRC-failure item and advances past it") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        // Encode two codewords, then corrupt the first one's payload
        // bits so its decoded CRC won't match.  The decoder should
        // emit one CorruptData item and one Ok item — both are
        // reachable in a single decodeAll call.
        auto first = encodeCodewordFloats(0x1111111111111111ULL);
        auto second = encodeCodewordFloats(0x2222222222222222ULL);
        // Flip a payload bit in the first codeword by overwriting the
        // first half of one cell with strong negative values (forces
        // the integrator to flip the bit).
        const size_t cellOffset = (AudioDataEncoder::SyncBits + 11) * 8; // payload bit 56's cell
        for (size_t k = 0; k < 4; ++k) first[cellOffset + k] = -1.0f;

        std::vector<float> stream;
        stream.insert(stream.end(), first.begin(), first.end());
        stream.insert(stream.end(), second.begin(), second.end());

        AudioDataDecoder::StreamState state;
        AudioDataDecoder::DecodedList items;
        dec.decodeAll(state, stream.data(), stream.size(), items);

        REQUIRE(items.size() == 2);
        CHECK(items[0].error == Error::CorruptData);
        CHECK(items[0].decodedCrc != items[0].expectedCrc);
        CHECK(items[1].error.isOk());
        CHECK(items[1].payload == 0x2222222222222222ULL);
}

TEST_CASE("AudioDataDecoder::decodeAll waits for a leading positive transition") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        // Leading silence in front of a codeword — findSync should
        // skip the silence, the decoded item's streamSampleStart
        // should equal the silence length.
        const size_t                  leadSilence = 200;
        auto                          full = encodeCodewordFloats(0xbeefULL, 8, 0.1f, leadSilence, 0);
        AudioDataDecoder::StreamState state;
        AudioDataDecoder::DecodedList items;
        dec.decodeAll(state, full.data(), full.size(), items);
        REQUIRE(items.size() == 1);
        CHECK(items[0].error.isOk());
        CHECK(items[0].streamSampleStart == static_cast<int64_t>(leadSilence));
}

TEST_CASE("AudioDataDecoder::decodeAll reports streamSampleStart across multiple calls") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        auto                          first = encodeCodewordFloats(0xaaaaULL);
        auto                          second = encodeCodewordFloats(0xbbbbULL);
        AudioDataDecoder::StreamState state;
        AudioDataDecoder::DecodedList items;

        dec.decodeAll(state, first.data(), first.size(), items);
        REQUIRE(items.size() == 1);
        CHECK(items[0].streamSampleStart == 0);
        const int64_t firstEnd = items[0].streamSampleStart +
                                 static_cast<int64_t>(items[0].samplesPerBit *
                                                      static_cast<double>(AudioDataDecoder::BitsPerPacket) +
                                                      0.5);

        dec.decodeAll(state, second.data(), second.size(), items);
        REQUIRE(items.size() == 1);
        // Second packet's leading edge sits past the first packet's
        // consumed range — same continuous stream coordinate.
        CHECK(items[0].streamSampleStart >= firstEnd);
}

// ============================================================================
// Real SRC round-trip — 48 k → 44.1 k → 48 k via libsamplerate
// ============================================================================

namespace {

        // Run a one-shot resample of @p in (mono float, @p inRate Hz) at
        // ratio outRate/inRate, returning the produced float vector at
        // @p outRate.  Allocates an output buffer sized for the
        // theoretical maximum and trims to the actual output length.
        std::vector<float> resampleMono(const std::vector<float> &in, float inRate, float outRate) {
                AudioResampler r;
                REQUIRE(r.setup(1).isOk());
                REQUIRE(r.setRatio(inRate, outRate).isOk());

                const double ratio = static_cast<double>(outRate) / static_cast<double>(inRate);
                std::vector<float> out(static_cast<size_t>(in.size() * ratio + 32));
                long               inputUsed = 0;
                long               outputGen = 0;
                Error              err = r.process(in.data(), in.size(), out.data(), out.size(),
                                                   inputUsed, outputGen, /*endOfInput=*/true);
                REQUIRE(err.isOk());
                out.resize(static_cast<size_t>(outputGen));
                return out;
        }

} // namespace

TEST_CASE("AudioDataDecoder survives a 48k -> 44.1k -> 48k SRC round-trip") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataEncoder enc(desc);
        AudioDataDecoder dec(desc);
        REQUIRE(enc.isValid());
        REQUIRE(dec.isValid());

        // Build the encoder's float-domain output for a known payload.
        const uint64_t payloadVal = 0x0123456789abcdefULL;
        const size_t   carrierSamples = 2048;
        auto           pp = makePayload(desc, carrierSamples);
        REQUIRE(enc.encode(*pp.modify(),
                           AudioDataEncoder::Item{0, carrierSamples, 0, payloadVal})
                        .isOk());
        const float *src = reinterpret_cast<const float *>(pp->plane(0).data());
        std::vector<float> at48k(src, src + carrierSamples);

        // Round-trip: 48k → 44.1k → 48k.  libsamplerate's default
        // SincMedium quality has linear-phase pre-/post-ringing on
        // the encoder's hard step edges — the inspector's accumulator
        // path doesn't see this in practice (it sees the decoded
        // output directly), but a real network/file round-trip does.
        auto at441k = resampleMono(at48k, 48000.0f, 44100.0f);
        REQUIRE(at441k.size() > AudioDataEncoder::DefaultSamplesPerBit *
                                        AudioDataEncoder::BitsPerPacket);
        auto roundTripped = resampleMono(at441k, 44100.0f, 48000.0f);
        REQUIRE(roundTripped.size() > AudioDataEncoder::DefaultSamplesPerBit *
                                              AudioDataEncoder::BitsPerPacket);

        // Decode the round-tripped float buffer.  The decoder must
        // recover the exact 64-bit payload — sub-sample sync and the
        // ±50 % bandwidth band are the levers that absorb the SRC's
        // pitch and edge softening.
        auto r = dec.decode(roundTripped.data(), roundTripped.size());
        INFO("round-trip samplesPerBit=" << r.samplesPerBit);
        CHECK(r.error.isOk());
        CHECK(r.payload == payloadVal);
}

TEST_CASE("AudioDataDecoder survives a 48k -> 44.1k -> 48k SRC round-trip across multiple phases") {
        // The single-codeword SRC test above exercises one specific
        // alignment of the codeword with the SRC's polyphase filter
        // bank.  libsamplerate's SincMedium uses hundreds of polyphase
        // taps; varying the leading silence at 48 k input shifts the
        // codeword into different phases of the filter and exposes
        // any phase-dependent decoder fragility.  The codeword itself
        // is the same 64 bits across runs — only the input alignment
        // changes.
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataEncoder enc(desc);
        AudioDataDecoder dec(desc);
        REQUIRE(enc.isValid());
        REQUIRE(dec.isValid());

        const uint64_t payloadVal = 0x0123456789abcdefULL;
        // Pre-encode the codeword once.
        const size_t carrierSamples = 1024;
        auto         pp = makePayload(desc, carrierSamples);
        REQUIRE(enc.encode(*pp.modify(),
                           AudioDataEncoder::Item{0, carrierSamples, 0, payloadVal})
                        .isOk());
        const float       *src = reinterpret_cast<const float *>(pp->plane(0).data());
        std::vector<float> codewordAt48k(src, src + carrierSamples);

        // Phase shifts spanning 0..15 exceed one full Manchester bit
        // cell at samplesPerBit=8 — that hits every distinct
        // mod-samplesPerBit alignment plus a margin.
        for (size_t leadSilence = 0; leadSilence <= 15; ++leadSilence) {
                CAPTURE(leadSilence);
                std::vector<float> at48k;
                at48k.insert(at48k.end(), leadSilence, 0.0f);
                at48k.insert(at48k.end(), codewordAt48k.begin(), codewordAt48k.end());
                // Trailing silence so the SRC's filter has somewhere
                // to put its post-ringing without truncating the
                // codeword's tail bits.
                at48k.insert(at48k.end(), 256, 0.0f);

                auto at441k = resampleMono(at48k, 48000.0f, 44100.0f);
                auto roundTripped = resampleMono(at441k, 44100.0f, 48000.0f);

                auto r = dec.decode(roundTripped.data(), roundTripped.size());
                INFO("samplesPerBit=" << r.samplesPerBit << " syncStart=" << r.syncStartSample);
                CHECK(r.error.isOk());
                CHECK(r.payload == payloadVal);
        }
}

TEST_CASE("AudioDataDecoder::decodeAll recovers all codewords across SRC phase variations") {
        // Multi-codeword equivalent of the per-phase test above:
        // three back-to-back codewords with realistic silence padding
        // round-tripped through 48 → 44.1 → 48 at varying input
        // phase offsets.  Verifies the streaming decoder still
        // recovers every codeword regardless of where each one
        // happens to land in the output sample grid.
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        const uint64_t payloads[3] = {0x1111111111111111ULL, 0xa5a5a5a5a5a5a5a5ULL, 0x0123456789abcdefULL};
        for (size_t leadSilence = 0; leadSilence <= 15; ++leadSilence) {
                CAPTURE(leadSilence);
                std::vector<float> stream;
                stream.insert(stream.end(), leadSilence, 0.0f);
                for (uint64_t p : payloads) {
                        auto chunk = encodeCodewordFloats(p, 8, 0.1f, /*leadSilence=*/0,
                                                          /*trailSilence=*/192);
                        stream.insert(stream.end(), chunk.begin(), chunk.end());
                }
                auto at441k = resampleMono(stream, 48000.0f, 44100.0f);
                auto roundTripped = resampleMono(at441k, 44100.0f, 48000.0f);

                AudioDataDecoder::StreamState state;
                AudioDataDecoder::DecodedList items;
                dec.decodeAll(state, roundTripped.data(), roundTripped.size(), items);

                std::vector<uint64_t> recovered;
                for (const auto &it : items) {
                        if (it.error.isOk()) recovered.push_back(it.payload);
                }
                REQUIRE(recovered.size() == 3);
                for (size_t i = 0; i < 3; ++i) CHECK(recovered[i] == payloads[i]);
        }
}

TEST_CASE("AudioDataDecoder::decodeAll survives a 48k -> 44.1k -> 48k SRC round-trip") {
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 1);
        AudioDataEncoder enc(desc);
        AudioDataDecoder dec(desc);

        // Three codewords with the silence pad the TPG produces in
        // real life (Item.sampleCount > packetSamples).  The pad
        // gives the SRC's pre-/post-ringing a place to settle so the
        // leading-edge detection on the next codeword still locks
        // onto the real edge.
        const uint64_t                payloads[3] = {0x1111111111111111ULL, 0xa5a5a5a5a5a5a5a5ULL,
                                                     0x0123456789abcdefULL};
        std::vector<float>            stream;
        for (uint64_t p : payloads) {
                auto chunk = encodeCodewordFloats(p, 8, 0.1f, /*leadSilence=*/0,
                                                  /*trailSilence=*/192);
                stream.insert(stream.end(), chunk.begin(), chunk.end());
        }
        auto at441k = resampleMono(stream, 48000.0f, 44100.0f);
        auto roundTripped = resampleMono(at441k, 44100.0f, 48000.0f);

        AudioDataDecoder::StreamState state;
        AudioDataDecoder::DecodedList items;
        dec.decodeAll(state, roundTripped.data(), roundTripped.size(), items);

        // Filter to successful decodes — SRC pre-/post-ringing might
        // still produce a stray failure-flagged item but the three
        // real codewords must all decode.
        std::vector<uint64_t> recovered;
        for (const auto &it : items) {
                if (it.error.isOk()) recovered.push_back(it.payload);
        }
        REQUIRE(recovered.size() == 3);
        for (size_t i = 0; i < 3; ++i) CHECK(recovered[i] == payloads[i]);
}

TEST_CASE("AudioDataDecoder survives a boxcar smoothing the encoder output") {
        // Default samplesPerBit=8, half-bit=4 samples.  A 3-sample
        // boxcar smooths the encoder's step edges over ~3 samples,
        // emulating a (very generous) SRC anti-aliasing transient
        // without changing the sample rate.
        AudioDesc        desc(AudioFormat(AudioFormat::NativeFloat), 48000.0f, 2);
        AudioDataEncoder enc(desc);
        AudioDataDecoder dec(desc);

        const uint64_t pVal = 0x0123456789abcdefULL;
        auto           pp = makePayload(desc, 2048);
        REQUIRE(enc.encode(*pp.modify(), AudioDataEncoder::Item{32, 2000, 0, pVal}).isOk());

        // Lift the channel into a flat float vector, smooth, write back.
        const size_t       channels = desc.channels();
        float             *data = reinterpret_cast<float *>(pp.modify()->data()[0].data());
        std::vector<float> ch(2048, 0.0f);
        for (size_t s = 0; s < 2048; ++s) ch[s] = data[s * channels + 0];
        boxcarLowPass(ch, 3);
        for (size_t s = 0; s < 2048; ++s) data[s * channels + 0] = ch[s];

        auto r = dec.decode(*pp, AudioDataDecoder::Band{32, 2000, 0});
        CHECK(r.error.isOk());
        CHECK(r.payload == pVal);
}
