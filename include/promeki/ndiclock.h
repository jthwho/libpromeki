/**
 * @file      ndiclock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <atomic>
#include <promeki/clock.h>
#include <promeki/clockdomain.h>
#include <promeki/framerate.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-stream @ref Clock backed by NDI per-frame timestamps.
 * @ingroup proav
 *
 * NDI's `NDIlib_video_frame_v2_t::timestamp` field carries an
 * absolute time-of-frame value in 100-nanosecond intervals,
 * generated on the sender side.  When the sender's host clock is
 * synchronised (NTP / PTP / GPS), every receiver in the network
 * sees the same per-frame timestamps for the same source — so the
 * value is a reliable cross-machine clock for downstream
 * timestamping and synchronisation.
 *
 * @par Lifecycle
 *
 * The clock is created by @ref NdiMediaIO at @c openSource time and
 * tied to the source-side @ref MediaIOPortGroup.  The capture thread
 * calls @ref setLatestTimestamp on every received video frame; the
 * clock's @ref raw is the most-recent timestamp, converted from NDI's
 * 100ns ticks to nanoseconds.
 *
 * Until the first frame arrives @ref raw returns a zero timestamp
 * with @c Error::Ok.  Returning an error here would force every
 * Clock consumer to handle "not yet" specially even though the
 * pipeline is still bringing up — zero with the resolution caveat is
 * a more usable bring-up signal.
 *
 * @par Domain
 *
 * Registered once per process under the name @c "Ndi" with epoch
 * @ref ClockEpoch::Absolute — NDI timestamps are wall-clock-anchored
 * 100ns ticks, so cross-stream and (with NTP/PTP) cross-machine
 * subtraction is meaningful.
 *
 * @par sleepUntilNs
 *
 * Blocks on the internal wait condition until the stream's most
 * recent timestamp is at or past the deadline (or until the clock
 * is shut down).  Useful for downstream stages that want to wait
 * for the next NDI tick.
 *
 * @par Thread Safety
 *
 * Fully thread-safe.  @ref setLatestTimestamp is intended to be
 * called from the NDI capture thread; @ref raw, @ref jitter, and
 * @ref sleepUntilNs may be called from any thread.
 */
class NdiClock : public Clock {
        public:
                /**
                 * @brief Returns the project-wide NDI ClockDomain ID.
                 *
                 * Lazily registers the @c "Ndi" domain on first call.
                 * Every NdiClock instance shares this domain.
                 */
                static const ClockDomain &domain();

                /**
                 * @brief Constructs an NdiClock with an optional frame-rate hint for jitter.
                 *
                 * @param frameRate Optional frame rate used to size the
                 *                  reported jitter envelope (± half a
                 *                  frame period).  When invalid the
                 *                  clock falls back to a wide envelope
                 *                  (± 50 ms) — better safe than overly
                 *                  precise when downstream consumers
                 *                  may not check.
                 */
                explicit NdiClock(const FrameRate &frameRate = FrameRate());

                ~NdiClock() override;

                /**
                 * @brief Updates the latest-frame timestamp from the capture thread.
                 *
                 * @param ndiTimestampTicks NDI's `timestamp` value in
                 *                          100-nanosecond intervals.
                 *                          Pass @c
                 *                          NDIlib_recv_timestamp_undefined
                 *                          unchanged — the clock will
                 *                          ignore the call.
                 */
                void setLatestTimestamp(int64_t ndiTimestampTicks);

                /**
                 * @brief Updates the frame-rate hint used for @ref jitter.
                 *
                 * Called by the capture thread the first time a frame
                 * with a non-default frame_rate_N/D arrives.  Cheap
                 * when the rate is unchanged.
                 */
                void setFrameRate(const FrameRate &frameRate);

                int64_t     resolutionNs() const override;
                ClockJitter jitter() const override;

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;

        private:
                // Atomic backing the fast-path raw() / setLatestTimestamp pair.
                // Stored in nanoseconds so raw() does not multiply on every
                // call — a hot path for downstream timing math.
                mutable std::atomic<int64_t> _lastTimestampNs{0};
                std::atomic<bool>            _hasTimestamp{false};
                std::atomic<bool>            _shutdown{false};

                // Wait condition for sleepUntilNs.  Signalled on every
                // setLatestTimestamp so the longest-waiter can wake
                // and recheck.  The mutex guards _frameRate too.
                mutable Mutex         _waitMutex;
                mutable WaitCondition _waitCond;
                FrameRate             _frameRate;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
