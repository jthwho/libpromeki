/**
 * @file      mediaiotask_nullpacing.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/duration.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiotask.h>
#include <promeki/mutex.h>
#include <promeki/ratetracker.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Snapshot of @ref MediaIOTask_NullPacing internal counters.
 * @ingroup proav
 *
 * Returned by @ref MediaIOTask_NullPacing::snapshot for tests and
 * status displays.  All counters are measured in frames; latency
 * fields are in microseconds and reflect the time each frame spent
 * inside the sink (arrival → discard).  The snapshot is a value
 * copy, taken under the sink's mutex, so it is safe to read from
 * any thread regardless of strand activity.
 */
struct NullPacingSnapshot {
        /// @brief Frames the sink has consumed (paced past a tick).
        int64_t framesConsumed = 0;
        /// @brief Frames the sink has dropped (arrived inside an
        /// active wallclock interval and were not paced past a
        /// tick).  Always zero in @ref NullPacingMode::Free.
        int64_t framesDropped  = 0;
        /// @brief Sum of all per-frame in-sink latencies in
        /// microseconds (arrival → discard).  Both consumed and
        /// dropped frames contribute.
        int64_t totalLatencyUs = 0;
        /// @brief Largest per-frame in-sink latency observed since
        /// the sink was opened, in microseconds.
        int64_t peakLatencyUs  = 0;
        /// @brief Number of latency samples recorded (consumed +
        /// dropped frames).  Equals the divisor used to compute the
        /// average latency.
        int64_t latencySamples = 0;
};

/**
 * @brief MediaIO sink that mimics a real playback device by pacing
 *        and discarding incoming frames.
 * @ingroup proav
 *
 * @c MediaIOTask_NullPacing is the demo-friendly counterpart to a
 * real sink (file, screen, transmitter): it accepts whatever the
 * upstream stage produces, holds each frame for the configured
 * wall-clock interval, and then throws it away.  Two modes are
 * supported (selected via @ref MediaConfig::NullPacingMode):
 *
 *  - @ref NullPacingMode::Wallclock — emit (consume) at most one
 *    frame per @c 1/TargetFps wall-clock period.  Frames arriving
 *    inside the same period are dropped immediately and counted
 *    in @ref MediaIOStats::FramesDropped.  This is the default and
 *    is what the pipeline demo uses to expose realistic "fps at the
 *    sink" numbers without needing an actual playback device.
 *  - @ref NullPacingMode::Free — accept every frame as fast as the
 *    upstream feeds it; never drops.  Useful as a passthrough sink
 *    when the upstream stage's natural rate is itself the
 *    measurement target.
 *
 * @par Target rate resolution
 * The target rate is taken from @ref MediaConfig::NullPacingTargetFps.
 * The sentinel value @c 0/1 means "follow the source descriptor":
 * the sink reads the upstream rate from the @ref MediaDesc that the
 * pipeline gave it at @c open().  If both the configured rate and
 * the descriptor rate are invalid, the sink reports
 * @ref Error::InvalidArgument from @c open().  In
 * @ref NullPacingMode::Free the target rate is irrelevant and the
 * sink opens unconditionally.
 *
 * @par Reported stats
 * The standard @ref MediaIOStats::FramesPerSecond and
 * @ref MediaIOStats::BytesPerSecond keys are populated by the base
 * @c MediaIO class from its happy-path rate tracker — backends do
 * not re-publish those.  This sink supplements with:
 *  - @ref MediaIOStats::FramesDropped — internal drop counter
 *    (frames discarded between ticks, never zero in Wallclock mode
 *    while the upstream over-feeds).
 *  - @ref MediaIOStats::AverageLatencyMs — running average of the
 *    in-sink hold time (arrival → discard) in milliseconds.
 *  - @ref MediaIOStats::PeakLatencyMs — peak observed in-sink hold
 *    time.
 *
 * @par Diagnostics
 * @ref MediaConfig::NullPacingBurnTimings, when true, causes the
 * sink to emit one @c promekiDebug line per consumed frame showing
 * the measured period since the previous consumption and the
 * jitter against the configured period.  Disabled by default.
 *
 * @par Example
 * @code
 * MediaIO::Config cfg = MediaIO::defaultConfig("NullPacing");
 * cfg.set(MediaConfig::NullPacingMode,
 *         NullPacingMode::Wallclock);
 * cfg.set(MediaConfig::NullPacingTargetFps, Rational<int>(24, 1));
 *
 * MediaIO *sink = MediaIO::create(cfg);
 * sink->open(MediaIO::Sink);
 * // ... pump frames in via writeFrame() ...
 * sink->close();
 * delete sink;
 * @endcode
 */
class MediaIOTask_NullPacing : public MediaIOTask {
        public:
                /**
                 * @brief Returns the format descriptor used by the
                 *        MediaIO factory registry.
                 *
                 * @par Example
                 * @code
                 * const auto desc = MediaIOTask_NullPacing::formatDesc();
                 * REQUIRE(desc.canBeSink);
                 * REQUIRE(desc.name == "NullPacing");
                 * @endcode
                 */
                static MediaIO::FormatDesc formatDesc();

                /**
                 * @brief Constructs an idle null-pacing sink.
                 *
                 * @par Example
                 * @code
                 * MediaIOTask_NullPacing *task = new MediaIOTask_NullPacing();
                 * MediaIO io;
                 * io.adoptTask(task);
                 * @endcode
                 */
                MediaIOTask_NullPacing();

                /**
                 * @brief Releases internal resources.
                 *
                 * @par Example
                 * @code
                 * delete new MediaIOTask_NullPacing();   // exits cleanly
                 * @endcode
                 */
                ~MediaIOTask_NullPacing() override;

                /**
                 * @brief Returns a thread-safe snapshot of the
                 *        sink's internal counters.
                 *
                 * Safe to call from any thread, including while the
                 * sink is processing frames on the strand worker.
                 *
                 * @par Example
                 * @code
                 * NullPacingSnapshot s = task->snapshot();
                 * CHECK(s.framesConsumed > 0);
                 * @endcode
                 *
                 * @return A value copy of the latest counter state.
                 */
                NullPacingSnapshot snapshot() const;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

                // ---- Resolved configuration (latched at open time) ----
                promeki::NullPacingMode _mode      = promeki::NullPacingMode::Wallclock;
                FrameRate               _targetRate;
                Duration                _period;
                bool                    _burnTimings = false;
                bool                    _isOpen      = false;

                // ---- Pacing state ----
                /// Wallclock anchor for the next tick.  Frames whose
                /// arrival time is < @c _nextDeadline are dropped;
                /// the first arrival at or after @c _nextDeadline
                /// consumes the frame and advances @c _nextDeadline
                /// by one period.
                TimeStamp _nextDeadline;
                /// Anchor for the burn-timings log: wallclock of the
                /// previous consumption.  Invalid until the first
                /// consumption.
                TimeStamp _lastConsumed;
                bool      _hasLastConsumed = false;

                // ---- Counters (mutex-guarded; cheap to read via
                // snapshot, cheap to update on every frame) ----
                mutable Mutex      _stateMutex;
                NullPacingSnapshot _stats;
};

PROMEKI_NAMESPACE_END
