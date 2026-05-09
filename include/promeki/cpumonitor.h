/**
 * @file      cpumonitor.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>

#include <promeki/atomic.h>
#include <promeki/duration.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/thread.h>
#include <promeki/threadpool.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Periodic per-thread CPU usage sampler.
 * @ingroup core
 *
 * Samples @c /proc/self/task/<tid>/{comm,stat} on every tick of the
 * configured interval, computes the user + kernel CPU time consumed
 * by each kernel-visible thread of the current process since the
 * previous tick, and publishes a @ref Report sorted by CPU% in
 * descending order.  The default report function logs a one-line
 * summary at @c Info level so the cost is bounded to one log message
 * per tick.
 *
 * @par Why
 * Pipelines that pace correctly should run at near-zero idle CPU.
 * When a backend, encoder, or worker thread is firehosing,
 * @ref CpuMonitor identifies the offending thread by name within
 * one interval — far quicker than attaching a profiler.  The
 * sampler is cheap (one @c open / @c read / @c close per thread per
 * tick, all on @c /proc); a 1 s interval over a 30-thread process
 * is sub-millisecond of overhead.
 *
 * @par Linux only
 * The implementation reads @c /proc/self/task — Linux-specific.  On
 * other platforms the sampler still constructs but produces empty
 * reports.  Promeki currently targets Linux only, so this is not a
 * limitation for shipping callers.
 *
 * @par Thread-safety
 * @ref start, @ref stop, @ref setInterval, and
 * @ref setReportFunction are safe to call from any thread.  The
 * sampler runs on its own dedicated worker thread (@c "cpumon" in
 * @c top -H).  The default report function logs through
 * @c promekiInfo, which is itself thread-safe.
 *
 * @par Lifecycle
 * Construction is cheap and idle.  @ref start launches the worker
 * (no-op if already running); @ref stop joins it.  The destructor
 * calls @ref stop, so you can safely place a @ref CpuMonitor on
 * the stack or own it via @ref UniquePtr.
 *
 * @par Typical usage
 * @code
 * Application app(argc, argv);
 * Application::startCpuMonitor(Duration::fromSeconds(5));
 * // ... pipeline runs ...
 * @endcode
 */
class CpuMonitor : public Thread {
        public:
                /**
                 * @brief One thread's slice of a CPU sample.
                 *
                 * Percentages are normalized so that 100% means
                 * "one full core for the entire interval".  A
                 * thread can therefore exceed 100% only if the
                 * sample interval shrunk between snapshots
                 * (clock skew); per-thread CPU% is otherwise
                 * capped at 100% by Linux's per-thread accounting.
                 */
                struct ThreadSample {
                                uint64_t tid = 0;             ///< Linux kernel TID.
                                String   name;                ///< Thread comm (≤ 15 chars).
                                double   userPercent = 0.0;   ///< Normalized 0-100.
                                double   systemPercent = 0.0; ///< Normalized 0-100.
                                double   totalPercent = 0.0;  ///< user + system.
                                Duration cpuTimeDelta;        ///< Total CPU consumed during the sample.
                };

                /**
                 * @brief One sample's worth of per-thread data.
                 *
                 * @ref threads is sorted by @c totalPercent
                 * descending so the top contributor lands first.
                 * @ref processPercent is the sum across all
                 * threads — values above 100% indicate multi-core
                 * usage (e.g. 480% on a video pipeline burning
                 * five cores).
                 */
                struct Report {
                                Duration           wallElapsed;        ///< Wall time covered by this sample.
                                double             processPercent = 0.0; ///< Sum of @ref ThreadSample::totalPercent.
                                List<ThreadSample> threads;            ///< Sorted CPU% desc.
                };

                /**
                 * @brief Callback signature for custom reporters.
                 *
                 * Invoked from the sampler thread once per tick
                 * with the freshly computed report.  The default
                 * reporter logs @ref formatReport at @c Info
                 * level — supply a custom function to redirect
                 * output (e.g. to a JSONL stats file or a HUD).
                 *
                 * The supplied @ref Report is owned by the
                 * sampler and only valid for the duration of the
                 * call; copy any fields you need to keep beyond
                 * the callback.
                 */
                using ReportFunction = std::function<void(const Report &)>;

                /// @brief Default sample interval if @ref start is called with @c Duration{}.
                static constexpr int64_t DefaultIntervalSec = 5;

                /// @brief Number of top-N threads emitted by @ref formatReport.
                ///        Threads beyond this rank are summed into a synthetic
                ///        "(N others)" line so the log stays one terse row.
                static constexpr size_t DefaultTopN = 8;

                /**
                 * @brief Constructs an idle monitor.
                 *
                 * No worker thread is created until @ref start is
                 * called.  The default reporter logs at @c Info.
                 */
                CpuMonitor();

                /// @brief Joins the sampler thread, if running.
                ~CpuMonitor() override;

                CpuMonitor(const CpuMonitor &) = delete;
                CpuMonitor &operator=(const CpuMonitor &) = delete;

