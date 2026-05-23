/**
 * @file      rtpseqtracker.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/duration.h>
#include <promeki/rtpseqtracker.h>
#include <promeki/timestamp.h>

using namespace promeki;

namespace {

// Build a TimeStamp from a "session-relative" microsecond offset off
// a fixed epoch.  Tests don't care about wallclock alignment; they
// care about deltas, so a synthetic anchor + microsecond offsets is
// the cleanest input shape.
TimeStamp at(int64_t offsetUs) {
        static const TimeStamp anchor = TimeStamp::now();
        return anchor + Duration::fromMicroseconds(offsetUs);
}

} // namespace

TEST_CASE("RtpSeqTracker: probation requires two sequential packets") {
        RtpSeqTracker t;
        // Even though MinSequential = 2, the first packet in a fresh
        // tracker is the implicit init — probation true.
        auto r1 = t.observe(/*seq=*/100, /*rtpTs=*/0, at(0));
        CHECK(r1.probation);
        CHECK(r1.ssrcInit);
        CHECK_FALSE(r1.duplicate);

        // Second sequential packet ends probation.
        auto r2 = t.observe(/*seq=*/101, /*rtpTs=*/3000, at(1000));
        CHECK_FALSE(r2.probation);
        CHECK_FALSE(r2.duplicate);

        auto s = t.snapshot();
        // After probation passes, init_seq runs again with the current
        // packet as the new base — exactly one received counted.
        CHECK(s.receivedPackets == 1u);
        CHECK(s.expectedPackets == 1u);
}

TEST_CASE("RtpSeqTracker: probation rearms on out-of-order during window") {
        RtpSeqTracker t;
        // First packet — implicit init sets max_seq = 99 and probation = 2.
        auto r1 = t.observe(100, 0, at(0));
        CHECK(r1.probation);
        // Out-of-order during probation rearms — sets probation =
        // MIN_SEQUENTIAL - 1 = 1 (so one more sequential packet
        // ends probation).
        auto r2 = t.observe(105, 0, at(1000));
        CHECK(r2.probation);
        // Sequential after rearm — ends probation.
        auto r3 = t.observe(106, 0, at(2000));
        CHECK_FALSE(r3.probation);
}

TEST_CASE("RtpSeqTracker: in-order accounting") {
        RtpSeqTracker t;
        // Probation through seq 100, 101.  After probation init_seq
        // adopts seq=101 as new base.
        t.observe(100, 0, at(0));
        t.observe(101, 0, at(1000));
        for (int i = 102; i <= 110; ++i) {
                t.observe(static_cast<uint16_t>(i), 0, at(i * 1000));
        }
        auto s = t.snapshot();
        // 101..110 inclusive → 10 packets, but post-probation init
        // re-anchored at 101 → received = 10.
        CHECK(s.receivedPackets == 10u);
        CHECK(s.expectedPackets == 10u);
        CHECK(s.cumulativeLost == 0);
        CHECK(s.cycles == 0u);
        CHECK(s.extendedHighestSeq == 110u);
}

TEST_CASE("RtpSeqTracker: 16-bit wraparound bumps cycles") {
        RtpSeqTracker t;
        // Probation 65530, 65531.
        t.observe(65530, 0, at(0));
        t.observe(65531, 0, at(1000));
        // Forward through wrap.
        t.observe(65532, 0, at(2000));
        t.observe(65533, 0, at(3000));
        t.observe(65534, 0, at(4000));
        t.observe(65535, 0, at(5000));
        t.observe(0, 0, at(6000));
        t.observe(1, 0, at(7000));
        auto s = t.snapshot();
        CHECK(s.cycles == RtpSeqTracker::SeqMod);
        CHECK(s.extendedHighestSeq == RtpSeqTracker::SeqMod + 1u);
        // 65531 (post-probation base) through 1 wrap is 6 packets
        // received: {65532,65533,65534,65535,0,1}.  Hmm wait — there
        // were 7 observe() calls after probation.  Let me recount:
        // Actually after probation passes on (65531), received=1.
        // Then 65532..65535 (4) + 0..1 (2) = 6 more, total received 7.
        // expected = extendedHighestSeq - baseSeq + 1
        //          = (65536+1) - 65531 + 1 = 7
        CHECK(s.receivedPackets == 7u);
        CHECK(s.expectedPackets == 7u);
}

