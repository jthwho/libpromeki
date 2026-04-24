/**
 * @file      mediaiotask_quicktime.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <promeki/mediaiotask_quicktime.h>
#include <promeki/colormodel.h>
#include <promeki/enums.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/iodevice.h>
#include <promeki/frame.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/uncompressedaudiopayload.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/audiopayload.h>
#include <promeki/mediadesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaIOTask_QuickTime)

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
        switch(src.format().id()) {
                case AudioFormat::PCMI_Float32LE:
                        // FIXME: promotes 32-bit float to 16-bit int, losing precision.
                        return AudioDesc(AudioFormat::PCMI_S16LE, src.sampleRate(), src.channels());
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
                true,    // canBeSource
                true,    // canBeSink
                false,   // canBeTransform
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
                        // Writer defaults — Classic is the broadly
                        // compatible choice for on-disk outputs; every
                        // player handles it without surprises.
                        // Fragmented stays available for streaming /
                        // pipe / socket sinks and for crash-resilient
                        // live capture (override via QuickTimeLayout cc).
                        s(MediaConfig::QuickTimeLayout, QuickTimeLayout::Classic);
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

        if(cmd.mode == MediaIO::Source) {
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

                // Accept both PCM and compressed audio tracks.  For PCM the
                // reader produces an AudioDesc whose AudioFormat is a PCM
                // variant; for compressed codecs (AAC, Opus, etc.) the
                // reader picks the matching compressed AudioFormat entry.
                // In both cases AudioDesc::isValid() is true and we pass
                // samples through to the consumer.
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
                        ImageDesc idesc(vt.size(), vt.pixelFormat());
                        mediaDesc.imageList().pushToBack(idesc);
                        if(vt.frameRate().isValid()) {
                                mediaDesc.setFrameRate(vt.frameRate());
                                _frameRate = vt.frameRate();
                                _frameCount = FrameCount(static_cast<int64_t>(vt.sampleCount()));
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

        // Pick a layout. Default is Classic for broad player
        // compatibility; Fragmented is opt-in for streaming or
        // crash-resilient live captures. The QuickTimeLayout Enum
        // integer values match QuickTime::Layout by construction so
        // we can cast directly.
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
                err = _qt.addVideoTrack(idesc.pixelFormat(), idesc.size(),
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
        if(_mode == MediaIO::Sink && _qt.isOpen()) {
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

Error MediaIOTask_QuickTime::readVideoFrame(const FrameNumber &frameIndex, Frame::Ptr &outFrame) {
        if(_videoTrackIndex < 0) return Error::NotSupported;
        if(!frameIndex.isValid()) return Error::IllegalSeek;

        QuickTime::Sample s;
        Error err = _qt.readSample(static_cast<size_t>(_videoTrackIndex),
                                   static_cast<uint64_t>(frameIndex.value()), s);
        if(err.isError()) return err;
        if(!s.data.isValid()) return Error::IOError;

        const QuickTime::Track &vt = _qt.tracks()[_videoTrackIndex];

        // Build the Frame and attach the video payload. Compressed
        // codecs flow through a CompressedVideoPayload built around the
        // sample buffer; uncompressed packed YUV / raw RGB become
        // UncompressedVideoPayload wrappers over the same bytes.
        Frame::Ptr frame = Frame::Ptr::create();

        // Image construction varies by layout:
        //   - Compressed codecs and single-plane uncompressed formats
        //     (2vuy, v210, packed RGB) adopt the sample Buffer::Ptr
        //     directly as plane 0 — zero-copy.
        //   - Multi-plane uncompressed formats (planar / semi-planar
        //     YUV) need one Buffer per plane.  The container delivers
        //     the sample as one contiguous blob, so we allocate a
        //     proper Image and memcpy the plane slices out of the
        //     sample buffer.  planeSize() gives each slice's size and
        //     they're packed back-to-back with no per-plane padding
        //     in QuickTime uncompressed tracks.
        const PixelFormat &samplePd = vt.pixelFormat();
        const size_t sampleWidth  = vt.size().width();
        const size_t sampleHeight = vt.size().height();
        ImageDesc idesc(Size2Du32(sampleWidth, sampleHeight), samplePd);
        idesc.metadata() = vt.metadata();

        MediaPayload::Ptr videoPayload;

        if(samplePd.isCompressed()) {
                // Compressed video path: build a CompressedVideoPayload
                // carrying the sample bitstream.  For H.264 / HEVC we
                // re-frame the AVCC sample to Annex-B and prepend the
                // parameter sets on keyframes so a downstream decoder
                // can init from any seek point.  Non-AVC/HEVC codecs
                // (JPEG, JPEG XS, ProRes, AV1, ...) keep their bytes
                // unchanged.
                Buffer::Ptr bitstream = s.data;
                const bool isH264 = (samplePd.id() == PixelFormat::H264);
                const bool isHEVC = (samplePd.id() == PixelFormat::HEVC);
                if((isH264 || isHEVC) && vt.codecConfig().isValid()) {
                        Buffer::Ptr annexB;
                        Error cerr = H264Bitstream::avccToAnnexB(
                                BufferView(s.data, 0, s.data->size()), 4, annexB);
                        if(cerr.isError()) {
                                promekiWarn("MediaIOTask_QuickTime: AVCC->Annex-B failed "
                                            "for sample %lld: %s",
                                            static_cast<long long>(frameIndex.value()),
                                            cerr.name().cstr());
                                bitstream = s.data; // fall back to original
                        } else {
                                bitstream = annexB;
                                if(s.keyframe) {
                                        Buffer::Ptr psAnnexB;
                                        BufferView cfgView(vt.codecConfig(), 0,
                                                           vt.codecConfig()->size());
                                        Error pe;
                                        if(isH264) {
                                                AvcDecoderConfig cfg;
                                                pe = AvcDecoderConfig::parse(cfgView, cfg);
                                                if(!pe.isError()) pe = cfg.toAnnexB(psAnnexB);
                                        } else {
                                                HevcDecoderConfig cfg;
                                                pe = HevcDecoderConfig::parse(cfgView, cfg);
                                                if(!pe.isError()) pe = cfg.toAnnexB(psAnnexB);
                                        }
                                        if(!pe.isError() && psAnnexB && psAnnexB->size() > 0) {
                                                const size_t total =
                                                        psAnnexB->size() + annexB->size();
                                                auto merged = Buffer::Ptr::create(total);
                                                if(merged) {
                                                        std::memcpy(merged->data(),
                                                                    psAnnexB->data(),
                                                                    psAnnexB->size());
                                                        std::memcpy(static_cast<uint8_t *>(merged->data())
                                                                    + psAnnexB->size(),
                                                                    annexB->data(),
                                                                    annexB->size());
                                                        merged->setSize(total);
                                                        bitstream = merged;
                                                }
                                        }
                                }
                        }
                }
                auto cvp = CompressedVideoPayload::Ptr::create(idesc, bitstream);
                if(s.keyframe) cvp.modify()->addFlag(MediaPayload::Keyframe);
                videoPayload = cvp;
        } else if(samplePd.planeCount() > 1) {
                // Multi-plane uncompressed: container delivers the
                // sample as one contiguous blob; allocate the payload
                // and memcpy each plane slice out of the sample.
                auto uvp = UncompressedVideoPayload::allocate(idesc);
                if(uvp.isValid()) {
                        const uint8_t *src =
                                static_cast<const uint8_t *>(s.data->data());
                        size_t off = 0;
                        bool ok = true;
                        for(int p = 0; p < samplePd.planeCount(); ++p) {
                                const size_t psz = samplePd.planeSize(p, idesc);
                                if(off + psz > s.data->size()) { ok = false; break; }
                                std::memcpy(uvp.modify()->data()[p].data(),
                                            src + off, psz);
                                off += psz;
                        }
                        if(ok) videoPayload = uvp;
                }
        } else {
                // Single-plane uncompressed (packed YUV / raw RGB):
                // adopt the sample buffer as plane 0 zero-copy.
                BufferView planes;
                planes.pushToBack(s.data, 0, s.data->size());
                videoPayload = UncompressedVideoPayload::Ptr::create(idesc, planes);
        }

        if(!videoPayload.isValid()) {
                promekiWarn("MediaIOTask_QuickTime: failed to wrap video sample %lld as payload",
                            static_cast<long long>(frameIndex.value()));
                return Error::DecodeFailed;
        }

        frame.modify()->addPayload(videoPayload);

        // Frame metadata: timecode (anchor + frame offset), frame number,
        // keyframe flag.
        Metadata &fmeta = frame.modify()->metadata();
        fmeta.set(Metadata::FrameNumber, frameIndex);
        fmeta.set(Metadata::FrameKeyframe, s.keyframe);
        if(_anchorTimecode.isValid()) {
                FrameNumber anchorFrame = _anchorTimecode.toFrameNumber();
                if(anchorFrame.isValid()) {
                        Timecode tc = Timecode::fromFrameNumber(_anchorTimecode.mode(),
                                anchorFrame + frameIndex.value());
                        if(tc.isValid()) fmeta.set(Metadata::Timecode, tc);
                }
        }

        outFrame = frame;
        return Error::Ok;
}

Error MediaIOTask_QuickTime::readAudioSlice(uint64_t startSample, size_t samples,
                                            MediaPayload::Ptr &out) {
        if(_audioTrackIndex < 0) return Error::NotSupported;
        if(samples == 0) { out = MediaPayload::Ptr(); return Error::Ok; }

        QuickTime::Sample range;
        Error err = _qt.readSampleRange(static_cast<size_t>(_audioTrackIndex),
                                        startSample, samples, range);
        if(err.isError()) return err;
        if(!range.data.isValid()) return Error::IOError;

        const size_t rangeSize = range.data->size();
        BufferView view(range.data, 0, rangeSize);

        if(_audioDesc.isCompressed()) {
                // Compressed audio: wrap the range bytes as a single-plane
                // compressed payload so downstream consumers can decode.
                auto p = CompressedAudioPayload::Ptr::create(_audioDesc, view);
                if(!p.isValid()) return Error::DecodeFailed;
                out = p;
                return Error::Ok;
        }

        // Zero-copy: adopt the range buffer as the PCM payload's single
        // plane.  Sample count derived from buffer size / stride.
        size_t frameBytes = _audioDesc.bytesPerSample() * _audioDesc.channels();
        size_t sampleCount = (frameBytes > 0) ? (rangeSize / frameBytes) : 0;
        auto p = UncompressedAudioPayload::Ptr::create(_audioDesc, sampleCount,
                                                       view);
        if(!p.isValid()) return Error::DecodeFailed;
        out = p;
        return Error::Ok;
}

Error MediaIOTask_QuickTime::executeCmd(MediaIOCommandRead &cmd) {
        if(_mode != MediaIO::Source) return Error::NotOpen;
        stampWorkBegin();

        if(!_currentFrame.isValid()
                        || (_frameCount.isFinite() && _currentFrame.value() >= _frameCount.value())) {
                cmd.result = Error::EndOfFile;
                stampWorkEnd();
                return Error::EndOfFile;
        }

        Frame::Ptr frame;
        Error err = readVideoFrame(_currentFrame, frame);
        if(err.isError()) {
                cmd.result = err;
                stampWorkEnd();
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
                                _currentFrame.value());
                        if(_audioSampleCursor + want > trackSamples) {
                                want = (_audioSampleCursor < trackSamples)
                                        ? static_cast<size_t>(trackSamples - _audioSampleCursor)
                                        : 0;
                        }
                        toRead = want;
                }
                if(toRead > 0) {
                        MediaPayload::Ptr audioPayload;
                        Error aerr = readAudioSlice(_audioSampleCursor, toRead, audioPayload);
                        if(!aerr.isError() && audioPayload.isValid()) {
                                frame.modify()->addPayload(audioPayload);
                        }
                        _audioSampleCursor += toRead;
                }
        }

        cmd.frame        = frame;
        cmd.currentFrame = _currentFrame;

        // Advance by the requested step.
        int s = cmd.step;
        _currentFrame += s;
        if(!_currentFrame.isValid()) _currentFrame = FrameNumber(0);
        stampWorkEnd();
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Write
// ----------------------------------------------------------------------------

Error MediaIOTask_QuickTime::setupWriterFromFrame(const Frame &frame) {
        if(_writerTracksRegistered) return Error::Ok;

        // Derive the video track's pixel format and size from whichever
        // essence carries the information.  Uncompressed video payloads
        // are the common case; compressed-only frames need to fall back
        // to the pixel desc on the payload — but since a bare
        // CompressedVideoPayload does not know the picture's resolution,
        // compressed-only track registration is only possible when
        // pendingMediaDesc was supplied at open time.
        PixelFormat inferPixelFormat;
        Size2Du32 inferSize;
        auto vids = frame.videoPayloads();
        if(!vids.isEmpty() && vids[0].isValid()) {
                const ImageDesc &id = vids[0]->desc();
                inferPixelFormat = id.pixelFormat();
                inferSize        = id.size();
        } else {
                promekiErr("MediaIOTask_QuickTime: cannot infer writer tracks; first frame has no image");
                return Error::InvalidArgument;
        }

        if(!_frameRate.isValid()) {
                _frameRate = FrameRate(FrameRate::RationalType(24, 1));
                promekiWarn("MediaIOTask_QuickTime: no frame rate provided; defaulting to 24/1");
        }
        uint32_t vid = 0;
        Error err = _qt.addVideoTrack(inferPixelFormat, inferSize, _frameRate, &vid);
        if(err.isError()) return err;
        _writerVideoTrackId = vid;

        // Register an audio track if the first frame has audio and one
        // hasn't been registered via pendingMediaDesc already.
        auto auds = frame.audioPayloads();
        if(_writerAudioTrackId == 0 && !auds.isEmpty() && auds[0].isValid()) {
                const AudioDesc &ad = auds[0]->desc();
                if(ad.isValid()) {
                        AudioDesc storage = pickStorageFormat(ad);
                        uint32_t aid = 0;
                        Error aerr = _qt.addAudioTrack(storage, &aid);
                        if(!aerr.isError()) {
                                _writerAudioTrackId = aid;
                                _writerAudioStorage = storage;
                                _writerAudioFifo    = AudioBuffer(storage);
                                _writerAudioFifo.setInputFormat(ad);
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
                int64_t frameIdx = _writerFrameCount.value() - 1;
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
        auto [got, popErr] = _writerAudioFifo.pop(buf.data(), toEmit);
        if(popErr.isError()) return popErr;
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
        if(_mode != MediaIO::Sink) return Error::NotOpen;
        if(!cmd.frame.isValid()) return Error::InvalidArgument;
        stampWorkBegin();
        const Frame &frame = *cmd.frame;

        Error err = setupWriterFromFrame(frame);
        if(err.isError()) { stampWorkEnd(); return err; }

        // Build the video sample from the first video payload.  When
        // the frame carries a @ref CompressedVideoPayload (the canonical
        // representation for encoder output and container demux), its
        // bytes take precedence over any uncompressed plane — the view
        // may cover a subset of a larger backing buffer, or a remapped
        // Annex-B byte stream built from the container's AVCC
        // length-prefixed samples.
        auto vidsWrite = frame.videoPayloads();
        if(vidsWrite.isEmpty()) {
                promekiWarn("MediaIOTask_QuickTime: write with no image; skipping");
                stampWorkEnd();
                return Error::InvalidArgument;
        }

        QuickTime::Sample s;
        s.trackId  = _writerVideoTrackId;
        s.duration = 0;  // let the writer derive from track frame rate by default.
        s.keyframe = true;

        const VideoPayload &vp = *vidsWrite[0];
        if(const auto *cvp = vp.as<CompressedVideoPayload>()) {
                // Compressed access unit — prefer plane(0)'s view zero-
                // copy when it covers its backing buffer in full, else
                // deep-copy into a fresh Buffer so the writer's
                // sample.data semantics (size == payload size) stay
                // clean.
                if(cvp->planeCount() == 0) {
                        promekiWarn("MediaIOTask_QuickTime: compressed payload has no planes; skipping");
                        stampWorkEnd();
                        return Error::InvalidArgument;
                }
                auto view = cvp->plane(0);
                if(!view.isValid() || view.size() == 0) {
                        promekiWarn("MediaIOTask_QuickTime: payload plane has no bytes; skipping");
                        stampWorkEnd();
                        return Error::InvalidArgument;
                }
                const Buffer::Ptr &backing = view.buffer();
                if(backing && view.offset() == 0 && view.size() == backing->size()) {
                        s.data = backing;
                } else {
                        Buffer copy(view.size());
                        std::memcpy(copy.data(), view.data(), view.size());
                        copy.setSize(view.size());
                        s.data = Buffer::Ptr::create(std::move(copy));
                }
                s.keyframe = cvp->isKeyframe();
        } else if(const auto *uvp = vp.as<UncompressedVideoPayload>()) {
                // Uncompressed: single-plane formats adopt plane 0's
                // backing Buffer directly; multi-plane formats are
                // concatenated plane-by-plane into one contiguous
                // sample because QuickTime's uncompressed sample
                // entries carry exactly one mdat payload per frame.
                const size_t pc = uvp->planeCount();
                if(pc == 0) {
                        promekiWarn("MediaIOTask_QuickTime: uncompressed payload has no planes");
                        stampWorkEnd();
                        return Error::InvalidArgument;
                }
                if(pc > 1) {
                        size_t total = 0;
                        for(size_t p = 0; p < pc; ++p) total += uvp->plane(p).size();
                        Buffer concat(total);
                        size_t off = 0;
                        for(size_t p = 0; p < pc; ++p) {
                                auto pv = uvp->plane(p);
                                std::memcpy(static_cast<uint8_t *>(concat.data()) + off,
                                            pv.data(), pv.size());
                                off += pv.size();
                        }
                        concat.setSize(total);
                        s.data = Buffer::Ptr::create(std::move(concat));
                } else {
                        auto pv = uvp->plane(0);
                        if(!pv.isValid() || pv.size() == 0) {
                                promekiWarn("MediaIOTask_QuickTime: uncompressed plane is empty");
                                stampWorkEnd();
                                return Error::InvalidArgument;
                        }
                        const Buffer::Ptr &backing = pv.buffer();
                        if(backing && pv.offset() == 0 && pv.size() == backing->size()) {
                                s.data = backing;
                        } else {
                                Buffer copy(pv.size());
                                std::memcpy(copy.data(), pv.data(), pv.size());
                                copy.setSize(pv.size());
                                s.data = Buffer::Ptr::create(std::move(copy));
                        }
                }
                if(frame.metadata().contains(Metadata::FrameKeyframe)) {
                        s.keyframe = frame.metadata().get(Metadata::FrameKeyframe).get<bool>();
                }
        } else {
                promekiWarn("MediaIOTask_QuickTime: unsupported video payload class");
                stampWorkEnd();
                return Error::InvalidArgument;
        }

        err = _qt.writeSample(_writerVideoTrackId, s);
        if(err.isError()) { stampWorkEnd(); return err; }

        _writerFrameCount++;
        _writerFramesSinceFlush++;

        // Push incoming audio into the FIFO (converting format if needed),
        // then drain one video-frame's worth into the engine. Any residual
        // audio stays in the FIFO until the next frame or close().
        auto audsWrite = frame.audioPayloads();
        if(_writerAudioTrackId != 0 && !audsWrite.isEmpty() && audsWrite[0].isValid()) {
                const auto *uap = audsWrite[0]->as<UncompressedAudioPayload>();
                if(uap != nullptr && uap->planeCount() > 0) {
                        auto view = uap->plane(0);
                        Error aerr = _writerAudioFifo.push(view.data(),
                                uap->sampleCount(), uap->desc());
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

        cmd.currentFrame = toFrameNumber(_writerFrameCount);
        cmd.frameCount   = _writerFrameCount;
        stampWorkEnd();
        return Error::Ok;
}

// ----------------------------------------------------------------------------
// Seek
// ----------------------------------------------------------------------------

Error MediaIOTask_QuickTime::executeCmd(MediaIOCommandSeek &cmd) {
        if(_mode != MediaIO::Source) return Error::IllegalSeek;
        if(_videoTrackIndex < 0) return Error::IllegalSeek;
        int64_t target = cmd.frameNumber.isValid() ? cmd.frameNumber.value() : 0;
        if(target < 0) target = 0;
        if(_frameCount.isFinite() && target >= _frameCount.value()) {
                target = _frameCount.value() - 1;
        }
        _currentFrame = FrameNumber(target);
        cmd.currentFrame = _currentFrame;
        return Error::Ok;
}

// ---- Phase 3 introspection / negotiation overrides ----
//
// QuickTime's writer is permissive at the @c writeSample byte level
// — it copies whatever the upstream stage hands it under the
// configured FourCC — but only a subset of those FourCCs are
// understood by real-world QuickTime / ISO-BMFF readers.  The
// supported set below is curated against (a) what shipped writer
// code paths actually do (avcC/hvcC extraction for H.264/HEVC,
// ProRes / JPEG / JPEG XS sample-entry boxes) and (b) which
// uncompressed FourCCs the dominant QT decoders (Apple AVFoundation,
// FFmpeg's mov demuxer) interpret correctly:
//
//   - Compressed: H.264, HEVC, AV1, all ProRes flavours, MJPEG,
//     JPEG XS.
//   - Uncompressed RGB: "raw " (RGB8), "RGBA" (RGBA8).
//   - Uncompressed YUV 4:2:2 8-bit: YUYV ("YUY2"), UYVY ("2vuy").
//   - Uncompressed YUV 4:2:2 10-bit packed: "v210".
//   - Uncompressed YUV planar: I422 ("yuv2"-class), I420 ("yv12"-class).
//   - Uncompressed YUV semi-planar: NV12, NV16.
//
// NV21 deliberately omitted — it has a fourcc registered in our
// PixelFormat DB but no QuickTime reader interprets it.  Same for
// 10-bit planar / semi-planar YUV variants beyond v210.

bool MediaIOTask_QuickTime::isSupportedPixelFormat(const PixelFormat &pd) {
        if(!pd.isValid()) return false;
        switch(pd.id()) {
                // Compressed video that the writer either passes
                // through verbatim or post-processes (avcC/hvcC).
                case PixelFormat::H264:
                case PixelFormat::HEVC:
                case PixelFormat::AV1:
                // JPEG (MJPEG in a QuickTime container) — every
                // variant the JpegVideoEncoder produces is byte-for-
                // byte compatible with the "mjpg"/"JPEG" sample
                // entry, so the planner can route any of them
                // straight through the writer without re-encoding.
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
                // JPEG XS is deliberately NOT listed here: the writer
                // can produce a "jxsv" sample entry but the reader has
                // no complementary container path wired up yet, so any
                // round-trip through a .mov would fail on the read
                // side (see project_mediaio notes on JPEG XS).  When
                // the reader catches up, add the JPEG_XS_* variants
                // here so the planner stops routing them through an
                // encoder bridge.
                case PixelFormat::ProRes_422_Proxy:
                case PixelFormat::ProRes_422_LT:
                case PixelFormat::ProRes_422:
                case PixelFormat::ProRes_422_HQ:
                case PixelFormat::ProRes_4444:
                case PixelFormat::ProRes_4444_XQ:
                // Uncompressed RGB.
                case PixelFormat::RGB8_sRGB:
                case PixelFormat::RGBA8_sRGB:
                // Uncompressed YUV 4:2:2 8-bit packed.
                case PixelFormat::YUV8_422_Rec709:
                case PixelFormat::YUV8_422_Rec601:
                case PixelFormat::YUV8_422_UYVY_Rec709:
                case PixelFormat::YUV8_422_UYVY_Rec601:
                // Uncompressed YUV 4:2:2 10-bit packed (v210).
                case PixelFormat::YUV10_422_v210_Rec709:
                // Uncompressed YUV 4:2:2 planar 8-bit (I422).
                case PixelFormat::YUV8_422_Planar_Rec709:
                // Uncompressed YUV 4:2:0 planar 8-bit (I420).
                case PixelFormat::YUV8_420_Planar_Rec709:
                case PixelFormat::YUV8_420_Planar_Rec601:
                // Uncompressed YUV 4:2:0 / 4:2:2 semi-planar 8-bit.
                case PixelFormat::YUV8_420_SemiPlanar_Rec709:
                case PixelFormat::YUV8_420_SemiPlanar_Rec601:
                case PixelFormat::YUV8_422_SemiPlanar_Rec709:
                        return true;
                default:
                        return false;
        }
}

PixelFormat MediaIOTask_QuickTime::pickSupportedPixelFormat(const PixelFormat &offered) {
        if(isSupportedPixelFormat(offered)) return offered;

        // Compressed-but-unsupported sources need to be transcoded —
        // hand off to the planner via VideoEncoder.  Indicate the
        // canonical mp4 codec.
        if(offered.isCompressed()) return PixelFormat(PixelFormat::H264);

        const bool offerYuv = offered.isValid()
                && offered.colorModel().type() == ColorModel::TypeYCbCr;
        const int  offerBits = (offered.isValid()
                && offered.memLayout().compCount() > 0)
                ? static_cast<int>(offered.memLayout().compDesc(0).bits)
                : 8;

        if(offerYuv) {
                // YUV source: pick the closest supported YUV form by
                // chroma subsampling and bit depth.
                const auto sampling = offered.memLayout().sampling();
                if(offerBits >= 10 && sampling != PixelMemLayout::Sampling420) {
                        return PixelFormat(PixelFormat::YUV10_422_v210_Rec709);
                }
                if(sampling == PixelMemLayout::Sampling420) {
                        return PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709);
                }
                return PixelFormat(PixelFormat::YUV8_422_Rec709);
        }
        // RGB source — match alpha presence; QT supports 8-bit RGB
        // and RGBA only.
        return PixelFormat(PixelFormat::RGBA8_sRGB);
}

Error MediaIOTask_QuickTime::proposeInput(const MediaDesc &offered,
                                          MediaDesc *preferred) const {
        if(preferred == nullptr) return Error::Invalid;
        if(offered.imageList().isEmpty()) {
                // Audio-only frame — let the AudioFile-equivalent
                // PCM path accept whatever; QT writer handles a wide
                // PCM set already.
                *preferred = offered;
                return Error::Ok;
        }

        const PixelFormat &offeredPd = offered.imageList()[0].pixelFormat();
        const PixelFormat target = pickSupportedPixelFormat(offeredPd);
        if(target == offeredPd) {
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
