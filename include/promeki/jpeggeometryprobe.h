/**
 * @file      jpeggeometryprobe.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/buffer.h>
#include <promeki/imagedesc.h>
#include <promeki/namespace.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief JFIF / RFC 2435 geometry discovery for the RTP video reader.
 * @ingroup proav
 *
 * RFC 2435 ships MJPEG over RTP without a width / height in the SDP
 * (the wire format puts geometry in the per-packet RTP/JPEG payload
 * header, capped at 2040 px each axis, with a SOF0 / SOF2 marker
 * inside the reassembled JFIF that gives the unrounded dimensions
 * for >2040-px rasters).  The reader therefore has to parse the
 * first reassembled JPEG frame to learn what shape it's decoding —
 * and after that, watch for a geometry change so it can adapt
 * mid-stream without rebuilding the pipeline.
 *
 * @c JpegGeometryProbe encapsulates that logic so the RTP receive
 * path doesn't need to keep open-coded JFIF parsing in the
 * @c VideoDepacketizerThread.  It walks the reassembled JFIF for
 * SOF0 / SOF2
 * markers, combines the SOF data with the RFC 2435 Type byte (which
 * disambiguates 4:2:2 vs 4:2:0 vs RGB) and the SDP @c fmtp
 * @c colorimetry / @c RANGE attributes (which carry the YCbCr matrix
 * / range), and resolves the matching @ref PixelFormat.
 *
 * @par Caching policy
 * The probe re-parses the JFIF on every @ref probe call (so the
 * caller can run it once per frame without complicating the call
 * site) but skips the full @ref PixelFormat resolution when the
 * SOF dimensions, the SOF subsampling-derived signals, the RFC 2435
 * Type byte, and the @c fmtp string all match the cached values.
 * The returned reference points at the cached @ref Result, so a
 * caller that takes the value once and stores it in
 * @c VideoReaderStream::readerImageDesc only needs to re-stamp on
 * an update.
 *
 * @par Thread safety
 * Plain value type with mutable cache state; not internally
 * synchronized.  One probe instance per stream (the caller owns
 * affinity).
 */
class JpegGeometryProbe {
        public:
                /**
                 * @brief SOF data extracted from a JFIF marker walk.
                 *
                 * Public so unit tests can drive the marker-walk in
                 * isolation; production callers go through
                 * @ref probe.
                 */
                struct SofData {
                                /// @brief Raster width from the SOF marker (0 = no SOF found).
                                uint32_t width = 0;
                                /// @brief Raster height from the SOF marker (0 = no SOF found).
                                uint32_t height = 0;
                                /// @brief Component count from the SOF marker (1 = grayscale, 3 = YCbCr / RGB).
                                int nf = 0;
                                /// @brief Sampling factor byte for the first
                                ///        component (Hi << 4 | Vi).
                                uint8_t ySf = 0;

                                /// @brief Returns @c true when @ref width and
                                ///        @ref height are both non-zero.
                                bool isValid() const { return width > 0 && height > 0; }
                };

                /**
                 * @brief Resolved geometry for a probed JPEG frame.
                 *
                 * @c valid is @c false until the probe finds a SOF
                 * marker and resolves a @ref PixelFormat.
                 */
                struct Result {
                                /// @brief @c true once a probe call has
                                ///        successfully resolved geometry.
                                bool valid = false;
                                /// @brief Probed raster size.
                                Size2Du32 size;
                                /// @brief Resolved pixel format
                                ///        (@ref PixelFormat::JPEG_*
                                ///        family).
                                PixelFormat pixelFormat;
                                /// @brief @c true when the probed JPEG is
                                ///        4:2:0 (YCbCr) — informational; the
                                ///        @ref pixelFormat already encodes
                                ///        this.
                                bool is420 = false;
                                /// @brief @c true when the probed JPEG is
                                ///        RGB (RFC 2435 Type ≥ 2 or SOF
                                ///        component-count == 3 with no
                                ///        chroma subsampling).
                                bool isRgb = false;

                                /// @brief Returns the resolved @ref ImageDesc.
                                ///        Invalid when @ref valid is @c false.
                                ImageDesc imageDesc() const {
                                        return valid ? ImageDesc(size, pixelFormat) : ImageDesc();
                                }
                };

                JpegGeometryProbe() = default;

                /**
                 * @brief Walks a JFIF byte stream for the first SOF0 or
                 *        SOF2 marker and returns the extracted geometry.
                 *
                 * Stateless — does not consult or update @ref probe's
                 * cache.  Public so the marker-walk is unit-testable
                 * in isolation.
                 *
                 * @param data Pointer to the JFIF byte stream.
                 * @param size Number of bytes at @p data.
                 * @return Extracted SOF data.  @c isValid() == @c false
                 *         when no SOF marker was found.
                 */
                static SofData parseSof(const uint8_t *data, size_t size);

                /**
                 * @brief Resets the cache so the next @ref probe call
                 *        re-resolves the @ref PixelFormat from scratch.
                 *
                 * Called on SSRC reset by the depacketizer.
                 */
                void reset();

                /**
                 * @brief Returns @c true once a probe call has produced
                 *        a valid @ref Result.
                 */
                bool hasGeometry() const { return _last.valid; }

                /**
                 * @brief Returns the most recently cached @ref Result.
                 *        @c valid is @c false before any successful
                 *        probe.
                 */
                const Result &lastResult() const { return _last; }

                /**
                 * @brief Probes a reassembled JFIF buffer.
                 *
                 * Walks the buffer for a SOF marker, combines its
                 * dimensions / sampling factor with the RFC 2435
                 * @p rfc2435Type byte and the SDP @p fmtp attribute
                 * to resolve the matching @ref PixelFormat, and
                 * caches the result.  When the inputs match the
                 * cached result (geometry + Type byte + fmtp all
                 * unchanged) the cached result is returned without
                 * re-resolving the @ref PixelFormat.
                 *
                 * @param reassembled  Reassembled JFIF bytes.  Never
                 *                     read past the @c size of the
                 *                     buffer.
                 * @param rfc2435Type  Type field from the first RTP
                 *                     packet's RFC 2435 payload header
                 *                     (byte 4 of the 8-byte header).
                 *                     0 → 4:2:2, 1 → 4:2:0, ≥ 2 → RGB.
                 * @param fmtp         SDP @c a=fmtp value (without the
                 *                     leading PT) — typically of the
                 *                     form
                 *                     @c "colorimetry=BT709-2;RANGE=NARROW".
                 *                     Empty falls back to JFIF
                 *                     defaults (BT.601, full range).
                 * @return Cached @ref Result reference, valid for the
                 *         lifetime of this @c JpegGeometryProbe.
                 */
                const Result &probe(const Buffer &reassembled, uint8_t rfc2435Type,
                                    const String &fmtp);

        private:
                Result _last;
                // Cache keys — used to skip the full PixelFormat
                // resolution when nothing the resolution depends on
                // has changed since the last probe call.
                uint32_t _cachedWidth = 0;
                uint32_t _cachedHeight = 0;
                int _cachedNf = 0;
                uint8_t _cachedYsf = 0;
                uint8_t _cachedRfc2435Type = 0xFFu;
                String _cachedFmtp;
                bool _hasCacheKey = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
