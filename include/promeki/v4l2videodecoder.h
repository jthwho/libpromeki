/**
 * @file      v4l2videodecoder.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_V4L2
#include <promeki/namespace.h>
#include <promeki/videodecoder.h>
#include <promeki/videocodec.h>
#include <promeki/uniqueptr.h>
#include <promeki/list.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;
class Frame;

/**
 * @brief Hardware H.264 / HEVC decoder backend over a V4L2 mem2mem codec.
 * @ingroup proav
 *
 * The inverse of @ref V4l2VideoEncoder: compressed access units are fed
 * to the codec's OUTPUT queue and decoded NV12 frames are pulled from
 * the CAPTURE queue, through the shared @ref V4l2M2mCodec engine.
 * Registered against @ref VideoCodec::H264 and @ref VideoCodec::HEVC
 * under the backend name @c "V4L2", it drives any kernel V4L2 stateful
 * mem2mem decoder — the Xilinx VCU (@c al5d), the Raspberry Pi
 * (@c bcm2835-codec), and the kernel @c vicodec test driver.
 *
 * @par Stateful decoder bring-up
 * Unlike the encoder, a stateful decoder does not know its output
 * geometry until the codec has parsed the bitstream headers.  The
 * session therefore comes up in stages: coded data is fed on the OUTPUT
 * queue, the driver raises a @c V4L2_EVENT_SOURCE_CHANGE once it knows
 * the resolution, and only then is the CAPTURE (raw) queue configured
 * and streamed.  The @ref V4l2M2mCodec engine exposes that staging; this
 * backend sequences it.
 *
 * @par Output format
 * 8-bit 4:2:0 NV12 (@ref PixelFormat::YUV8_420_SemiPlanar_Rec709),
 * pinned on the CAPTURE queue when the driver supports it.
 *
 * @par Memory
 * MMAP only for now — decoded frames are copied out of the CAPTURE
 * queue into host payloads.  DMABUF export (zero-copy) is a planned
 * second phase.
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VideoDecoder.
 */
class V4l2VideoDecoder : public VideoDecoder {
        public:
                /**
                 * @brief Constructs a decoder for a specific codec family.
                 * @param codecId @ref VideoCodec::H264 or @ref VideoCodec::HEVC.
                 */
                explicit V4l2VideoDecoder(VideoCodec::ID codecId);
                ~V4l2VideoDecoder() override;

                /**
                 * @brief Uncompressed @ref PixelFormat IDs this backend emits.
                 *
                 * Single-entry: NV12.
                 */
                static List<int> supportedOutputList();

                Error submitFrame(const Frame &frame) override;
                Frame receiveFrame() override;
                Error flush() override;
                Error reset() override;

        protected:
                void onConfigure(const MediaConfig &config) override;

        private:
                struct Impl;
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr _impl;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
