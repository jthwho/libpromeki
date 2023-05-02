/*****************************************************************************
 * point2d.h
 * April 11, 2023
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

#include <iostream>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <functional>
#include <promeki/string.h>

namespace promeki {

template<typename T>
class __Point2D {
	public:
                static __Point2D<T> fromString(const String &str, bool *ok = nullptr) {
                        std::stringstream ss(str);
                        T x, y;
                        char comma;
                        ss >> std::ws >> x >> std::ws >> comma >> std::ws >> y;
                        if(ss.fail() || comma != ',') {
                                if(ok != nullptr) *ok = false;
                                return __Point2D<T>();
                        }
                        if(ok != nullptr) *ok = true;
                        return __Point2D<T>(x, y);
                }

                static bool collinear(const __Point2D<T>& p1, const __Point2D<T>& p2, const __Point2D<T>& p3) {
                        __Point2D<T> v1 = p2 - p1;
                        __Point2D<T> v2 = p3 - p2;
                        return std::abs(v1.cross(v2)) < std::numeric_limits<T>::epsilon();
                }
                
                static T triangleArea(const __Point2D<T>& p1, const __Point2D<T>& p2, const __Point2D<T>& p3) {
                        __Point2D<T> v1 = p2 - p1;
                        __Point2D<T> v2 = p3 - p1;
                        return std::abs(v1.cross(v2)) / 2;
                }

		__Point2D(T x = 0, T y = 0) : _x(x), _y(y) {}
                __Point2D(const __Point2D<T>& other) : _x(other._x), _y(other._y) {}
                __Point2D(const std::initializer_list<T>& list) :
                        _x(list.size() > 0 ? *(list.begin()) : 0),
                        _y(list.size() > 1 ? *(list.begin() + 1) : 0) {}
                virtual ~__Point2D() { }

                __Point2D<T>& operator=(const __Point2D<T>& other) {
                        _x = other._x;
			_y = other._y;
			return *this;
		}

                __Point2D<T>& operator=(const std::initializer_list<T>& list) {
                        _x = list.size() > 0 ? *(list.begin()) : 0;
                        _y = list.size() > 1 ? *(list.begin() + 1) : 0;
                        return *this;
                }

		const T &x() const {
			return _x;
		}

		const T &y() const {
			return _y;
		}

                void set(const T &valX, const T &valY) {
                        _x = valX;
                        _y = valY;
                        return;
                }

		void setX(const T &val) {
			_x = val;
                        return;
		}

		void setY(const T &val) {
			_y = val;
                        return;
		}

		__Point2D<T> operator+(const __Point2D<T>& other) const {
			return __Point2D<T>(_x + other._x, _y + other._y);
		}

		__Point2D<T> operator-(const __Point2D<T>& other) const {
			return __Point2D<T>(_x - other._x, _y - other._y);
		}

		__Point2D<T> operator*(const T &scalar) const {
			return __Point2D<T>(_x * scalar, _y * scalar);
		}

		__Point2D<T> operator/(const T &scalar) const {
			return __Point2D<T>(_x / scalar, _y / scalar);
		}

		bool operator==(const __Point2D<T>& other) const {
			return _x == other._x && _y == other._y;
		}

		bool operator!=(const __Point2D<T>& other) const {
			return !(*this == other);
		}

		T dot(const __Point2D<T>& other) const {
			return _x * other._x + _y * other._y;
		}

		T cross(const __Point2D<T>& other) const {
			return _x * other._y - _y * other._x;
		}

		T magnitude() const {
			return std::sqrt(_x * _x + _y * _y);
		}

		__Point2D<T> normalize() const {
			T mag = magnitude();
			return mag == 0 ? __Point2D<T>(0, 0) : __Point2D<T>(_x / mag, _y / mag);
		}

		__Point2D<T> rotate(const T &angle) const {
			T cosAngle = std::cos(angle);
			T sinAngle = std::sin(angle);
			return __Point2D<T>(_x * cosAngle - _y * sinAngle, _x * sinAngle + _y * cosAngle);
		}

		__Point2D<T> translate(const T &dx, const T &dy) const {
			return __Point2D<T>(_x + dx, _y + dy);
		}

		__Point2D<T> scale(const T &sx, const T &sy) const {
			return __Point2D<T>(_x * sx, _y * sy);
		}

                String toString() const {
                        String ret = String::number(_x) + ',' + String::number(_y);
                        return ret;
                }

                operator String() const {
                        return toString();
                }
                
                T distance(const __Point2D<T>& other) const {
                        T dx = _x - other._x;
                        T dy = _y - other._y;
                        return std::sqrt(dx * dx + dy * dy);
                }

                T manhattanDistance(const __Point2D<T>& other) const {
                        T dx = std::abs(_x - other._x);
                        T dy = std::abs(_y - other._y);
                        return dx + dy;
                }

                __Point2D<T> lerp(const __Point2D<T>& other, const T &weight) const {
                        T invWeight = static_cast<T>(1) - weight;
                        T x = invWeight * _x + weight * other._x;
                        T y = invWeight * _y + weight * other._y;
                        return __Point2D<T>(x, y);
                }

                __Point2D<T> clamp(const __Point2D<T>& minPoint, const __Point2D<T>& maxPoint) const {
                        T x = std::max(minPoint._x, std::min(_x, maxPoint._x));
                        T y = std::max(minPoint._y, std::min(_y, maxPoint._y));
                        return __Point2D<T>(x, y);
                }

                __Point2D<T> project(const __Point2D<T>& lineStart, const __Point2D<T>& lineEnd) const {
                        __Point2D<T> line = lineEnd - lineStart;
                        T lineLengthSq = line.dot(line);
                        if (lineLengthSq == 0) {
                                return lineStart;
                        }
                        T t = (_x - lineStart._x) * line._x + (_y - lineStart._y) * line._y;
                        t /= lineLengthSq;
                        return lineStart + line * t;
                }

                __Point2D<T> reflect(const __Point2D<T>& lineStart, const __Point2D<T>& lineEnd) const {
                        __Point2D<T> line = lineEnd - lineStart;
                        T lineLengthSq = line.dot(line);
                        if (lineLengthSq == 0) {
                                return *this;
                        }
                        T t = (_x - lineStart._x) * line._x + (_y - lineStart._y) * line._y;
                        t /= lineLengthSq;
                        __Point2D<T> proj = lineStart + line * t;
                        return proj * 2 - *this;
                }

                T angleTo(const __Point2D<T>& other) const {
                        __Point2D<T> delta = other - *this;
                        return std::atan2(delta._y, delta._x);
                }

                __Point2D<T> rotateAbout(const __Point2D<T>& pivot, const T angle) const {
                        __Point2D<T> delta = *this - pivot;
                        T s = std::sin(angle);
                        T c = std::cos(angle);
                        T x = delta._x * c - delta._y * s + pivot._x;
                        T y = delta._x * s + delta._y * c + pivot._y;
                        return __Point2D<T>(x, y);
                }

                bool isInside(T xmin, T ymin, T xmax, T ymax) const {
                        return _x >= xmin && _x <= xmax && _y >= ymin && _y <= ymax;
                }
                
        private:
		T _x;
		T _y;
};

template<typename T>
std::ostream& operator<<(std::ostream& os, const __Point2D<T>& point) {
	os << point.toString();
	return os;
}

using Point2Dd = __Point2D<double>;
using Point2Df = __Point2D<float>;
using Point2D = __Point2D<int>;

} // namespace promeki


