/**
 * @file      statsaccumulator.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Online running-statistics accumulator for a stream of double samples.
 * @ingroup util
 *
 * Tracks count, sum, sum-of-squares, minimum, and maximum so that mean and
 * sample standard deviation can be computed without retaining the original
 * samples.  Mean and sample stddev use the classic `sumSq - sum*sum/n`
 * formulation; this is numerically fine for benchmark timings (positive
 * samples within a few orders of magnitude), and trades a tiny amount of
 * precision for trivial add/merge.
 *
 * StatsAccumulator is a plain value with no internal allocation.  It supports
 * merging (`merge()`) so that per-thread accumulators can be combined at
 * shutdown without locking.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently — typical usage gives each thread its own
 * accumulator and merges them via @ref merge at the end.
 * Concurrent access to a single instance must be externally
 * synchronized.
 *
 * @par Example
 * @code
 * StatsAccumulator acc;
 * for(double x : samples) acc.add(x);
 * printf("mean=%g stddev=%g min=%g max=%g\n",
 *        acc.mean(), acc.stddev(), acc.min(), acc.max());
 * @endcode
 */
class StatsAccumulator {
        public:
                /** @brief Constructs an empty accumulator. */
                StatsAccumulator() = default;

                /**
                 * @brief Adds a single sample to the accumulator.
                 * @param value The sample value.
                 */
                void add(double value) {
                        if (_count == 0) {
                                _min = value;
                                _max = value;
                        } else {
                                if (value < _min) _min = value;
                                if (value > _max) _max = value;
                        }
                        _count++;
                        _sum += value;
                        _sumSq += value * value;
                        return;
                }

                /**
                 * @brief Merges another accumulator into this one.
                 * @param other The accumulator to merge.
                 */
                void merge(const StatsAccumulator &other) {
                        if (other._count == 0) return;
                        if (_count == 0) {
                                _min = other._min;
                                _max = other._max;
                        } else {
                                if (other._min < _min) _min = other._min;
                                if (other._max > _max) _max = other._max;
                        }
                        _count += other._count;
                        _sum += other._sum;
                        _sumSq += other._sumSq;
                        return;
                }

                /** @brief Resets the accumulator to its empty state. */
                void reset() {
                        _count = 0;
                        _sum = 0.0;
                        _sumSq = 0.0;
                        _min = 0.0;
                        _max = 0.0;
                        return;
                }

                /** @brief Returns the number of samples added. */
                uint64_t count() const { return _count; }

                /** @brief Returns true if no samples have been added. */
                bool isEmpty() const { return _count == 0; }

                /** @brief Returns the sum of all samples (0 if empty). */
                double sum() const { return _sum; }

                /** @brief Returns the sum of squares of all samples (0 if empty). */
                double sumSq() const { return _sumSq; }

                /** @brief Returns the minimum sample value (0 if empty). */
                double min() const { return _min; }

                /** @brief Returns the maximum sample value (0 if empty). */
                double max() const { return _max; }

                /**
                 * @brief Returns the arithmetic mean of the samples.
                 * @return The mean, or 0.0 if no samples have been added.
                 */
                double mean() const {
                        if (_count == 0) return 0.0;
                        return _sum / static_cast<double>(_count);
                }

                /**
                 * @brief Returns the sample variance (divisor n-1).
                 * @return The variance, or 0.0 if fewer than two samples have been added.
                 */
                double variance() const {
                        if (_count < 2) return 0.0;
                        double n = static_cast<double>(_count);
                        double v = (_sumSq - _sum * _sum / n) / (n - 1.0);
                        return v > 0.0 ? v : 0.0;
                }

                /**
                 * @brief Returns the sample standard deviation (divisor n-1).
                 * @return The standard deviation, or 0.0 if fewer than two samples have been added.
                 */
                double stddev() const {
                        double v = variance();
                        return v > 0.0 ? std::sqrt(v) : 0.0;
                }

        private:
                uint64_t _count = 0;
                double   _sum = 0.0;
                double   _sumSq = 0.0;
                double   _min = 0.0;
                double   _max = 0.0;
};

PROMEKI_NAMESPACE_END
