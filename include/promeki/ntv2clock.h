/**
 * @file      ntv2clock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/atomic.h>
#include <promeki/clock.h>
#include <promeki/clockdomain.h>
#include <promeki/framerate.h>
#include <promeki/mediatimestamp.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class Ntv2Device;

/**
 * @brief Per-device sample-counter clock for an AJA card.
 * @ingroup proav
 *
 * AJA cards expose an onboard, free-running 48 kHz audio sample
 * counter (FPGA register @c kRegAud1Counter, accessed through
 * @c CNTV2Card::GetRawAudioTimer) that is physically clocked by the
 * same reference driving the card's SDI outputs and audio systems.
 * When a card is genlocked to an external reference, the counter
 * is too.  That makes it the best timing reference the host can
 * read against the card:
 *
 *  - **Sub-microsecond resolution** — one tick per audio sample
 *    (~20.83 µs at 48 kHz).
 *  - **Continuous** — ticks monotonically between VBIs, so sub-frame
 *    stamps remain meaningful.
 *  - **Shared across channels on the same card** — every channel on
 *    the device reads the same single FPGA counter, so cross-channel
 *    timestamps from one card share an epoch by construction.
 *  - **Always available** — the counter starts at zero on FPGA load
 *    at power-up and ticks regardless of whether any audio system is
 *    enabled.  No audio-system reservation is required to use it.
 *  - **Matches the AJA driver's per-frame stamp** — the same FPGA
 *    register is what the driver pre-extends and reports in
 *    @c FRAME_STAMP::acAudioClockTimeStamp on every captured frame,
 *    so per-frame PTS built from the FRAME_STAMP value lands in the
 *    same time base as this clock's @c now().
 *
 * One @ref Ntv2DeviceClock is constructed lazily by
 * @ref Ntv2Device::sampleClock and shared across every
 * @ref Ntv2MediaIO that opens a channel on the same card.  The
 * shared instance is what gives @c portGroup_A->clock() ==
 * @c portGroup_B->clock() == @c true for two channels on one card.
 *
 * @par 32 → 64-bit wrap extension
 *
 * The AJA register is 32-bit and wraps every ~24.8 h at 48 kHz.
 * @ref raw extends it to 64-bit by detecting wrap on every read
 * and incrementing a shadow high word.  Reads are serialised by
 * the clock's internal mutex so the wrap detection is race-free
 * across concurrent readers.
 *
 * @par Per-acquire epoch — stamps do not round-trip across close/reopen
 *
 * The FPGA register is free-running across process restarts, but
 * the 64-bit shadow is local to this clock instance and resets to
 * zero on every @ref Ntv2DeviceRegistry::acquire.  That means the
 * @c MediaTimeStamp values this clock produces have a @em per-acquire
 * @em epoch — they are coherent across every channel on the same
 * card for the lifetime of the acquisition, but a process that
 * closes the device and reopens it will see the next session start
 * from a fresh zero shadow rather than continuing from the previous
 * session's last value.
 *
 * The trade-off is monotonicity.  Anchoring the shadow to host wall
 * time on first read would let stamps round-trip across close /
 * reopen — at the cost of breaking the monotonic-clamp contract on
 * long-running processes (any wall-time jump would back-step the
 * stamp).  The choice here favours the in-pipeline timing guarantee.
 * Callers that need to persist stamps across an acquire boundary
 * should snapshot the (host-wall, device-stamp) pair before close
 * and re-anchor on reopen.
 *
 * @par VBI fallback mode
 *
 * When the card has no FPGA audio counter at all (no NTV2 hardware
 * shipped to date is in this category, but the cap check guards
 * against future ones), or the caller explicitly forces it via the
 * @c vbiFallback constructor flag, the clock falls back to host-
 * side ticks: @ref raw returns the host-monotonic timestamp at the
 * last @c SubscribeInputVerticalEvent wake (one tick per frame
 * period); @ref resolutionNs reports one frame period; @ref jitter
 * widens to ± half a frame period.  Mode is latched at construction
 * — there is no runtime promotion path (silently upgrading the
 * resolution mid-stream would break the monotonic-clamp contract
 * for consumers already latched onto the lower-resolution clock).
 *
 * @par Domain
 *
 * Each device's clock lives in its own per-device
 * @ref ClockDomain: @c "ntv2:N" for index-based acquisition,
 * @c "ntv2:serial:SERIAL" when the card exposes a serial number.
 * Cross-card timestamps therefore go through
 * @ref ClockDomain::isCrossStreamComparable as usual.
 *
 * @par Thread safety
 *
 * Fully thread-safe.  @ref raw and @ref sleepUntilNs are safe to
 * call from any thread.  The capture / playout worker updates the
 * VBI-tick estimate via @ref noteVbi when in fallback mode.
 */
