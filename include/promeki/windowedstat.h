/**
 * @file      windowedstat.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>

#include <promeki/namespace.h>
#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class Variant;

/**
 * @brief Fixed-capacity ring of @c double samples with descriptive statistics.
 * @ingroup util
 *
 * Used by the MediaIO telemetry path to track recent per-command
 * metrics (read/write execute duration, queue wait, bytes processed,
 * backend-specific keys) so the framework can surface
 * @ref min / @ref max / @ref average / @ref stddev / @ref sampleCount
 * over a bounded window without growing without bound.  The container
 * holds plain doubles so it stays cheap to copy and serialize even
 * when many metrics are tracked per stage.
 *
 * @par Storage
 * A single @c List<double> backs the ring.  When fewer than
 * @ref capacity samples have been pushed the list grows; once full,
 * @ref push overwrites the oldest entry.  @ref values returns the
 * samples in oldest-first order regardless of the internal write
 * head, so callers see a stable timeline.
 *
 * @par String form
 * The canonical string form is @c "cap=N:[v1,v2,...,vK]" — capacity
 * plus the bracketed sample list (oldest first).  An empty / zero-
 * capacity instance round-trips as @c "cap=0:[]".  This compact
 * form is what the @ref Variant integration uses for JSON / DataStream
 * round-trips, matching every other compound Variant type
 * (AudioStreamDesc, AudioChannelMap, etc.).  Sample values are
 * formatted with @c "%g" so trailing zeros are elided.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance — including
 * any combination of @ref push / @ref clear / @ref values — must be
 * externally synchronized.  Stats accessors (@ref min, @ref max,
 * @ref average, @ref stddev) walk the buffer once per call and
 * recompute from scratch — there is no cached aggregate, so an
 * external mutex protects every reader equally.
 */
class WindowedStat {
        public:
                /** @brief Sample list type returned by @ref values. */
                using Samples = List<double>;

                /**
                 * @brief Aggregated descriptive statistics computed in a single pass.
                 *
                 * Returned by @ref stats so callers wanting more than
                 * one of @ref min / @ref max / @ref average /
                 * @ref stddev / @ref sum / @ref sampleCount avoid the
                 * O(N * k) re-scan that calling each accessor in
                 * sequence would incur.  The fields hold the same
                 * values the matching accessors would return — empty
                 * windows produce all zeros (and zero stddev).
                 */
                struct Stats {
                                /// Number of samples currently held (oldest-first count).
                                int count = 0;
                                /// Configured ring capacity.
                                int capacity = 0;
                                /// Minimum sample value, or @c 0.0 when empty.
                                double min = 0.0;
                                /// Maximum sample value, or @c 0.0 when empty.
                                double max = 0.0;
                                /// Sum of every sample, or @c 0.0 when empty.
                                double sum = 0.0;
                                /// Arithmetic mean, or @c 0.0 when empty.
                                double average = 0.0;
                                /// Population standard deviation, or @c 0.0 with fewer than two samples.
                                double stddev = 0.0;
                };

                /**
                 * @brief Per-value formatter used by @ref toString to render
                 *        each numeric component with custom units.
                 *
                 * The formatter is invoked once per scalar (avg, stddev,
                 * min, max) so callers can humanise values via the
                 * @ref Units helpers (e.g. ms, MB) without WindowedStat
                 * needing to know the underlying physical unit.  When
                 * empty (the default), @ref toString falls back to
                 * @ref String::number for every value.
                 */
                using ValueFormatter = std::function<String(double)>;

                /** @brief Default-constructs an empty, zero-capacity window. */
                WindowedStat() = default;

                /**
                 * @brief Constructs an empty window with the given @p capacity.
                 *
                 * @p capacity values less than zero clamp to zero.  A
                 * zero-capacity window silently discards every
                 * @ref push and reports zero for every statistic.
                 */
                explicit WindowedStat(int capacity);

                /** @brief Returns the configured ring capacity. */
                int capacity() const { return _capacity; }

                /**
                 * @brief Resets the capacity, dropping the oldest samples if necessary.
                 *
                 * When @p capacity is smaller than the current sample
                 * count, the oldest samples are dropped first so the
                 * most recent window survives.  When @p capacity is
                 * zero the window is cleared.  Negative values clamp
                 * to zero.
                 */
                void setCapacity(int capacity);

                /** @brief Returns the number of samples currently held. */
                int count() const;

                /** @brief Returns true if no samples have been pushed yet. */
                bool isEmpty() const { return count() == 0; }

                /** @brief Returns true once the ring has filled to @ref capacity. */
                bool isFull() const { return _full; }

                /**
                 * @brief Pushes @p value into the ring, evicting the oldest sample when full.
                 *
                 * No-op for zero-capacity windows.
                 */
                void push(double value);

                /**
                 * @brief Pushes a numeric @p value extracted from a @ref Variant.
                 *
                 * Promotes any of the integer / floating-point Variant
                 * types to @c double and pushes the result.  Two
                 * domain-specific types receive bespoke handling:
                 *   - @c Duration is exposed in nanoseconds (matches
                 *     the unit @ref MediaIOCommand::executeDurationNs uses
                 *     internally so windowed values stay comparable
                 *     across strategies and backends).
                 *   - @c FrameCount honours its sentinel states:
                 *     @ref FrameCount::Unknown / @ref FrameCount::Infinity
                 *     return @c false rather than push a misleading
                 *     negative or sentinel value.
                 *
                 * Any other Variant type (String, Enum, Variant-of-
                 * object, Invalid) returns @c false without modifying
                 * the ring, so the call is safe to apply blindly to
                 * heterogeneous stat maps.
                 *
                 * @param v The Variant whose payload should be pushed.
                 * @return @c true if the variant was numeric and a
                 *         sample was pushed; @c false if it was
                 *         non-numeric or a non-finite sentinel.
                 */
                bool push(const Variant &v);

