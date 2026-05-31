/**
 * @file      mpegtsfilemediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/mpegtsfilemediaio.h>
#include <promeki/mpegts.h>
#include <promeki/mpegtsdemuxer.h>
#include <promeki/mpegtsframer.h>
#include <promeki/mpegtsmuxer.h>

#include <promeki/audiocodec.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/smpte302m.h>
#include <promeki/bufferview.h>
#include <promeki/videocodec.h>
#include <promeki/enums_mediaio.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(MpegTsFileFactory)

namespace {

        constexpr int kReadChunkBytes = 64 * 1024;

} // namespace

MediaIOFactory::Config::SpecMap MpegTsFileFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            add = [&specs](MediaConfig::ID id) {
                const VariantSpec *gs = MediaConfig::spec(id);
                if (gs) specs.insert(id, *gs);
        };
        add(MediaConfig::Filename);
        add(MediaConfig::OpenMode);
        add(MediaConfig::FrameRate);
        add(MediaConfig::MpegTsVideoPid);
        add(MediaConfig::MpegTsAudioPid);
        add(MediaConfig::MpegTsPmtPid);
        add(MediaConfig::MpegTsProgramNumber);
        add(MediaConfig::MpegTsPatPmtIntervalMs);
        add(MediaConfig::MpegTsPcrIntervalMs);
        add(MediaConfig::MpegTsMuxRateBps);
        add(MediaConfig::MpegTsAacFraming);
        add(MediaConfig::MpegTsVideoCodec);
        add(MediaConfig::MpegTsAudioCodec);
        return specs;
}

MediaIO *MpegTsFileFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new MpegTsFileMediaIO(parent);
        io->setConfig(config);
        return io;
}

MpegTsFileMediaIO::MpegTsFileMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

MpegTsFileMediaIO::~MpegTsFileMediaIO() {
        if (isOpen()) (void)close().wait();
        if (_file.isOpen()) _file.close();
}

void MpegTsFileMediaIO::applyFramerConfig(const MediaIO::Config &cfg) {
        if (!_framer.isValid()) _framer.reset(new MpegTsFramer);
        _framer->setVideoPid(static_cast<uint16_t>(
                cfg.getAs<int32_t>(MediaConfig::MpegTsVideoPid, static_cast<int32_t>(MpegTs::DefaultVideoPid))));
        _framer->setAudioPid(static_cast<uint16_t>(
                cfg.getAs<int32_t>(MediaConfig::MpegTsAudioPid, static_cast<int32_t>(MpegTs::DefaultAudioPid))));
        _framer->setPmtPid(static_cast<uint16_t>(
                cfg.getAs<int32_t>(MediaConfig::MpegTsPmtPid, static_cast<int32_t>(MpegTs::DefaultPmtPid))));
        _framer->setProgramNumber(static_cast<uint16_t>(
                cfg.getAs<int32_t>(MediaConfig::MpegTsProgramNumber, static_cast<int32_t>(MpegTs::DefaultProgramNumber))));
        _framer->setPatPmtIntervalMs(cfg.getAs<int32_t>(MediaConfig::MpegTsPatPmtIntervalMs, 100));
        _framer->setPcrIntervalMs(cfg.getAs<int32_t>(MediaConfig::MpegTsPcrIntervalMs, 20));
        _framer->setMuxRateBps(cfg.getAs<int64_t>(MediaConfig::MpegTsMuxRateBps, int64_t(0)));
        const Enum aacEnum = cfg.get(MediaConfig::MpegTsAacFraming)
                                     .asEnum(MpegTsAacFraming::Type);
        const MpegTsFramer::AacFraming framing = (aacEnum == MpegTsAacFraming::Latm)
                                                         ? MpegTsFramer::AacFraming::Latm
                                                         : MpegTsFramer::AacFraming::Adts;
        _framer->setAacFraming(framing);
}

Error MpegTsFileMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        _filename = cmd.config.getAs<String>(MediaConfig::Filename);
        if (_filename.isEmpty()) {
                promekiErr("MpegTsFileMediaIO: Filename is required");
                return Error::InvalidArgument;
        }
        const Enum modeEnum = cmd.config.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        _isWrite = modeEnum == MediaIOOpenMode::Write;
        _isOpen = true;
        _eof = false;
        _readQueue.clear();
        _packetsWritten = _bytesWritten = _framesWritten = 0;
        _packetsRead = _bytesRead = _framesRead = 0;
        applyFramerConfig(cmd.config);
        return _isWrite ? openSink(cmd) : openSource(cmd);
}

Error MpegTsFileMediaIO::openSink(const MediaIOCommandOpen &cmd) {
        _file.setFilename(_filename);
        Error err = _file.open(IODevice::WriteOnly, File::Create | File::Truncate);
        if (err.isError()) {
                promekiErr("MpegTsFileMediaIO: open '%s' for write failed: %s",
                           _filename.cstr(), err.name().cstr());
                return err;
        }

        FrameRate fps = cmd.pendingMediaDesc.frameRate();
        if (!fps.isValid()) fps = cmd.config.getAs<FrameRate>(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        if (!fps.isValid()) fps = FrameRate(FrameRate::FPS_30);
        _framer->setWriterFrameRate(fps);

        // Pre-declare the stream shape via pendingMediaDesc so the
        // very first PAT / PMT is correct.  The framer also
        // late-binds from the first compressed payload as a fallback
        // if pendingMediaDesc has no compressed entries.
        Error e = _framer->configureStreams(cmd.pendingMediaDesc);
        if (e.isError()) return e;

        // If neither stream came from the pendingMediaDesc, default
        // to H.264 video — many producers set up the encoder before
        // describing the stream and the planner negotiates the shape
        // through the addSink call below.
        if (!_framer->haveVideoStream() && !_framer->haveAudioStream()) {
                Error pe = _framer->muxer()->addStream(_framer->videoPid(), MpegTs::StreamTypeH264);
                if (pe.isError() && pe != Error::Exists) return pe;
        }

        MediaIOPortGroup *group = addPortGroup("mpegtsfile");
        if (group == nullptr) {
                promekiWarn("MpegTsFileMediaIO: addPortGroup failed");
                return Error::Invalid;
        }
        group->setFrameRate(fps);
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) {
                promekiWarn("MpegTsFileMediaIO: addSink failed");
                return Error::Invalid;
        }
        return Error::Ok;
}

Error MpegTsFileMediaIO::openSource(const MediaIOCommandOpen &cmd) {
        _file.setFilename(_filename);
        Error err = _file.open(IODevice::ReadOnly);
        if (err.isError()) {
                promekiErr("MpegTsFileMediaIO: open '%s' for read failed: %s",
                           _filename.cstr(), err.name().cstr());
                return err;
        }

        FrameRate fps = cmd.config.getAs<FrameRate>(MediaConfig::FrameRate, FrameRate());
        if (!fps.isValid()) fps = FrameRate(FrameRate::FPS_30);

        MediaDesc desc;
        desc.setFrameRate(fps);

        MediaIOPortGroup *group = addPortGroup("mpegtsfile");
        if (group == nullptr) {
                promekiWarn("MpegTsFileMediaIO: addPortGroup failed");
                return Error::Invalid;
        }
        group->setFrameRate(fps);
        group->setCanSeek(false);
        group->setFrameCount(FrameCount::unknown());
        if (addSource(group, desc) == nullptr) {
                promekiWarn("MpegTsFileMediaIO: addSource failed");
                return Error::Invalid;
        }

        // Wire framer's per-frame callback to bump the stats counter
        // and enqueue for executeCmd(Read).
        _framer->setFrameCallback([this](Frame &&f) -> Error {
                _framesRead++;
                _readQueue.pushToBack(std::move(f));
                return Error::Ok;
        });
        return Error::Ok;
}

Error MpegTsFileMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_framer.isValid()) {
                (void)_framer->flushReader();
                _framer.reset();
        }
        if (_file.isOpen()) _file.close();
        _readQueue.clear();
        _filename.clear();
        _isOpen = false;
        _eof = false;
        return Error::Ok;
}

Error MpegTsFileMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) return Error::InvalidArgument;
        if (!_file.isOpen()) return Error::NotOpen;
        if (!_framer.isValid()) return Error::Invalid;

        auto emit = [this](const BufferView &v) -> Error {
                if (!_file.isOpen()) return Error::NotOpen;
                if (!v.isValid() || v.size() == 0) return Error::Ok;
                const int64_t n = _file.write(v.data(), static_cast<int64_t>(v.size()));
                if (n < 0 || static_cast<size_t>(n) != v.size()) {
                        promekiErr("MpegTsFileMediaIO: short write (%lld / %zu)",
                                   static_cast<long long>(n), v.size());
                        return Error::IOError;
                }
                _bytesWritten += static_cast<int64_t>(v.size());
                _packetsWritten += static_cast<int64_t>(v.size() / MpegTs::PacketSize);
                return Error::Ok;
        };

        Error err = _framer->writeFrame(cmd.frame, emit);
        if (err.isError()) return err;

        _framesWritten++;
        cmd.currentFrame = toFrameNumber(_framesWritten);
        cmd.frameCount = _framesWritten;
        return Error::Ok;
}

Error MpegTsFileMediaIO::pumpReader() {
        if (!_file.isOpen()) return Error::NotOpen;
        if (_eof) return Error::Ok;
        if (!_framer.isValid()) return Error::Invalid;
        Buffer chunk(kReadChunkBytes);
        if (!chunk.isValid()) return Error::NoMem;
        const int64_t n = _file.read(chunk.data(), kReadChunkBytes);
        if (n < 0) {
                promekiErr("MpegTsFileMediaIO: read failed");
                return Error::IOError;
        }
        if (n == 0) {
                _eof = true;
                (void)_framer->flushReader();
                return Error::Ok;
        }
        chunk.setSize(static_cast<size_t>(n));
        _bytesRead += n;
        _packetsRead += n / MpegTs::PacketSize;
        BufferView view(chunk, 0, static_cast<size_t>(n));
        return _framer->pushBytes(view);
}

Error MpegTsFileMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        // Drive the framer until at least one frame lands on the
        // queue or we hit EOF.
        while (_readQueue.isEmpty() && !_eof) {
                Error err = pumpReader();
                if (err.isError()) return err;
        }
        if (_readQueue.isEmpty()) return Error::EndOfFile;
        cmd.frame = std::move(_readQueue.front());
        _readQueue.remove(0);
        cmd.currentFrame = toFrameNumber(_framesRead);
        return Error::Ok;
}

Error MpegTsFileMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsPacketsWritten, _packetsWritten);
        cmd.stats.set(StatsBytesWritten, _bytesWritten);
        cmd.stats.set(StatsFramesWritten, _framesWritten);
        cmd.stats.set(StatsPacketsRead, _packetsRead);
        cmd.stats.set(StatsBytesRead, _bytesRead);
        cmd.stats.set(StatsFramesRead, _framesRead);
        if (_framer.isValid() && _framer->demuxer() != nullptr) {
                cmd.stats.set(StatsContinuityErrors, static_cast<int64_t>(_framer->demuxer()->continuityErrors()));
                cmd.stats.set(StatsBytesDiscarded, static_cast<int64_t>(_framer->demuxer()->bytesDiscarded()));
        }
        return Error::Ok;
}

Error MpegTsFileMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        Error baseErr = MediaIO::describe(out);
        if (baseErr.isError()) return baseErr;
        // Advertise the cross product of supported video × audio so
        // the planner's orthogonal-axes pass sees both gaps on the
        // sink side.  A video-only entry would make the audio leg
        // look "missing" and the orthogonal chain would be skipped.
        auto advertise = [&out](PixelFormat::ID pid, AudioFormat::ID aid) {
                MediaDesc d;
                d.imageList().pushToBack(ImageDesc(Size2Du32(0, 0), PixelFormat(pid)));
                d.audioList().pushToBack(AudioDesc(AudioFormat(aid), 48000.0f, 2u));
                out->acceptableFormats().pushToBack(d);
        };
        // PCMI_S16LE stands in for "uncompressed audio" — the framer
        // packs it as SMPTE 302M on the way out.  JPEG_XS_YUV10_422_Rec709
        // stands in for the JPEG XS variants — every JPEG_XS_* maps to
        // the same MPEG-TS stream_type / registration descriptor.
        const PixelFormat::ID videos[] = {
                PixelFormat::H264,
                PixelFormat::HEVC,
                PixelFormat::AV1,
                PixelFormat::JPEG_XS_YUV10_422_Rec709,
        };
        const AudioFormat::ID audios[] = {
                AudioFormat::AAC,
                AudioFormat::Opus,
                AudioFormat::PCMI_S16LE,
        };
        for (PixelFormat::ID v : videos) {
                for (AudioFormat::ID a : audios) advertise(v, a);
        }
        return Error::Ok;
}

Error MpegTsFileMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;

        // Reader-mode: planner queries proposeOutput on sources.
        const Enum modeEnum = config().get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum == MediaIOOpenMode::Write;
        if (!isWrite) {
                *preferred = offered;
                return Error::Ok;
        }

        MediaDesc out = offered;

        // Video: same rewriting pattern as RtmpMediaIO::proposeInput /
        // SrtMediaIO::proposeInput.  Coerce an uncompressed offered
        // PixelFormat to the configured codec's compressed
        // representative so the planner's VideoEncoder bridge sees
        // the gap and splices an encoder in.
        if (!out.imageList().isEmpty()) {
                const PixelFormat &pd = out.imageList()[0].pixelFormat();
                if (pd.isValid() && !pd.isCompressed()) {
                        VideoCodec vc =
                                config().getAs<VideoCodec>(MediaConfig::MpegTsVideoCodec,
                                                           VideoCodec(VideoCodec::H264));
                        if (vc.isValid()) {
                                List<PixelFormat> compressed = vc.compressedPixelFormats();
                                if (!compressed.isEmpty()) {
                                        ImageDesc::List &imgs = out.imageList();
                                        for (size_t i = 0; i < imgs.size(); ++i) {
                                                imgs[i].setPixelFormat(compressed[0]);
                                        }
                                }
                        }
                }
        }

        // Audio: same idea.  Two paths:
        //
        // - Compressed codec (AAC / Opus / MP3 / AC-3): find the
        //   AudioFormat whose audioCodec() reverses to the configured
        //   codec and rewrite the offered descriptor's format to it.
        //   The planner's orthogonal-axes pass then sees an
        //   uncompressed→compressed gap and splices an AudioEncoder.
        //
        // - PCM (MpegTsAudioCodec=PCM): no encoder needed — the muxer
        //   packs PCM samples as SMPTE 302M directly.  But 302M only
        //   accepts PCMI_S16LE / PCMI_S24LE @ 48 kHz with 2/4/6/8
        //   channels.  Rewrite the offered descriptor to PCMI_S16LE
        //   when the source is offering an incompatible PCM layout
        //   (Float32, planar, etc.) so the planner inserts an SRC
        //   converter on the way in.
        if (!out.audioList().isEmpty()) {
                const AudioFormat &af = out.audioList()[0].format();
                if (af.isValid() && !af.isCompressed()) {
                        AudioCodec ac =
                                config().getAs<AudioCodec>(MediaConfig::MpegTsAudioCodec,
                                                           AudioCodec(AudioCodec::AAC));
                        if (ac.isValid() && ac.id() == AudioCodec::PCM) {
                                if (!Smpte302M::isFormatSupported(af)) {
                                        AudioDesc::List &auds = out.audioList();
                                        const AudioFormat pcm16(AudioFormat::PCMI_S16LE);
                                        for (size_t i = 0; i < auds.size(); ++i) {
                                                auds[i].setFormat(pcm16);
                                                auds[i].setSampleRate(Smpte302M::RequiredSampleRate);
                                        }
                                }
                        } else if (ac.isValid()) {
                                AudioFormat compressedFmt;
                                for (AudioFormat::ID fid : AudioFormat::registeredIDs()) {
                                        AudioFormat candidate(fid);
                                        if (!candidate.isCompressed()) continue;
                                        if (candidate.audioCodec() == ac) {
                                                compressedFmt = candidate;
                                                break;
                                        }
                                }
                                if (compressedFmt.isValid()) {
                                        AudioDesc::List &auds = out.audioList();
                                        for (size_t i = 0; i < auds.size(); ++i) {
                                                auds[i].setFormat(compressedFmt);
                                        }
                                }
                        }
                }
        }

        *preferred = out;
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
