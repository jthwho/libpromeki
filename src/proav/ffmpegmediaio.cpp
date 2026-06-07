/**
 * @file      ffmpegmediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_FFMPEG

#include <cstring>

#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/compressedaudiopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/enums_mediaio.h>
#include <promeki/ffmpegmediaio.h>
#include <promeki/ffmpegsupport.h>
#include <promeki/frame.h>
#include <promeki/h264bitstream.h>
#include <promeki/hevcbitstream.h>
#include <promeki/imagedesc.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/metadata.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(FfmpegMediaIO)

PROMEKI_REGISTER_MEDIAIO_FACTORY(FfmpegFactory)

namespace {

        // ===================================================================
        // Demux codec mapping: AVCodecID -> promeki compressed PixelFormat /
        // AudioFormat.  Only codecs the library has a compressed format ID for
        // can be passed through; an unmapped codec fails the open with a clear
        // message (the gap is then a one-line addition to the format enum).
        // ===================================================================

        // FourCC little-endian packing matching libavutil's MKTAG.
        constexpr uint32_t fourcc(char a, char b, char c, char d) {
                return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
                       (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
        }

        // ProRes carries its variant in a 4-char profile FourCC (the codec_tag,
        // stored as Matroska CodecPrivate / mp4 sample-entry type).  These map
        // the promeki ProRes PixelFormat variants to/from that tag.
        uint32_t proResTagForPixelFormat(PixelFormat::ID id) {
                switch (id) {
                        case PixelFormat::ProRes_422_Proxy: return fourcc('a', 'p', 'c', 'o');
                        case PixelFormat::ProRes_422_LT: return fourcc('a', 'p', 'c', 's');
                        case PixelFormat::ProRes_422: return fourcc('a', 'p', 'c', 'n');
                        case PixelFormat::ProRes_422_HQ: return fourcc('a', 'p', 'c', 'h');
                        case PixelFormat::ProRes_4444: return fourcc('a', 'p', '4', 'h');
                        case PixelFormat::ProRes_4444_XQ: return fourcc('a', 'p', '4', 'x');
                        default: return 0;
                }
        }

        PixelFormat::ID proResPixelFormatForTag(uint32_t tag) {
                if (tag == fourcc('a', 'p', 'c', 'o')) return PixelFormat::ProRes_422_Proxy;
                if (tag == fourcc('a', 'p', 'c', 's')) return PixelFormat::ProRes_422_LT;
                if (tag == fourcc('a', 'p', 'c', 'n')) return PixelFormat::ProRes_422;
                if (tag == fourcc('a', 'p', 'c', 'h')) return PixelFormat::ProRes_422_HQ;
                if (tag == fourcc('a', 'p', '4', 'h')) return PixelFormat::ProRes_4444;
                if (tag == fourcc('a', 'p', '4', 'x')) return PixelFormat::ProRes_4444_XQ;
                return PixelFormat::ProRes_422; // sane default when the tag is absent
        }

        // @p codecTag lets ProRes resolve its exact variant from the stream's
        // profile FourCC; pass 0 when unknown.
        PixelFormat::ID compressedPixelFormatForAvCodec(AVCodecID id, uint32_t codecTag = 0) {
                switch (id) {
                        case AV_CODEC_ID_H264: return PixelFormat::H264;
                        case AV_CODEC_ID_HEVC: return PixelFormat::HEVC;
                        case AV_CODEC_ID_AV1: return PixelFormat::AV1;
                        case AV_CODEC_ID_PRORES: return proResPixelFormatForTag(codecTag);
                        case AV_CODEC_ID_MJPEG: return PixelFormat::JPEG_YUV8_422_Rec709;
                        default: return PixelFormat::Invalid;
                }
        }

        AudioFormat::ID compressedAudioFormatForAvCodec(AVCodecID id) {
                switch (id) {
                        case AV_CODEC_ID_AAC: return AudioFormat::AAC;
                        case AV_CODEC_ID_AC3: return AudioFormat::AC3;
                        case AV_CODEC_ID_MP3: return AudioFormat::MP3;
                        case AV_CODEC_ID_FLAC: return AudioFormat::FLAC;
                        case AV_CODEC_ID_OPUS: return AudioFormat::Opus;
                        default: return AudioFormat::Invalid;
                }
        }

        // Interleaved PCM AVCodecID -> promeki interleaved-PCM AudioFormat.
        // libavformat hands demuxed PCM out interleaved.
        AudioFormat::ID pcmAudioFormatForAvCodec(AVCodecID id) {
                switch (id) {
                        case AV_CODEC_ID_PCM_S16LE: return AudioFormat::PCMI_S16LE;
                        case AV_CODEC_ID_PCM_S16BE: return AudioFormat::PCMI_S16BE;
                        case AV_CODEC_ID_PCM_U8: return AudioFormat::PCMI_U8;
                        case AV_CODEC_ID_PCM_S24LE: return AudioFormat::PCMI_S24LE;
                        case AV_CODEC_ID_PCM_S24BE: return AudioFormat::PCMI_S24BE;
                        case AV_CODEC_ID_PCM_S32LE: return AudioFormat::PCMI_S32LE;
                        case AV_CODEC_ID_PCM_S32BE: return AudioFormat::PCMI_S32BE;
                        case AV_CODEC_ID_PCM_F32LE: return AudioFormat::PCMI_Float32LE;
                        case AV_CODEC_ID_PCM_F32BE: return AudioFormat::PCMI_Float32BE;
                        default: return AudioFormat::Invalid;
                }
        }

        // ===================================================================
        // Mux codec mapping: promeki compressed format -> AVCodecID.
        // ===================================================================

        AVCodecID avCodecForPixelFormat(PixelFormat::ID id) {
                switch (id) {
                        case PixelFormat::H264: return AV_CODEC_ID_H264;
                        case PixelFormat::HEVC: return AV_CODEC_ID_HEVC;
                        case PixelFormat::AV1: return AV_CODEC_ID_AV1;
                        case PixelFormat::ProRes_422_Proxy:
                        case PixelFormat::ProRes_422_LT:
                        case PixelFormat::ProRes_422:
                        case PixelFormat::ProRes_422_HQ:
                        case PixelFormat::ProRes_4444:
                        case PixelFormat::ProRes_4444_XQ: return AV_CODEC_ID_PRORES;
                        case PixelFormat::JPEG_RGB8_sRGB:
                        case PixelFormat::JPEG_RGBA8_sRGB:
                        case PixelFormat::JPEG_YUV8_422_Rec709:
                        case PixelFormat::JPEG_YUV8_420_Rec709:
                        case PixelFormat::JPEG_YUV8_422_Rec601:
                        case PixelFormat::JPEG_YUV8_420_Rec601: return AV_CODEC_ID_MJPEG;
                        default: return AV_CODEC_ID_NONE;
                }
        }

        AVCodecID avCodecForAudioFormat(const AudioFormat &fmt) {
                if (!fmt.isValid()) return AV_CODEC_ID_NONE;
                if (fmt.isCompressed()) {
                        switch (fmt.id()) {
                                case AudioFormat::AAC: return AV_CODEC_ID_AAC;
                                case AudioFormat::AC3: return AV_CODEC_ID_AC3;
                                case AudioFormat::MP3: return AV_CODEC_ID_MP3;
                                case AudioFormat::FLAC: return AV_CODEC_ID_FLAC;
                                case AudioFormat::Opus: return AV_CODEC_ID_OPUS;
                                default: return AV_CODEC_ID_NONE;
                        }
                }
                // Interleaved PCM — the only PCM forms the writer emits.
                switch (fmt.id()) {
                        case AudioFormat::PCMI_S16LE: return AV_CODEC_ID_PCM_S16LE;
                        case AudioFormat::PCMI_S16BE: return AV_CODEC_ID_PCM_S16BE;
                        case AudioFormat::PCMI_U8: return AV_CODEC_ID_PCM_U8;
                        case AudioFormat::PCMI_S24LE: return AV_CODEC_ID_PCM_S24LE;
                        case AudioFormat::PCMI_S24BE: return AV_CODEC_ID_PCM_S24BE;
                        case AudioFormat::PCMI_S32LE: return AV_CODEC_ID_PCM_S32LE;
                        case AudioFormat::PCMI_S32BE: return AV_CODEC_ID_PCM_S32BE;
                        case AudioFormat::PCMI_Float32LE: return AV_CODEC_ID_PCM_F32LE;
                        case AudioFormat::PCMI_Float32BE: return AV_CODEC_ID_PCM_F32BE;
                        default: return AV_CODEC_ID_NONE;
                }
        }

        // True for compressed audio codecs whose container mapping requires an
        // out-of-band configuration record (AAC AudioSpecificConfig, FLAC
        // STREAMINFO, Opus OpusHead).  The writer builds streams from the
        // AudioDesc alone and carries no such extradata, so muxing these would
        // yield an undecodable track — they are rejected up front instead.
        bool audioFormatNeedsExtradata(AVCodecID id) {
                switch (id) {
                        case AV_CODEC_ID_AAC:
                        case AV_CODEC_ID_FLAC:
                        case AV_CODEC_ID_OPUS: return true;
                        default: return false;
                }
        }

        // The mp4toannexb-family bitstream filter name for a length-prefixed
        // H.264 / HEVC source, or null when no filter is needed.
        const char *annexbFilterName(AVCodecID id) {
                switch (id) {
                        case AV_CODEC_ID_H264: return "h264_mp4toannexb";
                        case AV_CODEC_ID_HEVC: return "hevc_mp4toannexb";
                        default: return nullptr;
                }
        }

        // Predicate: keep every NAL except H.264 parameter sets (SPS 7, PPS 8).
        // ISO/IEC 14496-15 carries parameter sets in the avcC record, not the
        // sample payload — same rule the QuickTime writer applies.
        bool keepNonH264ParameterSetNal(const H264Bitstream::NalUnit &nal) {
                const uint8_t t = nal.header0 & 0x1f;
                return t != 7 && t != 8;
        }

        // Predicate: keep every NAL except HEVC parameter sets (VPS 32, SPS 33,
        // PPS 34).
        bool keepNonHevcParameterSetNal(const H264Bitstream::NalUnit &nal) {
                const uint8_t t = static_cast<uint8_t>((nal.header0 >> 1) & 0x3f);
                return t < 32 || t > 34;
        }

        // Builds the avcC / hvcC configuration record (the bytes inside the
        // box, no header) from an Annex-B byte stream carrying the parameter
        // sets — the form an avc1 / hvc1 sample entry or a Matroska
        // CodecPrivate requires.  @p isHevc selects the HEVC path.
        Error buildDecoderConfig(const BufferView &annexB, bool isHevc, Buffer &out) {
                if (isHevc) {
                        HevcDecoderConfig cfg;
                        Error             e = HevcDecoderConfig::fromAnnexB(annexB, cfg);
                        if (e.isError()) return e;
                        return cfg.serialize(out);
                }
                AvcDecoderConfig cfg;
                Error            e = AvcDecoderConfig::fromAnnexB(annexB, cfg);
                if (e.isError()) return e;
                return cfg.serialize(out);
        }

} // namespace

// ============================================================================
// Factory
// ============================================================================

bool FfmpegFactory::canHandleDevice(IODevice *device) const {
        // Probe the leading bytes through libavformat's format prober.  As a
        // fallback backend this only ever runs after every native backend has
        // declined, so a positive answer here means "FFmpeg recognizes a
        // container nothing native handles".
        if (device == nullptr) return false;
        // av_probe_input_format requires AVPROBE_PADDING_SIZE zeroed bytes
        // past the probed data.
        uint8_t buf[4096 + AVPROBE_PADDING_SIZE];
        std::memset(buf, 0, sizeof(buf));
        int64_t n = device->read(buf, 4096);
        if (n <= 0) return false;

        AVProbeData pd;
        std::memset(&pd, 0, sizeof(pd));
        pd.buf = buf;
        pd.buf_size = static_cast<int>(n);
        pd.filename = "";
        int             score = 0;
        const AVInputFormat *fmt = av_probe_input_format3(&pd, 1, &score);
        return fmt != nullptr && score >= AVPROBE_SCORE_MAX / 4;
}

FfmpegFactory::Config::SpecMap FfmpegFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        s(MediaConfig::VideoTrack, int32_t(-1));
        s(MediaConfig::AudioTrack, int32_t(-1));
        s(MediaConfig::FfmpegFormat, String());
        s(MediaConfig::FfmpegVideoCodec, VideoCodec());
        s(MediaConfig::FfmpegAudioCodec, AudioCodec(AudioCodec::PCM));
        return specs;
}

MediaIO *FfmpegFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new FfmpegMediaIO(parent);
        io->setConfig(config);
        return io;
}

// ============================================================================
// Lifecycle
// ============================================================================

FfmpegMediaIO::FfmpegMediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {
        ffmpegInstallLogBridge();
}

FfmpegMediaIO::~FfmpegMediaIO() {
        if (isOpen()) (void)close().wait();
        closeContexts();
}

void FfmpegMediaIO::closeContexts() {
        if (_bsf != nullptr) {
                av_bsf_free(&_bsf);
                _bsf = nullptr;
        }
        if (_inFmt != nullptr) {
                avformat_close_input(&_inFmt);
                _inFmt = nullptr;
        }
        if (_outFmt != nullptr) {
                if (_outFmt->pb != nullptr && !(_outFmt->oformat->flags & AVFMT_NOFILE)) {
                        avio_closep(&_outFmt->pb);
                }
                avformat_free_context(_outFmt);
                _outFmt = nullptr;
        }
        _outVideo = nullptr;
        _outAudio = nullptr;
}

// ============================================================================
// Open
// ============================================================================

Error FfmpegMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;
        _filename = cfg.getAs<String>(MediaConfig::Filename);
        if (_filename.isEmpty()) {
                promekiErr("FfmpegMediaIO: filename is required");
                return Error::InvalidArgument;
        }

        Enum       modeEnum = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum == MediaIOOpenMode::Write;
        _isWrite = isWrite;

        MediaDesc  outMediaDesc;
        FrameCount outFrameCount(0);
        bool       outCanSeek = false;

        if (!isWrite) {
                Error err = openReader(cmd, cmd);
                if (err.isError()) {
                        closeContexts();
                        return err;
                }
                outMediaDesc = cmd.mediaDesc;
                outFrameCount = _frameCount;
                outCanSeek = true;
        } else {
                Error err = openWriter(cmd, cmd);
                if (err.isError()) {
                        closeContexts();
                        return err;
                }
                outMediaDesc = cmd.mediaDesc;
                outFrameCount = FrameCount(0);
                outCanSeek = false;
        }

        _isOpen = true;

        MediaIOPortGroup *group = addPortGroup("ffmpeg");
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

Error FfmpegMediaIO::openReader(const MediaIOCommandOpen &cmd, MediaIOCommandOpen &out) {
        int rc = avformat_open_input(&_inFmt, _filename.cstr(), nullptr, nullptr);
        if (rc < 0) {
                promekiErr("FfmpegMediaIO: open '%s' failed: %s", _filename.cstr(),
                           ffmpegErrorString(rc).cstr());
                return Error::IOError;
        }
        rc = avformat_find_stream_info(_inFmt, nullptr);
        if (rc < 0) {
                promekiErr("FfmpegMediaIO: find_stream_info '%s' failed: %s", _filename.cstr(),
                           ffmpegErrorString(rc).cstr());
                return Error::IOError;
        }

        int wantVideo = cmd.config.getAs<int>(MediaConfig::VideoTrack, -1);
        int wantAudio = cmd.config.getAs<int>(MediaConfig::AudioTrack, -1);

        _videoStream = (wantVideo >= 0)
                               ? wantVideo
                               : av_find_best_stream(_inFmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        _audioStream = (wantAudio >= 0)
                               ? wantAudio
                               : av_find_best_stream(_inFmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (_videoStream < 0 && _audioStream < 0) {
                promekiErr("FfmpegMediaIO: '%s' has no video or audio streams", _filename.cstr());
                return Error::NotSupported;
        }

        MediaDesc mediaDesc;

        if (_videoStream >= 0) {
                AVStream             *st = _inFmt->streams[_videoStream];
                const AVCodecParameters *par = st->codecpar;
                PixelFormat::ID       cpf = compressedPixelFormatForAvCodec(par->codec_id, par->codec_tag);
                if (cpf == PixelFormat::Invalid) {
                        promekiErr("FfmpegMediaIO: video codec '%s' has no promeki compressed "
                                   "PixelFormat (passthrough unsupported)",
                                   avcodec_get_name(par->codec_id));
                        return Error::NotSupported;
                }
                ImageDesc idesc(Size2Du32(static_cast<uint32_t>(par->width), static_cast<uint32_t>(par->height)),
                                PixelFormat(cpf));
                mediaDesc.imageList().pushToBack(idesc);

                AVRational fr = st->avg_frame_rate;
                if (fr.num <= 0 || fr.den <= 0) fr = st->r_frame_rate;
                if (fr.num > 0 && fr.den > 0) {
                        _frameRate = FrameRate(FrameRate::RationalType(fr.num, fr.den));
                        mediaDesc.setFrameRate(_frameRate);
                }
                // Frame count: prefer the container's explicit count, else
                // derive from duration × rate.
                if (st->nb_frames > 0) {
                        _frameCount = FrameCount(static_cast<int64_t>(st->nb_frames));
                } else if (_frameRate.isValid() && _inFmt->duration > 0) {
                        const double secs = static_cast<double>(_inFmt->duration) / AV_TIME_BASE;
                        const double fps = static_cast<double>(fr.num) / static_cast<double>(fr.den);
                        _frameCount = FrameCount(static_cast<int64_t>(secs * fps + 0.5));
                } else {
                        _frameCount = FrameCount::unknown();
                }

                // Set up the Annex-B bitstream filter for length-prefixed
                // H.264 / HEVC so emitted payloads carry in-band parameter sets.
                if (const char *bsfName = annexbFilterName(par->codec_id)) {
                        const AVBitStreamFilter *f = av_bsf_get_by_name(bsfName);
                        if (f != nullptr && av_bsf_alloc(f, &_bsf) == 0) {
                                avcodec_parameters_copy(_bsf->par_in, par);
                                _bsf->time_base_in = st->time_base;
                                if (av_bsf_init(_bsf) < 0) {
                                        av_bsf_free(&_bsf);
                                        _bsf = nullptr;
                                }
                        }
                }
        }

        if (_audioStream >= 0) {
                AVStream                *st = _inFmt->streams[_audioStream];
                const AVCodecParameters *par = st->codecpar;
                const int                channels = par->ch_layout.nb_channels;
                const int                rate = par->sample_rate;

                AudioFormat::ID afid = compressedAudioFormatForAvCodec(par->codec_id);
                if (afid == AudioFormat::Invalid) afid = pcmAudioFormatForAvCodec(par->codec_id);
                if (afid == AudioFormat::Invalid) {
                        promekiErr("FfmpegMediaIO: audio codec '%s' has no promeki AudioFormat "
                                   "(passthrough unsupported)",
                                   avcodec_get_name(par->codec_id));
                        // Audio is non-fatal — drop the audio stream and continue
                        // video-only rather than failing the whole open.
                        _audioStream = -1;
                } else {
                        _audioDesc = AudioDesc(AudioFormat(afid), static_cast<float>(rate),
                                               static_cast<unsigned int>(channels));
                        mediaDesc.audioList().pushToBack(_audioDesc);
                }
        }

        if (!mediaDesc.frameRate().isValid() && _audioStream >= 0) {
                _frameRate = FrameRate(FrameRate::RationalType(1, 1));
                mediaDesc.setFrameRate(_frameRate);
        }

        _currentFrame = 0;
        _eof = false;

        out.mediaDesc = mediaDesc;
        out.audioDesc = _audioDesc;
        out.frameRate = _frameRate;
        out.frameCount = _frameCount;
        out.canSeek = true;
        return Error::Ok;
}

Error FfmpegMediaIO::openWriter(const MediaIOCommandOpen &cmd, MediaIOCommandOpen &out) {
        const String fmtName = cmd.config.getAs<String>(MediaConfig::FfmpegFormat, String());
        int          rc = avformat_alloc_output_context2(
                &_outFmt, nullptr, fmtName.isEmpty() ? nullptr : fmtName.cstr(), _filename.cstr());
        if (rc < 0 || _outFmt == nullptr) {
                promekiErr("FfmpegMediaIO: alloc output context for '%s' failed: %s", _filename.cstr(),
                           ffmpegErrorString(rc).cstr());
                return Error::IOError;
        }

        if (cmd.pendingMediaDesc.frameRate().isValid()) {
                _frameRate = cmd.pendingMediaDesc.frameRate();
        }

        // The streams are created lazily from the first written frame
        // (setupWriterFromFrame), mirroring QuickTimeMediaIO — the offered
        // MediaDesc carries the planner-negotiated formats but the concrete
        // bytes (and any encoder extradata) arrive with the frame.
        _writerTracksRegistered = false;
        _headerWritten = false;
        _writerVideoPts = 0;
        _writerAudioPts = 0;

        out.mediaDesc = cmd.pendingMediaDesc;
        out.frameCount = FrameCount(0);
        out.canSeek = false;
        return Error::Ok;
}

// ============================================================================
// Close
// ============================================================================

Error FfmpegMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_isOpen && _isWrite && _outFmt != nullptr && _headerWritten) {
                int rc = av_write_trailer(_outFmt);
                if (rc < 0) {
                        promekiWarn("FfmpegMediaIO: write_trailer failed: %s", ffmpegErrorString(rc).cstr());
                }
        }
        closeContexts();
        _isOpen = false;
        _isWrite = false;
        _filename.clear();
        _videoStream = -1;
        _audioStream = -1;
        _frameRate = FrameRate();
        _frameCount = FrameCount::unknown();
        _currentFrame = 0;
        _audioDesc = AudioDesc();
        _eof = false;
        _headerWritten = false;
        _writerTracksRegistered = false;
        _writerVideoPts = 0;
        _writerAudioPts = 0;
        return Error::Ok;
}

// ============================================================================
// Read
// ============================================================================

Error FfmpegMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (!_isOpen || _isWrite) return Error::NotOpen;
        if (_eof) {
                cmd.result = Error::EndOfFile;
                return Error::EndOfFile;
        }
        if (_frameCount.isFinite() && _currentFrame.value() >= _frameCount.value()) {
                cmd.result = Error::EndOfFile;
                return Error::EndOfFile;
        }

        Frame      frame;
        AVPacket  *pkt = av_packet_alloc();
        if (pkt == nullptr) return Error::NoMem;

        bool gotVideoFrame = false;
        bool produced = false;

        // Drive one Frame per Read: accumulate audio packets until the next
        // video packet (file-interleaved order), which completes the frame.
        // Audio-only files emit one audio packet per Read.
        while (true) {
                int rc = av_read_frame(_inFmt, pkt);
                if (rc < 0) {
                        if (rc == AVERROR_EOF) _eof = true;
                        else
                                promekiWarn("FfmpegMediaIO: read_frame failed: %s",
                                            ffmpegErrorString(rc).cstr());
                        break;
                }

                if (pkt->stream_index == _videoStream) {
                        const AVStream *st = _inFmt->streams[_videoStream];
                        ImageDesc       idesc(
                                Size2Du32(static_cast<uint32_t>(st->codecpar->width),
                                          static_cast<uint32_t>(st->codecpar->height)),
                                PixelFormat(compressedPixelFormatForAvCodec(st->codecpar->codec_id,
                                                                            st->codecpar->codec_tag)));
                        const bool keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

                        if (_bsf != nullptr) {
                                // Convert length-prefixed -> Annex-B.  One input
                                // packet may yield one output packet.
                                if (av_bsf_send_packet(_bsf, pkt) == 0) {
                                        AVPacket *outPkt = av_packet_alloc();
                                        while (outPkt != nullptr && av_bsf_receive_packet(_bsf, outPkt) == 0) {
                                                BufferView bv = ffmpegWrapPacket(outPkt);
                                                if (bv.isValid()) {
                                                        auto cvp = CompressedVideoPayload::Ptr::create(idesc, bv);
                                                        if (keyframe)
                                                                cvp.modify()->addFlag(MediaPayload::Keyframe);
                                                        frame.addPayload(cvp);
                                                        gotVideoFrame = true;
                                                }
                                                av_packet_unref(outPkt);
                                        }
                                        if (outPkt != nullptr) av_packet_free(&outPkt);
                                }
                        } else {
                                BufferView bv = ffmpegWrapPacket(pkt);
                                if (bv.isValid()) {
                                        auto cvp = CompressedVideoPayload::Ptr::create(idesc, bv);
                                        if (keyframe) cvp.modify()->addFlag(MediaPayload::Keyframe);
                                        frame.addPayload(cvp);
                                        gotVideoFrame = true;
                                }
                        }
                        av_packet_unref(pkt);
                        if (gotVideoFrame) {
                                produced = true;
                                break; // video packet completes this frame
                        }
                        // The video packet yielded no payload (e.g. a
                        // bitstream-filter hiccup) — skip it and keep reading
                        // rather than completing the frame with no video.
                        continue;
                } else if (pkt->stream_index == _audioStream) {
                        const AVStream *st = _inFmt->streams[_audioStream];
                        BufferView      bv = ffmpegWrapPacket(pkt);
                        if (bv.isValid()) {
                                if (_audioDesc.isCompressed()) {
                                        // Decoded-sample count from packet duration.
                                        size_t samples = 0;
                                        if (pkt->duration > 0) {
                                                const double dur =
                                                        static_cast<double>(pkt->duration) *
                                                        static_cast<double>(st->time_base.num) /
                                                        static_cast<double>(st->time_base.den);
                                                samples = static_cast<size_t>(
                                                        dur * _audioDesc.sampleRate() + 0.5);
                                        }
                                        auto ap = CompressedAudioPayload::Ptr::create(_audioDesc, bv, samples);
                                        frame.addPayload(ap);
                                } else {
                                        const size_t frameBytes =
                                                _audioDesc.bytesPerSample() * _audioDesc.channels();
                                        const size_t sampleCount =
                                                (frameBytes > 0) ? (bv.size() / frameBytes) : 0;
                                        auto ap = PcmAudioPayload::Ptr::create(_audioDesc, sampleCount, bv);
                                        frame.addPayload(ap);
                                }
                                produced = true;
                        }
                        av_packet_unref(pkt);
                        if (_videoStream < 0) break; // audio-only: one packet per Read
                } else {
                        av_packet_unref(pkt); // stream we don't care about
                }
        }

        av_packet_free(&pkt);

        if (!produced) {
                cmd.result = Error::EndOfFile;
                return Error::EndOfFile;
        }

        Metadata &fmeta = frame.metadata();
        fmeta.set(Metadata::FrameNumber, _currentFrame);
        if (gotVideoFrame) fmeta.set(Metadata::FrameKeyframe, true);

        cmd.frame = frame;
        cmd.currentFrame = _currentFrame;

        const int step = (cmd.group != nullptr) ? cmd.group->nextStep() : 1;
        _currentFrame += step;
        if (!_currentFrame.isValid()) _currentFrame = FrameNumber(0);
        return Error::Ok;
}

// ============================================================================
// Write
// ============================================================================

Error FfmpegMediaIO::setupWriterFromFrame(const Frame &frame) {
        if (_writerTracksRegistered) return Error::Ok;

        // ---- Video stream ----
        auto vids = frame.videoPayloads();
        if (!vids.isEmpty() && vids[0].isValid()) {
                const VideoPayload &vp = *vids[0];
                const auto         *cvp = vp.as<CompressedVideoPayload>();
                if (cvp == nullptr) {
                        promekiErr("FfmpegMediaIO: writer requires a compressed video payload "
                                   "(select a codec via FfmpegVideoCodec)");
                        return Error::NotSupported;
                }
                const PixelFormat::ID pfid = cvp->desc().pixelFormat().id();
                const AVCodecID       cid = avCodecForPixelFormat(pfid);
                if (cid == AV_CODEC_ID_NONE) {
                        promekiErr("FfmpegMediaIO: no AVCodec for compressed PixelFormat id %d",
                                   static_cast<int>(pfid));
                        return Error::NotSupported;
                }
                AVStream *st = avformat_new_stream(_outFmt, nullptr);
                if (st == nullptr) return Error::NoMem;
                st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                st->codecpar->codec_id = cid;
                st->codecpar->width = static_cast<int>(cvp->desc().size().width());
                st->codecpar->height = static_cast<int>(cvp->desc().size().height());
                // ProRes carries its variant in a profile FourCC the muxer
                // stores (Matroska CodecPrivate / mp4 sample-entry type) and the
                // decoder reads back — without it the ProRes decoder reports
                // "Unknown prores profile".
                if (cid == AV_CODEC_ID_PRORES) {
                        st->codecpar->codec_tag = proResTagForPixelFormat(pfid);
                }
                if (_frameRate.isValid()) {
                        st->time_base.num = static_cast<int>(_frameRate.denominator());
                        st->time_base.den = static_cast<int>(_frameRate.numerator());
                        st->avg_frame_rate.num = static_cast<int>(_frameRate.numerator());
                        st->avg_frame_rate.den = static_cast<int>(_frameRate.denominator());
                } else {
                        st->time_base.num = 1;
                        st->time_base.den = 25;
                }
                // Stream extradata.  H.264 / HEVC require an avcC / hvcC
                // configuration record (raw Annex-B parameter sets would
                // produce a non-conformant mp4 / Matroska file).  Build it
                // from the parameter sets — preferring the explicit
                // out-of-band Metadata::CodecParameterSets, falling back to
                // the in-band SPS/PPS carried by the first (key)frame's
                // access unit.  Other codecs (ProRes, MJPEG, AV1, …) take any
                // CodecParameterSets bytes verbatim.
                const bool isH264 = (cid == AV_CODEC_ID_H264);
                const bool isHevc = (cid == AV_CODEC_ID_HEVC);

                Buffer     psBuf;
                BufferView psView;
                if (cvp->metadata().contains(Metadata::CodecParameterSets)) {
                        const String ps = cvp->metadata().getAs<String>(Metadata::CodecParameterSets);
                        if (!ps.isEmpty()) {
                                psBuf = Buffer(ps.size());
                                std::memcpy(psBuf.data(), ps.cstr(), ps.size());
                                psBuf.setSize(ps.size());
                                psView = BufferView(psBuf, 0, psBuf.size());
                        }
                }
                if ((isH264 || isHevc) && !psView.isValid() && cvp->planeCount() > 0) {
                        // Keyframe access unit carries SPS/PPS in-band.
                        auto e = cvp->plane(0);
                        if (e.isValid()) psView = BufferView(e.buffer(), e.offset(), e.size());
                }

                if (isH264 || isHevc) {
                        if (!psView.isValid()) {
                                promekiErr("FfmpegMediaIO: cannot mux %s without parameter sets "
                                           "(no CodecParameterSets and first frame has no in-band SPS)",
                                           isHevc ? "HEVC" : "H.264");
                                return Error::InvalidArgument;
                        }
                        Buffer cfg;
                        Error  e = buildDecoderConfig(psView, isHevc, cfg);
                        if (e.isError()) {
                                promekiErr("FfmpegMediaIO: failed to build %s record: %s",
                                           isHevc ? "hvcC" : "avcC", e.name().cstr());
                                return e;
                        }
                        st->codecpar->extradata = static_cast<uint8_t *>(
                                av_mallocz(cfg.size() + AV_INPUT_BUFFER_PADDING_SIZE));
                        if (st->codecpar->extradata == nullptr) return Error::NoMem;
                        std::memcpy(st->codecpar->extradata, cfg.data(), cfg.size());
                        st->codecpar->extradata_size = static_cast<int>(cfg.size());
                } else if (psView.isValid()) {
                        st->codecpar->extradata = static_cast<uint8_t *>(
                                av_mallocz(psView.size() + AV_INPUT_BUFFER_PADDING_SIZE));
                        if (st->codecpar->extradata == nullptr) return Error::NoMem;
                        std::memcpy(st->codecpar->extradata, psView.data(), psView.size());
                        st->codecpar->extradata_size = static_cast<int>(psView.size());
                }
                _outVideo = st;
        }

        // ---- Audio stream ----
        auto auds = frame.audioPayloads();
        if (!auds.isEmpty() && auds[0].isValid()) {
                const AudioPayload &ap = *auds[0];
                const AudioDesc    &ad = ap.desc();
                const AVCodecID     cid = avCodecForAudioFormat(ad.format());
                if (cid == AV_CODEC_ID_NONE) {
                        promekiErr("FfmpegMediaIO: no AVCodec for AudioFormat '%s'",
                                   ad.format().name().cstr());
                        return Error::NotSupported;
                }
                // The writer carries no audio codec extradata, so codecs whose
                // container mapping needs an out-of-band config record would
                // mux an undecodable track — reject them with a clear error
                // rather than producing a broken file.
                if (audioFormatNeedsExtradata(cid)) {
                        promekiErr("FfmpegMediaIO: muxing %s requires codec extradata this backend "
                                   "does not emit — use a native backend (e.g. QuickTime) instead",
                                   ad.format().name().cstr());
                        return Error::NotSupported;
                }
                AVStream *st = avformat_new_stream(_outFmt, nullptr);
                if (st == nullptr) return Error::NoMem;
                st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
                st->codecpar->codec_id = cid;
                st->codecpar->sample_rate = static_cast<int>(ad.sampleRate());
                av_channel_layout_default(&st->codecpar->ch_layout, static_cast<int>(ad.channels()));
                st->time_base.num = 1;
                st->time_base.den = static_cast<int>(ad.sampleRate());
                _outAudio = st;
        }

        if (_outVideo == nullptr && _outAudio == nullptr) {
                promekiErr("FfmpegMediaIO: first frame carries neither video nor audio");
                return Error::InvalidArgument;
        }

        if (!(_outFmt->oformat->flags & AVFMT_NOFILE)) {
                int rc = avio_open(&_outFmt->pb, _filename.cstr(), AVIO_FLAG_WRITE);
                if (rc < 0) {
                        promekiErr("FfmpegMediaIO: avio_open '%s' failed: %s", _filename.cstr(),
                                   ffmpegErrorString(rc).cstr());
                        return Error::IOError;
                }
        }
        int rc = avformat_write_header(_outFmt, nullptr);
        if (rc < 0) {
                promekiErr("FfmpegMediaIO: write_header failed: %s", ffmpegErrorString(rc).cstr());
                return Error::IOError;
        }
        _headerWritten = true;
        _writerTracksRegistered = true;
        return Error::Ok;
}

Error FfmpegMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!_isOpen || !_isWrite) return Error::NotOpen;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;
        const Frame &frame = cmd.frame;

        Error err = setupWriterFromFrame(frame);
        if (err.isError()) return err;

        // ---- Video ----
        if (_outVideo != nullptr) {
                auto vids = frame.videoPayloads();
                if (!vids.isEmpty() && vids[0].isValid()) {
                        const auto *cvp = vids[0]->as<CompressedVideoPayload>();
                        if (cvp != nullptr && cvp->planeCount() > 0) {
                                auto view = cvp->plane(0);
                                if (view.isValid() && view.size() > 0) {
                                        // H.264 / HEVC samples are stored
                                        // length-prefixed (AVCC) in mp4 /
                                        // Matroska, with parameter sets carried
                                        // only in the avcC / hvcC record — so
                                        // convert the encoder's Annex-B access
                                        // unit and strip its in-band SPS/PPS.
                                        // Other codecs are muxed verbatim.
                                        const AVCodecID vcid = _outVideo->codecpar->codec_id;
                                        const bool      isH264 = (vcid == AV_CODEC_ID_H264);
                                        const bool      isHevc = (vcid == AV_CODEC_ID_HEVC);
                                        Buffer          converted;
                                        const uint8_t  *outBytes = static_cast<const uint8_t *>(view.data());
                                        size_t          outSize = view.size();
                                        if (isH264 || isHevc) {
                                                BufferView bv(view.buffer(), view.offset(), view.size());
                                                Error      ce = H264Bitstream::annexBToAvccFiltered(
                                                        bv, 4,
                                                        isHevc ? keepNonHevcParameterSetNal
                                                               : keepNonH264ParameterSetNal,
                                                        converted);
                                                if (ce.isError()) {
                                                        promekiWarn("FfmpegMediaIO: Annex-B→AVCC "
                                                                    "conversion failed: %s",
                                                                    ce.name().cstr());
                                                } else {
                                                        outBytes = static_cast<const uint8_t *>(
                                                                converted.data());
                                                        outSize = converted.size();
                                                }
                                        }
                                        AVPacket *pkt = av_packet_alloc();
                                        if (pkt == nullptr) return Error::NoMem;
                                        if (av_new_packet(pkt, static_cast<int>(outSize)) == 0) {
                                                std::memcpy(pkt->data, outBytes, outSize);
                                                pkt->stream_index = _outVideo->index;
                                                pkt->pts = _writerVideoPts;
                                                pkt->dts = _writerVideoPts;
                                                pkt->duration = 1;
                                                if (cvp->isKeyframe()) pkt->flags |= AV_PKT_FLAG_KEY;
                                                int rc = av_interleaved_write_frame(_outFmt, pkt);
                                                if (rc < 0) {
                                                        promekiWarn("FfmpegMediaIO: write video frame "
                                                                    "failed: %s",
                                                                    ffmpegErrorString(rc).cstr());
                                                }
                                                _writerVideoPts += 1;
                                        }
                                        av_packet_free(&pkt);
                                }
                        }
                }
        }

        // ---- Audio ----
        if (_outAudio != nullptr) {
                auto auds = frame.audioPayloads();
                for (const auto &apPtr : auds) {
                        if (!apPtr.isValid()) continue;
                        const AudioPayload &ap = *apPtr;
                        // Both compressed and PCM payloads carry their bytes in
                        // plane 0.
                        if (ap.planeCount() == 0) continue;
                        auto view = ap.plane(0);
                        if (!view.isValid() || view.size() == 0) continue;
                        const size_t samples = ap.sampleCount();
                        AVPacket    *pkt = av_packet_alloc();
                        if (pkt == nullptr) return Error::NoMem;
                        if (av_new_packet(pkt, static_cast<int>(view.size())) == 0) {
                                std::memcpy(pkt->data, view.data(), view.size());
                                pkt->stream_index = _outAudio->index;
                                pkt->pts = _writerAudioPts;
                                pkt->dts = _writerAudioPts;
                                pkt->duration = static_cast<int64_t>(samples);
                                pkt->flags |= AV_PKT_FLAG_KEY;
                                int rc = av_interleaved_write_frame(_outFmt, pkt);
                                if (rc < 0) {
                                        promekiWarn("FfmpegMediaIO: write audio frame failed: %s",
                                                    ffmpegErrorString(rc).cstr());
                                }
                                _writerAudioPts += static_cast<int64_t>(samples);
                        }
                        av_packet_free(&pkt);
                }
        }

        cmd.currentFrame = FrameNumber(_writerVideoPts);
        cmd.frameCount = FrameCount(_writerVideoPts);
        return Error::Ok;
}

// ============================================================================
// Seek
// ============================================================================

Error FfmpegMediaIO::executeCmd(MediaIOCommandSeek &cmd) {
        if (!_isOpen || _isWrite) return Error::IllegalSeek;
        if (_inFmt == nullptr) return Error::IllegalSeek;

        int64_t target = cmd.frameNumber.isValid() ? cmd.frameNumber.value() : 0;
        if (target < 0) target = 0;
        if (_frameCount.isFinite() && target >= _frameCount.value()) target = _frameCount.value() - 1;

        const int seekStream = (_videoStream >= 0) ? _videoStream : _audioStream;
        int64_t   ts = 0;
        if (seekStream >= 0 && _frameRate.isValid()) {
                const AVStream  *st = _inFmt->streams[seekStream];
                const double     secs = static_cast<double>(target) * _frameRate.denominator() /
                                    _frameRate.numerator();
                ts = static_cast<int64_t>(secs * st->time_base.den / st->time_base.num + 0.5);
        }
        int rc = av_seek_frame(_inFmt, seekStream, ts, AVSEEK_FLAG_BACKWARD);
        if (rc < 0) {
                promekiWarn("FfmpegMediaIO: seek to frame %lld failed: %s", static_cast<long long>(target),
                            ffmpegErrorString(rc).cstr());
                return Error::IllegalSeek;
        }
        if (_bsf != nullptr) av_bsf_flush(_bsf);
        _eof = false;
        _currentFrame = FrameNumber(target);
        cmd.currentFrame = _currentFrame;
        return Error::Ok;
}

// ============================================================================
// Negotiation
// ============================================================================

Error FfmpegMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;
        MediaDesc want = offered;
        rewriteVideoForCodec(want);
        rewriteAudioForCodec(want);
        *preferred = want;
        return Error::Ok;
}

void FfmpegMediaIO::rewriteVideoForCodec(MediaDesc &desc) const {
        if (desc.imageList().isEmpty()) return;
        const PixelFormat &offeredPd = desc.imageList()[0].pixelFormat();
        if (!offeredPd.isValid() || offeredPd.isCompressed()) return;

        const VideoCodec vc = config().getAs<VideoCodec>(MediaConfig::FfmpegVideoCodec, VideoCodec());
        if (!vc.isValid()) return;
        const List<PixelFormat> cpfs = vc.compressedPixelFormats();
        if (cpfs.isEmpty()) return;
        const PixelFormat compressed = cpfs[0];
        ImageDesc::List &imgs = desc.imageList();
        for (size_t i = 0; i < imgs.size(); ++i) imgs[i].setPixelFormat(compressed);
}

void FfmpegMediaIO::rewriteAudioForCodec(MediaDesc &desc) const {
        if (desc.audioList().isEmpty()) return;
        const AudioFormat &af = desc.audioList()[0].format();
        if (!af.isValid() || af.isCompressed()) return;

        const AudioCodec ac = config().getAs<AudioCodec>(MediaConfig::FfmpegAudioCodec,
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

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_FFMPEG
