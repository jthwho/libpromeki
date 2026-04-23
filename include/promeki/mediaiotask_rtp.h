/**
 * @file      mediaiotask_rtp.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaiotask.h>
#include <promeki/queue.h>
#include <promeki/rtppacket.h>
#include <promeki/sdpsession.h>
#include <promeki/socketaddress.h>
#include <promeki/pixelformat.h>
#include <promeki/audiodesc.h>
#include <promeki/audiobuffer.h>
#include <promeki/histogram.h>
#include <promeki/mutex.h>
#include <promeki/waitcondition.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/clockdomain.h>
#include <promeki/eui64.h>

PROMEKI_NAMESPACE_BEGIN

class RtpSession;
class RtpPayload;
class UdpSocketTransport;
class Thread;

/**
 * @brief MediaIOTask backend that transmits frames as RTP streams.
 * @ingroup proav
 *
 * MediaIOTask_Rtp is a unified video + audio + metadata RTP sink.
 * One MediaIO instance carries one SDP session with up to three
 * @c m= sections (video / audio / data), each backed by its own
 * @ref RtpSession, @ref RtpPayload, and @ref UdpSocketTransport so
 * per-stream DSCP, SSRC, and destination can be set independently.
 * This factoring mirrors how SMPTE ST 2110 and AES67 deployments
 * group streams, and it lets the sink advertise a single SDP file
 * that a downstream receiver can consume as one bundle.
 *
 * @par Supported streams (writer mode)
 *
 * - **Video** — MJPEG (RFC 2435) via @ref RtpPayloadJpeg when the
 *   input @ref PixelFormat is in the JPEG family, or RFC 4175 raw
 *   video via @ref RtpPayloadRawVideo for 8-bit interleaved
 *   uncompressed formats (first pass; proper ST 2110-20 pgroup
 *   sizing for 10/12-bit lands later).  @b Pick @b the @b right
 *   @b JPEG @b sub-format: JFIF / RFC 2435 has no standard way to
 *   signal YCbCr matrix (Rec.601 vs Rec.709) or range (limited
 *   vs full) inside the bitstream, so a strict JFIF consumer
 *   like ffplay always decodes with Rec.601 full-range math.
 *   For correct playback in ffplay / browsers / any libjpeg-based
 *   receiver, use @c PixelFormat::JPEG_YUV8_422_Rec601_Full or
 *   @c PixelFormat::JPEG_YUV8_420_Rec601_Full.  Broadcast / SDI /
 *   ST 2110 pipelines that expect limited-range Rec.709 JPEG
 *   should use the unsuffixed @c JPEG_YUV8_422_Rec709 variant
 *   (the library-wide YCbCr default).  See the
 *   @ref pixelformat.h enum documentation for the full 2 × 2 × 2
 *   matrix × range × subsampling grid.
 * - **Audio** — L16 (RFC 3551 / AES67) via @ref RtpPayloadL16.
 *   Input can arrive in any PCM data type that @ref AudioBuffer
 *   can convert to @c PCMI_S16BE (signed 16 / float32 / float64 at
 *   any endian, etc.); the backend holds a FIFO in network-order
 *   wire format, drains AES67-sized packets out of it, and stamps
 *   each packet with a monotonic per-sample RTP timestamp so the
 *   audio clock never drifts across writeFrame boundaries or
 *   fractional video frame rates.  Sample rate and channel count
 *   must match between input and output — upstream rate / layout
 *   conversion belongs in @ref MediaIOTask_CSC.  L24 and
 *   ST 2110-30 pgroup handling are deferred to a follow-up.
 * - **Data** — per-frame @ref Metadata as JSON via
 *   @ref RtpPayloadJson (default), with
 *   @ref MetadataRtpFormat::St2110_40 reserved for the future
 *   SMPTE ST 2110-40 Ancillary Data payload class.
 *
 * A stream is transmitted only when its destination is set (e.g.
 * @ref MediaConfig::VideoRtpDestination is non-null and valid).
 * An empty destination disables that stream for the session, so a
 * video-only or audio-only instance is expressed by omitting the
 * other destination keys.
 *
 * @par Mode support
 *
 * Both @c MediaIO::Sink and @c MediaIO::Source are supported.
 * In reader mode, each configured stream opens its own
 * @ref UdpSocketTransport bound to the port in the corresponding
 * @c *RtpDestination key, joins the multicast group if the
 * destination is a group address, and runs an @ref RtpSession
 * receive thread that hands packets to a per-stream reassembler.
 * The reassembler emits completed @ref Frame objects into a shared
 * thread-safe output queue that @c executeCmd(MediaIOCommandRead)
 * drains with a bounded timeout.  @c ReadWrite is explicitly
 * rejected — an RTP sink and an RTP source are conceptually
 * different streams and should not share a MediaIO.
 *
 * @par Reader auto-config via SDP
 *
 * When @ref MediaConfig::RtpSdp is set, the reader parses
 * the SDP file at open time and uses the @c m= / @c a=rtpmap /
 * @c a=fmtp lines to populate stream destinations, payload types,
 * clock rates, and payload handlers — so a receiver configured
 * from an SDP written by an RTP sink (@ref MediaConfig::RtpSaveSdpPath)
 * just works with no manual key mirroring.  Explicit
 * @c *RtpDestination keys still override the SDP-discovered values
 * if both are set.
 *
 * @par Reader frame emission
 *
 * Each stream emits @ref Frame instances independently: a video
 * stream pushes frames with only @c imageList populated; an audio
 * stream pushes frames with only @c audioList populated; a data
 * stream pushes frames with only @c metadata populated.  Consumers
 * that need synchronised video-plus-audio frames should drive a
 * downstream @ref MediaIOTask_CSC or their own aggregator
 * over the resulting interleaved stream.
 *
 * @par Pacing
 *
 * The @ref MediaConfig::RtpPacingMode key selects how packets are
 * spaced out inside a frame interval:
 *
 * | Mode       | Mechanism |
 * |------------|-----------|
 * | @c Auto      | Probe the best available mechanism at open time.  Resolves to @c KernelFq on Linux and @c Userspace elsewhere.  This is the default — explicit modes only need to be set when overriding. |
 * | @c None      | Burst all packets at once (loopback / LAN only). |
 * | @c Userspace | @ref RtpSession::sendPacketsPaced() — sleeps between sends. |
 * | @c KernelFq  | @ref RtpSession::setPacingRate() — @c SO_MAX_PACING_RATE via the @c fq qdisc (Linux default). |
 * | @c TxTime    | Per-packet @c SCM_TXTIME deadlines via the ETF qdisc (deferred; falls back to @c KernelFq). |
 *
 * The target rate for @c KernelFq is drawn from
 * @ref MediaConfig::VideoRtpTargetBitrate (if set) or computed
 * from the video descriptor for uncompressed inputs, and from
 * @c sampleRate @c × @c channels @c × @c bytesPerSample for audio.
 * Compressed video streams (JPEG / JPEG XS) without an explicit
 * @c VideoRtpTargetBitrate are paced per frame instead: every
 * call to @c sendVideo recomputes the rate cap from that frame's
 * actual packed byte count and sets it via @ref RtpSession::setPacingRate
 * before dispatching, so the kernel @c fq qdisc spaces a VBR
 * frame's bytes over exactly one frame interval without needing
 * to know the bitrate in advance.  The cap is always set to the
 * exact source rate (no headroom) so the socket's send buffer
 * provides backpressure that holds the writer in lockstep with
 * the wall-clock frame schedule — any headroom would let the
 * encoder outrun the wire and the receiver would see playback
 * time advance faster than realtime (notably visible with fast
 * codecs like JPEG XS where encoding can run many multiples
 * of frame rate).
 *
 * @note The kernel rate-cap approach is approximate per-packet —
 *       it controls *byte rate*, not per-packet deadlines.  For
 *       SMPTE ST 2110-21 / IPMX Type N or Type W senders that
 *       must place each packet at a specific scanline-aligned
 *       offset, the proper mechanism is per-packet @c SCM_TXTIME
 *       deadlines via the ETF qdisc — the deferred @c TxTime
 *       pacing mode.  The @ref UdpSocket transport already plumbs
 *       @c SCM_TXTIME via the @c Datagram::txTimeNs field, but
 *       wiring it up here is the next step on that path and is
 *       not done yet.
 *
 * @par Audio packet sizing
 *
 * @ref MediaConfig::AudioRtpPacketTimeUs selects the AES67 packet
 * interval in microseconds.  The default is 1000 µs (1 ms, 48
 * samples @ 48 kHz per channel).  Values that would push a packet
 * past the MTU (e.g. 4 ms at 7.1 surround) are clamped at open
 * time to the largest AES67 standard interval that fits (4000 /
 * 1000 / 333 / 250 / 125 µs), with a warning logged to describe
 * the fallback.  The packet boundary is always an integer number
 * of samples — partial packets stay in the FIFO until the next
 * writeFrame fills them in.
 *
 * @par SDP export
 *
 * At @ref executeCmd(MediaIOCommandOpen&) time the task builds an
 * @ref SdpSession describing every active stream and exposes it
 * via two paths:
 *
 *  - **Config key** — if @ref MediaConfig::RtpSaveSdpPath is set,
 *    the SDP text is written to that file before the first frame
 *    is transmitted.
 *  - **Parameterized command** — callers can issue a
 *    @c MediaIOCommandParams with @c name == "GetSdp" and read
 *    the SDP text from the result under the same key name.
 *
 * Both paths use the same internal builder so the SDP on disk and
 * the SDP returned from a live task are byte-identical.
 *
 * Every generated @c m= section carries an @c a=rtcp-mux attribute
 * (RFC 5761), for reasons covered in the next section.
 *
 * @par Port conventions — the "port + 2" rule
 *
 * RTP-over-UDP traditionally reserves **pairs** of ports per media
 * stream.  The even port @c N carries the RTP packets themselves;
 * the next odd port @c N+1 is the corresponding RTCP channel (see
 * RFC 3550 §11).  Receivers that see an SDP with @c m=video @c 5004
 * will, by default, bind **both** 5004 (RTP) and 5005 (RTCP) even
 * when the sender never emits RTCP, because the classic RTP stack
 * assumes RTCP is there.
 *
 * The practical consequence is that two streams sharing a single
 * SDP session must not place their @c m= ports within 1 of each
 * other, or the receiver's RTCP socket for one stream collides
 * with the RTP socket of the other.  The safe minimum stride is
 * **2** — i.e. video on 5004, audio on 5006, metadata on 5008.  On
 * ST 2110 grade equipment the stride is usually even larger (5004
 * / 5006 or 5004 / 5010 are both common) to leave room for future
 * RTCP + RTCP-XR traffic.
 *
 * The classic cautionary failure mode:
 *
 * @code
 * --oc VideoRtpDestination:127.0.0.1:10000
 * --oc AudioRtpDestination:127.0.0.1:10001  // ← collides with video RTCP port
 * @endcode
 *
 * @c ffplay and any other SDP consumer that honours the RTP+1 RTCP
 * convention will fail to bind the audio receive socket with
 * "Address already in use" because it already opened port 10001
 * for video's implicit RTCP channel.
 *
 * This backend works around the classic case by emitting
 * @c a=rtcp-mux on every media description, per RFC 5761.  That
 * tells the receiver "RTP and RTCP share the same port for this
 * stream" so no separate RTCP socket is ever opened.  Since the
 * backend does not transmit RTCP in the first place, the attribute
 * is a cosmetic declaration that has zero wire impact — it exists
 * purely to unblock the receiver's socket setup.
 *
 * **That workaround is enough for modern ffmpeg / ffplay / GStreamer
 * and anything else that honours RFC 5761.**  For interop with
 * older receivers that ignore @c rtcp-mux (notably some pre-4.x
 * ffmpeg builds and legacy hardware ST 2110 receivers), the safe
 * fallback is to leave a **≥ 2 port gap** between adjacent streams
 * yourself:
 *
 * @code
 * --oc VideoRtpDestination:127.0.0.1:5004
 * --oc AudioRtpDestination:127.0.0.1:5006   // +2 avoids any RTCP collision
 * --oc DataRtpDestination:127.0.0.1:5008    // +2 again for metadata
 * @endcode
 *
 * Using even-numbered ports that are multiples of 2 matches how
 * ST 2110 / AES67 deployments conventionally lay out ports and
 * works with every RTP receiver regardless of @c rtcp-mux support.
 *
 * @par Example
 * @code
 * // Build a media descriptor whose video format is strict JFIF
 * // (Rec.601 full-range JPEG).  This is the variant ffplay /
 * // browsers / libjpeg-turbo decode correctly: the bytes inside
 * // the JFIF bitstream use the BT.601 matrix and the full
 * // 0..255 range that those decoders assume.  Using
 * // JPEG_YUV8_422_Rec709 (limited-range Rec.709) would produce
 * // a JPEG that's valid SDI / ST 2110 broadcast material but
 * // renders with a crushed black level on a consumer player.
 * MediaDesc mediaDesc;
 * mediaDesc.setFrameRate(FrameRate(FrameRate::FPS_30));
 * mediaDesc.imageList().pushToBack(
 *         ImageDesc(Size2Du32(1920, 1080),
 *                   PixelFormat(PixelFormat::JPEG_YUV8_422_Rec601_Full)));
 * mediaDesc.audioList().pushToBack(
 *         AudioDesc(AudioFormat::PCMI_S16LE, 48000.0f, 2));
 *
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "Rtp");
 * cfg.set(MediaConfig::RtpSessionName, "TPG stream");
 * // Video on 5004, audio on 5006 — a +2 stride keeps the classic
 * // RTCP-on-next-port convention happy even for receivers that
 * // ignore the rtcp-mux attribute.
 * cfg.set(MediaConfig::VideoRtpDestination,
 *         SocketAddress(Ipv4Address(239, 0, 0, 1), 5004));
 * cfg.set(MediaConfig::AudioRtpDestination,
 *         SocketAddress(Ipv4Address(239, 0, 0, 1), 5006));
 * cfg.set(MediaConfig::RtpSaveSdpPath, String("/tmp/stream.sdp"));
 *
 * MediaIO *io = MediaIO::create(cfg);
 * io->setExpectedDesc(mediaDesc);
 * io->open(MediaIO::Sink);
 * io->writeFrame(frame);
 * io->close();
 * delete io;
 * @endcode
 */
