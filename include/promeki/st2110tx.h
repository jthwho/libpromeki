/**
 * @file      st2110tx.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/duration.h>
#include <promeki/enums.h>
#include <promeki/framerate.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Pure-math primitives for SMPTE ST 2110-21 sender shaping.
 * @ingroup network
 *
 * ST 2110-21:2022 §7.1 defines three sender-side traffic-shaping
 * profiles (Type N gapped narrow, Type NL linear narrow, Type W
 * linear wide) by bounding the worst-case receiver virtual receive
 * buffer occupancy (VRX_FULL) and the maximum sustained burst
 * (CMAX) a conformant sender may emit.  This class collects the
 * formulas in one place — every input is explicit, every output
 * is computed from RFC-defined coefficients.  No runtime state.
 *
 * The integration points are:
 *
 *  - **SDP signalling** (Phase E21 step A): @c TP=2110TPN /
 *    @c 2110TPNL / @c 2110TPW.  @c TROFF=&lt;ticks&gt; when the
 *    sender pins a non-default TR_OFFSET.  @c CMAX=&lt;packets&gt;
 *    informational.
 *  - **PTP-anchored TX-time deadlines** (Phase E21 step B):
 *    @ref tprJ gives the absolute release instant of packet @c j
 *    of a frame, anchored at @c T_VD on the SMPTE Epoch grid.
 *    Drives per-packet @c SO_TXTIME stamping in
 *    @c RtpVideoTxThread.
 *  - **Runtime conformance monitoring** (Phase E21 step C):
 *    @c St2110VrxMonitor consumes a stream of @c (timestamp,
 *    size) samples and checks them against
 *    @ref vrxFullBytes / @ref cmaxPackets bounds.
 *
 * Units: all durations are @c Duration values (nanosecond-precision);
 * VRX_FULL is in bytes, CMAX is in packets.  @c R_ACTIVE is the
 * active/total line ratio (e.g. 1080/1125 = 0.96 for progressive
 * HD; 1920/2200 horizontal × 1080/1125 vertical is irrelevant here
 * — only vertical R_ACTIVE matters for ST 2110-21's flow model).
 *
 * @par Standards references
 *  - ST 2110-21:2022 §7.1 (sender model), §7.4 (TRO_DEFAULT).
 *  - Annex A (informative) — worked examples that the unit tests
 *    in @c tests/unit/network/st2110tx.cpp verify against.
 */
class St2110Tx {
        public:
                /// @brief Burst safety factor β (ST 2110-21 §7.1).
                ///        Constant 1.10 for all three sender types.
                static constexpr double Beta = 1.10;

                /**
                 * @brief Active line ratio for a SMPTE format —
                 *        active lines / total lines.
                 *
                 * The vertical fraction of the SMPTE wire format
                 * that carries pixel data; the rest is blanking.
                 * Drives Type N / NL's gapped pacing window
                 * (T_RS_g = T_FRAME × R_ACTIVE / N_PACKETS).
                 *
                 * @param activeLines  Active line count
                 *                     (e.g. 1080).
                 * @param totalLines   Total line count including
                 *                     blanking (e.g. 1125).
                 * @return Active/total ratio, or 0 when either
                 *         input is non-positive.
                 */
                static double activeRatio(int activeLines, int totalLines) {
                        if (activeLines <= 0 || totalLines <= 0) return 0.0;
                        return static_cast<double>(activeLines) /
                               static_cast<double>(totalLines);
                }

                /**
                 * @brief Gapped PRS interval @c T_RS_g (§7.1).
                 *
                 * @c T_RS_g = T_FRAME × R_ACTIVE / N_PACKETS.
                 * Drives Type N narrow-gapped pacing — packets
                 * land only inside the active portion of the
                 * frame interval.
                 *
                 * @param packetsPerFrame N_PACKETS (must be &gt; 0).
                 * @param frameInterval   T_FRAME = 1 / frame_rate.
                 * @param activeRatio     R_ACTIVE in [0, 1].
                 * @return T_RS_g as a @ref Duration, or
                 *         @c Duration::zero on invalid input.
                 */
                static Duration trsGapped(int packetsPerFrame,
                                          const Duration &frameInterval,
                                          double          activeRatio);

