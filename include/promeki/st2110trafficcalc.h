/**
 * @file      st2110trafficcalc.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK && PROMEKI_ENABLE_PROAV
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/duration.h>
#include <promeki/enums_rtp.h>
#include <promeki/enums_st2110.h>
#include <promeki/error.h>
#include <promeki/framerate.h>
#include <promeki/pixelformat.h>
#include <promeki/st2110tx.h>
#include <promeki/st2110video.h>
#include <promeki/string.h>
#include <promeki/videoformat.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief End-to-end SMPTE ST 2110-21 traffic-shaping calculator.
 * @ingroup network
 *
 * Given the three inputs an operator actually has on hand — a
 * @ref VideoFormat (raster + frame rate + scan mode), a wire
 * @ref PixelFormat (or an explicit @ref St2110Sampling /
 * @ref St2110Depth pair), and the desired ST 2110-21 sender class
 * (@ref RtpSenderType::TypeN "Narrow", @ref RtpSenderType::TypeNL
 * "Narrow Linear", or @ref RtpSenderType::TypeW "Wide") — this class
 * derives every quantity the standard's §6 / §7 model is expressed
 * in and packages them into a single @ref Result.
 *
 * It is a thin aggregator: the per-formula math lives in
 * @ref St2110Tx (the §7.1 / §7.4 primitives) and the wire-format
 * pgroup table lives in @ref St2110Video.  The value this class adds
 * is computing the two inputs those primitives can't derive on their
 * own —
 *
 *  - **N_PACKETS** — the packets-per-frame count, obtained from the
 *    @ref St2110Video pgroup table and a packetization model
 *    (@ref PacketModel: MTU, IP/UDP/RTP overhead, GPM vs BPM).
 *  - **R_ACTIVE** — the active-to-total line ratio, selected per
 *    ST 2110-21:2017 §6.3: a fixed @c 1080/1125 for every
 *    progressive format and the §6.3.3 Table&nbsp;1 ratios
 *    (@c 487/525, @c 576/625, @c 1080/1125) for interlaced / PsF.
 *
 * — and then driving the @ref St2110Tx primitives with them to
 * produce T_FRAME, T_RS (gapped and linear), TRO_DEFAULT, VRX_FULL,
 * C_MAX, β, the nominal packet rate R_NOMINAL, T_DRAIN, and the SDP
 * @c TP / @c TROFF media-type parameters.
 *
 * @par N_PACKETS packetization model
 *
 * The packet count is derived analytically, not by packing a real
 * frame.  Two models are offered, matching @ref RtpPayloadRawVideo:
 *
 *  - **GPM** (default) — General Packing Mode, modelled as
 *    line-aligned single-SRD packets: each scan line (or, for 4:2:0,
 *    each pgroup-paired row band) fragments into
 *    @c ceil(line_octets / usable_payload) packets, where
 *    @c usable_payload is the RTP payload budget less the 2-octet
 *    Extended Sequence Number and one 6-octet SRD header, floored to
 *    a whole number of pgroups.  This reproduces the standard's own
 *    worked example ("8 packets is just over 2 lines of 4:2:2/10
 *    samples in a 1920 sample width format" → 4320 packets/frame for
 *    1080-line 4:2:2/10).  Multi-line SRD coalescing for very small
 *    rasters is intentionally not modelled.
 *  - **BPM** — Block Packing Mode, modelled as contiguous fixed-size
 *    payloads of @ref RtpPayloadRawVideo::bpmTargetPayloadSize octets
 *    (a multiple of 180, ≤ 1260; Extended UDP is forbidden in BPM).
 *
 * The MTU defaults to 1500 (the Standard UDP Size Limit, where
 * MAXIP = 1500); pass a larger value to model the Extended UDP Size
 * Limit / jumbo frames.
 *
 * @par TRO_DEFAULT edition
 *
 * TRO_DEFAULT is delegated to @ref St2110Tx::troDefault, which
 * implements the ST 2110-21:2022 form (N_PACKETS-dependent).  This
 * keeps the calculator consistent with the rest of the library's
 * RTP / SDP stack.  The VRX_FULL, C_MAX, T_RS and β values are
 * identical across the 2017 and 2022 editions.
 *
 * @par Example
 * @code
 * VideoFormat fmt(VideoFormat::Smpte1080p59_94);
 * St2110TrafficCalc calc(fmt, St2110Sampling::YCbCr422,
 *                        St2110Depth::Bits10, RtpSenderType::TypeN);
 * if (calc.isValid()) {
 *     const auto &r = calc.result();
 *     promekiInfo("N_PACKETS=%d  VRX_FULL=%lld  CMAX=%d",
 *                 r.packetsPerFrame, (long long)r.vrxFull, r.cmax);
 *     promekiInfo("%s", calc.result().toString().cstr());
 * }
 * @endcode
 */
