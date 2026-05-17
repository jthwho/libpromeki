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
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class Ntv2Device;

/**
 * @brief Per-device sample-counter clock for an AJA card.
 * @ingroup proav
 *
 * AJA cards expose an onboard audio sample counter that is
 * physically clocked by the same reference driving the card's SDI
 * outputs and audio system.  When a card is genlocked to an
 * external reference, the counter is too.  That makes it the best
 * timing reference the host can read against the card:
 *
 *  - **Sub-microsecond resolution** — one tick per audio sample
 *    (~20.83 µs at 48 kHz).
 *  - **Continuous** — ticks monotonically between VBIs, so sub-frame
 *    stamps remain meaningful.
 *  - **Shared across channels on the same card** — every channel on
 *    the device reads the same counter, so cross-channel timestamps
 *    from one card share an epoch by construction.
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
 * @par VBI fallback mode
 *
 * When no audio system is reserved on the device (or the card has
 * no audio capture counter at all — playback-only cards), the
 * clock constructs in @c vbiFallback mode: @ref raw returns the
 * host-side wall time at the last @c SubscribeInputVerticalEvent
 * wake (one tick per frame period); @ref resolutionNs reports one
 * frame period; @ref jitter widens to ± half a frame period.  The
 * mode auto-locks once at construction time — there is no runtime
 * promotion to sample-counter mode if a later channel turns on an
 * audio system, by design (consumers that have already latched
 * onto the lower-resolution clock get monotonic stamps for free,
 * and silently upgrading the resolution would break that
 * invariant).
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
                 * @param audioSystem 1-based audio system index whose
                 *                    counter the clock reads, or 0 for
                 *                    VBI-fallback mode.
                 * @param vbiFallback @c true to use VBI fallback mode
                 *                    even when @p audioSystem is
                 *                    non-zero.  The constructor sets
                 *                    this to @c true unconditionally
                 *                    when @p audioSystem is 0 or when
                 *                    the device has no audio counter.
                 */
                Ntv2DeviceClock(Ntv2Device *device, int audioSystem, bool vbiFallback);

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
                 * Used to convert sample counts to nanoseconds in
                 * sample-counter mode.  Defaults to 48000.0; backends
                 * call this from @c openSource / @c openSink when the
                 * audio system is configured at a non-default rate.
                 */
                void setSampleRate(float sampleRateHz);

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
                 * Replaces the SDK-driven counter source with a
                 * caller-supplied callback returning the current 32-bit
                 * counter value.  Returning @c false means "counter
                 * unavailable" — the next @ref raw call propagates
                 * @c Error::DeviceError.
                 *
                 * Intended exclusively for unit tests that need to
                 * verify the 32 → 64-bit wrap extension without
                 * hardware.  Production code should never call this.
                 */
                void setCounterSourceForTest(bool (*fn)(uint32_t *out, void *ctx), void *ctx);

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;

        private:
                // Test-only constructor — takes the ClockDomain directly so
                // we can build a clock without an Ntv2Device handle.
                Ntv2DeviceClock(const ClockDomain &domain, bool vbiFallback);

                int64_t sampleTicksToNs(uint64_t ticks) const;

                Ntv2Device *_device       = nullptr;
                int         _audioSystem  = 0;
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
                // production — production reads CNTV2Card::ReadAudioLastIn.
                bool (*_testCounterFn)(uint32_t *, void *) = nullptr;
                void *_testCounterCtx                       = nullptr;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