class MediaIOTask_Rtp : public MediaIOTask {
        public:
                /** @brief int64_t — total frames transmitted. */
                static inline const MediaIOStats::ID StatsFramesSent{"FramesSent"};
                /** @brief int64_t — total RTP packets transmitted across all streams. */
                static inline const MediaIOStats::ID StatsPacketsSent{"PacketsSent"};
                /** @brief int64_t — total bytes transmitted across all streams. */
                static inline const MediaIOStats::ID StatsBytesSent{"BytesSent"};
                /** @brief int64_t — total frames received from the network. */
                static inline const MediaIOStats::ID StatsFramesReceived{"FramesReceived"};
                /** @brief int64_t — total RTP packets received across all streams. */
                static inline const MediaIOStats::ID StatsPacketsReceived{"PacketsReceived"};
                /** @brief int64_t — total bytes received across all streams. */
                static inline const MediaIOStats::ID StatsBytesReceived{"BytesReceived"};
                /** @brief String — pretty-printed video TX frame-interval histogram (us). */
                static inline const MediaIOStats::ID StatsTxVideoFrameIntervalUs{"TxVideoFrameIntervalUs"};
                /** @brief String — pretty-printed video TX send-duration histogram (us). */
                static inline const MediaIOStats::ID StatsTxVideoSendDurationUs{"TxVideoSendDurationUs"};
                /** @brief String — pretty-printed video RX packet-interval histogram (us). */
                static inline const MediaIOStats::ID StatsRxVideoPacketIntervalUs{"RxVideoPacketIntervalUs"};
                /** @brief String — pretty-printed video RX frame-interval histogram (us). */
                static inline const MediaIOStats::ID StatsRxVideoFrameIntervalUs{"RxVideoFrameIntervalUs"};
                /** @brief String — pretty-printed video RX frame-assemble-time histogram (us). */
                static inline const MediaIOStats::ID StatsRxVideoFrameAssembleUs{"RxVideoFrameAssembleUs"};

