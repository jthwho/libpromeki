/**
 * @file      duration.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <chrono>
#include <cstdint>
#include <type_traits>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/rational.h>
#include <promeki/datatype.h>

PROMEKI_NAMESPACE_BEGIN

class Error;
class String;
class DataStream;

/**
 * @brief Time duration with nanosecond precision.
 * @ingroup time
 *
 * Stores a single @c int64_t nanosecond count.  Default-constructed
 * instances carry the @ref Invalid sentinel and report
 * @c isValid() == false; @ref zero / @c fromNanoseconds(0) returns
 * an explicit zero-length duration.
 *
 * @par Thread Safety
 * Trivially thread-safe.  Duration is a value-type wrapper around
 * a single @c int64_t; distinct instances may be used concurrently
 * and a single instance carries no mutable shared state.
 */
class Duration {
        public:
                PROMEKI_DATATYPE(Duration, DataTypeDuration, 1)

                /**
                 * @brief Sentinel ns value that marks a Duration as invalid.
                 *
                 * Chosen as @c INT64_MIN so that every other @c int64_t
                 * value remains a representable nanosecond count.
                 */
                static constexpr int64_t Invalid = INT64_MIN;

                /** @brief Writes a tagged int64 nanosecond count. */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads a tagged int64 nanosecond count. */
                template <uint32_t V> static Result<Duration> readFromStream(DataStream &s);

                /**
                 * @brief Returns an explicit zero-length Duration.
                 *
                 * Use when intent is "no time", as opposed to the
                 * default-constructed Duration which is @ref Invalid.
                 */
                static Duration zero() { return Duration(int64_t{0}); }

                /**
                 * @brief Creates a Duration from hours.
                 * @param h Number of hours.
                 * @return The Duration.
                 */
                static Duration fromHours(int64_t h) {
                        return Duration(h * 3'600'000'000'000LL);
                }

