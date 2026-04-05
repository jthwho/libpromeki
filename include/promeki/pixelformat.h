/**
 * @file      pixelformat.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Describes the memory layout of pixel data without color semantics.
 * @ingroup proav
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight inline
 * wrapper around an immutable Data record, identified by an integer ID.
 * Well-known pixel formats are provided as ID constants; user-defined
 * formats can be registered at runtime via registerType() and registerData().
 *
 * PixelFormat concerns itself only with the mechanical aspects of pixel
 * storage: component count, bit depths, bytes per block, planes, and
 * chroma subsampling.  It does not know what the components represent
 * (e.g. R/G/B vs Y/Cb/Cr), whether there is an alpha channel, or any
 * color model details.  Those semantics belong to PixelDesc.
 *
 * @par Example
 * @code
 * PixelFormat pf(PixelFormat::Interleaved_4x8);
 * size_t stride = pf.lineStride(0, 1920, 0, 1);
 * size_t planeBytes = pf.planeSize(0, 1920, 1080, 0, 1);
 * @endcode
 *
 * @see PixelDesc, @ref typeregistry "TypeRegistry Pattern"
 */
class PixelFormat {
        public:
                static constexpr size_t MaxComps  = 8; ///< Maximum number of components per pixel.
                static constexpr size_t MaxPlanes = 4; ///< Maximum number of planes per format.

                /**
                 * @brief Identifies a pixel format.
                 *
                 * Well-known formats have named enumerators.  User-defined
                 * formats obtain IDs from registerType().  The atomic counter
                 * starts at UserDefined.
                 */
                enum ID {
                        Invalid             = 0,    ///< Invalid or uninitialized pixel format.
                        Interleaved_4x8     = 1,    ///< 4 components, 8 bits each, 1 interleaved plane.
                        Interleaved_3x8     = 2,    ///< 3 components, 8 bits each, 1 interleaved plane.
                        Interleaved_3x10    = 3,    ///< 3 components, 10 bits each, 1 interleaved plane.
                        Interleaved_422_3x8 = 4,    ///< 3 components, 8 bits, 4:2:2 YUYV, 1 interleaved plane.
                        Interleaved_422_3x10 = 5,   ///< 3 components, 10 bits, 4:2:2 YUYV, 1 interleaved plane.
                        Interleaved_422_UYVY_3x8 = 6,      ///< 3 components, 8 bits, 4:2:2 UYVY, 1 interleaved plane.
                        Interleaved_422_UYVY_3x10_LE = 7,  ///< 3 components, 10 bits in 16-bit LE words, 4:2:2 UYVY.
                        Interleaved_422_UYVY_3x10_BE = 8,  ///< 3 components, 10 bits in 16-bit BE words, 4:2:2 UYVY.
                        Interleaved_422_UYVY_3x12_LE = 9,  ///< 3 components, 12 bits in 16-bit LE words, 4:2:2 UYVY.
                        Interleaved_422_UYVY_3x12_BE = 10, ///< 3 components, 12 bits in 16-bit BE words, 4:2:2 UYVY.
                        Interleaved_422_v210 = 11,          ///< 3 components, 10 bits, 4:2:2 v210 packed (3x10 in 32-bit words).
                        Planar_422_3x8       = 12,  ///< 3 planes, 8-bit, 4:2:2 (Y + Cb half-width + Cr half-width).
                        Planar_422_3x10_LE   = 13,  ///< 3 planes, 10-bit in 16-bit LE words, 4:2:2.
                        Planar_422_3x10_BE   = 14,  ///< 3 planes, 10-bit in 16-bit BE words, 4:2:2.
                        Planar_422_3x12_LE   = 15,  ///< 3 planes, 12-bit in 16-bit LE words, 4:2:2.
                        Planar_422_3x12_BE   = 16,  ///< 3 planes, 12-bit in 16-bit BE words, 4:2:2.
                        Planar_420_3x8       = 17,  ///< 3 planes, 8-bit, 4:2:0 (Y + Cb quarter + Cr quarter).
                        Planar_420_3x10_LE   = 18,  ///< 3 planes, 10-bit in 16-bit LE words, 4:2:0.
                        Planar_420_3x10_BE   = 19,  ///< 3 planes, 10-bit in 16-bit BE words, 4:2:0.
                        Planar_420_3x12_LE   = 20,  ///< 3 planes, 12-bit in 16-bit LE words, 4:2:0.
                        Planar_420_3x12_BE   = 21,  ///< 3 planes, 12-bit in 16-bit BE words, 4:2:0.
                        SemiPlanar_420_8     = 22,  ///< 2 planes, 8-bit, 4:2:0 NV12 (Y + interleaved CbCr).
                        SemiPlanar_420_10_LE = 23,  ///< 2 planes, 10-bit in 16-bit LE words, 4:2:0 NV12.
                        SemiPlanar_420_10_BE = 24,  ///< 2 planes, 10-bit in 16-bit BE words, 4:2:0 NV12.
                        SemiPlanar_420_12_LE = 25,  ///< 2 planes, 12-bit in 16-bit LE words, 4:2:0 NV12.
                        SemiPlanar_420_12_BE = 26,  ///< 2 planes, 12-bit in 16-bit BE words, 4:2:0 NV12.
                        UserDefined         = 1024  ///< First ID available for user-registered types.
                };

