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

#include <cmath>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/array.h>

namespace promeki {

template <typename T, size_t NumValues> class Point {
        public:
               static Error fromString(const String &val, Point &d) {
                        std::stringstream ss(val);
                        ss >> std::ws;
                        for(size_t i = 0; i < NumValues; ++i) {
                                if(!(ss >> d[i])) return Error::Invalid;
                                ss >> std::ws;
                                if(i < NumValues - 1) {
                                        char c;
                                        if(!(ss >> c) || c != ',') return Error::Invalid;
                                }
                        }
                        return Error::Ok;
                }

                static Point fromString(const String &str, Error *err = nullptr) {
                        Array<T, NumValues> d;
                        Error e = fromString(str, d);
                        if(err != nullptr) *err = e;
                        return e.isOk() ? d : Point();
                }

                Point() : d{} {}
                Point(const Array<T, NumValues> &val) : d(val) {}
                template<typename... Args> Point(Args... args) : d{static_cast<T>(args)...} {}
                Point<T, NumValues>(const String &str) : d(fromString(str)) { }
                virtual ~Point() { }
 
                operator String() const {
                        return toString();
                }

                operator const Array<T, NumValues>&() {
                        return d;
                }

                bool operator==(const Array<T, NumValues> &val) const {
                        return d == val;
                }

                bool operator!=(const Array<T, NumValues> &val) const {
                        return d != val;
                }

                Point &operator+=(const Array<T, NumValues> &val) {
                        d += val;
                        return *this;
                }
                
                Point &operator-=(const Array<T, NumValues> &val) {
                        d -= val;
                        return *this;
                }

                Point &operator*=(const Array<T, NumValues> &val) {
                        d *= val;
                        return *this;
                }

                Point &operator/=(const Array<T, NumValues> &val) {
                        d /= val;
                        return *this;
                }
                
                template<size_t N = NumValues, typename std::enable_if_t<N >= 1, int> = 0> const T &x() const {
                        return d[0];
                }

                template<size_t N = NumValues, typename std::enable_if_t<N >= 1, int> = 0> T &x() {
                        return d[0];
                }

                template<size_t N = NumValues, typename std::enable_if_t<N >= 1, int> = 0> void setX(const T &val) {
                        d[0] = val;
                        return;
                }

                template<size_t N = NumValues, typename std::enable_if_t<N >= 2, int> = 0> const T &y() const {
                        return d[1];
                }

                template<size_t N = NumValues, typename std::enable_if_t<N >= 2, int> = 0> T &y() {
                        return d[1];
                }

                template<size_t N = NumValues, typename std::enable_if_t<N >= 2, int> = 0> void setY(const T &val) {
                        d[1] = val;
                        return;
                }

                template<size_t N = NumValues, typename std::enable_if_t<N >= 3, int> = 0> const T &z() const {
                        return d[2];
                }

                template<size_t N = NumValues, typename std::enable_if_t<N >= 3, int> = 0> T &z() {
                        return d[2];
                }

                template<size_t N = NumValues, typename std::enable_if_t<N >= 3, int> = 0> void setZ(const T &val) {
                        d[2] = val;
                        return;
                }

                String toString() const {
                        std::stringstream ss;
                        ss << d[0];
                        for(size_t i = 1; i < NumValues; ++i) ss << ", " << d[i];
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
                
                Point<T, NumValues> lerp(const Point<T, NumValues> &other, double t) const { return d.lerp(other, t); }

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

                bool isWithinBounds(const Point<T, NumValues>& min, const Point<T, NumValues>& max) const {
                        for (size_t i = 0; i < NumValues; ++i) {
                                if (d[i] < min.d[i] || d[i] > max.d[i]) {
                                        return false;
                                }
                        }
                        return true;
                }
                
                friend Point operator+(const Array<T, NumValues> &lh, const Array<T, NumValues> &rh) {
                        return Point(lh + rh);
                }
                
                friend Point operator-(const Array<T, NumValues> &lh, const Array<T, NumValues> &rh) {
                        return Point(lh - rh);
                }

                friend Point operator*(const Array<T, NumValues> &lh, const Array<T, NumValues> &rh) {
                        return Point(lh * rh);
                }

                friend Point operator/(const Array<T, NumValues> &lh, const Array<T, NumValues> &rh) {
                        return Point(lh / rh);
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
                Array<T, NumValues>     d;
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
