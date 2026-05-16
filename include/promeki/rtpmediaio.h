/**
 * @file      rtpmediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <atomic>
#include <promeki/atomic.h>
#include <promeki/audiobuffer.h>
#include <promeki/audiodesc.h>
#include <promeki/clockdomain.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/eui64.h>
#include <promeki/macaddress.h>
#include <promeki/frame.h>
#include <promeki/histogram.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/ntptime.h>
#include <promeki/pacinggate.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/queue.h>
#include <promeki/rtcpscheduler.h>
#include <promeki/rtpaggregatorthread.h>
#include <promeki/rtpancdepacketizerthread.h>
#include <promeki/rtpaudiodepacketizerthread.h>
#include <promeki/rtpaudiopacketizerthread.h>
#include <promeki/rtpaudiotxthread.h>
#include <promeki/rtpdatadepacketizerthread.h>
#include <promeki/rtpdepacketizerthread.h>
#include <promeki/rtpvideodepacketizerthread.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppacketizerthread.h>
#include <promeki/rtpseqreorderbuffer.h>
#include <promeki/rtpseqtracker.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/rtptxthread.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/sdpsession.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/videopayload.h>
#include <promeki/waitcondition.h>

PROMEKI_NAMESPACE_BEGIN

class RtpSession;
class RtpPayload;
class UdpSocketTransport;
class Thread;

/**
 * @brief MediaIO backend that transmits and receives frames as RTP streams.
 * @ingroup proav
 *
 * RtpMediaIO is a unified video + audio + metadata RTP backend.
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
 * - **Video** — one of:
 *   - **MJPEG** (RFC 2435) via @ref RtpPayloadJpeg when the input
 *     @ref PixelFormat is in the JPEG family.  @b Pick @b the
 *     @b right @b JPEG @b sub-format: JFIF / RFC 2435 has no
 *     standard way to signal YCbCr matrix (Rec.601 vs Rec.709)
 *     or range (limited vs full) inside the bitstream, so a
 *     strict JFIF consumer like ffplay always decodes with
 *     Rec.601 full-range math.  For correct playback in
 *     ffplay / browsers / any libjpeg-based receiver, use
 *     @c PixelFormat::JPEG_YUV8_422_Rec601_Full or
 *     @c PixelFormat::JPEG_YUV8_420_Rec601_Full.  Broadcast /
 *     SDI / ST 2110 pipelines that expect limited-range Rec.709
 *     JPEG should use the unsuffixed @c JPEG_YUV8_422_Rec709
 *     variant (the library-wide YCbCr default).  See the
 *     @ref pixelformat.h enum documentation for the full
 *     2 × 2 × 2 matrix × range × subsampling grid.
 *   - **JPEG XS** (RFC 9134) via @ref RtpPayloadJpegXs for the
 *     ST 2110-22 family of @c PixelFormat::JPEG_XS_* variants.
 *   - **H.264 / AVC** (RFC 6184) via @ref RtpPayloadH264 when
 *     the input @c PixelFormat is @c PixelFormat::H264.  The
 *     encoder is expected to feed Annex-B byte streams (start
 *     codes between NALs); @c RtpPayloadH264 fragments large
 *     NAL units into FU-A packets and sends small NALs as
 *     single-NAL packets.  Parameter sets (SPS / PPS) ship
 *     in-band as part of each IDR access unit; the SDP fmtp
 *     line carries @c packetization-mode=1 but does not
 *     currently carry @c sprop-parameter-sets — that
 *     out-of-band path is a planned follow-up (see @c
 *     _videoSpropParameterSets below).
 *   - **H.265 / HEVC** (RFC 7798) via @ref RtpPayloadH265 when
 *     the input @c PixelFormat is @c PixelFormat::HEVC.  Same
 *     in-band parameter-set policy as H.264 (VPS / SPS / PPS
 *     accompany each IRAP); the SDP fmtp asserts
 *     @c sprop-max-don-diff=0 to match the writer's no-DON
 *     emission ordering.
 *   - **Raw video** (RFC 4175) via @ref RtpPayloadRawVideo for
 *     8-bit interleaved uncompressed formats (first pass;
 *     proper ST 2110-20 pgroup sizing for 10/12-bit lands
 *     later).
 * - **Audio** — L16 (RFC 3551 / AES67) via @ref RtpPayloadL16.
 *   Input can arrive in any PCM data type that @ref AudioBuffer
 *   can convert to @c PCMI_S16BE (signed 16 / float32 / float64 at
 *   any endian, etc.); the backend holds a FIFO in network-order
 *   wire format, drains AES67-sized packets out of it, and stamps
 *   each packet with a monotonic per-sample RTP timestamp so the
 *   audio clock never drifts across writeFrame boundaries or
 *   fractional video frame rates.  Sample rate and channel count
 *   must match between input and output — upstream rate / layout
 *   conversion belongs in @ref CscMediaIO.  L24 and
 *   ST 2110-30 pgroup handling are deferred to a follow-up.
 * - **Data** — per-frame @ref Metadata as JSON via
 *   @ref RtpPayloadJson (default), with
 *   @c MetadataRtpFormat::St2110_40 reserved for the future
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
 * receive thread that pushes packets through the per-stream
 * @ref RtpSeqTracker / @ref RtpSeqReorderBuffer into a post-reorder
 * @c RtpPacket::Queue.  A per-stream depacketizer worker thread
 * pops the queue, runs reassembly + @c payload->unpack + JPEG
 * geometry probe / JSON parse, and produces typed bundles
 * (@c RxVideoFrame / @c RxAudioChunk / @c RxDataMessage) onto
 * its per-stream payload queue.  A single @c RtpAggregatorThread
 * per @ref RtpMediaIO consumes those queues, merges across the
 * video / audio / data domains, and pushes completed @ref Frame
 * objects into a thread-safe output queue that
 * @c executeCmd(MediaIOCommandRead) drains with a bounded timeout.
 * @c ReadWrite is explicitly rejected — an RTP sink and an RTP
 * source are conceptually different streams and should not share
 * a MediaIO.
 *
 * @par Parameter-set delivery (H.264 / H.265)
 *
 * For the temporal codecs the decoder needs out-of-band parameter
 * sets (H.264: SPS + PPS; HEVC: VPS + SPS + PPS) before it can
 * decode the first VCL slice.  Two paths exist:
 *
 *  - **In-band** — the encoder prefixes each IDR / IRAP access unit
 *    with its parameter sets, which then ride through @ref
 *    RtpPayloadH264 / @ref RtpPayloadH265 as ordinary NAL units.
 *    Every reasonable receiver (ffmpeg, GStreamer, browsers, all
 *    NVDec-based consumers) decodes correctly.  This is what the
 *    library does today.
 *  - **Out-of-band via SDP** — the writer publishes
 *    @c sprop-parameter-sets (H.264) or
 *    @c sprop-vps / @c sprop-sps / @c sprop-pps (H.265) in the
 *    @c a=fmtp line of the saved SDP so a receiver that parses the
 *    SDP before the first packet (some hardware ST 2110 decoders)
 *    can initialize its decoder ahead of time.  This requires
 *    capturing parameter sets at open time, before any frame has
 *    been encoded — either by deferring SDP emission until after
 *    the first IDR or by demanding the encoder publish its
 *    parameter sets via the @ref MediaConfig before open.  This
 *    path is not yet wired; the slot @c _videoSpropParameterSets is
 *    reserved for it.
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
 * downstream @ref CscMediaIO or their own aggregator
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
 * | @c Userspace | Per-stream TX thread paces via the @ref Cadence helper — anchored deadlines spaced at @c frameInterval / packetCount drive @c std::this_thread::sleep_until between per-packet sends. |
 * | @c KernelFq  | @ref RtpSession::setPacingRate() — @c SO_MAX_PACING_RATE via the @c fq qdisc (Linux default). |
 * | @c TxTime    | Per-packet @c SCM_TXTIME deadlines via the ETF qdisc (deferred; falls back to @c KernelFq). |
 *
 * The target rate for @c KernelFq is drawn from
 * @ref MediaConfig::VideoRtpTargetBitrate (if set) or computed
 * from the video descriptor for uncompressed inputs, and from
 * @c sampleRate @c × @c channels @c × @c bytesPerSample for audio.
 * Compressed video streams (JPEG / JPEG XS) without an explicit
 * @c VideoRtpTargetBitrate are paced per frame instead: the
 * per-stream @c VideoPacketizerThread recomputes the rate cap
 * from that frame's actual packed byte count and the
 * @c VideoTxThread applies it via @ref RtpSession::setPacingRate
 * before dispatch, so the kernel @c fq qdisc spaces a VBR
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
 *
 * @par Threading
 * Runs on a per-instance dedicated worker thread inherited from
 * @ref DedicatedThreadMediaIO; per-stream UDP transports and
 * reassembly threads run alongside.
 */
