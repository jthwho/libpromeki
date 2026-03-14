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
const Color Color::Ignored;

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

Color Color::complementary() const {
        // Convert RGB to HSL
        double rf = _r / 255.0;
        double gf = _g / 255.0;
        double bf = _b / 255.0;
        double maxC = std::fmax(rf, std::fmax(gf, bf));
        double minC = std::fmin(rf, std::fmin(gf, bf));
        double delta = maxC - minC;
        double l = (maxC + minC) / 2.0;
        double h = 0.0;
        double s = 0.0;

        if(delta > 0.0) {
                s = (l < 0.5) ? delta / (maxC + minC) : delta / (2.0 - maxC - minC);
                if(maxC == rf)      h = std::fmod((gf - bf) / delta, 6.0);
                else if(maxC == gf) h = (bf - rf) / delta + 2.0;
                else                h = (rf - gf) / delta + 4.0;
                h *= 60.0;
                if(h < 0.0) h += 360.0;
        }

        // Rotate hue 180 degrees
        h = std::fmod(h + 180.0, 360.0);

        // Convert HSL back to RGB
        auto hueToRgb = [](double p, double q, double t) -> double {
                if(t < 0.0) t += 1.0;
                if(t > 1.0) t -= 1.0;
                if(t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
                if(t < 1.0 / 2.0) return q;
                if(t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
                return p;
        };

        double ro, go, bo;
        if(s == 0.0) {
                ro = go = bo = l;
        } else {
                double q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
                double p = 2.0 * l - q;
                double hn = h / 360.0;
                ro = hueToRgb(p, q, hn + 1.0 / 3.0);
                go = hueToRgb(p, q, hn);
                bo = hueToRgb(p, q, hn - 1.0 / 3.0);
        }

        return Color(
                static_cast<uint8_t>(std::round(ro * 255.0)),
                static_cast<uint8_t>(std::round(go * 255.0)),
                static_cast<uint8_t>(std::round(bo * 255.0)),
                _a
        );
}

PROMEKI_NAMESPACE_END