                /** @brief Params command name: return the SDP text in @c result["Sdp"]. */
                static inline const MediaIOParamsID ParamGetSdp{"GetSdp"};
                /** @brief Key used in @c GetSdp result. */
                static inline const MediaIOParamsID ParamSdp{"Sdp"};

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc describing the RTP backend.
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_Rtp. */
                MediaIOTask_Rtp();

                /** @brief Destructor. Closes any still-open streams. */
                ~MediaIOTask_Rtp() override;

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandParams &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

                // describe() / proposeInput overrides intentionally
                // omitted for v1: each RFC 9134 / RFC 2435 / RFC 4175
                // payload type has its own accepted-shape constraints
                // (e.g. JPEG payload = 8-bit YUV422 only; raw payload
                // = configurable subsampling / bit depth; L16 audio =
                // 16-bit BE PCM at fixed channel counts).  Encoding
                // those rules per-payload is its own follow-up — for
                // now the planner's open() fallback inspects the SDP
                // (when configured) to learn the live shape, and
                // bridges insert as needed via the configured payload
                // type's own runtime checks.

                /**
                 * @brief Per-stream RTP state.
                 *
                 * Groups the transport, session, and payload handler
                 * for one of the three stream kinds so the per-frame
                 * dispatch in @c executeCmd(MediaIOCommandWrite&) can
                 * iterate over active streams uniformly.
                 */
                /**
                 * @brief Work item dispatched to a per-stream SendThread.
                 */
                struct TxWorkItem {
                        std::function<Error()>  work;
                        Queue<Error>           *resultQueue;
                };