                /**
                 * @brief Linear PRS interval @c T_RS_l (§7.1).
                 *
                 * @c T_RS_l = T_FRAME / N_PACKETS.  Drives Type NL
                 * and Type W linear pacing — packets are spread
                 * across the entire frame interval.
                 *
                 * @param packetsPerFrame N_PACKETS.
                 * @param frameInterval   T_FRAME.
                 * @return T_RS_l, or @c Duration::zero on invalid
                 *         input.
                 */
                static Duration trsLinear(int packetsPerFrame,
                                          const Duration &frameInterval);

                /**
                 * @brief @c T_VD = N × T_FRAME + TR_OFFSET (§7.1 / §7.4).
                 *
                 * Absolute wallclock instant of frame @p frameIndex 's
                 * first sample on the SMPTE Epoch grid.  Caller
                 * supplies the SMPTE-Epoch-aligned frame-zero anchor
                 * (typically from @ref RtpMediaClock::tvdUtcNs); this
                 * helper adds the TR_OFFSET shift.
                 *
                 * @param frameAnchorUtcNs   T_VD(0) — UTC ns of
                 *                           frame 0's first sample.
                 * @param frameIndex         Zero-based frame index N.
                 * @param frameInterval      T_FRAME.
                 * @param trOffset           TR_OFFSET (default
                 *                           @c Duration::zero).
                 * @return UTC ns of frame N's first sample, or 0
                 *         when @p frameAnchorUtcNs is non-positive.
                 */
                static int64_t tvdUtcNs(int64_t frameAnchorUtcNs, int64_t frameIndex,
                                        const Duration &frameInterval,
                                        const Duration &trOffset = Duration::zero());

                /**
                 * @brief Per-packet release instant @c TPR_j (§7.1).
                 *
                 * @c TPR_j = T_VD + j × T_RS.  Absolute wallclock
                 * instant at which packet @p j of the current frame
                 * is permitted to leave the sender.  Drives the
                 * per-packet @c SO_TXTIME deadline in the video TX
                 * thread.
                 *
                 * @param tvdUtcNs    @c T_VD for the current frame
                 *                    (from @ref tvdUtcNs).
                 * @param packetIndex Zero-based packet index @c j
                 *                    within the current frame.
                 * @param trs         @c T_RS — gapped or linear
                 *                    depending on sender type.
                 * @return UTC ns release instant for packet @c j,
                 *         or 0 when @p tvdUtcNs is non-positive.
                 */
                static int64_t tprJUtcNs(int64_t tvdUtcNs, int packetIndex, const Duration &trs);

                /**
                 * @brief VRX_FULL bound for narrow senders (§7.1).
                 *
                 * @c VRX_FULL = max(1500 × 8 / MAXUDP,
                 *                     N_PACKETS / (27000 × T_FRAME)).
                 *
                 * The receiver's worst-case occupancy budget in
                 * bytes.  Type N and Type NL share the same VRX
                 * formula — only their pacing model (gapped vs
                 * linear) differs.
                 *
                 * @param packetsPerFrame N_PACKETS.
                 * @param maxUdpBytes     MAXUDP (must be &gt; 0).
                 * @param frameInterval   T_FRAME (must have positive
                 *                        @c microseconds()).
                 * @return VRX_FULL in bytes, or 0 on invalid input.
                 */
                static int64_t vrxFullNarrowBytes(int packetsPerFrame, int maxUdpBytes,
                                                  const Duration &frameInterval);

                /**
                 * @brief CMAX bound for narrow senders (§7.1).
                 *
                 * @c CMAX = max(4, N_PACKETS / (43200 × R_ACTIVE ×
                 *                     T_FRAME)).
                 *
                 * Maximum sustained burst the sender may emit, in
                 * packets.  Floor of 4.
                 *
                 * @param packetsPerFrame N_PACKETS.
                 * @param activeRatio     R_ACTIVE in (0, 1].
                 * @param frameInterval   T_FRAME.
                 * @return CMAX in packets, or 0 on invalid input.
                 */
                static int cmaxNarrowPackets(int packetsPerFrame, double activeRatio,
                                             const Duration &frameInterval);

                /**
                 * @brief VRX_FULL bound for wide senders (§7.1).
                 *
                 * @c VRX_FULL = max(1500 × 720 / MAXUDP,
                 *                     N_PACKETS / (300 × T_FRAME)).
                 *
                 * Significantly larger than the narrow bound —
                 * absorbs the bursty arrivals a non-paced sender
                 * generates.
                 *
                 * @param packetsPerFrame N_PACKETS.
                 * @param maxUdpBytes     MAXUDP.
                 * @param frameInterval   T_FRAME.
                 * @return VRX_FULL in bytes, or 0 on invalid input.
                 */
                static int64_t vrxFullWideBytes(int packetsPerFrame, int maxUdpBytes,
                                                const Duration &frameInterval);

