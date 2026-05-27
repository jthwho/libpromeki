/**
 * @file      xyzcolor.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/array.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/datatype.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;

/**
 * @brief A color in the CIE 1931 XYZ color space.
 * @ingroup color
 *
 * The CIE 1931 XYZ color space is a device-independent color space defined
 * by the International Commission on Illumination (CIE) in 1931. It serves
 * as the universal "connection space" for converting between all other color
 * spaces: to convert from color space A to color space B, you convert
 * A -> XYZ -> B.
 *
 * The three components are called tristimulus values:
 * - **X** relates roughly to a mixture of the long-wavelength (red) cone
 *   response, but is not perceptual red itself.
 * - **Y** corresponds directly to luminance (perceived brightness). A Y of
 *   1.0 represents the luminance of the reference white.
 * - **Z** relates roughly to the short-wavelength (blue) cone response.
 *
 * Unlike RGB values, XYZ components are non-negative but not bounded to
 * [0, 1]. The X and Z components can exceed 1.0 for highly saturated colors.
 * The Y component is normalized so that Y = 1.0 represents the white point.
 *
 * A default-constructed XYZColor uses -1.0 for all components to indicate
 * an invalid/uninitialized state.
 *
 * @par References
 * - CIE 015:2004, "Colorimetry" (the defining standard).
 * - Wyszecki & Stiles, *Color Science: Concepts and Methods, Quantitative
 *   Data and Formulae*, 2nd ed. (Wiley-Interscience, 1982; reprinted
 *   2000) -- the standard reference text for CIE colorimetry.
 *
 * @see CIEPoint for the 2D chromaticity projection of this space.
 * @see ColorModel for converting between XYZ and other color models.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance
 * is conditionally thread-safe — const operations are safe, but
 * concurrent mutation requires external synchronization.
 */
class XYZColor {
        public:
                PROMEKI_DATATYPE(XYZColor, DataTypeXYZColor, 1)

                /** @brief Writes three tagged doubles (X, Y, Z). */
                Error writeToStream(DataStream &s) const;
                /** @brief Reads three tagged doubles (X, Y, Z). */
                template <uint32_t V> static Result<XYZColor> readFromStream(DataStream &s);

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

                /**
                 * @brief Returns true if all components are non-negative.
                 *
                 * CIE XYZ components can exceed 1.0 for some colors, so only
                 * the -1.0 sentinel (from default construction) is considered invalid.
                 */
                bool isValid() const {
                        for (size_t i = 0; i < d.size(); ++i)
                                if (d[i] < 0.0) return false;
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
                void set(double x, double y, double z) { d = {x, y, z}; }
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
                String toString() const { return String::sprintf("XYZ(%g, %g, %g)", d[0], d[1], d[2]); }

                /**
                 * @brief Parses the @c "XYZ(x, y, z)" form produced by @ref toString.
                 *
                 * Bare comma-separated triples (without the @c XYZ wrapper or
                 * surrounding parens) are also accepted to ease handwritten
                 * config files.
                 */
                static Result<XYZColor> fromString(const String &s) {
                        String body = s.trim();
                        if (body.startsWith("XYZ(") || body.startsWith("xyz(")) body = body.mid(4);
                        if (body.startsWith("(")) body = body.mid(1);
                        if (body.endsWith(")")) body = body.left(body.length() - 1);
                        StringList parts = body.split(",");
                        if (parts.size() != 3) return makeError<XYZColor>(Error::ParseFailed);
                        XYZColor out;
                        for (size_t i = 0; i < 3; ++i) {
                                Error  ex;
                                double v = parts[i].trim().to<double>(&ex);
                                if (ex.isError()) return makeError<XYZColor>(Error::ParseFailed);
                                out.d[i] = v;
                        }
                        return makeResult(std::move(out));
                }

        private:
                DataType d;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::XYZColor);

#endif // PROMEKI_ENABLE_CORE
