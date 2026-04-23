/**
 * @file      jpegvideocodec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/pixelformat.h>
#include <promeki/deque.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief JPEG @ref VideoEncoder built on libjpeg-turbo.
 * @ingroup proav
 *
 * Encodes uncompressed images (RGB8, RGBA8, YCbCr 4:2:2 YUYV/UYVY/planar,
 * YCbCr 4:2:0 planar/NV12) to JPEG compressed form.  Each call to
 * @ref submitFrame runs one JPEG encode through libjpeg-turbo and
 * queues the resulting compressed bytes as a @ref MediaPacket;
 * @ref receivePacket pops the head of that queue.
 *
 * Default subsampling is 4:2:2 for RFC 2435 RTP compatibility.
 *
 * Registered against @ref VideoCodec::JPEG.  Every emitted packet is
 * flagged @ref MediaPacket::Keyframe because every JPEG bitstream is
 * independently decodable.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::JpegQuality       | int                         | 85      | libjpeg-turbo quality (1-100, clamped). |
 * | @ref MediaConfig::JpegSubsampling   | Enum @ref ChromaSubsampling | YUV422  | Chroma subsampling for RGB encode paths. |
 * | @ref MediaConfig::OutputPixelFormat | PixelFormat                 | Invalid | Optional override of the encoder's reported @ref outputPixelFormat. |
 * | @ref MediaConfig::Capacity          | int                         | 8       | Output FIFO depth before a one-shot warning is logged. |
 */
class JpegVideoEncoder : public VideoEncoder {
        public:
                /** @brief Chroma subsampling mode for JPEG encoding. */
                enum Subsampling {
                        Subsampling444,   ///< @brief 4:4:4 (no chroma subsampling).
                        Subsampling422,   ///< @brief 4:2:2 (default, RFC 2435 compatible).
                        Subsampling420    ///< @brief 4:2:0.
                };

                JpegVideoEncoder();
                ~JpegVideoEncoder() override;

                /**
                 * @brief Static view of the encoder's accepted uncompressed input list.
                 *
                 * Exposed for the @ref VideoCodec backend registry so
                 * the planner can query supported inputs without
                 * allocating an encoder instance.
                 */
                static List<int> supportedInputList();

                void configure(const MediaConfig &config) override;
                Error submitFrame(const Image::Ptr &frame,
                                  const MediaTimeStamp &pts = MediaTimeStamp()) override;
                VideoPacket::Ptr receivePacket() override;
                Error flush() override;
                Error reset() override;

                /** @brief Returns the JPEG quality (1-100). */
                int quality() const { return _quality; }

                /** @brief Returns the chroma subsampling mode. */
                Subsampling subsampling() const { return _subsampling; }

        private:
                struct Impl;                          ///< pImpl shielding consumers from @c \<jpeglib.h\>.
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr                      _impl;

                int                          _quality       = 85;
                Subsampling                  _subsampling   = Subsampling422;
                PixelFormat                  _outputPd;
                int                          _capacity      = 8;
                Deque<VideoPacket::Ptr>      _queue;
                bool                         _capacityWarned = false;
};

/**
 * @brief JPEG @ref VideoDecoder built on libjpeg-turbo.
 * @ingroup proav
 *
 * Symmetric counterpart to @ref JpegVideoEncoder — incoming
 * @ref MediaPacket bytes are handed to libjpeg-turbo for decode and
 * the resulting uncompressed Image is queued for @ref receiveFrame.
 * The decoder's target uncompressed pixel description is set via
 * @ref MediaConfig::OutputPixelFormat; when unset, the first
 * declared decode target for the input JPEG sub-format is used.
 *
 * Registered against @ref VideoCodec::JPEG.
 */
class JpegVideoDecoder : public VideoDecoder {
        public:
                JpegVideoDecoder();
                ~JpegVideoDecoder() override;

                /**
                 * @brief Static view of the decoder's emitted uncompressed output list.
                 *
                 * Exposed for the @ref VideoCodec backend registry.
                 */
                static List<int> supportedOutputList();

                void configure(const MediaConfig &config) override;
                Error submitPacket(const VideoPacket::Ptr &packet) override;
                Image::Ptr receiveFrame() override;
                Error flush() override;
                Error reset() override;

        private:
                struct Impl;                          ///< pImpl shielding consumers from @c \<jpeglib.h\>.
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr              _impl;

                PixelFormat            _outputPd;
                int                    _capacity = 8;
                Deque<Image::Ptr>      _queue;
                bool                   _capacityWarned = false;
};

PROMEKI_NAMESPACE_END
