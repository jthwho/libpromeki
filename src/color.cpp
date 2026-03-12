/**
 * @file      color.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <cmath>
#include <promeki/color.h>

PROMEKI_NAMESPACE_BEGIN

const Color Color::Black(0, 0, 0);
const Color Color::White(255, 255, 255);
const Color Color::Red(255, 0, 0);
const Color Color::Green(0, 255, 0);
const Color Color::Blue(0, 0, 255);
const Color Color::Yellow(255, 255, 0);
const Color Color::Cyan(0, 255, 255);
const Color Color::Magenta(255, 0, 255);
const Color Color::DarkGray(64, 64, 64);
const Color Color::LightGray(192, 192, 192);
const Color Color::Orange(255, 165, 0);
const Color Color::Transparent(0, 0, 0, 0);

Color Color::fromHex(const String &hex) {
        const char *str = hex.cstr();
        if(str == nullptr || str[0] != '#') return Color();
        size_t len = std::strlen(str + 1);
        unsigned int rv = 0, gv = 0, bv = 0, av = 255;
        if(len == 6) {
                if(std::sscanf(str, "#%02x%02x%02x", &rv, &gv, &bv) != 3) return Color();
        } else if(len == 8) {
                if(std::sscanf(str, "#%02x%02x%02x%02x", &rv, &gv, &bv, &av) != 4) return Color();
        } else {
                return Color();
        }
        return Color(static_cast<uint8_t>(rv), static_cast<uint8_t>(gv),
                     static_cast<uint8_t>(bv), static_cast<uint8_t>(av));
}

String Color::toHex(bool includeAlpha) const {
        char buf[10];
        if(includeAlpha) {
                std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", _r, _g, _b, _a);
        } else {
                std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", _r, _g, _b);
        }
        return String(buf);
}

Color Color::lerp(const Color &other, double t) const {
        auto mix = [t](uint8_t a, uint8_t b) -> uint8_t {
                return static_cast<uint8_t>(std::round(a + (b - a) * t));
        };
        return Color(mix(_r, other._r), mix(_g, other._g),
                     mix(_b, other._b), mix(_a, other._a));
}

PROMEKI_NAMESPACE_END
