/**
 * @file      mediaiotask_rtp.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediaiotask.h>
#include <promeki/socketaddress.h>
#include <promeki/pixeldesc.h>
#include <promeki/audiodesc.h>
#include <promeki/audiobuffer.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class RtpSession;
class RtpPayload;
class UdpSocketTransport;
class SdpSession;

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
 *   input @ref PixelDesc is in the JPEG family, or RFC 4175 raw
 *   video via @ref RtpPayloadRawVideo for 8-bit interleaved
 *   uncompressed formats (first pass; proper ST 2110-20 pgroup
 *   sizing for 10/12-bit lands later).  @b Pick @b the @b right
 *   @b JPEG @b sub-format: JFIF / RFC 2435 has no standard way to
 *   signal YCbCr matrix (Rec.601 vs Rec.709) or range (limited
 *   vs full) inside the bitstream, so a strict JFIF consumer
 *   like ffplay always decodes with Rec.601 full-range math.
 *   For correct playback in ffplay / browsers / any libjpeg-based
 *   receiver, use @c PixelDesc::JPEG_YUV8_422_Rec601_Full or
 *   @c PixelDesc::JPEG_YUV8_420_Rec601_Full.  Broadcast / SDI /
 *   ST 2110 pipelines that expect limited-range Rec.709 JPEG
 *   should use the unsuffixed @c JPEG_YUV8_422_Rec709 variant
 *   (the library-wide YCbCr default).  See the
 *   @ref pixeldesc.h enum documentation for the full 2 × 2 × 2
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
 *   conversion belongs in @ref MediaIOTask_Converter.  L24 and
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
 * Only @c MediaIO::Writer is supported in this first pass.  Reader
 * mode (receiving an RTP stream into frames) is tracked separately
 * and will reuse the same @ref RtpSession / @ref RtpPayload stack
 * once the reassembly bookkeeping is in place.  @c ReadWrite is
 * explicitly rejected — an RTP sink and an RTP source are
 * conceptually different streams and should not share a MediaIO.
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
 *                   PixelDesc(PixelDesc::JPEG_YUV8_422_Rec601_Full)));
 * mediaDesc.audioList().pushToBack(
 *         AudioDesc(AudioDesc::PCMI_S16LE, 48000.0f, 2));
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
 * io->setMediaDesc(mediaDesc);
 * io->open(MediaIO::Writer);
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
                /** @brief int64_t — total frames dropped due to transport back-pressure. */
                static inline const MediaIOStats::ID StatsFramesDropped{"FramesDropped"};

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
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandParams &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

                /**
                 * @brief Per-stream RTP state.
                 *
                 * Groups the transport, session, and payload handler
                 * for one of the three stream kinds so the per-frame
                 * dispatch in @c executeCmd(MediaIOCommandWrite&) can
                 * iterate over active streams uniformly.
                 */
                struct Stream {
                        UdpSocketTransport *transport = nullptr;
                        RtpSession         *session   = nullptr;
                        RtpPayload         *payload   = nullptr;
                        SocketAddress       destination;
                        uint8_t             payloadType = 0;
                        uint32_t            clockRate   = 90000;
                        int                 dscp        = 0;
                        uint32_t            ssrc        = 0;
                        int64_t             packetsSent = 0;
                        int64_t             bytesSent   = 0;
                        String              mediaType;    ///< @brief "video", "audio", "data"
                        String              rtpmap;       ///< @brief SDP a=rtpmap:... value
                        String              fmtp;         ///< @brief SDP a=fmtp:... value, optional
                        bool                active = false;
                };

                Error configureVideoStream(const MediaIO::Config &cfg,
                                           const MediaDesc &mediaDesc);
                Error configureAudioStream(const MediaIO::Config &cfg,
                                           const MediaDesc &mediaDesc);
                Error configureDataStream(const MediaIO::Config &cfg);

                Error openStream(Stream &s, bool enableMulticastLoopback);

                Error sendVideo(const Image &image);
                Error sendAudio(const Audio &audio);
                Error sendData(const Metadata &metadata);

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
                int64_t         _frameCount = 0;
                int64_t         _framesSent = 0;
                int64_t         _framesDropped = 0;

                // SDP
                String          _sdpText;
                String          _sdpPath;
};

PROMEKI_NAMESPACE_END
