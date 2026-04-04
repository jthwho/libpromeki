/**
 * @file      jpegencodernode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/variant.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/medianode.h>

PROMEKI_NAMESPACE_BEGIN

class JpegImageCodec;

/**
 * @brief Compresses video frames to JPEG using JpegImageCodec.
 * @ingroup pipeline
 *
 * Processing node with one Image input and one Image output. The
 * input image uses an uncompressed pixel format (e.g. RGB8, RGBA8)
 * and the output image uses the corresponding JPEG pixel format
 * (e.g. JPEG_RGB8). The compressed JPEG data is stored in the
 * output Image's plane buffer, with Metadata::CompressedSize set
 * to the actual encoded byte count.
 *
 * JPEG encoding is delegated to JpegImageCodec.
 *
 * @par Config options
 * - `Quality` (int): JPEG quality 1-100 (default: 85).
 * - `Subsampling` (String): Chroma subsampling: "422" (default), "444", "420".
 *
 * @par Example
 * @code
 * MediaNodeConfig cfg("JpegEncoderNode", "encoder");
 * cfg.set("Quality", 90);
 * cfg.set("Subsampling", "422");
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
                virtual ~JpegEncoderNode();

                MediaNodeConfig defaultConfig() const override;
                BuildResult build(const MediaNodeConfig &config) override;

                /**
                 * @brief Returns node-specific statistics.
                 * @return A map containing FramesEncoded, AvgCompressedSize, and CompressionRatio.
                 */
                Map<String, Variant> extendedStats() const override;

        protected:
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override;

        private:
                JpegImageCodec  *_codec = nullptr;
                uint64_t        _framesEncoded = 0;
                uint64_t        _totalCompressedBytes = 0;
                uint64_t        _totalUncompressedBytes = 0;

                // Thread-safe stats snapshot
                mutable Mutex   _statsMutex;
                uint64_t        _statsFramesEncoded = 0;
                uint64_t        _statsTotalCompressedBytes = 0;
                uint64_t        _statsTotalUncompressedBytes = 0;
};

PROMEKI_NAMESPACE_END
