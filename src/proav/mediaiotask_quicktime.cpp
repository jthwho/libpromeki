/**
 * @file      mediaiotask_quicktime.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <promeki/mediaiotask_quicktime.h>
#include <promeki/enums.h>
#include <promeki/iodevice.h>
#include <promeki/image.h>
#include <promeki/frame.h>
#include <promeki/audio.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_QuickTime)

// ============================================================================
// Probe — looks for an "ftyp" atom with a recognized brand in the first
// 16 bytes of the device. Layout: [4B size][4B 'ftyp'][4B major brand]...
// ============================================================================

namespace {

/**
 * @brief Picks a QuickTime-compatible storage AudioDesc for the given source.
 *
 * QuickTime's canonical PCM codec tags are @c sowt / @c twos (s16),
 * @c in24 / @c in32 (big-endian), @c fl32 / @c fl64 (big-endian float),
 * and @c raw (unsigned 8-bit). Little-endian float does not have a
 * single-FourCC mapping without the @c pcmC extension atom (which we
 * don't currently emit), so sources in that format are promoted to
 * @c PCMI_S16LE for storage — the most broadly compatible PCM layout.
 *
 * @fixme Little-endian float is being stored lossily (promoted to s16le).
 *        Proper fix: either emit an @c lpcm sample entry with a @c pcmC
 *        extension atom describing endianness + sample-is-float, or
 *        byte-swap to @c PCMI_Float32BE and use the @c fl32 FourCC.
 *        Either preserves bit depth; the current promotion is a 32-bit
 *        → 16-bit quality loss. See devplan/fixme.md entry
 *        "QuickTime: Little-Endian Float Audio Storage".
 */
AudioDesc pickStorageFormat(const AudioDesc &src) {
        if(!src.isValid()) return src;
        switch(src.dataType()) {
                case AudioDesc::PCMI_Float32LE:
                        // FIXME: promotes 32-bit float to 16-bit int, losing precision.
                        return AudioDesc(AudioDesc::PCMI_S16LE, src.sampleRate(), src.channels());
                default:
                        return src;
        }
}

bool isRecognizedBrand(uint32_t brand) {
        switch(brand) {
                case 0x71742020: // 'qt  '
                case 0x69736f6d: // 'isom'
                case 0x6d703431: // 'mp41'
                case 0x6d703432: // 'mp42'
                case 0x69736f32: // 'iso2'
                case 0x69736f33: // 'iso3'
                case 0x69736f34: // 'iso4'
                case 0x69736f35: // 'iso5'
                case 0x69736f36: // 'iso6'
                case 0x69736f37: // 'iso7'
                case 0x69736f38: // 'iso8'
                case 0x6d345620: // 'm4V '
                case 0x4d345620: // 'M4V '
                case 0x66347620: // 'f4v '
                        return true;
                default:
                        return false;
        }
}

bool probeQuickTimeDevice(IODevice *device) {
        uint8_t buf[16] = {};
        int64_t n = device->read(buf, 16);
        if(n < 12) return false;

        uint32_t boxType = (uint32_t(buf[4]) << 24) |
                           (uint32_t(buf[5]) << 16) |
                           (uint32_t(buf[6]) <<  8) |
                            uint32_t(buf[7]);
        if(boxType != 0x66747970) return false; // 'ftyp'

        uint32_t major = (uint32_t(buf[8])  << 24) |
                         (uint32_t(buf[9])  << 16) |
                         (uint32_t(buf[10]) <<  8) |
                          uint32_t(buf[11]);
        return isRecognizedBrand(major);
}

} // namespace

// ============================================================================
// FormatDesc
// ============================================================================