class RtpMediaIO : public DedicatedThreadMediaIO {
                PROMEKI_OBJECT(RtpMediaIO, DedicatedThreadMediaIO)
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
                /**
                 * @brief int64_t — cumulative AES67 silence packets
                 *        emitted by @c AudioTxThread when its
                 *        @c PacketQueue was empty at the cadence
                 *        deadline.  Aggregated across all audio
                 *        streams.  A non-zero value means the source
                 *        stalled at least once during the run; very
                 *        large values point to upstream pacing
                 *        problems.  See also @ref
                 *        StatsAudioSilenceSamplesEmitted.
                 */
                static inline const MediaIOStats::ID StatsAudioSilencePacketsEmitted{"AudioSilencePacketsEmitted"};
                /**
                 * @brief int64_t — cumulative PCM samples (per
                 *        stream, per channel — i.e. one tick of the
                 *        AES67 cadence at @c packetSamples advances
                 *        the counter by @c packetSamples regardless
                 *        of channel count) emitted as silence by
                 *        @c AudioTxThread.
                 */
                static inline const MediaIOStats::ID StatsAudioSilenceSamplesEmitted{"AudioSilenceSamplesEmitted"};

                // ----------------------------------------------------------
                // Reader-side per-stream RFC 3550 §A counters,
                // aggregated across every active reader stream
                // (video + audio + data).  All published by
                // @c executeCmd(MediaIOCommandStats); see
                // @c devplan/network/rtp-rx.md for the full
                // ReaderStream::Stats block these correspond to.
                // ----------------------------------------------------------

                /** @brief uint32_t — most-recent extended-highest-seq across all reader streams. */
                static inline const MediaIOStats::ID StatsRxExtendedHighestSeq{"RxExtendedHighestSeq"};
                /** @brief uint32_t — total packets RFC 3550 §6.4.1 expected (per first reader stream). */
                static inline const MediaIOStats::ID StatsRxPacketsExpected{"RxPacketsExpected"};
                /** @brief int32_t — RFC 3550 §6.4.1 cumulative-lost (signed, summed across reader streams). */
                static inline const MediaIOStats::ID StatsRxCumulativeLost{"RxCumulativeLost"};
                /** @brief uint8_t — RFC 3550 §6.4.1 fraction-lost from the first reader stream. */
                static inline const MediaIOStats::ID StatsRxFractionLost{"RxFractionLost"};
                /** @brief int64_t — duplicate packets observed across all reader streams. */
                static inline const MediaIOStats::ID StatsRxDuplicatePackets{"RxDuplicatePackets"};
                /** @brief int64_t — reordered packets observed across all reader streams. */
                static inline const MediaIOStats::ID StatsRxReorderedPackets{"RxReorderedPackets"};
                /** @brief uint32_t — RFC 3550 §A.8 interarrival jitter (RTP-TS units) from the first reader stream. */
                static inline const MediaIOStats::ID StatsRxInterarrivalJitter{"RxInterarrivalJitter"};

                /** @brief int64_t — debounced SSRC-change events summed across all reader streams. */
                static inline const MediaIOStats::ID StatsRxSsrcChanges{"RxSsrcChanges"};

                /** @brief int64_t — reorder-buffer in-order emissions across all reader streams. */
                static inline const MediaIOStats::ID StatsRxReorderEmittedInOrder{"RxReorderEmittedInOrder"};
                /** @brief int64_t — reorder-buffer deadline-driven gap-fill emissions across all reader streams. */
                static inline const MediaIOStats::ID StatsRxReorderEmittedOnDeadline{"RxReorderEmittedOnDeadline"};
                /** @brief int64_t — reorder-buffer overflow drops across all reader streams. */
                static inline const MediaIOStats::ID StatsRxReorderDroppedOverflow{"RxReorderDroppedOverflow"};
                /** @brief int64_t — reorder-buffer duplicate-seq drops across all reader streams. */
                static inline const MediaIOStats::ID StatsRxReorderDroppedDuplicate{"RxReorderDroppedDuplicate"};

                /** @brief int64_t — current depth of the per-stream video PayloadQueue (first reader). */
                static inline const MediaIOStats::ID StatsRxVideoQueueDepth{"RxVideoQueueDepth"};
                /** @brief int64_t — current depth of the per-stream audio PayloadQueue (first reader). */
                static inline const MediaIOStats::ID StatsRxAudioQueueDepth{"RxAudioQueueDepth"};
                /** @brief int64_t — current depth of the per-stream data PayloadQueue (first reader). */
                static inline const MediaIOStats::ID StatsRxDataQueueDepth{"RxDataQueueDepth"};
                /** @brief int64_t — current depth of the aggregator's reader-output queue. */
                static inline const MediaIOStats::ID StatsRxReaderQueueDepth{"RxReaderQueueDepth"};

                /** @brief int64_t — frames the depacketizers have successfully reassembled
                 *         and pushed onto a payload queue (summed across reader streams).
                 *         Audio counts each @c RxAudioChunk; video / data counts each
                 *         emitted bundle. */
                static inline const MediaIOStats::ID StatsRxFramesReassembled{"RxFramesReassembled"};
                /** @brief int64_t — frames dropped by @c RtpPayload::validate returning
                 *         @c DropSilently (e.g. mid-frame join, partial reassembly,
                 *         pre-IDR compressed video).  Summed across reader streams. */
                static inline const MediaIOStats::ID StatsRxFramesDroppedValidate{"RxFramesDroppedValidate"};
                /** @brief int64_t — frames waiting on out-of-band parameter sets
                 *         (@c validate returning @c Wait) — typically H.264 / HEVC
                 *         streams without an SPS/PPS or VPS yet observed.  Summed
                 *         across reader streams. */
                static inline const MediaIOStats::ID StatsRxFramesWaitingParamSets{"RxFramesWaitingParamSets"};
                /** @brief int64_t — frames dropped due to an SSRC reset epoch flush
                 *         (i.e. the depacketizer had a partial reassembly when the
                 *         recv thread bumped the reset epoch and chose to discard
                 *         rather than emit a now-stale bundle).  Summed across
                 *         reader streams. */
                static inline const MediaIOStats::ID StatsRxFramesDroppedSsrcReset{"RxFramesDroppedSsrcReset"};

                /** @brief int64_t — cumulative count of SRs the receive thread has
                 *         parsed across every active reader-side RtpSession.  Zero
                 *         until the first SR arrives. */
                static inline const MediaIOStats::ID StatsRxSrObserved{"RxSrObserved"};
                /** @brief int64_t microseconds — age of the most-recent SR observed
                 *         on the first reader stream that has one (i.e. the
                 *         @c (now − arrivedAt) gap on the freshest @c receivedSr
                 *         snapshot).  Zero until the first SR has arrived. */
                static inline const MediaIOStats::ID StatsRxLastSrAgeUs{"RxLastSrAgeUs"};
                /** @brief int64_t microseconds — duration from the
                 *         @c executeCmd(MediaIOCommandOpen) entry point to the
                 *         first SR observed on any reader-side RtpSession.  Zero
                 *         until the first SR has arrived. */
                static inline const MediaIOStats::ID StatsRxFirstSrLatencyUs{"RxFirstSrLatencyUs"};

