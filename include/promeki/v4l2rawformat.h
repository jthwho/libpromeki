/**
 * @file      v4l2rawformat.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_V4L2
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/pixelformat.h>
#include <promeki/v4l2m2mcodec.h>

PROMEKI_NAMESPACE_BEGIN

class UncompressedVideoPayload;

/**
 * @brief Maps a promeki @ref PixelFormat to its V4L2 raw FourCC and the
 *        plane geometry the codec queues use, plus the byte/bit packing
 *        needed to move pixels between the two.
 * @ingroup proav
 *
 * The V4L2 mem2mem encoder and decoder share this table so the raw
 * pixel-format support (NV12, NV16, P010, …) lives in one place.  Only
 * @em semi-planar Y + interleaved-CbCr layouts are described here, which
 * is what the Xilinx VCU / Raspberry Pi codecs and the kernel @c vicodec
 * driver consume:
 *
 *   - 8-bit: a per-row byte copy (NV12 4:2:0, NV16 4:2:2).
 *   - 10-bit: 16-bit little-endian words.  promeki stores the sample
 *     @em LSB-aligned (value in the low 10 bits); V4L2 @c P010 stores it
 *     @em MSB-aligned (value @c <<6, low 6 bits zero), so the copy
 *     shifts each sample by @ref chromaShift on the way in / out.
 */
struct V4l2RawFormat {
                uint32_t        fourcc = 0;                       ///< V4L2 FourCC (0 = invalid entry).
                PixelFormat::ID pixelFormatId = PixelFormat::Invalid; ///< promeki PixelFormat.
                uint8_t         bytesPerSample = 1;               ///< 1 (8-bit) or 2 (10-bit in 16-bit word).
                uint8_t         shift = 0;                        ///< MSB-align shift for 10-bit (6 for P010); 0 otherwise.
                uint8_t         chromaVDiv = 2;                   ///< Vertical chroma subsample (2 = 4:2:0, 1 = 4:2:2).
                const char     *name = "";                        ///< Short label for diagnostics.

                bool isValid() const { return fourcc != 0; }
};

/** @brief Looks up the raw-format descriptor for a promeki @ref PixelFormat (null when unsupported). */
const V4l2RawFormat *v4l2RawFormatForPixelFormat(PixelFormat::ID id);

/** @brief Looks up the raw-format descriptor for a V4L2 FourCC (null when unsupported). */
const V4l2RawFormat *v4l2RawFormatForFourcc(uint32_t fourcc);

/** @brief Every promeki @ref PixelFormat (as @c int) the V4L2 codecs can ingest / emit. */
List<int> v4l2SupportedRawPixelFormats();

/**
 * @brief Packs a source payload's planes into the codec's OUTPUT buffer.
 *
 * Handles both the single-contiguous-plane and two-separate-plane V4L2
 * layouts, the 8-bit byte copy and the 10-bit MSB-align shift, and 4:2:0
 * vs 4:2:2 chroma height.
 *
 * @param      src       The source uncompressed payload (must match @p fmt).
 * @param      fmt       The descriptor selected for @p src's PixelFormat.
 * @param      dst       The OUTPUT planes from @ref V4l2M2mCodec::acquireOutput.
 * @param[out] bytesused One filled byte count per OUTPUT memory-plane.
 */
void v4l2PackSemiPlanar(const UncompressedVideoPayload &src, const V4l2RawFormat &fmt,
                        const List<V4l2M2mCodec::OutPlane> &dst, List<size_t> &bytesused);

/**
 * @brief Unpacks the codec's CAPTURE planes into a decoded payload.
 *
 * The inverse of @ref v4l2PackSemiPlanar.  @p dst must already be
 * allocated with @p fmt's PixelFormat at the decoded resolution.
 *
 * @param src The CAPTURE planes from @ref V4l2M2mCodec::dequeueRawFrame.
 * @param fmt The descriptor for the negotiated CAPTURE FourCC.
 * @param dst The payload to fill (its @c desc() supplies the geometry).
 */
void v4l2UnpackSemiPlanar(const List<V4l2M2mCodec::CapturePlane> &src, const V4l2RawFormat &fmt,
                          UncompressedVideoPayload *dst);

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