MediaIO::FormatDesc MediaIOTask_QuickTime::formatDesc() {
        return {
                "QuickTime",
                "QuickTime / ISO-BMFF container files (.mov, .mp4, .m4v)",
                {"mov", "qt", "mp4", "m4v"},
                true,    // canOutput
                true,    // canInput
                false,   // canInputAndOutput
                []() -> MediaIOTask * {
                        return new MediaIOTask_QuickTime();
                },
                []() -> MediaIO::Config::SpecMap {
                        MediaIO::Config::SpecMap specs;
                        auto s = [&specs](MediaConfig::ID id, const Variant &def) {
                                const VariantSpec *gs = MediaConfig::spec(id);
                                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
                        };
                        // -1 sentinel = "let the reader pick the
                        // first video / audio track it finds".
                        s(MediaConfig::VideoTrack, int32_t(-1));
                        s(MediaConfig::AudioTrack, int32_t(-1));
                        // Writer defaults — fragmented layout is the
                        // crash-safe choice for live capture, classic
                        // is only appropriate for offline rendering.
                        s(MediaConfig::QuickTimeLayout, QuickTimeLayout::Fragmented);
                        s(MediaConfig::QuickTimeFragmentFrames, int32_t(DefaultFragmentFrames));
                        s(MediaConfig::QuickTimeFlushSync, false);
                        return specs;
                },
                []() -> Metadata {
                        // udta ©-atom set written via addIfPresent()
                        // in quicktime_writer.cpp, plus the BWF-style
                        // extension fields the writer stamps at open
                        // time (Originator, OriginatorReference,
                        // OriginationDateTime, UMID).  Timecode comes
                        // off each frame rather than the container,
                        // but it's listed here too so callers can set
                        // an initial timecode if the producer doesn't
                        // stamp one.
                        Metadata m;
                        m.set(Metadata::Title,               String());
                        m.set(Metadata::Comment,             String());
                        m.set(Metadata::Date,                String());
                        m.set(Metadata::Artist,              String());
                        m.set(Metadata::Copyright,           String());
                        m.set(Metadata::Software,            String());
                        m.set(Metadata::Album,               String());
                        m.set(Metadata::Genre,               String());
                        m.set(Metadata::Description,         String());
                        m.set(Metadata::Originator,          String());
                        m.set(Metadata::OriginatorReference, String());
                        m.set(Metadata::OriginationDateTime, String());
                        m.set(Metadata::UMID,                String());
                        m.set(Metadata::Timecode,            Timecode());
                        return m;
                },
                probeQuickTimeDevice
        };
}

// ============================================================================
// Lifecycle
// ============================================================================

MediaIOTask_QuickTime::~MediaIOTask_QuickTime() = default;

