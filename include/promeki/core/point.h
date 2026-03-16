/**
 * @file      core/point.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cmath>
#include <type_traits>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/stringlist.h>
#include <promeki/core/error.h>
#include <promeki/core/logger.h>
#include <promeki/core/array.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief N-dimensional point with arithmetic, interpolation, and serialization.
 * @ingroup core_math
 *
 * A generic point class that stores NumValues components of type T, backed by
 * an Array. Named accessors (x, y, z) are conditionally available depending
 *
 * @par Example
 * @code
 * Point<float, 2> a(1.0f, 2.0f);
 * Point<float, 2> b(3.0f, 4.0f);
 * auto c = a + b;          // (4.0, 6.0)
 * float dist = a.dist(b);  // distance between a and b
 * @endcode
 * on the number of dimensions. Supports parsing from and formatting to
 * comma-separated strings.
 *
 * @tparam T         The component value type (e.g. int, float, double).
 * @tparam NumValues The number of dimensions.
 */
template <typename T, size_t NumValues> class Point {
        public:
               /**
                * @brief Parses a comma-separated string into a Point.
                * @param val The string to parse.
                * @param d The Point to populate with parsed values.
                * @return Error::Ok on success, Error::Invalid on failure.
                */
               static Error fromString(const String &val, Point &d) {
                        StringList parts = val.split(",");
                        if(parts.size() != NumValues) return Error::Invalid;
                        for(size_t i = 0; i < NumValues; ++i) {
                                Error err;
                                d[i] = static_cast<T>(parts[i].trim().toDouble(&err));
                                if(err.isError()) return Error::Invalid;
                        }
                        return Error::Ok;
                }

                /**
                 * @brief Parses a comma-separated string into a Point.
                 * @param str The string to parse.
                 * @param err Optional pointer to receive the error result.
                 * @return The parsed Point, or a default-constructed Point on failure.
                 */
                static Point fromString(const String &str, Error *err = nullptr) {
                        Array<T, NumValues> d;
                        Error e = fromString(str, d);
                        if(err != nullptr) *err = e;
                        return e.isOk() ? d : Point();
                }

                /** @brief Default constructor. Initializes all values to zero. */
                Point() : d{} {}

                /** @brief Constructs a Point from an Array. */
                Point(const Array<T, NumValues> &val) : d(val) {}

                /** @brief Constructs a Point from individual component values. */
                template<typename... Args> Point(Args... args) : d{static_cast<T>(args)...} {}

                /** @brief Constructs a Point by parsing a comma-separated string. */
                Point(const String &str) : d(fromString(str)) { }

                /** @brief Destructor. */
                virtual ~Point() { }
 
                /** @brief Converts the Point to a comma-separated String. */
                operator String() const {
                        return toString();
                }

                /** @brief Converts the Point to a const Array reference. */
                operator const Array<T, NumValues>&() const {
                        return d;
                }

                /** @brief Returns true if this Point equals the given Array. */
                bool operator==(const Array<T, NumValues> &val) const {
                        return d == val;
                }

                /** @brief Returns true if this Point does not equal the given Array. */
                bool operator!=(const Array<T, NumValues> &val) const {
                        return d != val;
                }

                /** @brief Adds the given Array component-wise to this Point. */
                Point &operator+=(const Array<T, NumValues> &val) {
                        d += val;
                        return *this;
                }

                /** @brief Subtracts the given Array component-wise from this Point. */
                Point &operator-=(const Array<T, NumValues> &val) {
                        d -= val;
                        return *this;
                }

                /** @brief Multiplies this Point component-wise by the given Array. */
                Point &operator*=(const Array<T, NumValues> &val) {
                        d *= val;
                        return *this;
                }

                /** @brief Divides this Point component-wise by the given Array. */
                Point &operator/=(const Array<T, NumValues> &val) {
                        d /= val;
                        return *this;
                }
                
                /** @brief Returns a const reference to the X component. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 1, int> = 0> const T &x() const {
                        return d[0];
                }

                /** @brief Returns a mutable reference to the X component. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 1, int> = 0> T &x() {
                        return d[0];
                }

                /** @brief Sets the X component to the given value. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 1, int> = 0> void setX(const T &val) {
                        d[0] = val;
                        return;
                }

                /** @brief Returns a const reference to the Y component. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 2, int> = 0> const T &y() const {
                        return d[1];
                }

                /** @brief Returns a mutable reference to the Y component. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 2, int> = 0> T &y() {
                        return d[1];
                }

                /** @brief Sets the Y component to the given value. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 2, int> = 0> void setY(const T &val) {
                        d[1] = val;
                        return;
                }

                /** @brief Returns a const reference to the Z component. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 3, int> = 0> const T &z() const {
                        return d[2];
                }

                /** @brief Returns a mutable reference to the Z component. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 3, int> = 0> T &z() {
                        return d[2];
                }

                /** @brief Sets the Z component to the given value. */
                template<size_t N = NumValues, typename std::enable_if_t<N >= 3, int> = 0> void setZ(const T &val) {
                        d[2] = val;
                        return;
                }

                /** @brief Returns a comma-separated string representation of the Point. */
                String toString() const {
                        String result;
                        for(size_t i = 0; i < NumValues; ++i) {
                                if(i > 0) result += ", ";
                                if constexpr (std::is_floating_point_v<T>) {
                                        result += String::number(static_cast<double>(d[i]));
                                } else {
                                        result += String::dec(static_cast<int64_t>(d[i]));
                                }
                        }
                        return result;
                }
               
                /**
                 * @brief Computes the Euclidean distance to another Point.
                 * @param other The other Point to measure distance to.
                 * @return The Euclidean distance as a double.
                 */
                template <typename U> double distanceTo(const Point<U, NumValues>& other) const {
                        double sum = 0;
                        for(size_t i = 0; i < NumValues; ++i) {
                                double diff = static_cast<double>(d[i]) - static_cast<double>(other.d[i]);
                                sum += diff * diff;
                        }
                        return std::sqrt(sum);
                }
                
                /**
                 * @brief Linearly interpolates between this Point and another.
                 * @param other The target Point to interpolate towards.
                 * @param t The interpolation factor, where 0.0 returns this Point and 1.0 returns other.
                 * @return The interpolated Point.
                 */
                Point<T, NumValues> lerp(const Point<T, NumValues> &other, double t) const { return d.lerp(other, t); }

                /**
                 * @brief Clamps each component of the Point to the given min and max bounds.
                 * @param minVal The minimum bounds per component.
                 * @param maxVal The maximum bounds per component.
                 * @return A new Point with each component clamped to the specified range.
                 */
                template <typename U> Point<T, NumValues> clamp(
                                const Point<U, NumValues>& minVal,
                                const Point<U, NumValues>& maxVal) const {
                        Point<T, NumValues> result;
                        for(size_t i = 0; i < NumValues; ++i) {
                                T val = d[i];
                                if (val < static_cast<T>(minVal.d[i])) val = static_cast<T>(minVal.d[i]);
                                if (val > static_cast<T>(maxVal.d[i])) val = static_cast<T>(maxVal.d[i]);
                                result.d[i] = val;
                        }
                        return result;
                }

                /**
                 * @brief Returns true if all components are within the given min and max bounds (inclusive).
                 * @param min The minimum bounds per component.
                 * @param max The maximum bounds per component.
                 * @return True if every component is within the specified range.
                 */
                bool isWithinBounds(const Point<T, NumValues>& min, const Point<T, NumValues>& max) const {
                        for (size_t i = 0; i < NumValues; ++i) {
                                if (d[i] < min.d[i] || d[i] > max.d[i]) {
                                        return false;
                                }
                        }
                        return true;
                }
                
                /** @brief Returns the component-wise sum of two Arrays as a Point. */
                friend Point operator+(const Array<T, NumValues> &lh, const Array<T, NumValues> &rh) {
                        return Point(lh + rh);
                }

                /** @brief Returns the component-wise difference of two Arrays as a Point. */
                friend Point operator-(const Array<T, NumValues> &lh, const Array<T, NumValues> &rh) {
                        return Point(lh - rh);
                }

                /** @brief Returns the component-wise product of two Arrays as a Point. */
                friend Point operator*(const Array<T, NumValues> &lh, const Array<T, NumValues> &rh) {
                        return Point(lh * rh);
                }

                /** @brief Returns the component-wise quotient of two Arrays as a Point. */
                friend Point operator/(const Array<T, NumValues> &lh, const Array<T, NumValues> &rh) {
                        return Point(lh / rh);
                }

        private:
                Array<T, NumValues>     d;
};

/** @brief 2D point with int32_t components. */
using Point2Di32 = Point<int32_t, 2>;
/** @brief 2D point with float components. */
using Point2Df = Point<float, 2>;
/** @brief 2D point with double components. */
using Point2Dd = Point<double, 2>;
/** @brief 3D point with int32_t components. */
using Point3Di32 = Point<int32_t, 3>;
/** @brief 3D point with float components. */
using Point3Df = Point<float, 3>;
/** @brief 3D point with double components. */
using Point3Dd = Point<double, 3>;
/** @brief 4D point with int32_t components. */
using Point4Di32 = Point<int32_t, 4>;
/** @brief 4D point with float components. */
using Point4Df = Point<float, 4>;
/** @brief 4D point with double components. */
using Point4Dd = Point<double, 4>;

PROMEKI_NAMESPACE_END