class Ntv2DeviceClock : public Clock {
        public:
                /**
                 * @brief Returns the @ref ClockDomain for @p device.
                 *
                 * Lazily registers a per-device domain
                 * (@c "ntv2:<key>") on first call; subsequent calls
                 * return the same domain.  Domain identity is keyed off
                 * @ref Ntv2Device::key so cross-card stamps stay
                 * distinguishable even when both cards happen to be at
                 * the same sample tick.
                 */
                static const ClockDomain &domainFor(const Ntv2Device &device);

                /**
                 * @brief Constructs the clock.
                 *
                 * @param device      Non-owning back-pointer to the
                 *                    device.  Must outlive the clock
                 *                    — the registry guarantees that
                 *                    by tearing the clock down before
                 *                    the device shuts down.
                 * @param vbiFallback @c true to force VBI-fallback
                 *                    mode even when the device exposes
                 *                    a usable FPGA audio counter.  The
                 *                    constructor also forces fallback
                 *                    when the device has no audio
                 *                    counter (cap negative).
                 */
                Ntv2DeviceClock(Ntv2Device *device, bool vbiFallback);

                ~Ntv2DeviceClock() override;

                /**
                 * @brief Test factory — builds a clock without an
                 *        attached @ref Ntv2Device.
                 *
                 * Used by the doctest unit tests to verify the 32 →
                 * 64-bit wrap extension and the VBI fallback logic
                 * without instantiating a real card.  The returned
                 * clock has no device back-pointer; the caller must
                 * install a counter source via
                 * @ref setCounterSourceForTest before invoking
                 * @c raw() / @c nowNs.  Production code must not call
                 * this.
                 *
                 * @param testDomainName Unique name used to register a
                 *                       per-test @ref ClockDomain.
                 *                       Tests should pass distinct
                 *                       names so domains don't bleed
                 *                       across cases.
                 * @param vbiFallback    @c true to build in VBI mode
                 *                       (raw counter source unused).
                 */
                static Ntv2DeviceClock *createForTest(const String &testDomainName, bool vbiFallback);

                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;

                /**
                 * @brief Long-window estimate of device-clock rate vs.
                 *        host wall clock.
                 *
                 * Defaults to @c 1.0 until enough wall time has
                 * elapsed for a meaningful estimate (see
                 * @c kRateBaselineMinWindowNs).  Beyond that, returns
                 * a slowly-tracking average of
                 * @c (counterNs_now - counterNs_baseline)
                 * @c / (wallNs_now - wallNs_baseline) — a value
                 * greater than one means the device counter is
                 * advancing faster than the host wall clock and a
                 * value less than one means the inverse.  Downstream
                 * drift-correcting consumers (audio resamplers,
                 * frame-syncs) read this to pull the true rate
                 * instead of measuring it themselves.
                 *
                 * VBI-fallback mode always returns @c 1.0 — the
                 * "device" and host clocks are identical in that
                 * configuration, so there is no drift to report.
                 *
                 * Thread-safe — the published ratio is stored in an
                 * @ref Atomic so concurrent readers never race the
                 * update.  Each call is O(1).
                 */
                double      rateRatio() const override;

                /**
                 * @brief Updates the VBI-tick estimate in VBI-fallback mode.
                 *
                 * Called by the capture / playout worker from
                 * @c WaitForInputVerticalInterrupt to advance the
                 * host-side per-frame anchor.  No-op outside VBI
                 * fallback mode.
                 *
                 * @param now Host monotonic-clock @ref TimeStamp at the
                 *            VBI event.  Typically @c TimeStamp::now()
                 *            captured immediately after the
                 *            @c WaitForInputVerticalInterrupt wake.
                 */
                void noteVbi(const TimeStamp &now);

                /**
                 * @brief Sets the assumed audio sample rate.
                 *
                 * Used to convert FPGA-counter ticks to nanoseconds in
                 * sample-counter mode.  Defaults to 48000.0 — the
                 * hardware-fixed rate of @c kRegAud1Counter.  Backends
                 * normally leave this alone; the setter exists for
                 * future cards that may run a different audio-clock
                 * rate.
                 */
                void setSampleRate(float sampleRateHz);

                /**
                 * @brief Wraps a raw FPGA-counter sample count in a
                 *        @ref MediaTimeStamp tagged with this clock's
                 *        domain.
                 *
                 * The capture loop pulls
                 * @c FRAME_STAMP::acAudioClockTimeStamp — a 64-bit
                 * driver-extended sample count captured at the VBI when
                 * the frame started ingesting — and converts it via
                 * this helper.  Result lands in the same time base as
                 * @c now() because both sources are the same FPGA
                 * register (@c kRegAud1Counter), so the per-frame PTS
                 * is directly comparable with stamps taken at arbitrary
                 * later points by @c now().
                 *
                 * In VBI-fallback mode this still does the arithmetic
                 * conversion, but the returned stamp is not in the
                 * clock's authoritative time base (which is host-wall
                 * ticks in that mode); callers should prefer
                 * @c now() under fallback.  The conversion uses the
                 * sample rate set via @ref setSampleRate.
                 */
                MediaTimeStamp mediaTimeStampFromSamples(uint64_t samples) const;