TEST_CASE("RtpSeqTracker: gap shows up as cumulativeLost") {
        RtpSeqTracker t;
        t.observe(100, 0, at(0));
        t.observe(101, 0, at(1000)); // probation passes here
        // Skip 102, 103, 104.  Receive 105..110.
        for (int seq : {105, 106, 107, 108, 109, 110}) {
                t.observe(static_cast<uint16_t>(seq), 0, at(seq * 1000));
        }
        auto s = t.snapshot();
        CHECK(s.expectedPackets == 10u); // 101..110 inclusive
        CHECK(s.receivedPackets == 7u);
        CHECK(s.cumulativeLost == 3);
}

TEST_CASE("RtpSeqTracker: in-window backwards arrival counts as reorder") {
        RtpSeqTracker t;
        t.observe(100, 0, at(0));
        t.observe(101, 0, at(1000));
        t.observe(102, 0, at(2000));
        t.observe(103, 0, at(3000));
        // 102 again — within MaxMisorder window; counted as reorder.
        auto rd = t.observe(102, 0, at(3500));
        CHECK_FALSE(rd.duplicate); // tracker passes through; reorder buffer dedups
        auto s = t.snapshot();
        CHECK(s.reorderedPackets >= 1u);
}

TEST_CASE("RtpSeqTracker: large forward jump tagged duplicate until bad_seq match") {
        RtpSeqTracker t;
        t.observe(100, 0, at(0));
        t.observe(101, 0, at(1000));
        // Jump to seq = 50000 — udelta = 49899, which exceeds the
        // current @c MaxDropout (32,768; sized for the worst-case
        // ST 2110-20 / JPEG XS burst per-frame seq-gap) but is still
        // < @c SeqMod - MaxMisorder (64,536).  Lands in the "very
        // large jump" branch where the tracker tags the first
        // occurrence as @c duplicate and stashes it as the candidate
        // restart seq.
        auto r1 = t.observe(50000, 0, at(2000));
        CHECK(r1.duplicate);
        // Second sequential packet at the new bad_seq matches → restart.
        auto r2 = t.observe(50001, 0, at(3000));
        CHECK_FALSE(r2.duplicate);
        CHECK(r2.ssrcInit);
        auto s = t.snapshot();
        CHECK(s.receivedPackets == 1u); // post-restart init zeroed counters
        CHECK(s.extendedHighestSeq == 50001u);
}

TEST_CASE("RtpSeqTracker: same-seq replay does not bump reorder counter") {
        RtpSeqTracker t;
        t.observe(100, 0, at(0));
        t.observe(101, 0, at(1000));
        // The §A layer only flags strictly-backwards arrivals
        // (udelta in the (SeqMod - MaxMisorder, SeqMod) wraparound
        // band) as reorders; an exact same-seq replay sits at
        // udelta=0 and is treated as in-order, with the strict-dup
        // detection delegated to the downstream reorder buffer.
        auto r = t.observe(101, 0, at(1500));
        CHECK_FALSE(r.duplicate);
        auto s = t.snapshot();
        CHECK(s.reorderedPackets == 0u);
}

TEST_CASE("RtpSeqTracker: jitter EWMA stays zero on perfectly-paced input") {
        RtpSeqTracker t;
        t.initSource(100, /*clockRateHz=*/48000);
        // 1ms-paced packets carrying a 48-sample step in RTP-TS units
        // should yield zero transit drift (D = 0 across consecutive
        // packets), so jitter stays at zero.
        t.observe(100, 48 * 0, at(0));
        t.observe(101, 48 * 1, at(1000));
        t.observe(102, 48 * 2, at(2000));
        t.observe(103, 48 * 3, at(3000));
        auto s = t.snapshot();
        CHECK(s.interarrivalJitter == 0u);
}

