/**
 * @file      datetime.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <ctime>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/duration.h>

PROMEKI_NAMESPACE_BEGIN

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
                /** @brief Default strftime format string ("%F %T" = "YYYY-MM-DD HH:MM:SS"). */
                constexpr static const char *DefaultFormat = "%F %T";

                /** @brief Underlying time point type from the system clock. */
                using Value = std::chrono::system_clock::time_point;

                /** @brief Returns a DateTime representing the current wall-clock time. */
                static DateTime now() {
                        return DateTime(std::chrono::system_clock::now());
                }
                
                /**
                 * @brief Parses a DateTime from a formatted string.
                 * @param str The date/time string to parse.
                 * @param fmt The strftime-style format string (default: DefaultFormat).
                 * @param err Optional error output; set to Error::Invalid on parse failure.
                 * @return The parsed DateTime, or a default-constructed DateTime on failure.
                 */
                static DateTime fromString(const String &str, const char *fmt = DefaultFormat, Error *err = nullptr) {
                        std::tm tm = {};
                        tm.tm_isdst = -1;
                        const char *result = strptime(str.cstr(), fmt, &tm);
                        if(result == nullptr) {
                                if(err != nullptr) *err = Error::Invalid;
                                return DateTime();
                        }
                        if(err != nullptr) *err = Error::Ok;
                        return DateTime(tm);
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

                /** @brief Default constructor. The resulting time point is epoch (uninitialized). */
                DateTime() { }

                /**
                 * @brief Constructs a DateTime from a system_clock time point.
                 * @param val The time point value.
                 */
                DateTime(const Value &val) : _value(val) { }

                /**
                 * @brief Constructs a DateTime from a broken-down std::tm value.
                 * @param val The calendar time to convert.
                 */
                DateTime(std::tm val) : _value(std::chrono::system_clock::from_time_t(std::mktime(&val))) { }

                /**
                 * @brief Constructs a DateTime from a time_t value.
                 * @param val The POSIX time value.
                 */
                DateTime(time_t val) : _value(std::chrono::system_clock::from_time_t(val)) { }

                /**
                 * @brief Returns the time elapsed from @p other to this DateTime.
                 *
                 * Equivalent to chrono's @c time_point - time_point yielding
                 * a duration.  A positive Duration means @c this is later
                 * than @p other.
                 *
                 * @param other The reference instant to subtract.
                 * @return The difference as a @ref Duration.
                 */
                Duration operator-(const DateTime &other) const {
                        return Duration::fromNanoseconds(
                                std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        _value - other._value).count());
                }

                /**
                 * @brief Returns this DateTime offset forward by @p d.
                 * @param d The Duration to add.
                 * @return The shifted DateTime.
                 */
                DateTime operator+(const Duration &d) const {
                        return DateTime(_value + std::chrono::nanoseconds(d.nanoseconds()));
                }

                /**
                 * @brief Returns this DateTime offset backward by @p d.
                 * @param d The Duration to subtract.
                 * @return The shifted DateTime.
                 */
                DateTime operator-(const Duration &d) const {
                        return DateTime(_value - std::chrono::nanoseconds(d.nanoseconds()));
                }

                /**
                 * @brief Advances this DateTime forward by @p d.
                 * @param d The Duration to add.
                 * @return Reference to this DateTime.
                 */
                DateTime &operator+=(const Duration &d) {
                        _value += std::chrono::nanoseconds(d.nanoseconds());
                        return *this;
                }

                /**
                 * @brief Reverses this DateTime backward by @p d.
                 * @param d The Duration to subtract.
                 * @return Reference to this DateTime.
                 */
                DateTime &operator-=(const Duration &d) {
                        _value -= std::chrono::nanoseconds(d.nanoseconds());
                        return *this;
                }

                /**
                 * @brief Returns a DateTime offset forward by the given number of seconds.
                 * @param seconds Number of seconds to add.
                 * @return The offset DateTime.
                 */
                DateTime operator+(double seconds) const {
                        return DateTime(_value + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(seconds)));
                }

                /**
                 * @brief Returns a DateTime offset backward by the given number of seconds.
                 * @param seconds Number of seconds to subtract.
                 * @return The offset DateTime.
                 */
                DateTime operator-(double seconds) const {
                        return DateTime(_value - std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(seconds)));
                }

                /**
                 * @brief Advances this DateTime forward by the given number of seconds.
                 * @param seconds Number of seconds to add.
                 * @return Reference to this DateTime.
                 */
                DateTime &operator+=(double seconds) {
                        _value += std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(seconds));
                        return *this;
                }

                /**
                 * @brief Moves this DateTime backward by the given number of seconds.
                 * @param seconds Number of seconds to subtract.
                 * @return Reference to this DateTime.
                 */
                DateTime& operator-=(double seconds) {
                        _value -= std::chrono::duration_cast<std::chrono::system_clock::duration>(
                                        std::chrono::duration<double>(seconds));
                        return *this;
                }

                /** @brief Returns true if both DateTimes represent the same time point. */
                bool operator==(const DateTime &other) const {
                        return _value == other._value;
                }

                /** @brief Returns true if the DateTimes represent different time points. */
                bool operator!=(const DateTime &other) const {
                        return _value != other._value;
                }

                /** @brief Returns true if this DateTime is earlier than @p other. */
                bool operator<(const DateTime &other) const {
                        return _value < other._value;
                }

                /** @brief Returns true if this DateTime is earlier than or equal to @p other. */
                bool operator<=(const DateTime &other) const {
                        return _value <= other._value;
                }

                /** @brief Returns true if this DateTime is later than @p other. */
                bool operator>(const DateTime &other) const {
                        return _value > other._value;
                }

                /** @brief Returns true if this DateTime is later than or equal to @p other. */
                bool operator>=(const DateTime& other) const {
                        return _value >= other._value;
                }

                /**
                 * @brief Formats the DateTime as a string.
                 * @param format The strftime-style format string (default: DefaultFormat).
                 * @return The formatted date/time string.
                 */
                String toString(const char *format = DefaultFormat) const;

                /** @brief Implicit conversion to String via toString(). */
                operator String() const {
                        return toString();
                }

                /**
                 * @brief Converts the DateTime to a POSIX time_t value.
                 * @return The time as seconds since the Unix epoch.
                 */
                time_t toTimeT() const {
                        return std::chrono::system_clock::to_time_t(_value);
                }

                /**
                 * @brief Converts the DateTime to a floating-point seconds value.
                 * @return Seconds since the Unix epoch as a double.
                 */
                double toDouble() const {
                        return std::chrono::duration_cast<std::chrono::duration<double>>(
                                _value.time_since_epoch()).count();
                }

                /**
                 * @brief Returns the underlying system_clock time point.
                 * @return The stored time point value.
                 */
                Value value() const {
                        return _value;
                }

        private:
                Value   _value;
};


PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::DateTime);