                class SendThread;

                struct Stream {
                        UdpSocketTransport *transport = nullptr;
                        RtpSession         *session   = nullptr;
                        RtpPayload         *payload   = nullptr;
                        SendThread         *txThread  = nullptr;
                        SocketAddress       destination;
                        uint8_t             payloadType = 0;
                        uint32_t            clockRate   = 90000;
                        int                 dscp        = 0;
                        uint32_t            ssrc        = 0;
                        int64_t             packetsSent = 0;
                        int64_t             bytesSent   = 0;
                        int64_t             packetsReceived = 0;
                        int64_t             bytesReceived   = 0;
                        FrameCount          framesReceived{0};
                        int64_t             packetsLost     = 0;
                        String              mediaType;    ///< @brief "video", "audio", "data"
                        String              rtpmap;       ///< @brief SDP a=rtpmap:... value
                        String              fmtp;         ///< @brief SDP a=fmtp:... value, optional
                        bool                active = false;
                        ClockDomain         clockDomain;       ///< @brief Clock domain derived from SDP ts-refclk.
                        EUI64               ptpGrandmaster;    ///< @brief PTP grandmaster ID from SDP ts-refclk.

                        // Reader-mode per-stream reassembly state.
                        ImageDesc           readerImageDesc;
                        AudioDesc           readerAudioDesc;
                        uint32_t            reasmTimestamp = 0;
                        bool                reasmHasTimestamp = false;
                        uint16_t            reasmLastSeq = 0;
                        bool                reasmHaveLastSeq = false;
                        bool                reasmSynced = false; ///< @brief True once a marker boundary has been seen.
                        RtpPacket::List     reasmPackets;