TEST_CASE("RtpSeqTracker: jitter EWMA grows on bursty arrivals") {
        RtpSeqTracker t;
        t.initSource(100, 48000);
        // Send packets at 1ms RTP-TS spacing but vary arrival deltas.
        t.observe(100, 48 * 0, at(0));
        t.observe(101, 48 * 1, at(1000));
        t.observe(102, 48 * 2, at(5000)); // 4ms late
        t.observe(103, 48 * 3, at(6000));
        auto s = t.snapshot();
        CHECK(s.interarrivalJitter > 0u);
}

TEST_CASE("RtpSeqTracker: snapshot is non-mutating") {
        RtpSeqTracker t;
        t.observe(100, 0, at(0));
        t.observe(101, 0, at(1000));
        auto s1 = t.snapshot();
        auto s2 = t.snapshot();
        CHECK(s1.expectedPackets == s2.expectedPackets);
        CHECK(s1.receivedPackets == s2.receivedPackets);
        CHECK(s1.fractionLost == s2.fractionLost);
}

TEST_CASE("RtpSeqTracker: commitRrInterval computes 8-bit fractionLost") {
        RtpSeqTracker t;
        t.observe(100, 0, at(0));
        t.observe(101, 0, at(1000)); // probation passes
        // Receive 4 of 8 in this interval (50% loss): 102,103, then 106,107.
        for (int seq : {102, 103, 106, 107}) {
                t.observe(static_cast<uint16_t>(seq), 0, at(seq * 1000));
        }
        // Expected packets so far: 101..107 inclusive = 7.
        // Received: 5 (101,102,103,106,107).  Loss in this RR
        // interval = 2 / 7 → fractionLost ≈ (2 << 8)/7 = 73.
        t.commitRrInterval();
        auto s = t.snapshot();
        CHECK(s.fractionLost == 73u);
}

TEST_CASE("RtpSeqTracker: commitRrInterval resets fraction on next interval") {
        RtpSeqTracker t;
        t.observe(100, 0, at(0));
        t.observe(101, 0, at(1000));
        for (int seq : {102, 103, 104, 105, 106, 107}) {
                t.observe(static_cast<uint16_t>(seq), 0, at(seq * 1000));
        }
        t.commitRrInterval();
        // Drop a couple in the second interval.
        for (int seq : {108, 110, 112}) {
                t.observe(static_cast<uint16_t>(seq), 0, at(seq * 1000));
        }
        t.commitRrInterval();
        auto s = t.snapshot();
        // Second interval expected = 5 (108..112), received = 3 (108,110,112).
        // loss = 2/5 → fractionLost = (2 << 8)/5 = 102.
        CHECK(s.fractionLost == 102u);
}

TEST_CASE("RtpSeqTracker: reset clears every counter (preserves clock rate)") {
        RtpSeqTracker t;
        t.initSource(100, 48000);
        for (int i = 100; i < 110; ++i) t.observe(static_cast<uint16_t>(i), 0, at(i * 1000));
        t.reset();
        // Clock rate survives reset — it's a per-stream config, not
        // per-source state.  An SSRC reset zeroes counters but the
        // jitter machinery stays wired for the next source.
        CHECK(t.clockRateHz() == 48000u);
        auto s = t.snapshot();
        CHECK(s.expectedPackets == 0u);
        CHECK(s.receivedPackets == 0u);
        CHECK(s.cycles == 0u);
        CHECK(s.interarrivalJitter == 0u);
        CHECK(s.fractionLost == 0u);
}

TEST_CASE("RtpSeqTracker: explicit initSource sets clock rate for jitter") {
        RtpSeqTracker t;
        t.initSource(0, 48000);
        CHECK(t.clockRateHz() == 48000u);
}
