/**
 * @file      datetime.h
 * @copyright Jason Howard. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <chrono>
#include <ctime>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/duration.h>
#include <promeki/datatype.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief Wall-clock date and time based on std::chrono::system_clock.
 * @ingroup time
 *
 * Wraps a system_clock time_point and provides construction from strings,
 * time_t, and std::tm values.  Supports arithmetic with @ref Duration and
 * with floating-point second offsets.  Output formatting uses
 * strftime-style format strings.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance
 * is conditionally thread-safe — const operations are safe, but
 * concurrent mutation requires external synchronization.  Static
 * helpers (@c now, @c fromString, @c strftime) are fully
 * thread-safe.
 *
 * @par Example
 * @code
 * DateTime now = DateTime::now();
 * String formatted = now.toString("%Y-%m-%d %H:%M:%S");
 *
 * // Parse from string
 * DateTime dt = DateTime::fromString("2025-03-15 14:30:00");
 *
 * // Arithmetic with seconds
 * DateTime later = now + 3600.0;  // one hour later
 * @endcode
 */
class DateTime {
        public:
                PROMEKI_DATATYPE(DateTime, DataTypeDateTime, 1)

                /**
                 * @brief Sentinel ns value that marks a DateTime as invalid.
                 *
                 * Chosen as @c INT64_MIN — far outside any meaningful
                 * system_clock reading and symmetric with @ref
                 * TimeStamp::Invalid and @ref Duration::Invalid.
                 */
                static constexpr int64_t Invalid = INT64_MIN;

                /** @brief Writes a tagged int64 of nanoseconds since the system_clock epoch. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads a tagged int64 of nanoseconds since the system_clock epoch. */
                template <uint32_t V> static Result<DateTime> readFromStream(DataStream &s);

                /** @brief Default strftime format string ("%F %T" = "YYYY-MM-DD HH:MM:SS"). */
                constexpr static const char *DefaultFormat = "%F %T";

                /** @brief Underlying time point type from the system clock. */
                using Value = std::chrono::system_clock::time_point;

                /** @brief Returns a DateTime representing the current wall-clock time. */
                static DateTime now() { return DateTime(std::chrono::system_clock::now()); }

                /**
                 * @brief Parses a DateTime from a string using @ref DefaultFormat.
                 * @param str The date/time string to parse.
                 * @return A Result containing the parsed DateTime and
                 *         Error::Ok on success, or a default-constructed
                 *         DateTime and Error::ParseFailed on failure.
                 */
                static Result<DateTime> fromString(const String &str) {
                        return fromString(str, DefaultFormat);
                }

                /**
                 * @brief Parses a DateTime from a string with an explicit format.
                 * @param str The date/time string to parse.
                 * @param fmt A strftime-style format string.
                 * @return A Result containing the parsed DateTime and
                 *         Error::Ok on success, or a default-constructed
                 *         DateTime and Error::ParseFailed on failure.
                 */
                static Result<DateTime> fromString(const String &str, const char *fmt) {
                        if (str == "invalid") return makeResult(DateTime());
                        std::tm tm = {};
                        tm.tm_isdst = -1;
                        const char *result = strptime(str.cstr(), fmt, &tm);
                        if (result == nullptr) return makeError<DateTime>(Error::ParseFailed);
                        return makeResult(DateTime(tm));
                }

                /**
                 * @brief Creates a DateTime relative to the current time from a natural-language description.
                 * @param description A human-readable offset description (e.g. "2 hours ago").
                 * @return The computed DateTime.
                 */
                static DateTime fromNow(const String &description);

                /**
                 * @brief Formats a std::tm value using a strftime-style format string.
                 * @param tm The broken-down time to format.
                 * @param format The strftime-style format string (default: DefaultFormat).
                 * @return The formatted date/time string.
                 */
                static String strftime(const std::tm &tm, const char *format = DefaultFormat);

                /** @brief Default constructor — produces an invalid DateTime.  See @ref Invalid. */
                DateTime() = default;

