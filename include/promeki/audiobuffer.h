/**
 * @file      audiobuffer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>
#include <promeki/audiodesc.h>
#include <promeki/audiometer.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/enums.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>
#if PROMEKI_ENABLE_SRC
#include <promeki/audioresampler.h>
#endif

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Thread-safe ring-buffered audio sample FIFO with format conversion on push.
 * @ingroup proav
 *
 * AudioBuffer stores interleaved PCM samples in a fixed output format.
 * Callers push audio in any compatible input format (differing bit
 * depth, endianness, integer vs float are handled automatically) and
 * pop samples in the storage format. The buffer is designed as the
 * efficient bridge between an Audio producer that works in one format
 * (e.g. native float at the video frame rate) and a consumer that
 * wants a different framing or format (e.g. a container writer that
 * writes fixed-size chunks of s16 little-endian).
 *
 * @par Relationship to @ref AudioBlock
 *
 * @c AudioBuffer is a @b data @b container — a value-type FIFO of
 * sample bytes plus an internal @ref AudioResampler.  It has no
 * graph identity, no parent, no signals, and is not part of the
 * @ref ObjectBase hierarchy.  Use it whenever you need a
 * thread-safe handoff of PCM samples between a producer and a
 * consumer that may run on different clocks or in different formats.
 *
 * @ref AudioBlock, in contrast, is a @b pipeline @b node — an
 * @ref ObjectBase-derived processing element that exposes named
 * source and sink channels for connecting into an audio graph.
 * The two classes are not redundant: an @ref AudioBlock typically
 * owns one or more @ref AudioBuffer instances internally to hand
 * samples between its own threads, but they sit at different
 * conceptual layers and should not be unified.
 *
 * @par Format conversion
 *
 * When @c inputFormat() differs from @c format() only in @c DataType
 * (bit depth / endian / float vs int), @c push() converts on the fly
 * via AudioDesc's @c samplesToFloat / @c floatToSamples helpers.
 *
 * When the input and output @b sample @b rates differ and
 * @c PROMEKI_ENABLE_SRC is available, @c push() resamples on the fly
 * via an internal @ref AudioResampler.  Use @c setResamplerQuality()
 * to control the quality mode (default: @ref SrcQuality::SincMedium).
 * When @c PROMEKI_ENABLE_SRC is not available, @c push() returns
 * @c Error::NotSupported for rate mismatches.
 *
 * When the input and output @b channel @b counts differ, @c push()
 * returns @c Error::NotSupported (channel-map hook TBD).
 *
 * @par Drift correction
 *
 * When the producer and consumer run on independent clocks (e.g.
 * ALSA capture vs. video frame timer), their rates drift relative to
 * each other.  Over time the buffer either fills up (producer faster)
 * or drains (consumer faster).
 *
 * Call @c enableDriftCorrection() to engage the resampler with
 * automatic ratio adjustment.  On each @c push(), the buffer
 * measures its fill level against a caller-supplied target and
 * adjusts the resampling ratio via a PI (proportional–integral)
 * controller:
 *
 * @code
 * error     = (available - target) / target
 * integral += error
 * ratio     = nominalRatio * (1.0 - Kp * error - Ki * integral)
 * @endcode
 *
 * The proportional term (Kp = gain) reacts immediately to fill
 * level deviations.  The integral term (Ki = gain / 1000)
 * accumulates over time, eliminating any steady-state offset
 * caused by a constant clock drift — without the integral, a
 * proportional-only controller would allow the buffer to
 * stabilize above or below the target.
 *
 * When the buffer is overfull the ratio decreases (fewer output
 * samples per input), draining it back to the target.  When it is
 * underfull the ratio increases, filling it back up.  The gain
 * parameter controls how aggressively the correction is applied —
 * small values (0.001 – 0.01) give inaudible corrections.
 *
 * Drift correction works whether the input and output sample rates
 * are the same or different.  When rates are the same, the nominal
 * ratio is 1.0 and only the drift adjustment moves it.  When rates
 * differ, the nominal ratio handles the fixed conversion and the
 * drift adjustment rides on top.
 *
 * @par Thread Safety
 *
 * Fully thread-safe.  All push, pop, and query methods are
 * internally synchronized via a Mutex.  Push methods wake any
 * thread blocked in @c popWait() after writing.  This allows a
 * producer thread and a consumer thread to operate on the same
 * AudioBuffer without external locking.
 *
 * @par Capacity and ownership
 *
 * The buffer is sized via @c reserve() (or constructor). Pushes that
 * would exceed capacity return @c Error::NoSpace — the caller is
 * expected to size the buffer for their expected peak residency. The
 * class is move-only; copying a FIFO is almost always a mistake.
 *
 * @par Example
 * @code
 * AudioBuffer fifo(AudioDesc(AudioFormat::PCMI_S16LE, 48000, 2));
 * fifo.reserve(48000);                    // 1 second of headroom
 * fifo.setInputFormat(AudioFormat::NativeFloat, 48000, 2);
 *
 * // Producer thread
 * Audio chunk(nativeDesc, 1602);
 * // ... fill chunk ...
 * fifo.push(chunk);
 *
 * // Consumer thread — blocks until 1600 samples are ready
 * Audio slice(outputDesc, 1600);
 * auto [got, err] = fifo.popWait(slice, 1600, 200);
 * if(err == Error::Timeout) { ... }
 * @endcode
 */
