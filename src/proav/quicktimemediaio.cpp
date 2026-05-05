/**
 * @file      quicktimemediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>

#include <promeki/audiopayload.h>
#include <promeki/colormodel.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/metadata.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/quicktimemediaio.h>
#include <promeki/timecode.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(QuickTimeMediaIO)

PROMEKI_REGISTER_MEDIAIO_FACTORY(QuickTimeFactory)

// ============================================================================
// Magic-number probe: 'ftyp' atom in the first 16 bytes plus a recognized
// major brand.  Layout: [4B size][4B 'ftyp'][4B major brand]...
// ============================================================================

namespace {

        // Picks a QuickTime-compatible storage AudioDesc for the given
        // source.  QuickTime's canonical PCM codec tags are sowt / twos
        // (s16), in24 / in32 (big-endian), fl32 / fl64 (big-endian
        // float), and raw (unsigned 8-bit).  Little-endian float lacks a
        // single-FourCC mapping without the pcmC extension atom (which
        // we don't currently emit), so sources in that format are
        // promoted to PCMI_S16LE for storage.
        AudioDesc pickStorageFormat(const AudioDesc &src) {
                if (!src.isValid()) return src;
                switch (src.format().id()) {
                        case AudioFormat::PCMI_Float32LE:
                                // FIXME: promotes 32-bit float to 16-bit int, losing precision.
                                return AudioDesc(AudioFormat::PCMI_S16LE, src.sampleRate(), src.channels());
                        default: return src;
                }
        }

        bool isRecognizedBrand(uint32_t brand) {
                switch (brand) {
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
                        default: return false;
                }
        }

        bool probeQuickTimeDevice(IODevice *device) {
                uint8_t buf[16] = {};
                int64_t n = device->read(buf, 16);
                if (n < 12) return false;

                uint32_t boxType = (uint32_t(buf[4]) << 24) | (uint32_t(buf[5]) << 16) | (uint32_t(buf[6]) << 8) |
                                   uint32_t(buf[7]);
                if (boxType != 0x66747970) return false; // 'ftyp'

                uint32_t major = (uint32_t(buf[8]) << 24) | (uint32_t(buf[9]) << 16) | (uint32_t(buf[10]) << 8) |
                                 uint32_t(buf[11]);
                return isRecognizedBrand(major);
        }

} // namespace

// ============================================================================
// QuickTimeFactory
// ============================================================================

bool QuickTimeFactory::canHandleDevice(IODevice *device) const {
        return probeQuickTimeDevice(device);
}

QuickTimeFactory::Config::SpecMap QuickTimeFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // -1 sentinel = "let the reader pick the first video / audio
        // track it finds".
        s(MediaConfig::VideoTrack, int32_t(-1));
        s(MediaConfig::AudioTrack, int32_t(-1));
        // Writer defaults — Classic is the broadly compatible choice for
        // on-disk outputs; every player handles it without surprises.
        // Fragmented stays available for streaming / pipe / socket sinks
        // and for crash-resilient live capture.
        s(MediaConfig::QuickTimeLayout, QuickTimeLayout::Classic);
        s(MediaConfig::QuickTimeFragmentFrames, int32_t(QuickTimeMediaIO::DefaultFragmentFrames));
        s(MediaConfig::QuickTimeFlushSync, false);
        return specs;
}

Metadata QuickTimeFactory::defaultMetadata() const {
        // udta ©-atom set written via addIfPresent() in
        // quicktime_writer.cpp, plus the BWF-style extension fields the
        // writer stamps at open time (Originator, OriginatorReference,
        // OriginationDateTime, UMID).  Timecode comes off each frame
        // rather than the container, but it's listed here too so callers
        // can set an initial timecode if the producer doesn't stamp one.
        Metadata m;
        m.set(Metadata::Title, String());
        m.set(Metadata::Comment, String());
        m.set(Metadata::Date, String());
        m.set(Metadata::Artist, String());
        m.set(Metadata::Copyright, String());
        m.set(Metadata::Software, String());
        m.set(Metadata::Album, String());
        m.set(Metadata::Genre, String());
        m.set(Metadata::Description, String());
        m.set(Metadata::Originator, String());
        m.set(Metadata::OriginatorReference, String());
        m.set(Metadata::OriginationDateTime, String());
        m.set(Metadata::UMID, String());
        m.set(Metadata::Timecode, Timecode());
        return m;
}

MediaIO *QuickTimeFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new QuickTimeMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ============================================================================
// QuickTimeMediaIO lifecycle
// ============================================================================

QuickTimeMediaIO::QuickTimeMediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {}

QuickTimeMediaIO::~QuickTimeMediaIO() {
        if (isOpen()) (void)close().wait();
}

Error QuickTimeMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        _filename = cfg.getAs<String>(MediaConfig::Filename);
        if (_filename.isEmpty()) {
                promekiErr("QuickTimeMediaIO: filename is required");
                return Error::InvalidArgument;
        }

        Enum       modeEnum = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        _isOpen = true;
        _isWrite = isWrite;

        MediaDesc  outMediaDesc;
        FrameCount outFrameCount(0);
        bool       outCanSeek = false;

        if (!isWrite) {
                _qt = QuickTime::createReader(_filename);
                Error err = _qt.open();
                if (err.isError()) {
                        promekiErr("QuickTimeMediaIO: open '%s' failed: %s", _filename.cstr(), err.name().cstr());
                        return err;
                }

                int videoIdx = cfg.getAs<int>(MediaConfig::VideoTrack, -1);
                int audioIdx = cfg.getAs<int>(MediaConfig::AudioTrack, -1);
                if (videoIdx < 0) {
                        for (size_t i = 0; i < _qt.tracks().size(); ++i) {
                                if (_qt.tracks()[i].type() == QuickTime::Video) {
                                        videoIdx = static_cast<int>(i);
                                        break;
                                }
                        }
                }
                if (audioIdx < 0) {
                        for (size_t i = 0; i < _qt.tracks().size(); ++i) {
                                if (_qt.tracks()[i].type() == QuickTime::Audio) {
                                        audioIdx = static_cast<int>(i);
                                        break;
                                }
                        }
                }
                _videoTrackIndex = videoIdx;
                _audioTrackIndex = audioIdx;

                if (_videoTrackIndex < 0 && _audioTrackIndex < 0) {
                        promekiErr("QuickTimeMediaIO: '%s' has no video or audio tracks", _filename.cstr());
                        return Error::NotSupported;
                }

                if (_audioTrackIndex >= 0) {
                        const QuickTime::Track &at = _qt.tracks()[_audioTrackIndex];
                        if (!at.audioDesc().isValid()) {
                                promekiErr("QuickTimeMediaIO: audio track has no AudioDesc");
                                return Error::NotSupported;
                        }
                        _audioDesc = at.audioDesc();
                }

                MediaDesc mediaDesc;
                if (_videoTrackIndex >= 0) {
                        const QuickTime::Track &vt = _qt.tracks()[_videoTrackIndex];
                        ImageDesc               idesc(vt.size(), vt.pixelFormat());
                        mediaDesc.imageList().pushToBack(idesc);
                        if (vt.frameRate().isValid()) {
                                mediaDesc.setFrameRate(vt.frameRate());
                                _frameRate = vt.frameRate();
                                _frameCount = FrameCount(static_cast<int64_t>(vt.sampleCount()));
                        }
                }
                if (_audioTrackIndex >= 0) {
                        mediaDesc.audioList().pushToBack(_audioDesc);
                }
                if (!mediaDesc.frameRate().isValid() && _audioTrackIndex >= 0) {
                        // Audio-only fallback: synthetic 1/1 rate so MediaIO
                        // has something coherent to report.
                        _frameRate = FrameRate(FrameRate::RationalType(1, 1));
                        mediaDesc.setFrameRate(_frameRate);
                }
                mediaDesc.metadata() = _qt.containerMetadata();

                _anchorTimecode = _qt.startTimecode();
                _audioSampleCursor = 0;
                _currentFrame = 0;

                outMediaDesc = mediaDesc;
                outFrameCount = _frameCount;
                outCanSeek = true;
        } else {
                _qt = QuickTime::createWriter(_filename);

                Error layoutErr;
                Enum  layoutEnum = cfg.get(MediaConfig::QuickTimeLayout).asEnum(QuickTimeLayout::Type, &layoutErr);
                if (layoutErr.isError() || !layoutEnum.hasListedValue()) {
                        promekiErr("QuickTimeMediaIO: unknown Layout value");
                        return Error::InvalidArgument;
                }
                QuickTime::Layout layout = static_cast<QuickTime::Layout>(layoutEnum.value());
                Error             err = _qt.setLayout(layout);
                if (err.isError()) return err;

                _writerFragmentFrames = cfg.getAs<int>(MediaConfig::QuickTimeFragmentFrames, DefaultFragmentFrames);
                if (_writerFragmentFrames < 1) _writerFragmentFrames = DefaultFragmentFrames;
                _writerFramesSinceFlush = 0;

                _qt.setFlushSync(cfg.getAs<bool>(MediaConfig::QuickTimeFlushSync, false));

                err = _qt.open();
                if (err.isError()) {
                        promekiErr("QuickTimeMediaIO: open writer '%s' failed: %s", _filename.cstr(),
                                   err.name().cstr());
                        return err;
                }
                _qt.setContainerMetadata(cmd.pendingMetadata);
                _writerTracksRegistered = false;
                _writerFrameCount = 0;
                _writerVideoTrackId = 0;
                _writerAudioTrackId = 0;
                _writerTimecodeTrackId = 0;
                _writerAudioFifo = AudioBuffer();
                _writerAudioStorage = AudioDesc();

                if (cmd.pendingMediaDesc.frameRate().isValid()) {
                        _frameRate = cmd.pendingMediaDesc.frameRate();
                }
                if (!cmd.pendingMediaDesc.imageList().isEmpty() && _frameRate.isValid()) {
                        const ImageDesc &idesc = cmd.pendingMediaDesc.imageList()[0];
                        uint32_t         vid = 0;
                        err = _qt.addVideoTrack(idesc.pixelFormat(), idesc.size(), _frameRate, &vid);
                        if (err.isError()) return err;
                        _writerVideoTrackId = vid;
                }
                if (!cmd.pendingMediaDesc.audioList().isEmpty() && _frameRate.isValid()) {
                        const AudioDesc &srcDesc = cmd.pendingMediaDesc.audioList()[0];
                        AudioDesc        storage = pickStorageFormat(srcDesc);
                        uint32_t         aid = 0;
                        err = _qt.addAudioTrack(storage, &aid);
                        if (err.isError()) return err;
                        _writerAudioTrackId = aid;
                        _writerAudioStorage = storage;
                        _writerAudioFifo = AudioBuffer(storage);
                        _writerAudioFifo.setInputFormat(srcDesc);
                        // Reserve ~1 second of headroom to absorb frame-rate jitter.
                        _writerAudioFifo.reserve(static_cast<size_t>(storage.sampleRate()));
                }
                if (!cmd.pendingMediaDesc.imageList().isEmpty() && _frameRate.isValid()) {
                        _writerTracksRegistered = true;
                }

                outMediaDesc = cmd.pendingMediaDesc;
                outFrameCount = FrameCount(0);
                outCanSeek = false;
        }

        MediaIOPortGroup *group = addPortGroup("quicktime");
        if (group == nullptr) return Error::Invalid;
        group->setFrameRate(_frameRate);
        group->setCanSeek(outCanSeek);
        group->setFrameCount(outFrameCount);
        if (isWrite) {
                if (addSink(group, outMediaDesc) == nullptr) return Error::Invalid;
        } else {
                if (addSource(group, outMediaDesc) == nullptr) return Error::Invalid;
        }
        return Error::Ok;
}

Error QuickTimeMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_isOpen && _isWrite && _qt.isOpen()) {
                drainWriterAudio(/*flush=*/true);
                Error err = _qt.finalize();
                if (err.isError()) {
                        promekiWarn("QuickTimeMediaIO: finalize failed: %s", err.name().cstr());
                }
        }
        _qt.close();
        _qt = QuickTime();
        _isOpen = false;
        _isWrite = false;
        _filename.clear();
        _videoTrackIndex = -1;
        _audioTrackIndex = -1;
        _currentFrame = 0;
        _frameCount = 0;
        _frameRate = FrameRate();
        _anchorTimecode = Timecode();
        _audioSampleCursor = 0;
        _audioDesc = AudioDesc();
        _writerTracksRegistered = false;
        _writerVideoTrackId = 0;
        _writerAudioTrackId = 0;
        _writerTimecodeTrackId = 0;
        _writerFrameCount = 0;
        _writerFramesSinceFlush = 0;
        _writerFragmentFrames = DefaultFragmentFrames;
        _writerAudioFifo = AudioBuffer();
        _writerAudioStorage = AudioDesc();
        return Error::Ok;
}

// ============================================================================
// Read
// ============================================================================

Error QuickTimeMediaIO::readVideoFrame(const FrameNumber &frameIndex, Frame::Ptr &outFrame) {
        if (_videoTrackIndex < 0) return Error::NotSupported;
        if (!frameIndex.isValid()) return Error::IllegalSeek;

        QuickTime::Sample s;
        Error err = _qt.readSample(static_cast<size_t>(_videoTrackIndex), static_cast<uint64_t>(frameIndex.value()), s);
        if (err.isError()) return err;
        if (!s.data.isValid()) return Error::IOError;

        const QuickTime::Track &vt = _qt.tracks()[_videoTrackIndex];

        Frame::Ptr frame = Frame::Ptr::create();

        const PixelFormat &samplePd = vt.pixelFormat();
        const size_t       sampleWidth = vt.size().width();
        const size_t       sampleHeight = vt.size().height();
        ImageDesc          idesc(Size2Du32(sampleWidth, sampleHeight), samplePd);
        idesc.metadata() = vt.metadata();

        MediaPayload::Ptr videoPayload;

        if (samplePd.isCompressed()) {
                Buffer bitstream = s.data;
                const bool  isH264 = (samplePd.id() == PixelFormat::H264);
                const bool  isHEVC = (samplePd.id() == PixelFormat::HEVC);
                if ((isH264 || isHEVC) && vt.codecConfig().isValid()) {
                        Buffer annexB;
                        Error cerr = H264Bitstream::avccToAnnexB(BufferView(s.data, 0, s.data.size()), 4, annexB);
                        if (cerr.isError()) {
                                promekiWarn("QuickTimeMediaIO: AVCC->Annex-B failed for sample %lld: %s",
                                            static_cast<long long>(frameIndex.value()), cerr.name().cstr());
                                bitstream = s.data;
                        } else {
                                bitstream = annexB;
                                if (s.keyframe) {
                                        Buffer psAnnexB;
                                        BufferView  cfgView(vt.codecConfig(), 0, vt.codecConfig().size());
                                        Error       pe;
                                        if (isH264) {
                                                AvcDecoderConfig cfg;
                                                pe = AvcDecoderConfig::parse(cfgView, cfg);
                                                if (!pe.isError()) pe = cfg.toAnnexB(psAnnexB);
                                        } else {
                                                HevcDecoderConfig cfg;
                                                pe = HevcDecoderConfig::parse(cfgView, cfg);
                                                if (!pe.isError()) pe = cfg.toAnnexB(psAnnexB);
                                        }
                                        if (!pe.isError() && psAnnexB && psAnnexB.size() > 0) {
                                                const size_t total = psAnnexB.size() + annexB.size();
                                                auto         merged = Buffer(total);
                                                if (merged) {
                                                        std::memcpy(merged.data(), psAnnexB.data(), psAnnexB.size());
                                                        std::memcpy(static_cast<uint8_t *>(merged.data()) +
                                                                            psAnnexB.size(),
                                                                    annexB.data(), annexB.size());
                                                        merged.setSize(total);
                                                        bitstream = merged;
                                                }
                                        }
                                }
                        }
                }
                auto cvp = CompressedVideoPayload::Ptr::create(idesc, bitstream);
                if (s.keyframe) cvp.modify()->addFlag(MediaPayload::Keyframe);
                videoPayload = cvp;
        } else if (samplePd.planeCount() > 1) {
                auto uvp = UncompressedVideoPayload::allocate(idesc);
                if (uvp.isValid()) {
                        const uint8_t *src = static_cast<const uint8_t *>(s.data.data());
                        size_t         off = 0;
                        bool           ok = true;
                        for (size_t p = 0; p < samplePd.planeCount(); ++p) {
                                const size_t psz = samplePd.planeSize(p, idesc);
                                if (off + psz > s.data.size()) {
                                        ok = false;
                                        break;
                                }
                                std::memcpy(uvp.modify()->data()[p].data(), src + off, psz);
                                off += psz;
                        }
                        if (ok) videoPayload = uvp;
                }
        } else {
                BufferView planes;
                planes.pushToBack(s.data, 0, s.data.size());
                videoPayload = UncompressedVideoPayload::Ptr::create(idesc, planes);
        }

        if (!videoPayload.isValid()) {
                promekiWarn("QuickTimeMediaIO: failed to wrap video sample %lld as payload",
                            static_cast<long long>(frameIndex.value()));
                return Error::DecodeFailed;
        }

        frame.modify()->addPayload(videoPayload);

        Metadata &fmeta = frame.modify()->metadata();
        fmeta.set(Metadata::FrameNumber, frameIndex);
        fmeta.set(Metadata::FrameKeyframe, s.keyframe);
        if (_anchorTimecode.isValid()) {
                FrameNumber anchorFrame = _anchorTimecode.toFrameNumber();
                if (anchorFrame.isValid()) {
                        Timecode tc =
                                Timecode::fromFrameNumber(_anchorTimecode.mode(), anchorFrame + frameIndex.value());
                        if (tc.isValid()) fmeta.set(Metadata::Timecode, tc);
                }
        }

        outFrame = frame;
        return Error::Ok;
}