                        // Timing instrumentation.  Each histogram is
                        // updated only by the worker thread that owns
                        // the stream — the TX worker for writer mode,
                        // the per-session RX thread for reader mode —
                        // so they need no internal locking.  Stats
                        // queries read them via @c executeCmd
                        // (MediaIOCommandStats&) and serialise the
                        // toString() form into the @ref MediaIOStats
                        // payload.  All durations are tracked in
                        // microseconds for compactness; nanoseconds
                        // would push past the bucket layout's useful
                        // range for the long tail.
                        Histogram           txFrameInterval;     ///< @brief us between sendVideo entries
                        Histogram           txSendDuration;      ///< @brief us spent inside sendVideo
                        Histogram           rxPacketInterval;    ///< @brief us between received packets
                        Histogram           rxFrameInterval;     ///< @brief us between completed frames
                        Histogram           rxFrameAssembleTime; ///< @brief us first packet -> marker
                        TimeStamp           txLastSendStart;     ///< @brief Last entry into sendVideo (TX worker only)
                        TimeStamp           rxLastPacketTime;    ///< @brief Last received packet time (RX thread only)
                        TimeStamp           rxLastFrameTime;     ///< @brief Last emitted frame time (RX thread only)
                        TimeStamp           rxFrameStartTime;    ///< @brief First packet of current reassembly
                        bool                txHasLastSend       = false;
                        bool                rxHasLastPacket     = false;
                        bool                rxHasLastFrame      = false;
                        bool                rxHasFrameStart     = false;
                };

