/**
 * @file      color.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/core/color.h>
#include <promeki/core/error.h>
#include <promeki/core/stringlist.h>

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

static Color parseFloatFunc(const String &str) {
        // Parse "rgb(r,g,b)" or "rgba(r,g,b,a)" with normalized 0.0-1.0 values
        String lower = str.toLower().trim();
        bool hasAlpha = lower.startsWith("rgba(");
        bool isRgb = lower.startsWith("rgb(");
        if(!hasAlpha && !isRgb) return Color();
        if(!lower.endsWith(")")) return Color();

        size_t prefixLen = hasAlpha ? 5 : 4;
        String inner = lower.mid(prefixLen, lower.length() - prefixLen - 1);
        StringList parts = inner.split(",");

        if(hasAlpha && parts.size() != 4) return Color();
        if(!hasAlpha && parts.size() != 3) return Color();

        Error err;
        double rv = parts[0].trim().toDouble(&err); if(err.isError()) return Color();
        double gv = parts[1].trim().toDouble(&err); if(err.isError()) return Color();
        double bv = parts[2].trim().toDouble(&err); if(err.isError()) return Color();
        double av = 1.0;
        if(hasAlpha) {
                av = parts[3].trim().toDouble(&err);
                if(err.isError()) return Color();
        }
        if(rv < 0.0 || rv > 1.0 || gv < 0.0 || gv > 1.0 ||
           bv < 0.0 || bv > 1.0 || av < 0.0 || av > 1.0) {
                return Color();
        }
        return Color(
                static_cast<uint8_t>(std::round(rv * 255.0)),
                static_cast<uint8_t>(std::round(gv * 255.0)),
                static_cast<uint8_t>(std::round(bv * 255.0)),
                static_cast<uint8_t>(std::round(av * 255.0))
        );
}

Color Color::fromString(const String &str) {
        if(str.isEmpty()) return Color();

        // Try functional notation: rgb(...) / rgba(...)
        {
                Color c = parseFloatFunc(str);
                if(c.isValid()) return c;
        }

        // Try hex format
        if(str.charAt(0) == '#') return fromHex(str);

        // Try named colors (case-insensitive)
        String lower = str.toLower();
        if(lower == "black")       return Black;
        if(lower == "white")       return White;
        if(lower == "red")         return Red;
        if(lower == "green")       return Green;
        if(lower == "blue")        return Blue;
        if(lower == "yellow")      return Yellow;
        if(lower == "cyan")        return Cyan;
        if(lower == "magenta")     return Magenta;
        if(lower == "darkgray" || lower == "darkgrey")   return DarkGray;
        if(lower == "lightgray" || lower == "lightgrey") return LightGray;
        if(lower == "orange")      return Orange;
        if(lower == "transparent") return Transparent;

        // Try comma-separated integer R,G,B or R,G,B,A
        StringList parts = str.split(",");
        if(parts.size() == 3 || parts.size() == 4) {
                Error err;
                int r = parts[0].trim().toInt(&err); if(err.isError()) return Color();
                int g = parts[1].trim().toInt(&err); if(err.isError()) return Color();
                int b = parts[2].trim().toInt(&err); if(err.isError()) return Color();
                int a = 255;
                if(parts.size() == 4) {
                        a = parts[3].trim().toInt(&err);
                        if(err.isError()) return Color();
                }
                if(r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 || a < 0 || a > 255) {
                        return Color();
                }
                return Color(r, g, b, a);
        }

        return Color();
}

static int parseHexByte(const String &str, size_t offset) {
        int hi = str.charAt(offset).hexDigitValue();
        int lo = str.charAt(offset + 1).hexDigitValue();
        if(hi < 0 || lo < 0) return -1;
        return (hi << 4) | lo;
}

Color Color::fromHex(const String &hex) {
        if(hex.isEmpty() || hex.charAt(0) != '#') return Color();
        size_t len = hex.length() - 1;
        if(len != 6 && len != 8) return Color();

        int rv = parseHexByte(hex, 1); if(rv < 0) return Color();
        int gv = parseHexByte(hex, 3); if(gv < 0) return Color();
        int bv = parseHexByte(hex, 5); if(bv < 0) return Color();
        int av = 255;
        if(len == 8) {
                av = parseHexByte(hex, 7);
                if(av < 0) return Color();
        }
        return Color(rv, gv, bv, av);
}

String Color::toString(StringFormat fmt, AlphaMode alpha) const {
        bool includeAlpha;
        switch(alpha) {
                case AlphaAlways: includeAlpha = true; break;
                case AlphaNever:  includeAlpha = false; break;
                default:          includeAlpha = (_a != 255); break;
        }

        switch(fmt) {
                case HexFormat:
                        return toHex(includeAlpha);

                case CSVFormat:
                        if(includeAlpha)
                                return String::sprintf("%u,%u,%u,%u", _r, _g, _b, _a);
                        return String::sprintf("%u,%u,%u", _r, _g, _b);

                case FloatFormat:
                        if(includeAlpha)
                                return String::sprintf("rgba(%.6g,%.6g,%.6g,%.6g)",
                                        _r / 255.0, _g / 255.0, _b / 255.0, _a / 255.0);
                        return String::sprintf("rgb(%.6g,%.6g,%.6g)",
                                _r / 255.0, _g / 255.0, _b / 255.0);
        }
        return String();
}

String Color::toHex(bool includeAlpha) const {
        if(includeAlpha)
                return String::sprintf("#%02x%02x%02x%02x", _r, _g, _b, _a);
        return String::sprintf("#%02x%02x%02x", _r, _g, _b);
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