Error QuickTimeMediaIO::readAudioSlice(uint64_t startSample, size_t samples, MediaPayload::Ptr &out) {
        if (_audioTrackIndex < 0) return Error::NotSupported;
        if (samples == 0) {
                out = MediaPayload::Ptr();
                return Error::Ok;
        }

        QuickTime::Sample range;
        Error             err = _qt.readSampleRange(static_cast<size_t>(_audioTrackIndex), startSample, samples, range);
        if (err.isError()) return err;
        if (!range.data.isValid()) return Error::IOError;

        const size_t rangeSize = range.data.size();
        BufferView   view(range.data, 0, rangeSize);

        if (_audioDesc.isCompressed()) {
                const size_t approxSampleCount = static_cast<size_t>(range.duration) * samples;
                auto         p = CompressedAudioPayload::Ptr::create(_audioDesc, view, approxSampleCount);
                if (!p.isValid()) return Error::DecodeFailed;
                out = p;
                return Error::Ok;
        }

        size_t frameBytes = _audioDesc.bytesPerSample() * _audioDesc.channels();
        size_t sampleCount = (frameBytes > 0) ? (rangeSize / frameBytes) : 0;
        auto   p = PcmAudioPayload::Ptr::create(_audioDesc, sampleCount, view);
        if (!p.isValid()) return Error::DecodeFailed;
        out = p;
        return Error::Ok;
}

