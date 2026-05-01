/**
 * @file      audiofilemediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/audiodesc.h>
#include <promeki/audiofile.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/mediaiofactory.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend for audio file formats.
 * @ingroup proav
 *
 * Wraps the @ref AudioFile subsystem (backed by libsndfile) to provide
 * frame-based audio I/O through the MediaIO interface.  Audio samples
 * are chunked into frame-sized blocks according to the configured
 * frame rate.
 *
 * Supported formats: WAV, BWF, AIFF, OGG.
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::Filename      | String    | — | File path. |
 * | @ref MediaConfig::FrameRate     | FrameRate | 29.97 | Frame rate for sample chunking. |
 * | @ref MediaConfig::AudioRate     | float     | 48000 | Audio sample rate Hz (required for writer). |
 * | @ref MediaConfig::AudioChannels | int       | 2     | Audio channel count (required for writer). |
 *
 * @par Threading
 * Runs on a per-instance dedicated worker thread inherited from
 * @ref DedicatedThreadMediaIO so blocking libsndfile syscalls cannot
 * starve the shared pool.
 */
class AudioFileMediaIO : public DedicatedThreadMediaIO {
                PROMEKI_OBJECT(AudioFileMediaIO, DedicatedThreadMediaIO)
        public:
                /** @brief Constructs an AudioFileMediaIO. */
                AudioFileMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~AudioFileMediaIO() override;

                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandSeek &cmd) override;

        private:
                // Returns the AudioFormat::ID libsndfile prefers for
                // @p filename's extension, picking the closest form to
                // @p source so the inserted SRC bridge does not drop bit
                // depth (e.g. 24-bit source stays 24-bit through a BWF
                // write).
                AudioFormat::ID preferredWriterDataType(const String &filename, AudioFormat::ID source) const;

                AudioFile   _audioFile;
                FrameRate   _frameRate;
                AudioDesc   _audioDesc;
                bool        _isOpen = false;
                bool        _isWrite = false;
                size_t      _samplesPerFrame = 0;
                FrameNumber _currentFrame{0};
                FrameCount  _totalFrames{0};
};

/**
 * @brief @ref MediaIOFactory for the AudioFile backend.
 * @ingroup proav
 */
class AudioFileFactory : public MediaIOFactory {
        public:
                AudioFileFactory() = default;

                String name() const override { return String("AudioFile"); }
                String displayName() const override { return String("Audio File"); }
                String description() const override {
                        return String("Audio file formats via libsndfile (WAV, BWF, AIFF, OGG)");
                }
                StringList extensions() const override {
                        return {String("wav"), String("bwf"), String("aiff"), String("aif"), String("ogg")};
                }

                bool canBeSource() const override { return true; }
                bool canBeSink() const override { return true; }

                bool            canHandleDevice(IODevice *device) const override;
                Config::SpecMap configSpecs() const override;
                Metadata        defaultMetadata() const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END