                /** @brief Params command name: return the SDP text in @c result["Sdp"]. */
                static inline const MediaIOParamsID ParamGetSdp{"GetSdp"};
                /** @brief Key used in @c GetSdp result. */
                static inline const MediaIOParamsID ParamSdp{"Sdp"};

                /** @brief Constructs an RtpMediaIO. */
                RtpMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor. Closes any still-open streams. */
                ~RtpMediaIO() override;

                /**
                 * @brief Returns the per-process unique RtpMediaIO instance ID.
                 *
                 * Assigned at construction from a process-local atomic
                 * counter that starts at 1 and never repeats.  Two
                 * @ref RtpMediaIO objects in the same process get
                 * distinct IDs; a fresh process restart resets the
                 * counter to 1.  Combined with @ref Application::pid
                 * (and an egress IP) this is what gives every
                 * @ref RtpMediaIO a stable, distinguishable RTCP
                 * SDES @c CNAME by default.
                 */
                uint64_t objectId() const { return _objectId; }

                /**
                 * @brief Composes the auto-generated RTCP SDES CNAME string.
                 *
                 * Pure formatting helper, exposed for testability.
                 * Builds @c "promeki-&lt;pid&gt;-&lt;objectId&gt;@&lt;host&gt;"
                 * (RFC 3550 §6.5.1 @c user@host shape).  IPv6 hosts
                 * must be supplied already bracket-wrapped (e.g.
                 * @c "[2001:db8::42]") so the @c '@' separator is
                 * unambiguous.  When @p host is empty the @c "@"
                 * separator is omitted entirely so callers that
                 * exhaust every host-derivation fallback still emit
                 * a structurally valid CNAME.
                 */
                static String buildDefaultCname(int64_t pid, uint64_t objectId, const String &host);

                /**
                 * @brief Picks the host portion of the auto-generated CNAME.
                 *
                 * Strategy: if @p destination has an IP, ask
                 * @ref NetworkInterface::findRoutesTo for the egress
                 * interface and take its first IPv4 (or first IPv6,
                 * bracket-wrapped) address.  When no interface
                 * matches — destination unroutable, hostname-only,
                 * or empty — fall back to
                 * @ref NetworkInterface::firstNonLoopback's first
                 * address.  When even that fails (loopback-only host,
                 * no backend), return an empty string and let the
                 * caller decide on a hostname / empty fallback.
                 *
                 * Static / pure-of-instance-state so a single derived
                 * value can be shared across every session of one
                 * @ref RtpMediaIO.
                 */
                static String pickEgressHostForCname(const SocketAddress &destination);

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandParams &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                /**
                 * @brief Accepts an external pacing clock for writer mode.
                 *
                 * In writer mode the supplied @ref Clock paces the
                 * frame-level cadence on the strand: the strand
                 * sleeps until the clock reports the next frame
                 * deadline before pushing the Frame onto every
                 * active stream's @c PayloadQueue.  Per-stream
                 * intra-frame packet spread (kernel-FQ /
                 * @c RtpPacingMode::Userspace / None) is unaffected
                 * and continues to use its own timing logic in the
                 * per-stream TX threads.  Audio is not paced by
                 * this clock — AES67 packet timing is governed by
                 * the @c AudioTxThread's @c Cadence helper.
                 *
                 * Reader mode returns @c Error::NotSupported.  A null
                 * @c cmd.clock detaches the external clock and lets
                 * the upstream pump's natural cadence resume control.
                 */
                Error executeCmd(MediaIOCommandSetClock &cmd) override;

                // Wakes the reader-side executeCmd(Read) loop so close()
                // can drain a strand parked on _readerQueue.pop().
                void cancelBlockingWork() override;

        private:

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
                // Per-stream packetizer + TX worker classes.  Each
                // active stream gets one of each: the packetizer
                // pulls Frames off the strand-side @c PayloadQueue
                // and emits payload-bytes-only packets onto the
                // sink-side queue; the TX thread pops the latter,
                // stamps the full RTP header, and dispatches to
                // the wire.  Packetizer and TX talk only through
                // their own per-stream @c Queue, so stream-level
                // jitter (heavy IDR encode, audio cadence) never
                // bleeds across stream boundaries.  Concrete
                // subclasses live as nested classes inside
                // @c rtpmediaio.cpp because they need access to
                // RtpMediaIO state (parameter-set cache, pacing
                // mode, SDP sprop refresh, etc.).
                class VideoPacketizerThread;
                class AudioPacketizerThread;
                class DataPacketizerThread;
                class VideoTxThread;
                class AudioTxThread;
                class DataTxThread;

                /**
                 * @brief Per-stream RTP state — kind-agnostic, mode-agnostic base.
                 *
                 * Carries identity fields shared by every kind and
                 * every mode: transport / session / payload pointer,
                 * destination address, RTP / SDP descriptors
                 * (payloadType / clockRate / rtpmap / fmtp / mediaType),
                 * SSRC, DSCP, the @c active gate set by openStream /
                 * openReaderStream, and the SDP-derived clock domain.
                 *
                 * Mode-specific state lives on the writer-side / reader-
                 * side subclasses:
                 *
                 *  - @ref WriterStream — TX threads, TX stats counters,
                 *    TX histograms.  All three writer-mode subclasses
                 *    (@ref VideoStream / @ref AudioStream /
                 *    @ref DataStream) inherit from @c WriterStream.
                 *  - @ref ReaderStream — reassembly state, RX stats
                 *    counters, RX histograms, JPEG-discovered
                 *    @c readerImageDesc.  All three reader-mode
                 *    subclasses (@ref VideoReaderStream /
                 *    @ref AudioReaderStream / @ref DataReaderStream)
                 *    inherit from @c ReaderStream.
                 *
                 * Helpers that read only identity fields take a
                 * @c Stream & — e.g. the RTCP scheduler's
                 * @c emitForStream, the SDP builder's @c addStream.
                 * Helpers whose work is mode-bound take the matching
                 * subclass — e.g. @c openStream takes
                 * @c WriterStream &, @c openReaderStream takes
                 * @c ReaderStream &.
                 */
                struct Stream {
                                Stream() = default;
                                Stream(Stream &&o) noexcept
                                    : transport(o.transport),
                                      session(o.session),
                                      payload(o.payload),
                                      destination(std::move(o.destination)),
                                      payloadType(o.payloadType),
                                      clockRate(o.clockRate),
                                      dscp(o.dscp),
                                      ssrc(o.ssrc),
                                      mediaType(std::move(o.mediaType)),
                                      rtpmap(std::move(o.rtpmap)),
                                      fmtp(std::move(o.fmtp)),
                                      active(o.active),
                                      clockDomain(o.clockDomain),
                                      tsRefClkMode(o.tsRefClkMode),
                                      ptpGrandmaster(o.ptpGrandmaster),
                                      ptpDomain(o.ptpDomain),
                                      refClockLocalMac(std::move(o.refClockLocalMac)),
                                      mediaClkOffset(o.mediaClkOffset) {
                                        // Null pointers on the moved-
                                        // from instance so a stray
                                        // reset can't double-delete.
                                        o.transport = nullptr;
                                        o.session = nullptr;
                                        o.payload = nullptr;
                                }
                                Stream(const Stream &) = delete;
                                Stream &operator=(const Stream &) = delete;
                                Stream &operator=(Stream &&) = delete;
                                UdpSocketTransport *transport = nullptr;
                                RtpSession         *session = nullptr;
                                RtpPayload         *payload = nullptr;
                                SocketAddress       destination;
                                uint8_t             payloadType = 0;
                                uint32_t            clockRate = 90000;
                                int                 dscp = 0;
                                uint32_t            ssrc = 0;
                                String              mediaType; ///< @brief "video", "audio", "data"
                                String              rtpmap;    ///< @brief SDP a=rtpmap:... value
                                String              fmtp;      ///< @brief SDP a=fmtp:... value, optional
                                bool                active = false;
                                ClockDomain         clockDomain;    ///< @brief Clock domain derived from SDP ts-refclk.
                                RtpRefClockMode     tsRefClkMode = RtpRefClockMode::None; ///< @brief Drives buildSdp ts-refclk emission.
                                EUI64               ptpGrandmaster; ///< @brief PTP grandmaster ID for SDP ts-refclk:ptp.
                                uint8_t             ptpDomain = 0;  ///< @brief PTP domain number for SDP ts-refclk:ptp.
                                MacAddress          refClockLocalMac; ///< @brief MAC for SDP ts-refclk:localmac.
                                int32_t             mediaClkOffset = 0; ///< @brief SDP mediaclk:direct=&lt;offset&gt;.
                };

