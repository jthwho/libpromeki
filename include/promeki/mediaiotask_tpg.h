/**
 * @file      mediaiotask_tpg.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/videotestpattern.h>
#include <promeki/audiotestpattern.h>
#include <promeki/timecodegenerator.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/mediadesc.h>
#include <promeki/size2d.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend that generates test patterns.
 * @ingroup proav
 *
 * Read-only MediaIO source that produces synchronized Frame objects
 * containing optional video, audio, and timecode.  Each component is
 * independently enabled so the generator can serve as a video-only,
 * audio-only, or combined source.
 *
 * Video generation is delegated to VideoTestPattern, audio to
 * AudioTestPattern, and timecode to TimecodeGenerator.
 *
 * This is an infinite source: frameCount returns FrameCountInfinite
 * and canSeek returns false.
 *
 * @par Config keys — General
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::FrameRate | FrameRate | 29.97 | Frame rate (required). |
 *
 * @par Config keys — Video
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::VideoEnabled     | bool      | true       | Enable video generation. |
 * | @ref MediaConfig::VideoPattern     | Enum @ref VideoPattern | ColorBars | Pattern selector. |
 * | @ref MediaConfig::VideoSize        | Size2Du32 | 1920x1080  | Frame size. |
 * | @ref MediaConfig::VideoPixelFormat | PixelDesc | RGB8_sRGB  | Pixel description. |
 * | @ref MediaConfig::VideoSolidColor  | Color     | Black      | Fill color for SolidColor pattern. |
 * | @ref MediaConfig::VideoMotion      | double    | 0.0        | Motion speed. |
 *
 * @par Config keys — Video burn-in
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::VideoBurnEnabled   | bool      | false      | Enable text burn-in on the pattern. |
 * | @ref MediaConfig::VideoBurnFontPath  | String    | ""         | TrueType font path. Empty = bundled default font. |
 * | @ref MediaConfig::VideoBurnFontSize  | int       | 36         | Font size in pixels. |
 * | @ref MediaConfig::VideoBurnText      | String    | ""         | Static custom burn text (shown below timecode). |
 * | @ref MediaConfig::VideoBurnPosition  | Enum @ref BurnPosition | BottomCenter | Position preset. |
 * | @ref MediaConfig::VideoBurnTextColor | Color     | White      | Burn text foreground color. |
 * | @ref MediaConfig::VideoBurnBgColor   | Color     | Black      | Burn text background color. |
 * | @ref MediaConfig::VideoBurnDrawBg    | bool      | true       | Draw padded background rectangle behind the burn text. |
 *
 * The burn-in runs on top of the cached static background when the
 * pattern is non-moving, so turning burn on is effectively free on the
 * render side beyond one plane copy plus the text draw.  When the
 * timecode generator is also enabled (@ref MediaConfig::TimecodeEnabled),
 * the current timecode is drawn on the top line of the burn block.
 *
 * @par Config keys — Audio
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::AudioEnabled       | bool   | true   | Enable audio generation. |
 * | @ref MediaConfig::AudioMode          | Enum @ref AudioPattern | Tone | Audio pattern selector. |
 * | @ref MediaConfig::AudioRate          | float  | 48000  | Sample rate in Hz. |
 * | @ref MediaConfig::AudioChannels      | int    | 2      | Channel count. |
 * | @ref MediaConfig::AudioToneFrequency | double | 1000.0 | Tone frequency in Hz. |
 * | @ref MediaConfig::AudioToneLevel     | double | -20.0  | Tone level in dBFS. |
 * | @ref MediaConfig::AudioLtcLevel      | double | -20.0  | LTC level in dBFS. |
 * | @ref MediaConfig::AudioLtcChannel    | int    | 0      | LTC channel (-1 = all). |
 *
 * @par Config keys — Timecode
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::TimecodeEnabled   | bool     | true        | Enable timecode metadata. |
 * | @ref MediaConfig::TimecodeStart     | String   | "01:00:00:00" | Starting timecode string. |
 * | @ref MediaConfig::TimecodeValue     | Timecode | —           | Pre-built start Timecode. |
 * | @ref MediaConfig::TimecodeDropFrame | bool     | false       | Drop-frame counting. |
 *
 * @par Example
 * @code
 * // The default config enables video+audio+timecode, so the plain
 * // form is enough for a ready-to-use TPG source.
 * MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::Reader);
 * Frame::Ptr frame;
 * io->readFrame(frame);
 * io->close();
 * delete io;
 * @endcode
 */
class MediaIOTask_TPG : public MediaIOTask {
        public:
                // All config keys for this backend live in MediaConfig —
                // use @c MediaConfig::FrameRate / @c MediaConfig::VideoSize
                // / @c MediaConfig::VideoPattern / etc. directly.  See
                // @ref MediaConfig for the full catalog.

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc with no file extensions (generator source).
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_TPG. */
                MediaIOTask_TPG() = default;

                /** @brief Destructor.  Releases the audio pattern generator. */
                ~MediaIOTask_TPG() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;

                // Video state
                VideoTestPattern        _videoPattern;
                ImageDesc               _imageDesc;
                double                  _motion = 0.0;
                double                  _motionOffset = 0.0;
                bool                    _videoEnabled = false;

                // Audio state
                AudioTestPattern        *_audioPattern = nullptr;
                AudioDesc               _audioDesc;
                bool                    _audioEnabled = false;

                // Timecode state
                TimecodeGenerator       _tcGen;
                bool                    _timecodeEnabled = false;

                // General state
                FrameRate               _frameRate;
                int64_t                 _frameCount = 0;
};

PROMEKI_NAMESPACE_END
