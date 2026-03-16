/**
 * @file      proav/ciepoint.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cmath>
#include <array>
#include <promeki/core/namespace.h>
#include <promeki/core/point.h>
#include <promeki/proav/xyzcolor.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A point in the CIE 1931 xy chromaticity diagram.
 * @ingroup proav_color
 *
 * Represents a two-dimensional chromaticity coordinate (x, y) and provides
 * conversions between wavelengths, correlated color temperatures, and
 * CIE XYZ color space.
 */
class CIEPoint {
        public:
                /** @brief Underlying storage type for the x and y coordinates. */
                using DataType = Array<double, 2>;

                /** @brief Minimum visible wavelength in nanometers. */
                static constexpr double MinWavelength = 360;

                /** @brief Maximum visible wavelength in nanometers. */
                static constexpr double MaxWavelength = 700;

                //static const CIEPoint D50;
                //static const CIEPoint D55;

                /**
                 * @brief Checks whether a wavelength is within the visible range.
                 * @param val The wavelength in nanometers.
                 * @return True if @p val is between MinWavelength and MaxWavelength inclusive.
                 */
                static bool isValidWavelength(double val) {
                        return val >= MinWavelength && val <= MaxWavelength;
                }

                /**
                 * @brief Converts a wavelength to a CIE XYZ color.
                 * @param wavelength The wavelength in nanometers.
                 * @return The corresponding XYZColor.
                 */
                static XYZColor wavelengthToXYZ(double wavelength);

                /**
                 * @brief Converts a wavelength to a CIE xy chromaticity point.
                 * @param wavelength The wavelength in nanometers.
                 * @return The corresponding CIEPoint.
                 */
                static CIEPoint wavelengthToCIEPoint(double wavelength);

                /**
                 * @brief Computes the white point for a correlated color temperature.
                 *
                 * Uses Bruce Lindbloom's approximation for CCT values between
                 * 4000 K and 25000 K. Returns an invalid CIEPoint for out-of-range values.
                 *
                 * @param cct The correlated color temperature in Kelvin.
                 * @return The corresponding CIE xy white point, or an invalid CIEPoint.
                 */
                static CIEPoint colorTempToWhitePoint(double cct) {
                        // from http://www.brucelindbloom.com/index.html?Eqn_T_to_xy.html
                        double x, y;
                        if(cct < 4000) {
                                return CIEPoint(); // Invalid.
                        } else if(cct <= 7000.0) {
                                x = (-4.6070e9  / std::pow(cct, 3)) +
                                    ( 2.9678e6  / std::pow(cct, 2)) +
                                    ( 0.09911e3 / cct) + 0.244063;
                        } else if(cct <= 25000) {
                                x = (-2.0064e9  / std::pow(cct, 3)) +
                                    ( 1.9108e6  / std::pow(cct, 2)) +
                                    ( 0.24748e3 / cct) + 0.237040;
                        } else {
                                return CIEPoint(); // Invalid
                        }
                        y = (-3.0 * std::pow(x, 2)) + (2.870 * x) - 0.275;
                        return CIEPoint(x, y);
                }

                /**
                 * @brief Constructs a CIEPoint with the given chromaticity coordinates.
                 * @param x The x chromaticity coordinate (default -1.0, indicating invalid).
                 * @param y The y chromaticity coordinate (default -1.0, indicating invalid).
                 */
                CIEPoint(double x = -1.0, double y = -1.0) : d(x, y) { }

                /**
                 * @brief Constructs a CIEPoint from raw coordinate data.
                 * @param other The coordinate array to copy from.
                 */
                CIEPoint(const DataType &other) : d(other) { }

                /**
                 * @brief Checks whether this point lies within valid chromaticity bounds.
                 * @return True if both coordinates are within the valid CIE xy range.
                 */
                bool isValid() const {
                        return d.isBetween(DataType(0.0, 0.0), DataType(0.8, 0.9));
                }

                /**
                 * @brief Linearly interpolates between this point and another.
                 * @param other The target CIEPoint.
                 * @param t     Interpolation factor (0.0 = this, 1.0 = other).
                 * @return The interpolated CIEPoint.
                 */
                CIEPoint lerp(const CIEPoint &other, double t) const {
                        return d.lerp(other.d, t);
                }
#if 0 
                XYZ toXYZ(double Y = 1.0) const {
                        double X = (x() * Y) / y();
                        double Z = ((1.0 - x() - y()) * Y) / y();
                        return {X, Y, Z};
                }

                bool isInGamut() const {
                        auto [X, Y, Z] = toXYZ();
                        double u = (4.0 * X) / (-2.0 * X + 12.0 * Y + 3.0 * Z);
                        double v = (9.0 * Y) / (-2.0 * X + 12.0 * Y + 3.0 * Z);
                        return u >= 0 && u <= 0.6 && v >= 0 && v <= 0.6 && u + v <= 0.6;
                }

                double colorTemp() const {
                        // McCamy's approximation
                        double n = (x() - 0.3320) / (0.1858 - y());
                        return 437.0 * std::pow(n, 3.0) + 
                               3601.0 * std::pow(n, 2.0) + 
                               6861.0 * n + 5517;
                }
#endif
        private:
                DataType d;

};

PROMEKI_NAMESPACE_END

