/**
 * @file      color.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/color.h>
#include <promeki/error.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN


// ---------------------------------------------------------------------------
// 8-bit sRGB accessors
// ---------------------------------------------------------------------------

Color Color::ensureSRGB() const {
        if(_model == ColorModel::sRGB) return *this;
        return convert(ColorModel::sRGB);
}

static uint8_t floatToU8(float v) {
        return static_cast<uint8_t>(std::round(std::fmax(0.0f, std::fmin(1.0f, v)) * 255.0f));
}

uint8_t Color::r8() const {
        Color c = ensureSRGB();
        return floatToU8(c._c[0]);
}

uint8_t Color::g8() const {
        Color c = ensureSRGB();
        return floatToU8(c._c[1]);
}

uint8_t Color::b8() const {
        Color c = ensureSRGB();
        return floatToU8(c._c[2]);
}

uint8_t Color::a8() const {
        return floatToU8(_c[3]);
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

Color Color::convert(const ColorModel &target) const {
        if(!isValid() || !target.isValid()) return Color();
        if(_model == target) return *this;
        float xyz[3], dst[3];
        _model.toXYZ(_c, xyz);
        target.fromXYZ(xyz, dst);
        return Color(target, dst[0], dst[1], dst[2], _c[3]);
}

Color Color::toRGB() const { return convert(ColorModel::sRGB); }
Color Color::toLinearRGB() const { return convert(ColorModel::LinearSRGB); }
Color Color::toHSV() const { return convert(ColorModel::HSV_sRGB); }
Color Color::toHSL() const { return convert(ColorModel::HSL_sRGB); }
Color Color::toYCbCr709() const { return convert(ColorModel::YCbCr_Rec709); }
Color Color::toXYZ() const { return convert(ColorModel::CIEXYZ); }
Color Color::toLab() const { return convert(ColorModel::CIELab); }

// ---------------------------------------------------------------------------
// String parsing
// ---------------------------------------------------------------------------

// Parse "ModelName(c0,c1,c2,c3)" — lossless model format
static Color parseModelFunc(const String &str) {
        size_t paren = str.find('(');
        if(paren == String::npos) return Color();
        if(!str.endsWith(")")) return Color();

        String modelName = str.mid(0, paren);

        ColorModel model = ColorModel::lookup(modelName);

        if(!model.isValid()) return Color();

        String inner = str.mid(paren + 1, str.length() - paren - 2);
        StringList parts = inner.split(",");
        if(parts.size() != 4) return Color();

        Error err;
        float c0 = (float)parts[0].trim().toDouble(&err); if(err.isError()) return Color();
        float c1 = (float)parts[1].trim().toDouble(&err); if(err.isError()) return Color();
        float c2 = (float)parts[2].trim().toDouble(&err); if(err.isError()) return Color();
        float c3 = (float)parts[3].trim().toDouble(&err); if(err.isError()) return Color();
        return Color(model, c0, c1, c2, c3);
}

// Parse "rgb(r,g,b)" or "rgba(r,g,b,a)" with normalized 0.0-1.0 values (sRGB)
static Color parseFloatFunc(const String &str) {
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
        return Color(ColorModel::sRGB, (float)rv, (float)gv, (float)bv, (float)av);
}

Color Color::fromString(const String &str) {
        if(str.isEmpty()) return Color();

        // Try model notation: "ModelName(c0,c1,c2,c3)"
        {
                Color c = parseModelFunc(str);
                if(c.isValid()) return c;
        }

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
                return Color((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a);
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
        return Color((uint8_t)rv, (uint8_t)gv, (uint8_t)bv, (uint8_t)av);
}

// ---------------------------------------------------------------------------
// String output
// ---------------------------------------------------------------------------

String Color::toString(StringFormat fmt, AlphaMode alpha) const {
        // ModelFormat: lossless, always includes all 4 components
        if(fmt == ModelFormat) {
                return String::sprintf("%s(%.6g,%.6g,%.6g,%.6g)",
                        _model.name().cstr(), _c[0], _c[1], _c[2], _c[3]);
        }

        // sRGB-specific formats: convert if needed
        Color c = ensureSRGB();
        uint8_t ri = floatToU8(c._c[0]);
        uint8_t gi = floatToU8(c._c[1]);
        uint8_t bi = floatToU8(c._c[2]);
        uint8_t ai = floatToU8(c._c[3]);

        bool includeAlpha;
        switch(alpha) {
                case AlphaAlways: includeAlpha = true; break;
                case AlphaNever:  includeAlpha = false; break;
                default:          includeAlpha = (ai != 255); break;
        }

        switch(fmt) {
                case HexFormat:
                        return toHex(includeAlpha);

                case CSVFormat:
                        if(includeAlpha)
                                return String::sprintf("%u,%u,%u,%u", ri, gi, bi, ai);
                        return String::sprintf("%u,%u,%u", ri, gi, bi);

                case FloatFormat:
                        if(includeAlpha)
                                return String::sprintf("rgba(%.6g,%.6g,%.6g,%.6g)",
                                        c._c[0], c._c[1], c._c[2], c._c[3]);
                        return String::sprintf("rgb(%.6g,%.6g,%.6g)",
                                c._c[0], c._c[1], c._c[2]);

                default:
                        break;
        }
        return String();
}

String Color::toHex(bool includeAlpha) const {
        Color c = ensureSRGB();
        uint8_t ri = floatToU8(c._c[0]);
        uint8_t gi = floatToU8(c._c[1]);
        uint8_t bi = floatToU8(c._c[2]);
        uint8_t ai = floatToU8(c._c[3]);
        if(includeAlpha)
                return String::sprintf("#%02x%02x%02x%02x", ri, gi, bi, ai);
        return String::sprintf("#%02x%02x%02x", ri, gi, bi);
}

// ---------------------------------------------------------------------------
// Color operations
// ---------------------------------------------------------------------------

Color Color::lerp(const Color &other, double t) const {
        Color o = (other._model == _model) ? other : other.convert(_model);
        return Color(_model,
                (float)(_c[0] + (o._c[0] - _c[0]) * t),
                (float)(_c[1] + (o._c[1] - _c[1]) * t),
                (float)(_c[2] + (o._c[2] - _c[2]) * t),
                (float)(_c[3] + (o._c[3] - _c[3]) * t));
}

Color Color::inverted() const {
        return Color(_model, 1.0f - _c[0], 1.0f - _c[1], 1.0f - _c[2], _c[3]);
}

double Color::luminance() const {
        Color lin = convert(ColorModel::LinearSRGB);

        return 0.2126 * lin._c[0] + 0.7152 * lin._c[1] + 0.0722 * lin._c[2];
}

Color Color::contrastingBW() const {
        return luminance() > 0.5
                ? Color(0, 0, 0, a8())
                : Color(255, 255, 255, a8());
}

Color Color::complementary() const {
        Color hslColor = convert(ColorModel::HSL_sRGB);
        // Rotate hue by 0.5 (180 degrees in normalized space)
        float newH = hslColor._c[0] + 0.5f;
        if(newH >= 1.0f) newH -= 1.0f;
        Color rotated(ColorModel::HSL_sRGB, newH, hslColor._c[1], hslColor._c[2], hslColor._c[3]);
        return rotated.convert(_model);
}

float Color::toNative(size_t comp) const {
        if(comp == 3) return _c[3]; // Alpha is always 0-1
        return _model.toNative(comp, _c[comp]);
}

Color Color::fromNative(const ColorModel &model, float n0, float n1, float n2, float n3) {
        return Color(model,
                model.fromNative(0, n0),
                model.fromNative(1, n1),
                model.fromNative(2, n2),
                n3);
}

bool Color::isClose(const Color &other, float epsilon) const {
        if(_model != other._model) return false;
        return std::fabs(_c[0] - other._c[0]) <= epsilon &&
               std::fabs(_c[1] - other._c[1]) <= epsilon &&
               std::fabs(_c[2] - other._c[2]) <= epsilon &&
               std::fabs(_c[3] - other._c[3]) <= epsilon;
}

PROMEKI_NAMESPACE_END