class St2110TrafficCalc {
        public:
                /**
                 * @brief Controls how N_PACKETS is derived from the
                 *        raster and pgroup.
                 *
                 * The defaults model the Standard UDP Size Limit
                 * (MTU = 1500, MAXIP = 1500) with IPv4 + UDP + RTP
                 * framing in General Packing Mode.
                 */
                struct PacketModel {
                                /// @brief §6.3 packing mode used to derive N_PACKETS.
                                St2110PackingMode packingMode = St2110PackingMode::Gpm;
                                /// @brief IP MTU in octets — also taken as MAXIP for
                                ///        the VRX_FULL minimum term.  1500 = Standard
                                ///        UDP Size Limit; larger models Extended UDP.
                                int mtu = 1500;
                                /// @brief IP header octets (IPv4 = 20, IPv6 = 40).
                                int ipHeaderBytes = 20;
                                /// @brief UDP header octets.
                                int udpHeaderBytes = 8;
                                /// @brief RTP fixed-header octets.
                                int rtpHeaderBytes = 12;
                };

                /**
                 * @brief Every value the ST 2110-21 model is expressed in.
                 *
                 * @ref error is @c Error::Ok on a successful compute;
                 * any other code leaves the numeric fields at their
                 * default-constructed values and @ref isValid returns
                 * @c false.
                 */
                struct Result {
                                /// @brief Ok on success; otherwise the reason the compute failed.
                                Error error = Error::Invalid;

                                // ---- Resolved inputs --------------------------------
                                VideoFormat       videoFormat;  ///< Echo of the input format.
                                St2110Sampling    sampling;     ///< Resolved wire sampling.
                                St2110Depth       depth;        ///< Resolved wire depth.
                                RtpSenderType     senderType;   ///< Requested sender class.
                                PacketModel       packetModel;  ///< Packetization model used.

                                // ---- Raster + timing --------------------------------
                                uint32_t          width = 0;          ///< Active raster width (pixels).
                                uint32_t          height = 0;         ///< Active raster height (pixels).
                                VideoScanMode     scanMode;           ///< Scan mode of the format.
                                bool              progressive = false;///< True for progressive (non-PsF).
                                int               activeLines = 0;    ///< R_ACTIVE numerator (§6.3).
                                int               totalLines = 0;     ///< R_ACTIVE denominator (§6.3).
                                double            activeRatio = 0.0;  ///< R_ACTIVE = activeLines/totalLines.
                                FrameRate         frameRate;          ///< Frame rate (exact rational).
                                Duration          frameInterval;      ///< T_FRAME = 1 / frame_rate.

                                // ---- Packetization ----------------------------------
                                St2110Video::Pgroup pgroup;             ///< pgroup descriptor (octets/pixels).
                                int               rowsPerSrd = 1;       ///< Image rows per SRD (2 for 4:2:0).
                                int64_t           octetsPerLine = 0;    ///< pgroup octets per SRD-row band.
                                int64_t           activeOctetsPerFrame = 0; ///< Active pixel octets per frame.
                                int               payloadDataPerPacket = 0;  ///< Max pgroup data octets per packet.
                                int               packetsPerLine = 0;   ///< Packets per SRD-row band.
                                int               packetsPerFrame = 0;  ///< N_PACKETS.
                                int               packetSizeBytes = 0;  ///< Typical full IP packet size (octets).

                                // ---- Rates ------------------------------------------
                                double            packetRate = 0.0;    ///< R_NOMINAL = N_PACKETS / T_FRAME (pps).
                                double            mediaRateBps = 0.0;  ///< Active payload bit rate (bit/s).
                                double            wireRateBps = 0.0;   ///< Approx. IP wire bit rate (bit/s).
                                double            beta = St2110Tx::Beta;///< Burst safety factor β (1.10).
                                Duration          tDrain;              ///< T_DRAIN = (T_FRAME/N_PACKETS) × (1/β).

                                // ---- Packet Read Schedule ---------------------------
                                Duration          trsGapped;           ///< T_RS for the gapped PRS.
                                Duration          trsLinear;           ///< T_RS for the linear PRS.
                                Duration          trs;                 ///< T_RS active for this sender type.
                                Duration          troDefault;          ///< TRO_DEFAULT (2022 form).

