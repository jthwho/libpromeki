/**
 * @file      imagedataencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/array.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/buffer.h>
#include <promeki/crc.h>

PROMEKI_NAMESPACE_BEGIN

class Image;
class UncompressedVideoPayload;

/**
 * @brief Fast VITC-style binary data encoder for raster images.
 * @ingroup proav
 *
 * Stamps 64-bit opaque payloads into Image scan lines as a sequence of
 * wide black/white bit cells, in a layout that resembles SMPTE VITC but
 * is *not* SMPTE-compliant — see @ref imagedataencoder for the full
 * wire format specification, the bit/byte ordering, the CRC parameters,
 * and a worked example a third party can use to write a compatible
 * decoder.
 *
 * @par Wire format
 * Each scan line that carries a payload is laid out as @c N adjacent
 * "bit cells" of equal pixel width followed by a black padding region:
 *
 * @verbatim
 *   |  4 sync bits  |       64 payload bits        |  8 CRC bits  | pad |
 *   | W B W B       | MSB  ............... LSB     | MSB ... LSB  |     |
 * @endverbatim
 *
 * - **Sync nibble**: white, black, white, black (binary @c 1010,
 *   transmitted MSB-first), giving the decoder a fixed alignment
 *   pattern at the start of every cell row.
 * - **Payload**: a 64-bit value (e.g. a frame ID, a BCD timecode word
 *   from @ref Timecode::toBcd64, an opaque tag), transmitted MSB-first.
 * - **CRC**: 8 bits of CRC-8/AUTOSAR computed over the 8 payload bytes
 *   in big-endian order — byte 0 is bits 56-63 of the payload, byte 7
 *   is bits 0-7.  The CRC is transmitted MSB-first.
 * - **Padding**: any pixels left over after @c 76 cells fit in the
 *   image width are filled with black on the luma plane (and the
 *   format's neutral gray on chroma planes).
 *
 * The bit-cell width is chosen at construction time as the largest
 * value such that @c 76 cells fit on the scan line *and* the cell
 * width in pixels is a multiple of the format's natural alignment
 * quantum (e.g. v210's 6-pixel block, 4:2:2 chroma subsampling's
 * 2-pixel quantum).  This guarantees that adjacent cells start at
 * properly-aligned byte offsets and lets the hot path use a single
 * @c memcpy per cell.
 *
 * @par Per-format value mapping
 * The encoder uses libpromeki's CSC pipeline to render its three
 * "primer" cells (one full-white pixel cell, one full-black pixel
 * cell, and one neutral / padding cell) into the target PixelFormat
 * once at construction time.  This means:
 *
 * - For RGB formats, "white" is the format's per-component max,
 *   "black" is the per-component min.
 * - For YCbCr formats, "white" is luma at @c rangeMax and chroma at
 *   the per-component midpoint (so the bit pattern only modulates
 *   luma, never chroma).
 * - For planar / semi-planar formats, the chroma planes are filled
 *   with neutral gray throughout the encoded scan-line range — both
 *   the bit cells and the padding region.
 *
 * After construction the only work the encoder does per scan line is
 * a fixed sequence of @c memcpy calls — 76 per scan line per plane,
 * plus one for the trailing padding.  At 1080p60 with 32 stamped scan
 * lines per frame and a 4-byte-per-pixel format, that's roughly
 * @c 2.4M memcpy bytes per second — comfortably below the noise
 * floor of any modern memory subsystem.
 *
 * @par Lifetime and reuse
 * Construct one encoder per (PixelFormat, image dimensions) pair and
 * reuse it across many frames.  The encoder owns its primer cells
 * and CRC state internally; @ref encode is reentrant on a single
 * instance only with respect to itself (do not call @c encode from
 * two threads on the same instance simultaneously).  Use
 * @ref isValid to detect a construction failure (image too narrow
 * for any cell width, format unsupported by CSC, etc.).
 *
 * @par Example
 * @code
 * ImageDataEncoder enc(img.desc());
 * if(!enc.isValid()) {
 *     // image too narrow for any bit cell — bail out
 * }
 *
 * ImageDataEncoder::Item items[] = {
 *     { 0,  16, frameId      },   // first 16 lines: rolling frame ID
 *     { 16, 16, tc.toBcd64() },   // next 16 lines: BCD timecode
 * };
 * Error err = enc.encode(img, items);
 * @endcode
 *
 * @see Timecode::toBcd64, CRC, @ref imagedataencoder
 */
class ImageDataEncoder {
        public:
                /**
                 * @brief Number of "primer" patterns the encoder maintains
                 *        (white cell, black cell, neutral pad).
                 */
                static constexpr size_t PrimerCount = 3;

