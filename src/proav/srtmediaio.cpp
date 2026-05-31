/**
 * @file      srtmediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/srtmediaio.h>
#include <promeki/mpegts.h>
#include <promeki/mpegtsdemuxer.h>
#include <promeki/mpegtsframer.h>
#include <promeki/mpegtsmuxer.h>

#include <cstring>
#include <promeki/audiocodec.h>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/smpte302m.h>
#include <promeki/clock.h>
#include <promeki/duration.h>
#include <promeki/videocodec.h>
#include <promeki/bufferview.h>
#include <promeki/enums_mediaio.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/iodevice.h>
#include <promeki/ipv4address.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/pixelformat.h>
#include <promeki/size2d.h>
#include <promeki/srtsocket.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(SrtFactory)

namespace {

        constexpr size_t kDefaultReadBufBytes = 4 * 1316; // 4 SRT messages worth.

        // Build a SocketAddress from "host" + "port".  An empty host
        // defaults to 0.0.0.0 (any); a numeric IPv4 / IPv6 / hostname
        // is parsed via SocketAddress::fromString.  Returns an empty
        // address on parse failure (caller surfaces Error::Invalid).
        SocketAddress buildAddress(const String &host, uint16_t port) {
                if (host.isEmpty()) {
                        return SocketAddress(Ipv4Address::any(), port);
                }
                String hp = host;
                hp += String(":");
                hp += String::number(port);
                Result<SocketAddress> r = SocketAddress::fromString(hp);
                if (r.second().isError()) return SocketAddress();
                return r.first();
        }

} // namespace

MediaIOFactory::Config::SpecMap SrtFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            add = [&specs](MediaConfig::ID id) {
                const VariantSpec *gs = MediaConfig::spec(id);
                if (gs) specs.insert(id, *gs);
        };
        add(MediaConfig::OpenMode);
        add(MediaConfig::FrameRate);
        add(MediaConfig::SrtMode);
        add(MediaConfig::SrtPeerHost);
        add(MediaConfig::SrtPeerPort);
        add(MediaConfig::SrtLocalHost);
        add(MediaConfig::SrtLocalPort);
        add(MediaConfig::SrtLatencyMs);
        add(MediaConfig::SrtPassphrase);
        add(MediaConfig::SrtEncryptionKeyLength);
        add(MediaConfig::SrtStreamId);
        add(MediaConfig::SrtMaxBandwidthBps);
        add(MediaConfig::SrtPayloadSize);
        add(MediaConfig::SrtAcceptTimeoutMs);
        add(MediaConfig::SrtVideoPacing);
        add(MediaConfig::SrtPaceSkipThresholdMs);
        add(MediaConfig::SrtPaceReanchorThresholdMs);
        add(MediaConfig::SrtVideoCodec);
        add(MediaConfig::SrtAudioCodec);
        add(MediaConfig::MpegTsVideoPid);
        add(MediaConfig::MpegTsAudioPid);
        add(MediaConfig::MpegTsPmtPid);
        add(MediaConfig::MpegTsProgramNumber);
        add(MediaConfig::MpegTsPatPmtIntervalMs);
        add(MediaConfig::MpegTsPcrIntervalMs);
        add(MediaConfig::MpegTsMuxRateBps);
        add(MediaConfig::MpegTsAacFraming);
        return specs;
}

Error SrtFactory::urlToConfig(const Url &url, Config *outConfig) const {
        if (outConfig == nullptr) return Error::InvalidArgument;
        if (url.scheme().toLower() != "srt") return Error::InvalidArgument;

        outConfig->set(MediaConfig::Type, name());

        // Authority (host:port) maps to peer-or-local depending on
        // mode.  Default mode is Caller; an explicit "mode" query
        // value wins.  The default-empty host (e.g. srt://:4200/?...)
        // means INADDR_ANY in Listener mode and "no peer specified"
        // (an error) in Caller / Rendezvous.
        const String hostStr = url.host();
        const int    portInt = url.port();
        const String modeStr = url.queryValue(String("mode"), String("caller")).toLower();

        SrtMode mode = SrtMode::Caller;
        if (modeStr == "listener") mode = SrtMode::Listener;
        else if (modeStr == "rendezvous") mode = SrtMode::Rendezvous;
        outConfig->set(MediaConfig::SrtMode, mode);

        if (mode == SrtMode::Listener) {
                if (!hostStr.isEmpty()) outConfig->set(MediaConfig::SrtLocalHost, hostStr);
                if (portInt != Url::PortUnset) {
                        outConfig->set(MediaConfig::SrtLocalPort, int32_t(portInt));
                }
        } else {
                if (!hostStr.isEmpty()) outConfig->set(MediaConfig::SrtPeerHost, hostStr);
                if (portInt != Url::PortUnset) {
                        outConfig->set(MediaConfig::SrtPeerPort, int32_t(portInt));
                }
        }

        // Translate Haivision-style query keys onto our long-form
        // MediaConfig keys.  Run the raw value through the target's
        // VariantSpec so int / int64 / enum keys land as the right
        // type and not as a String the caller's getAs<int32_t> would
        // resolve as 0.  Unknown keys fall through to the framework's
        // generic applyQueryToConfig path; values that don't parse
        // against the spec are flagged there.
        auto setIfPresent = [&](const char *key, MediaConfig::ID id) {
                if (!url.query().contains(String(key))) return;
                const String       raw = url.queryValue(String(key));
                const VariantSpec *spec = MediaConfig::spec(id);
                if (spec == nullptr) {
                        outConfig->set(id, raw);
                        return;
                }
                Error   perr;
                Variant v = spec->parseString(raw, &perr);
                if (perr.isError() || !v.isValid()) {
                        outConfig->set(id, raw);
                        return;
                }
                outConfig->set(id, v);
        };
        setIfPresent("latency", MediaConfig::SrtLatencyMs);
        setIfPresent("rcvlatency", MediaConfig::SrtLatencyMs);
        setIfPresent("peerlatency", MediaConfig::SrtLatencyMs);
        setIfPresent("passphrase", MediaConfig::SrtPassphrase);
        setIfPresent("pbkeylen", MediaConfig::SrtEncryptionKeyLength);
        setIfPresent("streamid", MediaConfig::SrtStreamId);
        setIfPresent("maxbw", MediaConfig::SrtMaxBandwidthBps);
        setIfPresent("payloadsize", MediaConfig::SrtPayloadSize);
        setIfPresent("payload_size", MediaConfig::SrtPayloadSize);
        setIfPresent("pkt_size", MediaConfig::SrtPayloadSize);
        setIfPresent("adapter", MediaConfig::SrtLocalHost);
        setIfPresent("localport", MediaConfig::SrtLocalPort);
        setIfPresent("timeout", MediaConfig::SrtAcceptTimeoutMs);
        setIfPresent("listen_timeout", MediaConfig::SrtAcceptTimeoutMs);

        return Error::Ok;
}

MediaIO *SrtFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new SrtMediaIO(parent);
        io->setConfig(config);
        return io;
}

SrtMediaIO::SrtMediaIO(ObjectBase *parent) : SharedThreadMediaIO(parent) {}

SrtMediaIO::~SrtMediaIO() {
        if (isOpen()) (void)close().wait();
        if (_transport) _transport->close();
}

void SrtMediaIO::applyFramerConfig(const MediaIO::Config &cfg) {
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
        const Enum aacEnum = cfg.get(MediaConfig::MpegTsAacFraming).asEnum(MpegTsAacFraming::Type);
        const MpegTsFramer::AacFraming framing = (aacEnum == MpegTsAacFraming::Latm)
                                                         ? MpegTsFramer::AacFraming::Latm
                                                         : MpegTsFramer::AacFraming::Adts;
        _framer->setAacFraming(framing);
}

Error SrtMediaIO::openTransport(const MediaIO::Config &cfg) {
        const Enum modeEnum = cfg.get(MediaConfig::SrtMode).asEnum(SrtMode::Type);
        SrtSocketTransport::Mode mode = SrtSocketTransport::Caller;
        if (modeEnum == SrtMode::Listener) mode = SrtSocketTransport::Listener;
        else if (modeEnum == SrtMode::Rendezvous) mode = SrtSocketTransport::Rendezvous;

        const String peerHost   = cfg.getAs<String>(MediaConfig::SrtPeerHost, String());
        const int    peerPort   = cfg.getAs<int32_t>(MediaConfig::SrtPeerPort, int32_t(0));
        const String localHost  = cfg.getAs<String>(MediaConfig::SrtLocalHost, String());
        const int    localPort  = cfg.getAs<int32_t>(MediaConfig::SrtLocalPort, int32_t(0));
        const int    latencyMs  = cfg.getAs<int32_t>(MediaConfig::SrtLatencyMs, int32_t(120));
        const String passphrase = cfg.getAs<String>(MediaConfig::SrtPassphrase, String());
        const int    pbKeyLen   = cfg.getAs<int32_t>(MediaConfig::SrtEncryptionKeyLength, int32_t(0));
        const String streamId   = cfg.getAs<String>(MediaConfig::SrtStreamId, String());
        const int64_t maxBwBps  = cfg.getAs<int64_t>(MediaConfig::SrtMaxBandwidthBps, int64_t(0));
        const int    payloadSz  = cfg.getAs<int32_t>(MediaConfig::SrtPayloadSize, int32_t(1316));
        const int    acceptTo   = cfg.getAs<int32_t>(MediaConfig::SrtAcceptTimeoutMs, int32_t(0));

        _writePayloadSize = (payloadSz > 0) ? static_cast<size_t>(payloadSz) : 1316;

        _transport.reset(new SrtSocketTransport(mode));
        if (!_transport) return Error::NoMem;
        _transport->setLatency(latencyMs);
        if (!passphrase.isEmpty()) {
                Error e = _transport->setPassphrase(passphrase);
                if (e.isError()) {
                        promekiErr("SrtMediaIO: passphrase invalid (len=%zu)", passphrase.byteCount());
                        return e;
                }
        }
        if (pbKeyLen != 0) {
                Error e = _transport->setEncryptionKeyLength(pbKeyLen);
                if (e.isError()) return e;
        }
        if (!streamId.isEmpty()) {
                Error e = _transport->setStreamId(streamId);
                if (e.isError()) return e;
        }
        if (maxBwBps != 0) {
                _transport->setMaxBandwidth(maxBwBps);
        }
        if (payloadSz > 0) {
                Error e = _transport->setPayloadSize(payloadSz);
                if (e.isError()) return e;
        }
        if (mode == SrtSocketTransport::Listener) {
                _transport->setAcceptTimeoutMs(static_cast<unsigned int>(acceptTo));
                SocketAddress local = buildAddress(localHost, static_cast<uint16_t>(localPort));
                if (local.isNull()) {
                        promekiErr("SrtMediaIO: Listener mode requires a parseable SrtLocalHost / SrtLocalPort");
                        return Error::InvalidArgument;
                }
                _transport->setLocalAddress(local);
        } else {
                SocketAddress peer = buildAddress(peerHost, static_cast<uint16_t>(peerPort));
                if (peer.isNull() || peer.port() == 0) {
                        promekiErr("SrtMediaIO: %s mode requires SrtPeerHost / SrtPeerPort",
                                   (mode == SrtSocketTransport::Caller) ? "Caller" : "Rendezvous");
                        return Error::InvalidArgument;
                }
                _transport->setPeerAddress(peer);
                if (mode == SrtSocketTransport::Rendezvous) {
                        SocketAddress local = buildAddress(localHost, static_cast<uint16_t>(localPort));
                        if (local.isNull() || local.port() == 0) {
                                promekiErr("SrtMediaIO: Rendezvous mode requires SrtLocalHost / SrtLocalPort");
                                return Error::InvalidArgument;
                        }
                        _transport->setLocalAddress(local);
                } else if (localPort != 0 || !localHost.isEmpty()) {
                        // Caller may optionally bind a specific source address.
                        SocketAddress local = buildAddress(localHost, static_cast<uint16_t>(localPort));
                        if (!local.isNull()) _transport->setLocalAddress(local);
                }
        }
        Error e = _transport->open();
        if (e.isError()) {
                promekiErr("SrtMediaIO: transport open failed (%s)", e.name().cstr());
                _transport.reset();
                return e;
        }
        return Error::Ok;
}

Error SrtMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const Enum modeEnum = cmd.config.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        _isWrite = (modeEnum == MediaIOOpenMode::Write);
        _eof = false;
        _readQueue.clear();
        _packetsWritten = _bytesWritten = _messagesWritten = _framesWritten = 0;
        _packetsRead = _bytesRead = _messagesRead = _framesRead = 0;
        _writeBufFill = 0;

        // Pacing setup — sink-only.  Defaults match RTMP / RTP:
        // Internal mode binds a fresh wall clock; External waits for
        // executeCmd(SetClock); None never paces.
        _paceClockIsExternal = false;
        _videoPaceGate.setClock(Clock::Ptr());
        _videoPaceGate.setPeriod(Duration::zero());
        if (_isWrite) {
                const Enum pacingEnum = cmd.config.get(MediaConfig::SrtVideoPacing)
                                                .asEnum(SrtVideoPacing::Type);
                _videoPacingMode = SrtVideoPacing(pacingEnum.value());
                _paceSkipThresholdMs = cmd.config.getAs<int32_t>(MediaConfig::SrtPaceSkipThresholdMs, 0);
                _paceReanchorThresholdMs =
                        cmd.config.getAs<int32_t>(MediaConfig::SrtPaceReanchorThresholdMs, 0);
                _frameRate = cmd.pendingMediaDesc.frameRate();
                if (!_frameRate.isValid()) {
                        _frameRate = cmd.config.getAs<FrameRate>(MediaConfig::FrameRate,
                                                                 FrameRate(FrameRate::FPS_30));
                }
        }

        applyFramerConfig(cmd.config);
        Error e = openTransport(cmd.config);
        if (e.isError()) return e;
        if (_isWrite) armVideoPaceGate();
        return _isWrite ? openSink(cmd) : openSource(cmd);
}

Error SrtMediaIO::openSink(const MediaIOCommandOpen &cmd) {
        _writeBuf = Buffer(_writePayloadSize);
        if (!_writeBuf.isValid()) return Error::NoMem;
        _writeBufFill = 0;

        FrameRate fps = cmd.pendingMediaDesc.frameRate();
        if (!fps.isValid()) fps = cmd.config.getAs<FrameRate>(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        if (!fps.isValid()) fps = FrameRate(FrameRate::FPS_30);
        _framer->setWriterFrameRate(fps);

        Error e = _framer->configureStreams(cmd.pendingMediaDesc);
        if (e.isError()) return e;

        // Default to H.264 video when nothing came through pendingMediaDesc.
        if (!_framer->haveVideoStream() && !_framer->haveAudioStream()) {
                Error pe = _framer->muxer()->addStream(_framer->videoPid(), MpegTs::StreamTypeH264);
                if (pe.isError() && pe != Error::Exists) return pe;
        }

        MediaIOPortGroup *group = addPortGroup("srt");
        if (group == nullptr) {
                promekiWarn("SrtMediaIO: addPortGroup failed");
                return Error::Invalid;
        }
        group->setFrameRate(fps);
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (addSink(group, cmd.pendingMediaDesc) == nullptr) {
                promekiWarn("SrtMediaIO: addSink failed");
                return Error::Invalid;
        }
        return Error::Ok;
}

Error SrtMediaIO::openSource(const MediaIOCommandOpen &cmd) {
        _readBuf = Buffer(kDefaultReadBufBytes);
        if (!_readBuf.isValid()) return Error::NoMem;

        FrameRate fps = cmd.config.getAs<FrameRate>(MediaConfig::FrameRate, FrameRate());
        if (!fps.isValid()) fps = FrameRate(FrameRate::FPS_30);

        MediaDesc desc;
        desc.setFrameRate(fps);

        MediaIOPortGroup *group = addPortGroup("srt");
        if (group == nullptr) {
                promekiWarn("SrtMediaIO: addPortGroup failed");
                return Error::Invalid;
        }
        group->setFrameRate(fps);
        group->setCanSeek(false);
        group->setFrameCount(FrameCount::unknown());
        if (addSource(group, desc) == nullptr) {
                promekiWarn("SrtMediaIO: addSource failed");
                return Error::Invalid;
        }

        _framer->setFrameCallback([this](Frame &&f) -> Error {
                _framesRead++;
                _readQueue.pushToBack(std::move(f));
                return Error::Ok;
        });
        return Error::Ok;
}

Error SrtMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        // Flush any tail-end TS bytes the sink accumulated before
        // tearing the SRT socket down; otherwise the receiver loses
        // the final partial chunk.
        if (_isWrite && _transport && _transport->isOpen()) {
                (void)flushWriteBuffer();
        }
        if (_framer.isValid()) {
                (void)_framer->flushReader();
                _framer.reset();
        }
        if (_transport) {
                _transport->close();
                _transport.reset();
        }
        _videoPaceGate.setClock(Clock::Ptr());
        _videoPaceGate.setPeriod(Duration::zero());
        _paceClockIsExternal = false;
        _readQueue.clear();
        _writeBuf = Buffer();
        _readBuf = Buffer();
        _writeBufFill = 0;
        _eof = false;
        return Error::Ok;
}

Error SrtMediaIO::flushWriteBuffer() {
        if (_writeBufFill == 0) return Error::Ok;
        if (!_transport || !_transport->isOpen()) return Error::NotOpen;
        const ssize_t n = _transport->sendPacket(_writeBuf.data(), _writeBufFill, SocketAddress());
        if (n < 0 || static_cast<size_t>(n) != _writeBufFill) {
                const String srtErr =
                        _transport->socket() ? _transport->socket()->lastSrtError() : String();
                promekiErr("SrtMediaIO: SRT send failed (req=%zu got=%lld): %s",
                           _writeBufFill, static_cast<long long>(n), srtErr.cstr());
                _writeBufFill = 0;
                return Error::IOError;
        }
        _bytesWritten += static_cast<int64_t>(_writeBufFill);
        _packetsWritten += static_cast<int64_t>(_writeBufFill / MpegTs::PacketSize);
        _messagesWritten++;
        _writeBufFill = 0;
        return Error::Ok;
}

Error SrtMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (!cmd.frame.isValid()) return Error::InvalidArgument;
        if (!_transport || !_transport->isOpen()) return Error::NotOpen;
        if (!_framer.isValid()) return Error::Invalid;

        // Wall-clock pacing gate — when the source isn't paced
        // (TPG / file replay), this enforces frame-rate cadence so
        // SRT's send buffer doesn't fill in milliseconds.  Skip
        // verdict drops the frame to bound the lag; everything else
        // ships.  Matches the RtmpMediaIO / RtpMediaIO pattern.
        //
        // Audio encoders emit a separate "audio-only" Frame per extra
        // AAC packet when the AAC frame size doesn't divide the input's
        // per-frame PCM count (1.56 AAC frames per video frame at
        // 29.97 fps / 48 kHz).  Pacing those at video-frame cadence
        // would throttle the audio rate to ~64% of real-time, which the
        // receiver perceives as a stream that runs slow / audio that
        // arrives too far ahead of PCR.  Only pace Frames that carry
        // an actual video AU; audio-only echoes ship immediately.
        const bool frameHasVideo = !cmd.frame.videoPayloads().isEmpty();
        if (frameHasVideo && !paceVideoFrame()) {
                _framesWritten++;
                cmd.currentFrame = toFrameNumber(_framesWritten);
                cmd.frameCount = _framesWritten;
                return Error::Ok;
        }

        auto emit = [this](const BufferView &v) -> Error {
                if (!v.isValid() || v.size() == 0) return Error::Ok;
                const uint8_t *src = static_cast<const uint8_t *>(v.data());
                size_t off = 0;
                while (off < v.size()) {
                        const size_t avail = _writePayloadSize - _writeBufFill;
                        const size_t copy = (avail < v.size() - off) ? avail : (v.size() - off);
                        std::memcpy(static_cast<uint8_t *>(_writeBuf.data()) + _writeBufFill,
                                    src + off, copy);
                        _writeBufFill += copy;
                        off += copy;
                        if (_writeBufFill == _writePayloadSize) {
                                Error e = flushWriteBuffer();
                                if (e.isError()) return e;
                        }
                }
                return Error::Ok;
        };

        Error err = _framer->writeFrame(cmd.frame, emit);
        if (err.isError()) return err;

        _framesWritten++;
        cmd.currentFrame = toFrameNumber(_framesWritten);
        cmd.frameCount = _framesWritten;
        return Error::Ok;
}

Error SrtMediaIO::pumpReader() {
        if (!_transport || !_transport->isOpen()) return Error::NotOpen;
        if (_eof) return Error::Ok;
        if (!_framer.isValid()) return Error::Invalid;
        const ssize_t n = _transport->receivePacket(_readBuf.data(),
                                                    _readBuf.allocSize(), nullptr);
        if (n == 0) {
                // Peer closed gracefully.
                _eof = true;
                (void)_framer->flushReader();
                return Error::Ok;
        }
        if (n < 0) {
                // Either real error or a broken connection.  Treat
                // both as EOF — the framer has whatever's in flight,
                // the consumer will surface NoData / EndOfFile.
                _eof = true;
                (void)_framer->flushReader();
                return Error::Ok;
        }
        _bytesRead += static_cast<int64_t>(n);
        _packetsRead += static_cast<int64_t>(n / MpegTs::PacketSize);
        _messagesRead++;
        BufferView view(_readBuf, 0, static_cast<size_t>(n));
        return _framer->pushBytes(view);
}

Error SrtMediaIO::executeCmd(MediaIOCommandRead &cmd) {
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

Error SrtMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsPacketsWritten, _packetsWritten);
        cmd.stats.set(StatsBytesWritten, _bytesWritten);
        cmd.stats.set(StatsMessagesWritten, _messagesWritten);
        cmd.stats.set(StatsFramesWritten, _framesWritten);
        cmd.stats.set(StatsPacketsRead, _packetsRead);
        cmd.stats.set(StatsBytesRead, _bytesRead);
        cmd.stats.set(StatsMessagesRead, _messagesRead);
        cmd.stats.set(StatsFramesRead, _framesRead);
        if (_framer.isValid() && _framer->demuxer() != nullptr) {
                cmd.stats.set(StatsContinuityErrors,
                              static_cast<int64_t>(_framer->demuxer()->continuityErrors()));
                cmd.stats.set(StatsBytesDiscarded,
                              static_cast<int64_t>(_framer->demuxer()->bytesDiscarded()));
        }
        if (_transport && _transport->isOpen()) {
                const SrtSocket::Stats s = _transport->stats(false);
                cmd.stats.set(StatsRttUs, static_cast<int64_t>(s.rttMs * 1000.0));
                cmd.stats.set(StatsLinkBandwidthBps, static_cast<int64_t>(s.linkBandwidthMbps * 1e6));
                cmd.stats.set(StatsRcvDrops, static_cast<int64_t>(s.pktRcvDrop));
                cmd.stats.set(StatsRetransmitted, static_cast<int64_t>(s.pktRetransmitted));
        }
        return Error::Ok;
}

Error SrtMediaIO::describe(MediaIODescription *out) const {
        if (out == nullptr) return Error::Invalid;
        Error baseErr = MediaIO::describe(out);
        if (baseErr.isError()) return baseErr;
        // Advertise the cross product of supported video × audio
        // codecs so the planner's orthogonal-axes pass sees both
        // gaps on the sink side (a video-only entry would make the
        // audio leg look "missing" and skip the chain).  PCMI_S16LE
        // stands in for "uncompressed audio" — the framer packs it
        // as SMPTE 302M on the way out.  JPEG_XS_YUV10_422_Rec709
        // stands in for "JPEG XS" — every JPEG_XS_* variant maps to
        // the same MPEG-TS stream_type / registration descriptor.
        auto advertise = [&out](PixelFormat::ID pid, AudioFormat::ID aid) {
                MediaDesc d;
                d.imageList().pushToBack(ImageDesc(Size2Du32(0, 0), PixelFormat(pid)));
                d.audioList().pushToBack(AudioDesc(AudioFormat(aid), 48000.0f, 2u));
                out->acceptableFormats().pushToBack(d);
        };
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

Error SrtMediaIO::executeCmd(MediaIOCommandSetClock &cmd) {
        // RX timing is driven by network arrival; the user can't
        // meaningfully replace it.  Mirrors the RtmpMediaIO contract.
        if (!_isWrite) return Error::NotSupported;

        if (cmd.clock.isValid()) {
                // Honour the externally-supplied clock regardless of
                // configured mode — capture-card upstreams typically
                // arrive after Open via the planner forwarding the
                // port-group clock.
                _videoPaceGate.setClock(cmd.clock);
                _paceClockIsExternal = true;
                if (_frameRate.isValid()) {
                        _videoPaceGate.setPeriod(_frameRate.frameDuration());
                }
        } else {
                _paceClockIsExternal = false;
                if (_videoPacingMode == SrtVideoPacing::Internal) {
                        armVideoPaceGate();
                } else {
                        _videoPaceGate.setClock(Clock::Ptr());
                }
        }
        return Error::Ok;
}

void SrtMediaIO::armVideoPaceGate() {
        _paceClockIsExternal = false;
        if (_videoPacingMode == SrtVideoPacing::Internal) {
                _videoPaceGate.setClock(Clock::Ptr::takeOwnership(new WallClock()));
        } else {
                _videoPaceGate.setClock(Clock::Ptr());
        }
        if (_frameRate.isValid()) {
                _videoPaceGate.setPeriod(_frameRate.frameDuration());
        }
        if (_paceSkipThresholdMs > 0) {
                _videoPaceGate.setSkipThreshold(Duration::fromMilliseconds(_paceSkipThresholdMs));
        }
        if (_paceReanchorThresholdMs > 0) {
                _videoPaceGate.setReanchorThreshold(Duration::fromMilliseconds(_paceReanchorThresholdMs));
        }
}

bool SrtMediaIO::paceVideoFrame() {
        if (!_videoPaceGate.hasClock()) return true;
        if (!_frameRate.isValid()) return true;
        _videoPaceGate.setPeriod(_frameRate.frameDuration());
        PacingResult pr = _videoPaceGate.wait();
        if (pr.error.isError()) {
                promekiErr("SrtMediaIO: video pacing clock failure: %s",
                           pr.error.name().cstr());
                return true;
        }
        switch (pr.verdict) {
                case PacingVerdict::Skip:
                        return false;
                case PacingVerdict::Reanchor:
                        promekiWarn("SrtMediaIO: video pacing re-anchored after %s lag",
                                    pr.slack.toString().cstr());
                        return true;
                case PacingVerdict::OnTime:
                case PacingVerdict::Late:
                        return true;
        }
        return true;
}

Error SrtMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;

        const Enum modeEnum = config().get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum == MediaIOOpenMode::Write;
        if (!isWrite) {
                // Reader-mode: just echo what was offered.  The
                // route-planner uses proposeOutput for sources.
                *preferred = offered;
                return Error::Ok;
        }

        MediaDesc out = offered;

        // Video: rewrite the first image's PixelFormat to the
        // compressed representative of the configured SrtVideoCodec
        // when the offered shape isn't already compressed.  Same
        // pattern as RtmpMediaIO::proposeInput — gives the planner's
        // VideoEncoder bridge a clean uncompressed→compressed gap
        // to splice into.
        if (!out.imageList().isEmpty()) {
                const PixelFormat &pd = out.imageList()[0].pixelFormat();
                if (pd.isValid() && !pd.isCompressed()) {
                        VideoCodec vc =
                                config().getAs<VideoCodec>(MediaConfig::SrtVideoCodec,
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

        // Audio: rewrite the first audio descriptor's AudioFormat to
        // the compressed representative of the configured
        // SrtAudioCodec.  PCM is special-cased — no encoder needed,
        // the framer packs it as SMPTE 302M — but the offered PCM
        // format must already match one of 302M's supported layouts
        // (16/20/24-bit interleaved S, 48 kHz, 2/4/6/8 ch).  When the
        // source offers something incompatible (Float32, planar, etc.)
        // rewrite to PCMI_S16LE @ 48 kHz so the planner splices an
        // SRC ahead of the sink.
        if (!out.audioList().isEmpty()) {
                const AudioFormat &af = out.audioList()[0].format();
                if (af.isValid() && !af.isCompressed()) {
                        AudioCodec ac =
                                config().getAs<AudioCodec>(MediaConfig::SrtAudioCodec,
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
