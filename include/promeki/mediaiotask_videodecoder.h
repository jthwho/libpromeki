/**
 * @file      mediaiotask_videodecoder.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/codec.h>
#include <promeki/pixeldesc.h>
#include <promeki/string.h>
#include <promeki/videocodec.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend that decodes compressed packets into uncompressed Frames.
 * @ingroup proav
 *
 * @c MediaIOTask_VideoDecoder is the symmetric counterpart of
 * @ref MediaIOTask_VideoEncoder.  It accepts a Frame whose
 * @ref Frame::packetList carries one encoded access unit, hands each
 * packet to a concrete @ref VideoDecoder via @c submitPacket, and
 * drains @c receiveFrame producing one output Frame per decoded
 * image in @ref Frame::imageList.  Audio on the input Frame is
 * forwarded unchanged so it stays synchronised on the same PTS.
 *
 * The registered backend name is @c "VideoDecoder"; callers pick a
 * concrete codec via @ref MediaConfig::VideoCodec (e.g. @c "H264",
 * @c "HEVC") — the task looks up the matching factory through
 * @ref VideoDecoder::createDecoder.
 *
 * @par Mode support
 *
 * Only @c MediaIO::InputAndOutput is supported.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::VideoCodec        | String   | (required) | Codec factory name (@c "H264", @c "HEVC"). |
 * | @ref MediaConfig::OutputPixelDesc  | PixelDesc | Invalid    | Desired uncompressed output format. Empty / Invalid means "use decoder's native". |
 * | @ref MediaConfig::Capacity         | int       | 8          | Output FIFO depth. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type,          "VideoDecoder");
 * cfg.set(MediaConfig::VideoCodec,     "H264");
 * cfg.set(MediaConfig::OutputPixelDesc, PixelDesc(PixelDesc::YUV8_420_SemiPlanar_Rec709));
 * MediaIO *dec = MediaIO::create(cfg);
 * dec->setMediaDesc(compressedDesc);
 * dec->open(MediaIO::InputAndOutput);
 * dec->writeFrame(packetFrame);
 * Frame::Ptr decoded;
 * dec->readFrame(decoded);   // decoded->imageList()[0] is the uncompressed image.
 * dec->close();
 * @endcode
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

                // Drains every currently-available image out of the
                // underlying decoder and pushes one Frame per image
                // onto @c _outputQueue, preserving @p srcFrame's audio
                // and metadata.
                void drainDecoderInto(const Frame::Ptr &srcFrame);

                VideoCodec            _codec;
                VideoDecoder         *_decoder = nullptr;
                PixelDesc             _outputPixelDesc;
                bool                  _outputPixelDescSet = false;
                int                   _capacity = 8;
                List<Frame::Ptr>      _outputQueue;
                int64_t               _frameCount = 0;
                int64_t               _readCount = 0;
                int64_t               _packetsDecoded = 0;
                int64_t               _imagesOut = 0;
                bool                  _capacityWarned = false;
                bool                  _closed = false;
};

PROMEKI_NAMESPACE_END
