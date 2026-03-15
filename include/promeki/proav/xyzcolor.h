/**
 * @file      proav/xyzcolor.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <sstream>
#include <promeki/core/namespace.h>
#include <promeki/core/array.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief CIE XYZ color value.
 *
 * Represents a color in the CIE 1931 XYZ color space as three double-precision
 * components. Valid component values are in the [0.0, 1.0] range. A
 * default-constructed XYZColor uses -1.0 for all components to indicate an
 * invalid/uninitialized state.
 */
class XYZColor {
        public:
                /** @brief Underlying storage type for the three XYZ components. */
                using DataType = Array<double, 3>;

                /** @brief Constructs an invalid XYZColor with all components set to -1.0. */
                XYZColor() : d{-1.0, -1.0, -1.0} {}

                /**
                 * @brief Constructs an XYZColor from a DataType array.
                 * @param val The array containing X, Y, and Z values.
                 */
                XYZColor(const DataType &val) : d(val) {}

                /**
                 * @brief Constructs an XYZColor from individual component values.
                 * @param x The X (luminance-related) component.
                 * @param y The Y (luminance) component.
                 * @param z The Z (blue-related) component.
                 */
                XYZColor(double x, double y, double z) : d(x, y, z) {}

                /** @brief Returns true if all components are in the valid [0.0, 1.0] range. */
                bool isValid() const {
                        for(size_t i = 0; i < d.size(); ++i) if(d[i] < 0.0 || d[i] > 1.0) return false;
                        return true;
                }

                /** @brief Returns a const reference to the underlying data array. */
                const DataType &data() const { return d; }
                /** @brief Returns a mutable reference to the underlying data array. */
                DataType &data() { return d; }
                /** @brief Returns the X component. */
                double x() { return d[0]; }
                /** @brief Returns the Y component. */
                double y() { return d[1]; }
                /** @brief Returns the Z component. */
                double z() { return d[2]; }
                /** @brief Sets all three XYZ components. */
                void set(double x, double y, double z) { d = { x, y, z }; }
                /** @brief Sets the X component. */
                void setX(double val) { d[0] = val; }
                /** @brief Sets the Y component. */
                void setY(double val) { d[1] = val; }
                /** @brief Sets the Z component. */
                void setZ(double val) { d[2] = val; }

                /**
                 * @brief Returns a linearly interpolated XYZColor between this and val.
                 * @param val The target color to interpolate towards.
                 * @param t   The interpolation factor, where 0.0 returns this color and 1.0 returns val.
                 * @return The interpolated XYZColor.
                 */
                XYZColor lerp(const XYZColor &val, double t) const { return d.lerp(val.d, t); }

                /** @brief Converts to a String using toString(). */
                operator String() { return toString(); }

                /** @brief Returns a string representation of the XYZ color. */
                String toString() const {
                        std::stringstream ss;
                        ss << "XYZ(" << d[0] << ", " << d[1] << ", " << d[2] << ")";
                        return ss.str();
                }
 
        private:
                DataType d;
};

PROMEKI_NAMESPACE_END

