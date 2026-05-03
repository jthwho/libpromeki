/**
 * @file      colormodel.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/array.h>
#include <promeki/ciepoint.h>
#include <promeki/matrix3x3.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Single source of truth for a color model and its associated color space.
 * @ingroup color
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight inline
 * wrapper around an immutable Data record, identified by an integer ID.
 * Well-known models are provided as enum constants; user-defined models
 * can be registered at runtime via registerType() and registerData().
 *
 * A "color model" defines how color is represented mathematically (RGB, HSV,
 * YCbCr, etc.), while a "color space" pins that model to specific real-world
 * colors by defining primaries, a white point, and a transfer function. This
 * class combines both concepts: a ColorModel constructed from sRGB, for
 * example, specifies not just "RGB" but the exact sRGB primaries, D65 white
 * point, and the IEC 61966-2-1 transfer curve.
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
 * @see @ref typeregistry "TypeRegistry Pattern" for the design pattern.
 *
 * @par Thread Safety
 * Distinct ColorModel instances may be used concurrently — each is
 * just a small handle to an immutable registry entry.  The static
 * registry (registerType / registerData / type lookup) is
 * internally synchronized and safe to call from any thread.
 */
class ColorModel {
        public:
                /**
                 * @brief Identifies a color model.
                 *
                 * Well-known models have named enumerators.  User-defined
                 * models obtain IDs from registerType().  The atomic counter
                 * starts at UserDefined.
                 */
                enum ID {
                        Invalid = 0,           ///< Invalid or unset.
                        sRGB = 1,              ///< sRGB (IEC 61966-2-1).
                        LinearSRGB = 2,        ///< Linear sRGB (scene-referred).
                        Rec709 = 3,            ///< ITU-R BT.709 (gamma-corrected).
                        LinearRec709 = 4,      ///< ITU-R BT.709 (linear).
                        Rec601_PAL = 5,        ///< ITU-R BT.601 PAL (gamma-corrected).
                        LinearRec601_PAL = 6,  ///< ITU-R BT.601 PAL (linear).
                        Rec601_NTSC = 7,       ///< ITU-R BT.601 NTSC (gamma-corrected).
                        LinearRec601_NTSC = 8, ///< ITU-R BT.601 NTSC (linear).
                        Rec2020 = 9,           ///< ITU-R BT.2020 (gamma-corrected).
                        LinearRec2020 = 10,    ///< ITU-R BT.2020 (linear).
                        DCI_P3 = 11,           ///< DCI-P3 Display (D65, sRGB transfer).
                        LinearDCI_P3 = 12,     ///< Linear DCI-P3 Display (D65).
                        AdobeRGB = 13,         ///< Adobe RGB (1998).
                        LinearAdobeRGB = 14,   ///< Linear Adobe RGB (1998).
                        ACES_AP0 = 15,         ///< ACES 2065-1 (AP0 primaries, linear).
                        ACES_AP1 = 16,         ///< ACEScg (AP1 primaries, linear).
                        CIEXYZ = 17,           ///< CIE 1931 XYZ (connection space).
                        CIELab = 18,           ///< CIE L*a*b* (D65 white point).
                        HSV_sRGB = 19,         ///< HSV derived from sRGB.
                        HSL_sRGB = 20,         ///< HSL derived from sRGB.
                        YCbCr_Rec709 = 21,     ///< YCbCr with BT.709 coefficients.
                        YCbCr_Rec601 = 22,     ///< YCbCr with BT.601 coefficients.
                        YCbCr_Rec2020 = 23,    ///< YCbCr with BT.2020 coefficients.
                        UserDefined = 1024     ///< First ID available for user-registered types.
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
                        TypeInvalid = 0, ///< Invalid or unset.
                        TypeRGB,         ///< Red, Green, Blue.
                        TypeXYZ,         ///< CIE 1931 XYZ.
                        TypeLab,         ///< CIE L*a*b*.
                        TypeYCbCr,       ///< Luma + chroma-difference.
                        TypeHSV,         ///< Hue, Saturation, Value.
                        TypeHSL          ///< Hue, Saturation, Lightness.
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
                                String name;      ///< Full name (e.g. "Red", "Hue", "Luma").
                                String abbrev;    ///< Abbreviation (e.g. "R", "H", "Y").
                                float  nativeMin; ///< Minimum in native display units.
                                float  nativeMax; ///< Maximum in native display units.
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

                /** @brief List of ColorModel IDs. */
                using IDList = ::promeki::List<ID>;

