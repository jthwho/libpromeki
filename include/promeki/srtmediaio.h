/**
 * @file      srtmediaio.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_SRT
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/buffer.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/list.h>
#include <promeki/mediaiofactory.h>
#include <promeki/enums_mediaio.h>
#include <promeki/framerate.h>
#include <promeki/pacinggate.h>
#include <promeki/sharedthreadmediaio.h>
#include <promeki/socketaddress.h>
#include <promeki/srtsockettransport.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>
#include <promeki/url.h>

PROMEKI_NAMESPACE_BEGIN

class MpegTsFramer;

/**
 * @brief Bidirectional MediaIO backend that sends and receives a
 *        single-program MPEG-TS stream over a Secure Reliable
 *        Transport (SRT) connection.
 * @ingroup proav
 *
 * @c SrtMediaIO is the network-shaped sibling of @ref MpegTsFileMediaIO.
 * Both compose the same @ref MpegTsFramer — they only differ in the
 * transport that moves the framed TS bytes:
 *
 *  - @ref MpegTsFileMediaIO writes / reads a local @ref File.
 *  - @c SrtMediaIO writes / reads through an @ref SrtSocketTransport.
 *
 * @par Mode support
 *
 * - @c Sink — accepts @ref Frame objects carrying one
 *   @ref CompressedVideoPayload (H.264 or HEVC) and any number of
 *   @ref CompressedAudioPayload entries.  Uncompressed inputs are
 *   rejected so the pipeline planner inserts an upstream encoder.
 * - @c Source — reads SRT messages, feeds the bytes to the framer,
 *   and emits each reassembled access unit as a one-payload Frame.
 *
 * @par Connection roles
 *
 * @ref SrtMode is part of the @ref MediaConfig surface (see config
 * keys below):
 *
 * - @c Caller — actively dials @c SrtPeerHost / @c SrtPeerPort.
 *   @ref open returns once the handshake completes.
 * - @c Listener — binds @c SrtLocalHost / @c SrtLocalPort, listens,
 *   and blocks the open call until exactly one peer connects (or the
 *   @c SrtAcceptTimeoutMs elapses).
 * - @c Rendezvous — binds locally and dials @c SrtPeerHost
 *   simultaneously; both endpoints meet in the middle.
 *
 * @par Config keys
 *
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::OpenMode | Enum | @c Read | @c Read = source, @c Write = sink. |
 * | @ref MediaConfig::FrameRate | FrameRate | 30/1 | Used to synthesise PTS when payloads don't carry one. |
 * | @ref MediaConfig::SrtMode | Enum | @c Caller | Caller / Listener / Rendezvous. |
 * | @ref MediaConfig::SrtPeerHost | String | (required for Caller / Rendezvous) | Peer host. |
 * | @ref MediaConfig::SrtPeerPort | int | 0 | Peer port. |
 * | @ref MediaConfig::SrtLocalHost | String | "" (any) | Local bind host. |
 * | @ref MediaConfig::SrtLocalPort | int | 0 | Local bind port. |
 * | @ref MediaConfig::SrtLatencyMs | int | 120 | Symmetric latency. |
 * | @ref MediaConfig::SrtPassphrase | String | "" | AES passphrase. |
 * | @ref MediaConfig::SrtEncryptionKeyLength | int | 0 | 0/16/24/32 byte AES key. |
 * | @ref MediaConfig::SrtStreamId | String | "" | SRTO_STREAMID. |
 * | @ref MediaConfig::SrtMaxBandwidthBps | int64 | 0 | SRTO_MAXBW. |
 * | @ref MediaConfig::SrtPayloadSize | int | 1316 | Live-mode payload size. |
 * | @ref MediaConfig::SrtAcceptTimeoutMs | int | 0 | Listener accept timeout. |
 * | @ref MediaConfig::MpegTsVideoPid | int | 0x100 | Video PID. |
 * | @ref MediaConfig::MpegTsAudioPid | int | 0x101 | Audio PID. |
 * | @ref MediaConfig::MpegTsPmtPid   | int | 0x1000 | PMT PID. |
 * | @ref MediaConfig::MpegTsProgramNumber | int | 1 | program_number. |
 * | @ref MediaConfig::MpegTsPatPmtIntervalMs | int | 100 | PAT/PMT emit cadence. |
 * | @ref MediaConfig::MpegTsPcrIntervalMs | int | 20 | PCR insertion cadence. |
 * | @ref MediaConfig::MpegTsMuxRateBps | int64 | 0 | CBR target (0 = disabled). |
 * | @ref MediaConfig::MpegTsAacFraming | Enum | Adts | ADTS or LATM. |
 *
 * @par Live-mode chunking
 *
 * SRT live mode treats each @c srt_send call as one message; the
 * default @ref MediaConfig::SrtPayloadSize is 1316 bytes (7 × 188 byte
 * TS packets, the canonical IPv4 / UDP-friendly MTU).  The sink
 * accumulates muxer-emitted TS bytes into a payload-sized scratch
 * buffer and flushes whenever the buffer is full, so on-wire messages
 * are always integer-multiples of 188 bytes (up to the configured
 * payload size).  Any final partial chunk is flushed on close.
 *
 * @par Thread Safety
 * Strand-affine — see @ref CommandMediaIO.
 *
 * @par Example (caller)
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "Srt");
 * cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
 * cfg.set(MediaConfig::SrtMode, SrtMode(SrtMode::Caller));
 * cfg.set(MediaConfig::SrtPeerHost, "127.0.0.1");
 * cfg.set(MediaConfig::SrtPeerPort, int32_t(4200));
 * cfg.set(MediaConfig::SrtLatencyMs, int32_t(120));
 * cfg.set(MediaConfig::SrtPassphrase, "very-secret-key");
 * MediaIO *sink = MediaIO::create(cfg);
 * sink->open(MediaIO::Sink);
 * @endcode
 */