                /**
                 * @brief Writer-mode stream state.
                 *
                 * Holds the per-stream packetizer + TX thread pointers
                 * (@c packetizer + @c tx), the cumulative TX stats
                 * counters that the TX thread bumps and the strand-side
                 * @c executeCmd(Stats) reads back, and the TX-side
                 * timing histograms.  @c Atomic<int64_t> for the
                 * counters keeps the TX-thread → strand handoff
                 * lock-free (acquire/release semantics — no torn
                 * reads).  Histograms are written only by the TX-side
                 * worker that owns the stream and read by the strand
                 * during the close-time dump and stats query.
                 */
                struct WriterStream : Stream {
                                WriterStream() = default;
                                WriterStream(WriterStream &&o) noexcept
                                    : Stream(std::move(o)),
                                      packetizer(o.packetizer),
                                      tx(o.tx),
                                      packetsSent(o.packetsSent.value()),
                                      bytesSent(o.bytesSent.value()),
                                      senderOctets(o.senderOctets.value()),
                                      txFrameInterval(std::move(o.txFrameInterval)),
                                      txSendDuration(std::move(o.txSendDuration)),
                                      txLastSendStart(o.txLastSendStart),
                                      txHasLastSend(o.txHasLastSend) {
                                        o.packetizer = nullptr;
                                        o.tx = nullptr;
                                }
                                WriterStream(const WriterStream &) = delete;
                                WriterStream &operator=(const WriterStream &) = delete;
                                WriterStream &operator=(WriterStream &&) = delete;
                                RtpPacketizerThread *packetizer = nullptr;
                                RtpTxThread         *tx = nullptr;
                                Atomic<int64_t>     packetsSent{0};
                                Atomic<int64_t>     bytesSent{0};
                                Atomic<int64_t>     senderOctets{0}; ///< @brief Total RTP payload bytes (no header) — for RTCP SR.
                                Histogram           txFrameInterval; ///< @brief µs between successive packetizer-thread entries (one push per frame)
                                Histogram           txSendDuration;  ///< @brief µs from packetize → wire emission (queue + send time)
                                TimeStamp           txLastSendStart; ///< @brief Last per-frame packetizer entry (writer's video packetizer thread only)
                                bool                txHasLastSend = false;
                };

                /**
                 * @brief Reader-mode stream state.
                 *
                 * Holds the reassembly state (current packet list,
                 * latched RTP timestamp) the per-stream depacketizer
                 * thread accumulates between marker bits, the RX
                 * stats counters, the per-stream RFC 3550 §A
                 * @ref RtpSeqTracker / @ref RtpSeqReorderBuffer +
                 * post-reorder queue + depacketizer pointer, and
                 * the RX-side timing histograms.  The
                 * JPEG-discovered @c readerImageDesc / generic
                 * @c readerAudioDesc descriptors live here too —
                 * they are populated by the depacketizer thread
                 * from the wire bytes plus the SDP's advertised
                 * shape, never by the writer side.
                 */
                struct ReaderStream : Stream {
                                ReaderStream() = default;
                                ReaderStream(ReaderStream &&o) noexcept
                                    : Stream(std::move(o)),
                                      packetsReceived(o.packetsReceived.value()),
                                      bytesReceived(o.bytesReceived.value()),
                                      framesReceived(o.framesReceived),
                                      packetsLost(o.packetsLost.value()),
                                      readerImageDesc(std::move(o.readerImageDesc)),
                                      readerAudioDesc(std::move(o.readerAudioDesc)),
                                      reasmTimestamp(o.reasmTimestamp),
                                      reasmHasTimestamp(o.reasmHasTimestamp),
                                      reasmLastSeq(o.reasmLastSeq),
                                      reasmHaveLastSeq(o.reasmHaveLastSeq),
                                      reasmPackets(std::move(o.reasmPackets)),
                                      rxPacketInterval(std::move(o.rxPacketInterval)),
                                      rxFrameInterval(std::move(o.rxFrameInterval)),
                                      rxFrameAssembleTime(std::move(o.rxFrameAssembleTime)),
                                      rxLastPacketTime(o.rxLastPacketTime),
                                      rxLastFrameTime(o.rxLastFrameTime),
                                      rxFrameStartTime(o.rxFrameStartTime),
                                      rxHasLastPacket(o.rxHasLastPacket),
                                      rxHasLastFrame(o.rxHasLastFrame),
                                      rxHasFrameStart(o.rxHasFrameStart),
                                      streamClock(std::move(o.streamClock)),
                                      lastSrArrivedAt(o.lastSrArrivedAt),
                                      hasSr(o.hasSr),
                                      seqTracker(std::move(o.seqTracker)),
                                      reorderBuffer(std::move(o.reorderBuffer)),
                                      reorderQueue(std::move(o.reorderQueue)),
                                      depacketizer(std::move(o.depacketizer)),
                                      ssrcChanges(o.ssrcChanges.value()),
                                      framesReassembled(o.framesReassembled.value()),
                                      framesDroppedValidate(o.framesDroppedValidate.value()),
                                      framesWaitingParamSets(o.framesWaitingParamSets.value()),
                                      framesDroppedSsrcReset(o.framesDroppedSsrcReset.value()) {}
                                ReaderStream(const ReaderStream &) = delete;
                                ReaderStream &operator=(const ReaderStream &) = delete;
                                ReaderStream &operator=(ReaderStream &&) = delete;
                                Atomic<int64_t> packetsReceived{0};
                                Atomic<int64_t> bytesReceived{0};
                                FrameCount      framesReceived{0};
                                Atomic<int64_t> packetsLost{0};
                                ImageDesc       readerImageDesc;
                                AudioDesc       readerAudioDesc;
                                uint32_t        reasmTimestamp = 0;
                                bool            reasmHasTimestamp = false;
                                uint16_t        reasmLastSeq = 0;
                                bool            reasmHaveLastSeq = false;
                                RtpPacket::List reasmPackets;
                                Histogram       rxPacketInterval;    ///< @brief µs between received packets
                                Histogram       rxFrameInterval;     ///< @brief µs between completed frames
                                Histogram       rxFrameAssembleTime; ///< @brief µs first packet -> marker
                                TimeStamp       rxLastPacketTime;    ///< @brief Last received packet time
                                TimeStamp       rxLastFrameTime;     ///< @brief Last emitted frame time
                                TimeStamp       rxFrameStartTime;    ///< @brief First packet of current reassembly
                                bool            rxHasLastPacket = false;
                                bool            rxHasLastFrame = false;
                                bool            rxHasFrameStart = false;
                                /// @brief Most-recently refreshed
                                ///        @ref RtpStreamClock for this
                                ///        stream.  Built from the SR
                                ///        the receive thread parsed
                                ///        out of the RTCP demux path
                                ///        and updated on every packet
                                ///        whose @c session->receivedSr
                                ///        snapshot has advanced.
                                ///        Invalid until the first SR
                                ///        for this stream lands —
                                ///        consumers must fall back to
                                ///        the non-wallclock-aware path
                                ///        until then.
                                RtpStreamClock streamClock;
                                /// @brief @c arrivedAt of the SR this
                                ///        @ref streamClock was last
                                ///        refreshed from.  Used to
                                ///        detect "has a fresh SR
                                ///        landed since I last
                                ///        looked?" cheaply on every
                                ///        incoming packet.
                                TimeStamp lastSrArrivedAt;
                                /// @brief Set once the first SR has
                                ///        seeded the stream clock.
                                bool hasSr = false;