                /**
                 * @brief Immutable descriptor for a color model.
                 *
                 * Holds all properties that define a color model: geometric type,
                 * primaries, transfer functions, conversion matrices, and
                 * component metadata.  Populated by the library for well-known
                 * models, or by users via registerData() for custom models.
                 *
                 * @see @ref typeregistry "TypeRegistry Pattern"
                 */
                struct Data {
                                ID           id = Invalid;       ///< The ID this data was registered under.
                                Type         type = TypeInvalid; ///< Geometric type (RGB, HSV, etc.).
                                String       name;               ///< Human-readable name.
                                String       desc;               ///< Longer description.
                                Primaries    primaries;          ///< CIE chromaticity primaries and white point.
                                CompInfo     comps[3] = {};      ///< Component descriptors (3 color channels).
                                TransferFunc oetf = nullptr;     ///< Linear -> encoded transfer function.
                                TransferFunc eotf = nullptr;     ///< Encoded -> linear transfer function.
                                bool         linear = false;     ///< True if transfer function is identity.
                                ID           linearCounterpart = Invalid;    ///< ID of linear version (or self).
                                ID           nonlinearCounterpart = Invalid; ///< ID of gamma-encoded version (or self).
                                ID           parentModel = Invalid;          ///< Parent RGB model for derived types.
                                Matrix3x3    rgbToXyz;                       ///< RGB-to-XYZ Normalized Primary Matrix.
                                Matrix3x3    xyzToRgb;                       ///< XYZ-to-RGB (inverse of rgbToXyz).
                                Matrix3x3    toParentMatrix;                 ///< For matrix-derived models (YCbCr).
                                Matrix3x3    fromParentMatrix;               ///< Inverse of toParentMatrix.
                                float        toParentOffset[3] = {};         ///< Offset applied before toParentMatrix.
                                float        fromParentOffset[3] = {};       ///< Offset applied after fromParentMatrix.
                                void (*toXYZFunc)(const Data *d, const float *src,
                                                  float *dst) = nullptr; ///< Convert to CIE XYZ.
                                void (*fromXYZFunc)(const Data *d, const float *src,
                                                    float *dst) = nullptr; ///< Convert from CIE XYZ.
                };

                /**
                 * @brief Allocates and returns a unique ID for a user-defined color model.
                 *
                 * Each call returns a new, never-before-used ID.  Thread-safe.
                 *
                 * @return A unique ID value.
                 * @see registerData()
                 */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the registry.
                 *
                 * After this call, constructing a ColorModel from @p data.id
                 * will resolve to the registered data.
                 *
                 * @param data The populated Data struct with id set to a value from registerType().
                 * @see registerType()
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns a list of all registered ColorModel IDs.
                 *
                 * Excludes Invalid.  Includes both well-known and user-registered types.
                 *
                 * @return A List of ID values.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a well-known ColorModel by name.
                 * @param name The name to search for (e.g. "sRGB", "HSV_sRGB").
                 * @return The matching model, or an invalid model if not found.
                 */
                static ColorModel lookup(const String &name);

                /**
                 * @brief H.273 / ISO/IEC 23091-4 codepoint triplet
                 *        describing a color space in codec VUI terms.
                 *
                 * Each field is the numeric value that would appear in
                 * an H.264 / HEVC VUI or AV1 color-description header.
                 * A @c 0 indicates "not derivable from this ColorModel"
                 * — callers typically substitute @c 2 (Unspecified) in
                 * that case, or fall through to an explicit user override.
                 *
                 * @note Transfer characteristics auto-derivation does
                 *       @em not distinguish HDR curves (PQ / HLG) today
                 *       because the library's @ref ColorModel doesn't
                 *       model them explicitly yet.  HDR callers must
                 *       stamp the correct transfer on the encoder config.
                 */
                struct H273 {
                                uint8_t primaries = 0; ///< H.273 @c colour_primaries.
                                uint8_t transfer = 0;  ///< H.273 @c transfer_characteristics.
                                uint8_t matrix = 0;    ///< H.273 @c matrix_coefficients.
                };

                /**
                 * @brief Returns the H.273 codepoint triplet for a
                 *        well-known ColorModel ID.
                 *
                 * Covers the SDR models that libpromeki ships as
                 * well-known: sRGB / Rec.709 / Rec.601 PAL / Rec.601
                 * NTSC / Rec.2020 / DCI-P3 / Adobe RGB / ACES AP0 /
                 * ACES AP1, plus the YCbCr_* derivations.  Linear
                 * variants map the transfer field to @c 8 (Linear);
                 * CIE XYZ / Lab / HSV / HSL return all-zeros.
                 *
                 * User-defined ColorModel IDs are not recognised and
                 * fall through to the all-zero default; that's by
                 * design — the library-wide mapping can't know what a
                 * user's custom model is supposed to represent on the
                 * wire.
                 */
                static H273 toH273(ID id);

                /**
                 * @brief Constructs a ColorModel from an ID.
                 *
                 * Resolves the ID to internal data via a construct-on-first-use
                 * registry. Safe to call during static initialization.
                 */
                inline ColorModel(ID id = Invalid);

                /** @brief Returns the ID of this model. */
                ID id() const { return _d->id; }

                /** @brief Returns true if this is a valid (non-Invalid) color model. */
                bool isValid() const { return _d != nullptr && _d->type != TypeInvalid; }

                /** @brief Equality operator. */
                bool operator==(const ColorModel &other) const { return _d == other._d; }

                /** @brief Inequality operator. */
                bool operator!=(const ColorModel &other) const { return _d != other._d; }

                /** @brief Returns the geometric type of this model. */
                Type type() const { return _d->type; }

