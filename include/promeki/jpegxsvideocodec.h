/**
 * @file      jpegxsvideocodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/codec.h>
#include <promeki/jpegxsimagecodec.h>
#include <deque>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief JPEG XS @ref VideoEncoder built on @ref JpegXsImageCodec.
 * @ingroup proav
 *
 * Adapts the existing intra-only @ref JpegXsImageCodec into the
 * stateful @ref VideoEncoder push-frame / pull-packet contract.
 * Holding a @ref JpegXsImageCodec instance across calls is what
 * lets future revisions reuse the SVT-JPEG-XS encoder context
 * across frames without paying the expensive setup cost on every
 * @ref submitFrame.
 *
 * Registered against @ref VideoCodec::JPEG_XS.  Every emitted
 * packet is flagged @ref MediaPacket::Keyframe because every
 * JPEG XS access unit is independently decodable.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::JpegXsBpp           | int | 3 | Forwarded to @ref JpegXsImageCodec. |
 * | @ref MediaConfig::JpegXsDecomposition | int | 5 | Forwarded to @ref JpegXsImageCodec. |
 * | @ref MediaConfig::OutputPixelDesc     | PixelDesc | Invalid | Optional override of the encoder's reported @ref outputPixelDesc. |
 * | @ref MediaConfig::Capacity            | int | 8 | Output FIFO depth before a one-shot warning is logged. |
 */
class JpegXsVideoEncoder : public VideoEncoder {
        public:
                JpegXsVideoEncoder() = default;
                ~JpegXsVideoEncoder() override;

                String name() const override;
                String description() const override;
                PixelDesc outputPixelDesc() const override;
                List<int> supportedInputs() const override;

                void configure(const MediaConfig &config) override;
                Error submitFrame(const Image &frame,
                                  const MediaTimeStamp &pts = MediaTimeStamp()) override;
                MediaPacket::Ptr receivePacket() override;
                Error flush() override;
                Error reset() override;
                void requestKeyframe() override;

        private:
                JpegXsImageCodec             _codec;
                PixelDesc                    _outputPd;
                int                          _capacity = 8;
                std::deque<MediaPacket::Ptr> _queue;
                bool                         _capacityWarned = false;
};

/**
 * @brief JPEG XS @ref VideoDecoder built on @ref JpegXsImageCodec.
 * @ingroup proav
 *
 * Symmetric counterpart to @ref JpegXsVideoEncoder.  Registered
 * against @ref VideoCodec::JPEG_XS.
 */
class JpegXsVideoDecoder : public VideoDecoder {
        public:
                JpegXsVideoDecoder() = default;
                ~JpegXsVideoDecoder() override;

                String name() const override;
                String description() const override;
                PixelDesc inputPixelDesc() const override;
                List<int> supportedOutputs() const override;

                void configure(const MediaConfig &config) override;
                Error submitPacket(const MediaPacket &packet) override;
                Image receiveFrame() override;
                Error flush() override;
                Error reset() override;

        private:
                JpegXsImageCodec    _codec;
                PixelDesc           _outputPd;
                int                 _capacity = 8;
                std::deque<Image>   _queue;
                bool                _capacityWarned = false;
};

PROMEKI_NAMESPACE_END
