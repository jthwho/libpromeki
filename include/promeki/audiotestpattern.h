/**
 * @file      audiotestpattern.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/audiolevel.h>
#include <promeki/audiodesc.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/enumlist.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/ltcencoder.h>

PROMEKI_NAMESPACE_BEGIN

class AudioGen;
class Timecode;

/**
 * @brief Multi-channel audio test pattern generator.
 * @ingroup proav
 *
 * Generates audio test patterns with per-channel pattern assignment.
 * Every channel is driven by its own @ref AudioPattern value pulled
 * from the configured @ref EnumList.  If the channel-mode list is
 * shorter than the stream's channel count, any remaining channels are
 * silenced; if the list is longer, the extra entries are ignored.
 *
 * Supported per-channel patterns:
 *  - @c Tone — continuous sine at @ref toneFrequency.
 *  - @c Silence — constant zero.
 *  - @c LTC — Linear Timecode audio for the current frame timecode.
 *  - @c AvSync — one-shot tone burst on @c tc.frame() == 0, silent
 *                otherwise.  Pairs with the picture AvSync marker for
 *                A/V drift detection.
 *  - @c SrcProbe — 997 Hz reference sine.  The prime-relative
 *                  frequency reveals sample-rate-conversion artifacts
 *                  on the decode side.
 *  - @c ChannelId — channel-unique sine at
 *                  `channelIdBaseFreq() + ch * channelIdStepFreq()`,
 *                  so a downstream consumer that sees channels out of
 *                  order can identify each one by frequency.
 *  - @c Sweep — linear frequency sweep.
 *  - @c Polarity — positive half-sine impulse for polarity verification.
 *  - @c SteppedTone — cycles through discrete frequency steps.
 *  - @c Blits — EBU BLITS identification / polarity / silence cycle.
 *  - @c EbuLineup — 1 kHz at −18 dBFS, 3 s on / 2 s off.
 *  - @c Dialnorm — pink noise at a calibrated loudness.
 *  - @c Iec60958 — biphase-mark channel-status block in PCM.
 *
 * Call @ref configure after changing any pattern parameter and before
 * calling @ref create or @ref render.  The class is not thread-safe;
 * external synchronization is required for concurrent access.
 *
 * @par Example
 * @code
 * AudioTestPattern gen(AudioDesc(48000, 4));
 *
 * // LTC on ch0, AvSync click on ch1, SRC probe on ch2, silence on ch3.
 * EnumList modes = EnumList::forType<AudioPattern>();
 * modes.append(AudioPattern::LTC);
 * modes.append(AudioPattern::AvSync);
 * modes.append(AudioPattern::SrcProbe);
 * modes.append(AudioPattern::Silence);
 * gen.setChannelModes(modes);
 *
 * gen.setToneFrequency(1000.0);
 * gen.setToneLevel(AudioLevel::fromDbfs(-20.0));
 * gen.setLtcLevel(AudioLevel::fromDbfs(-20.0));
 * gen.configure();
 *
 * Audio audio = gen.create(samplesPerFrame, currentTimecode);
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 */
class AudioTestPattern {
        public:
                /** @brief Unique-ownership pointer to an AudioTestPattern. */
                using UPtr = UniquePtr<AudioTestPattern>;

                /**
                 * @brief Constructs an AudioTestPattern for the given stream.
                 * @param desc Audio format descriptor.  The channel count
                 *             drives how many entries from
                 *             @ref channelModes are consumed.
                 */
                AudioTestPattern(const AudioDesc &desc);

                /** @brief Destructor. */
                ~AudioTestPattern();

                AudioTestPattern(const AudioTestPattern &) = delete;
                AudioTestPattern &operator=(const AudioTestPattern &) = delete;

                /** @brief Returns the audio descriptor. */
                const AudioDesc &desc() const { return _desc; }

                /** @brief Returns the per-channel pattern list. */
                const EnumList &channelModes() const { return _channelModes; }

                /**
                 * @brief Sets the per-channel pattern list.
                 *
                 * @p modes must be an @ref EnumList whose element type
                 * is @ref AudioPattern.  An empty / invalid list is
                 * accepted and silences every channel.
                 */
                void setChannelModes(const EnumList &modes) { _channelModes = modes; }

                /** @brief Returns the tone frequency in Hz. */
                double toneFrequency() const { return _toneFreq; }

                /** @brief Sets the tone frequency in Hz (for @c Tone / @c AvSync). */
                void setToneFrequency(double freq) { _toneFreq = freq; }