Error QuickTimeMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (!_isOpen || _isWrite) return Error::NotOpen;

        if (!_currentFrame.isValid() || (_frameCount.isFinite() && _currentFrame.value() >= _frameCount.value())) {
                cmd.result = Error::EndOfFile;
                return Error::EndOfFile;
        }

        Frame::Ptr frame;
        Error      err = readVideoFrame(_currentFrame, frame);
        if (err.isError()) {
                cmd.result = err;
                return err;
        }

        if (_audioTrackIndex >= 0 && _frameRate.isValid()) {
                const QuickTime::Track &at = _qt.tracks()[_audioTrackIndex];
                uint64_t                trackSamples = at.sampleCount();
                size_t                  toRead = 0;
                if (_audioDesc.isCompressed()) {
                        if (_audioSampleCursor < trackSamples) toRead = 1;
                } else {
                        size_t want = _frameRate.samplesPerFrame(static_cast<int64_t>(_audioDesc.sampleRate()),
                                                                 _currentFrame.value());
                        if (_audioSampleCursor + want > trackSamples) {
                                want = (_audioSampleCursor < trackSamples)
                                               ? static_cast<size_t>(trackSamples - _audioSampleCursor)
                                               : 0;
                        }
                        toRead = want;
                }
                if (toRead > 0) {
                        MediaPayload::Ptr audioPayload;
                        Error             aerr = readAudioSlice(_audioSampleCursor, toRead, audioPayload);
                        if (!aerr.isError() && audioPayload.isValid()) {
                                frame.modify()->addPayload(audioPayload);
                        }
                        _audioSampleCursor += toRead;
                }
        }

        cmd.frame = frame;
        cmd.currentFrame = _currentFrame;

        int s = cmd.step;
        _currentFrame += s;
        if (!_currentFrame.isValid()) _currentFrame = FrameNumber(0);
        return Error::Ok;
}

