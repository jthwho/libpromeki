/**
 * @file      core/colormodel.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/array.h>
#include <promeki/core/ciepoint.h>
#include <promeki/core/matrix3x3.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Single source of truth for a color model and its associated color space.
 * @ingroup core_color
 *
 * A "color model" defines how color is represented mathematically (RGB, HSV,
 * YCbCr, etc.), while a "color space" pins that model to specific real-world
 * colors by defining primaries, a white point, and a transfer function. This
 * class combines both concepts: ColorModel::sRGB, for example, specifies
 * not just "RGB" but the exact sRGB primaries, D65 white point, and the
 * IEC 61966-2-1 transfer curve.
 *
 * @par Color model categories
 *
 * **Primary RGB models** (sRGB, Rec.709, Rec.601, Rec.2020) are defined by
 * three chromaticity primaries, a white point, and a transfer function (the
 * "gamma curve" that maps between linear light and encoded values). Each
 * primary RGB model has a corresponding linear variant (e.g. LinearSRGB)
 * with the same primaries but an identity transfer function.
 *
 * **Derived models** (HSV, HSL) are mathematical rearrangements of a parent
 * RGB model. HSV and HSL separate hue, saturation, and brightness/lightness
 * into independent axes, which is useful for color pickers and artistic
 * manipulation. They are not device-independent on their own -- they inherit
 * the gamut and white point of their parent.
 *
 * **Matrix-derived models** (YCbCr) separate an RGB signal into a luma (Y)
 * component and two chroma-difference components (Cb, Cr). This separation
 * is fundamental to video compression because the human eye is less
 * sensitive to chroma detail than luma detail, so Cb and Cr can be
 * subsampled (e.g. 4:2:2 or 4:2:0) to save bandwidth.
 *
 * **CIE models** (XYZ, Lab) are device-independent. CIE XYZ is the
 * connection space through which all conversions pass. CIE L*a*b* is a
 * perceptually uniform rearrangement of XYZ: equal numerical distances in
 * L*a*b* correspond roughly to equal perceived color differences, making
 * it useful for measuring color accuracy and computing color distances.
 *
 * @par Conversion pipeline
 *
 * All conversions between color models pass through CIE XYZ as the
 * connection space:
 *
 *     source model -> CIE XYZ -> target model
 *
 * For an RGB model, "source -> XYZ" means: remove the transfer function
 * (EOTF) to get linear RGB, then multiply by the 3x3 Normalized Primary
 * Matrix (NPM) computed from the chromaticity primaries and white point.
 * The reverse direction is the inverse matrix followed by applying the
 * transfer function (OETF). See ColorModel::toXYZ() and
 * ColorModel::fromXYZ().
 *
 * @par Transfer functions (gamma)
 *
 * Most RGB color spaces store encoded (non-linear) values rather than
 * linear light intensities. This encoding is called a transfer function:
 * - The **OETF** (Opto-Electronic Transfer Function) converts from linear
 *   light to encoded values (what a camera does).
 * - The **EOTF** (Electro-Optical Transfer Function) converts from encoded
 *   values back to linear light (what a display does).
 *
 * For sRGB, the OETF is approximately a power curve of 1/2.4, with a
 * linear segment near black. For Rec.709 and Rec.601, the curve is
 * similar but uses different constants (power of 0.45). These curves are
 * often loosely called "gamma" although the precise definitions differ
 * between standards.
 *
 * The linear variants (LinearSRGB, LinearRec709, etc.) have identity
 * transfer functions; their values represent linear light directly.
 *
 * @par Component normalization
 *
 * All component values stored by Color are normalized to 0.0-1.0 internally.
 * The native display range for each component (e.g. 0-360 for hue, 0-100
 * for Lab lightness) is available via compInfo() and the toNative()/
 * fromNative() helpers.
 *
 * @par Implementation
 *
 * ColorModel is a lightweight value type that stores only a pointer to
 * immutable internal data. Construction from an ID resolves the pointer via
 * a construct-on-first-use registry, so ColorModel values can be safely
 * used in static initializers without cross-translation-unit ordering
 * concerns. Copying a ColorModel is trivial (pointer copy).
 *
 * This class is not thread-safe. Instances are immutable after construction,
 * so concurrent reads of the same instance from multiple threads are safe.
 *
 * @par References
 * - IEC 61966-2-1:1999, "Multimedia systems and equipment -- Colour
 *   measurement and management -- Part 2-1: Colour management -- Default
 *   RGB colour space -- sRGB" (the sRGB standard).
 * - ITU-R BT.709-6, "Parameter values for the HDTV standards for
 *   production and international programme exchange" (Rec.709).
 * - ITU-R BT.601-7, "Studio encoding parameters of digital television for
 *   standard 4:3 and wide-screen 16:9 aspect ratios" (Rec.601).
 * - ITU-R BT.2020-2, "Parameter values for ultra-high definition television
 *   systems for production and international programme exchange" (Rec.2020).
 * - Charles Poynton, *Digital Video and HD: Algorithms and Interfaces*,
 *   2nd ed. (Morgan Kaufmann, 2012) -- an excellent practical reference for
 *   RGB, YCbCr, transfer functions, and broadcast color spaces.
 * - Mark D. Fairchild, *Color Appearance Models*, 3rd ed. (Wiley, 2013) --
 *   thorough treatment of CIE colorimetry and perceptual color models.
 *
 * @see Color for the user-facing type that combines components with a ColorModel.
 * @see XYZColor for the connection space through which all conversions pass.
 * @see CIEPoint for chromaticity coordinates used to define primaries.
 */