                /**
                 * @brief Starts the sampler with the given interval.
                 *
                 * No-op if already running; call @ref setInterval
                 * to change the cadence on the fly.  An interval
                 * of zero or negative is replaced with
                 * @ref DefaultIntervalSec seconds.
                 *
                 * @param interval Sample period.  Sub-second
                 *                 values are honoured but very
                 *                 short intervals (< 100 ms)
                 *                 produce noisy output because
                 *                 the kernel only updates jiffy-
                 *                 granularity counters.
                 * @return @c Error::Ok or
                 *         @c Error::AlreadyOpen.
                 */
                Error start(const Duration &interval);

                /**
                 * @brief Stops the sampler and joins the worker.
                 *
                 * Idempotent.  The next @ref start call after a
                 * @ref stop is a fresh boot — previous-tick state
                 * is discarded so the first new tick reports
                 * deltas relative to fresh baselines (one tick
                 * of warm-up) instead of the long stale gap.
                 */
                void stop();

                /// @brief Returns @c true while the sampler is running.
                bool isRunning() const { return Thread::isRunning(); }

                /**
                 * @brief Updates the sample interval at runtime.
                 *
                 * The change takes effect on the next sleep
                 * boundary.  Sub-second intervals are accepted
                 * but see the @ref start note about jiffy
                 * granularity.
                 *
                 * @param d New sample interval.
                 */
                void setInterval(const Duration &d);

                /// @brief Returns the configured sample interval.
                Duration interval() const;

                /**
                 * @brief Replaces the report callback.
                 *
                 * Pass an empty function to revert to the default
                 * (one-line @c promekiInfo).  Safe to call
                 * concurrently with the sampler — the new
                 * function takes effect on the next tick.
                 *
                 * @param fn The new reporter or an empty function.
                 */
                void setReportFunction(ReportFunction fn);

                /**
                 * @brief Formats @p r as a single human-readable line.
                 *
                 * Output shape:
                 * @code
                 * cpu/5.0s proc=215.4%  tpg=98.4%  RtpVidPkt=42.1%  media0=18.0%  ...
                 * @endcode
                 *
                 * Threads beyond @ref DefaultTopN are summed
                 * into an @c "(N others=Y%)" tail entry.
                 *
                 * @param r       Report to format.
                 * @param topN    Maximum named entries; 0 means
                 *                emit every thread.
                 * @return One-line summary string.
                 */
                static String formatReport(const Report &r, size_t topN = DefaultTopN);

                /**
                 * @brief Controls whether each tick also dumps every
                 *        live @ref ThreadPool's per-tag work stats.
                 *
                 * When enabled (the default), the sampler iterates
                 * @ref ThreadPool::allPools after the per-thread
                 * report and emits one additional log line per pool:
                 *
                 * @code
                 * pool/media/5.0s  RtpMediaIO=cpu%/wall% (n=N)  CscMediaIO=cpu%/wall% ...
                 * @endcode
                 *
                 * Pools with zero work in the last interval are
                 * skipped.  Each tick calls
                 * @ref ThreadPool::resetWorkStats so the next
                 * interval's percentages reflect just that window.
                 *
                 * @param enabled Whether to emit the pool report.
                 */
                void setPoolReportEnabled(bool enabled);

                /// @brief Returns whether the pool report is enabled.
                bool isPoolReportEnabled() const;

                /**
                 * @brief Formats one @ref ThreadPool's per-tag work
                 *        stats as a single human-readable line.
                 *
                 * @param poolName  Logical pool name (or empty).
                 * @param wallElapsed Wall time of the sample window
                 *                    (drives the @c cpu%/wall% denominator).
                 * @param stats     Output of
                 *                  @ref ThreadPool::snapshotWorkStats.
                 * @param topN      Maximum named entries; 0 means
                 *                  emit every tag.
                 * @return One-line summary string.
                 */
                static String formatPoolReport(const String &poolName, const Duration &wallElapsed,
                                               const List<ThreadPool::WorkStats> &stats,
                                               size_t                             topN = DefaultTopN);

        protected:
                void run() override;

        private:
                struct ThreadCpuTimes {
                                uint64_t tid = 0;
                                String   name;
                                int64_t  utimeTicks = 0;
                                int64_t  stimeTicks = 0;
                };

                /// Linux-only.  Walks @c /proc/self/task/, parses
                /// the @c comm and @c stat fields needed for the
                /// next delta computation.  Returns the populated
                /// list; entries appear in TID order.  Failures on
                /// individual TIDs are silently dropped — a
                /// thread that exited mid-sample is normal.
                List<ThreadCpuTimes> readThreadCpuTimes() const;

                Atomic<int64_t> _intervalNs;
                Atomic<bool>    _stopRequested;
                Atomic<bool>    _poolReportEnabled;
                mutable Mutex   _fnMutex;
                ReportFunction  _reportFn;
};

PROMEKI_NAMESPACE_END
