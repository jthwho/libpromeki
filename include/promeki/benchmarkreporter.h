/**
 * @file      benchmarkreporter.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cmath>
#include <promeki/namespace.h>
#include <promeki/benchmark.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/pair.h>
#include <promeki/mutex.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Aggregates Benchmark objects and computes per-step timing statistics.
 * @ingroup pipeline
 *
 * BenchmarkReporter collects completed Benchmark traces (one per frame)
 * and maintains running statistics for every consecutive ID pair observed.
 *
 * @par Thread Safety
 * Fully thread-safe.  Multiple nodes may call @c submit
 * concurrently, and the read accessors (@c summaryReport,
 * @c stepsForPair) serialize on an internal mutex.
 *
 * @par Example
 * @code
 * BenchmarkReporter reporter;
 * // After each frame is processed:
 * reporter.submit(frame->benchmark());
 *
 * // At shutdown:
 * printf("%s\n", reporter.summaryReport().cstr());
 * @endcode
 */
class BenchmarkReporter {
        public:
                /** @brief Timing statistics for a single step (pair of IDs). */
                struct StepStats {
                        Benchmark::Id fromId;           ///< @brief Starting event ID.
                        Benchmark::Id toId;             ///< @brief Ending event ID.
                        uint64_t      count = 0;        ///< @brief Number of observations.
                        double        min = 0.0;        ///< @brief Minimum duration in seconds.
                        double        max = 0.0;        ///< @brief Maximum duration in seconds.
                        double        avg = 0.0;        ///< @brief Mean duration in seconds.
                        double        stddev = 0.0;     ///< @brief Standard deviation in seconds.
                        double        total = 0.0;      ///< @brief Sum of all durations in seconds.
                };

                /** @brief Constructs an empty BenchmarkReporter. */
                BenchmarkReporter() = default;

                /**
                 * @brief Submits a completed Benchmark for analysis.
                 *
                 * Extracts all consecutive entry pairs and updates running
                 * statistics for each step.
                 *
                 * @param bm The benchmark to analyze.
                 */
                void submit(const Benchmark &bm);

                /**
                 * @brief Returns statistics for a specific step.
                 * @param fromId The starting event.
                 * @param toId   The ending event.
                 * @return The accumulated StepStats, or a default StepStats if not observed.
                 */
                StepStats stepStats(Benchmark::Id fromId, Benchmark::Id toId) const;

                /**
                 * @brief Returns statistics for all observed consecutive-entry pairs.
                 * @return A list of StepStats for every observed step.
                 */
                promeki::List<StepStats> allStepStats() const;

                /**
                 * @brief Returns the number of benchmarks submitted.
                 * @return Submission count.
                 */
                uint64_t submittedCount() const;

                /** @brief Resets all statistics and submission count. */
                void reset();

                /**
                 * @brief Produces a human-readable summary of all step statistics.
                 * @return A formatted String containing per-step timing data.
                 */
                String summaryReport() const;

        private:
                /** @brief Internal accumulator for online variance computation. */
                struct Accumulator {
                        Benchmark::Id fromId;
                        Benchmark::Id toId;
                        uint64_t      count = 0;
                        double        sum = 0.0;
                        double        sumSq = 0.0;
                        double        min = 0.0;
                        double        max = 0.0;
                };

                using StepKey = Pair<uint32_t, uint32_t>;

                mutable Mutex                   _mutex;
                Map<StepKey, Accumulator>       _accumulators;
                uint64_t                        _submittedCount = 0;

                StepStats accumulatorToStats(const Accumulator &acc) const;
};

PROMEKI_NAMESPACE_END
