/**
 * @file      mediaiotask_videodecoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/mediaconfig.h>
#include <promeki/videodecoder.h>
#include <promeki/pixelformat.h>
#include <promeki/string.h>
#include <promeki/videocodec.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend that decodes compressed packets into uncompressed Frames.
 * @ingroup proav
 *
 * @c MediaIOTask_VideoDecoder is the symmetric counterpart of
 * @ref MediaIOTask_VideoEncoder.  It accepts a Frame whose
 * @ref CompressedVideoPayload entries carry the encoded bitstream,
 * hands each payload to a concrete @ref VideoDecoder via
 * @c submitPayload, and drains @c receiveVideoPayload producing one
 * output Frame per decoded @ref UncompressedVideoPayload.  Audio on
 * the input Frame is forwarded unchanged so it stays synchronised on
 * the same PTS.
 *
 * The registered backend name is @c "VideoDecoder"; callers select a
 * concrete codec in one of two ways:
 *
 *  -# **Explicit** — set @ref MediaConfig::VideoCodec in the config
 *     (e.g. @c "H264", @c "HEVC").  The decoder is created during
 *     @c open().
 *  -# **Auto-detect** — omit @ref MediaConfig::VideoCodec.  The task
 *     defers decoder creation until the first @c writeFrame() call,
 *     where it inspects the incoming @ref CompressedVideoPayload pixelFormat
 *     and resolves the codec via @ref VideoCodec::fromPixelFormat.
 *
 * @par Mode support
 *
 * Only @c MediaIO::Transform is supported.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::VideoCodec       | VideoCodec | (auto)  | Codec to use.  When omitted the codec is detected from the first payload's @ref CompressedVideoPayload pixelFormat. |
 * | @ref MediaConfig::OutputPixelFormat  | PixelFormat  | Invalid | Desired uncompressed output format. Empty / Invalid means "use decoder's native". |
 * | @ref MediaConfig::Capacity         | int        | 8       | Output FIFO depth. |
 *
 * @par Example — explicit codec
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type,          "VideoDecoder");
 * cfg.set(MediaConfig::VideoCodec,     "H264");
 * cfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
 * MediaIO *dec = MediaIO::create(cfg);
 * dec->setExpectedDesc(compressedDesc);
 * dec->open(MediaIO::Transform);
 * dec->writeFrame(packetFrame);
 * Frame::Ptr decoded;
 * dec->readFrame(decoded);
 * dec->close();
 * @endcode
 *
 * @par Example — auto-detect codec from packet
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "VideoDecoder");
 * MediaIO *dec = MediaIO::create(cfg);
 * dec->setExpectedDesc(compressedDesc);
 * dec->open(MediaIO::Transform);
 * dec->writeFrame(packetFrame);   // codec resolved here
 * Frame::Ptr decoded;
 * dec->readFrame(decoded);
 * dec->close();
 * @endcode
 *
 * @par Thread Safety
 * Strand-affine — see @ref MediaIOTask.
 */
class MediaIOTask_VideoDecoder : public MediaIOTask {
        public:
                /** @brief int64_t — total packets successfully decoded. */
                static inline const MediaIOStats::ID StatsPacketsDecoded{"PacketsDecoded"};

                /** @brief int64_t — total decoded images emitted. */
                static inline const MediaIOStats::ID StatsImagesOut{"ImagesOut"};

                /** @brief Returns the format descriptor for this backend. */
                static MediaIO::FormatDesc formatDesc();

                MediaIOTask_VideoDecoder() = default;
                ~MediaIOTask_VideoDecoder() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                int pendingOutput() const override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;
                Error proposeOutput(const MediaDesc &requested,
                                    MediaDesc *achievable) const override;

                // Drains every currently-available image out of the
                // underlying decoder and pushes one Frame per image
                // onto @c _outputQueue.  Each emitted image is paired
                // with the oldest queued source packet Frame (see
                // @c _pendingSrcFrames) so its audio / frame-level
                // metadata travel with the right input even across
                // the DPB / reorder buffering delay every H.264 /
                // HEVC decoder has on startup.
                void configChanged(const MediaConfig &delta) override;
                void drainDecoderInto();
                Error createDecoder(const VideoCodec &codec);

                MediaConfig           _config;
                VideoCodec            _codec;
                VideoDecoder::UPtr    _decoder;
                PixelFormat             _outputPixelFormat;
                bool                  _outputPixelFormatSet = false;
                int                   _capacity = 8;
                List<Frame::Ptr>      _outputQueue;

                // FIFO of source Frames awaiting a decoded image.  One
                // entry is pushed per packet handed to @c submitPacket
                // and popped per image emitted by the decoder.  Without
                // this, images emerging from the DPB warmup / reorder
                // buffer are stamped with the wrong input Frame's
                // metadata, which shows up as off-by-N timecode /
                // audio after an encode/decode round trip.
                List<Frame::Ptr>      _pendingSrcFrames;
                FrameCount            _frameCount{0};
                int64_t               _readCount = 0;
                int64_t               _packetsDecoded = 0;
                int64_t               _imagesOut = 0;
                bool                  _capacityWarned = false;
                bool                  _closed = false;
};

PROMEKI_NAMESPACE_END
