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
#include <promeki/colormodel.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A color value in any supported color model.
 * @ingroup color
 *
 * Color is the primary type for representing and manipulating colors in
 * the library. It stores four float components (three color channels plus
 * alpha) together with a ColorModel that defines what those components
 * mean -- whether they are red/green/blue, hue/saturation/value,
 * luma/chroma, CIE L*a*b*, etc.
 *
 * @par Component storage
 *
 * All component values are stored normalized to 0.0-1.0. For models
 * where the conventional range differs (e.g. hue 0-360 degrees, Lab
 * lightness 0-100), the normalized values are mapped linearly from the
 * native range. Use toNative() and fromNative() to work in conventional
 * units. HDR content in linear RGB may have component values exceeding 1.0.
 *
 * The fourth component is always alpha (opacity), regardless of the color
 * model. Alpha 1.0 is fully opaque, 0.0 is fully transparent.
 *
 * @par Creating colors
 *
 * The most common ways to create a Color:
 *
 * @code
 * // From 8-bit sRGB values (backward compatible)
 * Color c(255, 128, 64);
 *
 * // Using a static factory for a specific model
 * Color red   = Color::srgb(1.0f, 0.0f, 0.0f);
 * Color green = Color::hsv(120.0f / 360.0f, 1.0f, 1.0f);
 *
 * // From a named constant
 * Color white = Color::White;
 *
 * // With an explicit model
 * Color c(ColorModel::Rec2020, 0.5f, 0.3f, 0.8f);
 *
 * // From a string
 * Color c = Color::fromString("sRGB(1,0,0,1)");       // model format
 * Color c = Color::fromString("#ff8040");              // hex (sRGB)
 * Color c = Color::fromString("rgb(0.5,0.3,0.1)");    // float (sRGB)
 * Color c = Color::fromString("red");                  // named (sRGB)
 * @endcode
 *
 * @par Converting between models
 *
 * Color can convert itself to any other color model:
 *
 * @code
 * Color red = Color::Red;
 * Color hsvRed = red.toHSV();       // H~0, S=1, V=1
 * Color labRed = red.toLab();       // perceptual coordinates
 * Color back   = labRed.toRGB();    // back to sRGB
 *
 * // Or to any arbitrary model
 * Color r2020 = red.convert(ColorModel::Rec2020);
 * @endcode
 *
 * All conversions pass through CIE XYZ as the connection space. They are
 * precise but not optimized for bulk pixel processing; use the library's
 * image pipeline for that.
 *
 * @par Serialization
 *
 * The default toString() format is lossless ModelFormat: "sRGB(1,0,0,1)".
 * This preserves the color model and all four components, and can be
 * parsed back by fromString(). The older sRGB-specific formats (hex,
 * csv, float functional) are still available for display and compatibility.
 *
 * @par Validity
 *
 * A default-constructed Color is invalid (model = ColorModel::Invalid).
 * This is used by TuiStyle and other systems to represent "no color
 * specified" for style inheritance. The static constant Color::Ignored
 * is an alias for this state.
 *
 * @par Thread Safety
 * Distinct instances may be used concurrently.  A single instance
 * is conditionally thread-safe — const operations are safe, but
 * concurrent mutation requires external synchronization.  The
 * referenced @ref ColorModel data is immutable and shared from a
 * thread-safe registry.
 *
 * @see ColorModel for the definition of supported color models.
 */
class Color {
        public:
                /**
                 * @brief Output format for toString().
                 *
                 * Controls how a Color is serialized to a string.
                 */
                enum StringFormat {
                        ModelFormat, ///< Lossless: "ModelName(c0,c1,c2,c3)" (default).
                        HexFormat,   ///< sRGB: "#RRGGBB" or "#RRGGBBAA".
                        CSVFormat,   ///< sRGB: "128,64,32" or "128,64,32,200".
                        FloatFormat, ///< sRGB: "rgb(0.502,0.251,0.125)" or "rgba(...)".
                };

                /**
                 * @brief Alpha-channel inclusion policy for toString().
                 */
                enum AlphaMode {
                        AlphaAuto,   ///< Include alpha only when it differs from 1.0.
                        AlphaAlways, ///< Always include alpha.
                        AlphaNever,  ///< Never include alpha.
                };

