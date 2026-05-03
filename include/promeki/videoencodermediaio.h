/**
 * @file      videoencodermediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaconfig.h>
#include <promeki/videoencoder.h>
#include <promeki/string.h>
#include <promeki/videocodec.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that encodes uncompressed Frames into compressed packets.
 * @ingroup proav
 *
 * @c VideoEncoderMediaIO wires a @ref VideoEncoder session into
 * the MediaIO command/signal model.  It accepts a Frame on
 * @c writeFrame(), feeds the first @ref UncompressedVideoPayload into
 * the encoder via @ref VideoEncoder::submitPayload, and drains
 * @ref VideoEncoder::receiveCompressedPayload producing one output
 * Frame per emitted @ref CompressedVideoPayload.  Audio tracks on the
 * source Frame are forwarded alongside each output payload so
 * downstream stages still see them on the same PTS.
 *
 * The registered backend name is @c "VideoEncoder"; callers pick a
 * concrete codec via the @ref MediaConfig::VideoCodec key (e.g.
 * @c "H264", @c "HEVC") — the task looks up the matching factory
 * through @ref VideoCodec::createEncoder.
 *
 * @par Mode support
 *
 * Only @c MediaIO::Transform is supported — the backend has no
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
 *   @c NvencVideoEncoder v1 already satisfies this.  B-frame
 *   support needs an EOS protocol between the pipeline and this
 *   task and is deferred.
 * - No SPS/PPS/VPS extraction — parameter sets are concatenated with
 *   the first IDR in a single @ref CompressedVideoPayload, which is
 *   what the NVENC backend naturally emits.  Splitting them into
 *   their own @ref CompressedVideoPayload::ParameterSet payloads
 *   (using the @ref BufferView slicing we just landed) comes when
 *   the RTP H.264 / MP4 sinks need it.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::VideoCodec        | String                    | (required) | Codec factory name (@c "H264", @c "HEVC"). |
 * | @ref MediaConfig::BitrateKbps      | int                       | 5000       | Target / average bitrate. |
 * | @ref MediaConfig::MaxBitrateKbps   | int                       | 0          | Peak bitrate (VBR; 0 = uncapped). |
 * | @ref MediaConfig::VideoRcMode      | Enum @ref RateControlMode| VBR        | Rate-control mode (CBR / VBR / CQP). |
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
 * enc->setExpectedDesc(upstreamDesc);
 * enc->open(MediaIO::Transform);
 * enc->writeFrame(nv12Frame);
 * Frame::Ptr encoded;
 * enc->readFrame(encoded);
 * // encoded->videoPayloads()[0] is a CompressedVideoPayload
 * // carrying the H.264 bitstream.
 * enc->close();
 * @endcode
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 */
class VideoEncoderMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(VideoEncoderMediaIO, SharedThreadMediaIO)
        public:
                /** @brief int64_t — total frames successfully encoded. */
                static inline const MediaIOStats::ID StatsFramesEncoded{"FramesEncoded"};

                /** @brief int64_t — total compressed packets emitted. */
                static inline const MediaIOStats::ID StatsPacketsOut{"PacketsOut"};

                VideoEncoderMediaIO(ObjectBase *parent = nullptr);
                ~VideoEncoderMediaIO() override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;
                Error proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const override;
                int   pendingInternalWrites() const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                void  configChanged(const MediaConfig &delta) override;

        private:
                // Drains the underlying encoder's ready packets into
                // per-packet output frames, appending each to
                // @c _outputQueue.  Each drained packet is paired with
                // the source Frame that produced it via
                // @c _pendingSrcFrames so audio / metadata travel with
                // the correct input even when NVENC buffers an earlier
                // frame and emits it on a later submit.
                void drainEncoderInto();

                MediaConfig        _config;
                VideoCodec         _codec;
                VideoEncoder::UPtr _encoder;
                int                _capacity = 8;
                List<Frame::Ptr>   _outputQueue;

                // FIFO of submitted source Frames awaiting a matching
                // packet from the encoder.  One entry is pushed per
                // successful submitPayload() and popped per emitted
                // CompressedVideoPayload; the pairing is order-preserving because
                // the encoder runs in 1-in / 1-out sync mode (no
                // B-frames, no look-ahead).  Needed to preserve the
                // source frame's metadata + audio across the
                // NEED_MORE_INPUT case where submit N's packet emerges
                // during submit N+1.
                List<Frame::Ptr> _pendingSrcFrames;
                FrameCount       _frameCount{0};
                int64_t          _readCount = 0;
                FrameCount       _framesEncoded{0};
                int64_t          _packetsOut = 0;
                bool             _capacityWarned = false;
                bool             _multiImageWarned = false;
                bool             _closed = false;
};

/**
 * @brief @ref MediaIOFactory for the VideoEncoder backend.
 * @ingroup proav
 */
class VideoEncoderFactory : public MediaIOFactory {
        public:
                VideoEncoderFactory() = default;

                String name() const override { return String("VideoEncoder"); }
                String displayName() const override { return String("Video Encoder"); }
                String description() const override {
                        return String("Encodes uncompressed video frames into a registered VideoCodec bitstream");
                }
                bool canBeTransform() const override { return true; }

                Config::SpecMap configSpecs() const override;
                bool            bridge(const MediaDesc &from, const MediaDesc &to, Config *outConfig,
                                       int *outCost) const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END
