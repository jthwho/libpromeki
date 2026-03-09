/**
 * @file      rational.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <numeric>
#include <iostream>
#include <algorithm>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

template <typename T = int> class Rational {
        public:
                Rational(T n = 0, T d = 1) : _num(n), _den(d) {
                        if(_den == 0) throw std::invalid_argument("Denominator cannot be zero.");
                        simplify();
                }

                Rational operator+(const Rational &rhs) const {
                        T lcm = std::lcm(_den, rhs._den);
                        T num = num * (lcm / _den) + rhs._num * (lcm / rhs._den);
                        return Rational(num, lcm);
                }

                Rational operator-(const Rational &rhs) const {
                        T lcm = std::lcm(_den, rhs._den);
                        T num = _num * (lcm / _den) - rhs._num * (lcm / rhs._den);
                        return Rational(num, lcm);
                }

                Rational operator*(const Rational &rhs) const {
                        T num = _num * rhs._num;
                        T den = _den * rhs._den;
                        return Rational(num, den);
                }

                Rational operator/(const Rational &rhs) const {
                        if(rhs._num == 0) throw std::invalid_argument("Division by zero.");
                        T num = _num * rhs._den;
                        T den = _den * rhs._num;
                        return Rational(num, den);
                }

                bool operator==(const Rational &rhs) const {
                        return _num == rhs._num && _den == rhs._den;
                }

                bool operator!=(const Rational &rhs) const {
                        return !(*this == rhs);
                }

                friend std::ostream& operator<<(std::ostream& os, const Rational& r) {
                        os << r._num << "/" << r._den;
                        return os;
                }

                T numerator() const {
                    return _num;
                }

                T denominator() const {
                    return _den;
                }

                double toDouble() const {
                        return static_cast<double>(_num) / static_cast<double>(_den);
                }

                String toString() const {
                        std::stringstream s;
                        s << *this;
                        return s.str();
                }

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
                }
};

PROMEKI_NAMESPACE_END

