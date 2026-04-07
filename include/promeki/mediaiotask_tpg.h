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
 * | ConfigVideoWidth | int | 1920 | Frame width. |
 * | ConfigVideoHeight | int | 1080 | Frame height. |
 * | ConfigVideoPixelFormat | PixelDesc | RGB8_sRGB | Pixel description. |
 * | ConfigVideoSolidColor | Color | Black | Fill color for SolidColor pattern. |
 * | ConfigVideoMotion | double | 0.0 | Motion speed. |
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
 * | ConfigTimecodeEnabled | bool | false | Enable timecode metadata. |
 * | ConfigTimecodeStart | String | "00:00:00:00" | Starting timecode string. |
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
                static const MediaIO::ConfigID ConfigVideoWidth;          ///< @brief Frame width (int).
                static const MediaIO::ConfigID ConfigVideoHeight;         ///< @brief Frame height (int).
                static const MediaIO::ConfigID ConfigVideoPixelFormat;    ///< @brief Pixel format (PixelDesc).
                static const MediaIO::ConfigID ConfigVideoSolidColor;     ///< @brief Fill color (Color).
                static const MediaIO::ConfigID ConfigVideoMotion;         ///< @brief Motion speed (double).

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
                Image                   _cachedImage;
                bool                    _videoEnabled = false;

                // Audio state
                AudioTestPattern        *_audioPattern = nullptr;
                AudioDesc               _audioDesc;
                bool                    _audioEnabled = false;
                size_t                  _samplesPerFrame = 0;

                // Timecode state
                TimecodeGenerator       _tcGen;
                bool                    _timecodeEnabled = false;

                // General state
                FrameRate               _frameRate;
                int64_t                 _frameCount = 0;
};

PROMEKI_NAMESPACE_END
