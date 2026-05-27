/**
 * @file      timestamp.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <chrono>
#include <cstdint>
#include <thread>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/duration.h>
#include <promeki/datatype.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief A monotonic timestamp based on std::chrono::steady_clock.
 * @ingroup time
 *
 * Stores a single @c int64_t nanosecond count measured from
 * @c steady_clock 's epoch.  Default-constructed instances carry
 * the @ref Invalid sentinel and report @c isValid() == false;
 * @c TimeStamp(0) explicitly addresses the steady-clock epoch.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance
 * is conditionally thread-safe — const operations are safe, but
 * concurrent mutation requires external synchronization.  Static
 * helpers (@c now, @c sleep, etc.) are fully thread-safe.
 *
 * @par Example
 * @code
 * TimeStamp start = TimeStamp::now();
 * // ... do work ...
 * double elapsed = start.elapsedSeconds();
 * @endcode
 */
class TimeStamp {
        public:
                PROMEKI_DATATYPE(TimeStamp, DataTypeTimeStamp, 1)

                /**
                 * @brief Sentinel ns value that marks a TimeStamp as invalid.
                 *
                 * Chosen as @c INT64_MIN — far outside any meaningful
                 * steady_clock reading on any supported platform and
                 * symmetric with @ref Duration::Invalid.
                 */
                static constexpr int64_t Invalid = INT64_MIN;

                /** @brief Writes a tagged int64 nanoseconds-since-epoch on the steady clock. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads a tagged int64 nanoseconds-since-epoch on the steady clock. */
                template <uint32_t V> static Result<TimeStamp> readFromStream(DataStream &s);

                /** @brief Underlying clock type. */
                using Clock = std::chrono::steady_clock;
                /** @brief Time point value type. */
                using Value = std::chrono::time_point<Clock>;
                /** @brief Duration type derived from the clock. */
                using Duration = Clock::duration;

                /**
                 * @brief Converts a floating-point number of seconds to a Duration.
                 * @param val The time in seconds.
                 * @return The equivalent Duration value.
                 */
                static Duration secondsToDuration(double val) {
                        std::chrono::duration<double> doubleDuration(val);
                        return std::chrono::duration_cast<Duration>(doubleDuration);
                }

                /**
                 * @brief Sleeps the current thread for the given duration.
                 * @param d The duration to sleep.
                 */
                static void sleep(const Duration &d) {
                        std::this_thread::sleep_for(d);
                        return;
                }

                /**
                 * @brief Returns a TimeStamp representing the current time.
                 * @return A TimeStamp initialized to the current steady_clock time.
                 */
                static TimeStamp now() {
                        return TimeStamp(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
                                        .count());
                }

                /** @brief Constructs an invalid TimeStamp.  See @ref Invalid. */
                TimeStamp() = default;

                /**
                 * @brief Constructs a TimeStamp from a raw ns count since the steady-clock epoch.
                 *
                 * Pass @c TimeStamp(0) when you specifically want the
                 * epoch.  Passing @ref Invalid produces an invalid
                 * instance (equivalent to default construction).
                 *
                 * @param ns Nanoseconds since the steady-clock epoch.
                 */
                explicit TimeStamp(int64_t ns) : _ns(ns) {}

                /**
                 * @brief Constructs a TimeStamp from the given chrono time point value.
                 * @param v The time point value.
                 */
                TimeStamp(const Value &v)
                    : _ns(std::chrono::duration_cast<std::chrono::nanoseconds>(v.time_since_epoch()).count()) {}

                /** @brief True if this TimeStamp carries a real time point (not @ref Invalid). */
                bool isValid() const { return _ns != Invalid; }

                /**
                 * @brief Converts the TimeStamp to its underlying chrono Value type.
                 *
                 * Returns the chrono time_point reconstructed from the
                 * stored ns count.  An invalid TimeStamp will produce a
                 * time_point at @c Value(Invalid ns) — callers that
                 * care about validity should consult @ref isValid
                 * first.
                 */
                operator Value() const { return Value(std::chrono::nanoseconds(_ns)); }