                                /// @brief Per-source RFC 3550 §A
                                ///        seq / loss / jitter
                                ///        tracker.  Owned per
                                ///        ReaderStream; the recv
                                ///        socket thread updates it
                                ///        via the @c StreamReceiver
                                ///        list passed to
                                ///        @ref RtpSession::startReceiving.
                                ///        @c nullptr until
                                ///        @c openReaderStream
                                ///        instantiates it.
                                UniquePtr<RtpSeqTracker> seqTracker;

                                /// @brief Per-stream windowed
                                ///        reorder buffer in front
                                ///        of the depacketizer
                                ///        thread.
                                UniquePtr<RtpSeqReorderBuffer> reorderBuffer;

                                /// @brief Post-reorder packet
                                ///        queue.  Recv thread
                                ///        pushes via the reorder
                                ///        buffer's
                                ///        @c pushDropOldest path;
                                ///        @c depacketizer pops in
                                ///        its run loop.
                                UniquePtr<RtpPacket::Queue> reorderQueue;

                                /// @brief Owned per-stream
                                ///        depacketizer worker.
                                ///        Concrete type
                                ///        (Video / Audio / Data)
                                ///        is selected in
                                ///        @c rtpmediaio.cpp where
                                ///        the nested subclass
                                ///        definitions live.
                                UniquePtr<RtpDepacketizerThread> depacketizer;

                                /// @brief Monotone counter the recv
                                ///        thread bumps on every SSRC
                                ///        reset.  The depacketizer
                                ///        thread compares against its
                                ///        last-observed value at the
                                ///        top of @c handlePacket; on
                                ///        mismatch it drains its
                                ///        reassembly state and
                                ///        @c StreamAnchor before
                                ///        processing the new packet.
                                ///        Wraparound is harmless —
                                ///        only equality matters.
                                Atomic<uint32_t> resetEpoch{0};

                                /// @brief Nanoseconds-since-epoch of
                                ///        the most recent packet seen
                                ///        on this stream's
                                ///        depacketizer.  Updated by
                                ///        the depacketizer thread on
                                ///        every observed packet; read
                                ///        by the RTCP scheduler's
                                ///        wire-silence watchdog.  Zero
                                ///        until the first packet
                                ///        arrives.
                                Atomic<int64_t> lastPacketArrivalNs{0};

                                /// @brief @c true once the wire-
                                ///        silence watchdog has
                                ///        signalled EoS for this
                                ///        stream.  Owned by the
                                ///        watchdog (RTCP scheduler
                                ///        thread) — never written
                                ///        from any other thread.
                                bool wireSilenceEosSignaled = false;

                                /// @brief Cumulative count of debounced
                                ///        SSRC-change events observed on
                                ///        this stream.  Incremented by
                                ///        the recv socket thread (via the
                                ///        @c ssrcChangeSignal slot) every
                                ///        time the SSRC pin is updated to
                                ///        a new sustained value.  Used as
                                ///        the boundary marker when
                                ///        consumers compare seq-tracker
                                ///        deltas across resets.
                                Atomic<int64_t> ssrcChanges{0};

                                /// @brief Cumulative count of bundles the
                                ///        per-stream depacketizer has
                                ///        successfully pushed onto its
                                ///        @c payloadQueue.  Bumped by the
                                ///        depacketizer thread once per
                                ///        successful @c emitFrame /
                                ///        @c emitMessage / per-packet
                                ///        audio chunk.  Surfaced through
                                ///        @ref StatsRxFramesReassembled.
                                Atomic<int64_t> framesReassembled{0};

                                /// @brief Cumulative count of frames
                                ///        dropped because
                                ///        @ref RtpPayload::validate
                                ///        returned @c DropSilently —
                                ///        partial reassembly, mid-frame
                                ///        join, pre-IDR compressed video.
                                ///        Audio depacketizer leaves this
                                ///        zero.  Surfaced through
                                ///        @ref StatsRxFramesDroppedValidate.
                                Atomic<int64_t> framesDroppedValidate{0};

                                /// @brief Cumulative count of frames
                                ///        held back because
                                ///        @ref RtpPayload::validate
                                ///        returned @c Wait — typically
                                ///        compressed video gated on
                                ///        an SPS/PPS or VPS that has
                                ///        not yet been observed.
                                ///        Surfaced through
                                ///        @ref StatsRxFramesWaitingParamSets.
                                Atomic<int64_t> framesWaitingParamSets{0};

                                /// @brief Cumulative count of in-flight
                                ///        frames discarded when the
                                ///        depacketizer observes a new
                                ///        @ref resetEpoch and chooses to
                                ///        flush rather than emit a now-
                                ///        stale bundle.  Surfaced through
                                ///        @ref StatsRxFramesDroppedSsrcReset.
                                Atomic<int64_t> framesDroppedSsrcReset{0};
                };

                /**
                 * @brief Writer-mode video stream state.
                 *
                 * Inherits @ref WriterStream and adds the H.264 / HEVC
                 * parameter-set cache.  Cached SPS / PPS / VPS NAL
                 * payloads (no start codes, no length prefixes) hold
                 * the most-recently-observed copies pulled out of the
                 * input Annex-B bitstream by @ref VideoPacketizerThread.
                 * The self-healing parameter-set injector reads these
                 * to prepend parameter sets to any IDR / IRAP access
                 * unit whose upstream encoder did not emit them
                 * in-band.  Empty until the first SPS / PPS / VPS is
                 * observed; cleared on close.  Meaningful only for
                 * H.264 (@c cachedSps / @c cachedPps) and HEVC
                 * (@c cachedVps / @c cachedSps / @c cachedPps).
                 */
                struct VideoStream : WriterStream {
                                VideoStream() = default;
                                VideoStream(VideoStream &&o) noexcept
                                    : WriterStream(std::move(o)),
                                      imageDesc(std::move(o.imageDesc)),
                                      cachedSps(std::move(o.cachedSps)),
                                      cachedPps(std::move(o.cachedPps)),
                                      cachedVps(std::move(o.cachedVps)) {}
                                VideoStream(const VideoStream &) = delete;
                                VideoStream &operator=(const VideoStream &) = delete;
                                VideoStream &operator=(VideoStream &&) = delete;
                                /// @brief Source image descriptor as
                                ///        configured at open time.
                                ///        Used by @ref refreshSdpSprop
                                ///        to look up the codec ID
                                ///        when deciding whether to
                                ///        build H.264 sprop-parameter-
                                ///        sets or HEVC sprop-vps /
                                ///        sps / pps SDP lines.
                                ImageDesc imageDesc;
                                Buffer cachedSps;
                                Buffer cachedPps;
                                Buffer cachedVps;
                };

                /**
                 * @brief Writer-mode data stream state.
                 *
                 * Inherits @ref WriterStream with no kind-specific
                 * fields today.  Exists to symmetrize the per-kind
                 * type hierarchy with @ref VideoStream and
                 * @ref AudioStream so callers can write
                 * @c DataStream &ds parameters that document what the
                 * helper expects.
                 */
                struct DataStream : WriterStream {
                                DataStream() = default;
                                DataStream(DataStream &&o) noexcept = default;
                                DataStream(const DataStream &) = delete;
                                DataStream &operator=(const DataStream &) = delete;
                                DataStream &operator=(DataStream &&) = delete;
                };

