/**
 * @file      audiometer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <atomic>
#include <cmath>
#include <cstddef>
#include <memory>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Pluggable per-block metering callback.
 * @ingroup proav
 *
 * AudioMeter is the extension point through which @ref AudioBuffer
 * surfaces every sample it sees on the float-domain processing path
 * to an observer.  The buffer calls @ref process exactly once per
 * push that takes the via-float route, with @p frames frames of
 * @p channels-channel interleaved float samples already in
 * normalized @c [-1, 1] range and @b after channel remap and gain
 * have been applied.
 *
 * Implementations can compute whatever meter shape the application
 * needs — peak, RMS, EBU R 128 LU, true-peak, K-weighted loudness,
 * etc. — and stash the result wherever readers expect to find it.
 * The library ships @ref AudioPeakRmsMeter as a reasonable default.
 *
 * @par Thread Safety
 * @ref process is invoked from the push thread.  Readout methods
 * defined by the concrete subclass are responsible for being safe
 * to call concurrently from any other thread; the default
 * @ref AudioPeakRmsMeter implementation uses lock-free atomics for
 * exactly this reason.
 */
class AudioMeter {
                PROMEKI_SHARED_NOCOPY(AudioMeter)
        public:
                /** @brief Shared-pointer alias.  Copy-on-write disabled — meters carry atomic state. */
                using Ptr = SharedPtr<AudioMeter, /*CopyOnWrite=*/false>;

                virtual ~AudioMeter() = default;

                /**
                 * @brief Processes one block of post-gain float samples.
                 *
                 * @param samples  Interleaved float samples in @c [-1, 1].
                 * @param frames   Number of sample frames in the block.
                 * @param channels Number of channels in the interleaved layout.
                 */
                virtual void process(const float *samples, size_t frames, size_t channels) = 0;

                /** @brief Clears any accumulated state. */
                virtual void reset() = 0;
};

/**
 * @brief Default per-channel peak + RMS meter.
 * @ingroup proav
 *
 * Tracks two values per channel, both in linear amplitude:
 *
 *   - @b peak — the maximum absolute sample value seen since the
 *     last @ref reset, with optional exponential decay between
 *     consecutive @ref process calls (configured via
 *     @ref setPeakDecay).  Decay is a multiplicative factor in
 *     @c [0, 1] applied at the start of each block before the
 *     block's own peak is folded in; @c 1.0 disables decay (peak
 *     is a strict running maximum).
 *
 *   - @b rms — the running root-mean-square over the most recent
 *     samples within a sliding window whose length is configured
 *     via @ref setRmsWindow (frames per channel).  Internally the
 *     meter accumulates a sum of squares and the count, draining
 *     the oldest samples by exponential averaging — cheap, no
 *     ring buffer, and converges to the true sliding RMS within
 *     a few window lengths.
 *
 * Both values are stored in @c std::atomic<float> for lock-free
 * readout from any thread.  The meter does not hold internal
 * state beyond the per-channel atomics, so it is safe to query
 * concurrently with @ref process.
 *
 * Convert to dBFS via @c 20 @c * @c log10(value) at the call
 * site if needed; the meter intentionally exposes linear values
 * so callers picking different reference levels (e.g. EBU,
 * SMPTE, K-weighted) don't pay a log they then have to undo.
 */
class AudioPeakRmsMeter : public AudioMeter {
                PROMEKI_SHARED_DERIVED_NOCOPY(AudioMeter, AudioPeakRmsMeter)
        public:
                /** @brief Shared-pointer alias for @ref AudioPeakRmsMeter. */
                using Ptr = SharedPtr<AudioPeakRmsMeter, /*CopyOnWrite=*/false>;

                /** @brief Constructs a meter for @p channels channels. */
                explicit AudioPeakRmsMeter(size_t channels) { setChannels(channels); }

                AudioPeakRmsMeter(const AudioPeakRmsMeter &) = delete;
                AudioPeakRmsMeter &operator=(const AudioPeakRmsMeter &) = delete;

