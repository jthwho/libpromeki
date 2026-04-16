/**
 * @file      mediaiotask_csc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/pixeldesc.h>
#include <promeki/image.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend that performs color space conversion.
 * @ingroup proav
 *
 * ReadWrite MediaIO that accepts a frame on @c writeFrame(), converts
 * each uncompressed video image to the configured output @ref PixelDesc
 * via @ref Image::convert, and emits the result on @c readFrame().
 * Audio and metadata are forwarded unchanged.
 *
 * If no @ref MediaConfig::OutputPixelDesc is set (or it is invalid),
 * video images pass through unchanged.  Compressed pixel formats are
 * rejected — use @ref MediaIOTask_VideoEncoder /
 * @ref MediaIOTask_VideoDecoder for codec transitions.
 *
 * @par Mode support
 *
 * Only @c MediaIO::InputAndOutput is supported.
 *
 * @par Back-pressure
 *
 * The task maintains an internal output FIFO.  Writes always succeed —
 * frames are never silently dropped.  When the FIFO exceeds the
 * configured capacity a one-shot warning is logged.  When the FIFO is
 * empty, @c readFrame() returns @c Error::TryAgain.  FIFO capacity is
 * configurable via @ref MediaConfig::Capacity (default 4).
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::OutputPixelDesc | PixelDesc | Invalid (pass-through) | Target video pixel description. |
 * | @ref MediaConfig::Capacity        | int       | 4                      | Maximum output FIFO depth. |
 *
 * @par Stats keys
 *
 * | Key | Type | Description |
 * |-----|------|-------------|
 * | FramesConverted | int64_t | Total frames successfully converted. |
 * | QueueDepth      | int64_t | Current FIFO depth. |
 * | QueueCapacity   | int64_t | Maximum FIFO depth. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "CSC");
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
class MediaIOTask_CSC : public MediaIOTask {
        public:
                /** @brief int64_t — total frames successfully converted. */
                static inline const MediaIOStats::ID StatsFramesConverted{"FramesConverted"};

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc describing the CSC backend.
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_CSC. */
                MediaIOTask_CSC() = default;

                /** @brief Destructor. */
                ~MediaIOTask_CSC() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                int pendingOutput() const override;

                Error convertImage(const Image &input, Image &output);
                Error convertFrame(const Frame::Ptr &input, Frame::Ptr &output);

                PixelDesc                       _outputPixelDesc;
                bool                            _outputPixelDescSet = false;
                int                             _capacity = 4;

                List<Frame::Ptr>                _outputQueue;
                int64_t                         _frameCount = 0;
                int64_t                         _readCount = 0;
                int64_t                         _framesConverted = 0;
                bool                            _capacityWarned = false;
};

PROMEKI_NAMESPACE_END
