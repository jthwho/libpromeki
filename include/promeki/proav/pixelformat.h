/**
 * @file      proav/pixelformat.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once
#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/list.h>
#include <promeki/core/fourcc.h>
#include <promeki/core/size2d.h>
#include <promeki/core/point.h>
#include <promeki/core/util.h>

/**
 * @brief Registers a PixelFormat subclass in the global pixel format registry.
 * @ingroup proav_media
 *
 * @param name The PixelFormat subclass type to instantiate and register.
 */
#define PROMEKI_REGISTER_PIXELFORMAT(name) [[maybe_unused]] static int PROMEKI_CONCAT(__promeki_pixelformat_, PROMEKI_UNIQUE_ID) = \
        PixelFormat::registerPixelFormat(new name);


PROMEKI_NAMESPACE_BEGIN

class Image;
class ImageDesc;
class PaintEngine;

/**
 * @brief Describes a pixel packing format and provides format-specific operations.
 *
 * PixelFormat is a polymorphic base class that describes how pixel data is
 * packed in memory.  It does not concern itself with color semantics, only
 * with the layout of components, planes, and blocks.  Concrete subclasses
 * are registered at static-init time via PROMEKI_REGISTER_PIXELFORMAT and
 * looked up by numeric ID.
 */
class PixelFormat {
        public:
                /** @brief The ID of the unique packing format for the pixel. */
                enum ID {
                        Invalid = 0,       ///< Invalid or uninitialized pixel format.
                        RGBA8,             ///< 8-bit RGBA (32 bits per pixel).
                        RGB8,              ///< 8-bit RGB (24 bits per pixel).
                        RGB10,             ///< 10-bit RGB.
                        YUV8_422,          ///< 8-bit YUV with 4:2:2 chroma subsampling.
                        YUV10_422,         ///< 10-bit YUV with 4:2:2 chroma subsampling.
                        JPEG_RGBA8,        ///< JPEG-compressed 8-bit RGBA.
                        JPEG_RGB8,         ///< JPEG-compressed 8-bit RGB.
                        JPEG_YUV8_422      ///< JPEG-compressed 8-bit YUV 4:2:2.
                };

                /** @brief Chroma subsampling mode. */
                enum Sampling {
                        SamplingUndefined = 0, ///< Undefined or unknown sampling.
                        Sampling444,           ///< 4:4:4 (no chroma subsampling).
                        Sampling422,           ///< 4:2:2 (horizontal chroma subsampling by 2).
                        Sampling411,           ///< 4:1:1 (horizontal chroma subsampling by 4).
                        Sampling420            ///< 4:2:0 (horizontal and vertical chroma subsampling by 2).
                };

                /**
                 * @brief Pixel component type.
                 *
                 * Component identifiers are position-based rather than color-model-specific.
                 * The semantic meaning of Comp0/Comp1/Comp2 (e.g. R/G/B vs Y/Cb/Cr)
                 * is determined by the ColorModel associated with the image, not by
                 * the pixel format.
                 */
                enum CompType {
                        CompEmpty = 0, ///< Empty or unused component slot.
                        CompAlpha,     ///< Alpha (transparency) component.
                        Comp0,         ///< First color component (e.g. R, Y, H).
                        Comp1,         ///< Second color component (e.g. G, Cb, S).
                        Comp2          ///< Third color component (e.g. B, Cr, V).
                };

                /** @brief Describes a single component within the pixel format. */
                struct CompDesc {
                        int             plane; ///< Index of the plane this component resides in.
                        CompType        type;  ///< The type of this component.
                        size_t          bits;  ///< Number of bits used by this component.
                };

                /** @brief Describes a single image plane. */
                struct PlaneDesc {
                        String          name; ///< Human-readable name for the plane.
                };

                /**
                 * @brief Registers a pixel format in the global registry.
                 * @param pixelFormat Pointer to a heap-allocated PixelFormat subclass.
                 * @return The registered format's ID.
                 */
                static int registerPixelFormat(PixelFormat *pixelFormat);

                /**
                 * @brief Looks up a registered pixel format by its ID.
                 * @param id The format ID to look up.
                 * @return Pointer to the PixelFormat, or nullptr if not found.
                 */
                static const PixelFormat *lookup(int id);

                /** @brief Default constructor. */
                PixelFormat() = default;

                /** @brief Virtual destructor. */
                virtual ~PixelFormat() {}

                /** @brief Returns true if this pixel format is valid (not Invalid). */
                bool isValid() const { return _id != Invalid; }

                /** @brief Returns the unique ID of this pixel format. */
                int id() const { return _id; }

                /** @brief Returns the short name of this pixel format. */
                String name() const { return _name; }

                /** @brief Returns a human-readable description of this pixel format. */
                String desc() const { return _desc; }

