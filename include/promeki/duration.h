/**
 * @file      duration.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class String;

/**
 * @brief Time duration with nanosecond precision.
 * @ingroup time
 *
 * Simple value type wrapping std::chrono::nanoseconds. Provides
 * static factories for construction and accessors for various
 * time units. No PROMEKI_SHARED_FINAL.
 */
class Duration {
        public:
                /**
                 * @brief Creates a Duration from hours.
                 * @param h Number of hours.
                 * @return The Duration.
                 */
                static Duration fromHours(int64_t h) {
                        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::hours(h)));
                }

                /**
                 * @brief Creates a Duration from minutes.
                 * @param m Number of minutes.
                 * @return The Duration.
                 */
                static Duration fromMinutes(int64_t m) {
                        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::minutes(m)));
                }

                /**
                 * @brief Creates a Duration from seconds.
                 * @param s Number of seconds.
                 * @return The Duration.
                 */
                static Duration fromSeconds(int64_t s) {
                        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::seconds(s)));
                }

                /**
                 * @brief Creates a Duration from milliseconds.
                 * @param ms Number of milliseconds.
                 * @return The Duration.
                 */
                static Duration fromMilliseconds(int64_t ms) {
                        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::milliseconds(ms)));
                }

                /**
                 * @brief Creates a Duration from microseconds.
                 * @param us Number of microseconds.
                 * @return The Duration.
                 */
                static Duration fromMicroseconds(int64_t us) {
                        return Duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::microseconds(us)));
                }

                /**
                 * @brief Creates a Duration from nanoseconds.
                 * @param ns Number of nanoseconds.
                 * @return The Duration.
                 */
                static Duration fromNanoseconds(int64_t ns) {
                        return Duration(std::chrono::nanoseconds(ns));
                }

                /** @brief Default constructor. Creates a zero duration. */
                Duration() : _ns(0) {}

                /**
                 * @brief Returns the total number of whole hours.
                 * @return Hours as int64_t.
                 */
                int64_t hours() const {
                        return std::chrono::duration_cast<std::chrono::hours>(_ns).count();
                }

                /**
                 * @brief Returns the total number of whole minutes.
                 * @return Minutes as int64_t.
                 */
                int64_t minutes() const {
                        return std::chrono::duration_cast<std::chrono::minutes>(_ns).count();
                }

                /**
                 * @brief Returns the total number of whole seconds.
                 * @return Seconds as int64_t.
                 */
                int64_t seconds() const {
                        return std::chrono::duration_cast<std::chrono::seconds>(_ns).count();
                }

                /**
                 * @brief Returns the total number of whole milliseconds.
                 * @return Milliseconds as int64_t.
                 */
                int64_t milliseconds() const {
                        return std::chrono::duration_cast<std::chrono::milliseconds>(_ns).count();
                }

                /**
                 * @brief Returns the total number of whole microseconds.
                 * @return Microseconds as int64_t.
                 */
                int64_t microseconds() const {
                        return std::chrono::duration_cast<std::chrono::microseconds>(_ns).count();
                }

                /**
                 * @brief Returns the total number of nanoseconds.
                 * @return Nanoseconds as int64_t.
                 */
                int64_t nanoseconds() const {
                        return _ns.count();
                }

                /**
                 * @brief Returns the duration as a fractional number of seconds.
                 * @return Seconds as a double.
                 */
                double toSecondsDouble() const {
                        return std::chrono::duration<double>(_ns).count();
                }

                /**
                 * @brief Returns true if the duration is exactly zero.
                 * @return True if zero.
                 */
                bool isZero() const { return _ns.count() == 0; }

                /**
                 * @brief Returns true if the duration is negative.
                 * @return True if negative.
                 */
                bool isNegative() const { return _ns.count() < 0; }

                /**
                 * @brief Returns a human-readable HMS representation (e.g. "1h 23m 45.123s").
                 * @return Formatted string.
                 */
                String toString() const;

                /**
                 * @brief Returns an auto-scaled representation (e.g. "1.5 ms").
                 *
                 * Delegates to @ref Units::fromDurationNs.
                 *
                 * @param precision Number of significant decimal digits.
                 * @return Formatted string.
                 */
                String toScaledString(int precision = 2) const;

                /** @brief Addition operator. */
                Duration operator+(const Duration &o) const { return Duration(_ns + o._ns); }
                /** @brief Subtraction operator. */
                Duration operator-(const Duration &o) const { return Duration(_ns - o._ns); }
                /** @brief Scalar multiplication operator. */
                Duration operator*(int64_t s) const { return Duration(_ns * s); }
                /** @brief Scalar division operator. */
                Duration operator/(int64_t s) const { return Duration(_ns / s); }

                /** @brief Equality comparison. */
                bool operator==(const Duration &o) const { return _ns == o._ns; }
                /** @brief Inequality comparison. */
                bool operator!=(const Duration &o) const { return _ns != o._ns; }
                /** @brief Less-than comparison. */
                bool operator<(const Duration &o) const { return _ns < o._ns; }
                /** @brief Greater-than comparison. */
                bool operator>(const Duration &o) const { return _ns > o._ns; }
                /** @brief Less-than-or-equal comparison. */
                bool operator<=(const Duration &o) const { return _ns <= o._ns; }
                /** @brief Greater-than-or-equal comparison. */
                bool operator>=(const Duration &o) const { return _ns >= o._ns; }

        private:
                explicit Duration(std::chrono::nanoseconds ns) : _ns(ns) {}
                std::chrono::nanoseconds _ns;
};

PROMEKI_NAMESPACE_END

/**
 * @brief @c std::formatter specialization for @ref promeki::Duration.
 *
 * @par Format spec syntax
 * @code
 *   {}           // default HMS  e.g. "1h 23m 45.123s"
 *   {:hms}       // explicit HMS
 *   {:scaled}    // auto-scaled  e.g. "1.5 ms"
 * @endcode
 *
 * Standard string format specifiers (width, fill, alignment) follow
 * the style keyword after a colon:
 * @code
 *   {:scaled:>16}  // auto-scaled, right-justified, width 16
 * @endcode
 */
template <>
struct std::formatter<promeki::Duration> {
        enum class Style { Hms, Scaled };

        Style _style = Style::Hms;
        std::formatter<std::string_view> _base;

        constexpr auto parse(std::format_parse_context &ctx) {
                auto it = ctx.begin();
                auto end = ctx.end();

                auto tryKeyword = [&](const char *kw, Style s) {
                        auto p = it;
                        while(*kw && p != end && *p == *kw) { ++p; ++kw; }
                        if(*kw == 0 && (p == end || *p == '}' || *p == ':')) {
                                it = p;
                                _style = s;
                                return true;
                        }
                        return false;
                };

                if(!tryKeyword("scaled", Style::Scaled)) {
                        tryKeyword("hms", Style::Hms);
                }

                if(it != end && *it == ':') ++it;

                ctx.advance_to(it);
                return _base.parse(ctx);
        }

        template <typename FormatContext>
        auto format(const promeki::Duration &d, FormatContext &ctx) const {
                promeki::String s = (_style == Style::Scaled)
                        ? d.toScaledString()
                        : d.toString();
                return _base.format(std::string_view(s.cstr(), s.byteCount()), ctx);
        }
};
