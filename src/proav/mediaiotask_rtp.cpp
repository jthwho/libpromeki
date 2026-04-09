/**
 * @file      mediaiotask_rtp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <cstdint>
#include <promeki/mediaiotask_rtp.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/image.h>
#include <promeki/audio.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaconfig.h>
#include <promeki/metadata.h>
#include <promeki/rtpsession.h>
#include <promeki/rtppayload.h>
#include <promeki/udpsockettransport.h>
#include <promeki/sdpsession.h>
#include <promeki/pixeldesc.h>
#include <promeki/logger.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/json.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO(MediaIOTask_Rtp)

// ----- FormatDesc and defaults -----

MediaIO::FormatDesc MediaIOTask_Rtp::formatDesc() {
        return {
                "Rtp",
                "RTP video + audio + metadata transmitter (MJPEG / L16 / JSON)",
                {},     // No file extensions — this is a network sink.
                false,  // canRead  (reader path not yet implemented)
                true,   // canWrite
                false,  // canReadWrite
                []() -> MediaIOTask * {
                        return new MediaIOTask_Rtp();
                },
                []() -> MediaIO::Config {
                        MediaIO::Config cfg;
                        // Transport-global defaults.
                        cfg.set(MediaConfig::RtpLocalAddress, SocketAddress::any(0));
                        cfg.set(MediaConfig::RtpSessionName, String("promeki RTP stream"));
                        cfg.set(MediaConfig::RtpSessionOrigin, String("-"));
                        cfg.set(MediaConfig::RtpPacingMode, RtpPacingMode::Auto);
                        cfg.set(MediaConfig::RtpMulticastTTL, 64);
                        cfg.set(MediaConfig::RtpMulticastInterface, String());
                        cfg.set(MediaConfig::RtpSaveSdpPath, String());

                        // Per-stream defaults: empty destinations, so
                        // an unconfigured RTP sink transmits nothing.
                        cfg.set(MediaConfig::VideoRtpDestination, SocketAddress());
                        cfg.set(MediaConfig::VideoRtpPayloadType, 96);
                        cfg.set(MediaConfig::VideoRtpClockRate, 90000);
                        cfg.set(MediaConfig::VideoRtpSsrc, uint32_t(0));
                        cfg.set(MediaConfig::VideoRtpDscp, 46);
                        cfg.set(MediaConfig::VideoRtpTargetBitrate, 0);

                        cfg.set(MediaConfig::AudioRtpDestination, SocketAddress());
                        cfg.set(MediaConfig::AudioRtpPayloadType, 96);
                        cfg.set(MediaConfig::AudioRtpClockRate, 0);
                        cfg.set(MediaConfig::AudioRtpSsrc, uint32_t(0));
                        cfg.set(MediaConfig::AudioRtpDscp, 34);
                        cfg.set(MediaConfig::AudioRtpPacketTimeUs, 1000);

                        cfg.set(MediaConfig::DataEnabled, false);
                        cfg.set(MediaConfig::DataRtpDestination, SocketAddress());
                        cfg.set(MediaConfig::DataRtpPayloadType, 98);
                        cfg.set(MediaConfig::DataRtpClockRate, 90000);
                        cfg.set(MediaConfig::DataRtpSsrc, uint32_t(0));
                        cfg.set(MediaConfig::DataRtpDscp, 34);
                        cfg.set(MediaConfig::DataRtpFormat, MetadataRtpFormat::JsonMetadata);
                        return cfg;
                },
                []() -> Metadata {
                        // An RTP sink does not produce or consume
                        // container-level metadata today — the
                        // per-frame Metadata is what rides in the
                        // data stream.  Return an empty schema.
                        return Metadata();
                }
        };
}

// ----- Ctor / dtor -----

MediaIOTask_Rtp::MediaIOTask_Rtp() {
        _video.mediaType = "video";
        _audio.mediaType = "audio";
        _data.mediaType  = "application";
}

MediaIOTask_Rtp::~MediaIOTask_Rtp() {
        resetAll();
}

// ----- Helpers -----

void MediaIOTask_Rtp::resetStream(Stream &s) {
        if(s.session != nullptr) {
                s.session->stop();
                delete s.session;
                s.session = nullptr;
        }
        if(s.transport != nullptr) {
                s.transport->close();
                delete s.transport;
                s.transport = nullptr;
        }
        delete s.payload;
        s.payload = nullptr;
        s.destination = SocketAddress();
        s.packetsSent = 0;
        s.bytesSent   = 0;
        s.rtpmap.clear();
        s.fmtp.clear();
        s.active = false;
}

void MediaIOTask_Rtp::resetAll() {
        resetStream(_video);
        resetStream(_audio);
        resetStream(_data);
        _frameCount    = 0;
        _framesSent    = 0;
        _framesDropped = 0;
        _sdpText.clear();
}

static bool isJpegPixelDesc(const PixelDesc &pd) {
        // Any PixelDesc whose codec is "jpeg" is a valid RtpPayloadJpeg
        // input — the codec registry is the single source of truth
        // for what's in the JPEG family, and it's cheaper than
        // enumerating every (subsampling × matrix × range) variant
        // by hand.  Non-compressed formats and non-JPEG compressed
        // formats (H.264, HEVC, ProRes, ...) all fall through.
        return pd.isValid() && pd.isCompressed() && pd.codecName() == "jpeg";
}

Error MediaIOTask_Rtp::openStream(Stream &s, bool enableMulticastLoopback) {
        if(s.destination.isNull() || !s.destination.isIPv4()) {
                resetStream(s);
                return Error::Ok; // Nothing to open — stream disabled.
        }

        s.transport = new UdpSocketTransport();
        s.transport->setLocalAddress(_localAddress);
        s.transport->setDscp(static_cast<uint8_t>(s.dscp & 0x3F));
        if(_multicastTTL > 0) s.transport->setMulticastTTL(_multicastTTL);
        if(!_multicastInterface.isEmpty()) {
                s.transport->setMulticastInterface(_multicastInterface);
        }
        if(enableMulticastLoopback) s.transport->setMulticastLoopback(true);

        Error err = s.transport->open();
        if(err.isError()) {
                promekiErr("MediaIOTask_Rtp: failed to open %s transport: %s",
                           s.mediaType.cstr(), err.desc().cstr());
                resetStream(s);
                return err;
        }

        s.session = new RtpSession();
        s.session->setClockRate(s.clockRate);
        s.session->setPayloadType(s.payloadType);
        if(s.ssrc != 0) s.session->setSsrc(s.ssrc);
        s.session->setRemote(s.destination);

        err = s.session->start(s.transport);
        if(err.isError()) {
                promekiErr("MediaIOTask_Rtp: failed to start %s session: %s",
                           s.mediaType.cstr(), err.desc().cstr());
                resetStream(s);
                return err;
        }

        // Apply kernel pacing if requested.  KernelFq uses the
        // transport's setPacingRate() which maps to SO_MAX_PACING_RATE
        // on Linux; other modes are handled at per-frame send time.
        if(_pacingMode.value() == RtpPacingMode::KernelFq.value()) {
                // Rate drives how fast the fq qdisc drains.  Use a
                // 20% headroom above the configured bitrate.
                // Callers that know a tight bitrate pass it via
                // VideoRtpTargetBitrate; otherwise we compute a
                // generous default below.
        }

        s.active = true;
        return Error::Ok;
}

// ----- Per-stream configuration -----

Error MediaIOTask_Rtp::configureVideoStream(const MediaIO::Config &cfg,
                                             const MediaDesc &mediaDesc) {
        SocketAddress dest = cfg.getAs<SocketAddress>(MediaConfig::VideoRtpDestination, SocketAddress());
        if(dest.isNull()) return Error::Ok; // Disabled.

        if(mediaDesc.imageList().isEmpty()) {
                promekiErr("MediaIOTask_Rtp: VideoRtpDestination set but no video track in media descriptor");
                return Error::InvalidArgument;
        }
        const ImageDesc &img = mediaDesc.imageList()[0];
        if(!img.isValid()) {
                promekiErr("MediaIOTask_Rtp: invalid video descriptor");
                return Error::InvalidArgument;
        }

        _video.destination = dest;
        _video.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::VideoRtpPayloadType, 96) & 0x7F);
        _video.clockRate   = static_cast<uint32_t>(cfg.getAs<int>(MediaConfig::VideoRtpClockRate, 90000));
        _video.ssrc        = static_cast<uint32_t>(cfg.getAs<uint32_t>(MediaConfig::VideoRtpSsrc, 0));
        _video.dscp        = cfg.getAs<int>(MediaConfig::VideoRtpDscp, 46);

        // Pick payload class from the pixel descriptor family.
        const PixelDesc &pd = img.pixelDesc();
        if(isJpegPixelDesc(pd)) {
                // MJPEG / RFC 2435.  Width and height are in pixels;
                // the pack() path fragments the already-compressed
                // JPEG bitstream across packets.
                _video.payload = new RtpPayloadJpeg(static_cast<int>(img.width()),
                                                    static_cast<int>(img.height()));
                // RFC 2435 mandates PT=26, but many deployments use
                // a dynamic PT. Respect what the user configured,
                // with 26 as the fallback when the default (96) is
                // obviously wrong for JPEG.
                if(_video.payloadType == 96) _video.payloadType = 26;
                _video.rtpmap = String("JPEG/") + String::number(_video.clockRate);
        } else if(!pd.isCompressed()) {
                // RFC 4175 raw video — limited to 8-bit interleaved
                // formats in this first pass.  Proper ST 2110-20
                // pgroup handling for 10/12-bit is deferred.
                // bpp is computed from bytesPerBlock / pixelsPerBlock.
                const PixelFormat &pf = pd.pixelFormat();
                size_t ppb = pf.pixelsPerBlock();
                size_t bpb = pf.bytesPerBlock();
                int bpp = (ppb > 0) ? static_cast<int>((8 * bpb) / ppb) : 0;
                if(bpp == 0) {
                        promekiErr("MediaIOTask_Rtp: video pixel desc has zero bits-per-pixel");
                        return Error::InvalidArgument;
                }
                _video.payload = new RtpPayloadRawVideo(static_cast<int>(img.width()),
                                                         static_cast<int>(img.height()),
                                                         bpp);
                _video.rtpmap = String("raw/") + String::number(_video.clockRate);
        } else {
                promekiErr("MediaIOTask_Rtp: unsupported video pixel format '%s'",
                           pd.name().cstr());
                return Error::NotSupported;
        }

        return Error::Ok;
}

Error MediaIOTask_Rtp::configureAudioStream(const MediaIO::Config &cfg,
                                             const MediaDesc &mediaDesc) {
        SocketAddress dest = cfg.getAs<SocketAddress>(MediaConfig::AudioRtpDestination, SocketAddress());
        if(dest.isNull()) return Error::Ok; // Disabled.

        if(mediaDesc.audioList().isEmpty()) {
                promekiErr("MediaIOTask_Rtp: AudioRtpDestination set but no audio track in media descriptor");
                return Error::InvalidArgument;
        }
        const AudioDesc &ad = mediaDesc.audioList()[0];
        if(!ad.isValid()) {
                promekiErr("MediaIOTask_Rtp: invalid audio descriptor");
                return Error::InvalidArgument;
        }

        _audio.destination = dest;
        _audio.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::AudioRtpPayloadType, 96) & 0x7F);
        _audio.ssrc        = cfg.getAs<uint32_t>(MediaConfig::AudioRtpSsrc, 0);
        _audio.dscp        = cfg.getAs<int>(MediaConfig::AudioRtpDscp, 34);

        int cfgClockRate = cfg.getAs<int>(MediaConfig::AudioRtpClockRate, 0);
        _audio.clockRate = cfgClockRate > 0
                ? static_cast<uint32_t>(cfgClockRate)
                : static_cast<uint32_t>(ad.sampleRate());

        const uint32_t sr = static_cast<uint32_t>(ad.sampleRate());
        const unsigned int ch = ad.channels();
        if(sr == 0 || ch == 0) {
                promekiErr("MediaIOTask_Rtp: audio sample rate or channel count is zero");
                return Error::InvalidArgument;
        }

        // -- AES67 packet sizing --
        //
        // packetSamples = sampleRate × packetTimeUs / 1e6 samples per
        // channel.  That must fit in the transport MTU after the RTP
        // header, so there is an upper bound of
        // (maxPayload / (channels × bytesPerSample)) samples.  If the
        // requested packet time exceeds the cap we fall back through
        // the standard AES67 intervals (4ms → 1ms → 333µs → 250µs →
        // 125µs) until we find one that fits.
        //
        // L16 storage is 2 bytes per sample per channel; channels can
        // be anything up to 64 (AES67 allows 1-8 typically).
        static constexpr size_t kMaxBytesPerPacket = 1200;
        static constexpr size_t kStorageBytesPerSample = 2; // L16
        const size_t maxSamplesPerPacket =
                kMaxBytesPerPacket / (static_cast<size_t>(ch) * kStorageBytesPerSample);
        if(maxSamplesPerPacket == 0) {
                promekiErr("MediaIOTask_Rtp: %u audio channels at L16 will not fit in %zu-byte MTU",
                           ch, kMaxBytesPerPacket);
                return Error::InvalidArgument;
        }

        const int requestedUs = cfg.getAs<int>(MediaConfig::AudioRtpPacketTimeUs, 1000);
        auto samplesForUs = [sr](int us) -> size_t {
                return (static_cast<uint64_t>(sr) * static_cast<uint64_t>(us)) / 1'000'000ull;
        };

        size_t resolvedSamples = samplesForUs(requestedUs);
        int    resolvedUs      = requestedUs;
        if(resolvedSamples == 0 || resolvedSamples > maxSamplesPerPacket) {
                // Fall back through the AES67 standard set, largest
                // first so we keep packet counts low when possible.
                static constexpr int kAes67Intervals[] = { 4000, 1000, 333, 250, 125 };
                resolvedSamples = 0;
                for(int us : kAes67Intervals) {
                        size_t s = samplesForUs(us);
                        if(s > 0 && s <= maxSamplesPerPacket) {
                                resolvedSamples = s;
                                resolvedUs      = us;
                                break;
                        }
                }
                if(resolvedSamples == 0) {
                        // Last resort: one sample per packet.
                        resolvedSamples = 1;
                        resolvedUs      = static_cast<int>((1'000'000ull + sr - 1) / sr);
                }
                promekiWarn("MediaIOTask_Rtp: audio packet time %dus exceeds MTU for %u channels; clamped to %dus (%zu samples/packet)",
                            requestedUs, ch, resolvedUs, resolvedSamples);
        }

        _audioState.packetSamples = resolvedSamples;
        _audioState.packetBytes   = resolvedSamples *
                                    static_cast<size_t>(ch) *
                                    kStorageBytesPerSample;
        _audioState.packetTimeUs  = resolvedUs;
        _audioState.nextTimestamp = 0;

        // -- Payload handler --
        //
        // L16 only for this first pass.  Clamp the payload's
        // max-payload-size to exactly packetBytes so pack() emits one
        // RtpPacket per AES67 packet (instead of trying to pack more
        // into a single datagram).
        auto *pl16 = new RtpPayloadL16(sr, static_cast<int>(ch));
        pl16->setPayloadType(_audio.payloadType);
        pl16->setMaxPayloadSize(_audioState.packetBytes);
        _audio.payload = pl16;
        _audio.rtpmap = String("L16/") + String::number(_audio.clockRate) +
                        String("/") + String::number(ch);

        // -- FIFO --
        //
        // Storage format = PCMI_S16BE.  AudioBuffer will
        // transparently convert any compatible input (S16LE, S16BE,
        // Float32LE, Float32BE, S24LE/BE, S32LE/BE, etc.) into the
        // storage format on push, so the backend accepts whatever
        // the pipeline hands it provided the sample rate and channel
        // count already match.  Rate / channel conversion still
        // belongs to an upstream Converter stage.
        AudioDesc storageDesc(AudioDesc::PCMI_S16BE, ad.sampleRate(), ad.channels());
        if(!storageDesc.isValid()) {
                promekiErr("MediaIOTask_Rtp: could not build L16 storage descriptor (%.1f Hz, %u ch)",
                           ad.sampleRate(), ad.channels());
                return Error::InvalidArgument;
        }
        _audioState.fifo = AudioBuffer(storageDesc);
        // Reserve one second of headroom — generous enough to absorb
        // any reasonable writeFrame burstiness without the producer
        // ever hitting NoSpace.
        Error rsvErr = _audioState.fifo.reserve(static_cast<size_t>(ad.sampleRate()));
        if(rsvErr.isError()) {
                promekiErr("MediaIOTask_Rtp: failed to reserve audio FIFO: %s",
                           rsvErr.desc().cstr());
                return rsvErr;
        }

        return Error::Ok;
}

Error MediaIOTask_Rtp::configureDataStream(const MediaIO::Config &cfg) {
        bool enabled = cfg.getAs<bool>(MediaConfig::DataEnabled, false);
        SocketAddress dest = cfg.getAs<SocketAddress>(MediaConfig::DataRtpDestination, SocketAddress());
        if(!enabled || dest.isNull()) return Error::Ok; // Disabled.

        _data.destination = dest;
        _data.payloadType = static_cast<uint8_t>(cfg.getAs<int>(MediaConfig::DataRtpPayloadType, 98) & 0x7F);
        _data.clockRate   = static_cast<uint32_t>(cfg.getAs<int>(MediaConfig::DataRtpClockRate, 90000));
        _data.ssrc        = static_cast<uint32_t>(cfg.getAs<uint32_t>(MediaConfig::DataRtpSsrc, 0));
        _data.dscp        = cfg.getAs<int>(MediaConfig::DataRtpDscp, 34);

        Error fmtErr;
        Enum fmt = cfg.get(MediaConfig::DataRtpFormat).asEnum(MetadataRtpFormat::Type, &fmtErr);
        if(fmtErr.isError() || !fmt.hasListedValue()) {
                promekiErr("MediaIOTask_Rtp: unknown metadata RTP format");
                return Error::InvalidArgument;
        }
        _dataFormat = fmt;

        if(fmt.value() == MetadataRtpFormat::JsonMetadata.value()) {
                auto *p = new RtpPayloadJson(_data.payloadType, _data.clockRate);
                _data.payload = p;
                _data.rtpmap = String("x-promeki-metadata-json/") + String::number(_data.clockRate);
        } else {
                promekiErr("MediaIOTask_Rtp: metadata format %s is not yet implemented",
                           fmt.toString().cstr());
                return Error::NotSupported;
        }
        return Error::Ok;
}

// ----- SDP -----

void MediaIOTask_Rtp::buildSdp() {
        SdpSession sdp;
        sdp.setSessionName(_sessionName);
        sdp.setOrigin(_sessionOrigin, 0, 0, "IN", "IP4", "0.0.0.0");

        auto addStream = [&](const Stream &s) {
                if(!s.active) return;
                SdpMediaDescription md;
                md.setMediaType(s.mediaType);
                md.setPort(s.destination.port());
                md.setProtocol("RTP/AVP");
                md.addPayloadType(s.payloadType);
                if(!s.rtpmap.isEmpty()) {
                        md.setAttribute("rtpmap",
                                String::number(s.payloadType) +
                                String(" ") + s.rtpmap);
                }
                if(!s.fmtp.isEmpty()) {
                        md.setAttribute("fmtp",
                                String::number(s.payloadType) +
                                String(" ") + s.fmtp);
                }
                // RFC 5761 rtcp-mux: tells receivers that RTP and
                // RTCP share a single port for this stream, so the
                // receiver does not open a separate socket at RTP
                // port + 1.  Without this, ffplay (and any other SDP
                // consumer that honours the classic RTCP-on-next-port
                // convention) tries to bind port + 1 for RTCP, which
                // collides when the user has two streams on adjacent
                // ports (e.g. video 10000 / audio 10001).  Since this
                // backend does not transmit RTCP at all — we are a
                // pure sender — multiplexing is a cosmetic lie that
                // has zero wire impact but unblocks the receiver's
                // socket setup.
                md.setAttribute("rtcp-mux", String());
                md.setConnectionAddress(s.destination.address().toString());
                sdp.addMediaDescription(md);
        };
        addStream(_video);
        addStream(_audio);
        addStream(_data);

        _sdpText = sdp.toString();
}

Error MediaIOTask_Rtp::writeSdpFile(const String &path) {
        if(path.isEmpty()) return Error::Ok;
        File f(path);
        Error err = f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("MediaIOTask_Rtp: failed to open SDP file '%s' for write: %s",
                           path.cstr(), err.desc().cstr());
                return err;
        }
        int64_t n = f.write(_sdpText.cstr(), static_cast<int64_t>(_sdpText.size()));
        f.close();
        if(n != static_cast<int64_t>(_sdpText.size())) return Error::IOError;
        return Error::Ok;
}

// ----- Command dispatch -----

Error MediaIOTask_Rtp::executeCmd(MediaIOCommandOpen &cmd) {
        if(cmd.mode != MediaIO::Writer) {
                promekiErr("MediaIOTask_Rtp: only Writer mode is supported in this release");
                return Error::NotSupported;
        }

        const MediaIO::Config &cfg = cmd.config;

        // Transport-global parameters.
        _localAddress = cfg.getAs<SocketAddress>(MediaConfig::RtpLocalAddress, SocketAddress::any(0));
        _sessionName  = cfg.getAs<String>(MediaConfig::RtpSessionName, String("promeki RTP stream"));
        _sessionOrigin = cfg.getAs<String>(MediaConfig::RtpSessionOrigin, String("-"));
        _multicastTTL  = cfg.getAs<int>(MediaConfig::RtpMulticastTTL, 64);
        _multicastInterface = cfg.getAs<String>(MediaConfig::RtpMulticastInterface, String());
        _sdpPath = cfg.getAs<String>(MediaConfig::RtpSaveSdpPath, String());

        Error pmErr;
        _pacingMode = cfg.get(MediaConfig::RtpPacingMode).asEnum(RtpPacingMode::Type, &pmErr);
        if(pmErr.isError() || !_pacingMode.hasListedValue()) {
                promekiErr("MediaIOTask_Rtp: unknown RTP pacing mode");
                return Error::InvalidArgument;
        }
        // Auto resolves to the best mechanism available on this
        // platform.  Linux gets kernel pacing via SO_MAX_PACING_RATE
        // (zero per-packet CPU cost, honoured by the fq qdisc when
        // active); other platforms fall back to userspace sleep
        // pacing because they cannot offer anything better.  Users
        // who want a specific behaviour (None for loopback tests,
        // TxTime for ST 2110-21 deployments) still set the key
        // explicitly.
        if(_pacingMode.value() == RtpPacingMode::Auto.value()) {
#if defined(PROMEKI_PLATFORM_LINUX)
                _pacingMode = RtpPacingMode::KernelFq;
#else
                _pacingMode = RtpPacingMode::Userspace;
#endif
        }

        _frameRate = cmd.pendingMediaDesc.frameRate();
        if(!_frameRate.isValid()) {
                // Fall back to config; if still absent, the timestamp
                // math still works but downstream receivers may not
                // know the frame rate via SDP.
                _frameRate = cfg.getAs<FrameRate>(MediaConfig::FrameRate,
                                                   FrameRate(FrameRate::FPS_2997));
        }

        // Configure each stream from the media descriptor + per-stream config.
        Error err = configureVideoStream(cfg, cmd.pendingMediaDesc);
        if(err.isError()) { resetAll(); return err; }
        err = configureAudioStream(cfg, cmd.pendingMediaDesc);
        if(err.isError()) { resetAll(); return err; }
        err = configureDataStream(cfg);
        if(err.isError()) { resetAll(); return err; }

        // Enable multicast loopback when the destination is on this
        // host — lets a co-located receiver see our own packets.  Not
        // critical for production but useful for self-tests.
        auto isLocalMulticast = [](const SocketAddress &a) {
                return a.isMulticast() || a.isLoopback();
        };
        bool loopback = isLocalMulticast(_video.destination) ||
                        isLocalMulticast(_audio.destination) ||
                        isLocalMulticast(_data.destination);

        err = openStream(_video, loopback);
        if(err.isError()) { resetAll(); return err; }
        err = openStream(_audio, loopback);
        if(err.isError()) { resetAll(); return err; }
        err = openStream(_data, loopback);
        if(err.isError()) { resetAll(); return err; }

        // At least one stream must be active.
        if(!_video.active && !_audio.active && !_data.active) {
                promekiErr("MediaIOTask_Rtp: no RTP streams configured "
                           "(set VideoRtpDestination / AudioRtpDestination / DataRtpDestination)");
                resetAll();
                return Error::InvalidArgument;
        }

        // Apply kernel-FQ pacing rate if requested.
        if(_pacingMode.value() == RtpPacingMode::KernelFq.value()) {
                auto applyRate = [](Stream &s, uint64_t bitsPerSec) {
                        if(!s.active || bitsPerSec == 0) return;
                        // Add 20% headroom.
                        uint64_t bytesPerSec = (bitsPerSec * 12) / (10 * 8);
                        (void)s.session->setPacingRate(bytesPerSec);
                };
                // Video: user-specified bitrate, or estimate from the
                // descriptor.  Compressed inputs have no single right
                // answer — fall back to 200 Mbps per stream, which
                // accommodates 1080p JPEG at very high quality.
                uint64_t videoBitrate = static_cast<uint64_t>(
                        cfg.getAs<int>(MediaConfig::VideoRtpTargetBitrate, 0));
                if(videoBitrate == 0 && _video.active &&
                   !cmd.pendingMediaDesc.imageList().isEmpty()) {
                        const ImageDesc &img = cmd.pendingMediaDesc.imageList()[0];
                        if(img.pixelDesc().isCompressed()) {
                                videoBitrate = 200'000'000;
                        } else {
                                // Uncompressed: width * height * bpp * fps.
                                // bpp is approximated from
                                // bytesPerBlock / pixelsPerBlock.
                                const PixelFormat &pf = img.pixelDesc().pixelFormat();
                                size_t ppb = pf.pixelsPerBlock();
                                size_t bpb = pf.bytesPerBlock();
                                double bpp = ppb > 0
                                        ? (8.0 * static_cast<double>(bpb) /
                                                 static_cast<double>(ppb))
                                        : 0.0;
                                double fps = _frameRate.isValid()
                                        ? _frameRate.toDouble() : 30.0;
                                videoBitrate = static_cast<uint64_t>(
                                        static_cast<double>(img.width()) *
                                        static_cast<double>(img.height()) *
                                        bpp * fps);
                        }
                }
                applyRate(_video, videoBitrate);

                // Audio: sample_rate * channels * bytes_per_sample * 8.
                if(_audio.active && !cmd.pendingMediaDesc.audioList().isEmpty()) {
                        const AudioDesc &ad = cmd.pendingMediaDesc.audioList()[0];
                        uint64_t audioBitrate = static_cast<uint64_t>(
                                ad.sampleRate() * ad.channels() *
                                ad.bytesPerSample() * 8);
                        applyRate(_audio, audioBitrate);
                }

                // Data: no natural rate — leave unlimited.
        }

        buildSdp();
        if(!_sdpPath.isEmpty()) {
                Error sdpErr = writeSdpFile(_sdpPath);
                if(sdpErr.isError()) { resetAll(); return sdpErr; }
        }

        cmd.mediaDesc  = cmd.pendingMediaDesc;
        cmd.audioDesc  = cmd.pendingAudioDesc;
        cmd.metadata   = cmd.pendingMetadata;
        cmd.frameRate  = _frameRate;
        cmd.canSeek    = false;
        cmd.frameCount = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error MediaIOTask_Rtp::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        resetAll();
        return Error::Ok;
}

// ----- Per-stream send helpers -----

Error MediaIOTask_Rtp::sendVideo(const Image &image) {
        if(!_video.active || !image.isValid()) return Error::Ok;
        if(_video.session == nullptr || _video.payload == nullptr) return Error::Invalid;

        // Compute the RTP timestamp via FrameRate::cumulativeTicks(),
        // which returns the exact cumulative tick count at the start
        // of this frame using 64-bit rational math.  This is the
        // same primitive that drives samplesPerFrame for audio, so
        // the video RTP timestamps stay drift-free across both
        // integer rates (exact stride every frame) and fractional
        // rates (alternating stride within the 1001-frame NTSC
        // period).  Truncating to uint32_t produces the wire value
        // with natural modulo-2^32 wrap.
        uint32_t ts = static_cast<uint32_t>(
                _frameRate.cumulativeTicks(_video.clockRate, _frameCount));

        // Grab plane 0 bytes — for MJPEG this is the compressed
        // bitstream; for RFC 4175 raw video it is the interleaved
        // pixel data.
        const Buffer::Ptr &plane = image.plane(0);
        if(!plane || plane->size() == 0) return Error::Invalid;

        auto packets = _video.payload->pack(plane->data(), plane->size());
        if(packets.isEmpty()) return Error::Invalid;

        Error err;
        if(_pacingMode.value() == RtpPacingMode::Userspace.value() &&
           _frameRate.isValid()) {
                // Spread packets across one frame interval so the
                // receiver sees them arrive evenly instead of in a
                // micro-burst.  Only runs in Userspace pacing mode;
                // KernelFq leaves the spreading to the fq qdisc.
                const double fps = _frameRate.toDouble();
                Duration interval = Duration::fromNanoseconds(
                        static_cast<int64_t>(1'000'000'000.0 / fps));
                err = _video.session->sendPacketsPaced(packets, ts, interval, true);
        } else {
                err = _video.session->sendPackets(packets, ts, true);
        }
        if(err.isError()) return err;

        _video.packetsSent += static_cast<int64_t>(packets.size());
        for(size_t i = 0; i < packets.size(); i++) _video.bytesSent += static_cast<int64_t>(packets[i].size());
        return Error::Ok;
}

Error MediaIOTask_Rtp::sendAudio(const Audio &audio) {
        if(!_audio.active) return Error::Ok;
        if(_audio.session == nullptr || _audio.payload == nullptr) return Error::Invalid;
        if(audio.samples() == 0) return Error::Ok;
        if(_audioState.packetBytes == 0 || _audioState.packetSamples == 0) {
                return Error::Invalid;
        }

        // Push the incoming samples into the FIFO.  AudioBuffer
        // auto-converts bit depth / endian / float↔int from the
        // Audio's own descriptor into the stored L16 big-endian
        // wire format.  Sample rate and channel count must match
        // what we configured at open time.
        Error pushErr = _audioState.fifo.push(audio);
        if(pushErr.isError()) {
                promekiErr("MediaIOTask_Rtp: audio FIFO push failed: %s",
                           pushErr.desc().cstr());
                return pushErr;
        }

        // Drain whole AES67-sized packets out of the FIFO.  Leftover
        // samples (when the incoming audio is not a whole multiple
        // of packetSamples) stay in the FIFO for the next frame.
        const size_t packetSamples = _audioState.packetSamples;
        const size_t packetBytes   = _audioState.packetBytes;
        const size_t available     = _audioState.fifo.available();
        if(available < packetSamples) return Error::Ok;

        const size_t count       = available / packetSamples;
        const size_t totalSamples = count * packetSamples;
        const size_t totalBytes   = count * packetBytes;

        // Drain the aligned block in one pop — one contiguous byte
        // buffer ready to hand to the payload handler.
        List<uint8_t> drained;
        drained.resize(totalBytes);
        size_t popped = _audioState.fifo.pop(drained.data(), totalSamples);
        if(popped != totalSamples) {
                promekiErr("MediaIOTask_Rtp: audio FIFO pop short (%zu / %zu)",
                           popped, totalSamples);
                return Error::IOError;
        }

        // RtpPayloadL16::pack() was configured with maxPayloadSize =
        // packetBytes at open time, so feeding it `count * packetBytes`
        // of samples produces exactly `count` RTP packets all sharing
        // one backing buffer.
        RtpPacket::List packets = _audio.payload->pack(drained.data(), totalBytes);
        if(packets.isEmpty()) return Error::Ok;
        if(packets.size() != count) {
                promekiErr("MediaIOTask_Rtp: payload produced %zu packets, expected %zu",
                           packets.size(), count);
                return Error::IOError;
        }

        // Batch-send with per-packet monotonic timestamps.  Each
        // packet's timestamp reflects the first sample of that
        // packet's contents; the marker bit is off for audio
        // (RFC 3551 reserves it for talkspurt boundaries which we
        // don't track).
        const uint32_t startTs = _audioState.nextTimestamp;
        Error err = _audio.session->sendPackets(packets, startTs,
                                                 static_cast<uint32_t>(packetSamples),
                                                 false /* no marker */);
        if(err.isError()) return err;

        _audioState.nextTimestamp =
                startTs + static_cast<uint32_t>(totalSamples);
        _audio.packetsSent += static_cast<int64_t>(packets.size());
        for(size_t i = 0; i < packets.size(); i++) {
                _audio.bytesSent += static_cast<int64_t>(packets[i].size());
        }
        return Error::Ok;
}