                /**
                 * @brief Writer-mode audio stream state.
                 *
                 * Inherits @ref WriterStream and adds the AES67
                 * wire-format packetisation parameters and silence-
                 * stats counters.  The AudioBuffer FIFO and per-packet
                 * RTP-TS counter live inside
                 * @ref AudioPacketizerThread / @ref AudioTxThread
                 * respectively (see @c rtpmediaio.cpp).
                 *
                 * @c RtpMediaIO holds these in a list (@c _audios) so
                 * the surrounding code is structurally ready for
                 * sessions that carry more than one independent audio
                 * stream — e.g. an English mix on one m=audio line and
                 * a localized mix on the next, or a SMPTE 2110-30
                 * deliverable that splits a multichannel program
                 * across multiple network streams.
                 */
                struct AudioStream : WriterStream {
                                AudioStream() = default;
                                AudioStream(AudioStream &&o) noexcept
                                    : WriterStream(std::move(o)),
                                      storageDesc(std::move(o.storageDesc)),
                                      packetSamples(o.packetSamples),
                                      packetBytes(o.packetBytes),
                                      packetTimeUs(o.packetTimeUs),
                                      prerollSamples(o.prerollSamples),
                                      silencePacketsEmitted(
                                              o.silencePacketsEmitted.value()),
                                      silenceSamplesEmitted(
                                              o.silenceSamplesEmitted.value()) {}
                                AudioStream(const AudioStream &) = delete;
                                AudioStream &operator=(const AudioStream &) = delete;
                                AudioStream &operator=(AudioStream &&) = delete;
                                /// @brief Storage descriptor used by the
                                ///        AudioPacketizerThread's FIFO and
                                ///        the AudioTxThread's silence filler.
                                ///        Resolved at configure time
                                ///        from the upstream AudioDesc; the
                                ///        wire format is always
                                ///        @c PCMI_S16BE for L16.
                                AudioDesc storageDesc;
                                /// @brief Samples per AES67 packet, after MTU clamping.
                                size_t packetSamples = 0;
                                /// @brief Bytes per AES67 packet (samples × channels × 2).
                                size_t packetBytes = 0;
                                /// @brief Resolved packet time in microseconds.
                                int packetTimeUs = 0;
                                /// @brief Number of samples the
                                ///        AudioPacketizerThread waits for in
                                ///        its FIFO before producing the
                                ///        first packet.  Resolved from
                                ///        @ref MediaConfig::AudioRtpPrerollMs
                                ///        at configure time.  0 means no
                                ///        preroll (begin emitting on first
                                ///        source push).
                                size_t prerollSamples = 0;
                                /// @brief Cumulative AES67 packets the
                                ///        @c AudioTxThread emitted as
                                ///        silence when its @c PacketQueue
                                ///        was empty at a cadence
                                ///        deadline.  Read by
                                ///        @c executeCmd(Stats); surfaced
                                ///        through
                                ///        @ref StatsAudioSilencePacketsEmitted.
                                Atomic<int64_t> silencePacketsEmitted{0};
                                /// @brief Cumulative PCM samples emitted
                                ///        as silence (counts per packet
                                ///        × @c packetSamples).  See
                                ///        @ref silencePacketsEmitted.
                                Atomic<int64_t> silenceSamplesEmitted{0};
                };

                /**
                 * @brief Reader-mode video stream state.
                 *
                 * Inherits @ref ReaderStream and adds the typed
                 * @c Queue<RxVideoFrame> the
                 * @c VideoDepacketizerThread (sole producer) and
                 * @c RtpAggregatorThread (sole consumer) share.
                 * RFC 4175 / RFC 2435 / H.264 / H.265 reader state
                 * lives entirely on the base.
                 */
                struct VideoReaderStream : ReaderStream {
                                VideoReaderStream() = default;
                                VideoReaderStream(VideoReaderStream &&o) noexcept
                                    : ReaderStream(std::move(o)),
                                      payloadQueue(std::move(o.payloadQueue)) {}
                                VideoReaderStream(const VideoReaderStream &) = delete;
                                VideoReaderStream &operator=(const VideoReaderStream &) = delete;
                                VideoReaderStream &operator=(VideoReaderStream &&) = delete;
                                /// @brief Typed bundle queue between the
                                ///        @c VideoDepacketizerThread
                                ///        (sole producer) and
                                ///        @c RtpAggregatorThread
                                ///        (sole consumer).  Bounded
                                ///        at @ref VideoPayloadQueueDepth;
                                ///        block-on-full so the
                                ///        depacketizer back-pressures
                                ///        rather than dropping post-
                                ///        reassembly bundles.
                                UniquePtr<Queue<RxVideoFrame>> payloadQueue;
                };

                /**
                 * @brief Reader-mode audio stream state.
                 *
                 * Inherits @ref ReaderStream and adds a typed
                 * @c Queue<RxAudioChunk> that the
                 * @c AudioDepacketizerThread fills and the
                 * @c RtpAggregatorThread drains.  The aggregator
                 * owns the AudioBuffer FIFO that re-assembles
                 * samples into per-Frame slices, so this struct
                 * no longer carries one of its own.
                 *
                 * The unused legacy @c fifo field is retained for
                 * binary compatibility during the staged Phase
                 * 2.B → Phase 5 cleanup; it will go away with the
                 * rest of the legacy-receive-path scaffolding.
                 */
                struct AudioReaderStream : ReaderStream {
                                AudioReaderStream() = default;
                                AudioReaderStream(AudioReaderStream &&o) noexcept
                                    : ReaderStream(std::move(o)),
                                      fifo(std::move(o.fifo)),
                                      payloadQueue(std::move(o.payloadQueue)) {}
                                AudioReaderStream(const AudioReaderStream &) = delete;
                                AudioReaderStream &operator=(const AudioReaderStream &) = delete;
                                AudioReaderStream &operator=(AudioReaderStream &&) = delete;
                                /// @brief Audio-only-mode FIFO used when
                                ///        no video reader is active and
                                ///        the aggregator emits one
                                ///        Frame per audio cadence
                                ///        boundary.  Unused when video
                                ///        is active — in that case the
                                ///        aggregator owns its own FIFO
                                ///        and pulls from
                                ///        @ref payloadQueue.
                                AudioBuffer fifo;
                                /// @brief Typed bundle queue between the
                                ///        @c AudioDepacketizerThread
                                ///        and @c RtpAggregatorThread.
                                UniquePtr<Queue<RxAudioChunk>> payloadQueue;
                };

                /**
                 * @brief Reader-mode data stream state.
                 *
                 * Inherits @ref ReaderStream and carries the typed
                 * bundle queues for both wire formats the
                 * @c m=application section can choose between:
                 *  - JSON metadata (@ref MetadataRtpFormat::JsonMetadata)
                 *    feeds @ref payloadQueue;
                 *  - RFC 8331 ANC (@ref MetadataRtpFormat::St2110_40)
                 *    feeds @ref ancPayloadQueue.
                 * Exactly one is populated for a given session — the
                 * configure step picks based on @c DataRtpFormat.
                 */
                struct DataReaderStream : ReaderStream {
                                DataReaderStream() = default;
                                DataReaderStream(DataReaderStream &&o) noexcept
                                    : ReaderStream(std::move(o)),
                                      payloadQueue(std::move(o.payloadQueue)),
                                      ancPayloadQueue(std::move(o.ancPayloadQueue)) {}
                                DataReaderStream(const DataReaderStream &) = delete;
                                DataReaderStream &operator=(const DataReaderStream &) = delete;
                                DataReaderStream &operator=(DataReaderStream &&) = delete;
                                /// @brief Typed bundle queue between the
                                ///        @c DataDepacketizerThread
                                ///        and @c RtpAggregatorThread —
                                ///        populated for the JSON
                                ///        metadata format.
                                UniquePtr<Queue<RxDataMessage>> payloadQueue;
                                /// @brief Typed bundle queue between the
                                ///        @c RtpAncDepacketizerThread
                                ///        and @c RtpAggregatorThread —
                                ///        populated for the RFC 8331
                                ///        ANC format.
                                UniquePtr<Queue<RxAncFrame>> ancPayloadQueue;
                };

                /// @brief Per-stream payload queue depths.  Sized by
                ///        the devplan's "latency budget × cadence"
                ///        rule for audio (100 ms × 1 ms cadence = 100
                ///        chunks); video is bounded at 4 frames so a
                ///        stuck aggregator surfaces back-pressure
                ///        rather than OOMing on 4K HEVC; data is 8
                ///        messages because data is sparse.
                static constexpr size_t VideoPayloadQueueDepth = 4;
                static constexpr size_t AudioPayloadQueueDepth = 100;
                static constexpr size_t DataPayloadQueueDepth = 8;

