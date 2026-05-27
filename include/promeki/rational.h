/**
 * @file      rational.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <promeki/namespace.h>
#include <promeki/result.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Rational number with numerator and denominator.
 * @ingroup math
 *
 * Represents an exact fraction.  Automatically simplifies on
 * construction.  A zero denominator produces an invalid Rational
 * that can be detected with isValid().
 *
 * @par Thread Safety
 * Trivially thread-safe.  Rational is a value-type wrapper around
 * two integers; distinct instances may be used concurrently and a
 * single instance carries no mutable shared state.
 *
 * @tparam T Underlying integer type (default: int).
 *
 * @par Example
 * @code
 * Rational<int> fps(24000, 1001);  // 23.976 fps
 * double val = fps.toDouble();     // 23.976...
 * String s = fps.toString();       // "24000/1001"
 * Rational<int> half(1, 2);
 * auto sum = fps + half;
 * @endcode
 */
template <typename T = int> class Rational {
        public:
                /**
                 * @brief Constructs a rational number.
                 *
                 * If \p d is zero the Rational is marked invalid.
                 *
                 * @param n Numerator.
                 * @param d Denominator.
                 */
                Rational(T n = 0, T d = 1) : _num(n), _den(d) {
                        if (_den != 0) simplify();
                }

                /** @brief Returns true if the denominator is non-zero. */
                bool isValid() const { return _den != 0; }

                /**
                 * @brief Adds two rationals.
                 * @param rhs Right-hand operand.
                 * @return Sum, or invalid Rational if either operand is invalid.
                 */
                Rational operator+(const Rational &rhs) const {
                        if (!isValid() || !rhs.isValid()) return Rational(0, 0);
                        T lcm = std::lcm(_den, rhs._den);
                        T num = _num * (lcm / _den) + rhs._num * (lcm / rhs._den);
                        return Rational(num, lcm);
                }

                /**
                 * @brief Subtracts two rationals.
                 * @param rhs Right-hand operand.
                 * @return Difference, or invalid Rational if either operand is invalid.
                 */
                Rational operator-(const Rational &rhs) const {
                        if (!isValid() || !rhs.isValid()) return Rational(0, 0);
                        T lcm = std::lcm(_den, rhs._den);
                        T num = _num * (lcm / _den) - rhs._num * (lcm / rhs._den);
                        return Rational(num, lcm);
                }

                /**
                 * @brief Multiplies two rationals.
                 * @param rhs Right-hand operand.
                 * @return Product, or invalid Rational if either operand is invalid.
                 */
                Rational operator*(const Rational &rhs) const {
                        if (!isValid() || !rhs.isValid()) return Rational(0, 0);
                        T num = _num * rhs._num;
                        T den = _den * rhs._den;
                        return Rational(num, den);
                }

                /**
                 * @brief Divides two rationals.
                 * @param rhs Right-hand operand.
                 * @return Quotient, or invalid Rational if rhs is zero or either is invalid.
                 */
                Rational operator/(const Rational &rhs) const {
                        if (!isValid() || !rhs.isValid() || rhs._num == 0) return Rational(0, 0);
                        T num = _num * rhs._den;
                        T den = _den * rhs._num;
                        return Rational(num, den);
                }

                /** @brief Equality comparison. */
                bool operator==(const Rational &rhs) const { return _num == rhs._num && _den == rhs._den; }

                /** @brief Inequality comparison. */
                bool operator!=(const Rational &rhs) const { return !(*this == rhs); }

                /** @brief Returns the numerator. */
                T numerator() const { return _num; }

                /** @brief Returns the denominator. */
                T denominator() const { return _den; }

                /**
                 * @brief Converts to double.
                 * @return The rational as a floating-point value, or 0.0 if invalid.
                 */
                double toDouble() const {
                        if (!isValid()) return 0.0;
                        return static_cast<double>(_num) / static_cast<double>(_den);
                }

                /**
                 * @brief Constructs a Rational from a @c double.
                 *
                 * Conservative: accepts only finite integer values
                 * (where the input round-trips exactly via
                 * @c static_cast<T>) and produces @c Rational(N, 1).
                 * Non-integer inputs intentionally fail with
                 * @c Error::ParseFailed — rationalization of an
                 * arbitrary double is ambiguous and lossy, and silent
                 * approximation tends to lose more information than
                 * it preserves.  Callers that have a known rational
                 * to encode should use the explicit @c Rational(num,
                 * den) constructor instead.
                 */
                static Result<Rational<T>> fromDouble(double val) {
                        if (!std::isfinite(val)) return makeError<Rational<T>>(Error::ParseFailed);
                        double rounded = std::nearbyint(val);
                        if (rounded != val) return makeError<Rational<T>>(Error::ParseFailed);
                        if (rounded < static_cast<double>(std::numeric_limits<T>::lowest()) ||
                            rounded > static_cast<double>(std::numeric_limits<T>::max())) {
                                return makeError<Rational<T>>(Error::OutOfRange);
                        }
                        return makeResult(Rational<T>(static_cast<T>(rounded), T(1)));
                }

                /** @brief Converts to a String in "num/den" format. */
                String toString() const { return String::dec(_num) + "/" + String::dec(_den); }

                /** @brief Implicit conversion to String. */
                operator String() const { return toString(); }

        private:
                T _num;
                T _den;

                void simplify() {
                        T gcd = std::gcd(_num, _den);
                        _num /= gcd;
                        _den /= gcd;
                        return;
                }
};

PROMEKI_NAMESPACE_END

/**
 * @brief @c std::formatter partial specialization for @ref promeki::Rational.
 *
 * Class templates need a hand-written partial specialization rather than
 * the @ref PROMEKI_FORMAT_VIA_TOSTRING macro (which only handles concrete
 * types).  Inherits from @ref promeki::ToStringFormatter so the standard
 * string format specifiers (width, fill, alignment) work automatically.
 */
template <typename T> struct std::formatter<promeki::Rational<T>> : promeki::ToStringFormatter<promeki::Rational<T>> {};

#endif // PROMEKI_ENABLE_CORE
