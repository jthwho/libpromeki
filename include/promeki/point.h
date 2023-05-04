/*****************************************************************************
 * point.h
 * May 02, 2023
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

#include <array>
#include <cmath>
#include <cstddef>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/logger.h>

namespace promeki {

template <typename T, size_t NumValues> class Point {
        public:
                static Point<T, NumValues> fromString(const String &val, Error *err = nullptr) {
                        Point<T, NumValues> p;
                        std::stringstream ss(val);
                        ss >> std::ws;
                        for(size_t i = 0; i < NumValues; ++i) {
                                if(!(ss >> p.d[i])) {
                                        if(err != nullptr) *err = Error::Invalid;
                                        return Point<T, NumValues>();
                                }
                                ss >> std::ws;
                                if(i < NumValues - 1) {
                                        char c;
                                        if(!(ss >> c) || c != ',') {
                                                if(err != nullptr) *err = Error::Invalid;
                                                return Point<T, NumValues>();
                                        }
                                }
                        }
                        return p;
                }

                static constexpr size_t Dimensions = NumValues;
                
                Point() : d{} { }

                Point(const String &str) : d(fromString(str)) { }

                template <typename... Args> Point(Args&&... args) : d{std::forward<Args>(args)...} {
                        static_assert(sizeof...(Args) == NumValues, "Incorrect number of arguments");
                }

                template <typename U, size_t OtherNumValues> Point(const Point<U, OtherNumValues>& other) {
                        static_assert(std::is_convertible_v<T, T>, "Incompatible types");
                        static_assert(OtherNumValues <= NumValues, "Incompatible sizes");
                        for(size_t i = 0; i < NumValues; ++i) {
                                d[i] = i < OtherNumValues ? other[i] : T{};
                        }
                }

                template <typename U> Point<T, NumValues>& operator=(const Point<U, NumValues>& other) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = static_cast<T>(other[i]);
                        }
                        return *this;
                }

                template <typename U, size_t OtherNumValues> Point<T, NumValues>& operator=(const Point<U, OtherNumValues>& other) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        static_assert(OtherNumValues <= NumValues, "Incompatible sizes");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = i < OtherNumValues ? static_cast<T>(other[i]) : T{};
                        }
                        return *this;
                }

                template <typename U> Point<T, NumValues>& operator=(U value) {
                        static_assert(std::is_assignable_v<T&, U>, "Incompatible types");
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] = static_cast<T>(value);
                        }
                        return *this;
                }

                T& operator[](size_t index) {
                        return d[index];
                }

                const T& operator[](size_t index) const {
                        return d[index];
                }

                T *data() {
                        return d.data();
                }

                const T*data() const {
                        return d.data();
                }

                Point<T, NumValues>& operator+=(const Point<T, NumValues>& other) {
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] += other[i];
                        }
                        return *this;
                }

                Point<T, NumValues>& operator-=(const Point<T, NumValues>& other) {
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] -= other[i];
                        }
                        return *this;
                }

                Point<T, NumValues>& operator*=(const T& scalar) {
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] *= scalar;
                        }
                        return *this;
                }

                Point<T, NumValues>& operator/=(const T& scalar) {
                        for (size_t i = 0; i < NumValues; ++i) {
                                d[i] /= scalar;
                        }
                        return *this;
                }
                
                operator String() const {
                        return toString();
                }

                T sum() const {
                        T ret{};
                        for(size_t i = 0; i < NumValues; ++i) ret += d[i];
                        return ret;
                }

                String toString() const {
                        std::stringstream ss;
                        ss << d[0];
                        for(size_t i = 1; i < NumValues; ++i) {
                                ss << ", " << d[i];
                        }
                        return ss.str();
                }
               
                template <typename U> double distanceTo(const Point<U, NumValues>& other) const {
                        double sum = 0;
                        for(size_t i = 0; i < NumValues; ++i) {
                                double diff = static_cast<double>(d[i]) - static_cast<double>(other[i]);
                                sum += diff * diff;
                        }
                        return std::sqrt(sum);
                }
                
                template <typename U> Point<T, NumValues> lerp(const Point<U, NumValues>& other, double t) const {
                        static_assert(std::is_floating_point_v<decltype(t)>, "T must be a floating-point type");
                        Point<T, NumValues> result;
                        for(size_t i = 0; i < NumValues; ++i) {
                                result[i] = static_cast<T>((1 - t) * static_cast<double>(d[i]) + t * static_cast<double>(other[i]));
                        }
                        return result;
                }

                template <typename U> Point<T, NumValues> clamp(
                                const Point<U, NumValues>& minVal, 
                                const Point<U, NumValues>& maxVal) const {
                        Point<T, NumValues> result;
                        for(size_t i = 0; i < NumValues; ++i) {
                                T val = d[i];
                                if (val < static_cast<T>(minVal[i])) val = static_cast<T>(minVal[i]);
                                if (val > static_cast<T>(maxVal[i])) val = static_cast<T>(maxVal[i]);
                                result[i] = val;
                        }
                        return result;
                }

                friend Point<T, NumValues> operator+(Point<T, NumValues> lhs, const Point<T, NumValues>& rhs) {
                        lhs += rhs;
                        return lhs;
                }

                friend Point<T, NumValues> operator-(Point<T, NumValues> lhs, const Point<T, NumValues>& rhs) {
                        lhs -= rhs;
                        return lhs;
                }

                friend Point<T, NumValues> operator*(Point<T, NumValues> lhs, const T& scalar) {
                        lhs *= scalar;
                        return lhs;
                }

                friend Point<T, NumValues> operator*(const T& scalar, Point<T, NumValues> rhs) {
                        rhs *= scalar;
                        return rhs;
                }

                friend Point<T, NumValues> operator/(Point<T, NumValues> lhs, const T& scalar) {
                        lhs /= scalar;
                        return lhs;
                }

                friend bool operator==(const Point<T, NumValues>& lhs, const Point<T, NumValues>& rhs) {
                        for(size_t i = 0; i < NumValues; ++i) {
                                if(lhs[i] != rhs[i]) {
                                        return false;
                                }
                        }
                        return true;
                }

                friend bool operator!=(const Point<T, NumValues>& lhs, const Point<T, NumValues>& rhs) {
                        return !(lhs == rhs);
                }

                friend std::ostream &operator<<(std::ostream &os, const Point<T, NumValues>& p) {
                        os << p.toString();
                        return os;
                }

                friend std::istream &operator>>(std::istream &is, Point<T, NumValues> &p) {
                        std::string str;
                        if(std::getline(is, str)) {
                                Error err;
                                p = Point<T, NumValues>::fromString(str, &err);
                                if(err.isError()) is.setstate(std::ios_base::failbit);
                        }
                        return is;
                }

        private:
                std::array<T, NumValues> d;
};

using Point2D = Point<int, 2>;
using Point2Df = Point<float, 2>;
using Point2Dd = Point<double, 2>;
using Point3D = Point<int, 3>;
using Point3Df = Point<float, 3>;
using Point3Dd = Point<double, 3>;
using Point4D = Point<int, 4>;
using Point4Df = Point<float, 4>;
using Point4Dd = Point<double, 4>;

} // namespace promeki
