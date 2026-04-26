/**
 * @file      jpegxsvideocodec.h
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
 * @brief JPEG XS @ref VideoEncoder built on SVT-JPEG-XS.
 * @ingroup proav
 *
 * Encodes and decodes JPEG XS (ISO/IEC 21122), a modern low-complexity
 * intra-only codec designed for visually-lossless contribution and
 * transport.  JPEG XS targets the broadcast / ST 2110 / RFC 9134 RTP
 * workflow: sub-frame latency, constant bitrate, and native support
 * for 10- and 12-bit YCbCr.
 *
 * Supported input formats:
 * - @c YUV8_422_Planar_Rec709, @c YUV10_422_Planar_LE_Rec709,
 *   @c YUV12_422_Planar_LE_Rec709 (Rec.709 limited range)
 * - @c YUV8_420_Planar_Rec709, @c YUV10_420_Planar_LE_Rec709,
 *   @c YUV12_420_Planar_LE_Rec709 (Rec.709 limited range)
 * - @c RGB8_Planar_sRGB (sRGB full range)
 *
 * JPEG XS is constant-bitrate rather than quality-targeted: configure
 * the encoder with a bits-per-pixel budget via
 * @ref MediaConfig::JpegXsBpp.  Typical visually-lossless broadcast
 * values are 2-6 bpp; the codec default is 3 bpp (≈ 8:1 compression
 * for 8-bit 4:2:2).
 *
 * Registered against @ref VideoCodec::JPEG_XS.  Every emitted
 * payload is flagged @ref MediaPayload::Keyframe because every JPEG XS
 * access unit is independently decodable.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::JpegXsBpp           | int         | 3       | Bits-per-pixel target. |
 * | @ref MediaConfig::JpegXsDecomposition | int         | 5       | Horizontal decomposition depth (0-5). |
 * | @ref MediaConfig::OutputPixelFormat   | PixelFormat | Invalid | Optional override of the encoder's reported @ref outputPixelFormat. |
 * | @ref MediaConfig::Capacity            | int         | 8       | Output FIFO depth before a one-shot warning is logged. |
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VideoEncoder.
 */
class JpegXsVideoEncoder : public VideoEncoder {
        public:
                /// @brief Default bits-per-pixel target.  JPEG XS uses
                /// constant-bitrate coding; 3 bpp is visually lossless
                /// for most 8-bit 4:2:2 content.
                static constexpr int DefaultBpp = 3;

                /// @brief Default horizontal decomposition depth passed
                /// to SVT-JPEG-XS.  Matches the library default and is
                /// a good quality / speed tradeoff.
                static constexpr int DefaultDecomposition = 5;

                JpegXsVideoEncoder();
                ~JpegXsVideoEncoder() override;

                /**
                 * @brief Static view of the encoder's accepted uncompressed input list.
                 *
                 * Exposed for the @ref VideoCodec backend registry so
                 * the planner can query supported inputs without
                 * allocating an encoder instance.
                 */
                static List<int> supportedInputList();

                void configure(const MediaConfig &config) override;
                Error submitPayload(const UncompressedVideoPayload::Ptr &payload) override;
                CompressedVideoPayload::Ptr receiveCompressedPayload() override;
                Error flush() override;
                Error reset() override;

                /// @brief Returns the target bits-per-pixel budget.
                int bpp() const { return _bpp; }

                /// @brief Returns the horizontal decomposition depth (0-5).
                int decomposition() const { return _decomposition; }

        private:
                /// @brief pImpl wrapping the persistent SVT-JPEG-XS
                ///        encoder context.  Defined in the .cpp; only
                ///        the forward declaration is exposed here so
                ///        consumers don't pull in @c \<SvtJpegxsEnc.h\>.
                ///        All encode logic lives as member functions of
                ///        Impl so this type can stay private.
                struct Impl;
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr                      _impl;

                int                          _bpp           = DefaultBpp;
                int                          _decomposition = DefaultDecomposition;
                PixelFormat                  _outputPd;
                int                          _capacity      = 8;
                Deque<CompressedVideoPayload::Ptr> _queue;
                bool                         _capacityWarned = false;
};

/**
 * @brief JPEG XS @ref VideoDecoder built on SVT-JPEG-XS.
 * @ingroup proav
 *
 * Symmetric counterpart to @ref JpegXsVideoEncoder.  Registered
 * against @ref VideoCodec::JPEG_XS.  The decoder's output
 * @ref PixelFormat is set via @ref MediaConfig::OutputPixelFormat;
 * when unset, the natural planar target matching the incoming
 * bitstream (bit depth × subsampling) is produced.
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VideoDecoder.
 */
class JpegXsVideoDecoder : public VideoDecoder {
        public:
                JpegXsVideoDecoder();
                ~JpegXsVideoDecoder() override;

                /**
                 * @brief Static view of the decoder's emitted uncompressed output list.
                 *
                 * Exposed for the @ref VideoCodec backend registry.
                 */
                static List<int> supportedOutputList();

                void configure(const MediaConfig &config) override;
                Error submitPayload(const CompressedVideoPayload::Ptr &payload) override;
                UncompressedVideoPayload::Ptr receiveVideoPayload() override;
                Error flush() override;
                Error reset() override;

        private:
                /// @brief pImpl wrapping the persistent SVT-JPEG-XS
                ///        decoder context.  Defined in the .cpp so the
                ///        SVT-JPEG-XS library headers don't leak.
                struct Impl;
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr                _impl;

                PixelFormat            _outputPd;
                int                    _capacity = 8;
                Deque<UncompressedVideoPayload::Ptr> _queue;
                bool                   _capacityWarned = false;
};

PROMEKI_NAMESPACE_END