                /** @brief Drops every sample but keeps the configured capacity. */
                void clear();

                // ------------------------------------------------------------
                // Statistics
                // ------------------------------------------------------------

                /**
                 * @brief Computes every descriptive statistic in a single pass.
                 *
                 * Cheaper than calling each accessor individually when
                 * a caller wants more than one number — the accessors
                 * each rescan the buffer, while @ref stats walks it
                 * once and reuses the running mean for the variance.
                 *
                 * @return A @ref Stats record populated with the
                 *         current count, capacity, min, max, sum,
                 *         average, and population standard deviation.
                 */
                Stats stats() const;

                /** @brief Returns the minimum sample value, or 0.0 when empty. */
                double min() const;

                /** @brief Returns the maximum sample value, or 0.0 when empty. */
                double max() const;

                /** @brief Returns the arithmetic mean, or 0.0 when empty. */
                double average() const;

                /** @brief Returns the sum of every sample, or 0.0 when empty. */
                double sum() const;

                /**
                 * @brief Returns the population standard deviation, or 0.0 when empty.
                 *
                 * Population (not sample) variance — the implementation
                 * divides by @ref count rather than @c count-1 because
                 * the window is the entire dataset of interest, not a
                 * draw from a larger one.  Returns 0.0 when fewer than
                 * two samples are present.
                 */
                double stddev() const;

                /** @brief Same as @ref count; spelled out for the descriptive API. */
                int sampleCount() const { return count(); }

                /**
                 * @brief Returns every sample in oldest-first order.
                 *
                 * The returned list has @ref count entries even when
                 * the ring has not yet filled; iteration order is
                 * oldest first regardless of the internal write head.
                 */
                Samples values() const;

                // ------------------------------------------------------------
                // String / Variant round-trip
                // ------------------------------------------------------------

                /**
                 * @brief Renders the window as a compact human-readable summary.
                 *
                 * Format: @c "Avg: <a> StdDev: <s> Min: <mn> Max: <mx> WinSz: <n>"
                 * — single spaces between fields, no commas, so the
                 * line stays scannable in a fixed-width terminal.
                 * Each numeric field is rendered through @p formatter
                 * when supplied, so callers wanting unit-aware output
                 * (e.g. @c "4 ms" via @ref Units::fromDurationNs) can
                 * inject the right scaler without WindowedStat needing
                 * to know the physical unit.  When @p formatter is
                 * empty, every value is rendered via @ref String::number.
                 *
                 * Empty windows still emit a complete row with every
                 * field at zero so callers laying out tables don't
                 * have to special-case missing samples.
                 *
                 * @param formatter Optional per-value renderer; when
                 *                  empty, defaults to numeric output.
                 * @return The formatted summary line.
                 */
                String toString(const ValueFormatter &formatter = ValueFormatter()) const;

                /**
                 * @brief Renders the window as the canonical
                 *        @c "cap=N:[v1,v2,...,vK]" round-trip form.
                 *
                 * Empty windows render as @c "cap=N:[]"; sample values
                 * are formatted with @c "%g" so trailing zeros are
                 * elided.  This is the form parsed by @ref fromString
                 * and is the string @c Variant::get<String>() returns
                 * for a @c TypeWindowedStat payload, so JSON snapshots
                 * survive a load / save cycle even though
                 * @ref toString itself is no longer round-trippable.
                 */
                String toSerializedString() const;

                /**
                 * @brief Parses the canonical @c "cap=N:[...]" form
                 *        produced by @ref toSerializedString.
                 *
                 * Accepts the legacy bare-list form @c "[v1,v2,...]"
                 * (capacity inferred from sample count) so older
                 * snapshots remain readable.  Whitespace inside the
                 * brackets is ignored.  Returns @c Error::Invalid on
                 * any parse failure; the value is a default-constructed
                 * @ref WindowedStat in that case.
                 */
                static Result<WindowedStat> fromString(const String &s);

                /** @brief Equality compares capacity, sample count, and every value in order. */
                bool operator==(const WindowedStat &other) const;

                /** @brief Inverse of @ref operator==. */
                bool operator!=(const WindowedStat &other) const { return !(*this == other); }

        private:
                Samples _samples;      ///< Ring storage; size grows up to _capacity.
                int     _capacity = 0; ///< Configured ring capacity.
                int     _head = 0;     ///< Next write index (only meaningful once _full).
                bool    _full = false; ///< True once _samples has reached _capacity.
};

/**
 * @brief Writes a WindowedStat to a DataStream.
 *
 * Wire format: tag + uint32 capacity + uint32 sample count + N
 * @c double values (oldest-first order).  Mirrors the in-memory
 * snapshot semantics so a round-trip preserves both the configured
 * capacity and the visible sample order.
 */
DataStream &operator<<(DataStream &stream, const WindowedStat &val);

/** @brief Reads a WindowedStat from a DataStream. */
DataStream &operator>>(DataStream &stream, WindowedStat &val);

PROMEKI_NAMESPACE_END