                Error configureVideoStream(const MediaIO::Config &cfg, const MediaDesc &mediaDesc);
                Error configureAudioStream(const MediaIO::Config &cfg, const MediaDesc &mediaDesc);
                Error configureDataStream(const MediaIO::Config &cfg);

                /// @brief Resolves the writer-side ts-refclk / mediaclk
                ///        config and stamps the result onto every active
                ///        writer @ref Stream entry.  Picks the localmac
                ///        autodetect default via @ref NetworkInterface
                ///        when @ref MediaConfig::RtpRefClock = @c Auto
                ///        and no PTP grandmaster is configured.
                void applyClockReferenceConfig(const MediaIO::Config &cfg);

                Error openStream(WriterStream &s, bool enableMulticastLoopback);
                Error openReaderStream(ReaderStream &s, bool enableMulticastLoopback);

                /**
                 * @brief Self-healing parameter-set injector for
                 *        H.264 / HEVC.
                 *
                 * Inspects the Annex-B bytes at @p data / @p size in
                 * preparation for sending: every parameter-set NAL
                 * (H.264 SPS / PPS, HEVC VPS / SPS / PPS) is copied
                 * into the per-stream cache so the most-recent copy
                 * is always available, and if the access unit is an
                 * IDR / IRAP that does not already carry the cached
                 * parameter sets, a fresh Annex-B blob is built that
                 * prepends them.
                 *
                 * The result lets a late-joining receiver decode
                 * starting at the next IDR / IRAP regardless of
                 * whether the upstream encoder is configured to
                 * repeat parameter sets — once the writer has seen
                 * the parameter sets even once, every IDR / IRAP it
                 * sends will be self-contained.
                 *
                 * @param data  Pointer to the Annex-B bytes (input).
                 * @param size  Byte count of the input.
                 * @param healed  Receives a freshly allocated Annex-B
                 *                blob when prepending was needed.
                 *                On entry it must be empty; on
                 *                successful return it is either
                 *                empty (no change required — the
                 *                caller should ship the original
                 *                bytes) or non-empty (the caller
                 *                must ship @p healed instead).
                 * @return @ref Error::Ok always; the helper is
                 *         best-effort and never blocks the send.
                 */
                Error injectParameterSets(const uint8_t *data, size_t size, Buffer &healed);

                /**
                 * @brief Paces the next video frame against
                 *        @ref _videoGate.
                 *
                 * No-op when no external clock is bound or the frame
                 * rate is unknown.  Returns @c true when the frame
                 * should be sent, @c false when the gate's verdict
                 * recommends dropping it (lag past the skip
                 * threshold).  Reanchor is logged but the frame
                 * still ships.  Clock failures are logged and the
                 * frame ships unpaced.
                 *
                 * @return @c true to send, @c false to drop.
                 */
                bool paceVideoFrame();

                // sendVideo / sendAudio / sendData were per-stream
                // strand-side helpers in the previous architecture.
                // They are gone in Phase 2; their packetization
                // logic lives in the per-stream
                // @c VideoPacketizerThread / @c AudioPacketizerThread
                // / @c DataPacketizerThread (declared as nested
                // classes inside @c rtpmediaio.cpp), and their wire-
                // pacing logic lives in the matching
                // @c VideoTxThread / @c AudioTxThread /
                // @c DataTxThread.

                /**
                 * @brief Refreshes @c s.streamClock from the
                 *        session's most-recent received SR if a fresh
                 *        one has arrived.
                 *
                 * Cheap to call on every incoming packet: takes the
                 * @ref RtpSession::receivedSr snapshot once and
                 * compares its @c arrivedAt against
                 * @c s.lastSrArrivedAt; only on change does it rebuild
                 * the @ref RtpStreamClock.  Sets @c s.hasSr the first
                 * time an SR lands so the wallclock-aligned reader
                 * paths can start consulting the clock instead of
                 * falling back to the legacy per-frame-index
                 * @c samplesPerFrame drain.
                 *
                 * @param s The reader stream to refresh.
                 */
                void refreshStreamClock(ReaderStream &s);

                /**
                 * @brief Converts a wallclock NTP value back to a
                 *        local steady @ref TimeStamp via the per-
                 *        session @c (steady, NTP) anchor pinned at
                 *        open time.
                 *
                 * Used when stamping the per-Frame
                 * @ref Frame::captureTime on the receive side: each
                 * Frame's wallclock NTP — derived from its video
                 * stream's @ref RtpStreamClock — needs to land in a
                 * @c MediaTimeStamp that downstream consumers can
                 * compare against locally-sampled timestamps.
                 *
                 * Returns @c TimeStamp() when the anchor has not yet
                 * been pinned (e.g. before the first @c executeCmd
                 * @c (Open) seeds it).
                 *
                 * @param ntp The NTP wallclock instant to convert.
                 */
                TimeStamp ntpToSteady(const NtpTime &ntp) const;

                // Reader path.
                Error applySdp(const SdpSession &sdp, MediaIO::Config &cfg, MediaDesc &mediaDesc);
                Error openAllReaders();
                void  pushReaderFrame(Frame frame);

                void  buildSdp();
                Error writeSdpFile(const String &path);

                /// @brief Common reset: tears down the
                ///        transport / session / payload / pointer
                ///        identity fields shared across modes.
                ///        Called by @ref resetWriterStream and
                ///        @ref resetReaderStream below.
                void resetStreamCommon(Stream &s);
                /// @brief Writer-mode reset: stops the packetizer +
                ///        TX threads, deletes them, then runs
                ///        @ref resetStreamCommon, then clears writer-
                ///        only stats / histograms.
                void resetWriterStream(WriterStream &s);
                /// @brief Reader-mode reset: stops the per-session
                ///        receive thread (via @c session->stopReceiving),
                ///        joins the per-stream depacketizer thread,
                ///        runs @ref resetStreamCommon, then clears
                ///        reader-only reassembly state and stats.
                void resetReaderStream(ReaderStream &s);
                void resetAll();

                /// @brief Builds the @ref RtcpSchedulerContext handed
                ///        to the scheduler at construction time.
                ///        Populates per-stream views from the
                ///        @ref _videos / @ref _audios / @ref _datas /
                ///        @ref _videoReaders / @ref _audioReaders /
                ///        @ref _dataReaders lists, the wire-silence
                ///        timeout, and an EoS callback that performs
                ///        the receiver-side cancel + depacketizer-stop
                ///        cascade when the watchdog trips.  Called
                ///        once at @c executeCmd(Open) time after the
                ///        sessions / depacketizers are wired up.
                RtcpSchedulerContext buildRtcpSchedulerContext();

                // Per-mode stream lists — writer-mode populates the
                // first three, reader-mode the last three.  Reader
                // and writer modes are mutually exclusive on a
                // single @c RtpMediaIO instance, so only one set is
                // populated at any given time.  Each kind is a list
                // (instead of a single slot) so the routing, SDP
                // builder, RTCP scheduler, and per-stream threads
                // can iterate uniformly regardless of how many
                // streams of each kind a session carries.  Today's
                // @c configureVideoStream / @c configureAudioStream /
                // @c configureDataStream populate one entry per kind
                // on the matching list (per @c _readerMode); the
                // collective shape is ready for multi-stream config
                // that lands later.
                List<VideoStream>       _videos;
                List<AudioStream>       _audios;
                List<DataStream>        _datas;
                List<VideoReaderStream> _videoReaders;
                List<AudioReaderStream> _audioReaders;
                List<DataReaderStream>  _dataReaders;

                // Transport-global config
                SocketAddress _localAddress;
                String        _sessionName;
                String        _sessionOrigin;
                String        _multicastInterface;
                int           _multicastTTL = 0;
                int           _recvBufferBytes = 0;
                int           _sendBufferBytes = 0;
                Enum          _pacingMode;
                Enum          _dataFormat;