class ColorModel {
        public:
                /**
                 * @brief Identifies a well-known color model.
                 *
                 * Each ID corresponds to a unique combination of color model type,
                 * primaries, white point, and transfer function. The "_ID" suffix
                 * distinguishes these enumerators from the static ColorModel constants
                 * of the same name (e.g. sRGB_ID vs ColorModel::sRGB).
                 */
                enum ID {
                        Invalid_ID = 0,        ///< Invalid or unset.
                        sRGB_ID,               ///< sRGB (IEC 61966-2-1).
                        LinearSRGB_ID,         ///< Linear sRGB (scene-referred).
                        Rec709_ID,             ///< ITU-R BT.709 (gamma-corrected).
                        LinearRec709_ID,       ///< ITU-R BT.709 (linear).
                        Rec601_PAL_ID,         ///< ITU-R BT.601 PAL (gamma-corrected).
                        LinearRec601_PAL_ID,   ///< ITU-R BT.601 PAL (linear).
                        Rec601_NTSC_ID,        ///< ITU-R BT.601 NTSC (gamma-corrected).
                        LinearRec601_NTSC_ID,  ///< ITU-R BT.601 NTSC (linear).
                        Rec2020_ID,            ///< ITU-R BT.2020 (gamma-corrected).
                        LinearRec2020_ID,      ///< ITU-R BT.2020 (linear).
                        DCI_P3_ID,             ///< DCI-P3 Display (D65, sRGB transfer).
                        LinearDCI_P3_ID,       ///< Linear DCI-P3 Display (D65).
                        AdobeRGB_ID,           ///< Adobe RGB (1998).
                        LinearAdobeRGB_ID,     ///< Linear Adobe RGB (1998).
                        ACES_AP0_ID,           ///< ACES 2065-1 (AP0 primaries, linear).
                        ACES_AP1_ID,           ///< ACEScg (AP1 primaries, linear).
                        CIEXYZ_ID,             ///< CIE 1931 XYZ (connection space).
                        CIELab_ID,             ///< CIE L*a*b* (D65 white point).
                        HSV_sRGB_ID,           ///< HSV derived from sRGB.
                        HSL_sRGB_ID,           ///< HSL derived from sRGB.
                        YCbCr_Rec709_ID,       ///< YCbCr with BT.709 coefficients.
                        YCbCr_Rec601_ID,       ///< YCbCr with BT.601 coefficients.
                        YCbCr_Rec2020_ID,      ///< YCbCr with BT.2020 coefficients.
                };

