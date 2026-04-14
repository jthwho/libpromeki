/**
 * @file      framepacer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/framerate.h>
#include <promeki/clock.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Drift-free frame pacer with pluggable clock and phase control.
 * @ingroup time
 *
 * FramePacer blocks the calling thread until the next frame
 * deadline, using rational arithmetic via
 * @ref FrameRate::cumulativeTicks to compute absolute deadlines
 * from an origin time.  This avoids the cumulative drift that
 * arises from repeatedly adding a floating-point frame period,
 * which matters for NTSC rates (29.97, 59.94, etc.) over long
 * runs.
 *
 * @par Clock sources
 *
 * By default the pacer uses the system's monotonic wall clock
 * (steady_clock).  Call @ref setClock() to supply a custom
 * @ref Clock for PTP, audio, or other time sources.
 * The clock is not owned — the caller must keep it alive for
 * the lifetime of the pacer.
 *
 * @par Phase alignment (ST 2110)
 *
 * For broadcast applications where frame boundaries must align
 * to a network epoch, call @c reset(originNs) with the
 * desired origin in the clock's time domain.  For example, an
 * ST 2110 sender would compute the PTP time at which frame 0
 * of the stream begins and pass that as the origin:
 *
 * @code
 * FramePacer pacer(FrameRate(FrameRate::FPS_2997));
 * pacer.setClock(&ptpClock);
 * pacer.reset(streamPhaseNs);   // PTP time of frame 0
 * while(running) {
 *     auto r = pacer.pace();
 *     if(r.framesToDrop > 0) sender.reportDrops(r.framesToDrop);
 *     sender.transmitFrame(r.frameIndex);
 * }
 * @endcode
 *
 * @par Pace result and frame dropping
 *
 * @ref pace() returns a @ref PaceResult that tells the caller
 * everything it needs to react:
 *
 * - @c frameIndex — which frame this pace corresponds to.
 * - @c framesToDrop — how many frames the caller should drop
 *   to get back in sync.  Advisory only — the pacer does not
 *   skip frames on its own.
 * - @c error — signed wake-up error relative to the ideal
 *   deadline.  Positive = late, negative = early.
 *
 * The pacer always advances by exactly one frame per @c pace()
 * call.  If the caller drops frames, it tells the pacer via
 * @ref advance(), which jumps the frame counter forward.  If
 * the caller doesn't drop, the pacer's error compensation
 * handles it naturally: when the accumulated error exceeds the
 * remaining time, @c pace() returns immediately (zero sleep)
 * so the pipeline runs as fast as it can until the error drains
 * below a frame period.  The timeline stays anchored — deadlines
 * are always computed from the origin — so the caller catches
 * up as quickly as its processing allows.
 *
 * @par Error compensation
 *
 * After each sleep the pacer measures the actual wake-up time
 * and accumulates the signed error (positive = late,
 * negative = early).  On the next pace, it biases the sleep
 * target by the accumulated error so that small oversleeps
 * are corrected on the following frame.
 *
 * @par Typical wall-clock usage
 * @code
 * FramePacer pacer(FrameRate(FrameRate::FPS_2997));
 * pacer.reset();
 * while(running) {
 *     auto r = pacer.pace();
 *     if(r.framesToDrop > 0 && canDrop()) {
 *         pacer.advance(r.framesToDrop);
 *         // ... skip r.framesToDrop frames ...
 *     }
 *     presentFrame(r.frameIndex);
 * }
 * @endcode
 */
class FramePacer {
        public:
                /**
                 * @brief Result returned by @ref pace().
                 *
                 * Gives the caller everything it needs to react to
                 * the pacing outcome: which frame this is, how many
                 * should be dropped, and how far off the timing was.
                 */
                struct PaceResult {
                        /**
                         * @brief Frame index this pace corresponds to.
                         *
                         * This is the pacer's internal frame counter
                         * after the +1 advance for this call.  Starts
                         * at 0 on the first pace after reset.
                         */
                        int64_t frameIndex = 0;