                /** @brief Number of sync bits at the start of every cell row. */
                static constexpr uint32_t SyncBits      = 4;
                /** @brief Number of payload bits in every cell row. */
                static constexpr uint32_t PayloadBits   = 64;
                /** @brief Number of CRC bits in every cell row. */
                static constexpr uint32_t CrcBits       = 8;
                /** @brief Total bits per cell row (sync + payload + CRC). */
                static constexpr uint32_t BitsPerRow    = SyncBits + PayloadBits + CrcBits;

                /**
                 * @brief Sync nibble bit pattern @c 1010 (white / black /
                 *        white / black, MSB-first).
                 */
                static constexpr uint8_t SyncNibble = 0xAu;

                /**
                 * @brief Maximum number of planes the encoder supports.
                 *
                 * Matches @c PixelMemLayout::MaxPlanes.
                 */
                static constexpr size_t MaxPlanes = 4;

                /**
                 * @brief One scan-line band of an image to stamp with a
                 *        single 64-bit payload.
                 *
                 * @c firstLine and @c lineCount are in luma scan lines.
                 * For sub-sampled chroma planes the encoder writes any
                 * chroma row that overlaps the luma range — see the
                 * "Alignment" note in @ref imagedataencoder.
                 */
                struct Item {
                        uint32_t firstLine;   ///< First (luma) scan line to stamp.
                        uint32_t lineCount;   ///< Number of (luma) scan lines covered.
                        uint64_t payload;     ///< Opaque 64-bit payload to encode.
                };

                /** @brief Constructs an invalid encoder. */
                ImageDataEncoder() = default;

                /**
                 * @brief Constructs an encoder for an image of the given
                 *        descriptor.
                 *
                 * Computes the bit-cell width, builds the per-plane primer
                 * buffers via the CSC pipeline, and initialises the CRC
                 * table.  After construction call @ref isValid to check
                 * whether the image is wide enough for any cell row.
                 *
                 * @param desc Image descriptor (size + pixel description).
                 */
                explicit ImageDataEncoder(const ImageDesc &desc);

                /** @brief Returns @c true if the encoder is ready to use. */
                bool isValid() const { return _valid; }

                /** @brief Returns the bit-cell width in pixels. */
                uint32_t bitWidth() const { return _bitWidth; }

                /** @brief Returns the total number of pattern pixels per row. */
                uint32_t patternWidth() const { return _bitWidth * BitsPerRow; }

                /** @brief Returns the trailing pad width in pixels. */
                uint32_t padWidth() const { return _padWidth; }

                /** @brief Returns the image descriptor the encoder was built for. */
                const ImageDesc &desc() const { return _desc; }

                /**
                 * @brief Stamps every item's payload into the supplied image.
                 *
                 * The image must have the same descriptor as the one
                 * supplied to the constructor (@c img.desc() must compare
                 * equal — pixel description, dimensions, and metadata
                 * irrelevant).  The payload's plane buffers are modified
                 * in place.
                 *
                 * @param inout Payload to write into.  Must already be
                 *              allocated.
                 * @param items Bands of scan lines to stamp.
                 * @return @c Error::Ok on success, or an error code if
                 *         the encoder is invalid, the payload
                 *         descriptor does not match, or an item
                 *         references scan lines outside the payload.
                 */
                Error encode(UncompressedVideoPayload &inout,
                             const List<Item> &items) const;

                /** @brief Convenience overload taking a single Item. */
                Error encode(UncompressedVideoPayload &inout,
                             const Item &item) const;

        private:
                struct PlaneInfo {
                        size_t              lineStride   = 0;     ///< Full-image plane line stride in bytes.
                        size_t              hSubsampling = 1;
                        size_t              vSubsampling = 1;
                        size_t              cellBytes    = 0;     ///< Bytes per bit cell (this plane).
                        size_t              padBytes     = 0;     ///< Bytes for the trailing pad region (this plane).
                        Buffer              oneCell;              ///< White-bit primer (cellBytes bytes).
                        Buffer              zeroCell;             ///< Black-bit primer (cellBytes bytes).
                        Buffer              padBuf;               ///< Pad / neutral buffer (padBytes bytes).
                };

                ImageDesc                       _desc;
                uint32_t                        _bitWidth   = 0;
                uint32_t                        _padWidth   = 0;
                size_t                          _planeCount = 0;
                Array<PlaneInfo, MaxPlanes>     _planes{};
                bool                            _valid      = false;

                bool buildPrimers();
                void writeOneScanline(uint8_t *planeBase, size_t planeIndex,
                                      size_t lineInPlane,
                                      uint8_t syncBits,
                                      uint64_t payload,
                                      uint8_t crcBits) const;

        public:
                /** @internal Stamps one item across one plane; used by encode(). */
                void writeScanlineBase(uint8_t *planeBase,
                                       size_t planeIndex,
                                       const Item &item,
                                       uint64_t lastEx,
                                       uint8_t syncBits,
                                       uint8_t crcVal) const;
};

PROMEKI_NAMESPACE_END
