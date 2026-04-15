/**
 * @file      colormodel.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/colormodel.h>
#include <promeki/atomic.h>
#include <promeki/map.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered types
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{ColorModel::UserDefined};

ColorModel::ID ColorModel::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// Forward declaration — needed by conversion functions
static const ColorModel::Data &registryLookup(ColorModel::ID id);

// ---------------------------------------------------------------------------
// Transfer functions
// ---------------------------------------------------------------------------

static double transferLinear(double val) {
        return val;
}

static double transferSRGB(double val) {
        return val <= 0.0031308 ? 12.92 * val : 1.055 * std::pow(val, 1.0 / 2.4) - 0.055;
}

static double invTransferSRGB(double val) {
        return val <= 0.04045 ? val / 12.92 : std::pow((val + 0.055) / 1.055, 2.4);
}

static double transferRec709(double val) {
        return val < 0.018 ? 4.5 * val : 1.099 * std::pow(val, 0.45) - 0.099;
}

static double invTransferRec709(double val) {
        return val < 0.081 ? val / 4.5 : std::pow((val + 0.099) / 1.099, 1.0 / 0.45);
}

static double transferRec2020(double val) {
        const double alpha = 1.09929682680944;
        const double beta = 0.018053968510807;
        return val < beta ? 4.5 * val : alpha * std::pow(val, 0.45) - (alpha - 1.0);
}

static double invTransferRec2020(double val) {
        const double alpha = 1.09929682680944;
        const double beta = 0.018053968510807;
        return val < 4.5 * beta ? val / 4.5 : std::pow((val + (alpha - 1.0)) / alpha, 1.0 / 0.45);
}

// Adobe RGB (1998) uses a pure power function with gamma = 563/256
static double transferAdobeRGB(double val) {
        return std::pow(val, 256.0 / 563.0);
}

static double invTransferAdobeRGB(double val) {
        return std::pow(val, 563.0 / 256.0);
}

// ---------------------------------------------------------------------------
// Primaries for well-known color spaces
// ---------------------------------------------------------------------------
// Defined as functions (not file-scope statics) because the construct-on-first-use
// registry may be initialized before file-scope statics in this TU.

static ColorModel::Primaries primariesSRGB() {
        return { CIEPoint(0.64, 0.33), CIEPoint(0.30, 0.60),
                 CIEPoint(0.15, 0.06), CIEPoint(0.3127, 0.3290) };
}

static ColorModel::Primaries primariesRec601_PAL() {
        return { CIEPoint(0.64, 0.33), CIEPoint(0.29, 0.60),
                 CIEPoint(0.15, 0.06), CIEPoint(0.3127, 0.3290) };
}

static ColorModel::Primaries primariesRec601_NTSC() {
        return { CIEPoint(0.63, 0.34), CIEPoint(0.31, 0.595),
                 CIEPoint(0.155, 0.07), CIEPoint(0.3127, 0.3290) };
}

static ColorModel::Primaries primariesRec2020() {
        return { CIEPoint(0.708, 0.292), CIEPoint(0.170, 0.797),
                 CIEPoint(0.131, 0.046), CIEPoint(0.3127, 0.3290) };
}

static ColorModel::Primaries primariesDCI_P3() {
        return { CIEPoint(0.680, 0.320), CIEPoint(0.265, 0.690),
                 CIEPoint(0.150, 0.060), CIEPoint(0.3127, 0.3290) };
}

// Adobe RGB (1998): D65 white point, wider gamut than sRGB
static ColorModel::Primaries primariesAdobeRGB() {
        return { CIEPoint(0.6400, 0.3300), CIEPoint(0.2100, 0.7100),
                 CIEPoint(0.1500, 0.0600), CIEPoint(0.3127, 0.3290) };
}

// ACES AP0 (ACES 2065-1): encompasses all visible colors, D60 white
static ColorModel::Primaries primariesACES_AP0() {
        return { CIEPoint(0.7347, 0.2653), CIEPoint(0.0000, 1.0000),
                 CIEPoint(0.0001, -0.0770), CIEPoint(0.32168, 0.33767) };
}

// ACES AP1 (ACEScg): practical working space, D60 white
static ColorModel::Primaries primariesACES_AP1() {
        return { CIEPoint(0.713, 0.293), CIEPoint(0.165, 0.830),
                 CIEPoint(0.128, 0.044), CIEPoint(0.32168, 0.33767) };
}

// ---------------------------------------------------------------------------
// Normalized Primary Matrix computation
// ---------------------------------------------------------------------------

static Matrix3x3 computeRGBtoXYZ(const ColorModel::Primaries &p) {
        double xr = p[0].x(), yr = p[0].y();
        double xg = p[1].x(), yg = p[1].y();
        double xb = p[2].x(), yb = p[2].y();
        double xw = p[3].x(), yw = p[3].y();

        double Xr = xr / yr;
        double Zr = (1.0 - xr - yr) / yr;
        double Xg = xg / yg;
        double Zg = (1.0 - xg - yg) / yg;
        double Xb = xb / yb;
        double Zb = (1.0 - xb - yb) / yb;

        double Xw = xw / yw;
        double Yw = 1.0;
        double Zw = (1.0 - xw - yw) / yw;

        Matrix3x3 M;
        M.set(0, 0, (float)Xr); M.set(0, 1, (float)Xg); M.set(0, 2, (float)Xb);
        M.set(1, 0, 1.0f);      M.set(1, 1, 1.0f);      M.set(1, 2, 1.0f);
        M.set(2, 0, (float)Zr); M.set(2, 1, (float)Zg); M.set(2, 2, (float)Zb);

        Matrix3x3 Minv = M.inverse();
        float W[3] = { (float)Xw, (float)Yw, (float)Zw };
        Minv.vectorTransform(W);

        Matrix3x3 npm;
        npm.set(0, 0, M.get(0, 0) * W[0]); npm.set(0, 1, M.get(0, 1) * W[1]); npm.set(0, 2, M.get(0, 2) * W[2]);
        npm.set(1, 0, M.get(1, 0) * W[0]); npm.set(1, 1, M.get(1, 1) * W[1]); npm.set(1, 2, M.get(1, 2) * W[2]);
        npm.set(2, 0, M.get(2, 0) * W[0]); npm.set(2, 1, M.get(2, 1) * W[1]); npm.set(2, 2, M.get(2, 2) * W[2]);
        return npm;
}

// ---------------------------------------------------------------------------
// Conversion functions per model category
// ---------------------------------------------------------------------------

static void rgbGammaToXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float linear[3] = {
                (float)d->eotf(src[0]),
                (float)d->eotf(src[1]),
                (float)d->eotf(src[2])
        };
        d->rgbToXyz.vectorTransform(linear);
        dst[0] = linear[0]; dst[1] = linear[1]; dst[2] = linear[2];
}

static void rgbGammaFromXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float rgb[3] = { src[0], src[1], src[2] };
        d->xyzToRgb.vectorTransform(rgb);
        dst[0] = (float)d->oetf(rgb[0]);
        dst[1] = (float)d->oetf(rgb[1]);
        dst[2] = (float)d->oetf(rgb[2]);
}

static void rgbLinearToXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float linear[3] = { src[0], src[1], src[2] };
        d->rgbToXyz.vectorTransform(linear);
        dst[0] = linear[0]; dst[1] = linear[1]; dst[2] = linear[2];
}

static void rgbLinearFromXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float rgb[3] = { src[0], src[1], src[2] };
        d->xyzToRgb.vectorTransform(rgb);
        dst[0] = rgb[0]; dst[1] = rgb[1]; dst[2] = rgb[2];
}

static void xyzToXYZ(const ColorModel::Data *, const float *src, float *dst) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
}

static void xyzFromXYZ(const ColorModel::Data *, const float *src, float *dst) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
}

// --- CIE L*a*b* ---
static constexpr float LabWhiteX = 0.95047f;
static constexpr float LabWhiteY = 1.00000f;
static constexpr float LabWhiteZ = 1.08883f;

static float labF(float t) {
        constexpr float delta = 6.0f / 29.0f;
        constexpr float delta3 = delta * delta * delta;
        if(t > delta3) return std::cbrt(t);
        return t / (3.0f * delta * delta) + 4.0f / 29.0f;
}

static float labFInv(float t) {
        constexpr float delta = 6.0f / 29.0f;
        if(t > delta) return t * t * t;
        return 3.0f * delta * delta * (t - 4.0f / 29.0f);
}

static void labToXYZ(const ColorModel::Data *, const float *src, float *dst) {
        float L = src[0] * 100.0f;
        float a = src[1] * 255.0f - 128.0f;
        float b = src[2] * 255.0f - 128.0f;
        float fy = (L + 16.0f) / 116.0f;
        float fx = a / 500.0f + fy;
        float fz = fy - b / 200.0f;
        dst[0] = LabWhiteX * labFInv(fx);
        dst[1] = LabWhiteY * labFInv(fy);
        dst[2] = LabWhiteZ * labFInv(fz);
}

static void labFromXYZ(const ColorModel::Data *, const float *src, float *dst) {
        float fx = labF(src[0] / LabWhiteX);
        float fy = labF(src[1] / LabWhiteY);
        float fz = labF(src[2] / LabWhiteZ);
        float L = 116.0f * fy - 16.0f;
        float a = 500.0f * (fx - fy);
        float b = 200.0f * (fy - fz);
        dst[0] = L / 100.0f;
        dst[1] = (a + 128.0f) / 255.0f;
        dst[2] = (b + 128.0f) / 255.0f;
}

// --- HSV (derived from parent RGB) ---
static void hsvToXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float h = src[0] * 360.0f;
        float s = src[1];
        float v = src[2];

        float c = v * s;
        float hp = h / 60.0f;
        float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
        float r1, g1, b1;
        if(hp < 1.0f)      { r1 = c; g1 = x; b1 = 0; }
        else if(hp < 2.0f) { r1 = x; g1 = c; b1 = 0; }
        else if(hp < 3.0f) { r1 = 0; g1 = c; b1 = x; }
        else if(hp < 4.0f) { r1 = 0; g1 = x; b1 = c; }
        else if(hp < 5.0f) { r1 = x; g1 = 0; b1 = c; }
        else               { r1 = c; g1 = 0; b1 = x; }
        float m = v - c;
        float rgb[3] = { r1 + m, g1 + m, b1 + m };

        const auto &parent = registryLookup(d->parentModel);
        parent.toXYZFunc(&parent, rgb, dst);
}

static void hsvFromXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float rgb[3];
        const auto &parent = registryLookup(d->parentModel);
        parent.fromXYZFunc(&parent, src, rgb);

        float r = rgb[0], g = rgb[1], b = rgb[2];
        float cmax = std::fmax(r, std::fmax(g, b));
        float cmin = std::fmin(r, std::fmin(g, b));
        float delta = cmax - cmin;

        float h = 0.0f;
        if(delta > 0.0f) {
                if(cmax == r)      h = std::fmod((g - b) / delta + 6.0f, 6.0f);
                else if(cmax == g) h = (b - r) / delta + 2.0f;
                else               h = (r - g) / delta + 4.0f;
                h *= 60.0f;
        }

        float sv = (cmax > 0.0f) ? delta / cmax : 0.0f;
        dst[0] = h / 360.0f;
        dst[1] = sv;
        dst[2] = cmax;
}

// --- HSL (derived from parent RGB) ---
static void hslToXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float h = src[0] * 360.0f;
        float s = src[1];
        float l = src[2];

        float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
        float hp = h / 60.0f;
        float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
        float r1, g1, b1;
        if(hp < 1.0f)      { r1 = c; g1 = x; b1 = 0; }
        else if(hp < 2.0f) { r1 = x; g1 = c; b1 = 0; }
        else if(hp < 3.0f) { r1 = 0; g1 = c; b1 = x; }
        else if(hp < 4.0f) { r1 = 0; g1 = x; b1 = c; }
        else if(hp < 5.0f) { r1 = x; g1 = 0; b1 = c; }
        else               { r1 = c; g1 = 0; b1 = x; }
        float m = l - c * 0.5f;
        float rgb[3] = { r1 + m, g1 + m, b1 + m };

        const auto &parent = registryLookup(d->parentModel);
        parent.toXYZFunc(&parent, rgb, dst);
}

static void hslFromXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float rgb[3];
        const auto &parent = registryLookup(d->parentModel);
        parent.fromXYZFunc(&parent, src, rgb);

        float r = rgb[0], g = rgb[1], b = rgb[2];
        float cmax = std::fmax(r, std::fmax(g, b));
        float cmin = std::fmin(r, std::fmin(g, b));
        float delta = cmax - cmin;
        float l = (cmax + cmin) * 0.5f;

        float h = 0.0f;
        if(delta > 0.0f) {
                if(cmax == r)      h = std::fmod((g - b) / delta + 6.0f, 6.0f);
                else if(cmax == g) h = (b - r) / delta + 2.0f;
                else               h = (r - g) / delta + 4.0f;
                h *= 60.0f;
        }

        float s = (delta > 0.0f) ? delta / (1.0f - std::fabs(2.0f * l - 1.0f)) : 0.0f;

        dst[0] = h / 360.0f;
        dst[1] = s;
        dst[2] = l;
}

// --- YCbCr (matrix-derived from parent RGB) ---
static void ycbcrToXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float tmp[3] = {
                src[0] + d->toParentOffset[0],
                src[1] + d->toParentOffset[1],
                src[2] + d->toParentOffset[2]
        };
        d->toParentMatrix.vectorTransform(tmp);
        const auto &parent = registryLookup(d->parentModel);
        parent.toXYZFunc(&parent, tmp, dst);
}

static void ycbcrFromXYZ(const ColorModel::Data *d, const float *src, float *dst) {
        float rgb[3];
        const auto &parent = registryLookup(d->parentModel);
        parent.fromXYZFunc(&parent, src, rgb);
        d->fromParentMatrix.vectorTransform(rgb);
        dst[0] = rgb[0] + d->fromParentOffset[0];
        dst[1] = rgb[1] + d->fromParentOffset[1];
        dst[2] = rgb[2] + d->fromParentOffset[2];
}

// ---------------------------------------------------------------------------
// Helpers to set component info and init RGB matrices.
// CompInfo is set inline rather than from file-scope static arrays because
// the registry may be constructed before file-scope statics (CompInfo
// contains String which has non-trivial construction).
// ---------------------------------------------------------------------------

static void setRGBComps(ColorModel::Data &d) {
        d.comps[0] = { "Red",   "R", 0.0f, 1.0f };
        d.comps[1] = { "Green", "G", 0.0f, 1.0f };
        d.comps[2] = { "Blue",  "B", 0.0f, 1.0f };
}
static void setXYZComps(ColorModel::Data &d) {
        d.comps[0] = { "X", "X", 0.0f, 1.0f };
        d.comps[1] = { "Y", "Y", 0.0f, 1.0f };
        d.comps[2] = { "Z", "Z", 0.0f, 1.0f };
}
static void setLabComps(ColorModel::Data &d) {
        d.comps[0] = { "Lightness", "L", 0.0f, 100.0f };
        d.comps[1] = { "a",         "a", -128.0f, 127.0f };
        d.comps[2] = { "b",         "b", -128.0f, 127.0f };
}
static void setHSVComps(ColorModel::Data &d) {
        d.comps[0] = { "Hue",        "H", 0.0f, 360.0f };
        d.comps[1] = { "Saturation", "S", 0.0f, 1.0f };
        d.comps[2] = { "Value",      "V", 0.0f, 1.0f };
}
static void setHSLComps(ColorModel::Data &d) {
        d.comps[0] = { "Hue",        "H", 0.0f, 360.0f };
        d.comps[1] = { "Saturation", "S", 0.0f, 1.0f };
        d.comps[2] = { "Lightness",  "L", 0.0f, 1.0f };
}
static void setYCbCrComps(ColorModel::Data &d) {
        d.comps[0] = { "Luma",        "Y",  0.0f, 1.0f };
        d.comps[1] = { "Chroma Blue", "Cb", -0.5f, 0.5f };
        d.comps[2] = { "Chroma Red",  "Cr", -0.5f, 0.5f };
}
static void setInvalidComps(ColorModel::Data &d) {
        d.comps[0] = { "Invalid", "", 0.0f, 0.0f };
        d.comps[1] = { "Invalid", "", 0.0f, 0.0f };
        d.comps[2] = { "Invalid", "", 0.0f, 0.0f };
}

static void initRGBMatrices(ColorModel::Data &d) {
        d.rgbToXyz = computeRGBtoXYZ(d.primaries);
        d.xyzToRgb = d.rgbToXyz.inverse();
}

// ---------------------------------------------------------------------------
// Model data factory functions
// ---------------------------------------------------------------------------

static ColorModel::Data makeInvalid() {
        ColorModel::Data d;
        d.id = ColorModel::Invalid;
        d.type = ColorModel::TypeInvalid;
        d.name = "Invalid";
        d.desc = "Invalid color model";
        setInvalidComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.toXYZFunc = xyzToXYZ;
        d.fromXYZFunc = xyzFromXYZ;
        return d;
}

static ColorModel::Data makeSRGB() {
        ColorModel::Data d;
        d.id = ColorModel::sRGB;
        d.type = ColorModel::TypeRGB;
        d.name = "sRGB";
        d.desc = "IEC 61966-2-1 sRGB";
        d.primaries = primariesSRGB();
        setRGBComps(d);
        d.oetf = transferSRGB;
        d.eotf = invTransferSRGB;
        d.linear = false;
        d.linearCounterpart = ColorModel::LinearSRGB;
        d.nonlinearCounterpart = ColorModel::sRGB;
        d.toXYZFunc = rgbGammaToXYZ;
        d.fromXYZFunc = rgbGammaFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeLinearSRGB() {
        ColorModel::Data d;
        d.id = ColorModel::LinearSRGB;
        d.type = ColorModel::TypeRGB;
        d.name = "LinearSRGB";
        d.desc = "Linear sRGB";
        d.primaries = primariesSRGB();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.linearCounterpart = ColorModel::LinearSRGB;
        d.nonlinearCounterpart = ColorModel::sRGB;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeRec709() {
        ColorModel::Data d;
        d.id = ColorModel::Rec709;
        d.type = ColorModel::TypeRGB;
        d.name = "Rec709";
        d.desc = "ITU-R BT.709";
        d.primaries = primariesSRGB();
        setRGBComps(d);
        d.oetf = transferRec709;
        d.eotf = invTransferRec709;
        d.linear = false;
        d.linearCounterpart = ColorModel::LinearRec709;
        d.nonlinearCounterpart = ColorModel::Rec709;
        d.toXYZFunc = rgbGammaToXYZ;
        d.fromXYZFunc = rgbGammaFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeLinearRec709() {
        ColorModel::Data d;
        d.id = ColorModel::LinearRec709;
        d.type = ColorModel::TypeRGB;
        d.name = "LinearRec709";
        d.desc = "Linear ITU-R BT.709";
        d.primaries = primariesSRGB();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.linearCounterpart = ColorModel::LinearRec709;
        d.nonlinearCounterpart = ColorModel::Rec709;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeRec601PAL() {
        ColorModel::Data d;
        d.id = ColorModel::Rec601_PAL;
        d.type = ColorModel::TypeRGB;
        d.name = "Rec601_PAL";
        d.desc = "ITU-R BT.601 PAL";
        d.primaries = primariesRec601_PAL();
        setRGBComps(d);
        d.oetf = transferRec709;
        d.eotf = invTransferRec709;
        d.linear = false;
        d.linearCounterpart = ColorModel::LinearRec601_PAL;
        d.nonlinearCounterpart = ColorModel::Rec601_PAL;
        d.toXYZFunc = rgbGammaToXYZ;
        d.fromXYZFunc = rgbGammaFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeLinearRec601PAL() {
        ColorModel::Data d;
        d.id = ColorModel::LinearRec601_PAL;
        d.type = ColorModel::TypeRGB;
        d.name = "LinearRec601_PAL";
        d.desc = "Linear ITU-R BT.601 PAL";
        d.primaries = primariesRec601_PAL();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.linearCounterpart = ColorModel::LinearRec601_PAL;
        d.nonlinearCounterpart = ColorModel::Rec601_PAL;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeRec601NTSC() {
        ColorModel::Data d;
        d.id = ColorModel::Rec601_NTSC;
        d.type = ColorModel::TypeRGB;
        d.name = "Rec601_NTSC";
        d.desc = "ITU-R BT.601 NTSC";
        d.primaries = primariesRec601_NTSC();
        setRGBComps(d);
        d.oetf = transferRec709;
        d.eotf = invTransferRec709;
        d.linear = false;
        d.linearCounterpart = ColorModel::LinearRec601_NTSC;
        d.nonlinearCounterpart = ColorModel::Rec601_NTSC;
        d.toXYZFunc = rgbGammaToXYZ;
        d.fromXYZFunc = rgbGammaFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeLinearRec601NTSC() {
        ColorModel::Data d;
        d.id = ColorModel::LinearRec601_NTSC;
        d.type = ColorModel::TypeRGB;
        d.name = "LinearRec601_NTSC";
        d.desc = "Linear ITU-R BT.601 NTSC";
        d.primaries = primariesRec601_NTSC();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.linearCounterpart = ColorModel::LinearRec601_NTSC;
        d.nonlinearCounterpart = ColorModel::Rec601_NTSC;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeRec2020() {
        ColorModel::Data d;
        d.id = ColorModel::Rec2020;
        d.type = ColorModel::TypeRGB;
        d.name = "Rec2020";
        d.desc = "ITU-R BT.2020";
        d.primaries = primariesRec2020();
        setRGBComps(d);
        d.oetf = transferRec2020;
        d.eotf = invTransferRec2020;
        d.linear = false;
        d.linearCounterpart = ColorModel::LinearRec2020;
        d.nonlinearCounterpart = ColorModel::Rec2020;
        d.toXYZFunc = rgbGammaToXYZ;
        d.fromXYZFunc = rgbGammaFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeLinearRec2020() {
        ColorModel::Data d;
        d.id = ColorModel::LinearRec2020;
        d.type = ColorModel::TypeRGB;
        d.name = "LinearRec2020";
        d.desc = "Linear ITU-R BT.2020";
        d.primaries = primariesRec2020();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.linearCounterpart = ColorModel::LinearRec2020;
        d.nonlinearCounterpart = ColorModel::Rec2020;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeDCI_P3() {
        ColorModel::Data d;
        d.id = ColorModel::DCI_P3;
        d.type = ColorModel::TypeRGB;
        d.name = "DCI_P3";
        d.desc = "DCI-P3 Display (D65)";
        d.primaries = primariesDCI_P3();
        setRGBComps(d);
        d.oetf = transferSRGB;
        d.eotf = invTransferSRGB;
        d.linear = false;
        d.linearCounterpart = ColorModel::LinearDCI_P3;
        d.nonlinearCounterpart = ColorModel::DCI_P3;
        d.toXYZFunc = rgbGammaToXYZ;
        d.fromXYZFunc = rgbGammaFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeLinearDCI_P3() {
        ColorModel::Data d;
        d.id = ColorModel::LinearDCI_P3;
        d.type = ColorModel::TypeRGB;
        d.name = "LinearDCI_P3";
        d.desc = "Linear DCI-P3 Display (D65)";
        d.primaries = primariesDCI_P3();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.linearCounterpart = ColorModel::LinearDCI_P3;
        d.nonlinearCounterpart = ColorModel::DCI_P3;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeAdobeRGB() {
        ColorModel::Data d;
        d.id = ColorModel::AdobeRGB;
        d.type = ColorModel::TypeRGB;
        d.name = "AdobeRGB";
        d.desc = "Adobe RGB (1998)";
        d.primaries = primariesAdobeRGB();
        setRGBComps(d);
        d.oetf = transferAdobeRGB;
        d.eotf = invTransferAdobeRGB;
        d.linear = false;
        d.linearCounterpart = ColorModel::LinearAdobeRGB;
        d.nonlinearCounterpart = ColorModel::AdobeRGB;
        d.toXYZFunc = rgbGammaToXYZ;
        d.fromXYZFunc = rgbGammaFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeLinearAdobeRGB() {
        ColorModel::Data d;
        d.id = ColorModel::LinearAdobeRGB;
        d.type = ColorModel::TypeRGB;
        d.name = "LinearAdobeRGB";
        d.desc = "Linear Adobe RGB (1998)";
        d.primaries = primariesAdobeRGB();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.linearCounterpart = ColorModel::LinearAdobeRGB;
        d.nonlinearCounterpart = ColorModel::AdobeRGB;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

// ACES AP0 and AP1 are both linear-only working spaces
static ColorModel::Data makeACES_AP0() {
        ColorModel::Data d;
        d.id = ColorModel::ACES_AP0;
        d.type = ColorModel::TypeRGB;
        d.name = "ACES_AP0";
        d.desc = "ACES 2065-1 (AP0)";
        d.primaries = primariesACES_AP0();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeACES_AP1() {
        ColorModel::Data d;
        d.id = ColorModel::ACES_AP1;
        d.type = ColorModel::TypeRGB;
        d.name = "ACES_AP1";
        d.desc = "ACEScg (AP1)";
        d.primaries = primariesACES_AP1();
        setRGBComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.toXYZFunc = rgbLinearToXYZ;
        d.fromXYZFunc = rgbLinearFromXYZ;
        initRGBMatrices(d);
        return d;
}

static ColorModel::Data makeHSV_sRGB() {
        ColorModel::Data d;
        d.id = ColorModel::HSV_sRGB;
        d.type = ColorModel::TypeHSV;
        d.name = "HSV_sRGB";
        d.desc = "HSV (sRGB)";
        setHSVComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = false;
        d.parentModel = ColorModel::sRGB;
        d.toXYZFunc = hsvToXYZ;
        d.fromXYZFunc = hsvFromXYZ;
        return d;
}

static ColorModel::Data makeHSL_sRGB() {
        ColorModel::Data d;
        d.id = ColorModel::HSL_sRGB;
        d.type = ColorModel::TypeHSL;
        d.name = "HSL_sRGB";
        d.desc = "HSL (sRGB)";
        setHSLComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = false;
        d.parentModel = ColorModel::sRGB;
        d.toXYZFunc = hslToXYZ;
        d.fromXYZFunc = hslFromXYZ;
        return d;
}

static ColorModel::Data makeYCbCr_Rec709() {
        ColorModel::Data d;
        d.id = ColorModel::YCbCr_Rec709;
        d.type = ColorModel::TypeYCbCr;
        d.name = "YCbCr_Rec709";
        d.desc = "YCbCr (BT.709)";
        setYCbCrComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = false;
        d.parentModel = ColorModel::Rec709;

        float rgbToYcbcr[3][3] = {
                { 0.2126f,  0.7152f,  0.0722f},
                {-0.1146f, -0.3854f,  0.5f},
                { 0.5f,    -0.4542f, -0.0458f}
        };
        d.fromParentMatrix.set(rgbToYcbcr);
        d.fromParentOffset[0] = 0.0f;
        d.fromParentOffset[1] = 0.5f;
        d.fromParentOffset[2] = 0.5f;

        float ycbcrToRgb[3][3] = {
                {1.0f,  0.0f,     1.5748f},
                {1.0f, -0.1873f, -0.4681f},
                {1.0f,  1.8556f,  0.0f}
        };
        d.toParentMatrix.set(ycbcrToRgb);
        d.toParentOffset[0] = 0.0f;
        d.toParentOffset[1] = -0.5f;
        d.toParentOffset[2] = -0.5f;

        d.toXYZFunc = ycbcrToXYZ;
        d.fromXYZFunc = ycbcrFromXYZ;
        return d;
}

static ColorModel::Data makeYCbCr_Rec601() {
        ColorModel::Data d;
        d.id = ColorModel::YCbCr_Rec601;
        d.type = ColorModel::TypeYCbCr;
        d.name = "YCbCr_Rec601";
        d.desc = "YCbCr (BT.601)";
        setYCbCrComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = false;
        d.parentModel = ColorModel::Rec601_PAL;

        float rgbToYcbcr[3][3] = {
                { 0.299f,    0.587f,   0.114f},
                {-0.168736f,-0.331264f,0.5f},
                { 0.5f,     -0.418688f,-0.081312f}
        };
        d.fromParentMatrix.set(rgbToYcbcr);
        d.fromParentOffset[0] = 0.0f;
        d.fromParentOffset[1] = 0.5f;
        d.fromParentOffset[2] = 0.5f;

        float ycbcrToRgb[3][3] = {
                {1.0f,  0.0f,      1.402f},
                {1.0f, -0.344136f,-0.714136f},
                {1.0f,  1.772f,    0.0f}
        };
        d.toParentMatrix.set(ycbcrToRgb);
        d.toParentOffset[0] = 0.0f;
        d.toParentOffset[1] = -0.5f;
        d.toParentOffset[2] = -0.5f;

        d.toXYZFunc = ycbcrToXYZ;
        d.fromXYZFunc = ycbcrFromXYZ;
        return d;
}

static ColorModel::Data makeYCbCr_Rec2020() {
        ColorModel::Data d;
        d.id = ColorModel::YCbCr_Rec2020;
        d.type = ColorModel::TypeYCbCr;
        d.name = "YCbCr_Rec2020";
        d.desc = "YCbCr (BT.2020)";
        setYCbCrComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = false;
        d.parentModel = ColorModel::Rec2020;

        // BT.2020 luma coefficients (non-constant luminance)
        float rgbToYcbcr[3][3] = {
                { 0.2627f,   0.6780f,   0.0593f},
                {-0.13963f, -0.36037f,  0.5f},
                { 0.5f,     -0.45979f, -0.04021f}
        };
        d.fromParentMatrix.set(rgbToYcbcr);
        d.fromParentOffset[0] = 0.0f;
        d.fromParentOffset[1] = 0.5f;
        d.fromParentOffset[2] = 0.5f;

        float ycbcrToRgb[3][3] = {
                {1.0f,  0.0f,       1.4746f},
                {1.0f, -0.16455f,  -0.57135f},
                {1.0f,  1.8814f,    0.0f}
        };
        d.toParentMatrix.set(ycbcrToRgb);
        d.toParentOffset[0] = 0.0f;
        d.toParentOffset[1] = -0.5f;
        d.toParentOffset[2] = -0.5f;

        d.toXYZFunc = ycbcrToXYZ;
        d.fromXYZFunc = ycbcrFromXYZ;
        return d;
}

static ColorModel::Data makeCIEXYZ() {
        ColorModel::Data d;
        d.id = ColorModel::CIEXYZ;
        d.type = ColorModel::TypeXYZ;
        d.name = "CIEXYZ";
        d.desc = "CIE 1931 XYZ";
        setXYZComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = true;
        d.toXYZFunc = xyzToXYZ;
        d.fromXYZFunc = xyzFromXYZ;
        return d;
}

static ColorModel::Data makeCIELab() {
        ColorModel::Data d;
        d.id = ColorModel::CIELab;
        d.type = ColorModel::TypeLab;
        d.name = "CIELab";
        d.desc = "CIE L*a*b* (D65)";
        setLabComps(d);
        d.oetf = transferLinear;
        d.eotf = transferLinear;
        d.linear = false;
        d.toXYZFunc = labToXYZ;
        d.fromXYZFunc = labFromXYZ;
        return d;
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry
// ---------------------------------------------------------------------------

struct ColorModelRegistry {
        Map<ColorModel::ID, ColorModel::Data> entries;
        Map<String, ColorModel::ID> nameMap;

        ColorModelRegistry() {
                add(makeInvalid());
                add(makeSRGB());
                add(makeLinearSRGB());
                add(makeRec709());
                add(makeLinearRec709());
                add(makeRec601PAL());
                add(makeLinearRec601PAL());
                add(makeRec601NTSC());
                add(makeLinearRec601NTSC());
                add(makeRec2020());
                add(makeLinearRec2020());
                add(makeDCI_P3());
                add(makeLinearDCI_P3());
                add(makeAdobeRGB());
                add(makeLinearAdobeRGB());
                add(makeACES_AP0());
                add(makeACES_AP1());
                add(makeCIEXYZ());
                add(makeCIELab());
                add(makeHSV_sRGB());
                add(makeHSL_sRGB());
                add(makeYCbCr_Rec709());
                add(makeYCbCr_Rec601());
                add(makeYCbCr_Rec2020());
        }

        void add(ColorModel::Data d) {
                ColorModel::ID id = d.id;
                if(d.type != ColorModel::TypeInvalid) {
                        nameMap[d.name] = id;
                }
                entries[id] = std::move(d);
        }
};

static ColorModelRegistry &registry() {
        static ColorModelRegistry reg;
        return reg;
}

static const ColorModel::Data &registryLookup(ColorModel::ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return it->second;
        return reg.entries[ColorModel::Invalid];
}

// ---------------------------------------------------------------------------
// Static methods
// ---------------------------------------------------------------------------

const ColorModel::Data *ColorModel::lookupData(ID id) {
        return &registryLookup(id);
}

void ColorModel::registerData(Data &&data) {
        auto &reg = registry();
        if(data.type != TypeInvalid) {
                reg.nameMap[data.name] = data.id;
        }
        reg.entries[data.id] = std::move(data);
}

ColorModel ColorModel::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        return (it != reg.nameMap.end()) ? ColorModel(it->second) : ColorModel(Invalid);
}

ColorModel::IDList ColorModel::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                if(id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