                /**
                 * @brief The geometric type of the color model's component space.
                 *
                 * This describes the mathematical structure of the three color
                 * components, independent of the specific primaries or transfer
                 * function. For example, both sRGB and Rec.2020 have TypeRGB
                 * despite using different primaries.
                 */
                enum Type {
                        TypeInvalid = 0,  ///< Invalid or unset.
                        TypeRGB,          ///< Red, Green, Blue.
                        TypeXYZ,          ///< CIE 1931 XYZ.
                        TypeLab,          ///< CIE L*a*b*.
                        TypeYCbCr,        ///< Luma + chroma-difference.
                        TypeHSV,          ///< Hue, Saturation, Value.
                        TypeHSL           ///< Hue, Saturation, Lightness.
                };

                /**
                 * @brief Describes a single color component for display and documentation.
                 *
                 * Each color model has three color components (alpha is handled
                 * separately by Color). CompInfo provides the human-readable name
                 * and abbreviation, plus the native display range. Internally,
                 * components are stored normalized to 0.0-1.0, but the native
                 * range tells you what those values mean in conventional units.
                 * For example, HSV hue is stored as 0.0-1.0 but its native
                 * range is 0-360 degrees.
                 */
                struct CompInfo {
                        String  name;       ///< Full name (e.g. "Red", "Hue", "Luma").
                        String  abbrev;     ///< Abbreviation (e.g. "R", "H", "Y").
                        float   nativeMin;  ///< Minimum in native display units.
                        float   nativeMax;  ///< Maximum in native display units.
                };

                /**
                 * @brief Function pointer type for transfer functions (OETF/EOTF).
                 *
                 * Takes a single double-precision value and returns the
                 * transformed value. For the OETF (linear -> encoded), the
                 * input is a linear-light intensity (0.0-1.0) and the output
                 * is the encoded signal value. For the EOTF, the direction
                 * is reversed.
                 */
                typedef double (*TransferFunc)(double);

                /**
                 * @brief CIE chromaticity primaries: Red, Green, Blue, White point.
                 *
                 * An array of four CIEPoints defining the chromaticity coordinates
                 * of the three RGB primaries and the white point. These four
                 * points, together with the transfer function, fully define an
                 * RGB color space. For non-RGB models (XYZ, Lab, HSV, etc.)
                 * the primaries are inherited from the parent model or are
                 * not applicable.
                 */
                typedef Array<CIEPoint, 4> Primaries;

