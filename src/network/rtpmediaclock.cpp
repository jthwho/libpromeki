/**
 * @file      rtpmediaclock.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/rtpmediaclock.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Projects a UTC nanosecond instant onto the media-clock grid:
// floor(utcNs * mediaClockHz / 1e9) mod 2^32.  Decomposes into whole-
// seconds + sub-second nanoseconds first so the multiply doesn't
// overflow int64 for plausible @p mediaClockHz (≤ 2 GHz).  Negative
// @p utcNs returns 0.
uint32_t utcNsToMediaClockTicks(int64_t utcNs, uint32_t mediaClockHz) {
        if (utcNs <= 0 || mediaClockHz == 0) return 0;
        const int64_t secs = utcNs / 1'000'000'000LL;
        const int64_t rem = utcNs % 1'000'000'000LL;
        const int64_t hz = static_cast<int64_t>(mediaClockHz);
        const int64_t totalTicks = secs * hz + rem * hz / 1'000'000'000LL;
        return static_cast<uint32_t>(static_cast<uint64_t>(totalTicks) & 0xFFFFFFFFu);
}

} // namespace

RtpMediaClock RtpMediaClock::frameZeroAnchored(uint32_t mediaClockHz, const FrameRate &rate) {
        RtpMediaClock c(mediaClockHz, rate);
        c._anchorFrameIndex = 0;
        c._anchorUtcNs = 0;
        c._anchorRtpTs = 0;
        return c;
}

RtpMediaClock RtpMediaClock::ptpAnchored(uint32_t mediaClockHz, const FrameRate &rate,
                                         int64_t anchorUtcNs) {
        if (anchorUtcNs <= 0) {
                return frameZeroAnchored(mediaClockHz, rate);
        }
        RtpMediaClock c(mediaClockHz, rate);
        c._anchorFrameIndex = 0;
        c._anchorUtcNs = anchorUtcNs;
        c._anchorRtpTs = utcNsToMediaClockTicks(anchorUtcNs, mediaClockHz);
        return c;
}

namespace {
// Treats an invalid Duration as zero — defensive guard against a
// hand-crafted RtpMediaClock that bypassed the in-class default-
// initialiser (e.g. via a future @c readFromStream path).  Per
// project memory, @c Duration::nanoseconds() returns @c INT64_MIN
// for the invalid sentinel.
int64_t safeNs(const Duration &d) { return d.isValid() ? d.nanoseconds() : 0; }
} // namespace

uint32_t RtpMediaClock::rtpTsForFrame(int64_t frameIndex) const {
        if (!isValid()) return 0;
        const int64_t relFrame = frameIndex - _anchorFrameIndex;
        // cumulativeTicks clamps negative relFrame to 0; the
        // documented behaviour is "no advance before anchor".
        const int64_t cumTicks = _frameRate.cumulativeTicks(
                static_cast<int64_t>(_mediaClockHz), relFrame);
        const int64_t trTicks = safeNs(_trOffset) *
                                static_cast<int64_t>(_mediaClockHz) / 1'000'000'000LL;
        const uint64_t sum = static_cast<uint64_t>(_anchorRtpTs) +
                             static_cast<uint64_t>(cumTicks) +
                             static_cast<uint64_t>(trTicks);
        return static_cast<uint32_t>(sum & 0xFFFFFFFFu);
}

int64_t RtpMediaClock::tvdUtcNs(int64_t frameIndex) const {
        if (!isValid() || _anchorUtcNs <= 0) return 0;
        const int64_t relFrame = frameIndex - _anchorFrameIndex;
        const int64_t nsAdvance = _frameRate.cumulativeTicks(
                INT64_C(1'000'000'000), relFrame);
        return _anchorUtcNs + nsAdvance + safeNs(_trOffset);
}

int64_t RtpMediaClock::tvdUtcNsForRtpTs(uint32_t rtpTs) const {
        if (!isValid() || _anchorUtcNs <= 0) return 0;
        // delta = rtpTs - anchorRtpTs - trOffsetTicks (modular)
        const int64_t trTicks = safeNs(_trOffset) *
                                static_cast<int64_t>(_mediaClockHz) / 1'000'000'000LL;
        const uint32_t baseRtpTs = static_cast<uint32_t>(
                (static_cast<uint64_t>(_anchorRtpTs) + static_cast<uint64_t>(trTicks)) &
                0xFFFFFFFFu);
        const uint32_t deltaTicks = rtpTs - baseRtpTs; // modular subtract
        // Convert delta media-clock ticks → nanoseconds.
        // nsDelta = deltaTicks * 1e9 / mediaClockHz; decompose to
        // avoid overflow on plausible inputs.
        const uint64_t hz = static_cast<uint64_t>(_mediaClockHz);
        const uint64_t wholeSec = static_cast<uint64_t>(deltaTicks) / hz;
        const uint64_t subTicks = static_cast<uint64_t>(deltaTicks) % hz;
        const int64_t  nsDelta = static_cast<int64_t>(wholeSec) * 1'000'000'000LL +
                                 static_cast<int64_t>(subTicks * 1'000'000'000ULL / hz);
        return _anchorUtcNs + nsDelta + safeNs(_trOffset);
}

uint32_t RtpMediaClock::mediaClkDirectOffset() const {
        if (!isValid() || _anchorUtcNs <= 0) return 0;
        // Offset = RTP-TS at wallclock=0.  Walking backward from the
        // anchor by @c anchorUtcNs nanoseconds gives the RTP-TS at
        // wallclock=0; that's
        //   anchorRtpTs - floor(anchorUtcNs * hz / 1e9)
        // mod 2^32.  For a natural anchor (built via
        // @ref ptpAnchored) the subtraction is exact zero — the
        // factory wires anchorRtpTs to that same projection.  Hand-
        // pinned anchors that don't follow the natural rule keep the
        // non-zero offset so the SDP signalling reflects it.
        const uint32_t naturalTicks = utcNsToMediaClockTicks(_anchorUtcNs, _mediaClockHz);
        return _anchorRtpTs - naturalTicks; // modular
}

PROMEKI_NAMESPACE_END