                /** @brief Reconfigures the meter for a different channel count. */
                void setChannels(size_t channels) {
                        _channels = channels;
                        _peak = std::make_unique<std::atomic<float>[]>(channels);
                        _rms = std::make_unique<std::atomic<float>[]>(channels);
                        for (size_t i = 0; i < channels; ++i) {
                                _peak[i].store(0.0f, std::memory_order_relaxed);
                                _rms[i].store(0.0f, std::memory_order_relaxed);
                        }
                }

                /** @brief Returns the channel count. */
                size_t channels() const { return _channels; }

                /** @brief Returns the current peak linear amplitude on channel @p ch. */
                float peak(size_t ch) const {
                        if (ch >= _channels) return 0.0f;
                        return _peak[ch].load(std::memory_order_relaxed);
                }

                /** @brief Returns the current RMS linear amplitude on channel @p ch. */
                float rms(size_t ch) const {
                        if (ch >= _channels) return 0.0f;
                        return _rms[ch].load(std::memory_order_relaxed);
                }

                /**
                 * @brief Sets the peak decay factor.
                 *
                 * Multiplied into the running peak before each block's
                 * own peak is folded in.  @c 1.0 (default) disables
                 * decay; values below @c 1.0 cause the displayed peak
                 * to drop toward zero between transients, useful for
                 * VU-style meters.  Typical values: @c 0.999 for slow
                 * decay, @c 0.99 for moderate, @c 0.9 for fast.
                 */
                void setPeakDecay(float decay) { _peakDecay = decay; }

                /**
                 * @brief Sets the sliding RMS window length.
                 *
                 * @param windowFrames Window length in frames (per channel).
                 *                     Larger windows yield smoother RMS
                 *                     readings but lag transients.
                 *                     Default: @c 4800 frames (~100 ms at 48 kHz).
                 */
                void setRmsWindow(size_t windowFrames) {
                        if (windowFrames == 0) windowFrames = 1;
                        _rmsAlpha = 1.0f / static_cast<float>(windowFrames);
                }

                /** @copydoc AudioMeter::process */
                void process(const float *samples, size_t frames, size_t channels) override {
                        if (channels > _channels) channels = _channels;
                        if (frames == 0 || channels == 0) return;

                        const float decay = _peakDecay;
                        const float alpha = _rmsAlpha;

                        for (size_t c = 0; c < channels; ++c) {
                                float peak = _peak[c].load(std::memory_order_relaxed) * decay;
                                // Convert held RMS back to mean-square for accumulation.
                                float held = _rms[c].load(std::memory_order_relaxed);
                                float msq = held * held;
                                for (size_t f = 0; f < frames; ++f) {
                                        float s = samples[f * channels + c];
                                        float a = std::fabs(s);
                                        if (a > peak) peak = a;
                                        msq = msq + alpha * (s * s - msq);
                                }
                                _peak[c].store(peak, std::memory_order_relaxed);
                                _rms[c].store(std::sqrt(msq < 0.0f ? 0.0f : msq), std::memory_order_relaxed);
                        }
                }

                /** @copydoc AudioMeter::reset */
                void reset() override {
                        for (size_t i = 0; i < _channels; ++i) {
                                _peak[i].store(0.0f, std::memory_order_relaxed);
                                _rms[i].store(0.0f, std::memory_order_relaxed);
                        }
                }

        private:
                // std::atomic<float> is non-movable, so the per-channel
                // arrays are held by std::unique_ptr<T[]> — promeki::List
                // and the promeki::Atomic<> wrapper require movable
                // elements and would not compile here.  This is the one
                // class in the project where a raw std::unique_ptr<T[]>
                // is preferred over a library wrapper.
                size_t                                _channels = 0;
                std::unique_ptr<std::atomic<float>[]> _peak;
                std::unique_ptr<std::atomic<float>[]> _rms;
                float                                 _peakDecay = 1.0f;
                float                                 _rmsAlpha = 1.0f / 4800.0f;
};

PROMEKI_NAMESPACE_END