                                // ---- Virtual Receiver Buffer / burst ----------------
                                int64_t           vrxFull = 0;         ///< VRX_FULL (packets).
                                int               cmax = 0;            ///< C_MAX (packets).
                                /// @brief Derived (informative): max time the sender may
                                ///        run ahead of the Packet Read Schedule,
                                ///        @c VRX_FULL × T_RS.  The §6.6.2 buffer holds
                                ///        @c VRX_FULL packets draining one per @c T_RS,
                                ///        so a conformant sender emits packet @c j within
                                ///        @c [TPR_j − VRX_FULL·T_RS, TPR_j].  ST 2110-21
                                ///        specifies no numeric jitter range (§7.2.1
                                ///        leaves receiver tolerance to the implementer);
                                ///        this is the sender-side timing budget the
                                ///        traffic model does bound.
                                Duration          vrxTimingWindow;
                                /// @brief Derived (informative): max sustained burst
                                ///        duration the §6.6.1 leaky-bucket model permits,
                                ///        @c C_MAX × T_DRAIN.
                                Duration          cmaxBurstWindow;

                                // ---- SDP signalling ---------------------------------
                                String            tpParam;             ///< @c TP value (2110TPN / NL / W).
                                int64_t           troffMicroseconds = 0;///< @c TROFF in microseconds.

                                /// @brief True when the compute succeeded.
                                bool isValid() const { return error.isOk(); }

                                /**
                                 * @brief Renders a human-readable multi-line report
                                 *        of every computed value.
                                 */
                                String toString() const;
                };

                /** @brief Default-constructs a calculator with an invalid result. */
                St2110TrafficCalc() = default;

                /**
                 * @brief Returns the default packetization model
                 *        (Standard UDP Size Limit, GPM).
                 *
                 * Used as the default argument for the constructors and
                 * @ref compute overloads; defined out-of-line to avoid
                 * value-initialising @ref PacketModel inside the
                 * still-incomplete enclosing class.
                 */
                static PacketModel defaultModel();

                /**
                 * @brief Computes the model from an explicit (sampling, depth) pair.
                 *
                 * @param format     Input video format.
                 * @param sampling   ST 2110-20 wire sampling.
                 * @param depth      ST 2110-20 wire depth.
                 * @param senderType Sender class (TypeN / TypeNL / TypeW).
                 * @param model      Packetization model (defaults to Standard UDP + GPM).
                 */
                St2110TrafficCalc(const VideoFormat &format, const St2110Sampling &sampling,
                                  const St2110Depth &depth, const RtpSenderType &senderType,
                                  const PacketModel &model = defaultModel());

                /**
                 * @brief Computes the model from a @ref PixelFormat.
                 *
                 * The PixelFormat is mapped to its ST 2110-20 wire
                 * (sampling, depth) via
                 * @ref St2110Video::bridgeForPixelFormat; a PixelFormat
                 * with no wire equivalent yields an invalid result with
                 * @c Error::FormatMismatch.
                 *
                 * @param format      Input video format.
                 * @param pixelFormat Wire / source pixel format.
                 * @param senderType  Sender class (TypeN / TypeNL / TypeW).
                 * @param model       Packetization model.
                 */
                St2110TrafficCalc(const VideoFormat &format, const PixelFormat &pixelFormat,
                                  const RtpSenderType &senderType,
                                  const PacketModel &model = defaultModel());

                /** @brief Returns the computed result. */
                const Result &result() const { return _result; }

                /** @brief True when the computed result is valid. */
                bool isValid() const { return _result.isValid(); }

                /** @brief Returns the error from the most recent compute. */
                const Error &error() const { return _result.error; }

                /**
                 * @brief Computes a @ref Result from an explicit
                 *        (sampling, depth) pair without constructing a
                 *        calculator instance.
                 */
                static Result compute(const VideoFormat &format, const St2110Sampling &sampling,
                                      const St2110Depth &depth, const RtpSenderType &senderType,
                                      const PacketModel &model = defaultModel());

                /**
                 * @brief Computes a @ref Result from a @ref PixelFormat
                 *        without constructing a calculator instance.
                 */
                static Result compute(const VideoFormat &format, const PixelFormat &pixelFormat,
                                      const RtpSenderType &senderType,
                                      const PacketModel &model = defaultModel());

        private:
                Result _result;
};

inline St2110TrafficCalc::St2110TrafficCalc(const VideoFormat &format,
                                            const St2110Sampling &sampling,
                                            const St2110Depth &depth,
                                            const RtpSenderType &senderType,
                                            const PacketModel &model)
    : _result(compute(format, sampling, depth, senderType, model)) {}

inline St2110TrafficCalc::St2110TrafficCalc(const VideoFormat &format,
                                            const PixelFormat &pixelFormat,
                                            const RtpSenderType &senderType,
                                            const PacketModel &model)
    : _result(compute(format, pixelFormat, senderType, model)) {}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK && PROMEKI_ENABLE_PROAV