                /// @name Named color constants (sRGB)
                /// @{
                static const Color Black;       ///< Opaque black (0, 0, 0).
                static const Color White;       ///< Opaque white (255, 255, 255).
                static const Color Red;         ///< Opaque red (255, 0, 0).
                static const Color Green;       ///< Opaque green (0, 255, 0).
                static const Color Blue;        ///< Opaque blue (0, 0, 255).
                static const Color Yellow;      ///< Opaque yellow (255, 255, 0).
                static const Color Cyan;        ///< Opaque cyan (0, 255, 255).
                static const Color Magenta;     ///< Opaque magenta (255, 0, 255).
                static const Color DarkGray;    ///< Opaque dark gray (64, 64, 64).
                static const Color LightGray;   ///< Opaque light gray (192, 192, 192).
                static const Color Orange;      ///< Opaque orange (255, 165, 0).
                static const Color Transparent; ///< Fully transparent black (alpha 0).
                static const Color Ignored;     ///< Invalid/unset color sentinel (default-constructed).
                /// @}

                // --- Static factory helpers (one per well-known model) ---

                /** @brief Creates an sRGB color from normalized float components. */
                static Color srgb(float r, float g, float b, float a = 1.0f) {
                        return Color(ColorModel::sRGB, r, g, b, a);
                }

                /** @brief Creates a linear sRGB color. */
                static Color linearSrgb(float r, float g, float b, float a = 1.0f) {
                        return Color(ColorModel::LinearSRGB, r, g, b, a);
                }

                /** @brief Creates a Rec.709 color. */
                static Color rec709(float r, float g, float b, float a = 1.0f) {
                        return Color(ColorModel::Rec709, r, g, b, a);
                }

                /** @brief Creates an HSV color (relative to sRGB). H is normalized 0-1. */
                static Color hsv(float h, float s, float v, float a = 1.0f) {
                        return Color(ColorModel::HSV_sRGB, h, s, v, a);
                }

                /** @brief Creates an HSL color (relative to sRGB). H is normalized 0-1. */
                static Color hsl(float h, float s, float l, float a = 1.0f) {
                        return Color(ColorModel::HSL_sRGB, h, s, l, a);
                }

                /** @brief Creates a YCbCr (BT.709) color. */
                static Color ycbcr709(float y, float cb, float cr, float a = 1.0f) {
                        return Color(ColorModel::YCbCr_Rec709, y, cb, cr, a);
                }

                /** @brief Creates a YCbCr (BT.601) color. */
                static Color ycbcr601(float y, float cb, float cr, float a = 1.0f) {
                        return Color(ColorModel::YCbCr_Rec601, y, cb, cr, a);
                }

                /** @brief Creates a CIE XYZ color. */
                static Color xyz(float x, float y, float z, float a = 1.0f) {
                        return Color(ColorModel::CIEXYZ, x, y, z, a);
                }

                /** @brief Creates a CIE L*a*b* color (normalized components). */
                static Color lab(float l, float a, float b, float alpha = 1.0f) {
                        return Color(ColorModel::CIELab, l, a, b, alpha);
                }

                /**
                 * @brief Creates a Color from a hex string.
                 *
                 * Accepts "#RRGGBB" or "#RRGGBBAA" format. Always creates sRGB colors.
                 */
                static Color fromHex(const String &hex);

                /**
                 * @brief Creates a Color from a string representation.
                 *
                 * Tries the following formats in order:
                 *  1. Model notation: "ModelName(c0,c1,c2,c3)" where ModelName is a
                 *     known ColorModel name (e.g. "sRGB", "HSV_sRGB", "CIELab").
                 *  2. Functional notation: "rgb(r,g,b)" or "rgba(r,g,b,a)" with
                 *     normalized float values in the range 0.0 to 1.0 (sRGB).
                 *  3. Hex string: "#RRGGBB" or "#RRGGBBAA" (sRGB).
                 *  4. Named color (case-insensitive): "black", "white", "red", etc. (sRGB).
                 *  5. Comma-separated integer values: "128,64,32" or "128,64,32,200" (sRGB).
                 */
                static Color fromString(const String &str);

                /**
                 * @brief Creates a Color from native-range component values.
                 * @param model The color model.
                 * @param n0 First component in native units.
                 * @param n1 Second component in native units.
                 * @param n2 Third component in native units.
                 * @param n3 Alpha (always 0.0-1.0).
                 * @return A Color with components converted to normalized form.
                 */
                static Color fromNative(const ColorModel &model, float n0, float n1, float n2, float n3 = 1.0f);

                // --- Constructors ---

                /** @brief Default constructor. Creates an invalid color. */
                Color() = default;