                /** @brief Returns the tone level. */
                AudioLevel toneLevel() const { return _toneLevel; }

                /** @brief Sets the tone level (for @c Tone / @c AvSync / @c SrcProbe / @c ChannelId). */
                void setToneLevel(AudioLevel level) { _toneLevel = level; }

                /** @brief Returns the LTC level. */
                AudioLevel ltcLevel() const { return _ltcLevel; }

                /** @brief Sets the LTC level. */
                void setLtcLevel(AudioLevel level) { _ltcLevel = level; }

                /** @brief Returns the ChannelId base frequency in Hz. */
                double channelIdBaseFreq() const { return _chanIdBaseFreq; }

                /**
                 * @brief Sets the ChannelId base frequency in Hz.
                 *
                 * Channel @em N carries
                 * `channelIdBaseFreq() + N * channelIdStepFreq()`.
                 */
                void setChannelIdBaseFreq(double hz) { _chanIdBaseFreq = hz; }

                /** @brief Returns the ChannelId step frequency in Hz. */
                double channelIdStepFreq() const { return _chanIdStepFreq; }

                /** @brief Sets the ChannelId step frequency in Hz. */
                void setChannelIdStepFreq(double hz) { _chanIdStepFreq = hz; }

                /** @brief Returns the Chirp sweep start frequency in Hz. */
                double chirpStartFreq() const { return _chirpStartFreq; }

                /** @brief Sets the Chirp sweep start frequency in Hz. */
                void setChirpStartFreq(double hz) { _chirpStartFreq = hz; }

                /** @brief Returns the Chirp sweep end frequency in Hz. */
                double chirpEndFreq() const { return _chirpEndFreq; }

                /** @brief Sets the Chirp sweep end frequency in Hz. */
                void setChirpEndFreq(double hz) { _chirpEndFreq = hz; }

                /** @brief Returns the Chirp sweep period in seconds. */
                double chirpDurationSec() const { return _chirpDurationSec; }

                /** @brief Sets the Chirp sweep period in seconds. */
                void setChirpDurationSec(double sec) { _chirpDurationSec = sec; }

                /** @brief Returns the DualTone low-side frequency in Hz. */
                double dualToneFreq1() const { return _dualToneFreq1; }

                /** @brief Sets the DualTone low-side frequency in Hz. */
                void setDualToneFreq1(double hz) { _dualToneFreq1 = hz; }

                /** @brief Returns the DualTone high-side frequency in Hz. */
                double dualToneFreq2() const { return _dualToneFreq2; }

                /** @brief Sets the DualTone high-side frequency in Hz. */
                void setDualToneFreq2(double hz) { _dualToneFreq2 = hz; }

                /**
                 * @brief Returns the amplitude ratio of freq2 to freq1.
                 *
                 * Default 0.25 reproduces the SMPTE IMD-1 reference
                 * (4:1 freq1-to-freq2 amplitude).  The output is
                 * normalised so the peak stays at @ref toneLevel.
                 */
                double dualToneRatio() const { return _dualToneRatio; }

                /** @brief Sets the DualTone amplitude ratio. */
                void setDualToneRatio(double ratio) { _dualToneRatio = ratio; }

                /** @brief Returns the Sweep start frequency in Hz. */
                double sweepStartFreq() const { return _sweepStartFreq; }

                /** @brief Sets the Sweep start frequency in Hz. */
                void setSweepStartFreq(double hz) { _sweepStartFreq = hz; }

                /** @brief Returns the Sweep end frequency in Hz. */
                double sweepEndFreq() const { return _sweepEndFreq; }

                /** @brief Sets the Sweep end frequency in Hz. */
                void setSweepEndFreq(double hz) { _sweepEndFreq = hz; }

                /** @brief Returns the Sweep period in seconds. */
                double sweepDurationSec() const { return _sweepDurationSec; }

                /** @brief Sets the Sweep period in seconds. */
                void setSweepDurationSec(double sec) { _sweepDurationSec = sec; }

                /** @brief Returns the Polarity pulse repetition rate in Hz. */
                double polarityPulseHz() const { return _polarityPulseHz; }

                /** @brief Sets the Polarity pulse repetition rate in Hz. */
                void setPolarityPulseHz(double hz) { _polarityPulseHz = hz; }

                /** @brief Returns the Polarity half-sine pulse width in seconds. */
                double polarityPulseWidthSec() const { return _polarityPulseWidthSec; }

