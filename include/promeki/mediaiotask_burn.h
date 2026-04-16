/**
 * @file      mediaiotask_burn.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/videotestpattern.h>
#include <promeki/list.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend that burns text overlays onto video frames.
 * @ingroup proav
 *
 * ReadWrite MediaIO that accepts a frame on @c writeFrame(), renders a
 * text overlay onto each video image, and emits the result on
 * @c readFrame().  Audio and non-video metadata are forwarded unchanged.
 *
 * The burn text is specified as a @ref Frame::makeString template
 * (via @ref MediaConfig::VideoBurnText), resolved per-frame against
 * the frame's metadata.  This allows dynamic content such as
 * @c "{Timecode:smpte}" or @c "{@VideoFormat}" to update on every
 * frame.
 *
 * Font rendering is delegated to @ref VideoTestPattern::applyBurn,
 * which uses the @ref FastFont glyph cache for efficient repeated
 * draws.  The image must be in a paintable pixel format
 * (@c PixelDesc::hasPaintEngine() == true); non-paintable formats
 * pass through with a one-shot warning.
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
 * | @ref MediaConfig::VideoBurnEnabled   | bool                    | true         | Enable text burn-in. |
 * | @ref MediaConfig::VideoBurnFontPath  | String                  | ""           | TrueType font path.  Empty = bundled default font. |
 * | @ref MediaConfig::VideoBurnFontSize  | int                     | 0            | Font size in pixels.  0 = auto-scale from image height. |
 * | @ref MediaConfig::VideoBurnText      | String                  | "{Timecode:smpte}" | @ref Frame::makeString template resolved per-frame. |
 * | @ref MediaConfig::VideoBurnPosition  | Enum @ref BurnPosition  | BottomCenter | Position preset. |
 * | @ref MediaConfig::VideoBurnTextColor | Color                   | White        | Burn text foreground color. |
 * | @ref MediaConfig::VideoBurnBgColor   | Color                   | Black        | Burn text background color. |
 * | @ref MediaConfig::VideoBurnDrawBg    | bool                    | true         | Draw background rectangle behind burn text. |
 * | @ref MediaConfig::Capacity           | int                     | 4            | Maximum output FIFO depth. |
 *
 * @par Stats keys
 *
 * | Key | Type | Description |
 * |-----|------|-------------|
 * | FramesBurned  | int64_t | Total frames with burn-in applied. |
 * | QueueDepth    | int64_t | Current FIFO depth. |
 * | QueueCapacity | int64_t | Maximum FIFO depth. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "Burn");
 * cfg.set(MediaConfig::VideoBurnText, "{Timecode:smpte}");
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::InputAndOutput);
 * io->writeFrame(inputFrame);
 * Frame::Ptr outputFrame;
 * io->readFrame(outputFrame);
 * io->close();
 * delete io;
 * @endcode
 */
class MediaIOTask_Burn : public MediaIOTask {
        public:
                /** @brief int64_t — total frames with burn-in applied. */
                static inline const MediaIOStats::ID StatsFramesBurned{"FramesBurned"};

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc describing the Burn backend.
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_Burn. */
                MediaIOTask_Burn() = default;

                /** @brief Destructor. */
                ~MediaIOTask_Burn() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                int pendingOutput() const override;

                Error burnFrame(const Frame::Ptr &input, Frame::Ptr &output);

                VideoTestPattern                _pattern;
                String                          _burnTextTemplate;
                bool                            _burnEnabled = false;
                int                             _capacity = 4;

                List<Frame::Ptr>                _outputQueue;
                int64_t                         _frameCount = 0;
                int64_t                         _readCount = 0;
                int64_t                         _framesBurned = 0;
                bool                            _capacityWarned = false;
                bool                            _notPaintableWarned = false;
};

PROMEKI_NAMESPACE_END
