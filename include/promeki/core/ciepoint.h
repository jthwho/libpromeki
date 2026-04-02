/**
 * @file      core/ciepoint.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cmath>
#include <array>
#include <promeki/core/namespace.h>
#include <promeki/core/point.h>
#include <promeki/core/xyzcolor.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A point in the CIE 1931 xy chromaticity diagram.
 * @ingroup core_color
 *
 * The CIE xy chromaticity diagram is a 2D projection of the full 3D CIE XYZ
 * color space that removes the brightness (luminance) dimension, leaving only
 * the "color" information. The projection is computed as:
 *
 *     x = X / (X + Y + Z)
 *     y = Y / (X + Y + Z)
 *
 * This means that a CIEPoint describes *which* color something is (its hue
 * and saturation) but not *how bright* it is. The horseshoe-shaped boundary
 * of the diagram is called the spectral locus and corresponds to
 * monochromatic (single-wavelength) light from violet (~360 nm) through red
 * (~700 nm).
 *
 * CIEPoints are used primarily to define color space primaries and white
 * points. For example, the sRGB/Rec.709 color space defines its red primary
 * at (0.64, 0.33), green at (0.30, 0.60), blue at (0.15, 0.06), and the
 * D65 white point at (0.3127, 0.3290). These four CIEPoints, together with
 * a transfer function, fully define an RGB color space.
 *
 * This class also provides utility functions for converting between
 * wavelengths of visible light and chromaticity coordinates, and for
 * computing white points from correlated color temperatures (CCT).
 *
 * A default-constructed CIEPoint uses -1.0 for both coordinates to indicate
 * an invalid/uninitialized state.
 *
 * @par References
 * - CIE 015:2004, "Colorimetry".
 * - Bruce Lindbloom's color science website
 *   (http://www.brucelindbloom.com) -- the source for the
 *   CCT-to-white-point approximation used here.
 *
 * @see XYZColor for the full 3D color including luminance.
 * @see ColorModel for how primaries and white points define a color space.
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
                 * @brief Converts a wavelength of visible light to a CIE XYZ color.
                 *
                 * Uses the CIE 1931 2-degree standard observer color matching
                 * functions, interpolated from the built-in wavelength table.
                 * The wavelength must be in the visible range (360-700 nm).
                 *
                 * @param wavelength The wavelength in nanometers.
                 * @return The corresponding XYZColor, or an invalid XYZColor if
                 *         the wavelength is out of range.
                 */
                static XYZColor wavelengthToXYZ(double wavelength);

                /**
                 * @brief Converts a wavelength of visible light to a CIE xy chromaticity point.
                 *
                 * This gives the "pure spectral color" for that wavelength -- a
                 * point on the spectral locus of the chromaticity diagram.
                 *
                 * @param wavelength The wavelength in nanometers.
                 * @return The corresponding CIEPoint, or an invalid CIEPoint if
                 *         the wavelength is out of range.
                 */
                static CIEPoint wavelengthToCIEPoint(double wavelength);

                /**
                 * @brief Computes the white point for a correlated color temperature (CCT).
                 *
                 * The correlated color temperature describes the color of light
                 * emitted by an ideal black-body radiator heated to a given
                 * temperature. Common reference illuminants include:
                 * - D50 (~5003 K): used in graphic arts and prepress.
                 * - D55 (~5503 K): mid-morning/mid-afternoon daylight.
                 * - D65 (~6504 K): average noon daylight; the standard illuminant
                 *   for sRGB, Rec.709, and Rec.2020.
                 * - D75 (~7504 K): north-sky daylight.
                 *
                 * Uses Bruce Lindbloom's polynomial approximation, valid for
                 * 4000 K to 25000 K. Returns an invalid CIEPoint for
                 * out-of-range values.
                 *
                 * @param cct The correlated color temperature in Kelvin.
                 * @return The corresponding CIE xy white point, or an invalid CIEPoint.
                 *
                 * @par Reference
                 * Bruce Lindbloom, "CIE Color Calculator"
                 * (http://www.brucelindbloom.com/index.html?Eqn_T_to_xy.html).
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

                /** @brief Returns the x chromaticity coordinate. */
                double x() const { return d[0]; }

                /** @brief Returns the y chromaticity coordinate. */
                double y() const { return d[1]; }

                /** @brief Returns a const reference to the underlying data array. */
                const DataType &data() const { return d; }

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