                /** @brief Sets the Polarity half-sine pulse width in seconds. */
                void setPolarityPulseWidthSec(double sec) { _polarityPulseWidthSec = sec; }

                /** @brief Returns the SteppedTone frequency list. */
                const List<double> &steppedToneFreqs() const { return _steppedToneFreqs; }

                /** @brief Sets the SteppedTone frequency list. */
                void setSteppedToneFreqs(const List<double> &freqs) { _steppedToneFreqs = freqs; }

                /** @brief Returns the SteppedTone step duration in seconds. */
                double steppedToneStepSec() const { return _steppedToneStepSec; }

                /** @brief Sets the SteppedTone step duration in seconds. */
                void setSteppedToneStepSec(double sec) { _steppedToneStepSec = sec; }

                /** @brief Returns the Dialnorm target level. */
                AudioLevel dialnormLevel() const { return _dialnormLevel; }

                /** @brief Sets the Dialnorm target level. */
                void setDialnormLevel(AudioLevel level) { _dialnormLevel = level; }

                /**
                 * @brief Returns the noise buffer length in seconds.
                 *
                 * The white/pink noise channels share a pre-generated
                 * sample buffer of this duration, cached in
                 * @ref configure.  The default is ten seconds — long
                 * enough that a casual listener can't pick out the
                 * loop repetition, and cheap enough at typical sample
                 * rates (≈1.9 MB at 48 kHz float).  The buffer is
                 * generated with a raised-cosine crossfade at the
                 * wrap boundary so the loop is seamless; the longer
                 * the buffer the less often the (already-inaudible)
                 * transition fires.
                 */
                double noiseBufferSeconds() const { return _noiseBufferSeconds; }

                /** @brief Sets the noise buffer length in seconds. */
                void setNoiseBufferSeconds(double sec) { _noiseBufferSeconds = sec; }

                /**
                 * @brief Returns the seed used to generate the noise buffers.
                 *
                 * Fixed by default so noise is reproducible across
                 * runs — the TPG is a test-pattern generator, and
                 * reproducibility is strictly more useful than
                 * unpredictability for verification workloads.
                 */
                uint32_t noiseSeed() const { return _noiseSeed; }

                /** @brief Sets the seed used to generate the noise buffers. */
                void setNoiseSeed(uint32_t seed) { _noiseSeed = seed; }

                /**
                 * @brief Configures internal generators for the current settings.
                 *
                 * Must be called after changing mode list, frequencies, or
                 * levels and before calling @ref create or @ref render.
                 * Rebuilds per-channel generator state and drops any
                 * cached pattern buffers.
                 *
                 * @return @c Error::Ok on success.
                 */
                Error configure();

                /**
                 * @brief Creates a new Audio buffer with the test pattern.
                 *
                 * Iterates the per-channel mode list, generates the
                 * corresponding signal for each channel (or silence for
                 * channels with no mode entry), and returns the combined
                 * interleaved Audio buffer.
                 *
                 * @param samples Number of samples to generate.
                 * @param tc      Current frame timecode.  Used by @c LTC
                 *                and @c AvSync channels; invalid values
                 *                degrade to silence on those channels.
                 * @return A new payload, or a null Ptr on failure.
                 */
                SharedPtr<PcmAudioPayload, true, PcmAudioPayload>
                createPayload(size_t samples, const Timecode &tc) const;

                /**
                 * @brief Payload-native counterpart to @ref createPayload(samples, tc).
                 *
                 * Equivalent to @c createPayload(samples, Timecode()) —
                 * LTC and AvSync channels degrade to silence.
                 */
                SharedPtr<PcmAudioPayload, true, PcmAudioPayload>
                createPayload(size_t samples) const {
                        return createPayload(samples, Timecode());
                }

                /**
                 * @brief Returns the ChannelId tone frequency for a given channel.
                 *
                 * Exposed for the Inspector / test code so it can
                 * independently compute the expected frequency for a
                 * @c ChannelId channel without duplicating the formula.
                 *
                 * @param channelIndex Zero-based channel index.
                 * @param baseFreq     Base frequency (Hz).
                 * @param stepFreq     Per-channel step frequency (Hz).
                 * @return `baseFreq + channelIndex * stepFreq`.
                 */
                static double channelIdFrequency(size_t channelIndex,
                                                 double baseFreq,
                                                 double stepFreq) {
                        return baseFreq + static_cast<double>(channelIndex) * stepFreq;
                }