                // Runtime
                FrameRate  _frameRate;
                FrameCount _frameCount{0};
                FrameCount _framesSent{0};

                // Mode
                bool _readerMode = false;

                // RFC 4175 wire-format PixelFormat.  When the input
                // pixel format doesn't match what RFC 4175 expects
                // on the wire (e.g. YUYV vs UYVY), the
                // VideoPacketizerThread calls
                // UncompressedVideoPayload::convert() to the wire
                // format before packing.  Invalid means no conversion
                // needed.
                PixelFormat _videoWirePixelFormat;

                // Reader runtime
                Queue<Frame> _readerQueue;
                int               _readerMaxDepth = 4;
                int               _readerJitterMs = 50;
                int               _wireSilenceTimeoutMs = 0; ///< @brief 0 = derive as 10 × _rtcpIntervalMs.
                bool              _videoWatchdogEnabled = false; ///< @brief Off by default — see @ref MediaConfig::RtpVideoWatchdogEnabled.
                FrameCount        _readerFramesReceived{0};
                // Set by cancelBlockingWork() so the executeCmd(Read)
                // pop loop can break out of its short-timeout polling
                // when MediaIO::close is unwinding the strand.  Cleared
                // at every Open so a closed-then-reopened RtpMediaIO
                // doesn't carry the previous instance's cancellation
                // forward.
                std::atomic<bool> _readCancelled{false};

                /**
                 * @brief Reader-side anchor for NTP-↔-steady mapping.
                 *
                 * Captured once at open time as @c (steadyNow, ntpNow).
                 * Every subsequent emit computes
                 * @c steadyAnchor @c + @c (frameNtp @c - @c ntpAnchor)
                 * to convert a stream's wallclock NTP capture instant
                 * to a local steady-clock @ref TimeStamp for
                 * @ref Frame::captureTime stamping.
                 *
                 * Distinct from the per-stream @c StreamAnchor on
                 * @ref RtpDepacketizerThread, which interpolates
                 * per-frame @c captureTime when no SR has yet been
                 * observed.  Both are static configuration captured
                 * at open time.
                 */
                TimeStamp _readerSteadyAnchor;
                NtpTime   _readerNtpAnchor;
                bool      _readerHasAnchor = false;
                /// @brief Steady-clock instant at which
                ///        @c executeCmd(MediaIOCommandOpen) entered
                ///        in reader mode.  Used to compute the
                ///        @c StatsRxFirstSrLatencyUs reading.  Reset
                ///        on @ref resetAll.
                TimeStamp _openedAt;
                /// @brief Owned per-RtpMediaIO aggregator thread.
                ///        Spawned on reader-mode open after the
                ///        depacketizers; joined on close before the
                ///        depacketizers (so the cancel path doesn't
                ///        lose in-flight Frames).  Concrete class
                ///        definition lives in @c rtpmediaio.cpp.
                UniquePtr<RtpAggregatorThread> _aggregator;

                // SDP — the active session description built at
                // open time.  Reader mode leaves this empty (the
                // reader consumes an externally-supplied SDP via
                // RtpSdp); writer mode populates it so the
                // GetSdp params command and the RtpSaveSdpPath
                // export path can serve it.
                SdpSession _sdpSession;
                String     _sdpPath;

                // RTCP — one scheduler thread per RtpMediaIO.  Wakes
                // every @c _rtcpIntervalMs and emits an SR + SDES
                // compound on every active writer stream's
                // RtpSession plus an RR for every active reader
                // stream.  The wallclock anchor captured at open
                // time is shared across all streams so a single
                // receiver-side observation of any stream's first
                // SR is sufficient for cross-stream correlation.
                // Disabled in reader mode (we are not a sender
                // there) and when @c MediaConfig::RtpRtcpEnabled
                // is @c false.  The scheduler itself lives in
                // @c rtcpscheduler.h; this io populates an
                // @c RtcpSchedulerContext at @c executeCmd(Open)
                // time and hands it to the scheduler's constructor.
                UniquePtr<RtcpScheduler> _rtcpScheduler;
                bool           _rtcpEnabled = true;
                int            _rtcpIntervalMs = 5000;
                String         _rtcpCname;

                // Process-local monotonic counter that gives every
                // RtpMediaIO a distinct id within one process — see
                // objectId() and buildDefaultCname().  Starts at 0,
                // first instance gets 1.
                static Atomic<uint64_t> _nextObjectId;
                uint64_t                _objectId = 0;

                // SR-anchor seeding gate.  At @c openStream time
                // every active session is anchored with
                // @c (NtpTime::now(), 0) so an SR can be emitted
                // even before the first frame arrives (better than
                // a structurally-invalid SR for late receivers).
                // The very first @c executeCmd(Write) refines the
                // anchor from the Frame's @ref Frame::captureTime —
                // we use a compare-exchange on this flag so even
                // future changes that take the Write path off the
                // single-threaded strand still seed exactly once
                // per opening.
                Atomic<bool> _anchorSeeded;

                // External writer-mode video pacing — null clock means
                // the upstream pump's natural cadence is the only
                // timing source.  Set via
                // executeCmd(MediaIOCommandSetClock); read on the
                // dedicated worker thread in executeCmd(Write) (same
                // thread as the setter, no synchronization required).
                // Audio is not paced separately by the gate — AES67
                // packet timing is governed by the per-packet RTP
                // timestamp stride that the audio FIFO maintains.
                PacingGate _videoGate;

                // Out-of-band parameter sets for H.264 / HEVC writers,
                // populated lazily by injectParameterSets() the first
                // time a complete set passes through the bitstream
                // (typically frame 0).  Stored as the base64-encoded
                // SDP-ready string — for H.264 it's
                // @c "<sps-base64>,<pps-base64>" (matches RFC 6184
                // @c sprop-parameter-sets); for HEVC it's the three
                // separate sprop-vps / -sps / -pps strings (the bool
                // tracks whether they have been populated).  Empty
                // until the encoder's first IDR / IRAP flows through;
                // once populated, buildSdp() embeds them in the
                // @c a=fmtp line so a receiver that reads the SDP
                // (e.g. ffplay) can populate its decoder's extradata
                // before the first packet arrives.  ffmpeg's H.264 RTP
                // demuxer requires this — without it, a receiver that
                // joins after the first IDR fails its initial codec
                // probe and never recovers, even with in-band
                // parameter sets repeated on every IDR.
                String _h264SpropParameterSets;
                String _h265SpropVps;
                String _h265SpropSps;
                String _h265SpropPps;

                /**
                 * @brief Updates @c _h264SpropParameterSets /
                 *        @c _h265Sprop* from the cached parameter
                 *        sets and, if @p path is set and the SDP
                 *        sprop value changed, rewrites the SDP file.
                 *
                 * Called from @ref injectParameterSets the first
                 * time a complete parameter-set set is observed for
                 * the active codec, so the SDP file ends up
                 * containing @c sprop-parameter-sets (H.264) or
                 * @c sprop-vps / @c sprop-sps / @c sprop-pps (HEVC)
                 * before any external process reads it.
                 */
                void refreshSdpSprop();
};

/**
 * @brief @ref MediaIOFactory for the RTP backend.
 * @ingroup proav
 */
class RtpFactory : public MediaIOFactory {
        public:
                RtpFactory() = default;

                String name() const override { return String("Rtp"); }
                String displayName() const override { return String("RTP Stream"); }
                String description() const override {
                        return String("RTP video + audio + metadata reader / writer "
                                      "(MJPEG / JPEG XS / H.264 / H.265 / raw / L16 / JSON)");
                }
                // An SDP file on disk implies the Rtp reader; writers
                // never open via a filesystem path, but extension-based
                // dispatch still uses this list so `-i foo.sdp` in
                // mediaplay picks the Rtp backend automatically.
                StringList extensions() const override {
                        return {String("sdp")};
                }

                bool canBeSource() const override { return true; }
                bool canBeSink() const override { return true; }

                bool            canHandleDevice(IODevice *device) const override;
                Config::SpecMap configSpecs() const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK