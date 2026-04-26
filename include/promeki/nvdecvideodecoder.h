/**
 * @file      nvdecvideodecoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>
#include <promeki/videodecoder.h>
#include <promeki/uniqueptr.h>

#if PROMEKI_ENABLE_NVDEC

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Hardware H.264 / HEVC video decoder backed by NVIDIA NVDEC.
 * @ingroup proav
 *
 * Registered under the typed @c "Nvidia" backend for
 * @ref VideoCodec::H264 and @ref VideoCodec::HEVC so the generic
 * @ref VideoCodec::createDecoder factory returns an
 * @ref NvdecVideoDecoder when the build has NVDEC enabled.  The
 * decoder presentation surface is always NV12 in 8-bit (10-bit
 * P010 support is a follow-up); callers that want a different
 * uncompressed target should run the decoder's output through an
 * @ref MediaIOTask_CSC.
 *
 * @par Runtime requirements
 * Like @ref NvencVideoEncoder, this backend is lazily bound.  The
 * first call to @c submitPacket (or @c configure) dlopens
 * @c libnvcuvid.so.1, retains the CUDA primary context on the
 * current device, and creates a @c CUvideoparser.  The decoder
 * itself is created on the first sequence callback once the
 * parser has identified the bitstream's resolution and chroma
 * format.
 *
 * @par First-cut limitations
 *
 * - Annex-B byte stream only (what NVENC emits; what
 *   @ref MediaIOTask_RawBitstream writes).  AVCC length-prefixed
 *   input can be added later via a small adapter.
 * - NV12 output (8-bit, 4:2:0 semi-planar, Rec.709 assumed).
 * - System-memory output: each decoded frame is copied off the
 *   GPU to host memory so downstream stages that don't know about
 *   CUDA (SDL, ImageFile writers) can display / consume it as a
 *   regular @ref Image.  A zero-copy device-memory output path is
 *   a follow-up.
 *
 * @par Example
 * @code
 * auto res = VideoCodec(VideoCodec::H264).createDecoder();
 * VideoDecoder *dec = value(res);
 * dec->configure(MediaConfig());
 * dec->submitPacket(*pkt);
 * while(Image img = dec->receiveFrame()) { sink.render(img); }
 * dec->flush();
 * while(Image img = dec->receiveFrame()) { sink.render(img); }
 * delete dec;
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VideoDecoder.
 */
class NvdecVideoDecoder : public VideoDecoder {
        public:
                /** @brief Which codec this instance accepts on input. */
                enum Codec {
                        Codec_H264,   ///< H.264 / AVC.
                        Codec_HEVC    ///< H.265 / HEVC.
                };

                /**
                 * @brief Constructs a decoder for the given codec.
                 * @param codec Which codec the parser expects.
                 */
                explicit NvdecVideoDecoder(Codec codec);

                /** @brief Destructor — tears down the NVDEC session if one was opened. */
                ~NvdecVideoDecoder() override;

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
                class Impl;
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr _impl;
                Codec _codec;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NVDEC