                /**
                 * @brief Advances the timestamp by the given duration.
                 *
                 * If the timestamp is invalid the operation is a no-op
                 * (the result remains invalid).
                 *
                 * @param duration The duration to add.
                 * @return Reference to this TimeStamp.
                 */
                TimeStamp &operator+=(const Duration &duration) {
                        if (isValid()) {
                                _ns += std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
                        }
                        return *this;
                }

                /**
                 * @brief Moves the timestamp back by the given duration.
                 *
                 * If the timestamp is invalid the operation is a no-op
                 * (the result remains invalid).
                 *
                 * @param duration The duration to subtract.
                 * @return Reference to this TimeStamp.
                 */
                TimeStamp &operator-=(const Duration &duration) {
                        if (isValid()) {
                                _ns -= std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
                        }
                        return *this;
                }

                /**
                 * @brief Sets the underlying time point value.
                 * @param v The new time point value.
                 */
                void setValue(const Value &v) {
                        _ns = std::chrono::duration_cast<std::chrono::nanoseconds>(v.time_since_epoch()).count();
                        return;
                }

                /**
                 * @brief Returns the underlying chrono time point value.
                 *
                 * Reconstructs a chrono time_point from the stored ns
                 * count.  Invalid timestamps return @c Value(Invalid ns)
                 * — callers that care about validity should consult
                 * @ref isValid first.
                 *
                 * @return The stored time point.
                 */
                Value value() const { return Value(std::chrono::nanoseconds(_ns)); }

                /**
                 * @brief Marks this TimeStamp as invalid.
                 *
                 * Equivalent to assigning a default-constructed
                 * TimeStamp.  Provided so caller intent reads clearly
                 * at the call site.
                 */
                void invalidate() { _ns = Invalid; }

                /** @brief Updates the timestamp to the current time. */
                void update() {
                        _ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
                                      .count();
                }

                /**
                 * @brief Sleeps the current thread until this timestamp is reached.
                 *
                 * No-op for invalid timestamps.
                 */
                void sleepUntil() const {
                        if (isValid()) std::this_thread::sleep_until(value());
                }

                /**
                 * @brief Returns the duration since the clock's epoch.
                 * @return The duration from epoch to this timestamp.
                 */
                Duration timeSinceEpoch() const {
                        return std::chrono::duration_cast<Duration>(std::chrono::nanoseconds(_ns));
                }

                /**
                 * @brief Returns the time since epoch in seconds as a double.
                 *
                 * Returns @c 0.0 for an invalid TimeStamp.
                 *
                 * @return Seconds since the clock's epoch.
                 */
                double seconds() const {
                        if (!isValid()) return 0.0;
                        return static_cast<double>(_ns) / 1'000'000'000.0;
                }

                /**
                 * @brief Returns the time since epoch in milliseconds.
                 *
                 * Returns @c 0 for an invalid TimeStamp.
                 *
                 * @return Milliseconds since the clock's epoch.
                 */
                int64_t milliseconds() const {
                        if (!isValid()) return 0;
                        return _ns / 1'000'000LL;
                }

                /**
                 * @brief Returns the time since epoch in microseconds.
                 *
                 * Returns @c 0 for an invalid TimeStamp.
                 *
                 * @return Microseconds since the clock's epoch.
                 */
                int64_t microseconds() const {
                        if (!isValid()) return 0;
                        return _ns / 1'000LL;
                }

                /**
                 * @brief Returns the raw nanoseconds-since-epoch count.
                 *
                 * For an invalid TimeStamp this returns @ref Invalid
                 * (@c INT64_MIN) rather than @c 0 — callers that want
                 * "0 if invalid" should branch on @ref isValid.
                 *
                 * @return Nanoseconds since the clock's epoch, or @ref Invalid.
                 */
                int64_t nanoseconds() const { return _ns; }

                /**
                 * @brief Returns the elapsed time since this timestamp in seconds.
                 *
                 * Returns @c 0.0 for an invalid TimeStamp.
                 *
                 * @return Elapsed seconds as a double.
                 */
                double elapsedSeconds() const {
                        if (!isValid()) return 0.0;
                        return std::chrono::duration_cast<std::chrono::duration<double>>(Clock::now() - value()).count();
                }

