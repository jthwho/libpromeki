/**
 * @file      jpegvideocodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/codec.h>
#include <promeki/jpegimagecodec.h>
#include <deque>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief JPEG @ref VideoEncoder built on @ref JpegImageCodec.
 * @ingroup proav
 *
 * Adapts the existing intra-only @ref JpegImageCodec into the
 * stateful push-frame / pull-packet @ref VideoEncoder contract that
 * the unified codec subsystem expects.  Each call to
 * @ref submitFrame runs one JPEG encode through the underlying
 * @ref JpegImageCodec instance and queues the resulting compressed
 * bytes as a @ref MediaPacket; @ref receivePacket pops the head of
 * that queue.
 *
 * Holding an instance of @ref JpegImageCodec across calls is what
 * lets future revisions of @c JpegImageCodec reuse the libjpeg
 * @c jpeg_compress_struct between frames — the wrapper itself is
 * stateless beyond the output FIFO.
 *
 * Registered against @ref VideoCodec::JPEG.  Every emitted packet
 * is flagged @ref MediaPacket::Keyframe because every JPEG bitstream
 * is independently decodable.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::JpegQuality      | int                  | 85      | Forwarded to @ref JpegImageCodec. |
 * | @ref MediaConfig::JpegSubsampling  | Enum @ref ChromaSubsampling | YUV422 | Forwarded to @ref JpegImageCodec. |
 * | @ref MediaConfig::OutputPixelDesc  | PixelDesc | Invalid | Optional override of the encoder's reported @ref outputPixelDesc. |
 * | @ref MediaConfig::Capacity         | int       | 8       | Output FIFO depth before a one-shot warning is logged. |
 */
class JpegVideoEncoder : public VideoEncoder {
        public:
                JpegVideoEncoder() = default;
                ~JpegVideoEncoder() override;

                String name() const override;
                String description() const override;
                PixelDesc outputPixelDesc() const override;
                List<int> supportedInputs() const override;

                /**
                 * @brief Static view of the encoder's @ref supportedInputs list.
                 *
                 * Returned verbatim by the virtual @ref supportedInputs
                 * override.  Exposed statically so the
                 * @ref VideoCodec registry can populate
                 * @ref VideoCodec::Data::encoderSupportedInputs without
                 * allocating an encoder instance.
                 */
                static List<int> supportedInputList();

                void configure(const MediaConfig &config) override;
                Error submitFrame(const Image &frame,
                                  const MediaTimeStamp &pts = MediaTimeStamp()) override;
                MediaPacket::Ptr receivePacket() override;
                Error flush() override;
                Error reset() override;
                void requestKeyframe() override;

        private:
                JpegImageCodec               _codec;
                PixelDesc                    _outputPd;
                int                          _capacity = 8;
                std::deque<MediaPacket::Ptr> _queue;
                bool                         _capacityWarned = false;
};

/**
 * @brief JPEG @ref VideoDecoder built on @ref JpegImageCodec.
 * @ingroup proav
 *
 * Symmetric counterpart to @ref JpegVideoEncoder — incoming
 * @ref MediaPacket bytes are wrapped as a compressed @ref Image and
 * handed to @ref JpegImageCodec::decode, with the resulting
 * uncompressed Image queued for @ref receiveFrame.  The decoder's
 * target uncompressed pixel description is set via
 * @ref MediaConfig::OutputPixelDesc; when unset, the codec's first
 * declared decode target is used.
 *
 * Registered against @ref VideoCodec::JPEG.
 */
class JpegVideoDecoder : public VideoDecoder {
        public:
                JpegVideoDecoder() = default;
                ~JpegVideoDecoder() override;

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
                JpegImageCodec      _codec;
                PixelDesc           _outputPd;
                int                 _capacity = 8;
                std::deque<Image>   _queue;
                bool                _capacityWarned = false;
};

PROMEKI_NAMESPACE_END