                /** @brief Returns the human-readable name of this model. */
                const String &name() const { return _d->name; }

                /** @brief Returns a longer description of this model. */
                const String &desc() const { return _d->desc; }

                /**
                 * @brief Returns the number of color components (always 3).
                 *
                 * Alpha is managed by Color, not ColorModel.
                 */
                size_t compCount() const { return 3; }

                /**
                 * @brief Returns descriptor for the given component index (0-2).
                 * @param index Component index.
                 * @return CompInfo describing the component's name and native range.
                 */
                const CompInfo &compInfo(size_t index) const { return _d->comps[index < 3 ? index : 0]; }

                /**
                 * @brief Returns the CIE chromaticity primaries and white point.
                 *
                 * The array contains [Red, Green, Blue, WhitePoint] as CIEPoints.
                 */
                const Primaries &primaries() const { return _d->primaries; }

                /**
                 * @brief Returns the white point chromaticity coordinate.
                 *
                 * The white point defines what "white" means in this color space.
                 * Most modern standards use D65 (approximately 6504 K daylight).
                 */
                const CIEPoint &whitePoint() const { return _d->primaries[3]; }

                /**
                 * @brief Returns true if this model uses a linear (identity) transfer function.
                 */
                bool isLinear() const { return _d->linear; }

                /**
                 * @brief Returns the linear counterpart of this model.
                 *
                 * For a gamma-encoded RGB model, returns the linear version with the
                 * same primaries. For models that are already linear or have no
                 * transfer function, returns this.
                 */
                inline ColorModel linearCounterpart() const;

                /**
                 * @brief Returns the non-linear (gamma-encoded) counterpart of this model.
                 *
                 * For a linear RGB model, returns the gamma-encoded version. For
                 * models that are already non-linear, returns this.
                 */
                inline ColorModel nonlinearCounterpart() const;

                /**
                 * @brief Applies the forward transfer function (OETF) to a linear value.
                 *
                 * @param linear The linear-light value (0.0-1.0 for SDR content).
                 * @return The encoded (gamma-corrected) value.
                 */
                double applyTransfer(double linear) const { return _d->oetf(linear); }

                /**
                 * @brief Removes the transfer function (EOTF) from an encoded value.
                 *
                 * @param encoded The gamma-corrected value (0.0-1.0 for SDR content).
                 * @return The linear-light value.
                 */
                double removeTransfer(double encoded) const { return _d->eotf(encoded); }

                /**
                 * @brief Returns the parent model for derived types.
                 *
                 * HSV/HSL models return their parent RGB model. YCbCr models return
                 * the RGB model they are defined relative to. Primary RGB models
                 * and XYZ/Lab return Invalid.
                 */
                inline ColorModel parentModel() const;

                /**
                 * @brief Converts components from this model to CIE XYZ.
                 *
                 * @param src Source components in this model (3 floats, alpha excluded).
                 * @param dst Destination CIE XYZ components (3 floats).
                 */
                void toXYZ(const float *src, float *dst) const { _d->toXYZFunc(_d, src, dst); }

                /**
                 * @brief Converts components from CIE XYZ to this model.
                 *
                 * @param src Source CIE XYZ components (3 floats).
                 * @param dst Destination components in this model (3 floats, alpha excluded).
                 */
                void fromXYZ(const float *src, float *dst) const { _d->fromXYZFunc(_d, src, dst); }

                /**
                 * @brief Converts a normalized component value to native display units.
                 * @param comp Component index (0-2).
                 * @param normalized The normalized value (typically 0.0-1.0).
                 * @return The value in native units (e.g. 0-360 for hue).
                 */
                float toNative(size_t comp, float normalized) const {
                        if (comp >= 3) return 0.0f;
                        const CompInfo &ci = _d->comps[comp];
                        return ci.nativeMin + normalized * (ci.nativeMax - ci.nativeMin);
                }

                /**
                 * @brief Converts a native display value to normalized form.
                 * @param comp Component index (0-2).
                 * @param native The value in native units.
                 * @return The normalized value.
                 */
                float fromNative(size_t comp, float native) const {
                        if (comp >= 3) return 0.0f;
                        const CompInfo &ci = _d->comps[comp];
                        float           range = ci.nativeMax - ci.nativeMin;
                        if (range == 0.0f) return 0.0f;
                        return (native - ci.nativeMin) / range;
                }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return _d; }

        private:
                const Data        *_d = nullptr;
                static const Data *lookupData(ID id);
};

// Deferred inline definitions that require the full class to be visible.

inline ColorModel::ColorModel(ID id) : _d(lookupData(id)) {}

inline ColorModel ColorModel::linearCounterpart() const {
        ID lcid = _d->linearCounterpart;
        return lcid != Invalid ? ColorModel(lcid) : *this;
}

inline ColorModel ColorModel::nonlinearCounterpart() const {
        ID nlid = _d->nonlinearCounterpart;
        return nlid != Invalid ? ColorModel(nlid) : *this;
}

inline ColorModel ColorModel::parentModel() const {
        return ColorModel(_d->parentModel);
}

PROMEKI_NAMESPACE_END
