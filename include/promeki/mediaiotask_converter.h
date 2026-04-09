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
#include <promeki/jpegimagecodec.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend that transforms media frames.
 * @ingroup proav
 *
 * Generic ReadWrite MediaIO that accepts a frame on @c writeFrame(),
 * applies stateless intra-frame transforms, and emits the result on
 * @c readFrame().  Supported transforms in this initial version:
 *
 * - **Video colorspace conversion (CSC)** — delegated to
 *   @c Image::convert() via the CSC framework.  Applies whenever
 *   @c ConfigOutputPixelDesc is set and both source and target pixel
 *   descriptions are uncompressed.
 * - **JPEG encode** — delegated to @c JpegImageCodec::encode().
 *   Applies when @c ConfigOutputPixelDesc is a compressed JPEG pixel
 *   description and the source image is uncompressed.  Quality and
 *   chroma subsampling are taken from @c ConfigJpegQuality and
 *   @c ConfigJpegSubsampling.
 * - **JPEG decode** — delegated to @c JpegImageCodec::decode().
 *   Applies when the source image is a compressed JPEG and
 *   @c ConfigOutputPixelDesc is an uncompressed pixel description.
 * - **Audio sample-format conversion** — delegated to
 *   @c Audio::convertTo().  Applies whenever @c ConfigOutputAudioDataType
 *   is set and differs from the source data type.
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
 * Only @c MediaIO::ReadWrite is supported.  The Converter has no
 * independent input source, so Reader mode has nothing to read; and
 * no independent output sink, so Writer mode has nowhere to write.
 *
 * @par Back-pressure
 *
 * The Converter maintains an internal output FIFO.  When the FIFO is
 * full, @c executeCmd(MediaIOCommandWrite&) returns @c Error::TryAgain
 * so the producer can back off.  When the FIFO is empty,
 * @c executeCmd(MediaIOCommandRead&) returns @c Error::TryAgain so
 * the consumer can wait for more input.  FIFO capacity is configurable
 * via @c ConfigCapacity (default 4).
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigOutputPixelDesc | PixelDesc | Invalid (pass-through) | Target video pixel description. |
 * | ConfigJpegQuality | int | 85 | JPEG quality (1-100) for encode path. |
 * | ConfigJpegSubsampling | Enum (ChromaSubsampling) | YUV422 | JPEG chroma subsampling. |
 * | ConfigOutputAudioDataType | Enum (AudioDataType) | Invalid (pass-through) | Target audio sample format. |
 * | ConfigCapacity | int | 4 | Maximum output FIFO depth. |
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
 * cfg.set(MediaIO::ConfigType, "Converter");
 * cfg.set(MediaIOTask_Converter::ConfigOutputPixelDesc,
 *         PixelDesc(PixelDesc::RGBA8_sRGB));
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::ReadWrite);
 * io->writeFrame(inputFrame);
 * Frame::Ptr outputFrame;
 * io->readFrame(outputFrame);
 * io->close();
 * delete io;
 * @endcode
 */
class MediaIOTask_Converter : public MediaIOTask {
        public:
                /** @brief Target video pixel description (PixelDesc). */
                static const MediaIO::ConfigID ConfigOutputPixelDesc;

                /** @brief JPEG quality 1-100 (int). */
                static const MediaIO::ConfigID ConfigJpegQuality;

                /** @brief JPEG chroma subsampling (Enum ChromaSubsampling). */
                static const MediaIO::ConfigID ConfigJpegSubsampling;

                /** @brief Target audio sample format (Enum AudioDataType; Invalid = pass-through). */
                static const MediaIO::ConfigID ConfigOutputAudioDataType;

                /** @brief Maximum output FIFO depth (int, default 4). */
                static const MediaIO::ConfigID ConfigCapacity;

                /** @brief int64_t — total frames successfully converted. */
                static inline const MediaIOStats::ID StatsFramesConverted{"FramesConverted"};

                /** @brief int64_t — total input image+audio bytes processed. */
                static inline const MediaIOStats::ID StatsBytesIn{"BytesIn"};

                /** @brief int64_t — total output image+audio bytes produced. */
                static inline const MediaIOStats::ID StatsBytesOut{"BytesOut"};

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

                // Helpers
                Error convertImage(const Image &input, Image &output);
                Error convertFrame(const Frame::Ptr &input, Frame::Ptr &output);

                // Config state
                PixelDesc                       _outputPixelDesc;
                bool                            _outputPixelDescSet = false;
                AudioDesc::DataType             _outputAudioDataType = AudioDesc::Invalid;
                bool                            _outputAudioDataTypeSet = false;
                int                             _jpegQuality = 85;
                JpegImageCodec::Subsampling     _jpegSubsampling = JpegImageCodec::Subsampling422;
                int                             _capacity = 4;

                // Runtime state
                JpegImageCodec                  _jpegCodec;
                List<Frame::Ptr>                _outputQueue;
                int64_t                         _frameCount = 0;
                int64_t                         _readCount = 0;
                int64_t                         _framesConverted = 0;
                int64_t                         _bytesIn = 0;
                int64_t                         _bytesOut = 0;
};

PROMEKI_NAMESPACE_END