Error MediaIOTask_Rtp::sendData(const Metadata &metadata) {
        if(!_data.active) return Error::Ok;
        if(_data.session == nullptr || _data.payload == nullptr) return Error::Invalid;

        // Only the JsonMetadata format is wired up today; the
        // ST 2110-40 branch is rejected at configure time.
        JsonObject obj = metadata.toJson();
        String json = obj.toString(0); // compact
        if(json.isEmpty()) return Error::Ok;

        double fps = _frameRate.isValid() ? _frameRate.toDouble() : 30.0;
        uint32_t ts = static_cast<uint32_t>(
                static_cast<double>(_frameCount) *
                static_cast<double>(_data.clockRate) / fps);

        auto packets = _data.payload->pack(json.cstr(), json.size());
        if(packets.isEmpty()) return Error::Ok;

        Error err = _data.session->sendPackets(packets, ts, true);
        if(err.isError()) return err;

        _data.packetsSent += static_cast<int64_t>(packets.size());
        for(size_t i = 0; i < packets.size(); i++) _data.bytesSent += static_cast<int64_t>(packets[i].size());
        return Error::Ok;
}

Error MediaIOTask_Rtp::executeCmd(MediaIOCommandWrite &cmd) {
        if(cmd.frame.isNull()) return Error::InvalidArgument;
        const Frame &frame = *cmd.frame;

        // Video
        if(_video.active && !frame.imageList().isEmpty()) {
                const Image::Ptr &imgPtr = frame.imageList()[0];
                if(imgPtr) {
                        Error err = sendVideo(*imgPtr);
                        if(err.isError()) {
                                _framesDropped++;
                                return err;
                        }
                }
        }
        // Audio
        if(_audio.active && !frame.audioList().isEmpty()) {
                const Audio::Ptr &audPtr = frame.audioList()[0];
                if(audPtr) {
                        Error err = sendAudio(*audPtr);
                        if(err.isError()) {
                                _framesDropped++;
                                return err;
                        }
                }
        }
        // Data
        if(_data.active) {
                Error err = sendData(frame.metadata());
                if(err.isError()) {
                        _framesDropped++;
                        return err;
                }
        }

        _frameCount++;
        _framesSent++;
        cmd.currentFrame = _frameCount;
        cmd.frameCount   = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error MediaIOTask_Rtp::executeCmd(MediaIOCommandParams &cmd) {
        if(cmd.name == ParamGetSdp.name()) {
                cmd.result.set(ParamSdp, _sdpText);
                return Error::Ok;
        }
        return Error::NotSupported;
}

Error MediaIOTask_Rtp::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesSent, _framesSent);
        cmd.stats.set(StatsFramesDropped, _framesDropped);
        cmd.stats.set(StatsPacketsSent,
                _video.packetsSent + _audio.packetsSent + _data.packetsSent);
        cmd.stats.set(StatsBytesSent,
                _video.bytesSent + _audio.bytesSent + _data.bytesSent);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