class SrtMediaIO : public SharedThreadMediaIO {
                PROMEKI_OBJECT(SrtMediaIO, SharedThreadMediaIO)
        public:
                /** @brief int64_t — TS packets sent across all PIDs. */
                static inline const MediaIOStats::ID StatsPacketsWritten{"PacketsWritten"};

                /** @brief int64_t — payload bytes sent through SRT. */
                static inline const MediaIOStats::ID StatsBytesWritten{"BytesWritten"};

                /** @brief int64_t — SRT messages sent. */
                static inline const MediaIOStats::ID StatsMessagesWritten{"MessagesWritten"};

                /** @brief int64_t — Frames muxed by the framer. */
                static inline const MediaIOStats::ID StatsFramesWritten{"FramesWritten"};

                /** @brief int64_t — TS packets received. */
                static inline const MediaIOStats::ID StatsPacketsRead{"PacketsRead"};

                /** @brief int64_t — payload bytes received from SRT. */
                static inline const MediaIOStats::ID StatsBytesRead{"BytesRead"};

                /** @brief int64_t — SRT messages received. */
                static inline const MediaIOStats::ID StatsMessagesRead{"MessagesRead"};

                /** @brief int64_t — Frames emitted by the framer. */
                static inline const MediaIOStats::ID StatsFramesRead{"FramesRead"};

                /** @brief int64_t — continuity-counter discontinuities seen by the demuxer. */
                static inline const MediaIOStats::ID StatsContinuityErrors{"ContinuityErrors"};

                /** @brief int64_t — bytes the demuxer dropped while searching for a sync byte. */
                static inline const MediaIOStats::ID StatsBytesDiscarded{"BytesDiscarded"};

                /** @brief int64_t — SRT round-trip time, in microseconds. */
                static inline const MediaIOStats::ID StatsRttUs{"SrtRttUs"};

                /** @brief int64_t — SRT estimated link bandwidth, in bits/sec. */
                static inline const MediaIOStats::ID StatsLinkBandwidthBps{"SrtLinkBandwidthBps"};

                /** @brief int64_t — SRT receiver-side TSBPD drops. */
                static inline const MediaIOStats::ID StatsRcvDrops{"SrtRcvDrops"};

                /** @brief int64_t — SRT sender-side retransmissions. */
                static inline const MediaIOStats::ID StatsRetransmitted{"SrtRetransmitted"};

                SrtMediaIO(ObjectBase *parent = nullptr);
                ~SrtMediaIO() override;

                Error describe(MediaIODescription *out) const override;
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                Error executeCmd(MediaIOCommandSetClock &cmd) override;

        private:
                Error openSink(const MediaIOCommandOpen &cmd);
                Error openSource(const MediaIOCommandOpen &cmd);
                Error openTransport(const MediaIO::Config &cfg);
                Error flushWriteBuffer();
                Error pumpReader();
                void  applyFramerConfig(const MediaIO::Config &cfg);