                Error configureVideoStream(const MediaIO::Config &cfg,
                                           const MediaDesc &mediaDesc);
                Error configureAudioStream(const MediaIO::Config &cfg,
                                           const MediaDesc &mediaDesc);
                Error configureDataStream(const MediaIO::Config &cfg);

                Error openStream(Stream &s, bool enableMulticastLoopback);
                Error openReaderStream(Stream &s, bool enableMulticastLoopback);

                /**
                 * @brief Sends one video frame on the @c _video stream.
                 *
                 * Called from the per-stream transmit thread via
                 * @c executeCmd(MediaIOCommandWrite&).  The frame
                 * index is passed in (rather than read from
                 * @c _frameCount) so the worker thread does not race
                 * with the strand thread that owns the counter.
                 *
                 * @param image      The image plane to packetise.
                 * @param frameIndex Zero-based frame index for this
                 *                   transmission, used to compute
                 *                   the RTP timestamp via
                 *                   @ref FrameRate::cumulativeTicks.
                 */
                Error sendVideo(const Image &image, const FrameNumber &frameIndex);

                /**
                 * @brief Sends one audio chunk on the @c _audio stream.
                 *
                 * Audio packetisation maintains its own monotonic
                 * sample counter inside @c _audioState, so no frame
                 * index is needed here.
                 */
                Error sendAudio(const Audio &audio);

                /**
                 * @brief Sends one metadata blob on the @c _data stream.
                 * @param metadata   The metadata to serialise.
                 * @param frameIndex Zero-based frame index for the RTP timestamp.
                 */
                Error sendData(const Metadata &metadata, const FrameNumber &frameIndex);

                // Reader path.
                Error applySdp(const SdpSession &sdp,
                               MediaIO::Config &cfg,
                               MediaDesc &mediaDesc);
                Error openAllReaders();
                void  onVideoPacket(const RtpPacket &pkt);
                void  onAudioPacket(const RtpPacket &pkt);
                void  onDataPacket(const RtpPacket &pkt);
                void  emitVideoFrame();
                void  emitDataMessage();
                void  pushReaderFrame(Frame::Ptr frame);

                void  buildSdp();
                Error writeSdpFile(const String &path);

                void resetStream(Stream &s);
                void resetAll();

                // Streams — indexed by kind.
                Stream _video;
                Stream _audio;
                Stream _data;

