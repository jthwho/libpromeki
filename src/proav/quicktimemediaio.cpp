/**
 * @file      quicktimemediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>

#include <promeki/ancformat.h>
#include <promeki/ancpayload.h>
#include <promeki/anctranslator.h>
#include <promeki/audiopayload.h>
#include <promeki/cea608packet.h>
#include <promeki/cea708cdp.h>
#include <promeki/colormodel.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/enums_mediaio.h>
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
#include <promeki/qtclosedcaption.h>
#include <promeki/quicktimemediaio.h>
#include <promeki/st291packet.h>
#include <promeki/st436m.h>
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
        // source.  The writer stores the six canonical PCM types
        // (sowt/twos/in24/in32/fl32/raw) via their classic single-FourCC
        // sound sample entries, and every other interleaved PCM
        // permutation faithfully via a v2 'lpcm' sound description whose
        // formatSpecificFlags carry endianness / signedness / float /
        // alignment — so no lossy promotion is needed.  The only
        // adjustment here is planar → interleaved: containers store
        // interleaved PCM, so a planar (PCMP_*) memory layout maps to
        // its interleaved (PCMI_*) on-disk equivalent of the same sample
        // type.  Compressed audio passes through untouched.
        AudioDesc pickStorageFormat(const AudioDesc &src) {
                if (!src.isValid()) return src;
                const AudioFormat &fmt = src.format();
                if (fmt.isCompressed()) return src;
                if (fmt.isPlanar()) {
                        const String &n = fmt.name();
                        if (n.startsWith(String("PCMP_"))) {
                                String              iname = String("PCMI_") + n.mid(5);
                                Result<AudioFormat> r = AudioFormat::lookup(iname);
                                if (r.second().isOk())
                                        return AudioDesc(r.first().id(), src.sampleRate(), src.channels());
                        }
                }
                return src;
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

        // Returns true when @p frame carries at least one ancillary packet
        // we can derive c608 captions from — a CEA-708 CDP (AncFormat::Cea708)
        // or a raw SMPTE 334-1 line-21 CEA-608 packet (AncFormat::Cea608).
        bool frameHasCaptions(const Frame &frame) {
                for (const AncPayload::Ptr &ap : frame.ancPayloads()) {
                        if (!ap.isValid()) continue;
                        for (const AncPacket &pkt : ap->packets()) {
                                const AncFormat::ID fid = pkt.format().id();
                                if (fid == AncFormat::Cea708 || fid == AncFormat::Cea608) return true;
                        }
                }
                return false;
        }

        // Decodes the cc_data triples carried by a CEA-708 CDP ancillary
        // packet (the CDP bytes are the ST 291 packet's 8-bit user-data
        // words).  Returns an empty list for non-CEA-708 packets or on a
        // malformed CDP.
        Cea708Cdp::CcDataList ccDataFromCea708Packet(const AncPacket &pkt) {
                Cea708Cdp::CcDataList out;
                if (pkt.format().id() != AncFormat::Cea708) return out;
                Result<St291Packet> sp = St291Packet::from(pkt);
                if (sp.second().isError()) return out;
                List<uint16_t> udw = sp.first().udw();
                Buffer         cdpBuf(udw.size());
                uint8_t       *b = static_cast<uint8_t *>(cdpBuf.data());
                for (size_t i = 0; i < udw.size(); ++i) b[i] = static_cast<uint8_t>(udw[i] & 0xFF);
                cdpBuf.setSize(udw.size());
                Result<Cea708Cdp> cdp = Cea708Cdp::fromBuffer(cdpBuf);
                if (cdp.second().isOk()) out = cdp.first().ccData;
                return out;
        }

        // Decodes the cc_data triples carried by a raw SMPTE 334-1 line-21
        // CEA-608 ancillary packet (AncFormat::Cea608) through the registered
        // ANC codec.  Returns an empty list for other formats or a packet
        // that fails to parse.
        Cea708Cdp::CcDataList ccDataFromCea608Packet(const AncPacket &pkt) {
                Cea708Cdp::CcDataList out;
                if (pkt.format().id() != AncFormat::Cea608) return out;
                AncTranslator              t;
                AncTranslator::ParseResult parsed = t.parse(pkt);
                if (parsed.second().isError()) return out;
                return parsed.first().get<Cea608Packet>().ccData;
        }

        // Collects the CEA-608 (cc_type 0/1) cc_data triples from every
        // caption-bearing ancillary packet on @p frame — both CEA-708 CDP
        // (AncFormat::Cea708) and raw line-21 (AncFormat::Cea608) — for
        // emission into a QuickTime c608 caption track.  (encode608() keeps
        // only cc_type 0/1, so the 708 DTVCC triples are dropped at encode
        // time.)
        Cea708Cdp::CcDataList collectCaption608(const Frame &frame) {
                Cea708Cdp::CcDataList all;
                for (const AncPayload::Ptr &ap : frame.ancPayloads()) {
                        if (!ap.isValid()) continue;
                        for (const AncPacket &pkt : ap->packets()) {
                                Cea708Cdp::CcDataList cc = ccDataFromCea708Packet(pkt);
                                for (size_t i = 0; i < cc.size(); ++i) all.pushToBack(cc[i]);
                                Cea708Cdp::CcDataList cc608 = ccDataFromCea608Packet(pkt);
                                for (size_t i = 0; i < cc608.size(); ++i) all.pushToBack(cc608[i]);
                        }
                }
                return all;
        }

        // True when @p pkts already carries CEA-608 caption content — either
        // a raw CEA-608 packet or a CEA-708 CDP whose cc_data has a cc_type
        // 0/1 (field 1/2) triple.  Used by the Auto read policy to avoid
        // duplicating 608 that already rides inside a 708 CDP.
        bool ancListHas608(const AncPacket::List &pkts) {
                for (size_t i = 0; i < pkts.size(); ++i) {
                        const AncFormat::ID fid = pkts[i].format().id();
                        if (fid == AncFormat::Cea608) return true;
                        if (fid == AncFormat::Cea708) {
                                Cea708Cdp::CcDataList cc = ccDataFromCea708Packet(pkts[i]);
                                for (size_t j = 0; j < cc.size(); ++j)
                                        if (cc[j].valid && (cc[j].type == 0 || cc[j].type == 1)) return true;
                        }
                }
                return false;
        }

        // Returns @p pkts with every CEA-608/708 caption packet removed.
        AncPacket::List stripCaptionPackets(const AncPacket::List &pkts) {
                AncPacket::List out;
                for (size_t i = 0; i < pkts.size(); ++i) {
                        const AncFormat::ID fid = pkts[i].format().id();
                        if (fid == AncFormat::Cea708 || fid == AncFormat::Cea608) continue;
                        out.pushToBack(pkts[i]);
                }
                return out;
        }

        // Reconstructs a CEA-708 CDP ANC packet from a c608 caption sample
        // so a caption track can be surfaced into the ANC model on read.
        AncPacket reconstructCaptionAnc(const Buffer &c608Sample, const FrameRate &frameRate) {
                Cea708Cdp::CcDataList cc = QtClosedCaption::decode608(c608Sample);
                if (cc.isEmpty()) return AncPacket();
                Cea708Cdp      cdp(Cea708Cdp::frameRateCodeFor(frameRate), cc);
                Buffer         cdpBytes = cdp.toBuffer();
                List<uint16_t> udw;
                const uint8_t *p = static_cast<const uint8_t *>(cdpBytes.data());
                for (size_t i = 0; i < cdpBytes.size(); ++i) udw.pushToBack(p[i]);
                St291Packet sp =
                        St291Packet::build(AncFormat(AncFormat::Cea708), udw, St291Packet::UnspecifiedLine);
                return sp.packet();
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
        s(MediaConfig::QuickTimeCaptionReadPolicy, QuickTimeCaptionReadPolicy::Auto);
        // PCM = uncompressed lpcm track (historical default).  Set to a
        // compressed codec to have the planner splice an AudioEncoder.
        s(MediaConfig::QuickTimeAudioCodec, AudioCodec(AudioCodec::PCM));
        // Invalid = passthrough (store the offered video as-is).  Set to a
        // compressed codec to have the planner splice a VideoEncoder.
        s(MediaConfig::QuickTimeVideoCodec, VideoCodec());
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
        const bool isWrite = modeEnum == MediaIOOpenMode::Write;
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

                // First ST 436M ancillary-data track, if any.
                for (size_t i = 0; i < _qt.tracks().size(); ++i) {
                        if (_qt.tracks()[i].type() == QuickTime::AncData) {
                                _ancTrackIndex = static_cast<int>(i);
                                _ancDesc = _qt.tracks()[i].ancDesc();
                                break;
                        }
                }
                // First CEA-608 caption track, if any, plus the read policy.
                for (size_t i = 0; i < _qt.tracks().size(); ++i) {
                        if (_qt.tracks()[i].type() == QuickTime::Caption) {
                                _captionTrackIndex = static_cast<int>(i);
                                break;
                        }
                }
                {
                        Error policyErr;
                        Enum  policyEnum = cfg.get(MediaConfig::QuickTimeCaptionReadPolicy)
                                                  .asEnum(QuickTimeCaptionReadPolicy::Type, &policyErr);
                        if (!policyErr.isError() && policyEnum.hasListedValue())
                                _captionReadPolicy = QuickTimeCaptionReadPolicy(policyEnum.value());
                }

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

                        // For compressed audio, derive how many PCM samples one
                        // access unit decodes to (the stts per-sample delta,
                        // converted into the output sample rate).  This lets the
                        // read loop pull enough access units per video frame to
                        // keep the decoded PCM timeline aligned with the frame
                        // rate — reading a single AAC frame (1024 samples) per
                        // 23.98 fps frame (which spans ~2002 samples) otherwise
                        // under-delivers and the downstream resampler stretches
                        // the audio to half speed / an octave low.
                        _audioCompressedFrameSamples = 0;
                        if (_audioDesc.isCompressed() && at.sampleCount() > 0 && at.timescale() > 0 &&
                            at.duration() > 0) {
                                const double ticksPerUnit =
                                        static_cast<double>(at.duration()) / static_cast<double>(at.sampleCount());
                                const double pcmPerUnit = ticksPerUnit * static_cast<double>(_audioDesc.sampleRate()) /
                                                          static_cast<double>(at.timescale());
                                _audioCompressedFrameSamples = static_cast<uint64_t>(pcmPerUnit + 0.5);
                        }
                }

                MediaDesc mediaDesc;
                if (_videoTrackIndex >= 0) {
                        const QuickTime::Track &vt = _qt.tracks()[_videoTrackIndex];
                        ImageDesc               idesc(vt.size(), vt.pixelFormat());
                        idesc.setVideoScanMode(vt.videoScanMode());
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

                // Pick the container brand profile from the sink filename
                // extension: .mp4 / .m4v / .m4a write a true ISO-BMFF MP4
                // (major brand 'isom'), while .mov / .qt keep the Apple
                // QuickTime brand. The H.26x sample entries are identical
                // either way; only the ftyp advertisement differs.
                const String lowerName = _filename.toLower();
                if (lowerName.endsWith(".mp4") || lowerName.endsWith(".m4v") ||
                    lowerName.endsWith(".m4a")) {
                        _qt.setProfile(QuickTime::ProfileMp4);
                }

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
                        // Scan mode for the track's fiel box: session config, or
                        // the descriptor's own (authoritative) videoScanMode().
                        _qt.setVideoScanMode(resolveWriteScanMode(idesc.videoScanMode()));
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
                        // Compressed audio (AAC) is written one access unit per
                        // sample directly from the incoming CompressedAudioPayloads
                        // — it does not flow through the PCM rechunking FIFO.
                        if (!storage.isCompressed()) {
                                _writerAudioFifo = AudioBuffer(storage);
                                _writerAudioFifo.setInputFormat(srcDesc);
                                // Reserve ~1 second of headroom to absorb frame-rate jitter.
                                _writerAudioFifo.reserve(static_cast<size_t>(storage.sampleRate()));
                        }
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

Error QuickTimeMediaIO::readVideoFrame(const FrameNumber &frameIndex, Frame &outFrame) {
        if (_videoTrackIndex < 0) return Error::NotSupported;
        if (!frameIndex.isValid()) return Error::IllegalSeek;

        QuickTime::Sample s;
        Error err = _qt.readSample(static_cast<size_t>(_videoTrackIndex), static_cast<uint64_t>(frameIndex.value()), s);
        if (err.isError()) return err;
        if (!s.data.isValid()) return Error::IOError;

        const QuickTime::Track &vt = _qt.tracks()[_videoTrackIndex];

        Frame frame = Frame();

        const PixelFormat &samplePd = vt.pixelFormat();
        const size_t       sampleWidth = vt.size().width();
        const size_t       sampleHeight = vt.size().height();
        ImageDesc          idesc(Size2Du32(sampleWidth, sampleHeight), samplePd);
        idesc.metadata() = vt.metadata();
        idesc.setVideoScanMode(vt.videoScanMode());

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

        frame.addPayload(videoPayload);

        Metadata &fmeta = frame.metadata();
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

        Frame frame;
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
                        // Pull enough compressed access units so the cumulative
                        // decoded PCM samples keep pace with the video timeline.
                        // Each unit decodes to _audioCompressedFrameSamples PCM
                        // samples; the target is the cumulative PCM count at the
                        // end of the current video frame.  Without this the reader
                        // delivers one access unit per frame regardless of how
                        // much audio a frame actually spans, starving the
                        // downstream resampler (half-speed / octave-low audio).
                        if (_audioSampleCursor < trackSamples && _audioCompressedFrameSamples > 0) {
                                const uint64_t targetPcm = static_cast<uint64_t>(_frameRate.cumulativeTicks(
                                        static_cast<int64_t>(_audioDesc.sampleRate()), _currentFrame.value() + 1));
                                const uint64_t remaining = trackSamples - _audioSampleCursor;
                                uint64_t       projected = _audioSampleCursor * _audioCompressedFrameSamples;
                                uint64_t       want = 0;
                                while (projected < targetPcm && want < remaining) {
                                        ++want;
                                        projected += _audioCompressedFrameSamples;
                                }
                                // Never stall on a frame that still has audio left
                                // to deliver (guards against a degenerate target).
                                if (want == 0 && remaining > 0 && targetPcm == 0) want = 1;
                                toRead = static_cast<size_t>(want);
                        } else if (_audioSampleCursor < trackSamples) {
                                toRead = 1;
                        }
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
                        if (_audioDesc.isCompressed()) {
                                // Emit each compressed access unit as its own
                                // payload so the decoder can decode them one at a
                                // time.  Raw AAC access units carry no sync words,
                                // so concatenating several into a single payload
                                // would leave the decoder unable to find the
                                // boundaries (it decodes the first and errors on
                                // the rest).  One payload == one access unit keeps
                                // the boundaries explicit.
                                for (size_t k = 0; k < toRead; ++k) {
                                        MediaPayload::Ptr audioPayload;
                                        Error             aerr =
                                                readAudioSlice(_audioSampleCursor + k, 1, audioPayload);
                                        if (!aerr.isError() && audioPayload.isValid()) {
                                                frame.addPayload(audioPayload);
                                        }
                                }
                        } else {
                                MediaPayload::Ptr audioPayload;
                                Error             aerr = readAudioSlice(_audioSampleCursor, toRead, audioPayload);
                                if (!aerr.isError() && audioPayload.isValid()) {
                                        frame.addPayload(audioPayload);
                                }
                        }
                        _audioSampleCursor += toRead;
                }
        }

        // Ancillary data + captions: assemble the per-frame ANC packet list
        // from the ST 436M vanc track and the c608 caption track per the
        // configured read policy (QuickTimeCaptionReadPolicy).
        {
                const uint64_t  fi = static_cast<uint64_t>(_currentFrame.value());
                AncPacket::List ancPackets;

                // 1. Full-fidelity ANC from the ST 436M vanc track.
                if (_ancTrackIndex >= 0 && fi < _qt.tracks()[_ancTrackIndex].sampleCount()) {
                        QuickTime::Sample as;
                        if (_qt.readSample(static_cast<size_t>(_ancTrackIndex), fi, as).isOk() &&
                            as.data.isValid()) {
                                Result<AncPacket::List> dec = St436m::decodeFrame(as.data);
                                if (dec.second().isOk())
                                        for (const AncPacket &pkt : dec.first()) ancPackets.pushToBack(pkt);
                        }
                }

                // 2. c608 caption track, per policy (VancOnly skips it).
                if (_captionTrackIndex >= 0 && _captionReadPolicy != QuickTimeCaptionReadPolicy::VancOnly &&
                    fi < _qt.tracks()[_captionTrackIndex].sampleCount()) {
                        bool inject = false;
                        if (_captionReadPolicy == QuickTimeCaptionReadPolicy::Auto) {
                                // Only when the ANC has no 608 of its own — a 608
                                // riding inside a 708 CDP counts, so we don't
                                // duplicate it.
                                inject = !ancListHas608(ancPackets);
                        } else { // CaptionTrackOnly — the caption track is authoritative.
                                inject = true;
                                ancPackets = stripCaptionPackets(ancPackets);
                        }
                        if (inject) {
                                QuickTime::Sample cs;
                                if (_qt.readSample(static_cast<size_t>(_captionTrackIndex), fi, cs).isOk() &&
                                    cs.data.isValid()) {
                                        AncPacket cap = reconstructCaptionAnc(cs.data, _frameRate);
                                        if (cap.isValid()) ancPackets.pushToBack(cap);
                                }
                        }
                }

                if (!ancPackets.isEmpty()) {
                        AncPayload::Ptr ancPayload = AncPayload::Ptr::create(_ancDesc);
                        for (const AncPacket &pkt : ancPackets) ancPayload.modify()->addPacket(pkt);
                        frame.addPayload(ancPayload);
                }
        }

        cmd.frame = frame;
        cmd.currentFrame = _currentFrame;

        const int s = (cmd.group != nullptr) ? cmd.group->nextStep() : 1;
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
        // Scan mode for the track's fiel box: session config, or the first
        // video payload's own (authoritative) ImageDesc::videoScanMode().
        VideoScanMode descScan(VideoScanMode::Unknown);
        if (!vids.isEmpty() && vids[0].isValid()) descScan = vids[0]->desc().videoScanMode();
        _qt.setVideoScanMode(resolveWriteScanMode(descScan));
        Error err = _qt.addVideoTrack(inferPixelFormat, inferSize, _frameRate, &vid);
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
                                // Compressed audio bypasses the PCM rechunk FIFO
                                // (written one access unit per sample below).
                                if (!storage.isCompressed()) {
                                        _writerAudioFifo = AudioBuffer(storage);
                                        _writerAudioFifo.setInputFormat(ad);
                                        _writerAudioFifo.reserve(static_cast<size_t>(storage.sampleRate()));
                                }
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
        // Compressed audio is written directly per access unit, not through the
        // PCM FIFO — nothing to drain.
        if (_writerAudioStorage.isCompressed()) return Error::Ok;

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
        const Frame &frame = cmd.frame;

        Error err = setupWriterFromFrame(frame);
        if (err.isError()) {
                return err;
        }

        // Lazily create the ST 436M ancillary-data track from the first
        // frame that carries ANC payloads.  Done here (before the video
        // writeSample) so the fragmented init-moov includes the track.
        // Note: for the fragmented layout, ANC must appear on the first
        // written frame; the classic layout has no such ordering need.
        if (_writerAncTrackId == 0) {
                auto ancsFirst = frame.ancPayloads();
                if (!ancsFirst.isEmpty() && ancsFirst[0].isValid() && _frameRate.isValid()) {
                        uint32_t anid = 0;
                        Error    anerr = _qt.addAncTrack(ancsFirst[0]->desc(), _frameRate, &anid);
                        if (!anerr.isError()) _writerAncTrackId = anid;
                        else
                                promekiWarn("QuickTimeMediaIO: addAncTrack failed: %s", anerr.name().cstr());
                }
        }

        // Lazily create a player-renderable CEA-608 (c608) caption track
        // alongside the full-fidelity ST 436M track when the first frame
        // carries caption packets (CEA-708 CDP or raw line-21 CEA-608).
        // Same ordering rationale as the ANC track above.
        if (_writerCaptionTrackId == 0 && _frameRate.isValid() && frameHasCaptions(frame)) {
                uint32_t cid = 0;
                Error    cerr = _qt.addCaptionTrack(_frameRate, &cid);
                if (!cerr.isError()) _writerCaptionTrackId = cid;
                else
                        promekiWarn("QuickTimeMediaIO: addCaptionTrack failed: %s", cerr.name().cstr());
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

                // The QuickTime writer builds the avc1/hvc1 avcC/hvcC config box
                // by extracting SPS/PPS(/VPS) from the first keyframe's in-band
                // NAL units.  A planner-spliced encoder may carry parameter sets
                // only out-of-band — e.g. x264 with VideoRepeatHeaders off emits
                // them solely via Metadata::CodecParameterSets — leaving keyframe
                // AUs header-less.  Prepend the (Annex-B) parameter sets to such
                // keyframes so the config box can be built; the writer's
                // annexBToAvccFiltered() strips them back out of the stored
                // sample, so they only ever feed the config record.
                const PixelFormat::ID vpid = cvp->desc().pixelFormat().id();
                if (s.keyframe && (vpid == PixelFormat::H264 || vpid == PixelFormat::HEVC) &&
                    cvp->metadata().contains(Metadata::CodecParameterSets)) {
                        const String ps = cvp->metadata().getAs<String>(Metadata::CodecParameterSets);
                        if (!ps.isEmpty()) {
                                Buffer merged(ps.size() + s.data.size());
                                auto *dst = static_cast<uint8_t *>(merged.data());
                                std::memcpy(dst, ps.cstr(), ps.size());
                                std::memcpy(dst + ps.size(), s.data.data(), s.data.size());
                                merged.setSize(ps.size() + s.data.size());
                                s.data = std::move(merged);
                        }
                }
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
        if (_writerAudioTrackId != 0 && _writerAudioStorage.isCompressed()) {
                // Compressed audio (AAC): write every access unit on the frame
                // as its own sample.  A single video frame can carry several
                // access units (the reader paces them to the frame's audio
                // span), and each must keep its own boundary — so iterate all
                // CompressedAudioPayloads, not just the first.  stts duration
                // is the AU's decoded PCM sample count (e.g. 1024 for AAC-LC).
                for (const AudioPayload::Ptr &ap : audsWrite) {
                        if (!ap.isValid()) continue;
                        const auto *cap = ap->as<CompressedAudioPayload>();
                        if (cap == nullptr || cap->planeCount() == 0) continue;
                        auto view = cap->plane(0);
                        if (view.size() == 0) continue;
                        Buffer aubuf(view.size());
                        std::memcpy(aubuf.data(), view.data(), view.size());
                        aubuf.setSize(view.size());

                        QuickTime::Sample as;
                        as.trackId = _writerAudioTrackId;
                        as.data = Buffer(std::move(aubuf));
                        as.duration = static_cast<int64_t>(cap->sampleCount()); // PCM samples per AU
                        as.keyframe = true;                                     // every AAC AU is a sync sample
                        Error aerr = _qt.writeSample(_writerAudioTrackId, as);
                        if (aerr.isError()) {
                                promekiWarn("QuickTimeMediaIO: compressed audio writeSample failed: %s",
                                            aerr.name().cstr());
                        }
                }
        } else if (_writerAudioTrackId != 0 && !audsWrite.isEmpty() && audsWrite[0].isValid()) {
                const auto *uap = audsWrite[0]->as<PcmAudioPayload>();
                if (uap != nullptr && uap->planeCount() > 0) {
                        // Payload-aware push threads the payload's PTS
                        // through the FIFO's anchor queue automatically.
                        Error aerr = _writerAudioFifo.push(*uap);
                        if (aerr.isError()) {
                                promekiWarn("QuickTimeMediaIO: audio push failed: %s", aerr.name().cstr());
                        }
                }
        }
        drainWriterAudio(/*flush=*/false);

        // ST 436M ancillary data: write exactly one sample per video frame
        // (an empty 2-byte value when this frame carries no packets) so the
        // ANC track stays sample-aligned with the video track for indexed
        // reads.
        if (_writerAncTrackId != 0) {
                AncPacket::List allPackets;
                AncDesc         ancDesc;
                for (const AncPayload::Ptr &ap : frame.ancPayloads()) {
                        if (!ap.isValid()) continue;
                        if (!ancDesc.isValid()) ancDesc = ap->desc();
                        for (const AncPacket &pkt : ap->packets()) allPackets.pushToBack(pkt);
                }
                Buffer            ancSample = St436m::encodeFrame(allPackets, ancDesc);
                QuickTime::Sample asamp;
                asamp.trackId = _writerAncTrackId;
                asamp.data = ancSample;
                asamp.duration = 0; // writer derives from the track frame rate
                asamp.keyframe = true;
                Error anerr = _qt.writeSample(_writerAncTrackId, asamp);
                if (anerr.isError())
                        promekiWarn("QuickTimeMediaIO: ANC writeSample failed: %s", anerr.name().cstr());
        }

        // CEA-608 caption track: one c608 sample per video frame derived
        // from the frame's CEA-708 CDP and raw line-21 CEA-608 packets (an
        // empty cdat atom when this frame has none), keeping it
        // sample-aligned with the video track.
        if (_writerCaptionTrackId != 0) {
                Buffer            capSample = QtClosedCaption::encode608(collectCaption608(frame));
                QuickTime::Sample csamp;
                csamp.trackId = _writerCaptionTrackId;
                csamp.data = capSample;
                csamp.duration = 0; // writer derives from the track frame rate
                csamp.keyframe = true;
                Error cerr = _qt.writeSample(_writerCaptionTrackId, csamp);
                if (cerr.isError())
                        promekiWarn("QuickTimeMediaIO: caption writeSample failed: %s", cerr.name().cstr());
        }

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

        // Reposition the audio read cursor to match the seek target so audio
        // stays in sync after a seek.  The cursor counts PCM samples for PCM
        // tracks and compressed access units for compressed tracks.
        if (_audioTrackIndex >= 0 && _frameRate.isValid()) {
                const int64_t cumPcm =
                        _frameRate.cumulativeTicks(static_cast<int64_t>(_audioDesc.sampleRate()), target);
                if (_audioDesc.isCompressed()) {
                        _audioSampleCursor = (_audioCompressedFrameSamples > 0)
                                                     ? static_cast<uint64_t>(cumPcm) / _audioCompressedFrameSamples
                                                     : 0;
                } else {
                        _audioSampleCursor = static_cast<uint64_t>(cumPcm);
                }
        }

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

        MediaDesc want = offered;
        // Video axis, step 1: when QuickTimeVideoCodec names a compressed codec
        // and the offered video is uncompressed, request that codec's compressed
        // PixelFormat so the planner's CSC+VideoEncoder two-hop bridges the gap.
        rewriteVideoForCodec(want);
        // Video axis, step 2: coerce a still-uncompressed format the muxer can't
        // store to the closest supported one (the planner then splices a CSC).
        // A compressed target (codec request above, or already-compressed
        // passthrough) is left exactly as offered.
        if (!want.imageList().isEmpty()) {
                const PixelFormat &pd = want.imageList()[0].pixelFormat();
                if (!pd.isCompressed()) {
                        const PixelFormat target = pickSupportedPixelFormat(pd);
                        if (target != pd) {
                                want.imageList()[0].setPixelFormat(target);
                        }
                }
        }
        // Audio axis: optionally request a compressed codec (splices an encoder).
        rewriteAudioForCodec(want);
        *preferred = want;
        return Error::Ok;
}