class AudioBuffer {
        public:
                /** @brief Result type for pop operations: {sampleCount, Error}. */
                using PopResult = Result<size_t>;

                /** @brief Default-constructs an invalid AudioBuffer with no format. */
                AudioBuffer() = default;

                /** @brief Constructs an AudioBuffer with the given storage format. */
                explicit AudioBuffer(const AudioDesc &format);

                /** @brief Constructs an AudioBuffer with storage format and reserved capacity. */
                AudioBuffer(const AudioDesc &format, size_t capacity);

                AudioBuffer(const AudioBuffer &) = delete;
                AudioBuffer &operator=(const AudioBuffer &) = delete;

                /** @brief Move constructor. */
                AudioBuffer(AudioBuffer &&other) noexcept;

                /** @brief Move assignment. */
                AudioBuffer &operator=(AudioBuffer &&other) noexcept;

                /** @brief Destructor. */
                ~AudioBuffer() = default;

                /** @brief Returns true if a valid storage format is set. */
                bool isValid() const { return _format.isValid(); }

                /**
                 * @brief Returns a snapshot of the storage (output) format.
                 *
                 * The audio format, sample rate, and channel count are
                 * fixed at construction (or by @ref setFormat /
                 * @ref setChannels), but the descriptor's
                 * @ref AudioChannelMap is refreshed on every push that
                 * delivers a different stream/role assignment than the
                 * one currently cached.  Each successful push copies
                 * the input's per-channel @c (stream, role) pairs into
                 * the output map according to the active channel
                 * remap; @ref format only re-publishes the descriptor
                 * when that copy actually changes the cached map, so
                 * steady-state callers observe a stable snapshot
                 * almost all the time.
                 *
                 * Returned by value under the buffer's mutex so reads
                 * are safe to make from any thread, including
                 * concurrently with a producer's @ref push.
                 */
                AudioDesc format() const {
                        Mutex::Locker lock(_mutex);
                        return _format;
                }

                /**
                 * @brief Sets the storage format. Clears any buffered samples.
                 * @param format The new storage format.
                 */
                void setFormat(const AudioDesc &format);

                /** @brief Returns the expected input format for @c push(). */
                const AudioDesc &inputFormat() const { return _inputFormat; }

                /**
                 * @brief Sets the expected input format for @c push().
                 *
                 * If @p input differs from @c format() only in @c DataType,
                 * @c push() converts on-the-fly via @c samplesToFloat /
                 * @c floatToSamples.  If the sample rate differs and
                 * @c PROMEKI_ENABLE_SRC is available, @c push() resamples
                 * transparently.  If the channel count differs, @c push()
                 * returns @c NotSupported (channel-map hook TBD).
                 *
                 * Changing the input format resets any internal resampler
                 * state so filter history from the previous format does
                 * not bleed through.
                 */
                void setInputFormat(const AudioDesc &input);

                /**
                 * @brief Sets the resampler quality for sample-rate conversion on push.
                 *
                 * Controls the quality mode used for both fixed rate
                 * conversion and drift correction.  The resampler is
                 * lazily created on the first push that needs it.
                 * Calling this method resets any existing resampler state.
                 *
                 * @param quality The SrcQuality mode to use.
                 * @return Error::Ok on success, Error::NotSupported if
                 *         PROMEKI_ENABLE_SRC is off.
                 */
                Error setResamplerQuality(const SrcQuality &quality);

