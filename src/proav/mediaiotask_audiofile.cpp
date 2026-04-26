/**
 * @file      mediaiotask_audiofile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <cstdint>
#include <promeki/mediaiotask_audiofile.h>
#include <promeki/iodevice.h>
#include <promeki/frame.h>
#include <promeki/audiopayload.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_AudioFile)

// ============================================================================
// Magic number probing
// ============================================================================

static bool probeAudioDevice(IODevice *device) {
        uint8_t buf[4] = {};
        int64_t n = device->read(buf, 4);
        if (n < 4) return false;

        uint32_t magic =
                (uint32_t(buf[0]) << 24) | (uint32_t(buf[1]) << 16) | (uint32_t(buf[2]) << 8) | uint32_t(buf[3]);

        // WAV/BWF: "RIFF" header
        if (magic == 0x52494646) return true;

        // AIFF: "FORM" header
        if (magic == 0x464F524D) return true;

        // OGG: "OggS" header
        if (magic == 0x4F676753) return true;

        return false;
}

// ============================================================================
// FormatDesc
// ============================================================================

MediaIO::FormatDesc MediaIOTask_AudioFile::formatDesc() {
        return {"AudioFile",
                "Audio File",
                "Audio file formats via libsndfile (WAV, BWF, AIFF, OGG)",
                {"wav", "bwf", "aiff", "aif", "ogg"},
                true,  // canBeSource
                true,  // canBeSink
                false, // canBeTransform
                []() -> MediaIOTask * { return new MediaIOTask_AudioFile(); },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto                     s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        s(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_29_97));
                        s(MediaConfig::AudioRate, 48000.0f);
                        s(MediaConfig::AudioChannels, int32_t(2));
                        return specs;
                },
                []() -> Metadata {
                        // libsndfile INFO chunk keys (WAV/AIFF/OGG)
                        // plus the BWF extension block the backend
                        // emits when EnableBWF is true.  Values are
                        // empty strings / false / Invalid by default;
                        // callers fill in whatever they care about via
                        // MediaIO::setMetadata().
                        Metadata m;
                        m.set(Metadata::Title, String());
                        m.set(Metadata::Copyright, String());
                        m.set(Metadata::Software, String());
                        m.set(Metadata::Artist, String());
                        m.set(Metadata::Comment, String());
                        m.set(Metadata::Date, String());
                        m.set(Metadata::Album, String());
                        m.set(Metadata::License, String());
                        m.set(Metadata::TrackNumber, String());
                        m.set(Metadata::Genre, String());
                        // BWF extension (opt-in via EnableBWF).
                        m.set(Metadata::EnableBWF, false);
                        m.set(Metadata::Description, String());
                        m.set(Metadata::Originator, String());
                        m.set(Metadata::OriginatorReference, String());
                        m.set(Metadata::OriginationDateTime, String());
                        m.set(Metadata::CodingHistory, String());
                        m.set(Metadata::UMID, String());
                        m.set(Metadata::Timecode, Timecode());
                        return m;
                },
                probeAudioDevice};
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIOTask_AudioFile::~MediaIOTask_AudioFile() = default;

Error MediaIOTask_AudioFile::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        // Frame rate is required
        _frameRate = cfg.getAs<FrameRate>(MediaConfig::FrameRate, FrameRate());
        if (!_frameRate.isValid()) {
                promekiErr("MediaIOTask_AudioFile: frame rate is required");
                return Error::InvalidArgument;
        }

        String filename = cfg.getAs<String>(MediaConfig::Filename);
        if (filename.isEmpty()) {
                promekiErr("MediaIOTask_AudioFile: filename is required");
                return Error::InvalidArgument;
        }

        _mode = cmd.mode;
        MediaDesc mediaDesc;

        if (cmd.mode == MediaIO::Source) {
                _audioFile = AudioFile::createReader(filename);
                if (!_audioFile.isValid()) {
                        promekiErr("MediaIOTask_AudioFile: failed to create reader for '%s'", filename.cstr());
                        return Error::NotSupported;
                }

                Error err = _audioFile.open();
                if (err.isError()) {
                        promekiErr("MediaIOTask_AudioFile: open '%s' failed: %s", filename.cstr(), err.name().cstr());
                        return err;
                }

                _audioDesc = _audioFile.desc();
                if (!_audioDesc.isValid()) {
                        promekiErr("MediaIOTask_AudioFile: invalid audio desc from '%s'", filename.cstr());
                        _audioFile.close();
                        return Error::NotSupported;
                }

                double fpsVal = _frameRate.toDouble();
                _samplesPerFrame = (size_t)std::round(_audioDesc.sampleRate() / fpsVal);

                size_t  totalSamples = _audioFile.sampleCount();
                int64_t total = (_samplesPerFrame > 0 && totalSamples > 0)
                                        ? static_cast<int64_t>((totalSamples + _samplesPerFrame - 1) / _samplesPerFrame)
                                        : 0;
                _totalFrames = FrameCount(total);

                mediaDesc.setFrameRate(_frameRate);
                mediaDesc.audioList().pushToBack(_audioDesc);

                cmd.frameCount = _totalFrames;
                cmd.canSeek = true;
        } else { // Writer
                // Get AudioDesc from pendingAudioDesc, pendingMediaDesc, or config fields
                if (cmd.pendingAudioDesc.isValid()) {
                        _audioDesc = cmd.pendingAudioDesc;
                } else if (!cmd.pendingMediaDesc.audioList().isEmpty()) {
                        _audioDesc = cmd.pendingMediaDesc.audioList()[0];
                } else {
                        float        rate = cfg.getAs<float>(MediaConfig::AudioRate, 0.0f);
                        unsigned int channels = cfg.getAs<unsigned int>(MediaConfig::AudioChannels, 0u);
                        if (rate > 0.0f && channels > 0) {
                                _audioDesc = AudioDesc(rate, channels);
                        }
                }

                if (!_audioDesc.isValid()) {
                        promekiErr("MediaIOTask_AudioFile: audio desc required for writing");
                        return Error::InvalidArgument;
                }

                _audioFile = AudioFile::createWriter(filename);
                if (!_audioFile.isValid()) {
                        promekiErr("MediaIOTask_AudioFile: failed to create writer for '%s'", filename.cstr());
                        return Error::NotSupported;
                }

                // Fold the MediaIO container metadata (which MediaIO
                // has already stamped with the libpromeki write
                // defaults) into the AudioDesc's metadata so the
                // libsndfile backend can emit it as BWF/INFO chunks.
                // Any entries already present on the AudioDesc win
                // over the container-supplied values.
                if (!cmd.pendingMetadata.isEmpty()) {
                        Metadata merged = cmd.pendingMetadata;
                        merged.merge(_audioDesc.metadata());
                        _audioDesc.metadata() = std::move(merged);
                }

                _audioFile.setDesc(_audioDesc);
                Error err = _audioFile.open();
                if (err.isError()) {
                        promekiErr("MediaIOTask_AudioFile: open '%s' for write failed: %s", filename.cstr(),
                                   err.name().cstr());
                        return err;
                }

                double fpsVal = _frameRate.toDouble();
                _samplesPerFrame = (size_t)std::round(_audioDesc.sampleRate() / fpsVal);

                mediaDesc.setFrameRate(_frameRate);
                mediaDesc.audioList().pushToBack(_audioDesc);

                cmd.frameCount = 0;
                cmd.canSeek = false;
        }

        _currentFrame = 0;
        cmd.mediaDesc = mediaDesc;
        cmd.audioDesc = _audioDesc;
        cmd.frameRate = _frameRate;
        if (cmd.mode == MediaIO::Sink) {
                // Surface the merged AudioDesc metadata (caller values
                // plus MediaIO write defaults) as the container
                // metadata so MediaIO can cache it and clients can
                // read it back via MediaIO::metadata().
                cmd.metadata = _audioDesc.metadata();
        }
        return Error::Ok;
}

Error MediaIOTask_AudioFile::executeCmd(MediaIOCommandClose &cmd) {
        _audioFile.close();
        _audioFile = AudioFile();
        _audioDesc = AudioDesc();
        _frameRate = FrameRate();
        _mode = MediaIO_NotOpen;
        _samplesPerFrame = 0;
        _currentFrame = 0;
        _totalFrames = 0;
        return Error::Ok;
}

Error MediaIOTask_AudioFile::executeCmd(MediaIOCommandRead &cmd) {
        stampWorkBegin();
        PcmAudioPayload::Ptr payload;
        Error                err = _audioFile.read(payload, _samplesPerFrame);
        if (err.isError()) {
                stampWorkEnd();
                return err;
        }

        cmd.frame = Frame::Ptr::create();
        if (payload.isValid()) cmd.frame.modify()->addPayload(payload);
        ++_currentFrame;

        // Advance by step.  The read already moved forward by 1 frame,
        // so we seek by (step - 1) additional frames.
        int s = cmd.step;
        if (s != 1) {
                FrameNumber target = _currentFrame + int64_t(s - 1);
                if (!target.isValid()) target = FrameNumber(0);
                _audioFile.seekToSample(static_cast<size_t>(target.value()) * _samplesPerFrame);
                _currentFrame = target;
        }
        cmd.currentFrame = _currentFrame;
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_AudioFile::executeCmd(MediaIOCommandWrite &cmd) {
        auto auds = cmd.frame->audioPayloads();
        if (auds.isEmpty()) {
                promekiWarn("MediaIOTask_AudioFile: write with no audio");
                return Error::InvalidArgument;
        }
        stampWorkBegin();
        const auto *uap = auds[0]->as<PcmAudioPayload>();
        if (uap == nullptr) {
                stampWorkEnd();
                return Error::InvalidArgument;
        }
        Error err = _audioFile.write(*uap);
        if (err.isError()) {
                stampWorkEnd();
                return err;
        }
        ++_currentFrame;
        cmd.currentFrame = _currentFrame;
        cmd.frameCount = toFrameCount(_currentFrame);
        stampWorkEnd();
        return Error::Ok;
}

Error MediaIOTask_AudioFile::executeCmd(MediaIOCommandSeek &cmd) {
        if (_mode != MediaIO::Source) return Error::IllegalSeek;
        FrameNumber target = cmd.frameNumber.isValid() ? cmd.frameNumber : FrameNumber(0);
        size_t      targetSample = static_cast<size_t>(target.value()) * _samplesPerFrame;
        Error       err = _audioFile.seekToSample(targetSample);
        if (err.isError()) return err;
        _currentFrame = target;
        cmd.currentFrame = _currentFrame;
        return Error::Ok;
}

// ---- Phase 3 introspection / negotiation overrides ----

namespace {

        String extractAudioExt(const String &path) {
                const size_t dot = path.rfind('.');
                if (dot == String::npos || dot + 1 >= path.size()) return String();
                return path.mid(dot + 1).toLower();
        }

} // namespace

AudioFormat::ID MediaIOTask_AudioFile::preferredWriterDataType(const String &filename, AudioFormat::ID source) const {
        const String ext = extractAudioExt(filename);
        if (ext.isEmpty()) return AudioFormat::Invalid;

        // OGG / Opus / Vorbis are float-pipeline codecs — keep float
        // unconditionally so libsndfile doesn't have to round-trip
        // through int internally.
        if (ext == "ogg" || ext == "oga" || ext == "opus") {
                return AudioFormat::PCMI_Float32LE;
        }

        // WAV / BWF / AIFF / FLAC / W64 / RF64: integer-friendly
        // PCM containers.  Pass through the source's data type when
        // libsndfile can store it directly; otherwise fall back to
        // 24-bit signed little-endian (the production-typical form).
        if (ext == "wav" || ext == "bwf" || ext == "aiff" || ext == "aif" || ext == "flac" || ext == "w64" ||
            ext == "rf64") {
                switch (source) {
                        case AudioFormat::PCMI_S16LE:
                        case AudioFormat::PCMI_S16BE:
                        case AudioFormat::PCMI_S24LE:
                        case AudioFormat::PCMI_S24BE:
                        case AudioFormat::PCMI_S32LE:
                        case AudioFormat::PCMI_S32BE:
                        case AudioFormat::PCMI_Float32LE:
                        case AudioFormat::PCMI_Float32BE: return source; // pass-through
                        default: return AudioFormat::PCMI_S24LE;
                }
        }

        // Unknown extension — let the default propose nothing.
        return AudioFormat::Invalid;
}

Error MediaIOTask_AudioFile::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.audioList().isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        const MediaIO         *io = mediaIo();
        const MediaIO::Config &cfg = (io != nullptr) ? io->config() : MediaIO::Config();
        const String           filename =
                cfg.contains(MediaConfig::Filename) ? cfg.getAs<String>(MediaConfig::Filename) : String();

        MediaDesc want = offered;
        bool      anyChanged = false;
        for (size_t i = 0; i < want.audioList().size(); ++i) {
                AudioDesc            &ad = want.audioList()[i];
                const AudioFormat::ID target = preferredWriterDataType(filename, ad.format().id());
                if (target == AudioFormat::Invalid) continue;
                if (target == ad.format().id()) continue;
                ad.setFormat(target);
                anyChanged = true;
        }

        if (!anyChanged) {
                *preferred = offered;
                return Error::Ok;
        }

        *preferred = want;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