Error MediaIOTask_QuickTime::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        _filename = cfg.getAs<String>(MediaConfig::Filename);
        if(_filename.isEmpty()) {
                promekiErr("MediaIOTask_QuickTime: filename is required");
                return Error::InvalidArgument;
        }
        _mode = cmd.mode;

        if(cmd.mode == MediaIO::Output) {
                _qt = QuickTime::createReader(_filename);
                Error err = _qt.open();
                if(err.isError()) {
                        promekiErr("MediaIOTask_QuickTime: open '%s' failed: %s",
                                   _filename.cstr(), err.name().cstr());
                        return err;
                }

                // Pick the primary video and audio track indices.
                int videoIdx = cfg.getAs<int>(MediaConfig::VideoTrack, -1);
                int audioIdx = cfg.getAs<int>(MediaConfig::AudioTrack, -1);
                if(videoIdx < 0) {
                        for(size_t i = 0; i < _qt.tracks().size(); ++i) {
                                if(_qt.tracks()[i].type() == QuickTime::Video) {
                                        videoIdx = static_cast<int>(i);
                                        break;
                                }
                        }
                }
                if(audioIdx < 0) {
                        for(size_t i = 0; i < _qt.tracks().size(); ++i) {
                                if(_qt.tracks()[i].type() == QuickTime::Audio) {
                                        audioIdx = static_cast<int>(i);
                                        break;
                                }
                        }
                }
                _videoTrackIndex = videoIdx;
                _audioTrackIndex = audioIdx;

                if(_videoTrackIndex < 0 && _audioTrackIndex < 0) {
                        promekiErr("MediaIOTask_QuickTime: '%s' has no video or audio tracks",
                                   _filename.cstr());
                        return Error::NotSupported;
                }

                // Accept both PCM and compressed audio tracks. For PCM the
                // reader produces an AudioDesc with a valid DataType; for
                // compressed codecs (AAC, Opus, etc.) the reader sets
                // codecFourCC() and leaves DataType = Invalid. In both
                // cases AudioDesc::isValid() is true and we pass samples
                // through to the consumer.
                if(_audioTrackIndex >= 0) {
                        const QuickTime::Track &at = _qt.tracks()[_audioTrackIndex];
                        if(!at.audioDesc().isValid()) {
                                promekiErr("MediaIOTask_QuickTime: audio track has no AudioDesc");
                                return Error::NotSupported;
                        }
                        _audioDesc = at.audioDesc();
                }

                // Build the MediaDesc.
                MediaDesc mediaDesc;
                if(_videoTrackIndex >= 0) {
                        const QuickTime::Track &vt = _qt.tracks()[_videoTrackIndex];
                        ImageDesc idesc(vt.size(), vt.pixelDesc());
                        mediaDesc.imageList().pushToBack(idesc);
                        if(vt.frameRate().isValid()) {
                                mediaDesc.setFrameRate(vt.frameRate());
                                _frameRate = vt.frameRate();
                                _frameCount = static_cast<int64_t>(vt.sampleCount());
                        }
                }
                if(_audioTrackIndex >= 0) {
                        mediaDesc.audioList().pushToBack(_audioDesc);
                }
                if(!mediaDesc.frameRate().isValid() && _audioTrackIndex >= 0) {
                        // Audio-only fallback: use a synthetic 1/1 rate so MediaIO
                        // has something coherent to report.
                        _frameRate = FrameRate(FrameRate::RationalType(1, 1));
                        mediaDesc.setFrameRate(_frameRate);
                }
                mediaDesc.metadata() = _qt.containerMetadata();

                _anchorTimecode    = _qt.startTimecode();
                _audioSampleCursor = 0;
                _currentFrame      = 0;

                cmd.mediaDesc  = mediaDesc;
                cmd.frameRate  = mediaDesc.frameRate();
                cmd.metadata   = _qt.containerMetadata();
                if(_audioTrackIndex >= 0) {
                        cmd.audioDesc = _audioDesc;
                }
                cmd.frameCount = _frameCount;
                cmd.canSeek    = true;
                return Error::Ok;
        }

        // ---- Writer ----
        _qt = QuickTime::createWriter(_filename);

        // Pick a layout. Default is fragmented for crash safety.
        // The QuickTimeLayout Enum integer values match
        // QuickTime::Layout by construction so we can cast directly.
        Error layoutErr;
        Enum layoutEnum = cfg.get(MediaConfig::QuickTimeLayout)
                             .asEnum(QuickTimeLayout::Type, &layoutErr);
        if(layoutErr.isError() || !layoutEnum.hasListedValue()) {
                promekiErr("MediaIOTask_QuickTime: unknown Layout value");
                return Error::InvalidArgument;
        }
        QuickTime::Layout layout = static_cast<QuickTime::Layout>(layoutEnum.value());
        Error err = _qt.setLayout(layout);
        if(err.isError()) return err;

        _writerFragmentFrames = cfg.getAs<int>(MediaConfig::QuickTimeFragmentFrames, DefaultFragmentFrames);
        if(_writerFragmentFrames < 1) _writerFragmentFrames = DefaultFragmentFrames;
        _writerFramesSinceFlush = 0;

        // Optional durable-flush mode: fdatasync after every fragment.
        _qt.setFlushSync(cfg.getAs<bool>(MediaConfig::QuickTimeFlushSync, false));

        err = _qt.open();
        if(err.isError()) {
                promekiErr("MediaIOTask_QuickTime: open writer '%s' failed: %s",
                           _filename.cstr(), err.name().cstr());
                return err;
        }
        // Hand the container metadata (which MediaIO has already
        // stamped with the libpromeki write defaults) to the
        // QuickTime engine so it can be surfaced in the output
        // file's metadata box once udta serialization lands.
        _qt.setContainerMetadata(cmd.pendingMetadata);
        _writerTracksRegistered = false;
        _writerFrameCount       = 0;
        _writerVideoTrackId     = 0;
        _writerAudioTrackId     = 0;
        _writerTimecodeTrackId  = 0;
        _writerAudioFifo        = AudioBuffer();
        _writerAudioStorage     = AudioDesc();

        // Register tracks up front if the caller supplied a full
        // pendingMediaDesc (video description + optional audio list).
        if(cmd.pendingMediaDesc.frameRate().isValid()) {
                _frameRate = cmd.pendingMediaDesc.frameRate();
        }
        if(!cmd.pendingMediaDesc.imageList().isEmpty() && _frameRate.isValid()) {
                const ImageDesc &idesc = cmd.pendingMediaDesc.imageList()[0];
                uint32_t vid = 0;
                err = _qt.addVideoTrack(idesc.pixelDesc(), idesc.size(),
                                        _frameRate, &vid);
                if(err.isError()) return err;
                _writerVideoTrackId = vid;
        }
        if(!cmd.pendingMediaDesc.audioList().isEmpty() && _frameRate.isValid()) {
                const AudioDesc &srcDesc = cmd.pendingMediaDesc.audioList()[0];
                AudioDesc storage = pickStorageFormat(srcDesc);
                uint32_t aid = 0;
                err = _qt.addAudioTrack(storage, &aid);
                if(err.isError()) return err;
                _writerAudioTrackId = aid;
                _writerAudioStorage = storage;
                _writerAudioFifo    = AudioBuffer(storage);
                _writerAudioFifo.setInputFormat(srcDesc);
                // Reserve ~1 second of headroom to absorb frame-rate jitter.
                _writerAudioFifo.reserve(static_cast<size_t>(storage.sampleRate()));
        }
        if(!cmd.pendingMediaDesc.imageList().isEmpty() && _frameRate.isValid()) {
                _writerTracksRegistered = true;
        }

        cmd.mediaDesc  = cmd.pendingMediaDesc;
        cmd.metadata   = cmd.pendingMetadata;
        cmd.frameRate  = _frameRate;
        cmd.frameCount = 0;
        cmd.canSeek    = false;
        return Error::Ok;
}