                /**
                 * @brief Creates a Duration from minutes.
                 * @param m Number of minutes.
                 * @return The Duration.
                 */
                static Duration fromMinutes(int64_t m) {
                        return Duration(m * 60'000'000'000LL);
                }

                /**
                 * @brief Creates a Duration from seconds.
                 * @param s Number of seconds.
                 * @return The Duration.
                 */
                static Duration fromSeconds(int64_t s) {
                        return Duration(s * 1'000'000'000LL);
                }

                /**
                 * @brief Creates a Duration from milliseconds.
                 * @param ms Number of milliseconds.
                 * @return The Duration.
                 */
                static Duration fromMilliseconds(int64_t ms) {
                        return Duration(ms * 1'000'000LL);
                }

                /**
                 * @brief Creates a Duration from microseconds.
                 * @param us Number of microseconds.
                 * @return The Duration.
                 */
                static Duration fromMicroseconds(int64_t us) {
                        return Duration(us * 1'000LL);
                }

                /**
                 * @brief Creates a Duration from nanoseconds.
                 * @param ns Number of nanoseconds.
                 * @return The Duration.
                 */
                static Duration fromNanoseconds(int64_t ns) { return Duration(ns); }

                /**
                 * @brief Computes the wall-clock duration spanned by
                 *        @p count samples at integer rate @p rate.
                 *
                 * Integer overload.  Rate is interpreted as samples
                 * per second.  A zero rate yields @ref zero.
                 */
                template <typename Int, std::enable_if_t<std::is_integral_v<Int>, int> = 0>
                static Duration fromSamples(int64_t count, Int rate) {
                        if (rate == 0) return zero();
                        return fromNanoseconds(count * 1'000'000'000LL / static_cast<int64_t>(rate));
                }

                /**
                 * @brief Computes the wall-clock duration spanned by
                 *        @p count samples at floating-point rate @p rate.
                 *
                 * Rate is interpreted as samples per second.  A zero or
                 * non-finite rate yields @ref zero.
                 */
                template <typename Float, std::enable_if_t<std::is_floating_point_v<Float>, int> = 0>
                static Duration fromSamples(int64_t count, Float rate) {
                        if (!(rate > Float(0))) return zero();
                        const double ns = static_cast<double>(count) * 1'000'000'000.0 / static_cast<double>(rate);
                        return fromNanoseconds(static_cast<int64_t>(ns));
                }

                /**
                 * @brief Computes the wall-clock duration spanned by
                 *        @p count samples at rational rate @p rate.
                 *
                 * Rate is interpreted as samples per second.  Retains
                 * full precision of the rational — commonly useful for
                 * fractional video frame rates (24000/1001, 30000/1001).
                 * An invalid or zero-numerator rate yields @ref zero.
                 */
                template <typename T> static Duration fromSamples(int64_t count, const Rational<T> &rate) {
                        if (!rate.isValid() || rate.numerator() == 0) {
                                return zero();
                        }
                        const int64_t num = static_cast<int64_t>(rate.numerator());
                        const int64_t den = static_cast<int64_t>(rate.denominator());
                        return fromNanoseconds(count * den * 1'000'000'000LL / num);
                }

                /** @brief Default constructor — produces an invalid Duration.  See @ref Invalid. */
                Duration() = default;

                /** @brief True if this Duration carries a real value (not @ref Invalid). */
                bool isValid() const { return _ns != Invalid; }

                /**
                 * @brief Returns the total number of whole hours.
                 *
                 * Returns @c 0 for an invalid Duration.
                 *
                 * @return Hours as int64_t.
                 */
                int64_t hours() const { return isValid() ? _ns / 3'600'000'000'000LL : 0; }

                /**
                 * @brief Returns the total number of whole minutes.
                 *
                 * Returns @c 0 for an invalid Duration.
                 *
                 * @return Minutes as int64_t.
                 */
                int64_t minutes() const { return isValid() ? _ns / 60'000'000'000LL : 0; }

                /**
                 * @brief Returns the total number of whole seconds.
                 *
                 * Returns @c 0 for an invalid Duration.
                 *
                 * @return Seconds as int64_t.
                 */
                int64_t seconds() const { return isValid() ? _ns / 1'000'000'000LL : 0; }

                /**
                 * @brief Returns the total number of whole milliseconds.
                 *
                 * Returns @c 0 for an invalid Duration.
                 *
                 * @return Milliseconds as int64_t.
                 */
                int64_t milliseconds() const { return isValid() ? _ns / 1'000'000LL : 0; }

                /**
                 * @brief Returns the total number of whole microseconds.
                 *
                 * Returns @c 0 for an invalid Duration.
                 *
                 * @return Microseconds as int64_t.
                 */
                int64_t microseconds() const { return isValid() ? _ns / 1'000LL : 0; }

                /**
                 * @brief Returns the raw nanosecond count.
                 *
                 * For an invalid Duration this returns @ref Invalid
                 * (@c INT64_MIN) rather than @c 0 — callers that want
                 * "0 if invalid" should branch on @ref isValid.
                 *
                 * @return Nanoseconds as int64_t, or @ref Invalid.
                 */
                int64_t nanoseconds() const { return _ns; }

                /**
                 * @brief Returns the duration as a fractional number of seconds.
                 *
                 * Returns @c 0.0 for an invalid Duration.
                 *
                 * @return Seconds as a double.
                 */
                double toSecondsDouble() const {
                        return isValid() ? static_cast<double>(_ns) / 1'000'000'000.0 : 0.0;
                }

                /**
                 * @brief Returns true if the duration is exactly zero.
                 *
                 * An invalid Duration is not considered zero.
                 *
                 * @return True if zero.
                 */
                bool isZero() const { return _ns == 0; }

                /**
                 * @brief Returns true if the duration is strictly negative.
                 *
                 * An invalid Duration is not considered negative.
                 *
                 * @return True if negative.
                 */
                bool isNegative() const { return isValid() && _ns < 0; }

                /**
                 * @brief Returns a human-readable HMS representation (e.g. "1h 23m 45.123s").
                 *
                 * Invalid Durations render as @c "invalid".
                 *
                 * @return Formatted string.
                 */
                String toString() const;

                /**
                 * @brief Parses a Duration from a unit-suffixed string.
                 *
                 * Accepted forms:
                 *  - `"3s"`, `"500ms"`, `"100us"`, `"100ns"`, `"5m"`, `"2h"`
                 *  - Decimal magnitudes: `"1.5s"`, `"0.25ms"`
                 *  - Optional whitespace between number and unit: `"3 s"`
                 *  - Leading sign: `"-500ms"`, `"+1s"`
                 *  - Bare numbers (no unit) are interpreted as **seconds** —
                 *    so `"3"` parses as 3 seconds.  Add a unit suffix when
                 *    you mean ms / us / ns.
                 *  - The literal `"invalid"` round-trips to a
                 *    default-constructed Duration.
                 *
                 * On parse failure the returned @ref Result carries an
                 * @c Error::ParseFailed and a default-constructed
                 * (invalid) Duration.
                 *
                 * @param str Input string.
                 * @return @ref Result wrapping the parsed @ref Duration.
                 */
                static Result<Duration> fromString(const String &str);

                /**
                 * @brief Returns an auto-scaled representation (e.g. "1.5 ms").
                 *
                 * Delegates to @ref Units::fromDurationNs.  Invalid
                 * Durations render as @c "invalid".
                 *
                 * @param precision Number of significant decimal digits.
                 * @return Formatted string.
                 */
                String toScaledString(int precision = 2) const;

                /** @brief Addition operator — invalid operands propagate. */
                Duration operator+(const Duration &o) const {
                        if (!isValid() || !o.isValid()) return Duration();
                        return Duration(_ns + o._ns);
                }
                /** @brief Subtraction operator — invalid operands propagate. */
                Duration operator-(const Duration &o) const {
                        if (!isValid() || !o.isValid()) return Duration();
                        return Duration(_ns - o._ns);
                }
                /** @brief Scalar multiplication operator — an invalid Duration stays invalid. */
                Duration operator*(int64_t s) const {
                        if (!isValid()) return Duration();
                        return Duration(_ns * s);
                }
                /** @brief Scalar division operator — an invalid Duration stays invalid. */
                Duration operator/(int64_t s) const {
                        if (!isValid()) return Duration();
                        return Duration(_ns / s);
                }
                /** @brief Unary minus — invalid stays invalid. */
                Duration operator-() const {
                        if (!isValid()) return Duration();
                        return Duration(-_ns);
                }

                /** @brief Equality comparison (two Invalid Durations compare equal). */
                bool operator==(const Duration &o) const { return _ns == o._ns; }
                /** @brief Inequality comparison. */
                bool operator!=(const Duration &o) const { return _ns != o._ns; }
                /** @brief Less-than comparison on raw ns.  Invalid sorts below all valid Durations. */
                bool operator<(const Duration &o) const { return _ns < o._ns; }
                /** @brief Greater-than comparison on raw ns. */
                bool operator>(const Duration &o) const { return _ns > o._ns; }
                /** @brief Less-than-or-equal comparison on raw ns. */
                bool operator<=(const Duration &o) const { return _ns <= o._ns; }
                /** @brief Greater-than-or-equal comparison on raw ns. */
                bool operator>=(const Duration &o) const { return _ns >= o._ns; }

        private:
                explicit Duration(int64_t ns) : _ns(ns) {}
                int64_t _ns = Invalid;
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
template <> struct std::formatter<promeki::Duration> {
                enum class Style {
                        Hms,
                        Scaled
                };

                Style                            _style = Style::Hms;
                std::formatter<std::string_view> _base;

                constexpr auto parse(std::format_parse_context &ctx) {
                        auto it = ctx.begin();
                        auto end = ctx.end();

                        auto tryKeyword = [&](const char *kw, Style s) {
                                auto p = it;
                                while (*kw && p != end && *p == *kw) {
                                        ++p;
                                        ++kw;
                                }
                                if (*kw == 0 && (p == end || *p == '}' || *p == ':')) {
                                        it = p;
                                        _style = s;
                                        return true;
                                }
                                return false;
                        };

                        if (!tryKeyword("scaled", Style::Scaled)) {
                                tryKeyword("hms", Style::Hms);
                        }

                        if (it != end && *it == ':') ++it;

                        ctx.advance_to(it);
                        return _base.parse(ctx);
                }

                template <typename FormatContext> auto format(const promeki::Duration &d, FormatContext &ctx) const {
                        promeki::String s = (_style == Style::Scaled) ? d.toScaledString() : d.toString();
                        return _base.format(std::string_view(s.cstr(), s.byteCount()), ctx);
                }
};

#endif // PROMEKI_ENABLE_CORE
