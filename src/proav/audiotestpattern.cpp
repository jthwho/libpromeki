/**
 * @file      audiotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstring>
#include <random>

#include <promeki/audiotestpattern.h>
#include <promeki/audiogen.h>
#include <promeki/ltcencoder.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/timecode.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        /// Two-pi constant with enough precision for audio phase integration.
        static constexpr double kTwoPi = 6.28318530717958647692;

        /// EBU line-up reference level: −18 dBFS.
        static const AudioLevel kEbuLineupLevel = AudioLevel::fromDbfs(-18.0);

        /// IEC 60958 biphase-mark amplitude.
        static constexpr float kIec60958Level = 0.8f;

        /// Professional channel-status block (192 bits / 24 bytes).
        /// Byte 0 bit 0 = 1 → professional.
        /// Byte 0 bits 1-2 = 0 → linear PCM, no emphasis.
        /// Byte 2 bits 0-3 = 0010 → 48 kHz sample rate.
        /// Byte 3 bits 0-2 = 101 → 24-bit word length.
        /// Rest zeroed.
        static constexpr uint8_t kIec60958ChannelStatus[24] = {0x01, 0x00, 0x02, 0x05, 0x00, 0x00, 0x00, 0x00,
                                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

} // anonymous namespace

const List<double> AudioTestPattern::kDefaultSteppedToneFreqs = {100.0, 1000.0, 10000.0};

AudioTestPattern::AudioTestPattern(const AudioDesc &desc) : _desc(desc), _channelModes(AudioPattern::Type) {}

AudioTestPattern::~AudioTestPattern() {
        clearGenerators();
}

void AudioTestPattern::clearGenerators() {
        for (size_t i = 0; i < _chanGens.size(); ++i) {
                delete _chanGens[i];
        }
        _chanGens.clear();
}

AudioPattern AudioTestPattern::modeForChannel(size_t channelIndex) const {
        // Channels beyond the end of the mode list collapse to Silence,
        // matching the documented "short list silences the tail"
        // contract.  Invalid / unbound mode lists silence everything.
        if (!_channelModes.isValid()) return AudioPattern::Silence;
        if (channelIndex >= _channelModes.size()) return AudioPattern::Silence;
        Enum e = _channelModes[channelIndex];
        return AudioPattern(e.value());
}

Error AudioTestPattern::configure() {
        clearGenerators();
        _ltcEncoder.clear();
        _avSyncToneCache.clear();
        _whiteNoiseBuffer.clear();
        _pinkNoiseBuffer.clear();
        _chirpSampleCursor = 0.0;
        _chirpPhase = 0.0;
        _dualTonePhase1 = 0.0;
        _dualTonePhase2 = 0.0;
        _noiseSampleCursor = 0;
        _pcmMarkerCounter = 0;
        _sweepSampleCursor = 0.0;
        _sweepPhase = 0.0;
        _polarityCursor = 0.0;
        _steppedToneSampleCursor = 0.0;
        _steppedTonePhase = 0.0;
        _blitsSampleCursor = 0;
        _blitsPhase = 0.0;
        _ebuLineupSampleCursor = 0;
        _ebuLineupPhase = 0.0;
        _iec60958SampleCursor = 0;

        if (_steppedToneFreqs.isEmpty()) {
                _steppedToneFreqs = kDefaultSteppedToneFreqs;
        }

        const size_t channels = _desc.channels();

        // Scan the channel-mode list once to figure out which optional
        // subsystems we need this round — so a stream that only uses
        // Tone / Silence / SrcProbe doesn't pay the allocation for an
        // LTC encoder or a noise buffer.
        bool needsLtc = false;
        bool needsWhiteNoise = false;
        bool needsPinkNoise = false;
        for (size_t ch = 0; ch < channels; ++ch) {
                AudioPattern m = modeForChannel(ch);
                if (m == AudioPattern::LTC) needsLtc = true;
                if (m == AudioPattern::WhiteNoise) needsWhiteNoise = true;
                if (m == AudioPattern::PinkNoise) needsPinkNoise = true;
                if (m == AudioPattern::Dialnorm) needsPinkNoise = true;
        }

        if (needsLtc) {
                _ltcEncoder = LtcEncoder::UPtr::create(static_cast<int>(_desc.sampleRate()), _ltcLevel.toLinearFloat());
        }

        if (needsWhiteNoise) buildWhiteNoiseBuffer();
        if (needsPinkNoise) buildPinkNoiseBuffer();

        // Per-channel AudioGen entries: one mono generator per channel,
        // configured to emit the shape required by that channel's
        // mode.  LTC / AvSync / noise / chirp / dual-tone / PcmMarker
        // channels get a nullptr generator because they are synthesized
        // per-call by dedicated paths — storing them here would
        // duplicate state.
        _chanGens.resize(channels);
        const AudioDesc monoDesc(_desc.format().id(), _desc.sampleRate(), 1);
        for (size_t ch = 0; ch < channels; ++ch) {
                AudioPattern mode = modeForChannel(ch);
                _chanGens[ch] = nullptr;

                AudioGen::Config cfg;
                cfg.phase = 0.0f;
                cfg.dutyCycle = 0.0f;
                bool buildGen = true;

                if (mode == AudioPattern::Tone) {
                        cfg.type = AudioGen::Sine;
                        cfg.freq = static_cast<float>(_toneFreq);
                        cfg.level = _toneLevel;
                } else if (mode == AudioPattern::Silence) {
                        cfg.type = AudioGen::Silence;
                        cfg.freq = 0.0f;
                        cfg.level = AudioLevel();
                } else if (mode == AudioPattern::SrcProbe) {
                        cfg.type = AudioGen::Sine;
                        cfg.freq = static_cast<float>(kSrcProbeFrequencyHz);
                        cfg.level = _toneLevel;
                } else if (mode == AudioPattern::ChannelId) {
                        cfg.type = AudioGen::Sine;
                        cfg.freq = static_cast<float>(channelIdFrequency(ch, _chanIdBaseFreq, _chanIdStepFreq));
                        cfg.level = _toneLevel;
                } else {
                        // LTC / AvSync / noise / chirp / dual-tone /
                        // PcmMarker / sweep / polarity / stepped-tone /
                        // BLITS / EBU lineup / dialnorm / IEC 60958 /
                        // unknown → no persistent AudioGen state; the
                        // per-call dispatch handles them.
                        buildGen = false;
                }

                if (buildGen) {
                        AudioGen *gen = new AudioGen(monoDesc);
                        gen->setConfig(0, cfg);
                        _chanGens[ch] = gen;
                }
        }

        return Error::Ok;
}

namespace {

        /// Return the default crossfade length for a noise buffer.  One
        /// thirty-second of the buffer is long enough to hide even the
        /// low-frequency content of pink noise (≈300 ms at 10 s / 48 kHz),
        /// short enough that the faded region is a negligible fraction of the
        /// total, and always at least a few hundred samples so even tiny
        /// buffers still loop cleanly.
        size_t noiseCrossfadeLength(size_t bufferLength) {
                size_t fade = bufferLength / 32;
                if (fade < 256) fade = (bufferLength < 256) ? bufferLength / 2 : 256;
                if (fade > bufferLength / 2) fade = bufferLength / 2;
                return fade;
        }

} // anonymous namespace

void AudioTestPattern::buildWhiteNoiseBuffer() {
        // Pre-generate a deterministic PRNG buffer the noise channels
        // can index with a per-channel offset.  Fixed seed keeps the
        // output reproducible across runs (reproducibility is strictly
        // more useful than unpredictability for verification workloads
        // — a random-every-run noise source would force every test
        // downstream to match against a tolerance band).
        //
        // Seamless loop trick: generate `length + fadeLen` raw
        // samples, then build the cached buffer so output[0..fade) is
        // a raised-cosine crossfade between the raw tail
        // (raw[length..length+fade)) and the raw head (raw[0..fade)).
        // When a reader wraps from output[length-1] → output[0] the
        // sequence appears to continue from raw[length-1] → raw[length],
        // which is just "one more sample from the PRNG" with no
        // discontinuity — the click at the wrap boundary disappears.
        const size_t length = static_cast<size_t>(_noiseBufferSeconds * static_cast<double>(_desc.sampleRate()));
        if (length == 0) return;
        const size_t fadeLen = noiseCrossfadeLength(length);

        std::mt19937 rng(_noiseSeed);
        // Gaussian white noise — matches the "true" definition of
        // white noise (uncorrelated samples drawn from a normal
        // distribution) rather than the uniform-distribution
        // shortcut we used earlier.  Both are spectrally flat, but
        // Gaussian matches standard pro-audio test tooling and has
        // the heavier tails one expects from broadband noise.
        //
        // We pick the initial σ at a third of the target peak and
        // then peak-normalise the whole buffer after generation — a
        // long buffer is almost certain to contain samples out past
        // 3σ, so the post-generation rescale lands the actual peak
        // on `peak` regardless of the chosen starting σ.
        const float                     peak = _toneLevel.toLinearFloat();
        std::normal_distribution<float> dist(0.0f, peak / 3.0f);

        List<float> raw;
        raw.resize(length + fadeLen);
        double maxAbs = 0.0;
        for (size_t i = 0; i < length + fadeLen; ++i) {
                const float v = dist(rng);
                raw[i] = v;
                const double a = v < 0 ? -v : v;
                if (a > maxAbs) maxAbs = a;
        }
        if (maxAbs > 0.0) {
                const float scale = static_cast<float>(peak / maxAbs);
                for (size_t i = 0; i < length + fadeLen; ++i) {
                        raw[i] *= scale;
                }
        }

        _whiteNoiseBuffer.resize(length);
        for (size_t i = 0; i < fadeLen; ++i) {
                // Equal-power (sin/cos) crossfade — keeps the
                // combined power constant across the fade because
                // sin²(θ) + cos²(θ) = 1.  A raised-cosine or linear
                // weight would drop the combined power of two
                // uncorrelated noise streams by −3 dB at the
                // midpoint of the fade, which shows up as an audible
                // volume dip at the loop wrap.  At i=0 we play the
                // tail continuation raw[length+0]; at i=fadeLen-1 we
                // play raw[fadeLen-1]; the seam from output[length-1]
                // back to output[0] remains one natural sample step
                // in the PRNG sequence.
                const double theta = 0.5 * M_PI * static_cast<double>(i) / static_cast<double>(fadeLen);
                const double fadeIn = std::sin(theta);
                const double fadeOut = std::cos(theta);
                _whiteNoiseBuffer[i] = static_cast<float>(raw[i] * fadeIn + raw[length + i] * fadeOut);
        }
        for (size_t i = fadeLen; i < length; ++i) {
                _whiteNoiseBuffer[i] = raw[i];
        }

        // DC removal on the final buffer.  We do this AFTER the
        // crossfade (rather than on the raw samples) because the
        // crossfade's per-sample weights are sin/cos, not uniform,
        // and it's the weighted sum of the sample population that
        // becomes the DC component of the output.  Subtracting the
        // final buffer mean preserves seam continuity — the same
        // constant is removed from both sides of the wrap, so the
        // one-sample delta at the boundary is unchanged.
        double sum = 0.0;
        for (size_t i = 0; i < length; ++i) sum += _whiteNoiseBuffer[i];
        const float dc = static_cast<float>(sum / static_cast<double>(length));
        for (size_t i = 0; i < length; ++i) _whiteNoiseBuffer[i] -= dc;
}

void AudioTestPattern::buildPinkNoiseBuffer() {
        // Paul Kellet's 7-tap IIR approximation of a -3 dB/octave pink
        // slope applied to white noise.  The coefficient choice is
        // well-known in audio DSP circles and hits within ~0.05 dB of
        // the ideal pink slope from ~20 Hz to 20 kHz.  We burn a
        // 4096-sample warm-up pass so the IIR is settled before we
        // capture the cached buffer.
        //
        // Same seamless-loop trick as the white buffer: generate
        // `length + fadeLen` filtered samples and crossfade the tail
        // onto the head so the loop wraps without a click.  The
        // crossfade region needs to be a handful of low-frequency
        // cycles long for pink noise (the low end is where the wrap
        // artefact would otherwise be most audible), which is why
        // noiseCrossfadeLength() picks a generous 1/32 of the buffer
        // — ≈300 ms at the 10 s default.
        const size_t length = static_cast<size_t>(_noiseBufferSeconds * static_cast<double>(_desc.sampleRate()));
        if (length == 0) return;
        const size_t fadeLen = noiseCrossfadeLength(length);

        // Independent seed for pink so white and pink don't correlate
        // when both appear on different channels of the same stream.
        std::mt19937                          rng(_noiseSeed ^ 0xA3C7B1F0u);
        const float                           peak = _toneLevel.toLinearFloat();
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        auto   pinkStep = [&](float w) {
                b0 = 0.99886 * b0 + w * 0.0555179;
                b1 = 0.99332 * b1 + w * 0.0750759;
                b2 = 0.96900 * b2 + w * 0.1538520;
                b3 = 0.86650 * b3 + w * 0.3104856;
                b4 = 0.55000 * b4 + w * 0.5329522;
                b5 = -0.7616 * b5 - w * 0.0168980;
                double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
                b6 = w * 0.115926;
                return pink;
        };

        // Warm-up pass so the IIR state is representative of "steady
        // state" pink before the first captured sample.
        for (int i = 0; i < 4096; ++i) pinkStep(dist(rng));

        // Generate `length + fadeLen` pink samples into a scratch
        // buffer.  The Kellet IIR has very large DC gain (the slowest
        // pole alone multiplies DC by ≈ 49), so the raw output is
        // effectively guaranteed to have a real DC component coming
        // from the filter's transient response and any residual drift
        // in the white input.  We DC-remove the raw buffer first so
        // the subsequent peak-normalisation scales the actual AC
        // component to `peak` rather than "DC + AC"; a second
        // DC-removal pass runs at the very end on the final output
        // because the crossfade's sin/cos weighting can reintroduce
        // a small offset that the pre-crossfade pass can't see.
        List<float> raw;
        raw.resize(length + fadeLen);
        double rawSum = 0.0;
        for (size_t i = 0; i < length + fadeLen; ++i) {
                double s = pinkStep(dist(rng));
                raw[i] = static_cast<float>(s);
                rawSum += s;
        }
        const double rawDc = rawSum / static_cast<double>(length + fadeLen);
        double       maxAbs = 0.0;
        for (size_t i = 0; i < length + fadeLen; ++i) {
                const double centred = static_cast<double>(raw[i]) - rawDc;
                raw[i] = static_cast<float>(centred);
                const double a = centred < 0 ? -centred : centred;
                if (a > maxAbs) maxAbs = a;
        }
        if (maxAbs > 0.0) {
                const float scale = static_cast<float>(peak / maxAbs);
                for (size_t i = 0; i < length + fadeLen; ++i) {
                        raw[i] *= scale;
                }
        }

        // Crossfade the tail onto the head using an equal-power
        // sin/cos curve — see the comment in buildWhiteNoiseBuffer
        // for why a raised cosine would introduce an audible −3 dB
        // volume dip at the loop boundary.
        _pinkNoiseBuffer.resize(length);
        for (size_t i = 0; i < fadeLen; ++i) {
                const double theta = 0.5 * M_PI * static_cast<double>(i) / static_cast<double>(fadeLen);
                const double fadeIn = std::sin(theta);
                const double fadeOut = std::cos(theta);
                _pinkNoiseBuffer[i] = static_cast<float>(raw[i] * fadeIn + raw[length + i] * fadeOut);
        }
        for (size_t i = fadeLen; i < length; ++i) {
                _pinkNoiseBuffer[i] = raw[i];
        }

        // Final DC-removal pass on the cached buffer.  See the
        // matching comment in buildWhiteNoiseBuffer — the crossfade's
        // non-uniform weights can leave a small residual DC that we
        // can only eliminate by measuring and subtracting the mean
        // of the final, post-crossfade buffer.
        double outSum = 0.0;
        for (size_t i = 0; i < length; ++i) outSum += _pinkNoiseBuffer[i];
        const float outDc = static_cast<float>(outSum / static_cast<double>(length));
        for (size_t i = 0; i < length; ++i) _pinkNoiseBuffer[i] -= outDc;
}

const List<float> &AudioTestPattern::avSyncBurst(size_t samples) const {
        auto it = _avSyncToneCache.find(samples);
        if (it != _avSyncToneCache.end()) return it->second;

        // Mono one-shot sine burst.  Phase is reset to zero on every
        // call so every marker frame ends up byte-identical, which is
        // what the Inspector A/V sync check expects when it validates
        // the marker waveform.
        const AudioDesc  monoDesc(_desc.format().id(), _desc.sampleRate(), 1);
        AudioGen         burst(monoDesc);
        AudioGen::Config cfg;
        cfg.type = AudioGen::Sine;
        cfg.freq = static_cast<float>(_toneFreq);
        cfg.level = _toneLevel;
        cfg.phase = 0.0f;
        cfg.dutyCycle = 0.0f;
        burst.setConfig(0, cfg);
        List<float> out;
        out.resize(samples);
        burst.generate(out.data(), samples);
        _avSyncToneCache.insert(samples, std::move(out));
        return _avSyncToneCache.find(samples)->second;
}

PcmAudioPayload::Ptr AudioTestPattern::createPayload(size_t samples, const Timecode &tc) const {
        // Allocate a fresh Buffer, fill it with the pattern, then
        // hand it off to the payload.  Writing *before* wrapping
        // keeps us off the CoW detach path — once the Buffer
        // is also held by the BufferView inside the payload, any
        // later @c modify() on the local Buffer would detach
        // and the payload would see the untouched zero buffer.
        AudioDesc   workingDesc = _desc.workingDesc();
        size_t      bufBytes = workingDesc.bufferSize(samples);
        Buffer buf = Buffer(bufBytes);
        buf.setSize(bufBytes);
        std::memset(buf.data(), 0, bufBytes);

        if (_desc.channels() > 0) {
                float *out = reinterpret_cast<float *>(buf.data());
                writePattern(out, samples, tc);
        }

        BufferView view(buf, 0, bufBytes);
        auto       payload = PcmAudioPayload::Ptr::create(workingDesc, samples, view);
        if (!payload.isValid()) return PcmAudioPayload::Ptr();

        // Stamp PcmMarker channels via the AudioDataEncoder using the
        // current monotonic frame counter, then advance it for the
        // next call.  External wrappers that own a frame counter
        // (TpgMediaIO) overwrite via @ref setPcmMarkerFrameNumber
        // before each call.
        stampPcmMarkers(*payload.modify(), _pcmMarkerCounter);
        _pcmMarkerCounter++;

        return payload;
}

void AudioTestPattern::writePattern(float *out, size_t samples, const Timecode &tc) const {
        const size_t channels = _desc.channels();
        if (channels == 0) return;

        // LTC is synthesized once per call and then scattered into
        // every LTC channel.  Invalid timecode gracefully degrades
        // to silence on those channels (the encoder returns an empty
        // list in that case).
        List<int8_t> ltcBytes;
        if (_ltcEncoder.isValid() && tc.isValid()) {
                ltcBytes = _ltcEncoder->encode(tc);
        }
        const bool    haveLtc = !ltcBytes.isEmpty();
        const int8_t *ltcData = haveLtc ? ltcBytes.data() : nullptr;
        const size_t  ltcSamples = haveLtc ? ltcBytes.size() : 0;
        const size_t  ltcCopyN = (ltcSamples < samples) ? ltcSamples : samples;

        // AvSync tone burst fires only on the first frame of each
        // second (tc.frame() == 0) and only when the timecode is
        // valid.  Cache it up front so the per-channel loop is a
        // single pointer read.
        const bool   avSyncActive = tc.isValid() && tc.frame() == 0;
        const float *avSyncData = nullptr;
        if (avSyncActive) {
                const List<float> &burst = avSyncBurst(samples);
                if (!burst.isEmpty()) avSyncData = burst.data();
        }

        // PcmMarker channels are stamped in a post-pass via the
        // AudioDataEncoder once the float-domain pattern has been
        // composed and the buffer is wrapped in a PcmAudioPayload —
        // see @ref stampPcmMarkers.  The float-domain loop below
        // simply leaves those channels at the buffer's pre-zero state.
        for (size_t ch = 0; ch < channels; ++ch) {
                AudioPattern mode = modeForChannel(ch);

                if (mode == AudioPattern::LTC) {
                        if (!haveLtc) continue; // already zero-filled
                        for (size_t s = 0; s < ltcCopyN; ++s) {
                                out[s * channels + ch] = static_cast<float>(ltcData[s]) / 127.0f;
                        }
                        continue;
                }

                if (mode == AudioPattern::AvSync) {
                        if (avSyncData != nullptr) {
                                for (size_t s = 0; s < samples; ++s) {
                                        out[s * channels + ch] = avSyncData[s];
                                }
                        }
                        // Off-marker frames remain silent.
                        continue;
                }

                if (mode == AudioPattern::WhiteNoise) {
                        writeNoiseChannel(out, ch, channels, samples, _whiteNoiseBuffer);
                        continue;
                }
                if (mode == AudioPattern::PinkNoise) {
                        writeNoiseChannel(out, ch, channels, samples, _pinkNoiseBuffer);
                        continue;
                }
                if (mode == AudioPattern::Chirp) {
                        writeChirpChannel(out, ch, channels, samples);
                        continue;
                }
                if (mode == AudioPattern::DualTone) {
                        writeDualToneChannel(out, ch, channels, samples);
                        continue;
                }
                if (mode == AudioPattern::PcmMarker) {
                        // Handled in stampPcmMarkers post-pass —
                        // the float-domain loop just leaves the
                        // channel zeroed.
                        continue;
                }
                if (mode == AudioPattern::Sweep) {
                        writeSweepChannel(out, ch, channels, samples);
                        continue;
                }
                if (mode == AudioPattern::Polarity) {
                        writePolarityChannel(out, ch, channels, samples);
                        continue;
                }
                if (mode == AudioPattern::SteppedTone) {
                        writeSteppedToneChannel(out, ch, channels, samples);
                        continue;
                }
                if (mode == AudioPattern::Blits) {
                        writeBlitsChannel(out, ch, channels, channels, samples);
                        continue;
                }
                if (mode == AudioPattern::EbuLineup) {
                        writeEbuLineupChannel(out, ch, channels, samples);
                        continue;
                }
                if (mode == AudioPattern::Dialnorm) {
                        writeDialnormChannel(out, ch, channels, samples);
                        continue;
                }
                if (mode == AudioPattern::Iec60958) {
                        writeIec60958Channel(out, ch, channels, samples);
                        continue;
                }

                // Everything else flows through a per-channel AudioGen.
                // Channels beyond the mode list, or channels explicitly
                // set to Silence, have a nullptr generator (or
                // generate Silence) and leave the buffer zeroed.
                AudioGen *gen = (ch < _chanGens.size()) ? _chanGens[ch] : nullptr;
                if (gen == nullptr) continue;
                List<float> chData;
                chData.resize(samples);
                if (!gen->generate(chData.data(), samples)) continue;
                for (size_t s = 0; s < samples; ++s) {
                        out[s * channels + ch] = chData[s];
                }
        }

        // Chirp / DualTone phase is advanced inside their per-channel
        // generators now (one running phase accumulator per pattern,
        // wrapped modulo 2π).  They intentionally don't rely on an
        // end-of-create() bookkeeping pass, so phase stays correct
        // whether or not a given channel is present on this stream.

        // Noise channels share a single cumulative read cursor.
        size_t noiseBufLen = _whiteNoiseBuffer.size();
        if (noiseBufLen == 0) noiseBufLen = _pinkNoiseBuffer.size();
        if (noiseBufLen > 0) {
                _noiseSampleCursor = (_noiseSampleCursor + samples) % noiseBufLen;
        }

        // BLITS / EBU lineup / IEC 60958 cursors advance once per
        // create() call so they stay synchronised across channels.
        const size_t sampleRate = _desc.sampleRate();
        if (sampleRate > 0) {
                const size_t blitsCycle = static_cast<size_t>(
                        (kBlitsSegmentSec * (3.0 + static_cast<double>(channels))) * static_cast<double>(sampleRate));
                if (blitsCycle > 0) {
                        _blitsSampleCursor = (_blitsSampleCursor + samples) % blitsCycle;
                }

                const size_t ebuCycle = static_cast<size_t>(kEbuLineupCycleSec * static_cast<double>(sampleRate));
                if (ebuCycle > 0) {
                        _ebuLineupSampleCursor = (_ebuLineupSampleCursor + samples) % ebuCycle;
                }

                const size_t iecFrame = kIec60958StatusBits * kIec60958SamplesPerBit;
                if (iecFrame > 0) {
                        _iec60958SampleCursor = (_iec60958SampleCursor + samples) % iecFrame;
                }
        }
}

void AudioTestPattern::writeNoiseChannel(float *out, size_t channel, size_t channels, size_t samples,
                                         const List<float> &buffer) const {
        if (buffer.isEmpty()) return;
        const size_t bufLen = buffer.size();

        // Per-channel spatial offset decorrelates multiple noise
        // channels that share the same buffer — the spread is a
        // fraction of the buffer per extra channel, modulo bufLen.
        const size_t channelStride = bufLen / (channels > 1 ? channels : 1);
        const size_t channelOffset = (channel * channelStride) % bufLen;

        // The cumulative `_noiseSampleCursor` is advanced once per
        // create() call in create() itself.  Folding it into the base
        // index here turns the cached one-second buffer into a
        // rolling random-access window: successive frames read
        // successive regions instead of re-reading the same slice —
        // which is what would happen if we only used the per-channel
        // offset.  Without this, noise would modulate at the frame
        // rate and sound like a 30 Hz tone instead of hiss.
        const size_t base = (channelOffset + _noiseSampleCursor) % bufLen;

        for (size_t s = 0; s < samples; ++s) {
                const size_t idx = (base + s) % bufLen;
                out[s * channels + channel] = buffer[idx];
        }
}

void AudioTestPattern::writeChirpChannel(float *out, size_t channel, size_t channels, size_t samples) const {
        const double sampleRate = static_cast<double>(_desc.sampleRate());
        if (sampleRate <= 0.0) return;
        if (_chirpDurationSec <= 0.0) return;

        const double durationSec = _chirpDurationSec;
        const double periodSamples = durationSec * sampleRate;
        const double fStart = _chirpStartFreq;
        const double fEnd = _chirpEndFreq;
        if (fStart <= 0.0 || fEnd <= 0.0) return;
        if (periodSamples <= 0.0) return;

        const double logRatio = std::log(fEnd / fStart);
        const float  level = _toneLevel.toLinearFloat();

        // Advance phase incrementally.  For each sample we compute
        // the instantaneous frequency
        //   f = fStart * exp((cursor/periodSamples) * logRatio)
        // and add its per-sample contribution to the running phase:
        //   dφ = 2π * f / sampleRate
        //   φ += dφ
        // then take sin(φ) as the output.
        //
        // This is intentionally different from an earlier closed-form
        // implementation.  The closed form computes φ(t) from t=0 of
        // the current sweep period, which resets the phase to zero
        // whenever the cursor wraps — a once-per-period click in the
        // audio.  By carrying φ across create() calls AND across
        // period wraps, the waveform stays sample-exact continuous;
        // the only discontinuity is a tangent-slope change when the
        // frequency jumps from fEnd back to fStart at the wrap,
        // which is far less audible than a sample-level step.
        //
        // The phase is wrapped modulo 2π each sample so the
        // accumulator stays numerically well-conditioned even over
        // hour-long runs.
        const double wrapMod = kTwoPi;
        double       cursor = _chirpSampleCursor;
        if (cursor < 0.0) cursor = 0.0;
        if (cursor >= periodSamples) cursor = std::fmod(cursor, periodSamples);

        double phase = _chirpPhase;
        for (size_t s = 0; s < samples; ++s) {
                const double progress = cursor / periodSamples;
                const double freq = fStart * std::exp(progress * logRatio);
                phase += kTwoPi * freq / sampleRate;
                if (phase >= wrapMod) phase = std::fmod(phase, wrapMod);
                out[s * channels + channel] = level * static_cast<float>(std::sin(phase));

                cursor += 1.0;
                if (cursor >= periodSamples) cursor -= periodSamples;
        }

        _chirpPhase = phase;
        _chirpSampleCursor = cursor;
}

void AudioTestPattern::writeDualToneChannel(float *out, size_t channel, size_t channels, size_t samples) const {
        const double sampleRate = static_cast<double>(_desc.sampleRate());
        if (sampleRate <= 0.0) return;

        // Normalised dual-sine: the mix is `sin(f1) + ratio*sin(f2)`,
        // scaled so the peak stays at `toneLevel`.  The peak of the
        // sum is at most (1 + ratio), so dividing by that keeps the
        // signal inside [-level, +level].
        const double ratio = _dualToneRatio;
        const double norm = 1.0 + (ratio < 0.0 ? -ratio : ratio);
        const float  level = _toneLevel.toLinearFloat();
        const double w1 = kTwoPi * _dualToneFreq1 / sampleRate;
        const double w2 = kTwoPi * _dualToneFreq2 / sampleRate;

        // Phase state is carried across create() calls — the
        // accumulators live in _dualTonePhase1 / _dualTonePhase2 and
        // get wrapped modulo 2π each sample so the generator stays
        // sample-continuous regardless of chunk size.  An earlier
        // stateless implementation restarted phase at zero on every
        // create(), which at a 30 fps pipeline cadence produces a
        // 30 Hz buzz overlaid on the real signal.
        double       phase1 = _dualTonePhase1;
        double       phase2 = _dualTonePhase2;
        const double wrapMod = kTwoPi;
        for (size_t s = 0; s < samples; ++s) {
                const double v = (std::sin(phase1) + ratio * std::sin(phase2)) / norm;
                out[s * channels + channel] = level * static_cast<float>(v);
                phase1 += w1;
                if (phase1 >= wrapMod) phase1 = std::fmod(phase1, wrapMod);
                phase2 += w2;
                if (phase2 >= wrapMod) phase2 = std::fmod(phase2, wrapMod);
        }
        _dualTonePhase1 = phase1;
        _dualTonePhase2 = phase2;
}

void AudioTestPattern::stampPcmMarkers(PcmAudioPayload &payload, uint64_t frameNumber) const {
        const size_t channels = _desc.channels();
        if (channels == 0 || !_channelModes.isValid()) return;

        // Cheap pre-scan: bail before constructing an encoder when no
        // channel asked for PcmMarker.  The encoder builds primer
        // buffers in its constructor, so skipping when there's nothing
        // to do keeps the per-frame fast path free of the allocation.
        bool any = false;
        for (size_t ch = 0; ch < channels; ++ch) {
                if (modeForChannel(ch) == AudioPattern::PcmMarker) {
                        any = true;
                        break;
                }
        }
        if (!any) return;

        AudioDataEncoder enc(payload.desc());
        if (!enc.isValid()) return;
        if (payload.sampleCount() < enc.packetSamples()) return;

        const uint64_t maskedFrame = frameNumber & kPcmMarkerFrameMask;

        List<AudioDataEncoder::Item> items;
        for (size_t ch = 0; ch < channels; ++ch) {
                if (modeForChannel(ch) != AudioPattern::PcmMarker) continue;
                AudioDataEncoder::Item item{};
                item.firstSample = 0;
                item.sampleCount = payload.sampleCount();
                item.channel = static_cast<uint32_t>(ch);
                // Wire format: top byte = stream ID, next byte =
                // channel index, low 48 bits = frame number.  Mirrors
                // the @c TpgMediaIO video data band so a downstream
                // consumer can correlate audio and video by the same
                // 56-bit stream-plus-frame identifier.
                item.payload = (static_cast<uint64_t>(_pcmMarkerStreamId) << 56) |
                               (static_cast<uint64_t>(ch & 0xffu) << 48) | maskedFrame;
                items.pushToBack(item);
        }
        if (items.isEmpty()) return;
        enc.encode(payload, items);
}

void AudioTestPattern::writeSweepChannel(float *out, size_t channel, size_t channels, size_t samples) const {
        const double sampleRate = static_cast<double>(_desc.sampleRate());
        if (sampleRate <= 0.0) return;
        if (_sweepDurationSec <= 0.0) return;

        const double periodSamples = _sweepDurationSec * sampleRate;
        const double fStart = _sweepStartFreq;
        const double fEnd = _sweepEndFreq;
        if (fStart <= 0.0 || fEnd <= 0.0) return;
        if (periodSamples <= 0.0) return;

        const double fRange = fEnd - fStart;
        const float  level = _toneLevel.toLinearFloat();
        const double wrapMod = kTwoPi;

        double cursor = _sweepSampleCursor;
        if (cursor < 0.0) cursor = 0.0;
        if (cursor >= periodSamples) cursor = std::fmod(cursor, periodSamples);

        double phase = _sweepPhase;
        for (size_t s = 0; s < samples; ++s) {
                const double progress = cursor / periodSamples;
                const double freq = fStart + progress * fRange;
                phase += kTwoPi * freq / sampleRate;
                if (phase >= wrapMod) phase = std::fmod(phase, wrapMod);
                out[s * channels + channel] = level * static_cast<float>(std::sin(phase));

                cursor += 1.0;
                if (cursor >= periodSamples) cursor -= periodSamples;
        }

        _sweepPhase = phase;
        _sweepSampleCursor = cursor;
}

void AudioTestPattern::writePolarityChannel(float *out, size_t channel, size_t channels, size_t samples) const {
        const double sampleRate = static_cast<double>(_desc.sampleRate());
        if (sampleRate <= 0.0) return;
        if (_polarityPulseHz <= 0.0) return;
        if (_polarityPulseWidthSec <= 0.0) return;

        const double periodSamples = sampleRate / _polarityPulseHz;
        const double pulseSamples = _polarityPulseWidthSec * sampleRate;
        const float  level = _toneLevel.toLinearFloat();

        double cursor = _polarityCursor;
        if (cursor < 0.0) cursor = 0.0;
        if (cursor >= periodSamples) cursor = std::fmod(cursor, periodSamples);

        for (size_t s = 0; s < samples; ++s) {
                float v = 0.0f;
                if (cursor < pulseSamples) {
                        const double t = M_PI * cursor / pulseSamples;
                        v = level * static_cast<float>(std::sin(t));
                }
                out[s * channels + channel] = v;

                cursor += 1.0;
                if (cursor >= periodSamples) cursor -= periodSamples;
        }

        _polarityCursor = cursor;
}

void AudioTestPattern::writeSteppedToneChannel(float *out, size_t channel, size_t channels, size_t samples) const {
        const double sampleRate = static_cast<double>(_desc.sampleRate());
        if (sampleRate <= 0.0) return;
        if (_steppedToneFreqs.isEmpty()) return;
        if (_steppedToneStepSec <= 0.0) return;

        const size_t numSteps = _steppedToneFreqs.size();
        const double stepSamples = _steppedToneStepSec * sampleRate;
        const double cycleSamples = stepSamples * static_cast<double>(numSteps);
        const float  level = _toneLevel.toLinearFloat();
        const double wrapMod = kTwoPi;

        double cursor = _steppedToneSampleCursor;
        if (cursor < 0.0) cursor = 0.0;
        if (cursor >= cycleSamples) cursor = std::fmod(cursor, cycleSamples);

        double phase = _steppedTonePhase;
        for (size_t s = 0; s < samples; ++s) {
                const size_t stepIdx = static_cast<size_t>(cursor / stepSamples) % numSteps;
                const double freq = _steppedToneFreqs[stepIdx];
                phase += kTwoPi * freq / sampleRate;
                if (phase >= wrapMod) phase = std::fmod(phase, wrapMod);
                out[s * channels + channel] = level * static_cast<float>(std::sin(phase));

                cursor += 1.0;
                if (cursor >= cycleSamples) cursor -= cycleSamples;
        }

        _steppedTonePhase = phase;
        _steppedToneSampleCursor = cursor;
}

void AudioTestPattern::writeBlitsChannel(float *out, size_t channel, size_t channels, size_t totalChannels,
                                         size_t samples) const {
        // Simplified EBU Tech 3304 BLITS cycle:
        //   Segment 0                 : all channels play kBlitsFreqHz
        //   Segments 1..totalChannels : only the matching channel plays
        //   Segment totalChannels+1   : all channels, odd channels inverted
        //   Segment totalChannels+2   : silence
        const double sampleRate = static_cast<double>(_desc.sampleRate());
        if (sampleRate <= 0.0) return;

        const size_t numSegments = totalChannels + 3;
        const double segmentSamples = kBlitsSegmentSec * sampleRate;
        const float  level = kEbuLineupLevel.toLinearFloat();
        const double wFreq = kTwoPi * kBlitsFreqHz / sampleRate;
        const double wrapMod = kTwoPi;

        size_t cursor = _blitsSampleCursor;
        double phase = _blitsPhase;
        for (size_t s = 0; s < samples; ++s) {
                const size_t seg = static_cast<size_t>(static_cast<double>(cursor) / segmentSamples) % numSegments;

                float v = 0.0f;
                bool  active = false;
                bool  invert = false;

                if (seg == 0) {
                        active = true;
                } else if (seg >= 1 && seg <= totalChannels) {
                        active = (channel == (seg - 1));
                } else if (seg == totalChannels + 1) {
                        active = true;
                        invert = (channel & 1u) != 0;
                }
                // seg == totalChannels + 2 → silence

                if (active) {
                        v = level * static_cast<float>(std::sin(phase));
                        if (invert) v = -v;
                }

                out[s * channels + channel] = v;
                phase += wFreq;
                if (phase >= wrapMod) phase = std::fmod(phase, wrapMod);
                ++cursor;
        }

        _blitsPhase = phase;
}

void AudioTestPattern::writeEbuLineupChannel(float *out, size_t channel, size_t channels, size_t samples) const {
        const double sampleRate = static_cast<double>(_desc.sampleRate());
        if (sampleRate <= 0.0) return;

        const double onSamples = kEbuLineupOnSec * sampleRate;
        const float  level = kEbuLineupLevel.toLinearFloat();
        const double wFreq = kTwoPi * kEbuLineupFreqHz / sampleRate;
        const double wrapMod = kTwoPi;

        size_t cursor = _ebuLineupSampleCursor;
        double phase = _ebuLineupPhase;
        for (size_t s = 0; s < samples; ++s) {
                float v = 0.0f;
                if (static_cast<double>(cursor) < onSamples) {
                        v = level * static_cast<float>(std::sin(phase));
                }
                out[s * channels + channel] = v;
                phase += wFreq;
                if (phase >= wrapMod) phase = std::fmod(phase, wrapMod);
                ++cursor;
        }

        _ebuLineupPhase = phase;
}

void AudioTestPattern::writeDialnormChannel(float *out, size_t channel, size_t channels, size_t samples) const {
        if (_pinkNoiseBuffer.isEmpty()) return;
        const size_t bufLen = _pinkNoiseBuffer.size();

        // Scale from the buffer's peak level (_toneLevel) to _dialnormLevel.
        const float toneLinear = _toneLevel.toLinearFloat();
        const float dialnormLinear = _dialnormLevel.toLinearFloat();
        const float scale = (toneLinear > 0.0f) ? (dialnormLinear / toneLinear) : 0.0f;

        const size_t channelStride = bufLen / (channels > 1 ? channels : 1);
        const size_t channelOffset = (channel * channelStride) % bufLen;
        const size_t base = (channelOffset + _noiseSampleCursor) % bufLen;

        for (size_t s = 0; s < samples; ++s) {
                const size_t idx = (base + s) % bufLen;
                out[s * channels + channel] = _pinkNoiseBuffer[idx] * scale;
        }
}

void AudioTestPattern::writeIec60958Channel(float *out, size_t channel, size_t channels, size_t samples) const {
        // Biphase mark encoding: each bit of the channel-status block
        // occupies kIec60958SamplesPerBit samples.  A transition always
        // occurs at the start of each bit cell.  A '1' bit adds a
        // mid-cell transition; a '0' bit does not.  This produces a
        // decodable clock-embedded waveform in the PCM domain.
        const size_t frameLen = kIec60958StatusBits * kIec60958SamplesPerBit;

        size_t cursor = _iec60958SampleCursor;
        float  polarity = 1.0f;

        // Reconstruct polarity at the current cursor position so
        // that chunks starting mid-frame stay phase-coherent.
        for (size_t i = 0; i < cursor; ++i) {
                const size_t bitIdx = i / kIec60958SamplesPerBit;
                const size_t sub = i % kIec60958SamplesPerBit;
                const size_t byteIdx = bitIdx / 8;
                const size_t bitOff = bitIdx % 8;
                const int    bit = (kIec60958ChannelStatus[byteIdx] >> bitOff) & 1;

                if (sub == 0) {
                        polarity = -polarity;
                } else if (sub == kIec60958SamplesPerBit / 2 && bit) {
                        polarity = -polarity;
                }
        }

        for (size_t s = 0; s < samples; ++s) {
                const size_t pos = cursor % frameLen;
                const size_t bitIdx = pos / kIec60958SamplesPerBit;
                const size_t sub = pos % kIec60958SamplesPerBit;
                const size_t byteIdx = bitIdx / 8;
                const size_t bitOff = bitIdx % 8;
                const int    bit = (kIec60958ChannelStatus[byteIdx] >> bitOff) & 1;

                if (sub == 0) {
                        polarity = -polarity;
                } else if (sub == kIec60958SamplesPerBit / 2 && bit) {
                        polarity = -polarity;
                }

                out[s * channels + channel] = kIec60958Level * polarity;
                ++cursor;
                if (cursor >= frameLen) cursor -= frameLen;
        }
}

PROMEKI_NAMESPACE_END
