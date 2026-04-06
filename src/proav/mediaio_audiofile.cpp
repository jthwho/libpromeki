/**
 * @file      mediaio_audiofile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstdint>
#include <promeki/mediaio_audiofile.h>
#include <promeki/iodevice.h>
#include <promeki/audio.h>
#include <promeki/frame.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIO_AudioFile)

const MediaIO::ConfigID MediaIO_AudioFile::ConfigFrameRate("FrameRate");
const MediaIO::ConfigID MediaIO_AudioFile::ConfigAudioRate("AudioRate");
const MediaIO::ConfigID MediaIO_AudioFile::ConfigAudioChannels("AudioChannels");

// ============================================================================
// Magic number probing
// ============================================================================

static bool probeAudioDevice(IODevice *device) {
        uint8_t buf[4] = {};
        int64_t n = device->read(buf, 4);
        if(n < 4) return false;

        uint32_t magic = (uint32_t(buf[0]) << 24) |
                         (uint32_t(buf[1]) << 16) |
                         (uint32_t(buf[2]) << 8)  |
                         uint32_t(buf[3]);

        // WAV/BWF: "RIFF" header
        if(magic == 0x52494646) return true;

        // AIFF: "FORM" header
        if(magic == 0x464F524D) return true;

        // OGG: "OggS" header
        if(magic == 0x4F676753) return true;

        return false;
}

// ============================================================================
// FormatDesc
// ============================================================================

MediaIO::FormatDesc MediaIO_AudioFile::formatDesc() {
        return {
                "AudioFile",
                "Audio file formats via libsndfile (WAV, BWF, AIFF, OGG)",
                {"wav", "bwf", "aiff", "aif", "ogg"},
                true,   // canRead
                true,   // canWrite
                [](ObjectBase *parent) -> MediaIO * {
                        return new MediaIO_AudioFile(parent);
                },
                []() -> MediaIO::Config {
                        MediaIO::Config cfg;
                        cfg.set(MediaIO::ConfigType, "AudioFile");
                        cfg.set(ConfigFrameRate, FrameRate(FrameRate::FPS_2997));
                        cfg.set(ConfigAudioRate, 48000.0f);
                        cfg.set(ConfigAudioChannels, 2u);
                        return cfg;
                },
                probeAudioDevice
        };
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIO_AudioFile::~MediaIO_AudioFile() {
        if(isOpen()) close();
}

Error MediaIO_AudioFile::onOpen(Mode mode) {
        const Config &cfg = config();

        // Frame rate is required
        _frameRate = cfg.getAs<FrameRate>(ConfigFrameRate, FrameRate());
        if(!_frameRate.isValid()) {
                promekiErr("MediaIO_AudioFile: frame rate is required");
                return Error::InvalidArgument;
        }

        String filename = cfg.getAs<String>(MediaIO::ConfigFilename);
        if(filename.isEmpty()) {
                promekiErr("MediaIO_AudioFile: filename is required");
                return Error::InvalidArgument;
        }

        if(mode == Reader) {
                _audioFile = AudioFile::createReader(filename);
                if(!_audioFile.isValid()) {
                        promekiErr("MediaIO_AudioFile: failed to create reader for '%s'",
                                filename.cstr());
                        return Error::NotSupported;
                }

                Error err = _audioFile.open();
                if(err.isError()) {
                        promekiErr("MediaIO_AudioFile: open '%s' failed: %s",
                                filename.cstr(), err.name().cstr());
                        return err;
                }

                _audioDesc = _audioFile.desc();
                if(!_audioDesc.isValid()) {
                        promekiErr("MediaIO_AudioFile: invalid audio desc from '%s'",
                                filename.cstr());
                        _audioFile.close();
                        return Error::NotSupported;
                }

                double fpsVal = _frameRate.toDouble();
                _samplesPerFrame = (size_t)std::round(_audioDesc.sampleRate() / fpsVal);

                size_t totalSamples = _audioFile.sampleCount();
                _totalFrames = (_samplesPerFrame > 0 && totalSamples > 0)
                        ? (totalSamples + _samplesPerFrame - 1) / _samplesPerFrame
                        : 0;

                // Build MediaDesc: no images, one audio entry
                _mediaDesc = MediaDesc();
                _mediaDesc.setFrameRate(_frameRate);
                _mediaDesc.audioList().pushToBack(_audioDesc);

        } else { // Writer
                // Get AudioDesc from config fields or previously set MediaDesc
                if(!_mediaDesc.audioList().isEmpty()) {
                        _audioDesc = _mediaDesc.audioList()[0];
                } else {
                        float rate = cfg.getAs<float>(ConfigAudioRate, 0.0f);
                        unsigned int channels = cfg.getAs<unsigned int>(ConfigAudioChannels, 0u);
                        if(rate > 0.0f && channels > 0) {
                                _audioDesc = AudioDesc(rate, channels);
                        }
                }

                if(!_audioDesc.isValid()) {
                        promekiErr("MediaIO_AudioFile: audio desc required for writing");
                        return Error::InvalidArgument;
                }

                _audioFile = AudioFile::createWriter(filename);
                if(!_audioFile.isValid()) {
                        promekiErr("MediaIO_AudioFile: failed to create writer for '%s'",
                                filename.cstr());
                        return Error::NotSupported;
                }

                _audioFile.setDesc(_audioDesc);
                Error err = _audioFile.open();
                if(err.isError()) {
                        promekiErr("MediaIO_AudioFile: open '%s' for write failed: %s",
                                filename.cstr(), err.name().cstr());
                        return err;
                }

                double fpsVal = _frameRate.toDouble();
                _samplesPerFrame = (size_t)std::round(_audioDesc.sampleRate() / fpsVal);

                _mediaDesc = MediaDesc();
                _mediaDesc.setFrameRate(_frameRate);
                _mediaDesc.audioList().pushToBack(_audioDesc);
        }

        _currentFrame = 0;
        return Error::Ok;
}

Error MediaIO_AudioFile::onClose() {
        _audioFile.close();
        _audioFile = AudioFile();
        _audioDesc = AudioDesc();
        _mediaDesc = MediaDesc();
        _frameRate = FrameRate();
        _samplesPerFrame = 0;
        _currentFrame = 0;
        _totalFrames = 0;
        return Error::Ok;
}

// ============================================================================
// Descriptors
// ============================================================================

MediaDesc MediaIO_AudioFile::mediaDesc() const {
        return _mediaDesc;
}

Error MediaIO_AudioFile::setMediaDesc(const MediaDesc &desc) {
        if(isOpen()) return Error::AlreadyOpen;
        _mediaDesc = desc;
        return Error::Ok;
}

// ============================================================================
// Frame I/O
// ============================================================================

Error MediaIO_AudioFile::onReadFrame(Frame &frame) {
        Audio audio;
        Error err = _audioFile.read(audio, _samplesPerFrame);
        if(err.isError()) return err;

        frame.audioList().pushToBack(Audio::Ptr::create(audio));
        _currentFrame++;

        // Advance by step.  The read already moved forward by 1 frame,
        // so we seek by (step - 1) additional frames.
        int s = step();
        if(s != 1) {
                int64_t target = (int64_t)_currentFrame + (s - 1);
                if(target < 0) target = 0;
                _audioFile.seekToSample((size_t)target * _samplesPerFrame);
                _currentFrame = (uint64_t)target;
        }
        return Error::Ok;
}

Error MediaIO_AudioFile::onWriteFrame(const Frame &frame) {
        if(frame.audioList().isEmpty()) {
                promekiWarn("MediaIO_AudioFile: writeFrame called with no audio");
                return Error::InvalidArgument;
        }

        const Audio &audio = *frame.audioList()[0];
        Error err = _audioFile.write(audio);
        if(err.isError()) return err;

        _currentFrame++;
        return Error::Ok;
}

// ============================================================================
// Seeking
// ============================================================================

bool MediaIO_AudioFile::canSeek() const {
        return isOpen() && mode() == Reader;
}

Error MediaIO_AudioFile::seekToFrame(int64_t frameNumber) {
        if(!isOpen()) return Error::NotOpen;
        if(mode() != Reader) return Error::IllegalSeek;
        size_t targetSample = frameNumber * _samplesPerFrame;
        Error err = _audioFile.seekToSample(targetSample);
        if(err.isError()) return err;
        _currentFrame = frameNumber;
        return Error::Ok;
}

int64_t MediaIO_AudioFile::frameCount() const {
        if(!isOpen()) return 0;
        if(mode() == Writer) return _currentFrame;
        return _totalFrames;
}

uint64_t MediaIO_AudioFile::currentFrame() const {
        return _currentFrame;
}

PROMEKI_NAMESPACE_END