                /**
                 * @brief Constructs a DateTime from a system_clock time point.
                 * @param val The time point value.
                 */
                DateTime(const Value &val)
                    : _ns(std::chrono::duration_cast<std::chrono::nanoseconds>(val.time_since_epoch()).count()) {}

                /**
                 * @brief Constructs a DateTime from a broken-down std::tm value.
                 * @param val The calendar time to convert.
                 */
                DateTime(std::tm val)
                    : _ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::from_time_t(std::mktime(&val)).time_since_epoch())
                                  .count()) {}

                /**
                 * @brief Constructs a DateTime from a time_t value.
                 * @param val The POSIX time value.
                 */
                DateTime(time_t val)
                    : _ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::system_clock::from_time_t(val).time_since_epoch())
                                  .count()) {}

                /** @brief True if this DateTime carries a real wall-clock value (not @ref Invalid). */
                bool isValid() const { return _ns != Invalid; }

                /**
                 * @brief Marks this DateTime as invalid.
                 *
                 * Equivalent to assigning a default-constructed
                 * DateTime.  Provided so caller intent reads clearly
                 * at the call site.
                 */
                void invalidate() { _ns = Invalid; }

                /**
                 * @brief Returns the time elapsed from @p other to this DateTime.
                 *
                 * Equivalent to chrono's @c time_point - time_point yielding
                 * a duration.  A positive Duration means @c this is later
                 * than @p other.  Returns an invalid Duration when
                 * either operand is invalid.
                 *
                 * @param other The reference instant to subtract.
                 * @return The difference as a @ref Duration.
                 */
                Duration operator-(const DateTime &other) const {
                        if (!isValid() || !other.isValid()) return Duration();
                        return Duration::fromNanoseconds(_ns - other._ns);
                }

                /**
                 * @brief Returns this DateTime offset forward by @p d.
                 *
                 * Invalid operands propagate — the result is invalid
                 * when either input is.
                 *
                 * @param d The Duration to add.
                 * @return The shifted DateTime.
                 */
                DateTime operator+(const Duration &d) const {
                        DateTime r(*this);
                        r += d;
                        return r;
                }

                /**
                 * @brief Returns this DateTime offset backward by @p d.
                 *
                 * Invalid operands propagate — the result is invalid
                 * when either input is.
                 *
                 * @param d The Duration to subtract.
                 * @return The shifted DateTime.
                 */
                DateTime operator-(const Duration &d) const {
                        DateTime r(*this);
                        r -= d;
                        return r;
                }

                /**
                 * @brief Advances this DateTime forward by @p d.
                 *
                 * If either operand is invalid the DateTime becomes invalid.
                 *
                 * @param d The Duration to add.
                 * @return Reference to this DateTime.
                 */
                DateTime &operator+=(const Duration &d) {
                        if (!isValid() || !d.isValid()) {
                                _ns = Invalid;
                        } else {
                                _ns += d.nanoseconds();
                        }
                        return *this;
                }

                /**
                 * @brief Reverses this DateTime backward by @p d.
                 *
                 * If either operand is invalid the DateTime becomes invalid.
                 *
                 * @param d The Duration to subtract.
                 * @return Reference to this DateTime.
                 */
                DateTime &operator-=(const Duration &d) {
                        if (!isValid() || !d.isValid()) {
                                _ns = Invalid;
                        } else {
                                _ns -= d.nanoseconds();
                        }
                        return *this;
                }

                /**
                 * @brief Returns a DateTime offset forward by the given number of seconds.
                 *
                 * Invalid inputs stay invalid.
                 *
                 * @param seconds Number of seconds to add.
                 * @return The offset DateTime.
                 */
                DateTime operator+(double seconds) const {
                        DateTime r(*this);
                        r += seconds;
                        return r;
                }

                /**
                 * @brief Returns a DateTime offset backward by the given number of seconds.
                 *
                 * Invalid inputs stay invalid.
                 *
                 * @param seconds Number of seconds to subtract.
                 * @return The offset DateTime.
                 */
                DateTime operator-(double seconds) const {
                        DateTime r(*this);
                        r -= seconds;
                        return r;
                }

                /**
                 * @brief Advances this DateTime forward by the given number of seconds.
                 *
                 * Invalid inputs stay invalid.
                 *
                 * @param seconds Number of seconds to add.
                 * @return Reference to this DateTime.
                 */
                DateTime &operator+=(double seconds) {
                        if (isValid()) _ns += static_cast<int64_t>(seconds * 1'000'000'000.0);
                        return *this;
                }

                /**
                 * @brief Moves this DateTime backward by the given number of seconds.
                 *
                 * Invalid inputs stay invalid.
                 *
                 * @param seconds Number of seconds to subtract.
                 * @return Reference to this DateTime.
                 */
                DateTime &operator-=(double seconds) {
                        if (isValid()) _ns -= static_cast<int64_t>(seconds * 1'000'000'000.0);
                        return *this;
                }

                /** @brief Returns true if both DateTimes represent the same time point (or are both invalid). */
                bool operator==(const DateTime &other) const { return _ns == other._ns; }

                /** @brief Returns true if the DateTimes represent different time points. */
                bool operator!=(const DateTime &other) const { return _ns != other._ns; }

                /** @brief Raw int64 ordering on the ns count.  Invalid sorts below all valid DateTimes. */
                bool operator<(const DateTime &other) const { return _ns < other._ns; }

                /** @brief Raw int64 ordering on the ns count. */
                bool operator<=(const DateTime &other) const { return _ns <= other._ns; }

                /** @brief Raw int64 ordering on the ns count. */
                bool operator>(const DateTime &other) const { return _ns > other._ns; }

                /** @brief Raw int64 ordering on the ns count. */
                bool operator>=(const DateTime &other) const { return _ns >= other._ns; }

                /**
                 * @brief Formats the DateTime as a string.
                 *
                 * Invalid DateTimes render as @c "invalid" regardless
                 * of the format string.
                 *
                 * @param format The strftime-style format string (default: DefaultFormat).
                 * @return The formatted date/time string.
                 */
                String toString(const char *format = DefaultFormat) const;

                /** @brief Implicit conversion to String via toString(). */
                operator String() const { return toString(); }

                /**
                 * @brief Converts the DateTime to a POSIX time_t value.
                 *
                 * Returns @c 0 for an invalid DateTime.
                 *
                 * @return The time as seconds since the Unix epoch.
                 */
                time_t toTimeT() const {
                        if (!isValid()) return 0;
                        return std::chrono::system_clock::to_time_t(value());
                }

                /**
                 * @brief Converts the DateTime to a floating-point seconds value.
                 *
                 * Returns @c 0.0 for an invalid DateTime.
                 *
                 * @return Seconds since the Unix epoch as a double.
                 */
                double toDouble() const {
                        if (!isValid()) return 0.0;
                        return static_cast<double>(_ns) / 1'000'000'000.0;
                }

                /**
                 * @brief Returns the raw nanoseconds-since-Unix-epoch count.
                 *
                 * For an invalid DateTime this returns @ref Invalid
                 * (@c INT64_MIN) rather than @c 0 — callers that want
                 * "0 if invalid" should branch on @ref isValid.
                 *
                 * @return Nanoseconds since the Unix epoch, or @ref Invalid.
                 */
                int64_t nanoseconds() const { return _ns; }

                /**
                 * @brief Returns the underlying system_clock time point.
                 *
                 * Reconstructs a chrono time_point from the stored ns
                 * count.  Invalid DateTimes return @c Value(Invalid ns)
                 * — callers that care about validity should consult
                 * @ref isValid first.
                 *
                 * @return The stored time point value.
                 */
                Value value() const {
                        return Value(std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                std::chrono::nanoseconds(_ns)));
                }

        private:
                int64_t _ns = Invalid;
};


PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::DateTime);

#endif // PROMEKI_ENABLE_CORE
