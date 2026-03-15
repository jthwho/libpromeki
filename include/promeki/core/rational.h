/**
 * @file      core/rational.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <numeric>
#include <sstream>
#include <algorithm>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Rational number with numerator and denominator.
 *
 * Represents an exact fraction.  Automatically simplifies on
 * construction.  A zero denominator produces an invalid Rational
 * that can be detected with isValid().
 *
 * @tparam T Underlying integer type (default: int).
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
                        if(_den != 0) simplify();
                }

                /** @brief Returns true if the denominator is non-zero. */
                bool isValid() const { return _den != 0; }

                /**
                 * @brief Adds two rationals.
                 * @param rhs Right-hand operand.
                 * @return Sum, or invalid Rational if either operand is invalid.
                 */
                Rational operator+(const Rational &rhs) const {
                        if(!isValid() || !rhs.isValid()) return Rational(0, 0);
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
                        if(!isValid() || !rhs.isValid()) return Rational(0, 0);
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
                        if(!isValid() || !rhs.isValid()) return Rational(0, 0);
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
                        if(!isValid() || !rhs.isValid() || rhs._num == 0) return Rational(0, 0);
                        T num = _num * rhs._den;
                        T den = _den * rhs._num;
                        return Rational(num, den);
                }

                /** @brief Equality comparison. */
                bool operator==(const Rational &rhs) const {
                        return _num == rhs._num && _den == rhs._den;
                }

                /** @brief Inequality comparison. */
                bool operator!=(const Rational &rhs) const {
                        return !(*this == rhs);
                }

                /**
                 * @brief Writes the rational as "num/den" to a stream.
                 * @param os Output stream.
                 * @param r  Rational to format.
                 * @return Reference to \p os.
                 */
                friend std::ostream& operator<<(std::ostream& os, const Rational& r) {
                        os << r._num << "/" << r._den;
                        return os;
                }

                /** @brief Returns the numerator. */
                T numerator() const { return _num; }

                /** @brief Returns the denominator. */
                T denominator() const { return _den; }

                /**
                 * @brief Converts to double.
                 * @return The rational as a floating-point value, or 0.0 if invalid.
                 */
                double toDouble() const {
                        if(!isValid()) return 0.0;
                        return static_cast<double>(_num) / static_cast<double>(_den);
                }

                /** @brief Converts to a String in "num/den" format. */
                String toString() const {
                        std::stringstream s;
                        s << *this;
                        return s.str();
                }

                /** @brief Implicit conversion to String. */
                operator String() const {
                        return toString();
                }

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