                /** @brief Returns the chroma subsampling mode. */
                Sampling sampling() const { return _sampling; }

                /** @brief Returns the number of pixels in one encoded block. */
                size_t pixelsPerBlock() const { return _pixelsPerBlock; }

                /** @brief Returns the number of bytes in one encoded block. */
                size_t bytesPerBlock() const { return _bytesPerBlock; }

                /** @brief Returns true if this format includes an alpha channel. */
                bool hasAlpha() const { return _hasAlpha; }

                /**
                 * @brief Returns true if this is a compressed pixel format.
                 *
                 * Compressed formats (e.g. JPEG_RGB8) store an encoded
                 * bitstream rather than scanline-addressable pixel data.
                 * Image::isCompressed() is a convenience wrapper around this.
                 *
                 * @see Image::isCompressed(), Image::compressedSize(),
                 *      Image::fromCompressedData()
                 */
                bool isCompressed() const { return _compressed; }

                /** @brief Returns the list of FourCC codes associated with this format. */
                const FourCCList &fourccList() const { return _fourccList; }

                /** @brief Returns the number of components in this pixel format. */
                size_t compCount() const { return _compList.size(); }

                /**
                 * @brief Returns the component descriptor at the given index.
                 * @param index Zero-based component index.
                 * @return Reference to the CompDesc for that component.
                 */
                const CompDesc &compDesc(size_t index) const { return _compList[index]; }

                /** @brief Returns the number of planes in this pixel format. */
                size_t planeCount() const { return _planeList.size(); }

                /**
                 * @brief Returns the plane descriptor at the given index.
                 * @param index Zero-based plane index.
                 * @return Reference to the PlaneDesc for that plane.
                 */
                const PlaneDesc &planeDesc(size_t index) const { return _planeList[index]; }

                /**
                 * @brief Returns true if the given plane index is valid.
                 * @param index Zero-based plane index to check.
                 * @return true if the index is within the plane list.
                 */
                bool isValidPlane(size_t index) const { return index < _planeList.size(); }

                /**
                 * @brief Returns true if the given component count is sufficient.
                 * @param ct Number of components to check against.
                 * @return true if ct is at least as large as the format's component count.
                 */
                bool isValidCompCount(size_t ct) const { return ct >= _compList.size(); }

                /**
                 * @brief Returns the number of bytes per line for a given plane.
                 * @param planeIndex Zero-based plane index.
                 * @param desc       Image descriptor providing dimensions.
                 * @return Line stride in bytes.
                 */
                size_t lineStride(size_t planeIndex, const ImageDesc &desc) const;

                /**
                 * @brief Returns the total byte size of a given plane.
                 * @param planeIndex Zero-based plane index.
                 * @param desc       Image descriptor providing dimensions.
                 * @return Plane size in bytes.
                 */
                size_t planeSize(size_t planeIndex, const ImageDesc &desc) const;

                /**
                 * @brief Creates a PaintEngine that can draw on the given image.
                 * @param img The image to create a paint engine for.
                 * @return A PaintEngine configured for this pixel format.
                 */
                PaintEngine createPaintEngine(const Image &img) const;

        protected:
                int                             _id = Invalid;            ///< Unique format identifier.
                String                          _name;                    ///< Short format name.
                String                          _desc;                    ///< Human-readable description.
                Sampling                        _sampling = SamplingUndefined; ///< Chroma subsampling mode.
                size_t                          _pixelsPerBlock = 0;      ///< Pixels per encoded block.
                size_t                          _bytesPerBlock = 0;       ///< Bytes per encoded block.
                bool                            _hasAlpha = false;        ///< Whether format has alpha.
                bool                            _compressed = false;      ///< Whether format is compressed.
                FourCCList                      _fourccList;              ///< Associated FourCC codes.
                List<CompDesc>                  _compList;                ///< Component descriptors.
                List<PlaneDesc>                 _planeList;               ///< Plane descriptors.

                /**
                 * @brief Virtual implementation of lineStride().
                 * @param planeIndex Zero-based plane index.
                 * @param desc       Image descriptor providing dimensions.
                 * @return Line stride in bytes.
                 */
                virtual size_t __lineStride(size_t planeIndex, const ImageDesc &desc) const;

                /**
                 * @brief Virtual implementation of planeSize().
                 * @param planeIndex Zero-based plane index.
                 * @param desc       Image descriptor providing dimensions.
                 * @return Plane size in bytes.
                 */
                virtual size_t __planeSize(size_t planeIndex, const ImageDesc &desc) const;

                /**
                 * @brief Virtual implementation of createPaintEngine().
                 * @param image The image to create a paint engine for.
                 * @return A PaintEngine configured for this pixel format.
                 */
                virtual PaintEngine __createPaintEngine(const Image &image) const;
};

PROMEKI_NAMESPACE_END