void QuickTimeMediaIO::rewriteVideoForCodec(MediaDesc &desc) const {
        // Mirror of rewriteAudioForCodec on the video axis.  Only uncompressed
        // offered video is rewritten; an already-compressed offered stream is a
        // passthrough request and stays untouched, as does an Invalid codec
        // (the default — store whatever the source offers).
        if (desc.imageList().isEmpty()) return;
        const PixelFormat &offeredPd = desc.imageList()[0].pixelFormat();
        if (!offeredPd.isValid() || offeredPd.isCompressed()) return;

        const VideoCodec vc = config().getAs<VideoCodec>(MediaConfig::QuickTimeVideoCodec, VideoCodec());
        if (!vc.isValid()) return;
        const List<PixelFormat> cpfs = vc.compressedPixelFormats();
        if (cpfs.isEmpty()) return;
        const PixelFormat compressed = cpfs[0];

        // Rewrite every image descriptor's format to the codec's compressed
        // PixelFormat.  The planner sees an uncompressed→compressed gap and
        // splices a VideoEncoder (whose VideoCodec is derived from this format
        // by VideoCodec::fromPixelFormat), inserting a CSC ahead of it when the
        // offered raster doesn't match the encoder's accepted inputs.
        ImageDesc::List &imgs = desc.imageList();
        for (size_t i = 0; i < imgs.size(); ++i) imgs[i].setPixelFormat(compressed);
}

