/**
 * @file      rtpstreamclock.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpstreamclock.h>

PROMEKI_NAMESPACE_BEGIN

NtpTime RtpStreamClock::toNtp(uint32_t rtpTs) const {
        if (!_valid) return NtpTime();
        // Modular @c uint32_t subtraction handles wraparound the
        // same way the writer's RtpSession::emitRtcpSr derivation
        // does, so a 32-bit wrap on either side cancels out and the
        // sender / receiver mappings stay aligned.
        const uint32_t deltaTicks = rtpTs - _srRtpTs;
        // 64-bit intermediate: deltaTicks (≤ 2^32-1) × 2^32 fits in
        // a u64.  We accumulate in NTP fractional units (1/2^32 s)
        // for precision parity with the @c (NtpTime + Duration)
        // forward path used on the writer side.
        const uint64_t deltaFractional =
                (static_cast<uint64_t>(deltaTicks) << 32) / static_cast<uint64_t>(_clockRate);
        const uint64_t anchor64 = (static_cast<uint64_t>(_srNtp.seconds()) << 32) |
                                  static_cast<uint64_t>(_srNtp.fraction());
        const uint64_t result64 = anchor64 + deltaFractional;
        return NtpTime(static_cast<uint32_t>(result64 >> 32) & 0xFFFFFFFFu,
                       static_cast<uint32_t>(result64 & 0xFFFFFFFFu));
}

uint32_t RtpStreamClock::toRtpTs(const NtpTime &ntp) const {
        if (!_valid) return 0;
        // Convert both NTP values into a fractional-second packed
        // representation and subtract.  The packed form's units are
        // 1/2^32 s, so dividing by (2^32 / clockRate) recovers RTP
        // ticks — but we instead multiply the seconds out directly
        // in 64-bit to avoid the divide:
        //   rtpDelta = (ntp - anchor)_seconds × clockRate.
        // The @c uint64_t subtraction wraps the same way the wire
        // protocol does past a 2^32-second boundary, which is well
        // beyond any session lifetime we care about — so the wrap
        // happens only via the deliberate @c uint32_t cast at the
        // end (matching the forward mapping's modular cadence on
        // the RTP-TS axis).
        const uint64_t anchor64 = (static_cast<uint64_t>(_srNtp.seconds()) << 32) |
                                  static_cast<uint64_t>(_srNtp.fraction());
        const uint64_t target64 = (static_cast<uint64_t>(ntp.seconds()) << 32) |
                                  static_cast<uint64_t>(ntp.fraction());
        // Modular @c uint64_t subtraction so a target before the
        // anchor produces a large positive delta that the trailing
        // @c uint32_t truncation wraps on the RTP-TS axis exactly
        // as the receive-side comparator would.
        const uint64_t deltaFractional = target64 - anchor64;
        // deltaFractional is in 1/2^32 s units; multiply by clockRate
        // and right-shift 32 to get RTP ticks (delta_seconds × Hz).
        // Cast through 128-bit-equivalent split to avoid overflow on
        // the multiply: split deltaFractional into high32 (whole
        // seconds) and low32 (sub-second fraction in 1/2^32 s).
        const uint64_t whole = deltaFractional >> 32;
        const uint64_t frac  = deltaFractional & 0xFFFFFFFFull;
        const uint64_t ticks = whole * static_cast<uint64_t>(_clockRate) +
                               (frac * static_cast<uint64_t>(_clockRate)) / (1ull << 32);
        return static_cast<uint32_t>(ticks + _srRtpTs);
}

PROMEKI_NAMESPACE_END
