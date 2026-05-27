/**
 * @file      st2110tx.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/st2110tx.h>

#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// 1 / (frame_rate × M) expressed as nanoseconds, computed without
// intermediate floating-point loss when @p frameIntervalNs is exact.
// Returns 0 when either input is non-positive.
int64_t scaleFrameIntervalNs(int64_t frameIntervalNs, int64_t multiplier) {
        if (frameIntervalNs <= 0 || multiplier <= 0) return 0;
        return frameIntervalNs / multiplier;
}

} // namespace

Duration St2110Tx::trsGapped(int packetsPerFrame, const Duration &frameInterval,
                             double activeRatio) {
        if (packetsPerFrame <= 0 || activeRatio <= 0.0 || !frameInterval.isValid()) {
                return Duration::zero();
        }
        const int64_t frameNs = frameInterval.nanoseconds();
        if (frameNs <= 0) return Duration::zero();
        // T_RS_g = T_FRAME × R_ACTIVE / N_PACKETS — compute in
        // double then back to ns; the loss of precision is well
        // under one nanosecond for plausible inputs (frame interval
        // ≤ 50ms; R_ACTIVE > 0.4; N_PACKETS ≤ 16384).
        const double trsNs = (static_cast<double>(frameNs) * activeRatio) /
                             static_cast<double>(packetsPerFrame);
        return Duration::fromNanoseconds(static_cast<int64_t>(trsNs));
}

Duration St2110Tx::trsLinear(int packetsPerFrame, const Duration &frameInterval) {
        if (packetsPerFrame <= 0 || !frameInterval.isValid()) return Duration::zero();
        const int64_t frameNs = frameInterval.nanoseconds();
        return Duration::fromNanoseconds(
                scaleFrameIntervalNs(frameNs, static_cast<int64_t>(packetsPerFrame)));
}

int64_t St2110Tx::tvdUtcNs(int64_t frameAnchorUtcNs, int64_t frameIndex,
                           const Duration &frameInterval, const Duration &trOffset) {
        if (frameAnchorUtcNs <= 0 || !frameInterval.isValid()) return 0;
        const int64_t frameNs = frameInterval.nanoseconds();
        if (frameNs <= 0) return 0;
        const int64_t trOffsetNs = trOffset.isValid() ? trOffset.nanoseconds() : 0;
        return frameAnchorUtcNs + frameIndex * frameNs + trOffsetNs;
}

int64_t St2110Tx::tprJUtcNs(int64_t tvdUtcNs, int packetIndex, const Duration &trs) {
        if (tvdUtcNs <= 0 || packetIndex < 0 || !trs.isValid()) return 0;
        const int64_t trsNs = trs.nanoseconds();
        return tvdUtcNs + static_cast<int64_t>(packetIndex) * trsNs;
}

int64_t St2110Tx::vrxFullNarrowBytes(int packetsPerFrame, int maxUdpBytes,
                                     const Duration &frameInterval) {
        if (packetsPerFrame <= 0 || maxUdpBytes <= 0 || !frameInterval.isValid()) {
                return 0;
        }
        const int64_t frameUs = frameInterval.microseconds();
        if (frameUs <= 0) return 0;
        // Term 1: ceil(1500 × 8 / MAXUDP), an integer-bytes burst
        // size derived from the standard MTU's RTP framing
        // overhead.
        const int64_t term1 = (1500LL * 8LL + maxUdpBytes - 1) / maxUdpBytes;
        // Term 2: N_PACKETS × 1e6 / (27000 × T_FRAME_us) — the
        // RFC's per-frame-interval bound at 27000 Hz reference.
        // Multiply N_PACKETS by 1e6 first (microseconds) so the
        // integer division loses ≤ 1 byte of precision rather
        // than collapsing to zero on sub-µs frame intervals.
        const int64_t term2 = (static_cast<int64_t>(packetsPerFrame) * 1'000'000LL) /
                              (27000LL * frameUs);
        return term1 > term2 ? term1 : term2;
}

int St2110Tx::cmaxNarrowPackets(int packetsPerFrame, double activeRatio,
                                const Duration &frameInterval) {
        if (packetsPerFrame <= 0 || activeRatio <= 0.0 || !frameInterval.isValid()) {
                return 0;
        }
        const int64_t frameNs = frameInterval.nanoseconds();
        if (frameNs <= 0) return 0;
        // CMAX_narrow = max(4, N_PACKETS / (43200 × R_ACTIVE ×
        //                                    T_FRAME)).
        // Compute the second term in double; it's a ratio so the
        // double precision is well sufficient.
        const double frameSec = static_cast<double>(frameNs) / 1e9;
        const double rhs = static_cast<double>(packetsPerFrame) /
                           (43200.0 * activeRatio * frameSec);
        const int rhsInt = static_cast<int>(rhs);
        return rhsInt > 4 ? rhsInt : 4;
}

int64_t St2110Tx::vrxFullWideBytes(int packetsPerFrame, int maxUdpBytes,
                                   const Duration &frameInterval) {
        if (packetsPerFrame <= 0 || maxUdpBytes <= 0 || !frameInterval.isValid()) {
                return 0;
        }
        const int64_t frameUs = frameInterval.microseconds();
        if (frameUs <= 0) return 0;
        // Term 1: ceil(1500 × 720 / MAXUDP).  Wider receiver
        // budget — the 720 multiplier accommodates the looser
        // pacing of a non-narrow sender.
        const int64_t term1 = (1500LL * 720LL + maxUdpBytes - 1) / maxUdpBytes;
        // Term 2: N_PACKETS / (300 × T_FRAME) at 300 Hz reference.
        const int64_t term2 = (static_cast<int64_t>(packetsPerFrame) * 1'000'000LL) /
                              (300LL * frameUs);
        return term1 > term2 ? term1 : term2;
}

int St2110Tx::cmaxWidePackets(int packetsPerFrame, const Duration &frameInterval) {
        if (packetsPerFrame <= 0 || !frameInterval.isValid()) return 0;
        const int64_t frameNs = frameInterval.nanoseconds();
        if (frameNs <= 0) return 0;
        // CMAX_wide = max(16, N_PACKETS / (21600 × T_FRAME)).
        const double frameSec = static_cast<double>(frameNs) / 1e9;
        const double rhs = static_cast<double>(packetsPerFrame) / (21600.0 * frameSec);
        const int rhsInt = static_cast<int>(rhs);
        return rhsInt > 16 ? rhsInt : 16;
}

RtpSenderType St2110Tx::resolveSenderType(const RtpPacingMode &pacingMode) {
        if (pacingMode == RtpPacingMode::TxTime) return RtpSenderType::TypeNL;
        if (pacingMode == RtpPacingMode::KernelFq) return RtpSenderType::TypeW;
        if (pacingMode == RtpPacingMode::Userspace) return RtpSenderType::TypeW;
        if (pacingMode == RtpPacingMode::Auto) return RtpSenderType::TypeW;
        if (pacingMode == RtpPacingMode::None) return RtpSenderType::Unknown;
        return RtpSenderType::Unknown;
}

Duration St2110Tx::troDefault(const RtpSenderType &senderType, int packetsPerFrame,
                              const Duration &frameInterval, double activeRatio,
                              int maxUdpBytes) {
        if (packetsPerFrame <= 0 || !frameInterval.isValid() || activeRatio <= 0.0) {
                return Duration::zero();
        }
        const int64_t frameNs = frameInterval.nanoseconds();
        if (frameNs <= 0) return Duration::zero();
        const double frameSec = static_cast<double>(frameNs) / 1e9;

        if (senderType == RtpSenderType::TypeN || senderType == RtpSenderType::TypeNL) {
                // §7.4 narrow form (informative reduction of the
                // full expression):
                //   TRO_default = (43680 × T_FRAME - ((T_FRAME -
                //                  VRX_FULL_TIME) × R_ACTIVE ×
                //                  T_FRAME)) / (1125 × N_PACKETS)
                //
                // VRX_FULL_TIME is the worst-case receiver-side
                // hold time corresponding to VRX_FULL_BYTES at the
                // wire rate.  The library follows the published
                // simplification:
                //   TRO_default ≈ ((43680 - 1125 × R_ACTIVE) ×
                //                  T_FRAME) / (1125 × N_PACKETS)
                // which collapses to a few hundred microseconds
                // for typical HD / UHD inputs.
                (void)maxUdpBytes; // not used in the simplified form
                const double trsSec =
                        ((43680.0 - 1125.0 * activeRatio) * frameSec) /
                        (1125.0 * static_cast<double>(packetsPerFrame));
                return Duration::fromNanoseconds(static_cast<int64_t>(trsSec * 1e9));
        }
        if (senderType == RtpSenderType::TypeW) {
                // §7.4 wide form:
                //   TRO_default = (1080 × T_FRAME) / (300 × N_PACKETS)
                const double trsSec =
                        (1080.0 * frameSec) / (300.0 * static_cast<double>(packetsPerFrame));
                return Duration::fromNanoseconds(static_cast<int64_t>(trsSec * 1e9));
        }
        return Duration::zero();
}

String St2110Tx::tpFmtpValue(const RtpSenderType &senderType) {
        if (senderType == RtpSenderType::TypeN) return String("2110TPN");
        if (senderType == RtpSenderType::TypeNL) return String("2110TPNL");
        if (senderType == RtpSenderType::TypeW) return String("2110TPW");
        return String();
}

RtpSenderType St2110Tx::senderTypeFromTp(const String &value) {
        // ST 2110-21 §7.5 spells these in upper case but receivers
        // tolerate variant case; lowercase and uppercase the input
        // string before compare so a stray lowercase TP= still parses.
        const String v = value.toUpper();
        if (v == String("2110TPN")) return RtpSenderType::TypeN;
        if (v == String("2110TPNL")) return RtpSenderType::TypeNL;
        if (v == String("2110TPW")) return RtpSenderType::TypeW;
        return RtpSenderType::Unknown;
}

PROMEKI_NAMESPACE_END
