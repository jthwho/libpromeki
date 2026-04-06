/**
 * @file      mediaio_tpg.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaio.h>
#include <promeki/videotestpattern.h>
#include <promeki/audiotestpattern.h>
#include <promeki/timecodegenerator.h>
#include <promeki/imagedesc.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that generates test patterns.
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
 * This is an infinite source: frameCount() returns 0 and canSeek()
 * returns false.
 *
 * @par Config keys — General
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigFrameRate | FrameRate | 29.97 | Frame rate (required). |
 *
 * @par Config keys — Video (omit ConfigVideoEnabled or set false to disable)
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
 * @par Config keys — Audio (omit ConfigAudioEnabled or set false to disable)
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
 * @par Config keys — Timecode (omit ConfigTimecodeEnabled or set false to disable)
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigTimecodeEnabled | bool | false | Enable timecode metadata. |
 * | ConfigTimecodeStart | String | "00:00:00:00" | Starting timecode string. |
 * | ConfigTimecodeValue | Timecode | — | Pre-built start Timecode. |
 * | ConfigTimecodeDropFrame | bool | false | Drop-frame counting. |
 *
 * @par Example — Video + Audio
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaIO::ConfigType, "TPG");
 * cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
 * cfg.set(MediaIO_TPG::ConfigVideoEnabled, true);
 * cfg.set(MediaIO_TPG::ConfigVideoWidth, 1920);
 * cfg.set(MediaIO_TPG::ConfigVideoHeight, 1080);
 * cfg.set(MediaIO_TPG::ConfigAudioEnabled, true);
 * cfg.set(MediaIO_TPG::ConfigTimecodeEnabled, true);
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::Reader);
 * Frame frame;
 * io->readFrame(frame);
 * io->close();
 * delete io;
 * @endcode
 *
 * @par Example — Audio only
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaIO::ConfigType, "TPG");
 * cfg.set(MediaIO_TPG::ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
 * cfg.set(MediaIO_TPG::ConfigAudioEnabled, true);
 * cfg.set(MediaIO_TPG::ConfigAudioMode, "tone");
 * cfg.set(MediaIO_TPG::ConfigAudioToneFrequency, 440.0);
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::Reader);
 * @endcode
 */
class MediaIO_TPG : public MediaIO {
        PROMEKI_OBJECT(MediaIO_TPG, MediaIO)
        public:
                // General
                static const ConfigID ConfigFrameRate;           ///< @brief Frame rate (FrameRate, required).

                // Video
                static const ConfigID ConfigVideoEnabled;        ///< @brief Enable video (bool).
                static const ConfigID ConfigVideoPattern;        ///< @brief Pattern name (String).
                static const ConfigID ConfigVideoWidth;          ///< @brief Frame width (int).
                static const ConfigID ConfigVideoHeight;         ///< @brief Frame height (int).
                static const ConfigID ConfigVideoPixelFormat;    ///< @brief Pixel format (PixelDesc).
                static const ConfigID ConfigVideoSolidColor;     ///< @brief Fill color for SolidColor pattern (Color).
                static const ConfigID ConfigVideoMotion;         ///< @brief Motion speed (double).

                // Audio
                static const ConfigID ConfigAudioEnabled;        ///< @brief Enable audio (bool).
                static const ConfigID ConfigAudioMode;           ///< @brief Audio mode (String).
                static const ConfigID ConfigAudioRate;           ///< @brief Sample rate Hz (float).
                static const ConfigID ConfigAudioChannels;       ///< @brief Channel count (int).
                static const ConfigID ConfigAudioToneFrequency;  ///< @brief Tone frequency Hz (double).
                static const ConfigID ConfigAudioToneLevel;      ///< @brief Tone level dBFS (double).
                static const ConfigID ConfigAudioLtcLevel;       ///< @brief LTC level dBFS (double).
                static const ConfigID ConfigAudioLtcChannel;     ///< @brief LTC channel (int, -1 = all).

                // Timecode
                static const ConfigID ConfigTimecodeEnabled;     ///< @brief Enable timecode (bool).
                static const ConfigID ConfigTimecodeStart;       ///< @brief Start timecode string (String).
                static const ConfigID ConfigTimecodeValue;       ///< @brief Pre-built Timecode (Timecode).
                static const ConfigID ConfigTimecodeDropFrame;   ///< @brief Drop-frame flag (bool).

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc with no file extensions (generator source).
                 */
                static FormatDesc formatDesc();

                /**
                 * @brief Constructs a MediaIO_TPG.
                 * @param parent Optional parent object.
                 */
                MediaIO_TPG(ObjectBase *parent = nullptr) : MediaIO(parent) {}

                /** @brief Destructor. */
                ~MediaIO_TPG() override;

                Error onOpen(Mode mode) override;
                Error onClose() override;
                void setStep(int val) override;
                MediaDesc mediaDesc() const override;
                Error onReadFrame(Frame &frame) override;
                int64_t frameCount() const override;
                uint64_t currentFrame() const override;

        private:
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
                MediaDesc               _mediaDesc;
                uint64_t                _frameCount = 0;
};

PROMEKI_NAMESPACE_END
