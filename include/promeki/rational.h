/*****************************************************************************
 * rational.h
 * May 18, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

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

