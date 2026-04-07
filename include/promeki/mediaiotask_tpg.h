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
 * | ConfigFrameRate | FrameRate | 29.97 | Frame rate (required). |
 *
 * @par Config keys — Video
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigVideoEnabled | bool | false | Enable video generation. |
 * | ConfigVideoPattern | String | "colorbars" | Pattern name. |
 * | ConfigVideoSize | Size2Du32 | 1920x1080 | Frame size. |
 * | ConfigVideoPixelFormat | PixelDesc | RGB8_sRGB | Pixel description. |
 * | ConfigVideoSolidColor | Color | Black | Fill color for SolidColor pattern. |
 * | ConfigVideoMotion | double | 0.0 | Motion speed. |
 *
 * @par Config keys — Video burn-in
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigVideoBurnEnabled | bool | false | Enable text burn-in on the pattern. |
 * | ConfigVideoBurnFontPath | String | "" | TrueType font path (required when enabled). |
 * | ConfigVideoBurnFontSize | int | 36 | Font size in pixels. |
 * | ConfigVideoBurnText | String | "" | Static custom burn text (shown below timecode). |
 * | ConfigVideoBurnPosition | String | "bottomcenter" | Position preset. |
 * | ConfigVideoBurnTextColor | Color | White | Burn text foreground color. |
 * | ConfigVideoBurnBgColor | Color | Black | Burn text background color. |
 * | ConfigVideoBurnDrawBg | bool | true | Draw padded background rectangle behind the burn text. |
 *
 * The burn-in runs on top of the cached static background when the
 * pattern is non-moving, so turning burn on is effectively free on the
 * render side beyond one plane copy plus the text draw.  When the
 * timecode generator is also enabled, @c ConfigTimecodeEnabled, the
 * current timecode is drawn on the top line of the burn block.
 *
 * @par Config keys — Audio
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigAudioEnabled | bool | false | Enable audio generation. |
 * | ConfigAudioMode | String | "tone" | "tone", "silence", or "ltc". |
 * | ConfigAudioRate | float | 48000 | Sample rate in Hz. |
 * | ConfigAudioChannels | int | 2 | Channel count. |
 * | ConfigAudioToneFrequency | double | 1000.0 | Tone frequency in Hz. |
 * | ConfigAudioToneLevel | double | -20.0 | Tone level in dBFS. |
 * | ConfigAudioLtcLevel | double | -20.0 | LTC level in dBFS. |
 * | ConfigAudioLtcChannel | int | 0 | LTC channel (-1 = all). |
 *
 * @par Config keys — Timecode
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigTimecodeEnabled | bool | true | Enable timecode metadata. |
 * | ConfigTimecodeStart | String | "01:00:00:00" | Starting timecode string. |
 * | ConfigTimecodeValue | Timecode | — | Pre-built start Timecode. |
 * | ConfigTimecodeDropFrame | bool | false | Drop-frame counting. |
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaIO::ConfigType, "TPG");
 * cfg.set(MediaIOTask_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
 * cfg.set(MediaIOTask_TPG::ConfigVideoEnabled, true);
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
                // General
                static const MediaIO::ConfigID ConfigFrameRate;           ///< @brief Frame rate (FrameRate, required).

                // Video
                static const MediaIO::ConfigID ConfigVideoEnabled;        ///< @brief Enable video (bool).
                static const MediaIO::ConfigID ConfigVideoPattern;        ///< @brief Pattern name (String).
                static const MediaIO::ConfigID ConfigVideoSize;           ///< @brief Frame size (Size2Du32).
                static const MediaIO::ConfigID ConfigVideoPixelFormat;    ///< @brief Pixel format (PixelDesc).
                static const MediaIO::ConfigID ConfigVideoSolidColor;     ///< @brief Fill color (Color).
                static const MediaIO::ConfigID ConfigVideoMotion;         ///< @brief Motion speed (double).

                // Video burn-in (text overlay on top of the pattern)
                static const MediaIO::ConfigID ConfigVideoBurnEnabled;    ///< @brief Enable text burn-in (bool).
                static const MediaIO::ConfigID ConfigVideoBurnFontPath;   ///< @brief Burn font file path (String).
                static const MediaIO::ConfigID ConfigVideoBurnFontSize;   ///< @brief Burn font size in pixels (int).
                static const MediaIO::ConfigID ConfigVideoBurnText;       ///< @brief Static custom burn text (String).
                static const MediaIO::ConfigID ConfigVideoBurnPosition;   ///< @brief Burn position preset (String).
                static const MediaIO::ConfigID ConfigVideoBurnTextColor;  ///< @brief Burn text color (Color).
                static const MediaIO::ConfigID ConfigVideoBurnBgColor;    ///< @brief Burn background color (Color).
                static const MediaIO::ConfigID ConfigVideoBurnDrawBg;     ///< @brief Draw background rect behind burn text (bool).

                // Audio
                static const MediaIO::ConfigID ConfigAudioEnabled;        ///< @brief Enable audio (bool).
                static const MediaIO::ConfigID ConfigAudioMode;           ///< @brief Audio mode (String).
                static const MediaIO::ConfigID ConfigAudioRate;           ///< @brief Sample rate Hz (float).
                static const MediaIO::ConfigID ConfigAudioChannels;       ///< @brief Channel count (int).
                static const MediaIO::ConfigID ConfigAudioToneFrequency;  ///< @brief Tone frequency Hz (double).
                static const MediaIO::ConfigID ConfigAudioToneLevel;      ///< @brief Tone level dBFS (double).
                static const MediaIO::ConfigID ConfigAudioLtcLevel;       ///< @brief LTC level dBFS (double).
                static const MediaIO::ConfigID ConfigAudioLtcChannel;     ///< @brief LTC channel (int).

                // Timecode
                static const MediaIO::ConfigID ConfigTimecodeEnabled;     ///< @brief Enable timecode (bool).
                static const MediaIO::ConfigID ConfigTimecodeStart;       ///< @brief Start timecode string (String).
                static const MediaIO::ConfigID ConfigTimecodeValue;       ///< @brief Pre-built Timecode (Timecode).
                static const MediaIO::ConfigID ConfigTimecodeDropFrame;   ///< @brief Drop-frame flag (bool).

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
