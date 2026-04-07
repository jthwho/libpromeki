/**
 * @file      mediaiotask_audiofile.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/audiofile.h>
#include <promeki/audiodesc.h>
#include <promeki/framerate.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend for audio file formats.
 * @ingroup proav
 *
 * Wraps the AudioFile subsystem (backed by libsndfile) to provide
 * frame-based audio I/O through the MediaIO interface.  Audio
 * samples are chunked into frame-sized blocks according to the
 * configured frame rate.
 *
 * Supported formats: WAV, BWF, AIFF, OGG.
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | ConfigFilename | String | — | File path (inherited from MediaIO). |
 * | ConfigFrameRate | FrameRate | 29.97 | Frame rate for sample chunking (required). |
 * | ConfigAudioRate | float | 48000 | Audio sample rate Hz (required for writer). |
 * | ConfigAudioChannels | int | 2 | Audio channel count (required for writer). |
 */
class MediaIOTask_AudioFile : public MediaIOTask {
        public:
                static const MediaIO::ConfigID ConfigFrameRate;      ///< @brief Frame rate (FrameRate, required).
                static const MediaIO::ConfigID ConfigAudioRate;      ///< @brief Sample rate Hz (float, required for writer).
                static const MediaIO::ConfigID ConfigAudioChannels;  ///< @brief Channel count (int, required for writer).

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc covering supported audio file extensions.
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_AudioFile. */
                MediaIOTask_AudioFile() = default;

                /** @brief Destructor. */
                ~MediaIOTask_AudioFile() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandSeek &cmd) override;

                AudioFile       _audioFile;
                FrameRate       _frameRate;
                AudioDesc       _audioDesc;
                MediaIOMode     _mode = MediaIO_NotOpen;
                size_t          _samplesPerFrame = 0;
                int64_t         _currentFrame = 0;
                int64_t         _totalFrames = 0;
};

PROMEKI_NAMESPACE_END