Error MediaIOTask_QuickTime::executeCmd(MediaIOCommandClose & /*cmd*/) {
        if(_mode == MediaIO::Input && _qt.isOpen()) {
                // Drain any tail audio remaining in the FIFO before finalize.
                drainWriterAudio(/*flush=*/true);
                Error err = _qt.finalize();
                if(err.isError()) {
                        promekiWarn("MediaIOTask_QuickTime: finalize failed: %s", err.name().cstr());
                }
        }
        _qt.close();
        _qt = QuickTime();
        _mode             = MediaIO_NotOpen;
        _filename.clear();
        _videoTrackIndex  = -1;
        _audioTrackIndex  = -1;
        _currentFrame     = 0;
        _frameCount       = 0;
        _frameRate        = FrameRate();
        _anchorTimecode   = Timecode();
        _audioSampleCursor = 0;
        _audioDesc         = AudioDesc();
        _writerTracksRegistered = false;
        _writerVideoTrackId     = 0;
        _writerAudioTrackId     = 0;
        _writerTimecodeTrackId  = 0;
        _writerFrameCount       = 0;
        _writerFramesSinceFlush = 0;
        _writerFragmentFrames   = DefaultFragmentFrames;
        _writerAudioFifo        = AudioBuffer();
        _writerAudioStorage     = AudioDesc();
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Read
// ----------------------------------------------------------------------------

Error MediaIOTask_QuickTime::readVideoFrame(uint64_t frameIndex, Frame::Ptr &outFrame) {
        if(_videoTrackIndex < 0) return Error::NotSupported;

        QuickTime::Sample s;
        Error err = _qt.readSample(static_cast<size_t>(_videoTrackIndex), frameIndex, s);
        if(err.isError()) return err;
        if(!s.data.isValid()) return Error::IOError;

        const QuickTime::Track &vt = _qt.tracks()[_videoTrackIndex];

        // Build the Frame and attach the Image. Compressed codecs flow
        // through Image::fromCompressedData; uncompressed packed YUV /
        // raw RGB end up as raster Images via Image(ImageDesc).
        Frame::Ptr frame = Frame::Ptr::create();

        // Zero-copy Image construction: adopt the sample Buffer::Ptr
        // directly as plane 0. Works for both compressed codecs and
        // single-plane uncompressed formats (2vuy, v210, packed RGB).
        // Multi-plane (planar YUV) formats would need a separate path
        // — none of the currently-supported QuickTime video codecs hit
        // that case.
        Image img = Image::fromBuffer(s.data, vt.size().width(), vt.size().height(),
                                      vt.pixelDesc(), vt.metadata());
        if(!img.isValid()) {
                promekiWarn("MediaIOTask_QuickTime: failed to wrap video sample %llu as Image",
                            static_cast<unsigned long long>(frameIndex));
                return Error::DecodeFailed;
        }
        frame.modify()->imageList().pushToBack(Image::Ptr::create(img));

        // Frame metadata: timecode (anchor + frame offset), frame number,
        // keyframe flag.
        Metadata &fmeta = frame.modify()->metadata();
        fmeta.set(Metadata::FrameNumber, static_cast<int64_t>(frameIndex));
        fmeta.set(Metadata::FrameKeyframe, s.keyframe);
        if(_anchorTimecode.isValid()) {
                auto [anchorFrame, fnErr] = _anchorTimecode.toFrameNumber();
                if(!fnErr.isError()) {
                        Timecode tc = Timecode::fromFrameNumber(_anchorTimecode.mode(),
                                anchorFrame + frameIndex);
                        if(tc.isValid()) fmeta.set(Metadata::Timecode, tc);
                }
        }

        outFrame = frame;
        return Error::Ok;
}

Error MediaIOTask_QuickTime::readAudioSlice(uint64_t startSample, size_t samples, Audio &out) {
        if(_audioTrackIndex < 0) return Error::NotSupported;
        if(samples == 0) { out = Audio(); return Error::Ok; }

        QuickTime::Sample range;
        Error err = _qt.readSampleRange(static_cast<size_t>(_audioTrackIndex),
                                        startSample, samples, range);
        if(err.isError()) return err;
        if(!range.data.isValid()) return Error::IOError;

        // Zero-copy: adopt the range buffer directly as the Audio's data.
        // For PCM audio the Audio's samples() is derived from buffer size /
        // stride; for compressed audio it's a blob of encoded bytes.
        out = Audio::fromBuffer(range.data, _audioDesc);
        if(!out.isValid()) return Error::DecodeFailed;
        return Error::Ok;
}

Error MediaIOTask_QuickTime::executeCmd(MediaIOCommandRead &cmd) {
        if(_mode != MediaIO::Output) return Error::NotOpen;

        if(_currentFrame < 0 || _currentFrame >= _frameCount) {
                cmd.result = Error::EndOfFile;
                return Error::EndOfFile;
        }

        Frame::Ptr frame;
        Error err = readVideoFrame(static_cast<uint64_t>(_currentFrame), frame);
        if(err.isError()) {
                cmd.result = err;
                return err;
        }

        // Audio slice for this video frame (if we have an audio track and a
        // valid frame rate to compute the per-frame sample count).
        if(_audioTrackIndex >= 0 && _frameRate.isValid()) {
                const QuickTime::Track &at = _qt.tracks()[_audioTrackIndex];
                uint64_t trackSamples = at.sampleCount();
                size_t toRead = 0;
                if(_audioDesc.isCompressed()) {
                        // Compressed audio packets are variable-size and
                        // variable-duration. We pull one packet per video
                        // frame for MVP — consumers that need tight A/V
                        // alignment can drive the engine's readSample
                        // directly using per-sample dts. Works well enough
                        // for AAC in MP4 since AAC frames (~21 ms @ 48 kHz)
                        // and video frames (~33 ms @ 30 fps) are within
                        // one packet of each other per video frame over
                        // the short haul.
                        if(_audioSampleCursor < trackSamples) toRead = 1;
                } else {
                        // PCM: samples-per-video-frame via FrameRate helper.
                        size_t want = _frameRate.samplesPerFrame(
                                static_cast<int64_t>(_audioDesc.sampleRate()),
                                _currentFrame);
                        if(_audioSampleCursor + want > trackSamples) {
                                want = (_audioSampleCursor < trackSamples)
                                        ? static_cast<size_t>(trackSamples - _audioSampleCursor)
                                        : 0;
                        }
                        toRead = want;
                }
                if(toRead > 0) {
                        Audio audio;
                        Error aerr = readAudioSlice(_audioSampleCursor, toRead, audio);
                        if(!aerr.isError() && audio.isValid()) {
                                frame.modify()->audioList().pushToBack(Audio::Ptr::create(audio));
                        }
                        _audioSampleCursor += toRead;
                }
        }

        cmd.frame        = frame;
        cmd.currentFrame = _currentFrame;

        // Advance by the requested step.
        int s = cmd.step;
        _currentFrame += s;
        if(_currentFrame < 0) _currentFrame = 0;
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Write
// ----------------------------------------------------------------------------

Error MediaIOTask_QuickTime::setupWriterFromFrame(const Frame &frame) {
        if(_writerTracksRegistered) return Error::Ok;
        if(frame.imageList().isEmpty()) {
                promekiErr("MediaIOTask_QuickTime: cannot infer writer tracks; first frame has no image");
                return Error::InvalidArgument;
        }
        const Image &img = *frame.imageList()[0];
        if(!_frameRate.isValid()) {
                _frameRate = FrameRate(FrameRate::RationalType(24, 1));
                promekiWarn("MediaIOTask_QuickTime: no frame rate provided; defaulting to 24/1");
        }
        uint32_t vid = 0;
        Error err = _qt.addVideoTrack(img.pixelDesc(), img.size(), _frameRate, &vid);
        if(err.isError()) return err;
        _writerVideoTrackId = vid;

        // Register an audio track if the first frame has audio and one
        // hasn't been registered via pendingMediaDesc already.
        if(_writerAudioTrackId == 0 && !frame.audioList().isEmpty()) {
                const Audio &a0 = *frame.audioList()[0];
                if(a0.desc().isValid()) {
                        AudioDesc storage = pickStorageFormat(a0.desc());
                        uint32_t aid = 0;
                        Error aerr = _qt.addAudioTrack(storage, &aid);
                        if(!aerr.isError()) {
                                _writerAudioTrackId = aid;
                                _writerAudioStorage = storage;
                                _writerAudioFifo    = AudioBuffer(storage);
                                _writerAudioFifo.setInputFormat(a0.desc());
                                _writerAudioFifo.reserve(static_cast<size_t>(storage.sampleRate()));
                        }
                }
        }

        // Optional timecode track derived from Metadata::Timecode on the first frame.
        if(frame.metadata().contains(Metadata::Timecode)) {
                Variant v = frame.metadata().get(Metadata::Timecode);
                Timecode tc = v.get<Timecode>();
                if(tc.isValid()) {
                        uint32_t tid = 0;
                        Error tcErr = _qt.addTimecodeTrack(tc, _frameRate, &tid);
                        if(!tcErr.isError()) _writerTimecodeTrackId = tid;
                }
        }
        _writerTracksRegistered = true;
        return Error::Ok;
}

Error MediaIOTask_QuickTime::drainWriterAudio(bool flush) {
        if(_writerAudioTrackId == 0 || !_writerAudioStorage.isValid()) return Error::Ok;

        // On the regular drain path, emit one chunk of
        // samplesPerFrame(rate, _writerFrameCount-1) samples (matching the
        // video frame we just wrote). On the flush path, emit everything
        // remaining in the FIFO.
        size_t toEmit = 0;
        if(flush) {
                toEmit = _writerAudioFifo.available();
        } else {
                // The video frame we just wrote has index (_writerFrameCount-1).
                int64_t frameIdx = static_cast<int64_t>(_writerFrameCount) - 1;
                if(frameIdx < 0) frameIdx = 0;
                size_t want = _frameRate.samplesPerFrame(
                        static_cast<int64_t>(_writerAudioStorage.sampleRate()),
                        frameIdx);
                if(want == 0) return Error::Ok;
                if(_writerAudioFifo.available() < want) {
                        // Not enough audio yet — leave it buffered until more
                        // arrives or we flush at close.
                        return Error::Ok;
                }
                toEmit = want;
        }
        if(toEmit == 0) return Error::Ok;

        // Pull the samples out of the FIFO into a contiguous Buffer, then
        // hand that Buffer to the engine as one audio sample.
        size_t bytes = _writerAudioStorage.bufferSize(toEmit);
        Buffer buf(bytes);
        size_t got = _writerAudioFifo.pop(buf.data(), toEmit);
        if(got == 0) return Error::Ok;
        buf.setSize(_writerAudioStorage.bufferSize(got));

        QuickTime::Sample s;
        s.trackId  = _writerAudioTrackId;
        s.data     = Buffer::Ptr::create(std::move(buf));
        s.duration = 0;
        s.keyframe = true;
        return _qt.writeSample(_writerAudioTrackId, s);
}

Error MediaIOTask_QuickTime::executeCmd(MediaIOCommandWrite &cmd) {
        if(_mode != MediaIO::Input) return Error::NotOpen;
        if(!cmd.frame.isValid()) return Error::InvalidArgument;
        const Frame &frame = *cmd.frame;

        Error err = setupWriterFromFrame(frame);
        if(err.isError()) return err;

        // Write the first image as a single video sample.
        if(frame.imageList().isEmpty()) {
                promekiWarn("MediaIOTask_QuickTime: write with no image; skipping");
                return Error::InvalidArgument;
        }
        const Image &img = *frame.imageList()[0];

        // Pull the encoded bytes from plane(0). For compressed images this is
        // the raw codec payload; for uncompressed images it's the pixel bytes.
        Buffer::Ptr plane = img.plane(0);
        if(!plane.isValid() || plane->size() == 0) {
                promekiWarn("MediaIOTask_QuickTime: image plane is empty");
                return Error::InvalidArgument;
        }

        QuickTime::Sample s;
        s.trackId  = _writerVideoTrackId;
        s.data     = plane;
        s.duration = 0; // let the writer derive from track frame rate
        s.keyframe = true;
        if(frame.metadata().contains(Metadata::FrameKeyframe)) {
                s.keyframe = frame.metadata().get(Metadata::FrameKeyframe).get<bool>();
        }
        err = _qt.writeSample(_writerVideoTrackId, s);
        if(err.isError()) return err;

        _writerFrameCount++;
        _writerFramesSinceFlush++;

        // Push incoming audio into the FIFO (converting format if needed),
        // then drain one video-frame's worth into the engine. Any residual
        // audio stays in the FIFO until the next frame or close().
        if(_writerAudioTrackId != 0 && !frame.audioList().isEmpty()) {
                const Audio &a = *frame.audioList()[0];
                if(a.isValid()) {
                        Error aerr = _writerAudioFifo.push(a);
                        if(aerr.isError()) {
                                promekiWarn("MediaIOTask_QuickTime: audio push failed: %s",
                                            aerr.name().cstr());
                        }
                }
        }
        drainWriterAudio(/*flush=*/false);

        // Flush a fragment every N video frames so the on-disk file stays
        // playable at regular intervals. Only meaningful in LayoutFragmented;
        // QuickTime::Impl::flush() is a no-op for classic writers.
        if(_writerFragmentFrames > 0 &&
           _writerFramesSinceFlush >= static_cast<uint64_t>(_writerFragmentFrames)) {
                Error fe = _qt.flush();
                if(fe.isError()) {
                        promekiWarn("MediaIOTask_QuickTime: periodic flush failed: %s",
                                    fe.name().cstr());
                } else {
                        _writerFramesSinceFlush = 0;
                }
        }

        cmd.currentFrame = static_cast<int64_t>(_writerFrameCount);
        cmd.frameCount   = static_cast<int64_t>(_writerFrameCount);
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Seek
// ----------------------------------------------------------------------------

Error MediaIOTask_QuickTime::executeCmd(MediaIOCommandSeek &cmd) {
        if(_mode != MediaIO::Output) return Error::IllegalSeek;
        if(_videoTrackIndex < 0) return Error::IllegalSeek;
        int64_t target = cmd.frameNumber;
        if(target < 0) target = 0;
        if(target >= _frameCount) target = _frameCount - 1;
        _currentFrame = target;
        cmd.currentFrame = _currentFrame;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