                /**
                 * @name Well-known color model constants
                 *
                 * Pre-defined constants for common color models. These are the
                 * values you pass to Color's constructor or convert() method.
                 *
                 * **sRGB** is the default for consumer displays, the web, and
                 * most image formats (PNG, JPEG). If you are unsure which model
                 * to use, sRGB is almost certainly correct.
                 *
                 * **Rec.709** shares the same primaries as sRGB but uses a
                 * slightly different transfer function; it is the standard for
                 * HD video (1080i/1080p). **Rec.601** is the older SD video
                 * standard (PAL and NTSC variants differ in primaries).
                 * **Rec.2020** is the wide-gamut standard for UHD/4K/8K video.
                 *
                 * Each gamma-encoded model has a **Linear** counterpart with
                 * the same primaries but an identity transfer function, for use
                 * in physically-based rendering, compositing, or any context
                 * where arithmetic on light intensities must be linear.
                 *
                 * **HSV** and **HSL** are derived from sRGB and separate hue,
                 * saturation, and brightness/lightness into independent axes.
                 * They are primarily useful for color pickers and artistic
                 * adjustments.
                 *
                 * **YCbCr** models separate luma from chroma and are used in
                 * video compression (H.264, H.265, etc.) and broadcast.
                 *
                 * **CIEXYZ** is the device-independent connection space used
                 * internally for all conversions. **CIELab** is a perceptually
                 * uniform rearrangement of XYZ useful for measuring color
                 * differences.
                 * @{
                 */
                static const ColorModel Invalid;          ///< Invalid / unset.
                static const ColorModel sRGB;             ///< sRGB (IEC 61966-2-1). The standard for consumer displays and the web.
                static const ColorModel LinearSRGB;       ///< Linear sRGB. Same primaries as sRGB, no gamma. Use for compositing and physically-based rendering.
                static const ColorModel Rec709;           ///< ITU-R BT.709 (HD video). Same primaries as sRGB, different transfer function.
                static const ColorModel LinearRec709;     ///< Linear Rec.709. Same primaries, identity transfer.
                static const ColorModel Rec601_PAL;       ///< ITU-R BT.601 PAL (SD video, 625-line systems).
                static const ColorModel LinearRec601_PAL; ///< Linear Rec.601 PAL.
                static const ColorModel Rec601_NTSC;      ///< ITU-R BT.601 NTSC (SD video, 525-line systems).
                static const ColorModel LinearRec601_NTSC;///< Linear Rec.601 NTSC.
                static const ColorModel Rec2020;          ///< ITU-R BT.2020 (UHD/4K/8K video). Wide gamut.
                static const ColorModel LinearRec2020;    ///< Linear Rec.2020.
                static const ColorModel DCI_P3;           ///< DCI-P3 Display (D65 white point, sRGB transfer). Used by Apple displays and HDR content.
                static const ColorModel LinearDCI_P3;     ///< Linear DCI-P3 Display (D65).
                static const ColorModel AdobeRGB;         ///< Adobe RGB (1998). Wide-gamut space for photography and prepress.
                static const ColorModel LinearAdobeRGB;   ///< Linear Adobe RGB (1998).
                static const ColorModel ACES_AP0;         ///< ACES 2065-1 (AP0 primaries, linear, D60 white). Encompasses all visible colors. The archival/interchange format for ACES.
                static const ColorModel ACES_AP1;         ///< ACEScg (AP1 primaries, linear, D60 white). The working space for ACES compositing and CGI rendering.
                static const ColorModel CIEXYZ;           ///< CIE 1931 XYZ. The device-independent connection space for all conversions.
                static const ColorModel CIELab;           ///< CIE L*a*b* (D65 white point). Perceptually uniform; useful for color difference metrics.
                static const ColorModel HSV_sRGB;         ///< HSV derived from sRGB. Hue-Saturation-Value for color pickers.
                static const ColorModel HSL_sRGB;         ///< HSL derived from sRGB. Hue-Saturation-Lightness.
                static const ColorModel YCbCr_Rec709;     ///< YCbCr with BT.709 luma coefficients. Used in HD video compression.
                static const ColorModel YCbCr_Rec601;     ///< YCbCr with BT.601 luma coefficients. Used in SD video compression.
                static const ColorModel YCbCr_Rec2020;    ///< YCbCr with BT.2020 luma coefficients. Used in UHD video compression.
                /** @} */

                /**
                 * @brief Looks up a well-known ColorModel by name.
                 * @param name The name to search for (e.g. "sRGB", "HSV_sRGB").
                 * @return The matching model, or Invalid if not found.
                 */
                static ColorModel lookup(const String &name);

                /**
                 * @brief Constructs a ColorModel from an ID.
                 *
                 * Resolves the ID to internal data via a construct-on-first-use
                 * registry. Safe to call during static initialization.
                 */
                ColorModel(ID id = Invalid_ID);

                /** @brief Returns the ID of this model. */
                ID id() const;

                /** @brief Returns true if this is a valid (non-Invalid) color model. */
                bool isValid() const;

                /** @brief Equality operator. */
                bool operator==(const ColorModel &other) const { return _d == other._d; }

                /** @brief Inequality operator. */
                bool operator!=(const ColorModel &other) const { return _d != other._d; }

                /** @brief Returns the geometric type of this model. */
                Type type() const;

                /** @brief Returns the human-readable name of this model. */
                const String &name() const;

                /** @brief Returns a longer description of this model. */
                const String &desc() const;

                /**
                 * @brief Returns the number of color components (always 3).
                 *
                 * Alpha is managed by Color, not ColorModel.
                 */
                size_t compCount() const;

                /**
                 * @brief Returns descriptor for the given component index (0-2).
                 * @param index Component index.
                 * @return CompInfo describing the component's name and native range.
                 */
                const CompInfo &compInfo(size_t index) const;

                /**
                 * @brief Returns the CIE chromaticity primaries and white point.
                 *
                 * The array contains [Red, Green, Blue, WhitePoint] as CIEPoints.
                 * These define the triangle of reproducible colors (the gamut)
                 * for RGB models. For non-RGB models, the primaries are
                 * inherited from the parent model or may be empty.
                 */
                const Primaries &primaries() const;

