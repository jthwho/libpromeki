/**
 * @file      pixeldesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>
#include <promeki/fourcc.h>
#include <promeki/colormodel.h>
#include <promeki/pixelformat.h>

PROMEKI_NAMESPACE_BEGIN

class Image;
class ImageDesc;
class PaintEngine;

/**
 * @brief Complete pixel description combining format, color model, and semantics.
 * @ingroup proav
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight inline
 * wrapper around an immutable Data record, identified by an integer ID.
 *
 * PixelDesc combines a PixelFormat (memory layout) with a ColorModel (color
 * semantics), per-component value ranges, compression information, and
 * paint engine creation.  It fully describes everything needed to interpret
 * and manipulate pixel data.
 *
 * @par Example
 * @code
 * PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
 * assert(pd.pixelFormat().id() == PixelFormat::Interleaved_4x8);
 * assert(pd.colorModel().id() == ColorModel::sRGB);
 * assert(pd.hasAlpha());
 * assert(!pd.isCompressed());
 *
 * PaintEngine pe = pd.createPaintEngine(image);
 * @endcode
 *
 * @see PixelFormat, ColorModel, @ref typeregistry "TypeRegistry Pattern"
 */
class PixelDesc {
        public:
                static constexpr size_t MaxComps = PixelFormat::MaxComps; ///< Maximum number of components.

                /**
                 * @brief Identifies a pixel description.
                 *
                 * Well-known descriptions have named enumerators.  User-defined
                 * descriptions obtain IDs from registerType().
                 */
                enum ID {
                        Invalid                      = 0,    ///< Invalid or uninitialized pixel description.
                        RGBA8_sRGB_Full              = 1,    ///< 8-bit RGBA, sRGB, full range.
                        RGB8_sRGB_Full               = 2,    ///< 8-bit RGB, sRGB, full range.
                        RGB10_sRGB_Full              = 3,    ///< 10-bit RGB, sRGB, full range.
                        YUV8_422_Rec709_Limited      = 4,    ///< 8-bit YCbCr 4:2:2, Rec.709, limited range.
                        YUV10_422_Rec709_Limited     = 5,    ///< 10-bit YCbCr 4:2:2, Rec.709, limited range.
                        JPEG_RGBA8_sRGB_Full         = 6,    ///< JPEG-compressed 8-bit RGBA, sRGB, full range.
                        JPEG_RGB8_sRGB_Full          = 7,    ///< JPEG-compressed 8-bit RGB, sRGB, full range.
                        JPEG_YUV8_422_Rec709_Limited = 8,    ///< JPEG-compressed 8-bit YCbCr 4:2:2, Rec.709, limited range.
                        JPEG_YUV8_420_Rec709_Limited = 9,    ///< JPEG-compressed 8-bit YCbCr 4:2:0, Rec.709, limited range.
                        YUV8_422_UYVY_Rec709_Limited = 10,     ///< 8-bit YCbCr 4:2:2 UYVY, Rec.709, limited range.
                        YUV10_422_UYVY_LE_Rec709_Limited = 11, ///< 10-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range.
                        YUV10_422_UYVY_BE_Rec709_Limited = 12, ///< 10-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range.
                        YUV12_422_UYVY_LE_Rec709_Limited = 13, ///< 12-bit YCbCr 4:2:2 UYVY LE, Rec.709, limited range.
                        YUV12_422_UYVY_BE_Rec709_Limited = 14, ///< 12-bit YCbCr 4:2:2 UYVY BE, Rec.709, limited range.
                        YUV10_422_v210_Rec709_Limited = 15,    ///< 10-bit YCbCr 4:2:2 v210 packed, Rec.709, limited range.
                        YUV8_422_Planar_Rec709_Limited = 16,       ///< 8-bit YCbCr 4:2:2 planar, Rec.709, limited range.
                        YUV10_422_Planar_LE_Rec709_Limited = 17,   ///< 10-bit YCbCr 4:2:2 planar LE, Rec.709, limited range.
                        YUV10_422_Planar_BE_Rec709_Limited = 18,   ///< 10-bit YCbCr 4:2:2 planar BE, Rec.709, limited range.
                        YUV12_422_Planar_LE_Rec709_Limited = 19,   ///< 12-bit YCbCr 4:2:2 planar LE, Rec.709, limited range.
                        YUV12_422_Planar_BE_Rec709_Limited = 20,   ///< 12-bit YCbCr 4:2:2 planar BE, Rec.709, limited range.
                        YUV8_420_Planar_Rec709_Limited = 21,       ///< 8-bit YCbCr 4:2:0 planar, Rec.709, limited range.
                        YUV10_420_Planar_LE_Rec709_Limited = 22,   ///< 10-bit YCbCr 4:2:0 planar LE, Rec.709, limited range.
                        YUV10_420_Planar_BE_Rec709_Limited = 23,   ///< 10-bit YCbCr 4:2:0 planar BE, Rec.709, limited range.
                        YUV12_420_Planar_LE_Rec709_Limited = 24,   ///< 12-bit YCbCr 4:2:0 planar LE, Rec.709, limited range.
                        YUV12_420_Planar_BE_Rec709_Limited = 25,   ///< 12-bit YCbCr 4:2:0 planar BE, Rec.709, limited range.
                        YUV8_420_SemiPlanar_Rec709_Limited = 26,   ///< 8-bit YCbCr 4:2:0 NV12, Rec.709, limited range.
                        YUV10_420_SemiPlanar_LE_Rec709_Limited = 27, ///< 10-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range.
                        YUV10_420_SemiPlanar_BE_Rec709_Limited = 28, ///< 10-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range.
                        YUV12_420_SemiPlanar_LE_Rec709_Limited = 29, ///< 12-bit YCbCr 4:2:0 NV12 LE, Rec.709, limited range.
                        YUV12_420_SemiPlanar_BE_Rec709_Limited = 30, ///< 12-bit YCbCr 4:2:0 NV12 BE, Rec.709, limited range.
                        UserDefined                  = 1024  ///< First ID available for user-registered types.
                };

