/**
 * @file      nvencvideoencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>
#include <promeki/codec.h>

#if PROMEKI_ENABLE_NVENC

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Hardware H.264 / HEVC / AV1 video encoder backed by NVIDIA NVENC.
 * @ingroup proav
 *
 * Registered under the codec names @c "H264", @c "HEVC", and @c "AV1", so
 * callers ask for this backend through the generic
 * @ref VideoEncoder::createEncoder factory rather than constructing
 * it directly.  The constructor picks which codec the instance emits
 * via its @ref Codec parameter, and the class maps
 * @ref MediaConfig rate-control knobs onto the matching NVENC
 * parameters during @ref configure.
 *
 * Compiled only when @c PROMEKI_ENABLE_NVENC is defined.  When the
 * build does not have NVENC, @c VideoEncoder::createEncoder("H264")
 * still works if any other H.264 backend is registered; the CPU
 * fallback path is a separate concern.
 *
 * @par Runtime requirements
 * The constructor does not touch the GPU — it is safe to instantiate
 * in any process.  The first call to @ref configure or
 * @ref submitFrame loads @c libnvidia-encode.so.1 at runtime and
 * opens an encoder session against the currently-selected CUDA
 * device; if the runtime is missing, the error is reported through
 * @ref lastError / @ref lastErrorMessage and the session remains
 * uninitialised.
 *
 * @par Supported input formats (v1)
 * Only @ref PixelDesc::YUV8_420_SemiPlanar_Rec709 (NV12) is accepted
 * for the first cut.  Callers with RGB or other YCbCr sources should
 * convert first via @ref Image::convert.  Additional formats (NV12
 * BT.601, 10-bit P010, device-memory buffers for zero-copy) will be
 * added in later iterations.
 *
 * @par Example
 * @code
 * VideoEncoder *enc = VideoEncoder::createEncoder("H264");
 * MediaConfig cfg;
 * cfg.set(MediaConfig::BitrateKbps, 8000);
 * cfg.set(MediaConfig::GopLength,   60);
 * cfg.set(MediaConfig::VideoPreset, VideoEncoderPreset::LowLatency);
 * enc->configure(cfg);
 *
 * for(const Image &nv12 : source) {
 *         enc->submitFrame(nv12);
 *         while(auto pkt = enc->receivePacket()) { sink.write(pkt); }
 * }
 * enc->flush();
 * while(auto pkt = enc->receivePacket()) { sink.write(pkt); }
 * delete enc;
 * @endcode
 */
class NvencVideoEncoder : public VideoEncoder {
        public:
                /** @brief Which codec this instance emits. */
                enum Codec {
                        Codec_H264,   ///< H.264 / AVC (@c NV_ENC_CODEC_H264_GUID).
                        Codec_HEVC,   ///< H.265 / HEVC (@c NV_ENC_CODEC_HEVC_GUID).
                        Codec_AV1     ///< AV1 (@c NV_ENC_CODEC_AV1_GUID).
                };

                /**
                 * @brief Constructs an encoder for the given codec.
                 * @param codec Which codec the instance emits.
                 */
                explicit NvencVideoEncoder(Codec codec);

                /** @brief Destructor — tears down the NVENC session if one was opened. */
                ~NvencVideoEncoder() override;

                String name() const override;
                String description() const override;
                PixelDesc outputPixelDesc() const override;
                List<int> supportedInputs() const override;

                /**
                 * @brief Static view of the encoder's @ref supportedInputs list.
                 *
                 * Exposed for the @ref VideoCodec registry so planners
                 * can query supported inputs without instantiating an
                 * encoder session.
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
                class Impl;
                Impl *_impl;
                Codec _codec;
                bool  _requestKey = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NVENC