// ============================================================================
// Write
// ============================================================================

Error QuickTimeMediaIO::setupWriterFromFrame(const Frame &frame) {
        if (_writerTracksRegistered) return Error::Ok;

        PixelFormat inferPixelFormat;
        Size2Du32   inferSize;
        auto        vids = frame.videoPayloads();
        if (!vids.isEmpty() && vids[0].isValid()) {
                const ImageDesc &id = vids[0]->desc();
                inferPixelFormat = id.pixelFormat();
                inferSize = id.size();
        } else {
                promekiErr("QuickTimeMediaIO: cannot infer writer tracks; first frame has no image");
                return Error::InvalidArgument;
        }

        if (!_frameRate.isValid()) {
                _frameRate = FrameRate(FrameRate::RationalType(24, 1));
                promekiWarn("QuickTimeMediaIO: no frame rate provided; defaulting to 24/1");
        }
        uint32_t vid = 0;
        Error    err = _qt.addVideoTrack(inferPixelFormat, inferSize, _frameRate, &vid);
        if (err.isError()) return err;
        _writerVideoTrackId = vid;

        auto auds = frame.audioPayloads();
        if (_writerAudioTrackId == 0 && !auds.isEmpty() && auds[0].isValid()) {
                const AudioDesc &ad = auds[0]->desc();
                if (ad.isValid()) {
                        AudioDesc storage = pickStorageFormat(ad);
                        uint32_t  aid = 0;
                        Error     aerr = _qt.addAudioTrack(storage, &aid);
                        if (!aerr.isError()) {
                                _writerAudioTrackId = aid;
                                _writerAudioStorage = storage;
                                _writerAudioFifo = AudioBuffer(storage);
                                _writerAudioFifo.setInputFormat(ad);
                                _writerAudioFifo.reserve(static_cast<size_t>(storage.sampleRate()));
                        }
                }
        }

        if (frame.metadata().contains(Metadata::Timecode)) {
                Variant  v = frame.metadata().get(Metadata::Timecode);
                Timecode tc = v.get<Timecode>();
                if (tc.isValid()) {
                        uint32_t tid = 0;
                        Error    tcErr = _qt.addTimecodeTrack(tc, _frameRate, &tid);
                        if (!tcErr.isError()) _writerTimecodeTrackId = tid;
                }
        }
        _writerTracksRegistered = true;
        return Error::Ok;
}

