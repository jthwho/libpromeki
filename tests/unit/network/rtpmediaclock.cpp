/**
 * @file      rtpmediaclock.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>

#include <doctest/doctest.h>
#include <promeki/duration.h>
#include <promeki/framerate.h>
#include <promeki/rtpmediaclock.h>

using namespace promeki;

namespace {

// 2026-05-21T00:00:00Z in Unix nanoseconds.
constexpr int64_t kAnchorUtcNs = INT64_C(1779494400'000'000'000);

// Tolerance for double-precision sanity checks (1 media-clock tick).
constexpr int64_t kOneTickTolerance = 1;

} // namespace

// ============================================================================
// Default / validity
// ============================================================================

TEST_CASE("RtpMediaClock — default-constructed is invalid") {
        RtpMediaClock c;
        CHECK_FALSE(c.isValid());
        CHECK(c.mediaClockHz() == 0u);
        CHECK_FALSE(c.frameRate().isValid());
        CHECK(c.anchorUtcNs() == 0);
        CHECK(c.anchorRtpTs() == 0u);
        CHECK_FALSE(c.hasPtpAnchor());
        CHECK(c.rtpTsForFrame(0) == 0u);
        CHECK(c.rtpTsForFrame(100) == 0u);
        CHECK(c.tvdUtcNs(0) == 0);
        CHECK(c.tvdUtcNs(100) == 0);
        CHECK(c.tvdUtcNsForRtpTs(0u) == 0);
        CHECK(c.mediaClkDirectOffset() == 0u);
}

// ============================================================================
// frameZeroAnchored — backward-compatible mode
// ============================================================================

TEST_CASE("RtpMediaClock — frameZeroAnchored matches cumulativeTicks at integer rates") {
        const FrameRate rate = FrameRate::FPS_60;
        const RtpMediaClock c = RtpMediaClock::frameZeroAnchored(90000, rate);
        REQUIRE(c.isValid());
        CHECK_FALSE(c.hasPtpAnchor());
        CHECK(c.anchorUtcNs() == 0);
        CHECK(c.anchorRtpTs() == 0u);

        // FPS_60 + 90 kHz → 1500 ticks/frame, exactly.
        for (int64_t i = 0; i < 10; ++i) {
                const uint32_t expected = static_cast<uint32_t>(i * 1500);
                CHECK(c.rtpTsForFrame(i) == expected);
        }
}

TEST_CASE("RtpMediaClock — frameZeroAnchored handles NTSC fractional rates drift-free") {
        const FrameRate rate = FrameRate::FPS_59_94;
        const RtpMediaClock c = RtpMediaClock::frameZeroAnchored(90000, rate);
        REQUIRE(c.isValid());

        // FPS_59_94 = 60000/1001; at 90kHz the cumulative ticks are
        //   N * 90000 * 1001 / 60000 (integer rational math)
        // Per-frame stride alternates 1501/1502 around the average
        // of 1501.5 so the cumulative count stays drift-free.
        CHECK(c.rtpTsForFrame(0) == 0u);
        CHECK(c.rtpTsForFrame(1) == 1501u);
        CHECK(c.rtpTsForFrame(2) == 3003u);
        // Frame 1001: floor(1001 * 90000 * 1001 / 60000)
        //           = floor(90'180'090'000 / 60'000)
        //           = floor(1'503'001.5) = 1'503'001.
        CHECK(c.rtpTsForFrame(1001) == 1503001u);
}

TEST_CASE("RtpMediaClock — frameZeroAnchored tvdUtcNs returns 0 (no wallclock anchor)") {
        const RtpMediaClock c = RtpMediaClock::frameZeroAnchored(90000, FrameRate::FPS_60);
        CHECK(c.tvdUtcNs(0) == 0);
        CHECK(c.tvdUtcNs(100) == 0);
        CHECK(c.mediaClkDirectOffset() == 0u);
}

// ============================================================================
// ptpAnchored — PTP-traceable mode
// ============================================================================

TEST_CASE("RtpMediaClock — ptpAnchored exposes the anchor and SMPTE-Epoch-aligned RTP-TS") {
        const RtpMediaClock c =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_60, kAnchorUtcNs);
        REQUIRE(c.isValid());
        CHECK(c.hasPtpAnchor());
        CHECK(c.anchorUtcNs() == kAnchorUtcNs);

        // anchorRtpTs = floor(kAnchorUtcNs * 90000 / 1e9) mod 2^32.
        // 1779494400 s * 90000 Hz = 160154496000000 ticks.
        // mod 2^32 = 160154496000000 - 37 * 2^32
        //          = 160154496000000 - 158913789952
        //          = 160154496000000 - 158913789952 (37 wraps) = ...
        // Easier: compute via the same arithmetic the impl uses.
        const int64_t  expectedTotalTicks = 1779494400LL * 90000LL;
        const uint32_t expectedAnchorRtpTs =
                static_cast<uint32_t>(static_cast<uint64_t>(expectedTotalTicks) & 0xFFFFFFFFu);
        CHECK(c.anchorRtpTs() == expectedAnchorRtpTs);

        // Frame 0's RTP-TS matches the anchor.
        CHECK(c.rtpTsForFrame(0) == expectedAnchorRtpTs);
        // Frame 1 = anchor + 1500 ticks.
        CHECK(c.rtpTsForFrame(1) == static_cast<uint32_t>(expectedAnchorRtpTs + 1500));
}

TEST_CASE("RtpMediaClock — ptpAnchored with 0/negative wallclock falls back to frame-zero") {
        const RtpMediaClock c0 =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_60, 0);
        CHECK_FALSE(c0.hasPtpAnchor());
        CHECK(c0.rtpTsForFrame(0) == 0u);

        const RtpMediaClock cNeg =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_60, -1234);
        CHECK_FALSE(cNeg.hasPtpAnchor());
        CHECK(cNeg.rtpTsForFrame(0) == 0u);
}

TEST_CASE("RtpMediaClock — ptpAnchored tvdUtcNs advances by T_FRAME per frame") {
        const RtpMediaClock c =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_60, kAnchorUtcNs);
        // FPS_60 → T_FRAME = 1/60 s ≈ 16666666.666... ns.
        // cumulativeTicks(1e9, N) handles the rational math.
        CHECK(c.tvdUtcNs(0) == kAnchorUtcNs);
        CHECK(c.tvdUtcNs(60) == kAnchorUtcNs + 1'000'000'000LL); // 60 frames = 1 s
        CHECK(c.tvdUtcNs(120) == kAnchorUtcNs + 2'000'000'000LL);
}

TEST_CASE("RtpMediaClock — ptpAnchored tvdUtcNs at NTSC tracks 60000/1001 cadence") {
        const RtpMediaClock c =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_59_94, kAnchorUtcNs);
        // FPS_59_94 = 60000/1001 frames/s → T_FRAME = 1001/60000 s.
        // 60000 frames covers 60060/60000 = 1001 s exactly.  Verify
        // the 60000-frame cycle.
        CHECK(c.tvdUtcNs(0) == kAnchorUtcNs);
        // cumulativeTicks(1e9, 60000) = 60000 * 1e9 * 1001 / 60000 = 1001e9
        CHECK(c.tvdUtcNs(60000) == kAnchorUtcNs + 1001'000'000'000LL);
}

// ============================================================================
// Inverse — tvdUtcNsForRtpTs
// ============================================================================

TEST_CASE("RtpMediaClock — tvdUtcNsForRtpTs inverts rtpTsForFrame for integer rates") {
        const RtpMediaClock c =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_60, kAnchorUtcNs);
        for (int64_t i = 0; i < 100; ++i) {
                const uint32_t rtpTs = c.rtpTsForFrame(i);
                const int64_t  derived = c.tvdUtcNsForRtpTs(rtpTs);
                const int64_t  expected = c.tvdUtcNs(i);
                const int64_t  diff = derived > expected ? derived - expected : expected - derived;
                CHECK(diff <= kOneTickTolerance);
        }
}

TEST_CASE("RtpMediaClock — tvdUtcNsForRtpTs returns 0 for frame-zero-anchored clocks") {
        const RtpMediaClock c = RtpMediaClock::frameZeroAnchored(90000, FrameRate::FPS_60);
        CHECK(c.tvdUtcNsForRtpTs(0u) == 0);
        CHECK(c.tvdUtcNsForRtpTs(123456u) == 0);
}

// ============================================================================
// mediaclk:direct=<offset>
// ============================================================================

TEST_CASE("RtpMediaClock — natural ptpAnchored anchor yields mediaclk:direct=0") {
        const RtpMediaClock c =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_60, kAnchorUtcNs);
        // The factory wires anchorRtpTs to the SMPTE-Epoch-grid
        // projection, so the offset is exactly 0.
        CHECK(c.mediaClkDirectOffset() == 0u);
}

TEST_CASE("RtpMediaClock — frame-zero-anchored has no mediaclk:direct semantics") {
        const RtpMediaClock c = RtpMediaClock::frameZeroAnchored(90000, FrameRate::FPS_60);
        CHECK(c.mediaClkDirectOffset() == 0u);
}

// ============================================================================
// TR_OFFSET
// ============================================================================

TEST_CASE("RtpMediaClock — trOffset shifts both RTP-TS and T_VD") {
        RtpMediaClock c =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_60, kAnchorUtcNs);
        const uint32_t rtpTs0 = c.rtpTsForFrame(0);
        const int64_t  tvd0 = c.tvdUtcNs(0);

        c.setTrOffset(Duration::fromMicroseconds(125)); // 125 µs offset
        // 125 µs at 90 kHz = 125 * 90 = 11250 / 1000 = 11.25 → 11 ticks (floor).
        CHECK(c.rtpTsForFrame(0) == rtpTs0 + 11u);
        CHECK(c.tvdUtcNs(0) == tvd0 + 125'000); // 125 µs = 125 000 ns
        CHECK(c.trOffset() == Duration::fromMicroseconds(125));
}

// ============================================================================
// Audio cadence sanity — cumulativeTicks gives integer strides for 48k @ 1ms
// ============================================================================

TEST_CASE("RtpMediaClock — audio 48kHz / 1000 packets-per-second gives 48 samples / packet") {
        // For audio, frame rate represents packets/second; cumulativeTicks
        // at the sample rate yields the sample count.
        const FrameRate packetRate(FrameRate::RationalType(1000, 1));
        const RtpMediaClock c =
                RtpMediaClock::frameZeroAnchored(48000, packetRate);
        REQUIRE(c.isValid());
        for (uint32_t pkt = 0; pkt < 100; ++pkt) {
                CHECK(c.rtpTsForFrame(pkt) == pkt * 48u);
        }
}

TEST_CASE("RtpMediaClock — audio 96kHz / 8000 pps (125 µs) gives 12 samples / packet") {
        const FrameRate packetRate(FrameRate::RationalType(8000, 1));
        const RtpMediaClock c =
                RtpMediaClock::frameZeroAnchored(96000, packetRate);
        REQUIRE(c.isValid());
        for (uint32_t pkt = 0; pkt < 50; ++pkt) {
                CHECK(c.rtpTsForFrame(pkt) == pkt * 12u);
        }
}

// ============================================================================
// Modular wrap
// ============================================================================

TEST_CASE("RtpMediaClock — RTP-TS wraps cleanly at the 2^32 boundary") {
        const RtpMediaClock c =
                RtpMediaClock::ptpAnchored(90000, FrameRate::FPS_60, kAnchorUtcNs);
        // Anchor is mid-grid; advance by a large frame count and
        // verify modular subtraction round-trips.
        const int64_t  largeN = INT64_C(10'000'000);
        const uint32_t rtp = c.rtpTsForFrame(largeN);
        // No specific expected value — just confirm it differs from anchor and
        // that consecutive frames advance by the right stride.
        CHECK(rtp != c.anchorRtpTs());
        CHECK(static_cast<uint32_t>(c.rtpTsForFrame(largeN + 1) - rtp) == 1500u);
}