                /** @brief Chroma subsampling mode. */
                enum Sampling {
                        SamplingUndefined = 0, ///< Undefined or unknown sampling.
                        Sampling444,           ///< 4:4:4 (no chroma subsampling).
                        Sampling422,           ///< 4:2:2 (horizontal chroma subsampling by 2).
                        Sampling411,           ///< 4:1:1 (horizontal chroma subsampling by 4).
                        Sampling420            ///< 4:2:0 (horizontal and vertical chroma subsampling by 2).
                };

                /** @brief Horizontal chroma sample siting relative to luma. */
                enum ChromaSitingH {
                        ChromaHUndefined = 0, ///< Undefined (not subsampled or unspecified).
                        ChromaHLeft,          ///< Co-sited with left luma sample.
                        ChromaHCenter         ///< Centered between luma samples.
                };

                /** @brief Vertical chroma sample siting relative to luma. */
                enum ChromaSitingV {
                        ChromaVUndefined = 0, ///< Undefined (not subsampled or unspecified).
                        ChromaVTop,           ///< Co-sited with top luma row.
                        ChromaVCenter         ///< Centered between luma rows.
                };

                /** @brief List of PixelFormat IDs. */
                using IDList = List<ID>;

                /** @brief Describes a single component within the pixel format. */
                struct CompDesc {
                        int    plane;      ///< Index of the plane this component resides in.
                        size_t bits;       ///< Number of bits used by this component.
                        size_t byteOffset; ///< Byte offset of this component within the pixel block.
                };

                /** @brief Describes a single image plane. */
                struct PlaneDesc {
                        String name;              ///< Human-readable name for the plane.
                        size_t hSubsampling = 1;  ///< Horizontal subsampling (1 = full, 2 = half width).
                        size_t vSubsampling = 1;  ///< Vertical subsampling (1 = full, 2 = half height).
                        size_t bytesPerSample = 0;///< Bytes per sample in this plane (0 = use block math).
                };

                /** @brief Immutable data record for a pixel format. */
                struct Data {
                        ID              id = Invalid;                   ///< Unique format identifier.
                        String          name;                           ///< Short format name (e.g. "Interleaved_4x8").
                        String          desc;                           ///< Human-readable description.
                        Sampling        sampling = SamplingUndefined;   ///< Chroma subsampling mode.
                        size_t          pixelsPerBlock = 0;             ///< Number of pixels in one encoded block.
                        size_t          bytesPerBlock = 0;              ///< Number of bytes in one encoded block.
                        size_t          compCount = 0;                  ///< Number of components per pixel.
                        CompDesc        comps[MaxComps] = {};           ///< Component descriptors.
                        size_t          planeCount = 0;                 ///< Number of planes.
                        PlaneDesc       planes[MaxPlanes] = {};         ///< Plane descriptors.
                        ChromaSitingH   chromaSitingH = ChromaHUndefined; ///< Horizontal chroma siting.
                        ChromaSitingV   chromaSitingV = ChromaVUndefined; ///< Vertical chroma siting.

                        /**
                         * @brief Computes the line stride for a given plane.
                         * @param d         Pointer to this Data record.
                         * @param planeIdx  Zero-based plane index.
                         * @param width     Image width in pixels.
                         * @param linePad   Padding bytes appended to each scanline.
                         * @param lineAlign Alignment requirement in bytes.
                         * @return Line stride in bytes, or 0 if not applicable.
                         */
                        size_t (*lineStrideFunc)(const Data *d, size_t planeIdx,
                                                size_t width, size_t linePad, size_t lineAlign) = nullptr;

                        /**
                         * @brief Computes the total byte size of a given plane.
                         * @param d         Pointer to this Data record.
                         * @param planeIdx  Zero-based plane index.
                         * @param width     Image width in pixels.
                         * @param height    Image height in pixels.
                         * @param linePad   Padding bytes appended to each scanline.
                         * @param lineAlign Alignment requirement in bytes.
                         * @return Plane size in bytes, or 0 if not applicable.
                         */
                        size_t (*planeSizeFunc)(const Data *d, size_t planeIdx,
                                               size_t width, size_t height,
                                               size_t linePad, size_t lineAlign) = nullptr;
                };