                /**
                 * @brief Sets the frame-rate hint used by @ref jitter
                 *        in VBI fallback mode.
                 *
                 * No-op in sample-counter mode (where jitter is bounded
                 * by the sample period, not the frame period).
                 */
                void setFrameRate(const FrameRate &frameRate);

                // ---- Test seam ----

                /**
                 * @brief Injects a custom raw-counter reader.
                 *
                 * Replaces @c CNTV2Card::GetRawAudioTimer with a
                 * caller-supplied callback returning the current 32-bit
                 * counter value.  Returning @c false means "counter
                 * unavailable" — the next @ref raw call propagates
                 * @c Error::DeviceError.
                 *
                 * Intended exclusively for unit tests that need to
                 * verify the 32 → 64-bit wrap extension and the
                 * sample-to-ns conversion without hardware.  Production
                 * code should never call this.
                 */
                void setCounterSourceForTest(bool (*fn)(uint32_t *out, void *ctx), void *ctx);

                /**
                 * @brief Injects a custom wall-time source for the
                 *        drift estimator.
                 *
                 * Replaces @c TimeStamp::now() inside @ref raw's drift-
                 * tracking branch with a caller-supplied callback
                 * returning the current host monotonic nanoseconds.
                 * Lets unit tests synthesise the wall + counter
                 * advance pairs that drive @ref rateRatio without
                 * waiting on real wall time.
                 *
                 * Intended exclusively for unit tests; production code
                 * must not call this.
                 */
                void setWallTimeSourceForTest(int64_t (*fn)(void *ctx), void *ctx);

                /**
                 * @brief Minimum wall-time window before @ref rateRatio
                 *        publishes a real estimate.
                 *
                 * Set to 5 seconds — long enough that crystal-vs-
                 * crystal drift (typically tens of ppm) shows above
                 * the measurement floor, short enough that downstream
                 * consumers see the estimate stabilise within a
                 * normal session.  Tests can wait less time by
                 * driving the wall-time source forward via
                 * @ref setWallTimeSourceForTest.
                 */
                static constexpr int64_t kRateBaselineMinWindowNs = 5'000'000'000LL;

                /**
                 * @brief Minimum gap between rate-estimate updates
                 *        once the baseline has stabilised.
                 *
                 * Updating more often than once a second burns
                 * mutexes without improving the estimate (the device
                 * counter is a stable FPGA oscillator and the drift
                 * is sub-Hz).
                 */
                static constexpr int64_t kRateUpdateIntervalNs = 1'000'000'000LL;

                /**
                 * @brief LPF blend coefficient (×1000 fixed point) for
                 *        the published rate ratio.
                 *
                 * Each update mixes the measured ratio in at
                 * @c kRateLpfAlphaPer1000 / 1000.  Default 100 (10%)
                 * — gives a ~10-sample time constant against the
                 * 1-Hz update cadence, so meaningful changes show up
                 * within ~10 s.
                 */
                static constexpr int _kRateLpfAlphaPer1000 = 100;

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;

        private:
                // Test-only constructor — takes the ClockDomain directly so
                // we can build a clock without an Ntv2Device handle.
                Ntv2DeviceClock(const ClockDomain &domain, bool vbiFallback);

                int64_t sampleTicksToNs(uint64_t ticks) const;

                Ntv2Device *_device       = nullptr;
                bool        _vbiFallback  = false;

                // Sample rate in Hz; used to convert counter → ns.
                // Mutated via setSampleRate which takes _mutex.
                float _sampleRateHz = 48000.0f;

                // 32 → 64 wrap shadow.  _highBits and _lastLow are
                // updated under _mutex on every raw() read.
                mutable Mutex        _mutex;
                mutable uint32_t     _lastLow  = 0;
                mutable uint64_t     _highBits = 0;
                mutable bool         _hasShadow = false;

                // VBI-fallback last-wake tick (host monotonic ns).
                mutable Atomic<int64_t> _lastVbiNs{0};

                // Jitter sizing in VBI mode.
                FrameRate _frameRate;

                // Optional test counter source (set via
                // setCounterSourceForTest).  Always nullptr in
                // production — production reads CNTV2Card::GetRawAudioTimer.
                bool (*_testCounterFn)(uint32_t *, void *) = nullptr;
                void *_testCounterCtx                       = nullptr;

                // Optional test wall-time source (set via
                // setWallTimeSourceForTest).  Production reads
                // TimeStamp::now().nanoseconds() inside the drift
                // estimator.
                int64_t (*_testWallFn)(void *) = nullptr;
                void *_testWallCtx              = nullptr;

                // Drift estimator state.  All mutated from raw()
                // under _mutex; rateRatio() reads _ratePpb lock-free
                // via Atomic.  PPB = parts per billion; 1e9 ≡ 1.0.
                mutable Atomic<int64_t> _ratePpb{1'000'000'000};
                mutable bool            _rateBaselineValid     = false;
                mutable int64_t         _rateBaselineWallNs    = 0;
                mutable int64_t         _rateBaselineCounterNs = 0;
                mutable int64_t         _lastRateUpdateWallNs  = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