                        /**
                         * @brief Recommended number of frames to drop.
                         *
                         * Zero when the deadline was met or the error
                         * is less than one frame period.  When
                         * positive, the caller is behind by this many
                         * whole frame periods and should drop that
                         * many frames to get back in sync.
                         *
                         * Advisory only — the pacer does not advance
                         * past these frames on its own.  If the
                         * caller drops frames, it should call
                         * @ref advance() to inform the pacer.  If
                         * it doesn't, the pacer returns immediately
                         * on subsequent @c pace() calls until the
                         * error drains naturally.
                         */
                        int64_t framesToDrop = 0;

                        /**
                         * @brief Signed wake-up error relative to the
                         *        ideal deadline.
                         *
                         * Positive means we woke up late (overslept
                         * or called pace() too late).  Negative means
                         * we woke up early.  On the first frame after
                         * reset this is zero.
                         */
                        Duration error;
                };

                /** @brief Constructs a pacer with the default wall clock. */
                FramePacer();

                /**
                 * @brief Constructs a named pacer with the given frame rate.
                 * @param name Human label used to prefix all log messages.
                 * @param fps  The target frame rate.
                 */
                FramePacer(const String &name, const FrameRate &fps);

                /** @brief Destructor.  Frees the clock if owned. */
                ~FramePacer();

                FramePacer(const FramePacer &) = delete;
                FramePacer &operator=(const FramePacer &) = delete;
                FramePacer(FramePacer &&) = delete;
                FramePacer &operator=(FramePacer &&) = delete;

                /**
                 * @brief Sets the human-readable name.
                 *
                 * Used to prefix every log line so multiple pacers
                 * are distinguishable.
                 *
                 * @param name The label to use.
                 */
                void setName(const String &name) { _name = name; }

                /**
                 * @brief Returns the configured name.
                 */
                const String &name() const { return _name; }

                /**
                 * @brief Sets the target frame rate.
                 *
                 * Changing the rate mid-run requires a @c reset() to
                 * re-anchor the timeline.
                 *
                 * @param fps The target frame rate.
                 */
                void setFrameRate(const FrameRate &fps) { _frameRate = fps; }

                /**
                 * @brief Returns the configured frame rate.
                 * @return Const reference to the frame rate.
                 */
                const FrameRate &frameRate() const { return _frameRate; }

                /**
                 * @brief Sets a custom clock source.
                 *
                 * Pass @c nullptr to revert to the built-in wall
                 * clock.  The clock is not owned by the pacer —
                 * the caller must keep it alive.  Replaces any
                 * previously set clock (and frees it if the pacer
                 * owned it, i.e. it was the default wall clock).
                 *
                 * @param clock The clock to use, or nullptr for
                 *              wall clock.
                 */
                void setClock(Clock *clock);

                /**
                 * @brief Returns the active clock.
                 *
                 * Never returns nullptr — a default @ref WallClock
                 * is used when no external clock is set.
                 */
                Clock *clock() const { return _clock; }

                /**
                 * @brief Resets the pacer for a new run.
                 *
                 * The origin is captured from the clock on the first
                 * call to @c pace().  Use @ref reset(int64_t) to set
                 * an explicit origin for phase-aligned operation.
                 */
                void reset();

                /**
                 * @brief Resets the pacer with an explicit origin.
                 *
                 * Sets the origin to @p originNs in the clock's
                 * time domain.  Frame N's deadline is
                 * <tt>originNs + cumulativeTicks(1e9, N)</tt>.
                 *
                 * Use this for phase-aligned operation (e.g. ST 2110
                 * where frame boundaries must align to specific PTP
                 * times).
                 *
                 * @param originNs Origin time in nanoseconds from
                 *                 the clock's epoch.
                 */
                void reset(int64_t originNs);

                /**
                 * @brief Paces one frame and returns the result.
                 *
                 * Advances the frame counter by 1 and sleeps until
                 * the deadline for that frame (biased by accumulated
                 * error).  If the deadline has already passed, returns
                 * immediately and reports the lateness and recommended
                 * drop count in the result.
                 *
                 * On the first call after @c reset(), captures the
                 * origin (unless an explicit origin was provided) and
                 * returns immediately with frame index 0.
                 *
                 * Returns a default PaceResult if the frame rate is
                 * invalid.
                 *
                 * @return A PaceResult describing the outcome.
                 */
                PaceResult pace();