                /// @brief Canonical SRC-probe reference frequency in Hz.
                ///
                /// 997 Hz is coprime with every common audio sample rate
                /// so any SRC applied downstream shifts the observed
                /// frequency by a detectable amount.
                static constexpr double kSrcProbeFrequencyHz = 997.0;

                /// @brief Length of the PcmMarker sync preamble in samples.
                static constexpr size_t kPcmMarkerPreambleSamples = 16;

                /// @brief Length of the PcmMarker "start of payload" marker
                ///        (four high samples followed by four low samples).
                static constexpr size_t kPcmMarkerStartSamples = 8;

                /// @brief Bits in the PcmMarker payload.
                static constexpr size_t kPcmMarkerPayloadBits = 64;

                /// @brief Total number of deterministic samples in one
                ///        PcmMarker framing unit (preamble + start + 64 bits
                ///        + parity).  Channels shorter than this carry
                ///        a truncated frame; the decoder is expected to
                ///        resynchronise.
                static constexpr size_t kPcmMarkerFrameSamples =
                        kPcmMarkerPreambleSamples +
                        kPcmMarkerStartSamples +
                        kPcmMarkerPayloadBits + 1;  // +1 trailing parity bit

                /// @brief Default SteppedTone frequency list (low / mid / high).
                static const List<double> kDefaultSteppedToneFreqs;

                /// @brief EBU line-up tone frequency (Hz).
                static constexpr double kEbuLineupFreqHz = 1000.0;

                /// @brief EBU line-up on-time within the 5 s cycle.
                static constexpr double kEbuLineupOnSec = 3.0;

                /// @brief EBU line-up full cycle length.
                static constexpr double kEbuLineupCycleSec = 5.0;

                /// @brief BLITS identification tone frequency (Hz).
                static constexpr double kBlitsFreqHz = 1000.0;

                /// @brief Duration of each BLITS segment in seconds.
                static constexpr double kBlitsSegmentSec = 1.0;

                /// @brief IEC 60958 professional channel-status block length in bits.
                static constexpr size_t kIec60958StatusBits = 192;

                /// @brief Samples per biphase-mark symbol in the Iec60958 pattern.
                static constexpr size_t kIec60958SamplesPerBit = 4;

        private:
                AudioDesc       _desc;
                EnumList        _channelModes;
                double          _toneFreq        = 1000.0;
                AudioLevel      _toneLevel       = AudioLevel::fromDbfs(-20.0);
                AudioLevel      _ltcLevel        = AudioLevel::fromDbfs(-20.0);
                double          _chanIdBaseFreq  = 1000.0;
                double          _chanIdStepFreq  = 100.0;

                double          _chirpStartFreq   = 20.0;
                double          _chirpEndFreq     = 20000.0;
                double          _chirpDurationSec = 1.0;

                double          _dualToneFreq1 = 60.0;
                double          _dualToneFreq2 = 7000.0;
                double          _dualToneRatio = 0.25;

                double          _sweepStartFreq   = 20.0;
                double          _sweepEndFreq     = 20000.0;
                double          _sweepDurationSec = 1.0;

                double          _polarityPulseHz       = 1.0;
                double          _polarityPulseWidthSec = 0.001;

                List<double>    _steppedToneFreqs;
                double          _steppedToneStepSec = 1.0;

                AudioLevel      _dialnormLevel = AudioLevel::fromDbfs(-24.0);

                double          _noiseBufferSeconds = 10.0;
                uint32_t        _noiseSeed          = 0x505244A4u; // 'PRDA' — TPG test seed

                // Per-channel generators built in configure().  A nullptr
                // entry means the channel is silenced (either because
                // the mode list is shorter than the channel count, or
                // because it carries a mode that does not need a
                // persistent generator — e.g. LTC / AvSync / noise /
                // chirp / PCM marker, which are synthesized per-call).
                mutable List<AudioGen *> _chanGens;
                LtcEncoder::UPtr         _ltcEncoder;

                // AvSync tone burst cache: built lazily per requested
                // sample count so the cadenced rates (29.97, 59.94)
                // which alternate between two sizes don't thrash the
                // cache.  Each entry is a one-shot mono tone burst
                // (phase reset to zero so every marker frame is
                // byte-identical).
                mutable Map<size_t, List<float>> _avSyncToneCache;

