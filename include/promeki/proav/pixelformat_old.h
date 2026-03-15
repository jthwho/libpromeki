/**
 * @file      proav/pixelformat_old.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <vector>
#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/util.h>
#include <promeki/core/size2d.h>

#define PROMEKI_FOURCC(code) \
( \
        static_cast<PixelFormat::FourCC>(code[0]) | \
        (static_cast<PixelFormat::FourCC>(code[1])) << 8 | \
        (static_cast<PixelFormat::FourCC>(code[2])) << 16 | \
        (static_cast<PixelFormat::FourCC>(code[3])) << 24 \
)

PROMEKI_NAMESPACE_BEGIN

class Image;
class Pixel;

/**
 * @brief Legacy pixel format descriptor (superseded by the new PixelFormat).
 *
 * Provides an interface for interacting with data in a particular pixel
 * packing format.  This class does not concern itself with the concept of
 * color, only with the layout of components within packed data.
 *
 * @deprecated Use the new PixelFormat class in pixelformat.h instead.
 */
class PixelFormat {
        public:
                /** @brief Unique packing format identifier. */
                enum ID {
                        Invalid = 0,   ///< Invalid or uninitialized pixel format.
                        RGBA8,         ///< 8-bit RGBA (32 bits per pixel).
                        RGB8,          ///< 8-bit RGB (24 bits per pixel).
                        RGB10,         ///< 10-bit RGB.
                        YUV8_422,      ///< 8-bit YUV with 4:2:2 chroma subsampling.
                        YUV10_422      ///< 10-bit YUV with 4:2:2 chroma subsampling.
                };

                /** @brief Chroma subsampling mode. */
                enum Sampling {
                        SamplingUndefined = 0, ///< Undefined or unknown sampling.
                        Sampling444,           ///< 4:4:4 (no chroma subsampling).
                        Sampling422,           ///< 4:2:2 (horizontal chroma subsampling by 2).
                        Sampling411,           ///< 4:1:1 (horizontal chroma subsampling by 4).
                        Sampling420            ///< 4:2:0 (horizontal and vertical chroma subsampling by 2).
                };

                /** @brief Pixel component type. */
                enum CompType {
                        CompEmpty = 0, ///< Empty or unused component slot.
                        CompAlpha,     ///< Alpha (transparency) component.
                        CompRed,       ///< Red color component.
                        CompGreen,     ///< Green color component.
                        CompBlue,      ///< Blue color component.
                        CompY,         ///< Luma (Y) component.
                        CompCb,        ///< Blue-difference chroma (Cb/U) component.
                        CompCr         ///< Red-difference chroma (Cr/V) component.
                };

                /** @brief Component value type. */
                using Comp = uint16_t;

                /** @brief List of component values. */
                using CompList = std::vector<Comp>;

                /** @brief Describes a single component within the pixel format. */
                struct CompDesc {
                        int             plane; ///< Index of the plane this component resides in.
                        CompType        type;  ///< The type of this component.
                        size_t          bits;  ///< Number of bits used by this component.
                };

                /**
                 * @brief Function that returns the number of bytes per line.
                 * @param size Image dimensions.
                 * @return Line stride in bytes.
                 */
                typedef size_t (*StrideFunc)(const Size2Du32 &size);

                /**
                 * @brief Function that returns the total number of bytes for an image of the given size.
                 * @param size Image dimensions.
                 * @return Total image size in bytes.
                 */
                typedef size_t (*SizeFunc)(const Size2Du32 &size);

                /**
                 * @brief Function that fills an image with a pixel value from a component array.
                 * @param img   The image to fill.
                 * @param comps Array of component values (must have at least the required count).
                 * @return true on success.
                 */
                typedef bool (*FillFunc)(const Image &img, const Comp *comps);

                /**
                 * @brief Function that computes a Pixel from component inputs.
                 * @param comps Array of component values.
                 * @return A Pixel value.
                 */
                typedef Pixel (*CreatePixelFunc)(const Comp *comps);

                /** @brief Describes a single image plane with its stride and size functions. */
                struct PlaneDesc {
                        String          name;   ///< Human-readable plane name.
                        StrideFunc      stride; ///< Function to compute line stride.
                        SizeFunc        size;   ///< Function to compute plane size.
                };

                /** @brief FourCC code type. */
                typedef uint32_t FourCC;