Error QuickTimeMediaIO::drainWriterAudio(bool flush) {
        if (_writerAudioTrackId == 0 || !_writerAudioStorage.isValid()) return Error::Ok;

        size_t toEmit = 0;
        if (flush) {
                toEmit = _writerAudioFifo.available();
        } else {
                int64_t frameIdx = _writerFrameCount.value() - 1;
                if (frameIdx < 0) frameIdx = 0;
                size_t want =
                        _frameRate.samplesPerFrame(static_cast<int64_t>(_writerAudioStorage.sampleRate()), frameIdx);
                if (want == 0) return Error::Ok;
                if (_writerAudioFifo.available() < want) {
                        // Not enough audio yet — leave buffered until more
                        // arrives or we flush at close.
                        return Error::Ok;
                }
                toEmit = want;
        }
        if (toEmit == 0) return Error::Ok;

        size_t bytes = _writerAudioStorage.bufferSize(toEmit);
        Buffer buf(bytes);
        auto [got, popErr] = _writerAudioFifo.pop(buf.data(), toEmit);
        if (popErr.isError()) return popErr;
        if (got == 0) return Error::Ok;
        buf.setSize(_writerAudioStorage.bufferSize(got));

        QuickTime::Sample s;
        s.trackId = _writerAudioTrackId;
        s.data = Buffer(std::move(buf));
        s.duration = 0;
        s.keyframe = true;
        return _qt.writeSample(_writerAudioTrackId, s);
}

