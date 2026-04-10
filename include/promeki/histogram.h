/**
 * @file      histogram.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/duration.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Lightweight sub-bucketed log2 histogram for hot-path instrumentation.
 * @ingroup time
 *
 * Designed to capture latency / interval / size distributions on the
 * fly without per-sample allocation, system calls, or locking.  Each
 * sample is binned into a power-of-two octave plus one of
 * @ref SubBucketsPerOctave linear sub-buckets within the octave, so
 * percentiles can be approximated to within roughly 100% /
 * @c SubBucketsPerOctave relative error — about 6% with the default
 * of 16 sub-buckets per octave.  That precision is enough to spot
 * pacing wobble at the 5–10% level (the typical regime where
 * RTP receivers start dropping or hitching) while keeping the
 * memory cost cheap (1024 buckets = 8 KiB per histogram).
 *
 * Running min, max, count, and sum are tracked separately so the
 * exact mean is also available without a second pass over the buckets.
 *
 * @par Threading
 *
 * Single-writer, multiple-reader.  @ref addSample is intentionally
 * not internally synchronised — the intended usage is "one writer
 * thread per Histogram instance" (e.g. one Histogram per RTP stream
 * owned by its TX or RX worker thread).  Readers calling
 * @ref toString or @ref percentile see a consistent enough snapshot
 * for diagnostics: small races on the bucket counts manifest as
 * off-by-one errors that are well below the noise floor for the
 * intended use.  Wrap externally if true multi-writer semantics
 * are needed.
 *
 * @par Bucket layout
 *
 * The address space is laid out as @c OctaveCount octaves of
 * @c SubBucketsPerOctave linear sub-buckets each.  Octave @c O
 * covers values in the half-open range @c [2^O, 2^(O+1)) and
 * subdivides that range into @c SubBucketsPerOctave equal-width
 * sub-buckets, so the bucket width within an octave is
 * @c 2^O / SubBucketsPerOctave.  This is the same scheme HDR
 * Histogram uses, just at a fixed precision.  64 octaves cover 0
 * through @c 2^64-1, which is around 580 years in nanoseconds —
 * more dynamic range than any practical timing or size measurement
 * needs.  Percentile estimation walks the cumulative bucket counts
 * and returns the midpoint of the matched sub-bucket.  The result
 * is then clamped to @c [min, max] so it never reports a value
 * outside the observed range.
 *
 * @par Example
 * @code
 * Histogram h;
 * h.setName("rtp-tx-frame-interval");
 * h.setUnit("us");
 * for (int i = 0; i < 1000; ++i) {
 *         TimeStamp t0 = TimeStamp::now();
 *         doWork();
 *         h.addSample((TimeStamp::now() - t0).microseconds());
 * }
 * promekiInfo("%s", h.toString().cstr());
 * @endcode
 */
class Histogram {
        public:
                /** @brief Number of power-of-two octaves the histogram covers. */
                static constexpr size_t OctaveCount = 64;
                /** @brief Linear sub-buckets per octave (must be a power of two). */
                static constexpr size_t SubBucketsPerOctave = 16;
                /** @brief Total number of buckets in the histogram. */
                static constexpr size_t BucketCount = OctaveCount * SubBucketsPerOctave;

                /** @brief Constructs an empty histogram. */
                Histogram();

                /**
                 * @brief Sets the histogram's display name.
                 * @param name The name shown in @ref toString and log lines.
                 */
                void setName(const String &name);

                /** @brief Returns the configured name. */
                const String &name() const { return _name; }

                /**
                 * @brief Sets the unit suffix appended to summary values.
                 * @param unit Unit string (e.g. "us", "ms", "bytes").
                 */
                void setUnit(const String &unit);

                /** @brief Returns the configured unit suffix. */
                const String &unit() const { return _unit; }

                /** @brief Resets all counts and statistics to zero. */
                void reset();

                /**
                 * @brief Records one sample.
                 *
                 * Negative values are clamped to 0.  O(1) integer math
                 * with no allocation; safe to call from latency-
                 * sensitive paths.
                 *
                 * @param value The sample value to record.
                 */
                void addSample(int64_t value);

                /**
                 * @brief Records a Duration sample (nanoseconds).
                 * @param d The duration to record.
                 */
                void addSample(const Duration &d) { addSample(d.nanoseconds()); }

                /** @brief Number of samples accumulated since the last reset. */
                int64_t count() const { return _count; }

                /** @brief Smallest observed sample, or 0 when @c count()==0. */
                int64_t min() const { return _count > 0 ? _min : 0; }

                /** @brief Largest observed sample, or 0 when @c count()==0. */
                int64_t max() const { return _count > 0 ? _max : 0; }

                /**
                 * @brief Returns the exact mean of all observed samples.
                 *
                 * Computed from the running sum and count, so it is
                 * not affected by the bucket quantisation that
                 * percentile estimates use.
                 *
                 * @return Mean sample value, or @c 0.0 when @c count()==0.
                 */
                double mean() const;

                /**
                 * @brief Approximates a percentile via bucket walk.
                 *
                 * Walks the cumulative bucket counts and returns the
                 * midpoint of the bucket containing the requested
                 * percentile.  Maximum error is one bucket — i.e. the
                 * returned value is within a factor of two of the
                 * true percentile, regardless of dataset size.
                 *
                 * @param p Percentile in @c [0.0, 1.0].
                 * @return The approximated percentile, or 0 if empty.
                 */
                int64_t percentile(double p) const;

                /**
                 * @brief Pretty-printed multi-line summary suitable for logging.
                 *
                 * Includes name, count, min, mean, p50/p95/p99, and max,
                 * with the configured unit appended to each numeric.
                 *
                 * @return A summary String.
                 */
                String toString() const;

        private:
                String  _name;
                String  _unit;
                int64_t _count = 0;
                int64_t _sum   = 0;
                int64_t _min   = 0;
                int64_t _max   = 0;
                int64_t _buckets[BucketCount] = {};
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::Histogram);
