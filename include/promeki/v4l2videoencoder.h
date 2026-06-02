/**
 * @file      v4l2videoencoder.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_V4L2
#include <promeki/namespace.h>
#include <promeki/videoencoder.h>
#include <promeki/videocodec.h>
#include <promeki/uniqueptr.h>
#include <promeki/list.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class MediaConfig;
class Frame;

/**
 * @brief Hardware H.264 / HEVC encoder backend over a V4L2 mem2mem codec.
 * @ingroup proav
 *
 * Registered against @ref VideoCodec::H264 and @ref VideoCodec::HEVC
 * under the backend name @c "V4L2" (so @c VideoCodec("H264:V4L2") or
 * @c MediaConfig::CodecBackend = @c "V4L2" select it).  It drives any
 * kernel V4L2 stateful memory-to-memory encoder through the common
 * @ref V4l2M2mCodec engine — the same code path serves the Xilinx VCU
 * (@c allegro), the Raspberry Pi (@c bcm2835-codec), and the kernel's
 * @c vicodec test driver, since all three share the V4L2 stateful codec
 * API.  Per-SoC differences (which controls exist, supported levels)
 * are absorbed by best-effort control programming.
 *
 * @par Backend registration
 * The backend probes @c /dev/video* at static-init time and only
 * registers a codec when a matching encoder node is present, so a host
 * with no hardware codec never advertises @c "V4L2" (and the planner
 * falls back to NVENC / x264).  Hardware nodes outrank the x264
 * software encoder by weight.
 *
 * @par Input format
 * 8-bit 4:2:0 NV12 (@ref PixelFormat::YUV8_420_SemiPlanar_Rec709) — the
 * format every target SoC accepts.  The planner inserts a CSC when the
 * source is something else.  10-bit / 4:2:2 SoC formats (XV15 / NV16)
 * are a future addition.
 *
 * @par Memory
 * MMAP only for now — raw frames are copied into the OUTPUT queue and
 * coded bitstream copied out of the CAPTURE queue.  DMABUF zero-copy
 * (for the VCU) is a planned second phase.
 *
 * @par Session lifecycle
 * The device is opened lazily on the first @ref submitFrame, once the
 * input dimensions are known.  Coded buffers are emitted as the codec
 * produces them; call @ref flush at end-of-stream and keep draining
 * @ref receiveFrame until a frame carrying
 * @c MediaPayload::Flags::EndOfStream arrives.
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VideoEncoder.
 */
class V4l2VideoEncoder : public VideoEncoder {
        public:
                /**
                 * @brief Constructs an encoder for a specific codec family.
                 * @param codecId @ref VideoCodec::H264 or @ref VideoCodec::HEVC.
                 */
                explicit V4l2VideoEncoder(VideoCodec::ID codecId);
                ~V4l2VideoEncoder() override;

                /**
                 * @brief Uncompressed @ref PixelFormat IDs this backend ingests.
                 *
                 * Single-entry: NV12.  Mirrors the format set the OUTPUT queue
                 * is configured with, so the planner picks NV12 (or a CSC to
                 * it) ahead of @ref submitFrame.
                 */
                static List<int> supportedInputList();

                Error submitFrame(const Frame &frame) override;
                Frame receiveFrame() override;
                Error flush() override;
                Error reset() override;
                void  requestKeyframe() override;

        protected:
                void onConfigure(const MediaConfig &config) override;

        private:
                struct Impl;
                using ImplPtr = UniquePtr<Impl>;
                ImplPtr _impl;
                bool    _requestKey = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