                /**
                 * @brief Audio-specific send state.
                 *
                 * The @ref AudioBuffer stores samples in the
                 * network-order wire format (@c PCMI_S16BE for L16)
                 * and accepts any compatible input format on push,
                 * transparently converting bit depth / endian / float
                 * vs int.  Samples that do not align to a full AES67
                 * packet are left in the FIFO and drained on the next
                 * writeFrame.  @c nextTimestamp advances by exactly
                 * @c packetSamples per emitted packet, so the audio
                 * clock never drifts regardless of what the video
                 * frame rate or writeFrame cadence looks like.
                 */
                struct AudioSendState {
                        AudioBuffer fifo;               ///< @brief Ring-buffered PCM FIFO in wire format.
                        size_t      packetSamples = 0;  ///< @brief Samples per AES67 packet (after MTU clamping).
                        size_t      packetBytes   = 0;  ///< @brief Bytes per AES67 packet.
                        int         packetTimeUs  = 0;  ///< @brief Resolved packet time in microseconds.
                        uint32_t    nextTimestamp = 0;  ///< @brief RTP timestamp for the next packet to emit.
                };
                AudioSendState _audioState;

                // Transport-global config
                SocketAddress   _localAddress;
                String          _sessionName;
                String          _sessionOrigin;
                String          _multicastInterface;
                int             _multicastTTL = 0;
                Enum            _pacingMode;
                Enum            _dataFormat;

                // Runtime
                FrameRate       _frameRate;
                FrameCount      _frameCount{0};
                FrameCount      _framesSent{0};

                // Mode
                bool            _readerMode = false;

                // RFC 4175 wire-format PixelFormat.  When the input
                // pixel format doesn't match what RFC 4175 expects
                // on the wire (e.g. YUYV vs UYVY), sendVideo()
                // calls Image::convert() to the wire format before
                // packing.  Invalid means no conversion needed.
                PixelFormat       _videoWirePixelFormat;

                // Reader runtime
                Queue<Frame::Ptr> _readerQueue;
                int             _readerMaxDepth = 4;
                int             _readerJitterMs = 50;
                FrameCount      _readerFramesReceived{0};

                /**
                 * @brief Reader-side frame aggregator.
                 *
                 * The three RTP RX threads (video, audio, data)
                 * receive packets independently and at different
                 * cadences.  Without aggregation, each stream would
                 * push separate Frame objects into @c _readerQueue,
                 * which breaks the SDL player's audio-led pacing
                 * model (it expects each Frame to carry both video
                 * and audio, like every other reader backend).
                 *
                 * This aggregator uses the video stream as the
                 * frame clock: when a complete video frame is
                 * reassembled (marker bit), @c emitVideoFrame
                 * drains one frame's worth of audio from the FIFO
                 * and merges the latest metadata snapshot, then
                 * pushes a single combined Frame that downstream
                 * consumers can process as a coherent A/V unit.
                 *
                 * Audio that arrives ahead of video accumulates in
                 * the FIFO.  If audio is late, @c emitVideoFrame
                 * waits up to @c audioTimeoutMs for the samples to
                 * appear before emitting with partial or no audio.
                 * The AudioBuffer will eventually support resampling
                 * which lets us compensate for long-term clock drift
                 * between audio and video RTP sources; for now the
                 * pass-through rate must match.
                 */
                struct ReaderAggregator {
                        /// @brief FIFO accumulating L16 samples from the audio RX thread.
                        AudioBuffer     audioFifo;
                        /// @brief Latest metadata snapshot from the data RX thread.
                        Metadata        pendingMetadata;
                        /// @brief Protects @c pendingMetadata and @c hasMetadata.
                        Mutex           dataMutex;
                        /// @brief True when @c pendingMetadata has been updated since the last video frame.
                        bool            hasMetadata = false;
                        /// @brief Zero-based frame index for @c samplesPerFrame.
                        FrameNumber     videoFrameIndex{0};
                        /// @brief Max wait (ms) for audio before emitting without it.
                        int             audioTimeoutMs = 50;
                };
                ReaderAggregator _readerAgg;

                // SDP — the active session description built at
                // open time.  Reader mode leaves this empty (the
                // reader consumes an externally-supplied SDP via
                // RtpSdp); writer mode populates it so the
                // GetSdp params command and the RtpSaveSdpPath
                // export path can serve it.
                SdpSession      _sdpSession;
                String          _sdpPath;
};

PROMEKI_NAMESPACE_END
