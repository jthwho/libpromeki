/**
 * @file      jpegimagecodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/codec.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief JPEG image codec using libjpeg-turbo.
 * @ingroup proav
 *
 * Encodes uncompressed images (RGB8, RGBA8) to JPEG compressed format.
 * Decoding support is not yet implemented.
 *
 * Configuration (quality, subsampling) is set once after construction
 * and applies to all subsequent encode() calls.
 *
 * Default subsampling is 4:2:2 for RFC 2435 RTP compatibility.
 *
 * This class is not thread-safe. Each thread should use its own instance.
 *
 * @par Example
 * @code
 * JpegImageCodec codec;
 * codec.setQuality(90);
 * codec.setSubsampling(JpegImageCodec::Subsampling422);
 * Image compressed = codec.encode(sourceImage);
 * @endcode
 */
class JpegImageCodec : public ImageCodec {
        public:
                /** @brief Chroma subsampling mode for JPEG encoding. */
                enum Subsampling {
                        Subsampling444,   ///< @brief 4:4:4 (no chroma subsampling).
                        Subsampling422,   ///< @brief 4:2:2 (default, RFC 2435 compatible).
                        Subsampling420    ///< @brief 4:2:0.
                };

                /** @brief Constructs a JpegImageCodec with default settings (quality 85, 4:2:2). */
                JpegImageCodec() = default;

                /** @brief Destructor. */
                ~JpegImageCodec() override;

                String name() const override;
                String description() const override;
                bool canEncode() const override;
                bool canDecode() const override;

                /**
                 * @brief Encodes an uncompressed image to JPEG.
                 * @param input Source image (RGB8 or RGBA8).
                 * @return Compressed image, or an invalid Image on failure.
                 *
                 * @par Example
                 * @code
                 * JpegImageCodec codec;
                 * Image jpeg = codec.encode(rawImage);
                 * if(!jpeg.isValid()) {
                 *         qWarning() << codec.lastErrorMessage();
                 * }
                 * @endcode
                 */
                Image encode(const Image &input) override;

                /**
                 * @brief Decode is not yet implemented — always returns an invalid Image.
                 * @param input Compressed image (unused).
                 * @param outputFormat Desired pixel format (unused).
                 * @return An invalid Image; lastError() will be Error::NotImplemented.
                 */
                Image decode(const Image &input, int outputFormat = 0) override;

                /** @brief Returns the JPEG quality (1-100). */
                int quality() const { return _quality; }

                /**
                 * @brief Sets the JPEG quality.
                 * @param quality Quality value 1-100. Clamped if out of range.
                 *
                 * @par Example
                 * @code
                 * codec.setQuality(95);  // high quality, larger file
                 * codec.setQuality(40);  // aggressive compression
                 * @endcode
                 */
                void setQuality(int quality);

                /** @brief Returns the chroma subsampling mode. */
                Subsampling subsampling() const { return _subsampling; }

                /**
                 * @brief Sets the chroma subsampling mode.
                 * @param subsampling The desired subsampling.
                 *
                 * @par Example
                 * @code
                 * // RFC 2435 compatible (default)
                 * codec.setSubsampling(JpegImageCodec::Subsampling422);
                 * // Full chroma (larger file, better quality)
                 * codec.setSubsampling(JpegImageCodec::Subsampling444);
                 * @endcode
                 */
                void setSubsampling(Subsampling subsampling) { _subsampling = subsampling; }

        private:
                int             _quality = 85;
                Subsampling     _subsampling = Subsampling422;
};

PROMEKI_NAMESPACE_END
