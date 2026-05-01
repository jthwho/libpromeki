/**
 * @file      nullpacingmediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/atomic.h>
#include <promeki/duration.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/ratetracker.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Snapshot of @ref NullPacingMediaIO internal counters.
 * @ingroup proav
 *
 * Returned by @ref NullPacingMediaIO::snapshot for tests and status
 * displays.  All counters are measured in frames; latency fields are
 * in microseconds and reflect the time each frame spent inside the
 * sink (arrival → discard).  The snapshot is a value copy, taken
 * under the sink's mutex, so it is safe to read from any thread
 * regardless of strand activity.
 */
struct NullPacingSnapshot {
                /// @brief Frames the sink has consumed (paced past a tick).
                int64_t framesConsumed = 0;
                /// @brief Frames the sink has dropped (arrived inside an
                /// active wallclock interval and were not paced past a
                /// tick).  Always zero in @ref NullPacingMode::Free.
                int64_t framesDropped = 0;
                /// @brief Sum of all per-frame in-sink latencies in
                /// microseconds (arrival → discard).  Both consumed and
                /// dropped frames contribute.
                int64_t totalLatencyUs = 0;
                /// @brief Largest per-frame in-sink latency observed since
                /// the sink was opened, in microseconds.
                int64_t peakLatencyUs = 0;
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
 * @c NullPacingMediaIO is the demo-friendly counterpart to a real
 * sink (file, screen, transmitter): it accepts whatever the upstream
 * stage produces, holds each frame for the configured wall-clock
 * interval, and then throws it away.  Two modes are supported
 * (selected via @ref MediaConfig::NullPacingMode):
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
 * Standard @ref MediaIOStats keys are populated by the framework from
 * the per-group rate tracker.  This sink supplements with:
 *  - @ref MediaIOStats::FramesDropped — internal drop counter
 *    (frames discarded between ticks).
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
 * @par Thread Safety
 * Strand-affine — see @ref SharedThreadMediaIO.
 */
class NullPacingMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(NullPacingMediaIO, SharedThreadMediaIO)
        public:
                /**
                 * @brief Constructs an idle null-pacing sink.
                 *
                 * The instance is registered with the factory (via
                 * @ref NullPacingFactory::create) but typically
                 * callers obtain one from @ref MediaIO::create with
                 * @c MediaConfig::Type set to @c "NullPacing".
                 */
                NullPacingMediaIO(ObjectBase *parent = nullptr);

                /** @brief Releases internal resources. */
                ~NullPacingMediaIO() override;

                /**
                 * @brief Returns a thread-safe snapshot of the sink's
                 *        internal counters.
                 *
                 * Safe to call from any thread, including while the
                 * sink is processing frames on the strand worker.
                 *
                 * @return A value copy of the latest counter state.
                 */
                NullPacingSnapshot snapshot() const;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

        private:
                // ---- Resolved configuration (latched at open time) ----
                promeki::NullPacingMode _mode = promeki::NullPacingMode::Wallclock;
                FrameRate               _targetRate;
                Duration                _period;
                bool                    _burnTimings = false;
                bool                    _isOpen = false;

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

/**
 * @brief @ref MediaIOFactory for the NullPacing sink backend.
 * @ingroup proav
 *
 * Registered at static-init via
 * @ref PROMEKI_REGISTER_MEDIAIO_FACTORY in @c nullpacingmediaio.cpp.
 * Callers normally use @c MediaIO::create with
 * @c MediaConfig::Type set to @c "NullPacing" rather than touching
 * this factory directly.
 */
class NullPacingFactory : public MediaIOFactory {
        public:
                NullPacingFactory() = default;

                String name() const override { return String("NullPacing"); }
                String displayName() const override { return String("Null Pacing Sink"); }
                String description() const override {
                        return String("Frame-pacing null sink (consumes and discards frames at a target rate).");
                }

                bool canBeSink() const override { return true; }

                Config::SpecMap configSpecs() const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END