                /**
                 * @brief Constructs a color with an explicit model and components.
                 * @param model The color model.
                 * @param c0    First component (e.g. Red, Hue, Luma).
                 * @param c1    Second component.
                 * @param c2    Third component.
                 * @param c3    Fourth component (alpha, default 1.0).
                 */
                Color(const ColorModel &model, float c0, float c1, float c2, float c3 = 1.0f)
                    : _c{c0, c1, c2, c3}, _model(model) {}

                /**
                 * @brief Disambiguation guard: constructs a color from a ColorModel::ID.
                 *
                 * This overload prevents the unscoped ColorModel::ID enum (which
                 * is integer-compatible) from silently matching the
                 * Color(uint8_t, uint8_t, uint8_t, uint8_t) constructor.
                 * See @ref tr_disambiguation "ID Disambiguation Guards".
                 */
                Color(ColorModel::ID id, float c0, float c1, float c2, float c3 = 1.0f)
                    : _c{c0, c1, c2, c3}, _model(id) {}

                /**
                 * @brief Constructs an sRGB color from 8-bit RGBA components.
                 *
                 * Provided for backward compatibility. Components are divided by 255.
                 */
                Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
                    : _c{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f}, _model(ColorModel::sRGB) {}

                // --- State ---

                /** @brief Returns true if this color has a valid model. */
                bool isValid() const { return _model.isValid(); }

                /** @brief Returns the color model. */
                const ColorModel &model() const { return _model; }

                // --- Generic component access ---

                /** @brief Returns component at the given index (0-3). */
                float comp(size_t index) const { return index < 4 ? _c[index] : 0.0f; }

                /** @brief Sets component at the given index (0-3). */
                void setComp(size_t index, float val) {
                        if (index < 4) _c[index] = val;
                }

                /** @brief Returns the alpha component. */
                float alpha() const { return _c[3]; }

                /** @brief Returns the alpha component (alias for alpha()). */
                float a() const { return _c[3]; }

                /** @brief Sets the alpha component. */
                void setAlpha(float val) { _c[3] = val; }

                /** @brief Sets the alpha component (alias for setAlpha()). */
                void setA(float val) { _c[3] = val; }

                // --- RGB accessors (model must be TypeRGB) ---

                /** @brief Returns the red component (for RGB models). */
                float r() const { return _c[0]; }

                /** @brief Returns the green component (for RGB models). */
                float g() const { return _c[1]; }

                /** @brief Returns the blue component (for RGB models). */
                float b() const { return _c[2]; }

                /** @brief Sets the red component. */
                void setR(float val) { _c[0] = val; }

                /** @brief Sets the green component. */
                void setG(float val) { _c[1] = val; }

                /** @brief Sets the blue component. */
                void setB(float val) { _c[2] = val; }

                // --- 8-bit sRGB accessors (convert to sRGB if needed) ---

                /** @brief Returns the red component as a uint8_t (0-255) in sRGB. */
                uint8_t r8() const;

                /** @brief Returns the green component as a uint8_t (0-255) in sRGB. */
                uint8_t g8() const;

                /** @brief Returns the blue component as a uint8_t (0-255) in sRGB. */
                uint8_t b8() const;

                /** @brief Returns the alpha component as a uint8_t (0-255). */
                uint8_t a8() const;

                // --- HSV accessors ---

                /** @brief Returns the hue component (for HSV/HSL models, normalized 0-1). */
                float h() const { return _c[0]; }

                /** @brief Returns the saturation component (for HSV/HSL models). */
                float s() const { return _c[1]; }

                /** @brief Returns the value component (for HSV models). */
                float v() const { return _c[2]; }

                /** @brief Returns the lightness component (for HSL/Lab models). */
                float l() const { return _c[0]; }

                // --- YCbCr accessors ---

                /** @brief Returns the luma component (for YCbCr models). */
                float y() const { return _c[0]; }

                /** @brief Returns the Cb component (for YCbCr models). */
                float cb() const { return _c[1]; }

                /** @brief Returns the Cr component (for YCbCr models). */
                float cr() const { return _c[2]; }

                // --- Conversion ---

                /**
                 * @brief Converts this color to a different color model.
                 * @param target The target color model.
                 * @return A new Color in the target model. Alpha is preserved.
                 */
                Color convert(const ColorModel &target) const;

                /** @brief Converts to sRGB. */
                Color toRGB() const;

                /** @brief Converts to linear sRGB. */
                Color toLinearRGB() const;

                /** @brief Converts to HSV (sRGB-based). */
                Color toHSV() const;

                /** @brief Converts to HSL (sRGB-based). */
                Color toHSL() const;

                /** @brief Converts to YCbCr (BT.709). */
                Color toYCbCr709() const;