                /** @brief List of PixelDesc IDs. */
                using IDList = List<ID>;

                /** @brief Describes the semantic meaning and value range of a single component. */
                struct CompSemantic {
                        String  name;           ///< Human-readable name (e.g. "Red", "Luma").
                        String  abbrev;         ///< Abbreviation (e.g. "R", "Y").
                        float   rangeMin = 0;   ///< Minimum valid value (e.g. 0, 16).
                        float   rangeMax = 0;   ///< Maximum valid value (e.g. 255, 235).
                };

                /** @brief Immutable data record for a pixel description. */
                struct Data {
                        ID              id = Invalid;                     ///< Unique description identifier.
                        String          name;                             ///< Short name (e.g. "RGBA8").
                        String          desc;                             ///< Human-readable description.
                        PixelFormat     pixelFormat;                      ///< Memory layout (by value).
                        ColorModel      colorModel;                      ///< Color semantics (by value).
                        bool            hasAlpha = false;                 ///< Whether this description includes alpha.
                        int             alphaCompIndex = -1;              ///< Component index for alpha (-1 = none).
                        bool            compressed = false;               ///< Whether this is a compressed format.
                        String          codecName;                        ///< Codec name for ImageCodec lookup (e.g. "jpeg").
                        List<ID>        encodeSources;                    ///< Uncompressed PixelDescs the codec can encode from.
                        List<ID>        decodeTargets;                    ///< Uncompressed PixelDescs the codec can decode to.
                        FourCCList      fourccList;                       ///< Associated FourCC codes.
                        CompSemantic    compSemantics[MaxComps] = {};     ///< Per-component semantics.

                        /**
                         * @brief Creates a PaintEngine for drawing on the given image.
                         * @param d   Pointer to this Data record.
                         * @param img The image to create a paint engine for.
                         * @return A PaintEngine, or an invalid PaintEngine if not supported.
                         */
                        PaintEngine (*createPaintEngineFunc)(const Data *d, const Image &img) = nullptr;
                };

                /**
                 * @brief Allocates and returns a unique ID for a user-defined pixel description.
                 * @return A unique ID value.
                 * @see registerData()
                 */
                static ID registerType();

                /**
                 * @brief Registers a Data record in the registry.
                 * @param data The populated Data struct.
                 * @see registerType()
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns a list of all registered PixelDesc IDs.
                 *
                 * Excludes Invalid.  Includes both well-known and user-registered types.
                 *
                 * @return A list of ID values.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a pixel description by name.
                 * @param name The description name to search for.
                 * @return A PixelDesc wrapping the found description, or Invalid if not found.
                 */
                static PixelDesc lookup(const String &name);