                // Cached noise buffers.  One-dimensional arrays of
                // samples that the per-channel noise dispatch indexes
                // with a per-channel offset so different WhiteNoise /
                // PinkNoise channels stay decorrelated while sharing
                // the same underlying samples.  Generated at most once
                // per configure() call; empty until a noise channel
                // requests them.
                List<float>     _whiteNoiseBuffer;
                List<float>     _pinkNoiseBuffer;

                // Continuous-sweep cursor/phase for the Chirp
                // generator.  The cursor is the sample position inside
                // the current sweep period (wraps at
                // _chirpDurationSec * sampleRate).  The phase is the
                // running sin-argument; we accumulate it sample-by-
                // sample instead of recomputing it from the closed-
                // form integral so the waveform stays sample-exact
                // continuous across both chunk boundaries and period
                // wraps (the closed form resets to 0 at each period
                // wrap, which causes an audible click).
                mutable double  _chirpSampleCursor = 0.0;
                mutable double  _chirpPhase        = 0.0;

                // Per-tone phase accumulators for the DualTone
                // generator.  Tracking these across create() calls is
                // the only way to avoid a once-per-chunk phase-reset
                // click; a 30 fps pipeline restarts the pattern 30
                // times a second otherwise, which is plainly audible.
                mutable double  _dualTonePhase1 = 0.0;
                mutable double  _dualTonePhase2 = 0.0;

                // Cumulative sample cursor for the cached noise
                // buffers.  Advanced once per create() call so
                // successive frames read from successive regions of
                // the buffer instead of re-reading the same slice —
                // otherwise the noise would modulate at the frame
                // rate and sound like a tone, not noise.  All noise
                // channels share this cursor, with a per-channel
                // offset layered on top so multiple noise channels
                // on the same stream stay decorrelated.
                mutable size_t _noiseSampleCursor = 0;

                // Running monotonic counter used by the PcmMarker
                // generator when the current frame has no valid
                // timecode.  Incremented on every create() call.
                mutable uint64_t _pcmMarkerCounter = 0;

                mutable double  _sweepSampleCursor = 0.0;
                mutable double  _sweepPhase        = 0.0;

                mutable double  _polarityCursor = 0.0;

                mutable double  _steppedToneSampleCursor = 0.0;
                mutable double  _steppedTonePhase        = 0.0;

                mutable size_t  _blitsSampleCursor = 0;
                mutable double  _blitsPhase        = 0.0;

                mutable size_t  _ebuLineupSampleCursor = 0;
                mutable double  _ebuLineupPhase        = 0.0;

                mutable size_t  _iec60958SampleCursor = 0;

                void clearGenerators();
                AudioPattern modeForChannel(size_t channelIndex) const;
                const List<float> &avSyncBurst(size_t samples) const;

                // Shared inner loop: writes @p samples samples per
                // channel of the configured pattern into @p out,
                // advancing all phase and cursor state.  Used by both
                // @ref create (Audio allocation) and @ref createPayload
                // (PcmAudioPayload allocation).
                void writePattern(float *out, size_t samples,
                                  const Timecode &tc) const;
                void buildWhiteNoiseBuffer();
                void buildPinkNoiseBuffer();

                void writeNoiseChannel(float *out, size_t channel, size_t channels,
                                       size_t samples,
                                       const List<float> &buffer) const;
                void writeChirpChannel(float *out, size_t channel, size_t channels,
                                       size_t samples) const;
                void writeDualToneChannel(float *out, size_t channel, size_t channels,
                                          size_t samples) const;
                void writePcmMarkerChannel(float *out, size_t channel, size_t channels,
                                           size_t samples, uint64_t payload) const;
                void writeSweepChannel(float *out, size_t channel, size_t channels,
                                       size_t samples) const;
                void writePolarityChannel(float *out, size_t channel, size_t channels,
                                          size_t samples) const;
                void writeSteppedToneChannel(float *out, size_t channel, size_t channels,
                                             size_t samples) const;
                void writeBlitsChannel(float *out, size_t channel, size_t channels,
                                       size_t totalChannels, size_t samples) const;
                void writeEbuLineupChannel(float *out, size_t channel, size_t channels,
                                           size_t samples) const;
                void writeDialnormChannel(float *out, size_t channel, size_t channels,
                                          size_t samples) const;
                void writeIec60958Channel(float *out, size_t channel, size_t channels,
                                          size_t samples) const;
};

PROMEKI_NAMESPACE_END
