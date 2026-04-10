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
 * @par Naming Convention
 *
 * C++ enum identifiers use a short prefix to indicate the plane layout:
 * - @b I_ — Interleaved (all components in a single plane).
 * - @b P_ — Planar (each component in its own plane).
 * - @b SP_ — Semi-planar (luma in one plane, chroma interleaved in another).
 *
 * The string name returned by name() omits the prefix for interleaved
 * formats (interleaved is the default assumption) and spells out
 * "Planar_" or "SemiPlanar_" for the others.
 *
 * @par Component Count
 *
 * Formats can have 1, 3, or 4 components:
 * - @b 1 — Monochrome / grayscale (single luma or intensity value).
 * - @b 3 — Typically RGB or Y'CbCr without alpha.
 * - @b 4 — Typically RGBA or Y'CbCr with alpha.
 *
 * After the prefix, the format of the name is:
 * @code
 * [<subsampling>_][<variant>_]<comps>x<bits>[_<storage>]
 * @endcode
 *
 * Where:
 * - @b subsampling — Chroma subsampling ratio, e.g. "422", "420", or
 *   "411".  Omitted for 4:4:4 (no subsampling).
 * - @b variant — Component ordering or packing variant, e.g. "UYVY",
 *   "v210", "NV21".  Omitted when the default ordering applies (YUYV
 *   for 4:2:2 interleaved, CbCr for semi-planar, sequential for
 *   4:4:4).  NV21 indicates CrCb chroma order (as opposed to the
 *   default CbCr order used by NV12/NV16).
 * - @b comps x bits — Component count and bit depth, e.g. "3x8",
 *   "4x16".  Omitted for formats with a unique packing (v210).
 *   For mixed bit-depth packed formats, the individual bit depths
 *   are spelled out (e.g. "10_10_10_2" for 3x10 + 1x2 in 32 bits).
 * - @b storage — Byte-order or packing mode:
 *   - "LE" / "BE" — for components stored in 16-bit or 32-bit words.
 *   - "DPX" — DPX/Cineon-style packed formats (e.g. 3x10 bits packed
 *     into a 32-bit word).
 *   - "F16" — Half-precision IEEE 754 float (16-bit).
 *   - "F32" — Single-precision IEEE 754 float (32-bit).
 *   - "10_10_10_2" — Mixed bit-depth packed format (3x10 + 1x2 in
 *     32 bits).
 *   .
 *   Omitted for byte-aligned formats (8-bit) where endianness is
 *   irrelevant.
 *
 * @par Examples
 * | C++ enum          | String name          | Description                                    |
 * |-------------------|----------------------|------------------------------------------------|
 * | I_4x8             | 4x8                  | 4 components, 8 bits, interleaved               |
 * | I_3x10_DPX        | 3x10_DPX             | 3 components, 10 bits, DPX packed (4 bytes)     |
 * | I_3x12_LE         | 3x12_LE              | 3 components, 12 bits in 16-bit LE words        |
 * | I_1x8             | 1x8                  | 1 component, 8 bits, monochrome                 |
 * | I_4xF16_LE        | 4xF16_LE             | 4 components, half-float LE                     |
 * | I_3xF32_LE        | 3xF32_LE             | 3 components, float LE                          |
 * | I_10_10_10_2_LE   | 10_10_10_2_LE        | 4 components (10+10+10+2) packed LE             |
 * | I_422_3x8         | 422_3x8              | 3 components, 8 bits, 4:2:2 YUYV                |
 * | I_422_UYVY_3x10_LE| 422_UYVY_3x10_LE     | 3 components, 10 bits LE, 4:2:2 UYVY            |
 * | P_422_3x8         | Planar_422_3x8       | 3 planes, 8 bits, 4:2:2 planar                  |
 * | P_411_3x8         | Planar_411_3x8       | 3 planes, 8 bits, 4:1:1 planar                  |
 * | SP_420_10_LE      | SemiPlanar_420_10_LE  | 2 planes, 10 bits LE, 4:2:0 NV12                |
 * | SP_420_NV21_8     | SemiPlanar_420_NV21_8 | 2 planes, 8 bits, 4:2:0 NV21 (CrCb order)       |
 * | SP_422_8          | SemiPlanar_422_8      | 2 planes, 8 bits, 4:2:2 NV16 semi-planar        |
 *
 * @par Example
 * @code
 * PixelFormat pf(PixelFormat::I_4x8);
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
                        Invalid              = 0,    ///< Invalid or uninitialized pixel format.

                        // -- Interleaved 4:4:4 (byte-aligned) --
                        I_4x8                = 1,    ///< 4 components, 8 bits each, interleaved (4 bytes/pixel).
                        I_3x8                = 2,    ///< 3 components, 8 bits each, interleaved (3 bytes/pixel).

                        // -- Interleaved 4:4:4 (DPX/Cineon packed) --
                        I_3x10_DPX           = 3,    ///< 3 components, 10 bits, DPX packed (30 bits in 4 bytes).

                        // -- Interleaved 4:2:2 YUYV --
                        I_422_3x8            = 4,    ///< 3 components, 8 bits, 4:2:2 YUYV interleaved.
                        I_422_3x10           = 5,    ///< 3 components, 10 bits, 4:2:2 YUYV interleaved.

                        // -- Interleaved 4:2:2 UYVY --
                        I_422_UYVY_3x8       = 6,    ///< 3 components, 8 bits, 4:2:2 UYVY interleaved.
                        I_422_UYVY_3x10_LE   = 7,    ///< 3 components, 10 bits in 16-bit LE words, 4:2:2 UYVY.
                        I_422_UYVY_3x10_BE   = 8,    ///< 3 components, 10 bits in 16-bit BE words, 4:2:2 UYVY.
                        I_422_UYVY_3x12_LE   = 9,    ///< 3 components, 12 bits in 16-bit LE words, 4:2:2 UYVY.
                        I_422_UYVY_3x12_BE   = 10,   ///< 3 components, 12 bits in 16-bit BE words, 4:2:2 UYVY.

                        // -- Interleaved 4:2:2 v210 --
                        I_422_v210           = 11,   ///< 3 components, 10 bits, 4:2:2 v210 packed (3x10 in 32-bit words).

                        // -- Planar 4:2:2 --
                        P_422_3x8            = 12,   ///< 3 planes, 8-bit, 4:2:2 (Y + Cb half-width + Cr half-width).
                        P_422_3x10_LE        = 13,   ///< 3 planes, 10-bit in 16-bit LE words, 4:2:2.
                        P_422_3x10_BE        = 14,   ///< 3 planes, 10-bit in 16-bit BE words, 4:2:2.
                        P_422_3x12_LE        = 15,   ///< 3 planes, 12-bit in 16-bit LE words, 4:2:2.
                        P_422_3x12_BE        = 16,   ///< 3 planes, 12-bit in 16-bit BE words, 4:2:2.

                        // -- Planar 4:2:0 --
                        P_420_3x8            = 17,   ///< 3 planes, 8-bit, 4:2:0 (Y + Cb quarter + Cr quarter).
                        P_420_3x10_LE        = 18,   ///< 3 planes, 10-bit in 16-bit LE words, 4:2:0.
                        P_420_3x10_BE        = 19,   ///< 3 planes, 10-bit in 16-bit BE words, 4:2:0.
                        P_420_3x12_LE        = 20,   ///< 3 planes, 12-bit in 16-bit LE words, 4:2:0.
                        P_420_3x12_BE        = 21,   ///< 3 planes, 12-bit in 16-bit BE words, 4:2:0.

                        // -- Semi-planar 4:2:0 (NV12) --
                        SP_420_8             = 22,   ///< 2 planes, 8-bit, 4:2:0 NV12 (Y + interleaved CbCr).
                        SP_420_10_LE         = 23,   ///< 2 planes, 10-bit in 16-bit LE words, 4:2:0 NV12.
                        SP_420_10_BE         = 24,   ///< 2 planes, 10-bit in 16-bit BE words, 4:2:0 NV12.
                        SP_420_12_LE         = 25,   ///< 2 planes, 12-bit in 16-bit LE words, 4:2:0 NV12.
                        SP_420_12_BE         = 26,   ///< 2 planes, 12-bit in 16-bit BE words, 4:2:0 NV12.

                        // -- Interleaved 4:4:4 (10/12/16-bit in 16-bit words) --
                        I_4x10_LE            = 27,   ///< 4 components, 10 bits in 16-bit LE words (8 bytes/pixel).
                        I_4x10_BE            = 28,   ///< 4 components, 10 bits in 16-bit BE words (8 bytes/pixel).
                        I_3x10_LE            = 29,   ///< 3 components, 10 bits in 16-bit LE words (6 bytes/pixel).
                        I_3x10_BE            = 30,   ///< 3 components, 10 bits in 16-bit BE words (6 bytes/pixel).
                        I_4x12_LE            = 31,   ///< 4 components, 12 bits in 16-bit LE words (8 bytes/pixel).
                        I_4x12_BE            = 32,   ///< 4 components, 12 bits in 16-bit BE words (8 bytes/pixel).
                        I_3x12_LE            = 33,   ///< 3 components, 12 bits in 16-bit LE words (6 bytes/pixel).
                        I_3x12_BE            = 34,   ///< 3 components, 12 bits in 16-bit BE words (6 bytes/pixel).
                        I_4x16_LE            = 35,   ///< 4 components, 16 bits LE (8 bytes/pixel).
                        I_4x16_BE            = 36,   ///< 4 components, 16 bits BE (8 bytes/pixel).
                        I_3x16_LE            = 37,   ///< 3 components, 16 bits LE (6 bytes/pixel).
                        I_3x16_BE            = 38,   ///< 3 components, 16 bits BE (6 bytes/pixel).

                        // -- Monochrome (single component) --
                        I_1x8                = 39,   ///< 1 component, 8 bits (1 byte/pixel).
                        I_1x10_LE            = 40,   ///< 1 component, 10 bits in 16-bit LE word (2 bytes/pixel).
                        I_1x10_BE            = 41,   ///< 1 component, 10 bits in 16-bit BE word (2 bytes/pixel).
                        I_1x12_LE            = 42,   ///< 1 component, 12 bits in 16-bit LE word (2 bytes/pixel).
                        I_1x12_BE            = 43,   ///< 1 component, 12 bits in 16-bit BE word (2 bytes/pixel).
                        I_1x16_LE            = 44,   ///< 1 component, 16 bits LE (2 bytes/pixel).
                        I_1x16_BE            = 45,   ///< 1 component, 16 bits BE (2 bytes/pixel).

                        // -- Float half-precision (16-bit IEEE 754) --
                        I_4xF16_LE           = 46,   ///< 4 components, half-float LE (8 bytes/pixel).
                        I_4xF16_BE           = 47,   ///< 4 components, half-float BE (8 bytes/pixel).
                        I_3xF16_LE           = 48,   ///< 3 components, half-float LE (6 bytes/pixel).
                        I_3xF16_BE           = 49,   ///< 3 components, half-float BE (6 bytes/pixel).
                        I_1xF16_LE           = 50,   ///< 1 component, half-float LE (2 bytes/pixel).
                        I_1xF16_BE           = 51,   ///< 1 component, half-float BE (2 bytes/pixel).

                        // -- Float single-precision (32-bit IEEE 754) --
                        I_4xF32_LE           = 52,   ///< 4 components, float LE (16 bytes/pixel).
                        I_4xF32_BE           = 53,   ///< 4 components, float BE (16 bytes/pixel).
                        I_3xF32_LE           = 54,   ///< 3 components, float LE (12 bytes/pixel).
                        I_3xF32_BE           = 55,   ///< 3 components, float BE (12 bytes/pixel).
                        I_1xF32_LE           = 56,   ///< 1 component, float LE (4 bytes/pixel).
                        I_1xF32_BE           = 57,   ///< 1 component, float BE (4 bytes/pixel).

                        // -- 10:10:10:2 packed (3x10-bit + 1x2-bit in 32 bits) --
                        I_10_10_10_2_LE      = 58,   ///< 4 components (10+10+10+2 bits) in 32-bit LE word.
                        I_10_10_10_2_BE      = 59,   ///< 4 components (10+10+10+2 bits) in 32-bit BE word.

                        // -- Semi-planar 4:2:0 NV21 (CrCb order) --
                        SP_420_NV21_8        = 60,   ///< 2 planes, 8-bit, 4:2:0 NV21 (Y + interleaved CrCb).
                        SP_420_NV21_10_LE    = 61,   ///< 2 planes, 10-bit in 16-bit LE words, 4:2:0 NV21.
                        SP_420_NV21_10_BE    = 62,   ///< 2 planes, 10-bit in 16-bit BE words, 4:2:0 NV21.
                        SP_420_NV21_12_LE    = 63,   ///< 2 planes, 12-bit in 16-bit LE words, 4:2:0 NV21.
                        SP_420_NV21_12_BE    = 64,   ///< 2 planes, 12-bit in 16-bit BE words, 4:2:0 NV21.

                        // -- Semi-planar 4:2:2 (NV16) --
                        SP_422_8             = 65,   ///< 2 planes, 8-bit, 4:2:2 NV16 (Y + interleaved CbCr).
                        SP_422_10_LE         = 66,   ///< 2 planes, 10-bit in 16-bit LE words, 4:2:2 NV16.
                        SP_422_10_BE         = 67,   ///< 2 planes, 10-bit in 16-bit BE words, 4:2:2 NV16.
                        SP_422_12_LE         = 68,   ///< 2 planes, 12-bit in 16-bit LE words, 4:2:2 NV16.
                        SP_422_12_BE         = 69,   ///< 2 planes, 12-bit in 16-bit BE words, 4:2:2 NV16.

                        // -- Planar 4:1:1 --
                        P_411_3x8            = 70,   ///< 3 planes, 8-bit, 4:1:1 (Y + Cb quarter-width + Cr quarter-width).

                        // -- 16-bit YCbCr additions --
                        P_422_3x16_LE        = 71,   ///< 3 planes, 16-bit LE, 4:2:2.
                        P_422_3x16_BE        = 72,   ///< 3 planes, 16-bit BE, 4:2:2.
                        P_420_3x16_LE        = 73,   ///< 3 planes, 16-bit LE, 4:2:0.
                        P_420_3x16_BE        = 74,   ///< 3 planes, 16-bit BE, 4:2:0.
                        SP_420_16_LE         = 75,   ///< 2 planes, 16-bit LE, 4:2:0 NV12.
                        SP_420_16_BE         = 76,   ///< 2 planes, 16-bit BE, 4:2:0 NV12.
                        I_422_UYVY_3x16_LE   = 77,   ///< 3 components, 16-bit LE, 4:2:2 UYVY.
                        I_422_UYVY_3x16_BE   = 78,   ///< 3 components, 16-bit BE, 4:2:2 UYVY.

                        // -- Planar 4:4:4 (RGB / YUV 4:4:4) --
                        P_444_3x8            = 79,   ///< 3 planes, 8-bit, 4:4:4 (equal-sized planes, no subsampling).

                        UserDefined          = 1024  ///< First ID available for user-registered types.
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
                        String          name;                           ///< Short format name (e.g. "I_4x8").
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
