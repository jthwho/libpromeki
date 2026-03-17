/**
 * @file      proav/jpegencodernode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/variant.h>
#include <promeki/core/map.h>
#include <promeki/proav/medianode.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Compresses video frames to JPEG using libjpeg-turbo.
 * @ingroup proav_pipeline
 *
 * Processing node with one Image input and one Image output. The
 * input image uses an uncompressed pixel format (e.g. RGB8, RGBA8)
 * and the output image uses the corresponding JPEG pixel format
 * (e.g. JPEG_RGB8). The compressed JPEG data is stored in the
 * output Image's plane buffer, with Metadata::CompressedSize set
 * to the actual encoded byte count.
 *
 * @par Example
 * @code
 * JpegEncoderNode *enc = new JpegEncoderNode();
 * enc->setQuality(90);
 * @endcode
 */
class JpegEncoderNode : public MediaNode {
        PROMEKI_OBJECT(JpegEncoderNode, MediaNode)
        public:
                /**
                 * @brief Constructs a JpegEncoderNode.
                 * @param parent Optional parent object.
                 */
                JpegEncoderNode(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                virtual ~JpegEncoderNode() = default;

                /**
                 * @brief Sets the JPEG compression quality.
                 * @param quality Quality level 1-100 (default: 85).
                 */
                void setQuality(int quality) { _quality = quality; return; }

                /**
                 * @brief Returns the JPEG compression quality.
                 * @return The quality level 1-100.
                 */
                int quality() const { return _quality; }

                // ---- Lifecycle overrides ----

                /**
                 * @brief Validates input format and configures the node.
                 *
                 * Transitions to Configured on success. Fails if the node
                 * is not in Idle state.
                 *
                 * @return Error::Ok on success, or Error::Invalid.
                 */
                Error configure() override;

                /**
                 * @brief Compresses an input image to JPEG.
                 *
                 * Dequeues a Frame from the input port, compresses the first
                 * image via libjpeg-turbo, wraps the result in an Image with
                 * a JPEG pixel format, and delivers it on the output port.
                 */
                void process() override;

                /**
                 * @brief Returns node-specific statistics.
                 * @return A map containing framesEncoded, avgCompressedSize, and compressionRatio.
                 */
                Map<String, Variant> extendedStats() const override;

        private:
                int             _quality = 85;
                uint64_t        _framesEncoded = 0;
                uint64_t        _totalCompressedBytes = 0;
                uint64_t        _totalUncompressedBytes = 0;
};

PROMEKI_NAMESPACE_END