                /**
                 * @brief Returns the elapsed time since this timestamp in milliseconds.
                 *
                 * Returns @c 0 for an invalid TimeStamp.
                 *
                 * @return Elapsed milliseconds.
                 */
                int64_t elapsedMilliseconds() const {
                        if (!isValid()) return 0;
                        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - value()).count();
                }

                /**
                 * @brief Returns the elapsed time since this timestamp in microseconds.
                 *
                 * Returns @c 0 for an invalid TimeStamp.
                 *
                 * @return Elapsed microseconds.
                 */
                int64_t elapsedMicroseconds() const {
                        if (!isValid()) return 0;
                        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - value()).count();
                }

                /**
                 * @brief Returns the elapsed time since this timestamp in nanoseconds.
                 *
                 * Returns @c 0 for an invalid TimeStamp.
                 *
                 * @return Elapsed nanoseconds.
                 */
                int64_t elapsedNanoseconds() const {
                        if (!isValid()) return 0;
                        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - value()).count();
                }

                /**
                 * @brief Returns a string representation of the timestamp in seconds.
                 *
                 * Invalid timestamps render as @c "invalid".
                 *
                 * @return A String containing the seconds value.
                 */
                String toString() const {
                        if (!isValid()) return String("invalid");
                        return String::number(seconds());
                }

                /**
                 * @brief Parses the seconds-since-epoch form produced by @ref toString.
                 *
                 * Accepts the literal @c "invalid" (round-trips to a
                 * default-constructed TimeStamp) or any floating-point
                 * literal @ref String::to<double> recognizes; returns
                 * @c Error::ParseFailed on malformed input.
                 */
                static Result<TimeStamp> fromString(const String &s) {
                        if (s == "invalid") return makeResult(TimeStamp());
                        Error  e;
                        double v = s.to<double>(&e);
                        if (e.isError()) return makeError<TimeStamp>(Error::ParseFailed);
                        return makeResult(TimeStamp(Value(secondsToDuration(v))));
                }

                /** @brief Returns true if both timestamps represent the same time point (or are both invalid). */
                bool operator==(const TimeStamp &other) const { return _ns == other._ns; }

                /** @brief Returns true if the timestamps represent different time points. */
                bool operator!=(const TimeStamp &other) const { return _ns != other._ns; }

                /** @brief Raw int64 ordering on the ns count.  Invalid sorts below all valid timestamps. */
                bool operator<(const TimeStamp &other) const { return _ns < other._ns; }
                /** @brief Raw int64 ordering on the ns count. */
                bool operator>(const TimeStamp &other) const { return _ns > other._ns; }
                /** @brief Raw int64 ordering on the ns count. */
                bool operator<=(const TimeStamp &other) const { return _ns <= other._ns; }
                /** @brief Raw int64 ordering on the ns count. */
                bool operator>=(const TimeStamp &other) const { return _ns >= other._ns; }

                /** @brief Converts the TimeStamp to a String. */
                operator String() const { return toString(); }

        private:
                int64_t _ns = Invalid;
};

/**
 * @brief Returns a new TimeStamp advanced by the given duration.
 *
 * Invalid timestamps propagate — the result is invalid when @p ts is.
 *
 * @param ts       The base timestamp.
 * @param duration The duration to add.
 * @return A new TimeStamp offset forward by @p duration.
 */
inline TimeStamp operator+(const TimeStamp &ts, const TimeStamp::Duration &duration) {
        TimeStamp result(ts);
        result += duration;
        return result;
}

/**
 * @brief Returns a new TimeStamp moved back by the given duration.
 *
 * Invalid timestamps propagate — the result is invalid when @p ts is.
 *
 * @param ts       The base timestamp.
 * @param duration The duration to subtract.
 * @return A new TimeStamp offset backward by @p duration.
 */
inline TimeStamp operator-(const TimeStamp &ts, const TimeStamp::Duration &duration) {
        TimeStamp result(ts);
        result -= duration;
        return result;
}