                /** @brief Arms (or re-arms) @ref _videoPaceGate per the configured mode. */
                void armVideoPaceGate();

                /** @brief Blocks the strand until the next frame's tick is due.
                 *  Returns @c true to send, @c false to drop (lag past skip threshold). */
                bool paceVideoFrame();

                bool _isWrite = false;
                bool _eof = false;

                UniquePtr<SrtSocketTransport> _transport;
                UniquePtr<MpegTsFramer>       _framer;
                Frame::List                   _readQueue;

                // Writer-side accumulation buffer.  We collect TS bytes
                // from the muxer and only flush whole SRT messages
                // (configurable, default 1316 bytes = 7 × 188).
                Buffer _writeBuf;
                size_t _writeBufFill = 0;
                size_t _writePayloadSize = 1316;

                // Reader-side scratch buffer for one SRT message.
                Buffer _readBuf;

                int64_t _packetsWritten = 0;
                int64_t _bytesWritten = 0;
                int64_t _messagesWritten = 0;
                int64_t _framesWritten = 0;
                int64_t _packetsRead = 0;
                int64_t _bytesRead = 0;
                int64_t _messagesRead = 0;
                int64_t _framesRead = 0;

                // Sink-side wall-clock pacing.  Mirrors RtmpMediaIO /
                // RtpMediaIO's pattern so live-mode transport sinks
                // gate upstream-produced frames at frame-rate cadence
                // even when no upstream stage is paced.
                FrameRate      _frameRate;
                PacingGate     _videoPaceGate;
                SrtVideoPacing _videoPacingMode{SrtVideoPacing::Internal};
                int            _paceSkipThresholdMs = 0;
                int            _paceReanchorThresholdMs = 0;
                bool           _paceClockIsExternal = false;
};

/**
 * @brief @ref MediaIOFactory for the SRT MediaIO backend.
 * @ingroup proav
 *
 * Registers the @c "srt" URL scheme so users can call
 * @ref MediaIO::createFromUrl with a Haivision-style URL:
 *
 * @code
 * srt://host:port?mode=caller&latency=120&passphrase=secret
 * srt://0.0.0.0:4200?mode=listener&streamid=publish/cam1
 * @endcode
 *
 * Recognised query parameters (case-insensitive keys, common ffmpeg
 * spelling):
 *
 * | Query key                        | MediaConfig target |
 * |----------------------------------|--------------------|
 * | @c mode                          | @ref MediaConfig::SrtMode |
 * | @c latency, @c rcvlatency, @c peerlatency | @ref MediaConfig::SrtLatencyMs |
 * | @c passphrase                    | @ref MediaConfig::SrtPassphrase |
 * | @c pbkeylen                      | @ref MediaConfig::SrtEncryptionKeyLength |
 * | @c streamid                      | @ref MediaConfig::SrtStreamId |
 * | @c maxbw                         | @ref MediaConfig::SrtMaxBandwidthBps |
 * | @c payloadsize, @c payload_size, @c pkt_size | @ref MediaConfig::SrtPayloadSize |
 * | @c adapter                       | @ref MediaConfig::SrtLocalHost |
 * | @c localport                     | @ref MediaConfig::SrtLocalPort |
 * | @c timeout, @c listen_timeout    | @ref MediaConfig::SrtAcceptTimeoutMs |
 *
 * The URL's authority (@c host:port) is the @b peer address in
 * @c Caller and @c Rendezvous modes and the @b local bind address in
 * @c Listener mode.  Any remaining query keys are passed through to
 * the generic @ref MediaIO::applyQueryToConfig path, so the canonical
 * long-form MediaConfig key names (e.g. @c SrtPayloadSize) also work
 * directly.
 */
class SrtFactory : public MediaIOFactory {
        public:
                SrtFactory() = default;

                String name() const override { return String("Srt"); }
                String displayName() const override { return String("SRT (MPEG-TS over SRT)"); }
                String description() const override {
                        return String("MPEG-2 Transport Stream over Secure Reliable Transport "
                                      "(H.264 / HEVC + AAC).");
                }
                bool canBeSink() const override { return true; }
                bool canBeSource() const override { return true; }

                StringList      schemes() const override { return {String("srt")}; }
                Config::SpecMap configSpecs() const override;
                Error           urlToConfig(const Url &url, Config *outConfig) const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_SRT
