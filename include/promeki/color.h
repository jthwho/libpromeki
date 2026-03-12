/**
 * @file      color.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief General-purpose RGBA color.
 *
 * A simple value type storing red, green, blue, and alpha channels as
 * uint8_t values.  Provides named color constants, hex conversion, and
 * linear interpolation.
 */
class Color {
        public:
                /** @brief Default constructor. Creates an invalid (all-zero) color. */
                Color() = default;

                /** @brief Constructs a color from RGBA components. */
                Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
                        : _r(r), _g(g), _b(b), _a(a), _valid(true) {}

                /** @brief Returns true if this color was explicitly constructed. */
                bool isValid() const { return _valid; }

                /** @brief Returns the red component. */
                uint8_t r() const { return _r; }

                /** @brief Returns the green component. */
                uint8_t g() const { return _g; }

                /** @brief Returns the blue component. */
                uint8_t b() const { return _b; }

                /** @brief Returns the alpha component. */
                uint8_t a() const { return _a; }

                /** @brief Sets the red component. */
                void setR(uint8_t val) { _r = val; }

                /** @brief Sets the green component. */
                void setG(uint8_t val) { _g = val; }

                /** @brief Sets the blue component. */
                void setB(uint8_t val) { _b = val; }

                /** @brief Sets the alpha component. */
                void setA(uint8_t val) { _a = val; }

                /**
                 * @brief Creates a Color from a hex string.
                 *
                 * Accepts "#RRGGBB" or "#RRGGBBAA" format.
                 *
                 * @param hex The hex string.
                 * @return The parsed Color, or an invalid Color on failure.
                 */
                static Color fromHex(const String &hex);

                /**
                 * @brief Converts this color to a hex string.
                 * @param includeAlpha If true, includes the alpha channel.
                 * @return The hex string in "#RRGGBB" or "#RRGGBBAA" format.
                 */
                String toHex(bool includeAlpha = false) const;

                /**
                 * @brief Linearly interpolates between this color and another.
                 * @param other The target color.
                 * @param t The interpolation factor (0.0 = this, 1.0 = other).
                 * @return The interpolated color.
                 */
                Color lerp(const Color &other, double t) const;

                /** @brief Equality operator. */
                bool operator==(const Color &other) const {
                        return _r == other._r && _g == other._g &&
                               _b == other._b && _a == other._a;
                }

                /** @brief Inequality operator. */
                bool operator!=(const Color &other) const {
                        return !(*this == other);
                }

                // Named color constants
                static const Color Black;
                static const Color White;
                static const Color Red;
                static const Color Green;
                static const Color Blue;
                static const Color Yellow;
                static const Color Cyan;
                static const Color Magenta;
                static const Color DarkGray;
                static const Color LightGray;
                static const Color Orange;
                static const Color Transparent;

        private:
                uint8_t _r = 0;
                uint8_t _g = 0;
                uint8_t _b = 0;
                uint8_t _a = 0;
                bool    _valid = false;
};

PROMEKI_NAMESPACE_END