// ---------------------------------------------------------------------------
// promeki::Duration interop
//
// promeki::Duration and TimeStamp::Duration are intentionally different
// types — promeki::Duration is the library's portable nanosecond-precision
// value object, while TimeStamp::Duration is the platform's
// std::chrono::steady_clock::duration.  These free function overloads let
// callers do TimeStamp + promeki::Duration arithmetic directly, without
// the lossy round-trip through @c secondsToDuration / @c toSecondsDouble
// that earlier code had to fall back on.
//
// Implemented as free functions so the parameter type can be the
// namespace-level @c promeki::Duration without colliding with the
// in-class @c TimeStamp::Duration typedef.
// ---------------------------------------------------------------------------

/**
 * @brief Converts a @ref promeki::Duration to the underlying clock duration type.
 *
 * Invalid Durations are converted as their raw ns count; callers that
 * care about validity should branch on @c Duration::isValid first.
 *
 * @param d The library Duration to convert.
 * @return The equivalent @c TimeStamp::Duration (clock-native).
 */
inline TimeStamp::Duration toClockDuration(const Duration &d) {
        return std::chrono::duration_cast<TimeStamp::Duration>(std::chrono::nanoseconds(d.nanoseconds()));
}

/**
 * @brief Advances a TimeStamp by a library Duration in place.
 *
 * If either operand is invalid the TimeStamp becomes invalid.
 *
 * @param ts The TimeStamp to advance.
 * @param d  The duration to add.
 * @return Reference to @p ts.
 */
inline TimeStamp &operator+=(TimeStamp &ts, const Duration &d) {
        if (!ts.isValid() || !d.isValid()) {
                ts.invalidate();
        } else {
                ts = TimeStamp(ts.nanoseconds() + d.nanoseconds());
        }
        return ts;
}

/**
 * @brief Moves a TimeStamp back by a library Duration in place.
 *
 * If either operand is invalid the TimeStamp becomes invalid.
 *
 * @param ts The TimeStamp to retard.
 * @param d  The duration to subtract.
 * @return Reference to @p ts.
 */
inline TimeStamp &operator-=(TimeStamp &ts, const Duration &d) {
        if (!ts.isValid() || !d.isValid()) {
                ts.invalidate();
        } else {
                ts = TimeStamp(ts.nanoseconds() - d.nanoseconds());
        }
        return ts;
}

/**
 * @brief Returns a TimeStamp advanced by a library Duration.
 *
 * Invalid operands propagate — the result is invalid when either input is.
 *
 * @param ts The base timestamp.
 * @param d  The duration to add.
 * @return A new TimeStamp offset forward by @p d.
 */
inline TimeStamp operator+(const TimeStamp &ts, const Duration &d) {
        TimeStamp result(ts);
        result += d;
        return result;
}

/**
 * @brief Returns a TimeStamp moved back by a library Duration.
 *
 * Invalid operands propagate — the result is invalid when either input is.
 *
 * @param ts The base timestamp.
 * @param d  The duration to subtract.
 * @return A new TimeStamp offset backward by @p d.
 */
inline TimeStamp operator-(const TimeStamp &ts, const Duration &d) {
        TimeStamp result(ts);
        result -= d;
        return result;
}

/**
 * @brief Returns the @ref promeki::Duration between two TimeStamps.
 *
 * Equivalent to <tt>a.value() - b.value()</tt> converted to the
 * library's portable @ref promeki::Duration type.  Used wherever code wants
 * "time since" or "time between" measurements without having to
 * dip into raw @c std::chrono.  Returns an invalid Duration when
 * either operand is invalid.
 *
 * @param a The later TimeStamp.
 * @param b The earlier TimeStamp.
 * @return @c a - @c b as a @ref promeki::Duration.  Can be negative if @p a
 *         precedes @p b.
 */
inline Duration operator-(const TimeStamp &a, const TimeStamp &b) {
        if (!a.isValid() || !b.isValid()) return Duration();
        return Duration::fromNanoseconds(a.nanoseconds() - b.nanoseconds());
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::TimeStamp);

#endif // PROMEKI_ENABLE_CORE
