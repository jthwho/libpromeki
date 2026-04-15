/**
 * @file      mediaiotask_videoencoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/codec.h>
#include <promeki/mediapacket.h>
#include <promeki/string.h>
#include <promeki/videocodec.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend that encodes uncompressed Frames into compressed packets.
 * @ingroup proav
 *
 * @c MediaIOTask_VideoEncoder wires a @ref VideoEncoder session into
 * the MediaIO command/signal model.  It accepts a Frame on
 * @c writeFrame(), feeds @ref Image "image[0]" into the encoder via
 * @ref VideoEncoder::submitFrame, and drains @ref
 * VideoEncoder::receivePacket producing one output Frame per emitted
 * @ref MediaPacket.  Audio tracks on the source Frame are forwarded
 * alongside each output packet so downstream stages still see them
 * on the same PTS.
 *
 * The registered backend name is @c "VideoEncoder"; callers pick a
 * concrete codec via the @ref MediaConfig::VideoCodec key (e.g.
 * @c "H264", @c "HEVC") — the task looks up the matching factory
 * through @ref VideoEncoder::createEncoder.
 *
 * @par Mode support
 *
 * Only @c MediaIO::InputAndOutput is supported — the backend has no
 * independent source or sink of its own.
 *
 * @par First-cut limitations
 *
 * - Only the first video image in a Frame is encoded; additional
 *   images are dropped with a one-shot warning.  Stereoscopic /
 *   multi-camera encoding is a follow-up.
 * - The concrete @ref VideoEncoder must operate in a 1-in / 1-out
 *   regime (no B-frames, no look-ahead) so every @c writeFrame
 *   produces its corresponding packet(s) synchronously.  The
 *   @ref NvencVideoEncoder v1 already satisfies this.  B-frame
 *   support needs an EOS protocol between the pipeline and this
 *   task and is deferred.
 * - No SPS/PPS/VPS extraction — parameter sets are concatenated with
 *   the first IDR in a single @ref MediaPacket, which is what the
 *   NVENC backend naturally emits.  Splitting them into their own
 *   @ref MediaPacket::ParameterSet packets (using the @ref BufferView
 *   slicing we just landed) comes when the RTP H.264 / MP4 sinks
 *   need it.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::VideoCodec        | String                    | (required) | Codec factory name (@c "H264", @c "HEVC"). |
 * | @ref MediaConfig::BitrateKbps      | int                       | 5000       | Target / average bitrate. |
 * | @ref MediaConfig::MaxBitrateKbps   | int                       | 0          | Peak bitrate (VBR; 0 = uncapped). |
 * | @ref MediaConfig::VideoRcMode      | Enum @ref VideoRateControl| VBR        | Rate-control mode (CBR / VBR / CQP). |
 * | @ref MediaConfig::GopLength        | int                       | 60         | GOP length in frames. |
 * | @ref MediaConfig::IdrInterval      | int                       | 0          | IDR interval (0 = same as GOP length). |
 * | @ref MediaConfig::BFrames          | int                       | 0          | B-frames between references. |
 * | @ref MediaConfig::LookaheadFrames  | int                       | 0          | Look-ahead depth. |
 * | @ref MediaConfig::VideoPreset      | Enum @ref VideoEncoderPreset | Balanced | Speed/quality preset. |
 * | @ref MediaConfig::VideoProfile     | String                    | (empty)    | Codec profile name. |
 * | @ref MediaConfig::VideoLevel       | String                    | (empty)    | Codec level name. |
 * | @ref MediaConfig::VideoQp          | int                       | 23         | QP for CQP mode. |
 * | @ref MediaConfig::Capacity         | int                       | 8          | Output FIFO depth. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type,          "VideoEncoder");
 * cfg.set(MediaConfig::VideoCodec,     "H264");
 * cfg.set(MediaConfig::BitrateKbps,   8000);
 * cfg.set(MediaConfig::VideoPreset,   VideoEncoderPreset::LowLatency);
 * MediaIO *enc = MediaIO::create(cfg);
 * enc->setMediaDesc(upstreamDesc);
 * enc->open(MediaIO::InputAndOutput);
 * enc->writeFrame(nv12Frame);
 * Frame::Ptr encoded;
 * enc->readFrame(encoded);  // encoded->packetList()[0] is the H.264 packet.
 * enc->close();
 * @endcode
 */
class MediaIOTask_VideoEncoder : public MediaIOTask {
        public:
                /** @brief int64_t — total frames successfully encoded. */
                static inline const MediaIOStats::ID StatsFramesEncoded{"FramesEncoded"};

                /** @brief int64_t — total compressed packets emitted. */
                static inline const MediaIOStats::ID StatsPacketsOut{"PacketsOut"};

                /** @brief Returns the format descriptor for this backend. */
                static MediaIO::FormatDesc formatDesc();

                MediaIOTask_VideoEncoder() = default;
                ~MediaIOTask_VideoEncoder() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                int pendingOutput() const override;

                // Drains the underlying encoder's ready packets into
                // @p srcFrame-derived output frames, appending each to
                // @c _outputQueue.  The source frame supplies audio and
                // metadata that get copied alongside each packet.
                void drainEncoderInto(const Frame::Ptr &srcFrame);

                VideoCodec            _codec;
                VideoEncoder         *_encoder = nullptr;
                int                   _capacity = 8;
                List<Frame::Ptr>      _outputQueue;
                int64_t               _frameCount = 0;
                int64_t               _readCount = 0;
                int64_t               _framesEncoded = 0;
                int64_t               _packetsOut = 0;
                bool                  _capacityWarned = false;
                bool                  _multiImageWarned = false;
                bool                  _closed = false;
};

PROMEKI_NAMESPACE_END