Error QuickTimeMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!_isOpen || !_isWrite) return Error::NotOpen;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;
        const Frame &frame = *cmd.frame;

        Error err = setupWriterFromFrame(frame);
        if (err.isError()) {
                return err;
        }

        auto vidsWrite = frame.videoPayloads();
        if (vidsWrite.isEmpty()) {
                promekiWarn("QuickTimeMediaIO: write with no image; skipping");
                return Error::InvalidArgument;
        }

        QuickTime::Sample s;
        s.trackId = _writerVideoTrackId;
        s.duration = 0;
        s.keyframe = true;

        const VideoPayload &vp = *vidsWrite[0];
        if (const auto *cvp = vp.as<CompressedVideoPayload>()) {
                if (cvp->planeCount() == 0) {
                        promekiWarn("QuickTimeMediaIO: compressed payload has no planes; skipping");
                        return Error::InvalidArgument;
                }
                auto view = cvp->plane(0);
                if (!view.isValid() || view.size() == 0) {
                        promekiWarn("QuickTimeMediaIO: payload plane has no bytes; skipping");
                        return Error::InvalidArgument;
                }
                const Buffer &backing = view.buffer();
                if (backing && view.offset() == 0 && view.size() == backing.size()) {
                        s.data = backing;
                } else {
                        Buffer copy(view.size());
                        std::memcpy(copy.data(), view.data(), view.size());
                        copy.setSize(view.size());
                        s.data = Buffer(std::move(copy));
                }
                s.keyframe = cvp->isKeyframe();
        } else if (const auto *uvp = vp.as<UncompressedVideoPayload>()) {
                const size_t pc = uvp->planeCount();
                if (pc == 0) {
                        promekiWarn("QuickTimeMediaIO: uncompressed payload has no planes");
                        return Error::InvalidArgument;
                }
                if (pc > 1) {
                        size_t total = 0;
                        for (size_t p = 0; p < pc; ++p) total += uvp->plane(p).size();
                        Buffer concat(total);
                        size_t off = 0;
                        for (size_t p = 0; p < pc; ++p) {
                                auto pv = uvp->plane(p);
                                std::memcpy(static_cast<uint8_t *>(concat.data()) + off, pv.data(), pv.size());
                                off += pv.size();
                        }
                        concat.setSize(total);
                        s.data = Buffer(std::move(concat));
                } else {
                        auto pv = uvp->plane(0);
                        if (!pv.isValid() || pv.size() == 0) {
                                promekiWarn("QuickTimeMediaIO: uncompressed plane is empty");
                                return Error::InvalidArgument;
                        }
                        const Buffer &backing = pv.buffer();
                        if (backing && pv.offset() == 0 && pv.size() == backing.size()) {
                                s.data = backing;
                        } else {
                                Buffer copy(pv.size());
                                std::memcpy(copy.data(), pv.data(), pv.size());
                                copy.setSize(pv.size());
                                s.data = Buffer(std::move(copy));
                        }
                }
                if (frame.metadata().contains(Metadata::FrameKeyframe)) {
                        s.keyframe = frame.metadata().get(Metadata::FrameKeyframe).get<bool>();
                }
        } else {
                promekiWarn("QuickTimeMediaIO: unsupported video payload class");
                return Error::InvalidArgument;
        }

        err = _qt.writeSample(_writerVideoTrackId, s);
        if (err.isError()) {
                return err;
        }

        _writerFrameCount++;
        _writerFramesSinceFlush++;

        auto audsWrite = frame.audioPayloads();
        if (_writerAudioTrackId != 0 && !audsWrite.isEmpty() && audsWrite[0].isValid()) {
                const auto *uap = audsWrite[0]->as<PcmAudioPayload>();
                if (uap != nullptr && uap->planeCount() > 0) {
                        auto  view = uap->plane(0);
                        Error aerr = _writerAudioFifo.push(view.data(), uap->sampleCount(), uap->desc());
                        if (aerr.isError()) {
                                promekiWarn("QuickTimeMediaIO: audio push failed: %s", aerr.name().cstr());
                        }
                }
        }
        drainWriterAudio(/*flush=*/false);

        if (_writerFragmentFrames > 0 && _writerFramesSinceFlush >= static_cast<uint64_t>(_writerFragmentFrames)) {
                Error fe = _qt.flush();
                if (fe.isError()) {
                        promekiWarn("QuickTimeMediaIO: periodic flush failed: %s", fe.name().cstr());
                } else {
                        _writerFramesSinceFlush = 0;
                }
        }

        cmd.currentFrame = toFrameNumber(_writerFrameCount);
        cmd.frameCount = _writerFrameCount;
        return Error::Ok;
}

// ============================================================================
// Seek
// ============================================================================