                /**
                 * @brief Allocates and returns a unique ID for a user-defined pixel format.
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
                 * After this call, constructing a PixelFormat from @p data.id
                 * will resolve to the registered data.
                 *
                 * @param data The populated Data struct with id set to a value from registerType().
                 * @see registerType()
                 */
                static void registerData(Data &&data);

                /**
                 * @brief Returns a list of all registered PixelFormat IDs.
                 *
                 * Excludes Invalid.  Includes both well-known and user-registered types.
                 *
                 * @return A list of ID values.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Looks up a pixel format by name.
                 * @param name The format name to search for.
                 * @return A PixelFormat wrapping the found format, or Invalid if not found.
                 */
                static PixelFormat lookup(const String &name);

                /**
                 * @brief Constructs a PixelFormat for the given ID.
                 * @param id The pixel format to use (default: Invalid).
                 */
                inline PixelFormat(ID id = Invalid);

                /** @brief Returns true if this pixel format is valid (not Invalid). */
                bool isValid() const { return d != nullptr && d->id != Invalid; }

                /** @brief Returns the unique ID of this pixel format. */
                ID id() const { return d->id; }

                /** @brief Returns the short name of this pixel format. */
                const String &name() const { return d->name; }

                /** @brief Returns a human-readable description of this pixel format. */
                const String &desc() const { return d->desc; }

                /** @brief Returns the chroma subsampling mode. */
                Sampling sampling() const { return d->sampling; }

                /** @brief Returns the horizontal chroma sample siting. */
                ChromaSitingH chromaSitingH() const { return d->chromaSitingH; }

                /** @brief Returns the vertical chroma sample siting. */
                ChromaSitingV chromaSitingV() const { return d->chromaSitingV; }

                /** @brief Returns the number of pixels in one encoded block. */
                size_t pixelsPerBlock() const { return d->pixelsPerBlock; }

                /** @brief Returns the number of bytes in one encoded block. */
                size_t bytesPerBlock() const { return d->bytesPerBlock; }

                /** @brief Returns the number of components in this pixel format. */
                size_t compCount() const { return d->compCount; }

                /**
                 * @brief Returns the component descriptor at the given index.
                 * @param index Zero-based component index.
                 * @return Reference to the CompDesc for that component.
                 */
                const CompDesc &compDesc(size_t index) const { return d->comps[index]; }

                /** @brief Returns the number of planes in this pixel format. */
                size_t planeCount() const { return d->planeCount; }

                /**
                 * @brief Returns the plane descriptor at the given index.
                 * @param index Zero-based plane index.
                 * @return Reference to the PlaneDesc for that plane.
                 */
                const PlaneDesc &planeDesc(size_t index) const { return d->planes[index]; }

                /** @brief Returns true if the given plane index is valid. */
                bool isValidPlane(size_t index) const { return index < d->planeCount; }

                /**
                 * @brief Returns the line stride in bytes for a given plane.
                 * @param planeIndex Zero-based plane index.
                 * @param width      Image width in pixels.
                 * @param linePad    Padding bytes appended to each scanline.
                 * @param lineAlign  Alignment requirement in bytes.
                 * @return Line stride in bytes, or 0 if the plane is invalid or no function is set.
                 */
                size_t lineStride(size_t planeIndex, size_t width,
                                  size_t linePad = 0, size_t lineAlign = 1) const {
                        if(!isValidPlane(planeIndex) || d->lineStrideFunc == nullptr) return 0;
                        return d->lineStrideFunc(d, planeIndex, width, linePad, lineAlign);
                }

                /**
                 * @brief Returns the total byte size of a given plane.
                 * @param planeIndex Zero-based plane index.
                 * @param width      Image width in pixels.
                 * @param height     Image height in pixels.
                 * @param linePad    Padding bytes appended to each scanline.
                 * @param lineAlign  Alignment requirement in bytes.
                 * @return Plane size in bytes, or 0 if the plane is invalid or no function is set.
                 */
                size_t planeSize(size_t planeIndex, size_t width, size_t height,
                                 size_t linePad = 0, size_t lineAlign = 1) const {
                        if(!isValidPlane(planeIndex) || d->planeSizeFunc == nullptr) return 0;
                        return d->planeSizeFunc(d, planeIndex, width, height, linePad, lineAlign);
                }

                /** @brief Equality comparison (identity-based). */
                bool operator==(const PixelFormat &o) const { return d == o.d; }

                /** @brief Inequality comparison. */
                bool operator!=(const PixelFormat &o) const { return d != o.d; }

                /** @brief Returns the underlying Data pointer. */
                const Data *data() const { return d; }

        private:
                const Data *d = nullptr;
                static const Data *lookupData(ID id);
};

inline PixelFormat::PixelFormat(ID id) : d(lookupData(id)) {}

PROMEKI_NAMESPACE_END