                /**
                 * @brief Enables clock-drift correction on push.
                 *
                 * When enabled, the buffer dynamically adjusts the
                 * resampling ratio on each push() to steer the fill
                 * level toward @p targetSamples.  This compensates for
                 * clock drift between a producer and consumer running
                 * on independent oscillators.
                 *
                 * Drift correction works for both same-rate and
                 * different-rate configurations.  When rates are the
                 * same, the resampler is created with a nominal ratio
                 * of 1.0 and only the drift adjustment moves it.
                 *
                 * @param targetSamples Desired steady-state fill level
                 *        in samples.  A good default is half the
                 *        reserved capacity.
                 * @param gain Proportional gain for the correction.
                 *        Small values (0.001 – 0.01) give smooth,
                 *        inaudible corrections.  Larger values track
                 *        drift faster but may cause audible pitch
                 *        shifts.  Default: 0.001.
                 * @return Error::Ok on success, Error::NotSupported if
                 *         PROMEKI_ENABLE_SRC is off.
                 */
                Error enableDriftCorrection(size_t targetSamples, double gain = 0.001);

                /**
                 * @brief Disables clock-drift correction.
                 *
                 * The internal resampler is reset but kept alive if
                 * the input and output sample rates still differ
                 * (fixed rate conversion continues).  If rates are
                 * the same, the resampler is no longer used until
                 * drift correction is re-enabled.
                 *
                 * When PROMEKI_ENABLE_SRC is off this is a no-op.
                 */
                void disableDriftCorrection();

                /**
                 * @brief Returns true if drift correction is currently enabled.
                 *
                 * Always returns false when PROMEKI_ENABLE_SRC is off.
                 */
                bool driftCorrectionEnabled() const;

                /**
                 * @brief Returns the current effective resampling ratio.
                 *
                 * When drift correction is active this reflects the
                 * most recent drift-adjusted ratio.  When only fixed
                 * rate conversion is in use this is the nominal ratio
                 * (outputRate / inputRate).  Returns 1.0 when no
                 * resampler is active or PROMEKI_ENABLE_SRC is off.
                 */
                double driftRatio() const;

                /**
                 * @brief Ensures capacity for at least @p samples samples.
                 *
                 * If the current capacity is already sufficient this is a
                 * no-op. Growing the buffer linearizes any current contents
                 * (moving head to index 0).
                 */
                Error reserve(size_t samples);

                /** @brief Returns the current capacity in samples. */
                size_t capacity() const {
                        Mutex::Locker lock(_mutex);
                        return _capacity;
                }

                /** @brief Returns the number of samples currently buffered. */
                size_t available() const {
                        Mutex::Locker lock(_mutex);
                        return _count;
                }

                /** @brief Returns the free capacity in samples. */
                size_t free() const {
                        Mutex::Locker lock(_mutex);
                        return _capacity - _count;
                }

                /** @brief Returns true if no samples are buffered. */
                bool isEmpty() const {
                        Mutex::Locker lock(_mutex);
                        return _count == 0;
                }

                /** @brief Returns true if the buffer is full. */
                bool isFull() const {
                        Mutex::Locker lock(_mutex);
                        return _count >= _capacity;
                }

                /** @brief Clears all buffered samples. */
                void clear();

                /**
                 * @brief Sets per-output-channel linear gains.
                 *
                 * @p gains.size() must equal @c format().channels().
                 * Pass an empty list to disable (gain = 1.0 on every
                 * channel).
                 *
                 * Gains are applied in the float domain after channel
                 * remap and before metering.  Any non-1.0 gain forces
                 * the via-float push path even when a direct integer
                 * fastpath would otherwise be available.
                 *
                 * @param gains One linear gain per output channel.
                 * @return @c Error::Ok or @c Error::InvalidArgument
                 *         on length mismatch.
                 */
                Error setChannelGains(const List<float> &gains);

                /** @brief Returns the active per-channel gains (empty when all 1.0). */
                List<float> channelGains() const;

                /**
                 * @brief Sets a per-output-channel source-channel remap.
                 *
                 * @p remap.size() must equal @c format().channels().
                 * Each entry is the input channel index feeding that
                 * output channel, or @c -1 to fill the output with
                 * silence.  Pass an empty list to disable (identity
                 * mapping: output[i] comes from input[i]).
                 *
                 * Activating a remap also forces the via-float path on
                 * push, and lifts the requirement that the push input
                 * channel count match the buffer's output count — the
                 * remap explains the mapping.
                 *
                 * The remap operates in the float domain.  It does
                 * @b not change @c format().channels() or the output's
                 * @ref AudioChannelMap; call
                 * @c format().setChannelMap() separately if the new
                 * channel order should also be reflected in the
                 * descriptor returned by @ref format().
                 *
                 * @param remap Source channel index per output channel.
                 * @return @c Error::Ok or @c Error::InvalidArgument
                 *         on length mismatch.
                 */
                Error setChannelRemap(const List<int> &remap);

                /** @brief Returns the active channel remap (empty when identity). */
                List<int> channelRemap() const;