Error QuickTimeMediaIO::executeCmd(MediaIOCommandSeek &cmd) {
        if (!_isOpen || _isWrite) return Error::IllegalSeek;
        if (_videoTrackIndex < 0) return Error::IllegalSeek;
        int64_t target = cmd.frameNumber.isValid() ? cmd.frameNumber.value() : 0;
        if (target < 0) target = 0;
        if (_frameCount.isFinite() && target >= _frameCount.value()) {
                target = _frameCount.value() - 1;
        }
        _currentFrame = FrameNumber(target);
        cmd.currentFrame = _currentFrame;
        return Error::Ok;
}

// ============================================================================
// Negotiation overrides
// ============================================================================

bool QuickTimeMediaIO::isSupportedPixelFormat(const PixelFormat &pd) {
        if (!pd.isValid()) return false;
        switch (pd.id()) {
                case PixelFormat::H264:
                case PixelFormat::HEVC:
                case PixelFormat::AV1:
                case PixelFormat::JPEG_RGB8_sRGB:
                case PixelFormat::JPEG_RGBA8_sRGB:
                case PixelFormat::JPEG_YUV8_422_Rec709:
                case PixelFormat::JPEG_YUV8_420_Rec709:
                case PixelFormat::JPEG_YUV8_422_Rec601:
                case PixelFormat::JPEG_YUV8_420_Rec601:
                case PixelFormat::JPEG_YUV8_422_Rec709_Full:
                case PixelFormat::JPEG_YUV8_420_Rec709_Full:
                case PixelFormat::JPEG_YUV8_422_Rec601_Full:
                case PixelFormat::JPEG_YUV8_420_Rec601_Full:
                case PixelFormat::ProRes_422_Proxy:
                case PixelFormat::ProRes_422_LT:
                case PixelFormat::ProRes_422:
                case PixelFormat::ProRes_422_HQ:
                case PixelFormat::ProRes_4444:
                case PixelFormat::ProRes_4444_XQ:
                case PixelFormat::RGB8_sRGB:
                case PixelFormat::RGBA8_sRGB:
                case PixelFormat::YUV8_422_Rec709:
                case PixelFormat::YUV8_422_Rec601:
                case PixelFormat::YUV8_422_UYVY_Rec709:
                case PixelFormat::YUV8_422_UYVY_Rec601:
                case PixelFormat::YUV10_422_v210_Rec709:
                case PixelFormat::YUV8_422_Planar_Rec709:
                case PixelFormat::YUV8_420_Planar_Rec709:
                case PixelFormat::YUV8_420_Planar_Rec601:
                case PixelFormat::YUV8_420_SemiPlanar_Rec709:
                case PixelFormat::YUV8_420_SemiPlanar_Rec601:
                case PixelFormat::YUV8_422_SemiPlanar_Rec709: return true;
                default: return false;
        }
}

PixelFormat QuickTimeMediaIO::pickSupportedPixelFormat(const PixelFormat &offered) {
        if (isSupportedPixelFormat(offered)) return offered;

        if (offered.isCompressed()) return PixelFormat(PixelFormat::H264);

        const bool offerYuv = offered.isValid() && offered.colorModel().type() == ColorModel::TypeYCbCr;
        const int  offerBits = (offered.isValid() && offered.memLayout().compCount() > 0)
                                       ? static_cast<int>(offered.memLayout().compDesc(0).bits)
                                       : 8;

        if (offerYuv) {
                const auto sampling = offered.memLayout().sampling();
                if (offerBits >= 10 && sampling != PixelMemLayout::Sampling420) {
                        return PixelFormat(PixelFormat::YUV10_422_v210_Rec709);
                }
                if (sampling == PixelMemLayout::Sampling420) {
                        return PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709);
                }
                return PixelFormat(PixelFormat::YUV8_422_Rec709);
        }
        return PixelFormat(PixelFormat::RGBA8_sRGB);
}

Error QuickTimeMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        if (offered.imageList().isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        const PixelFormat &offeredPd = offered.imageList()[0].pixelFormat();
        const PixelFormat  target = pickSupportedPixelFormat(offeredPd);
        if (target == offeredPd) {
                *preferred = offered;
                return Error::Ok;
        }

        MediaDesc want = offered;
        ImageDesc img(offered.imageList()[0].size(), target);
        img.setVideoScanMode(offered.imageList()[0].videoScanMode());
        want.imageList().clear();
        want.imageList().pushToBack(img);
        *preferred = want;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
