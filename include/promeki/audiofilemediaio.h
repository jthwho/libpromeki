/**
 * @file      audiofilemediaio.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
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
 * Supported formats: WAV, BWF, AIFF; OGG (Vorbis), FLAC, and MP3
 * are additionally available when the corresponding
 * @c PROMEKI_ENABLE_VORBIS / @c PROMEKI_ENABLE_FLAC /
 * @c PROMEKI_ENABLE_MP3 flag is on for both libpromeki and the
 * vendored libsndfile.
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
                        return String("Audio file formats via libsndfile (WAV, BWF, AIFF"
#if PROMEKI_ENABLE_VORBIS
                                      ", OGG"
#endif
#if PROMEKI_ENABLE_FLAC
                                      ", FLAC"
#endif
#if PROMEKI_ENABLE_MP3
                                      ", MP3"
#endif
                                      ")");
                }
                StringList extensions() const override {
                        // The factory advertises every extension the
                        // libsndfile backend can probe at this build
                        // configuration; runtime feature gating (the
                        // vendored libsndfile's own MPEG/Vorbis/FLAC
                        // toggles) is handled inside
                        // @ref AudioFileFactory_LibSndFile, which
                        // intersects this list against libsndfile's
                        // reported major-format set.  Keeping this
                        // header gated on the @c PROMEKI_ENABLE_*
                        // flags matches the same flags that compile
                        // the codec wiring in.
                        StringList exts{String("wav"), String("bwf"), String("aiff"), String("aif")};
#if PROMEKI_ENABLE_VORBIS
                        exts.pushToBack(String("ogg"));
                        exts.pushToBack(String("oga"));
#endif
#if PROMEKI_ENABLE_FLAC
                        exts.pushToBack(String("flac"));
#endif
#if PROMEKI_ENABLE_MP3
                        exts.pushToBack(String("mp3"));
                        exts.pushToBack(String("mpeg"));
#endif
                        return exts;
                }

                bool canBeSource() const override { return true; }
                bool canBeSink() const override { return true; }

                bool            canHandleDevice(IODevice *device) const override;
                Config::SpecMap configSpecs() const override;
                Metadata        defaultMetadata() const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