                /**
                 * @brief Returns the white point chromaticity coordinate.
                 *
                 * The white point defines what "white" means in this color space.
                 * Most modern standards use D65 (approximately 6504 K daylight),
                 * which has chromaticity coordinates (0.3127, 0.3290). The
                 * graphic arts industry sometimes uses D50 (~5003 K) instead.
                 */
                const CIEPoint &whitePoint() const;

                /**
                 * @brief Returns true if this model uses a linear (identity) transfer function.
                 *
                 * Linear models store values proportional to physical light
                 * intensity. This is required for correct alpha compositing,
                 * lighting calculations, and any arithmetic that assumes
                 * superposition of light. Non-linear (gamma-encoded) models
                 * are more efficient for storage and display but must be
                 * linearized before arithmetic operations.
                 */
                bool isLinear() const;

                /**
                 * @brief Returns the linear counterpart of this model.
                 *
                 * For a gamma-encoded RGB model, returns the linear version with the
                 * same primaries. For models that are already linear or have no
                 * transfer function, returns this.
                 */
                ColorModel linearCounterpart() const;

                /**
                 * @brief Returns the non-linear (gamma-encoded) counterpart of this model.
                 *
                 * For a linear RGB model, returns the gamma-encoded version. For
                 * models that are already non-linear, returns this.
                 */
                ColorModel nonlinearCounterpart() const;

                /**
                 * @brief Applies the forward transfer function (OETF) to a linear value.
                 *
                 * Converts a linear-light intensity to the encoded (non-linear)
                 * representation used for storage and display. For sRGB, this is
                 * approximately raising to the power of 1/2.4 (with a linear
                 * segment near black).
                 *
                 * @param linear The linear-light value (0.0-1.0 for SDR content).
                 * @return The encoded (gamma-corrected) value.
                 */
                double applyTransfer(double linear) const;

                /**
                 * @brief Removes the transfer function (EOTF) from an encoded value.
                 *
                 * Converts an encoded (non-linear) value back to linear light.
                 * This is the inverse of applyTransfer(). For sRGB, this is
                 * approximately raising to the power of 2.4 (with a linear
                 * segment near black).
                 *
                 * @param encoded The gamma-corrected value (0.0-1.0 for SDR content).
                 * @return The linear-light value.
                 */
                double removeTransfer(double encoded) const;

                /**
                 * @brief Returns the parent model for derived types.
                 *
                 * HSV/HSL models return their parent RGB model. YCbCr models return
                 * the RGB model they are defined relative to. Primary RGB models
                 * and XYZ/Lab return Invalid.
                 */
                ColorModel parentModel() const;

                /**
                 * @brief Converts components from this model to CIE XYZ.
                 *
                 * This is the core conversion primitive. For RGB models the
                 * pipeline is: remove transfer function (EOTF), then multiply
                 * by the RGB-to-XYZ matrix. For derived models (HSV, YCbCr)
                 * the components are first converted to the parent RGB model,
                 * then that model's toXYZ is applied.
                 *
                 * This method is intended for precise single-color conversions,
                 * not for bulk pixel processing.
                 *
                 * @param src Source components in this model (3 floats, alpha excluded).
                 * @param dst Destination CIE XYZ components (3 floats).
                 */
                void toXYZ(const float *src, float *dst) const;

                /**
                 * @brief Converts components from CIE XYZ to this model.
                 *
                 * The reverse of toXYZ(). For RGB models: multiply by the
                 * XYZ-to-RGB matrix, then apply the transfer function (OETF).
                 *
                 * @param src Source CIE XYZ components (3 floats).
                 * @param dst Destination components in this model (3 floats, alpha excluded).
                 */
                void fromXYZ(const float *src, float *dst) const;

                /**
                 * @brief Converts a normalized component value to native display units.
                 * @param comp Component index (0-2).
                 * @param normalized The normalized value (typically 0.0-1.0).
                 * @return The value in native units (e.g. 0-360 for hue).
                 */
                float toNative(size_t comp, float normalized) const;

                /**
                 * @brief Converts a native display value to normalized form.
                 * @param comp Component index (0-2).
                 * @param native The value in native units.
                 * @return The normalized value.
                 */
                float fromNative(size_t comp, float native) const;

                /** @cond INTERNAL */
                struct Data;
                /** @endcond */

        private:
                const Data *_d = nullptr;
};

PROMEKI_NAMESPACE_END