                /**
                 * @brief CMAX bound for wide senders (§7.1).
                 *
                 * @c CMAX = max(16, N_PACKETS / (21600 × T_FRAME)).
                 * Floor of 16.
                 *
                 * @param packetsPerFrame N_PACKETS.
                 * @param frameInterval   T_FRAME.
                 * @return CMAX in packets, or 0 on invalid input.
                 */
                static int cmaxWidePackets(int packetsPerFrame, const Duration &frameInterval);

                /**
                 * @brief Resolves sender type from pacing mode (§7.1
                 *        realistic deliverable matrix).
                 *
                 * Linux deliverable map:
                 *  - @c TxTime  → @c TypeNL (per-packet TXTIME
                 *                deadlines, sub-µs precision via NIC).
                 *  - @c KernelFq → @c TypeW  (byte-rate cap, fq qdisc,
                 *                wide-class bursts).
                 *  - @c Userspace → @c TypeW (cadence sleep_until,
                 *                wide-class behaviour).
                 *  - @c None / Burst → @c Unknown (no pacing, can't
                 *                claim a type honestly).
                 *  - @c Auto → @c TypeW (the auto-resolved default
                 *                lands on kernel fq, so wide).
                 *
                 * Type N is intentionally absent — true Type N
                 * requires DPDK / dedicated NIC pacing infrastructure
                 * outside the library's scope.
                 *
                 * @param pacingMode One of @ref RtpPacingMode 's
                 *                   enumerated values.
                 * @return Auto-resolved @ref RtpSenderType, or
                 *         @c Unknown when @p pacingMode is invalid.
                 */
                static RtpSenderType resolveSenderType(const RtpPacingMode &pacingMode);

                /**
                 * @brief @c TRO_DEFAULT per ST 2110-21 §7.4.
                 *
                 * The §7.4 expression for the default per-stream
                 * timing offset (when the sender has not pinned an
                 * explicit @c TROFF) varies by sender type:
                 *
                 *  - Type N / NL — @c (43680 × T_FRAME - ((T_FRAME
                 *    - VRX_FULL) × R_ACTIVE × T_FRAME)) /
                 *    @c (1125 × N_PACKETS) ... (§7.4 narrow form).
                 *  - Type W      — @c (1080 × T_FRAME) /
                 *    @c (300 × N_PACKETS) ... (§7.4 wide form).
                 *
                 * Both expressions collapse to "a few hundred
                 * microseconds" in practice.  When the sender's
                 * inputs are unknown (Type Unknown / Type Auto),
                 * returns @c Duration::zero so the SDP emitter
                 * omits @c TROFF.
                 *
                 * @param senderType      Resolved sender type.
                 * @param packetsPerFrame N_PACKETS.
                 * @param frameInterval   T_FRAME.
                 * @param activeRatio     R_ACTIVE.
                 * @param maxUdpBytes     MAXUDP (drives the VRX_FULL
                 *                        term in the narrow form).
                 * @return TRO_DEFAULT as a @ref Duration, or
                 *         @c Duration::zero on invalid input.
                 */
                static Duration troDefault(const RtpSenderType &senderType,
                                           int packetsPerFrame, const Duration &frameInterval,
                                           double activeRatio, int maxUdpBytes);

                /**
                 * @brief Returns the SDP @c TP fmtp value for @p senderType.
                 *
                 * Maps the enum to its on-wire spelling:
                 *  - @c TypeN  → @c "2110TPN"
                 *  - @c TypeNL → @c "2110TPNL"
                 *  - @c TypeW  → @c "2110TPW"
                 *  - @c Auto / @c Unknown → empty (suppresses TP
                 *    emission).
                 *
                 * @param senderType The resolved sender type.
                 * @return Wire spelling, or empty for unknown / auto.
                 */
                static String tpFmtpValue(const RtpSenderType &senderType);

                /**
                 * @brief Parses an SDP @c TP fmtp value back to a
                 *        @ref RtpSenderType.
                 *
                 * Recognises @c "2110TPN", @c "2110TPNL",
                 * @c "2110TPW" (case-insensitive).  Anything else
                 * yields @c Unknown.
                 *
                 * @param value Wire spelling from a parsed fmtp.
                 * @return Resolved sender type.
                 */
                static RtpSenderType senderTypeFromTp(const String &value);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