                /** @brief Converts to CIE XYZ. */
                Color toXYZ() const;

                /** @brief Converts to CIE L*a*b*. */
                Color toLab() const;

                // --- String conversion ---

                /**
                 * @brief Converts this color to a string representation.
                 *
                 * The default ModelFormat produces a lossless representation
                 * "ModelName(c0,c1,c2,c3)" that preserves the color model and all
                 * components. The sRGB-specific formats (HexFormat, CSVFormat,
                 * FloatFormat) convert to sRGB internally if needed.
                 *
                 * @param fmt   Output format (default: ModelFormat).
                 * @param alpha Alpha inclusion policy (default: AlphaAuto, ignored for ModelFormat).
                 * @return The string representation.
                 */
                String toString(StringFormat fmt = ModelFormat, AlphaMode alpha = AlphaAuto) const;

                /**
                 * @brief Converts this color to a hex string.
                 *
                 * Converts to sRGB internally if needed.
                 *
                 * @param includeAlpha If true, includes the alpha channel.
                 * @return The hex string in "#RRGGBB" or "#RRGGBBAA" format.
                 */
                String toHex(bool includeAlpha = false) const;

                // --- Color operations ---

                /**
                 * @brief Linearly interpolates between this color and another.
                 * @param other The target color (converted to this model if different).
                 * @param t The interpolation factor (0.0 = this, 1.0 = other).
                 * @return The interpolated color in this model.
                 */
                Color lerp(const Color &other, double t) const;

                /**
                 * @brief Returns the RGB-inverted color (1.0 - each channel).
                 *
                 * Only meaningful for RGB models. Alpha is preserved.
                 */
                Color inverted() const;

                /**
                 * @brief Returns the perceptual luminance (0.0 to 1.0).
                 *
                 * Converts to linear sRGB and applies Rec. 709 luminance coefficients.
                 */
                double luminance() const;

                /**
                 * @brief Returns black or white, whichever contrasts best.
                 *
                 * Uses perceptual luminance to decide. Alpha is preserved.
                 */
                Color contrastingBW() const;

                /**
                 * @brief Returns the complementary color (hue rotated 180 degrees).
                 *
                 * Converts to HSL, rotates hue, converts back. Alpha is preserved.
                 */
                Color complementary() const;

                /**
                 * @brief Returns a component value in its native display range.
                 * @param comp Component index (0-2 for color, 3 for alpha).
                 * @return The value in native units (e.g. 0-360 for hue, 0-255 for 8-bit RGB).
                 */
                float toNative(size_t comp) const;

                /**
                 * @brief Approximate equality check.
                 * @param other The color to compare against.
                 * @param epsilon Maximum per-component difference.
                 * @return True if all components are within epsilon and models match.
                 */
                bool isClose(const Color &other, float epsilon = 1e-5f) const;

                /** @brief Equality operator. Exact float comparison, same model. */
                bool operator==(const Color &other) const {
                        return _model == other._model && _c[0] == other._c[0] && _c[1] == other._c[1] &&
                               _c[2] == other._c[2] && _c[3] == other._c[3];
                }

                /** @brief Inequality operator. */
                bool operator!=(const Color &other) const { return !(*this == other); }

        private:
                float      _c[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                ColorModel _model;

                // Helper: get sRGB version of this color (for string/hex output)
                Color ensureSRGB() const;
};

inline const Color Color::Black{(uint8_t)0, (uint8_t)0, (uint8_t)0};
inline const Color Color::White{(uint8_t)255, (uint8_t)255, (uint8_t)255};
inline const Color Color::Red{(uint8_t)255, (uint8_t)0, (uint8_t)0};
inline const Color Color::Green{(uint8_t)0, (uint8_t)255, (uint8_t)0};
inline const Color Color::Blue{(uint8_t)0, (uint8_t)0, (uint8_t)255};
inline const Color Color::Yellow{(uint8_t)255, (uint8_t)255, (uint8_t)0};
inline const Color Color::Cyan{(uint8_t)0, (uint8_t)255, (uint8_t)255};
inline const Color Color::Magenta{(uint8_t)255, (uint8_t)0, (uint8_t)255};
inline const Color Color::DarkGray{(uint8_t)64, (uint8_t)64, (uint8_t)64};
inline const Color Color::LightGray{(uint8_t)192, (uint8_t)192, (uint8_t)192};
inline const Color Color::Orange{(uint8_t)255, (uint8_t)165, (uint8_t)0};
inline const Color Color::Transparent{(uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)0};
inline const Color Color::Ignored{};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::Color);
