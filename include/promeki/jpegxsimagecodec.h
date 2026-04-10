/**
 * @file      jpegxsimagecodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/codec.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief JPEG XS image codec using SVT-JPEG-XS.
 * @ingroup proav
 *
 * Encodes and decodes JPEG XS (ISO/IEC 21122), a modern low-complexity
 * intra-only codec designed for visually-lossless contribution and
 * transport.  JPEG XS targets the broadcast / ST 2110 / RFC 9134 RTP
 * workflow: sub-frame latency, constant bitrate, and native support
 * for 10- and 12-bit YCbCr.
 *
 * Supported input formats (all Rec.709 limited range):
 * - @c YUV8_422_Planar_Rec709, @c YUV10_422_Planar_LE_Rec709,
 *   @c YUV12_422_Planar_LE_Rec709
 * - @c YUV8_420_Planar_Rec709, @c YUV10_420_Planar_LE_Rec709,
 *   @c YUV12_420_Planar_LE_Rec709
 *
 * Decoder targets are the matching uncompressed formats.  The
 * @c JPEG_XS_RGB8_sRGB PixelDesc entry exists so the library can
 * describe RGB JPEG XS streams, but the packed-RGB input path is
 * not implemented in this codec yet — use a planar YUV encode for
 * now.
 *
 * JPEG XS is constant-bitrate rather than quality-targeted: configure
 * the encoder with a bits-per-pixel budget via @c setBpp (or
 * @ref MediaConfig::JpegXsBpp).  Typical visually-lossless broadcast
 * values are 2-6 bpp; the codec default is 3 bpp (≈ 8:1 compression
 * for 8-bit 4:2:2).
 *
 * Configuration (bpp, decomposition depth) is set once after
 * construction and applies to all subsequent encode() calls.  Changing
 * parameters between calls is legal; the internal SVT encoder is
 * re-initialised lazily on the next encode.
 *
 * This class is not thread-safe.  Each thread should use its own
 * instance.  The underlying SVT encoder is multi-threaded internally,
 * so a single instance per producer thread is already parallel.
 *
 * @par Example
 * @code
 * JpegXsImageCodec codec;
 * codec.setBpp(4);                 // 4 bits/pixel constant bitrate
 * Image compressed = codec.encode(sourceImage);
 * @endcode
 */
class JpegXsImageCodec : public ImageCodec {
        public:
                /// @brief Default bits-per-pixel target.  JPEG XS uses
                /// constant-bitrate coding; 3 bpp is visually lossless
                /// for most 8-bit 4:2:2 content.
                static constexpr int DefaultBpp = 3;

                /// @brief Default horizontal decomposition depth passed
                /// to SVT-JPEG-XS.  Matches the library default and is
                /// a good quality / speed tradeoff.
                static constexpr int DefaultDecomposition = 5;

                /// @brief Constructs with default settings (3 bpp, decomposition 5).
                JpegXsImageCodec() = default;

                /// @brief Destructor.
                ~JpegXsImageCodec() override;

                /** @copydoc ImageCodec::name() */
                String name() const override;
                /** @copydoc ImageCodec::description() */
                String description() const override;
                /** @copydoc ImageCodec::canEncode() */
                bool canEncode() const override;
                /** @copydoc ImageCodec::canDecode() */
                bool canDecode() const override;

                /**
                 * @brief Reads JPEG XS-specific knobs from @p config.
                 *
                 * Honored keys:
                 * - @ref MediaConfig::JpegXsBpp (int or float) — sets
                 *   @ref setBpp.  Values <= 0 are ignored.
                 * - @ref MediaConfig::JpegXsDecomposition (int) — sets
                 *   @ref setDecomposition.  Clamped to 0-5.
                 *
                 * Missing or malformed keys leave the corresponding
                 * setting at its current value.
                 */
                void configure(const MediaConfig &config) override;

                /**
                 * @brief Encodes an uncompressed image to JPEG XS.
                 * @param input Source image.  See class docs for the
                 *              accepted PixelDesc IDs.
                 * @return Compressed image, or an invalid Image on failure.
                 *
                 * @par Example
                 * @code
                 * JpegXsImageCodec codec;
                 * Image jxs = codec.encode(rawImage);
                 * if(!jxs.isValid()) promekiErr("%s", codec.lastErrorMessage().cstr());
                 * @endcode
                 */
                Image encode(const Image &input) override;

                /**
                 * @brief Decodes a JPEG XS image to an uncompressed format.
                 * @param input        Compressed JPEG XS image.
                 * @param outputFormat Target PixelDesc::ID (0 for codec default
                 *                     — the first entry in the compressed
                 *                     descriptor's @c decodeTargets list).
                 * @return Decoded image, or an invalid Image on failure.
                 */
                Image decode(const Image &input, int outputFormat = 0) override;

                /// @brief Returns the target bits-per-pixel budget.
                int bpp() const { return _bpp; }

                /**
                 * @brief Sets the target bits-per-pixel budget.
                 * @param bpp Target bits per pixel.  Typical visually-
                 *            lossless values are 2-6.  Values <= 0 are
                 *            silently coerced to the default.
                 */
                void setBpp(int bpp);

                /// @brief Returns the horizontal decomposition depth (0-5).
                int decomposition() const { return _decomposition; }

                /**
                 * @brief Sets the horizontal decomposition depth.
                 * @param depth Value 0-5 (clamped).  Higher values give
                 *              better quality at a modest encode cost.
                 */
                void setDecomposition(int depth);

        private:
                int _bpp           = DefaultBpp;
                int _decomposition = DefaultDecomposition;
};

PROMEKI_NAMESPACE_END
