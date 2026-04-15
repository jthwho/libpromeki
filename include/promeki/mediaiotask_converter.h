/**
 * @file      mediaiotask_converter.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/pixeldesc.h>
#include <promeki/audiodesc.h>
#include <promeki/image.h>
#include <promeki/list.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend that transforms media frames.
 * @ingroup proav
 *
 * Generic ReadWrite MediaIO that accepts a frame on @c writeFrame(),
 * applies stateless intra-frame transforms, and emits the result on
 * @c readFrame().  Supported transforms in this initial version:
 *
 * - **Video conversion** — every video transform (uncompressed CSC,
 *   JPEG encode, JPEG decode, compressed→compressed transcode) is
 *   delegated to @ref Image::convert.  The converter just forwards
 *   @ref MediaConfig::OutputPixelDesc as the target and threads
 *   @ref MediaConfig::JpegQuality / @ref MediaConfig::JpegSubsampling
 *   through the same @ref MediaConfig so the codec path honours them.
 * - **Audio sample-format conversion** — delegated to
 *   @ref Audio::convertTo.  Applies whenever
 *   @ref MediaConfig::OutputAudioDataType is set and differs from the
 *   source data type.
 *
 * If no output keys are set, the Converter is a pass-through: the
 * input frame is delivered unchanged on the next @c readFrame().
 * Images and audio for which no corresponding output key is set are
 * forwarded as-is, so a Converter with only an audio config leaves
 * video untouched and vice-versa.
 *
 * The Converter is stateless with respect to temporal codecs: each
 * @c writeFrame() produces exactly one output frame.  Stateful
 * encoders (H.264, HEVC, etc.) are deferred to a later revision.
 *
 * @par Mode support
 *
 * Only @c MediaIO::InputAndOutput is supported.  The Converter has no
 * independent input source, so Reader mode has nothing to read; and
 * no independent output sink, so Writer mode has nowhere to write.
 *
 * @par Back-pressure
 *
 * The Converter maintains an internal output FIFO.  Writes always
 * succeed — frames are never silently dropped.  When the FIFO
 * exceeds the configured capacity a one-shot warning is logged;
 * back-pressure is the caller's responsibility.  When the FIFO is
 * empty, @c executeCmd(MediaIOCommandRead&) returns @c Error::TryAgain
 * so the consumer can wait for more input.  FIFO capacity is
 * configurable via @ref MediaConfig::Capacity (default 4).
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::OutputPixelDesc     | PixelDesc                       | Invalid (pass-through) | Target video pixel description. |
 * | @ref MediaConfig::JpegQuality         | int                             | 85                     | JPEG quality (1-100) for encode path. |
 * | @ref MediaConfig::JpegSubsampling     | Enum @ref ChromaSubsampling     | YUV422                 | JPEG chroma subsampling. |
 * | @ref MediaConfig::OutputAudioDataType | Enum @ref AudioDataType         | Invalid (pass-through) | Target audio sample format. |
 * | @ref MediaConfig::Capacity            | int                             | 4                      | Maximum output FIFO depth. |
 *
 * @par Stats keys
 *
 * | Key | Type | Description |
 * |-----|------|-------------|
 * | FramesConverted | int64_t | Total frames successfully converted. |
 * | BytesIn | int64_t | Total input image+audio bytes processed. |
 * | BytesOut | int64_t | Total output image+audio bytes produced. |
 * | QueueDepth | int64_t | Current FIFO depth. |
 * | QueueCapacity | int64_t | Maximum FIFO depth. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "Converter");
 * cfg.set(MediaConfig::OutputPixelDesc,
 *         PixelDesc(PixelDesc::RGBA8_sRGB));
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::InputAndOutput);
 * io->writeFrame(inputFrame);
 * Frame::Ptr outputFrame;
 * io->readFrame(outputFrame);
 * io->close();
 * delete io;
 * @endcode
 */
class MediaIOTask_Converter : public MediaIOTask {
        public:
                // Config keys for this backend live in @ref MediaConfig:
                // @c OutputPixelDesc, @c JpegQuality, @c JpegSubsampling,
                // @c OutputAudioDataType, @c Capacity.  See @ref MediaConfig
                // for the full catalog.

                /** @brief int64_t — total frames successfully converted. */
                static inline const MediaIOStats::ID StatsFramesConverted{"FramesConverted"};

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc describing the Converter backend.
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_Converter. */
                MediaIOTask_Converter() = default;

                /** @brief Destructor. */
                ~MediaIOTask_Converter() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                int pendingOutput() const override;

                // Helpers
                Error convertImage(const Image &input, Image &output);
                Error convertFrame(const Frame::Ptr &input, Frame::Ptr &output);

                // Config state
                PixelDesc                       _outputPixelDesc;
                bool                            _outputPixelDescSet = false;
                AudioDesc::DataType             _outputAudioDataType = AudioDesc::Invalid;
                bool                            _outputAudioDataTypeSet = false;
                int                             _capacity = 4;

                // Runtime state
                List<Frame::Ptr>                _outputQueue;
                int64_t                         _frameCount = 0;
                int64_t                         _readCount = 0;
                int64_t                         _framesConverted = 0;
                bool                            _capacityWarned = false;
};

PROMEKI_NAMESPACE_END