                /**
                 * @brief Advances the frame counter by @p frames.
                 *
                 * Call this after the caller has dropped frames in
                 * response to a @ref PaceResult::framesToDrop
                 * recommendation.  The pacer jumps ahead so the
                 * next @c pace() targets the correct future deadline.
                 *
                 * Prefer @ref noteDropped or @ref noteRepeated when
                 * you want the bump to be counted in the
                 * drop/repeat stats as well.
                 *
                 * @param frames Number of frames to skip forward.
                 *               Must be positive.
                 */
                void advance(int64_t frames);

                /**
                 * @brief Records that the caller rendered the frame
                 *        returned by the most recent @c pace().
                 *
                 * Does not advance the frame counter (@c pace() has
                 * already done so for this frame).  Bumps the
                 * @ref renderedFrames stat so the caller's render
                 * rate can be observed independently of pacer
                 * bookkeeping.
                 */
                void noteRendered();

                /**
                 * @brief Records that the caller dropped @p frames
                 *        frames (skipped their render) in response to
                 *        a @ref PaceResult::framesToDrop
                 *        recommendation.
                 *
                 * Advances the pacer by @p frames (via
                 * @ref advance) so the pacer's timeline re-syncs to
                 * wall time, and bumps the @ref droppedFrames stat.
                 * No-op when @p frames is not positive.
                 *
                 * @param frames Number of frames the caller dropped.
                 */
                void noteDropped(int64_t frames);

                /**
                 * @brief Records that @p frames frame periods passed
                 *        without new source data, so the caller
                 *        repeated the last frame (or let time advance
                 *        without rendering anything new).
                 *
                 * Advances the pacer by @p frames (via
                 * @ref advance) so the pacer's timeline re-syncs to
                 * wall time, and bumps the @ref repeatedFrames stat.
                 * No-op when @p frames is not positive.
                 *
                 * Use this for slow-source scenarios where the sink
                 * has no new data to render but must still keep its
                 * pacer-time aligned with wall time — e.g. the SDL
                 * player receiving a source that delivers frames
                 * slower than the reported rate.
                 *
                 * @param frames Number of frame periods to skip.
                 */
                void noteRepeated(int64_t frames);

                /**
                 * @brief Returns the number of frames paced since
                 *        the last @c reset().
                 * @return Frame count (0 before the first @c pace()).
                 */
                int64_t frameCount() const { return _frameCount; }

                /**
                 * @brief Returns the number of deadlines missed since
                 *        the last @c reset().
                 * @return Missed-frame count.
                 */
                int64_t missedFrames() const { return _missedFrames; }

                /**
                 * @brief Returns the number of frames the caller has
                 *        rendered (reported via @ref noteRendered)
                 *        since the last @c reset().
                 */
                int64_t renderedFrames() const { return _renderedFrames; }

                /**
                 * @brief Returns the number of frames the caller has
                 *        dropped (reported via @ref noteDropped)
                 *        since the last @c reset().
                 */
                int64_t droppedFrames() const { return _droppedFrames; }

                /**
                 * @brief Returns the number of frames the caller has
                 *        repeated (reported via @ref noteRepeated)
                 *        since the last @c reset().
                 */
                int64_t repeatedFrames() const { return _repeatedFrames; }

                /**
                 * @brief Returns the current accumulated timing error.
                 *
                 * Positive means we have been waking up late on
                 * average (the next sleep will be shortened).
                 * Negative means early (the next sleep will be
                 * lengthened).
                 *
                 * @return Accumulated error as a Duration.
                 */
                Duration accumulatedError() const {
                        return Duration::fromNanoseconds(_accumulatedErrorNs);
                }

        private:
                void ensureClock();
                void periodicDebugLog(int64_t nowNs);

                String             _name;
                FrameRate          _frameRate;
                Clock             *_clock = nullptr;
                bool               _ownsClock = false;
                int64_t            _originNs = 0;
                int64_t            _frameCount = 0;
                int64_t            _missedFrames = 0;
                int64_t            _renderedFrames = 0;
                int64_t            _droppedFrames = 0;
                int64_t            _repeatedFrames = 0;
                int64_t            _accumulatedErrorNs = 0;
                int64_t            _lastPeriodicLogNs = 0;
                int64_t            _frameCountAtLastLog = 0;
                bool               _started = false;
                bool               _explicitOrigin = false;
};

PROMEKI_NAMESPACE_END