                /**
                 * @brief Installs (or clears) the metering callback.
                 *
                 * The meter receives every sample on the via-float
                 * processing path — i.e. every push that goes through
                 * remap, gain, or sample-rate conversion.  Activating
                 * a meter forces the via-float path even when an
                 * integer fastpath would otherwise be available, so
                 * the meter sees a consistent stream of post-processing
                 * float samples.
                 *
                 * Pass @c nullptr to remove the current meter.
                 *
                 * @param meter Pluggable meter, or @c nullptr.
                 */
                void setMeter(AudioMeter::Ptr meter);

                /** @brief Returns the active meter, or null if none is installed. */
                AudioMeter::Ptr meter() const;

                /**
                 * @brief Pushes interleaved raw samples into the buffer.
                 *
                 * @param data       Pointer to @p samples x bytes-per-sample bytes.
                 * @param samples    Number of samples (not bytes) in @p data.
                 * @param srcFormat  Format of @p data.
                 * @return Error::Ok, NoSpace, NotSupported, or InvalidArgument.
                 */
                Error push(const void *data, size_t samples, const AudioDesc &srcFormat);

                /**
                 * @brief Pops up to @p samples samples into a raw buffer.
                 *
                 * Non-blocking: returns immediately with whatever is
                 * available (which may be 0).
                 *
                 * @param dst     Destination buffer, must be at least
                 *                @p samples x bytes-per-sample bytes.
                 * @param samples Number of samples to pop.
                 * @return {count, Error::Ok}.
                 */
                PopResult pop(void *dst, size_t samples);

                /**
                 * @brief Blocks until @p samples samples are available, then pops into a raw buffer.
                 *
                 * @param dst       Destination buffer.
                 * @param samples   Number of samples to wait for and pop.
                 * @param timeoutMs Maximum wait in milliseconds (0 = indefinite).
                 * @return {samples, Error::Ok} on success, or
                 *         {0, Error::Timeout} if the wait expired.
                 */
                PopResult popWait(void *dst, size_t samples, unsigned int timeoutMs = 0);

                /**
                 * @brief Peeks at the next @p samples samples without consuming.
                 * @return {count, Error::Ok}.
                 */
                PopResult peek(void *dst, size_t samples) const;

                /**
                 * @brief Drops the next @p samples samples without copying.
                 * @return {count, Error::Ok}.
                 */
                PopResult drop(size_t samples);

        private:
                /** @brief Bytes per sample frame (all channels combined). */
                size_t bytesPerSample() const;

                /** @brief Writes @p samples samples (already in storage format) starting at _tail. */
                void writeBytesAtTail(const uint8_t *data, size_t samples);

                /** @brief Reads @p samples samples from @p startSample into @p dst. */
                void readBytesFromHead(uint8_t *dst, size_t samples, size_t skip) const;

                // Lock-free internals called with _mutex already held.
                Error  pushLocked(const void *data, size_t samples, const AudioDesc &srcFormat);
                size_t popLocked(void *dst, size_t samples);

                mutable Mutex _mutex;
                WaitCondition _cv;

                AudioDesc       _format;
                AudioDesc       _inputFormat;
                Buffer          _storage;
                size_t          _capacity = 0;
                size_t          _head = 0;  ///< Next sample index to pop (mod _capacity).
                size_t          _tail = 0;  ///< Next sample index to push (mod _capacity).
                size_t          _count = 0; ///< Currently buffered samples.
                List<float>     _gains;     ///< Per-output-channel linear gain (empty = all 1.0).
                List<int>       _remap;     ///< Per-output-channel source-channel index (empty = identity).
                AudioMeter::Ptr _meter;     ///< Optional metering observer.

                /** @brief Returns true when remap, gain, or meter forces the via-float path. */
                bool needsFloatProcessing() const { return !_remap.isEmpty() || !_gains.isEmpty() || _meter.isValid(); }

#if PROMEKI_ENABLE_SRC
                SrcQuality     _resamplerQuality;
                AudioResampler _resampler;
                bool           _driftEnabled = false;
                size_t         _driftTarget = 0;
                double         _driftGain = 0.001;
                double         _driftRatio = 1.0;    ///< Last applied ratio.
                double         _driftIntegral = 0.0; ///< Accumulated error for I term.

                /** @brief Resamples native-float data and writes the result to the ring. */
                Error resampleAndPush(const float *nativeData, size_t samples);

                /** @brief Returns true if the resampler should be used for this push. */
                bool needsResampler(const AudioDesc &srcFormat) const;

                /** @brief Computes the effective ratio (nominal + drift adjustment). */
                double computeRatio(const AudioDesc &srcFormat);
#endif
};

PROMEKI_NAMESPACE_END