                /**
                 * @brief Constructs a PixelDesc for the given ID.
                 * @param id The pixel description to use (default: Invalid).
                 */
                inline PixelDesc(ID id = Invalid);

                /** @brief Returns true if this pixel description is valid. */
                bool isValid() const { return d != nullptr && d->id != Invalid; }

                /** @brief Returns the unique ID. */
                ID id() const { return d->id; }

                /** @brief Returns the short name. */
                const String &name() const { return d->name; }

                /** @brief Returns a human-readable description. */
                const String &desc() const { return d->desc; }

                /** @brief Returns the pixel format (memory layout). */
                const PixelFormat &pixelFormat() const { return d->pixelFormat; }

                /** @brief Returns the color model. */
                const ColorModel &colorModel() const { return d->colorModel; }

                /** @brief Returns true if this description includes an alpha channel. */
                bool hasAlpha() const { return d->hasAlpha; }

                /** @brief Returns the component index for alpha, or -1 if none. */
                int alphaCompIndex() const { return d->alphaCompIndex; }

                /** @brief Returns true if this is a compressed format. */
                bool isCompressed() const { return d->compressed; }

                /** @brief Returns the codec name for compressed formats. */
                const String &codecName() const { return d->codecName; }

                /**
                 * @brief Returns the uncompressed PixelDescs the codec can encode from.
                 *
                 * For a compressed PixelDesc (e.g. JPEG_RGB8_sRGB_Full), this lists
                 * the uncompressed pixel descriptions that the codec accepts as input
                 * for encoding.  Empty for uncompressed formats.
                 *
                 * @return A const reference to the list of source PixelDesc IDs.
                 */
                const List<ID> &encodeSources() const { return d->encodeSources; }

                /**
                 * @brief Returns the uncompressed PixelDescs the codec can decode to.
                 *
                 * For a compressed PixelDesc, this lists the uncompressed pixel
                 * descriptions that the codec can produce when decoding.  Empty for
                 * uncompressed formats.
                 *
                 * @return A const reference to the list of target PixelDesc IDs.
                 */
                const List<ID> &decodeTargets() const { return d->decodeTargets; }

                /** @brief Returns the list of associated FourCC codes. */
                const FourCCList &fourccList() const { return d->fourccList; }

                /** @brief Returns the number of components. */
                size_t compCount() const { return d->pixelFormat.compCount(); }

                /**
                 * @brief Returns the semantic descriptor for a component.
                 * @param index Zero-based component index.
                 * @return Reference to the CompSemantic for that component.
                 */
                const CompSemantic &compSemantic(size_t index) const { return d->compSemantics[index]; }

                /** @brief Returns the number of planes. */
                size_t planeCount() const { return d->pixelFormat.planeCount(); }

                // The following methods depend on proav types (ImageDesc, Image,
                // PaintEngine) and are implemented in pixeldesc_proav.cpp rather
                // than pixeldesc.cpp.

                /**
                 * @brief Returns the line stride for a given plane, using ImageDesc for dimensions.
                 * @param planeIndex Zero-based plane index.
                 * @param desc       Image descriptor providing width, linePad, lineAlign.
                 * @return Line stride in bytes.
                 */
                size_t lineStride(size_t planeIndex, const ImageDesc &desc) const;

                /**
                 * @brief Returns the total byte size of a given plane, using ImageDesc for dimensions.
                 *
                 * For compressed formats, reads Metadata::CompressedSize from the ImageDesc.
                 *
                 * @param planeIndex Zero-based plane index.
                 * @param desc       Image descriptor providing dimensions and metadata.
                 * @return Plane size in bytes.
                 */
                size_t planeSize(size_t planeIndex, const ImageDesc &desc) const;

                /**
                 * @brief Creates a PaintEngine for drawing on the given image.
                 *
                 * Returns an invalid PaintEngine for compressed formats.
                 *
                 * @param img The image to create a paint engine for.
                 * @return A PaintEngine configured for this pixel description.
                 */
                PaintEngine createPaintEngine(const Image &img) const;

                /** @brief Equality comparison (identity-based). */
                bool operator==(const PixelDesc &o) const { return d == o.d; }

                /** @brief Inequality comparison. */
                bool operator!=(const PixelDesc &o) const { return d != o.d; }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return d; }

        private:
                const Data *d = nullptr;
                static const Data *lookupData(ID id);
};

inline PixelDesc::PixelDesc(ID id) : d(lookupData(id)) {}

PROMEKI_NAMESPACE_END