void QuickTimeMediaIO::rewriteAudioForCodec(MediaDesc &desc) const {
        // Writer audio-codec selection (mirrors MpegTsFileMediaIO::proposeInput).
        // When QuickTimeAudioCodec names a compressed codec and the offered
        // audio is uncompressed, rewrite each audio descriptor's format to the
        // codec's compressed AudioFormat.  The planner's orthogonal-axes pass
        // then sees an uncompressed→compressed gap and splices an AudioEncoder
        // (whose AudioCodec is derived from this very format by
        // audioEncoderBridge).  PCM / Invalid leaves the audio untouched so the
        // muxer writes an lpcm track (the historical default).
        if (desc.audioList().isEmpty()) return;
        const AudioFormat &af = desc.audioList()[0].format();
        if (!af.isValid() || af.isCompressed()) return;

        const AudioCodec ac = config().getAs<AudioCodec>(MediaConfig::QuickTimeAudioCodec,
                                                         AudioCodec(AudioCodec::PCM));
        if (!ac.isValid() || ac.id() == AudioCodec::PCM) return;

        AudioFormat compressedFmt;
        for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                AudioFormat candidate(fid);
                if (candidate.isCompressed() && candidate.audioCodec() == ac) {
                        compressedFmt = candidate;
                        break;
                }
        }
        if (!compressedFmt.isValid()) return;
        AudioDesc::List &auds = desc.audioList();
        for (size_t i = 0; i < auds.size(); ++i) auds[i].setFormat(compressedFmt);
}

VideoScanMode QuickTimeMediaIO::resolveWriteScanMode(const VideoScanMode &descScanMode) const {
        // Session config wins when it names a concrete mode; otherwise honour
        // the source ImageDesc's own (authoritative) scan mode; otherwise
        // progressive (Unknown).
        VideoScanMode cfg = config().getAs<VideoScanMode>(MediaConfig::VideoScanMode, VideoScanMode::Unknown);
        if (cfg != VideoScanMode::Unknown) return cfg;
        return descScanMode;
}

PROMEKI_NAMESPACE_END