                /** @brief Aggregate descriptor holding all metadata for a pixel format. */
                struct Data {
                        ID                              id;             ///< Format identifier.
                        String                          name;           ///< Short format name.
                        String                          desc;           ///< Human-readable description.
                        Sampling                        sampling;       ///< Chroma subsampling mode.
                        size_t                          pixelsPerBlock; ///< Pixels per encoded block.
                        size_t                          bytesPerBlock;  ///< Bytes per encoded block.
                        bool                            hasAlpha;       ///< Whether format has alpha.
                        std::vector<FourCC>             fourccList;     ///< Associated FourCC codes.
                        std::vector<CompDesc>           compList;       ///< Component descriptors.
                        std::vector<PlaneDesc>          planeList;      ///< Plane descriptors.

                        /** @brief Function to create a Pixel from component values. */
                        CreatePixelFunc                 createPixel;
                        /** @brief Function to fill an image with a pixel value. */
                        FillFunc                        fill;
                };

                /**
                 * @brief Returns the human-readable name for a given format ID.
                 * @param id The pixel format ID.
                 * @return Reference to the format name string.
                 */
                static const String &formatName(ID id);

                /**
                 * @brief Constructs a PixelFormat from an ID.
                 * @param id The pixel format ID (defaults to Invalid).
                 */
                PixelFormat(ID id = Invalid) : d(lookup(id)) { }

                /** @brief Copy assignment operator. */
                PixelFormat &operator=(const PixelFormat &o) {
                        d = o.d;
                        return *this;
                }

                /** @brief Returns true if both formats have the same ID. */
                bool operator==(const PixelFormat &o) const {
                        return d->id == o.d->id;
                }

                /** @brief Returns true if the formats have different IDs. */
                bool operator!=(const PixelFormat &o) const {
                        return d->id != o.d->id;
                }

                /** @brief Returns true if this pixel format is valid (not Invalid). */
                bool isValid() const {
                        return d->id != Invalid;
                }

                /** @brief Returns a const reference to the underlying Data descriptor. */
                const Data &data() const { return *d; }

                /** @brief Returns the format ID. */
                ID id() const { return d->id; }

                /** @brief Returns the short format name. */
                const String &name() const { return d->name; }

                /** @brief Returns the human-readable description. */
                const String &desc() const { return d->desc; }

                /** @brief Returns the number of components in this format. */
                size_t comps() const { return d->compList.size(); }

                /** @brief Returns the number of planes in this format. */
                size_t planes() const { return d->planeList.size(); }

                /**
                 * @brief Returns the line stride in bytes for the given image size and plane.
                 * @param s Image dimensions.
                 * @param p Plane index (defaults to 0).
                 * @return Line stride in bytes, or 0 if no stride function is set.
                 */
                size_t stride(const Size2Du32 &s, int p = 0) const {
                        StrideFunc func = d->planeList[p].stride;
                        return func == nullptr ? 0 : func(s);
                }

                /**
                 * @brief Returns the total byte size for the given image size and plane.
                 * @param s Image dimensions.
                 * @param p Plane index (defaults to 0).
                 * @return Plane size in bytes, or 0 if no size function is set.
                 */
                size_t size(const Size2Du32 &s, int p = 0) const {
                        SizeFunc func = d->planeList[p].size;
                        return func == nullptr ? 0 : func(s);
                }

                /**
                 * @brief Fills an image with the pixel value defined by a component list.
                 * @param img   The image to fill.
                 * @param comps List of component values.
                 * @return true on success.
                 */
                bool fill(const Image &img, const CompList &comps) const {
                        return fill(img, comps.data(), comps.size());
                }

                /**
                 * @brief Fills an image with the pixel value defined by a component array.
                 * @param img       The image to fill.
                 * @param comps     Array of component values.
                 * @param compCount Number of components in the array.
                 * @return true on success.
                 */
                bool fill(const Image &img, const Comp *comps, size_t compCount) const;

                /**
                 * @brief Creates a Pixel from a component array.
                 * @param comps     Array of component values.
                 * @param compCount Number of components in the array.
                 * @return A Pixel value.
                 */
                Pixel createPixel(const Comp *comps, size_t compCount) const;

        private:
                const Data *d = nullptr;

                static const Data *lookup(ID id);
};

PROMEKI_NAMESPACE_END

