/**
 * @file      ratetracker.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/mutex.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Sliding-window rate counter for live telemetry.
 * @ingroup proav
 *
 * RateTracker accumulates frame and byte counts over a short rolling
 * window and exposes them as smoothed rates.  It is designed for the
 * MediaIO "live telemetry" path (see @ref MediaIOStats::BytesPerSecond
 * and @ref MediaIOStats::FramesPerSecond): cheap enough to call on
 * every frame, queryable lazily by status displays like
 * @c mediaplay @c --stats.
 *
 * @par How it works
 *
 * The tracker holds one current window plus a snapshot of the last
 * completed window.  @ref record is lock-free and only does an atomic
 * fetch_add on the two counters.  Query functions
 * (@ref bytesPerSecond / @ref framesPerSecond) take a small mutex and
 * check whether the current window has aged past @ref windowMs; if so
 * they promote it to the "last" snapshot and reset the counters.  The
 * value returned is:
 *
 * - the finished last-window rate while the current window is still
 *   short (< half the window), so that a freshly-reset counter does not
 *   report zero;
 * - the current-window rate once enough data has accumulated;
 * - zero until at least one window has ever been observed.
 *
 * The window is not resampled on every query — rotation happens only
 * when the elapsed time crosses the full window boundary — so repeated
 * queries inside a window see a stable value.
 *
 * @par Thread safety
 *
 * @ref record is safe to call from any thread without coordination.
 * Query functions are also safe but serialize on an internal mutex.
 * @ref reset must not race with in-flight @ref record calls.
 */
class RateTracker {
        public:
                /** @brief Default rolling window size in milliseconds. */
                static constexpr int64_t kDefaultWindowMs = 5000;

                /**
                 * @brief Constructs a tracker with the given window length.
                 * @param windowMs The window length in milliseconds (>= 1).
                 */
                explicit RateTracker(int64_t windowMs = kDefaultWindowMs);

                RateTracker(const RateTracker &) = delete;
                RateTracker &operator=(const RateTracker &) = delete;
                RateTracker(RateTracker &&) = delete;
                RateTracker &operator=(RateTracker &&) = delete;

                /**
                 * @brief Records a frame of the given size in bytes.
                 *
                 * Atomically increments the frame counter and adds the byte
                 * count to the byte counter.  Lock-free.  May be called
                 * from any thread.
                 *
                 * @param bytes The frame size in bytes.
                 */
                void record(int64_t bytes);

                /**
                 * @brief Returns the current bytes-per-second rate.
                 *
                 * Takes the internal mutex; rotates the window if it has
                 * aged past @ref windowMs.  Returns 0.0 before any data
                 * has been recorded.
                 *
                 * @return The smoothed byte rate.
                 */
                double bytesPerSecond() const;

                /**
                 * @brief Returns the current frames-per-second rate.
                 *
                 * Same semantics as @ref bytesPerSecond but for the frame
                 * counter.
                 *
                 * @return The smoothed frame rate.
                 */
                double framesPerSecond() const;

                /**
                 * @brief Resets all counters and the window.
                 *
                 * After this returns both rate accessors report 0.0 until
                 * new data is recorded.  Must not race with @ref record.
                 */
                void reset();

                /** @brief Returns the window length in milliseconds. */
                int64_t windowMs() const { return _windowMs; }

        private:
                /**
                 * @brief Rotates the window if it has aged out.
                 *
                 * Called under @c _mutex by every query.  Copies the
                 * current counter snapshot into @c _lastBytes / @c _lastFrames
                 * and resets the current window when @c elapsed >= @c _windowMs.
                 *
                 * @param nowNs The current monotonic timestamp in ns.
                 */
                void rotateIfStale(int64_t nowNs) const;

                int64_t                 _windowMs;
                mutable Atomic<int64_t> _bytes;
                mutable Atomic<int64_t> _frames;
                mutable Mutex           _mutex;
                mutable int64_t         _windowStartNs = 0;
                mutable int64_t         _lastWindowBytes = 0;
                mutable int64_t         _lastWindowFrames = 0;
                mutable int64_t         _lastWindowElapsedNs = 0;
                mutable bool            _haveLastWindow = false;
};

PROMEKI_NAMESPACE_END
