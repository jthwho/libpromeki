/**
 * @file      mediaio_audiofile.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaio.h>
#include <promeki/audiofile.h>
#include <promeki/audiodesc.h>
#include <promeki/framerate.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend for audio file formats.
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
 *
 * @par Example — Read a WAV file
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaIO::ConfigFilename, "audio.wav");
 * cfg.set(MediaIO::ConfigType, "AudioFile");
 * cfg.set(MediaIO_AudioFile::ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::Reader);
 * Frame frame;
 * while(io->readFrame(frame).isOk()) {
 *         // process audio frame
 *         frame = Frame();
 * }
 * io->close();
 * delete io;
 * @endcode
 *
 * @par Example — Write a WAV file
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaIO::ConfigFilename, "output.wav");
 * cfg.set(MediaIO::ConfigType, "AudioFile");
 * cfg.set(MediaIO_AudioFile::ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
 * cfg.set(MediaIO_AudioFile::ConfigAudioRate, 48000.0f);
 * cfg.set(MediaIO_AudioFile::ConfigAudioChannels, 2);
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::Writer);
 * io->writeFrame(frame);
 * io->close();
 * delete io;
 * @endcode
 */
class MediaIO_AudioFile : public MediaIO {
        PROMEKI_OBJECT(MediaIO_AudioFile, MediaIO)
        public:
                static const ConfigID ConfigFrameRate;      ///< @brief Frame rate for sample chunking (FrameRate, required).
                static const ConfigID ConfigAudioRate;      ///< @brief Audio sample rate in Hz (float, required for writer).
                static const ConfigID ConfigAudioChannels;  ///< @brief Audio channel count (int, required for writer).

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc covering supported audio file extensions.
                 */
                static FormatDesc formatDesc();

                /**
                 * @brief Constructs a MediaIO_AudioFile.
                 * @param parent Optional parent object.
                 */
                MediaIO_AudioFile(ObjectBase *parent = nullptr) : MediaIO(parent) {}

                /** @brief Destructor. */
                ~MediaIO_AudioFile() override;

                Error onOpen(Mode mode) override;
                Error onClose() override;
                MediaDesc mediaDesc() const override;
                Error setMediaDesc(const MediaDesc &desc) override;
                Error onReadFrame(Frame &frame) override;
                Error onWriteFrame(const Frame &frame) override;
                bool canSeek() const override;
                Error seekToFrame(int64_t frameNumber) override;
                int64_t frameCount() const override;
                uint64_t currentFrame() const override;

        private:
                AudioFile       _audioFile;
                FrameRate       _frameRate;
                MediaDesc       _mediaDesc;
                AudioDesc       _audioDesc;
                size_t          _samplesPerFrame = 0;
                uint64_t        _currentFrame = 0;
                uint64_t        _totalFrames = 0;
};

PROMEKI_NAMESPACE_END
