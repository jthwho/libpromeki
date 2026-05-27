/**
 * @file      rtpstreamclock.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/ntptime.h>
#include <promeki/rtpstreamclock.h>
#include <cstdint>

using namespace promeki;

TEST_CASE("RtpStreamClock: default-constructed is invalid") {
        RtpStreamClock c;
        CHECK(c.isValid() == false);
        // toNtp on an invalid clock returns zero NtpTime.
        CHECK(c.toNtp(12345u) == NtpTime());
        CHECK(c.toRtpTs(NtpTime(100u, 0u)) == 0u);
}

TEST_CASE("RtpStreamClock: zero clock rate keeps clock invalid") {
        RtpStreamClock c(NtpTime(100u, 0u), 0u, /*clockRate=*/0u);
        CHECK(c.isValid() == false);
}

TEST_CASE("RtpStreamClock: toNtp at the anchor returns the anchor NTP") {
        const NtpTime  anchor(3913056000u, 0x80000000u);
        RtpStreamClock c(anchor, /*srRtpTs=*/12345u, /*clockRate=*/90000u);
        REQUIRE(c.isValid());
        CHECK(c.toNtp(12345u) == anchor);
}

TEST_CASE("RtpStreamClock: toNtp at one second past anchor advances by one second") {
        const NtpTime  anchor(100u, 0u);
        const uint32_t clockRate = 90000u;
        RtpStreamClock c(anchor, /*srRtpTs=*/0u, clockRate);
        // One second later in RTP-TS units is exactly clockRate.
        NtpTime r = c.toNtp(clockRate);
        CHECK(r.seconds() == 101u);
        CHECK(r.fraction() == 0u);
}

TEST_CASE("RtpStreamClock: toNtp at half a second past anchor advances by 2^31 fraction") {
        const NtpTime  anchor(100u, 0u);
        const uint32_t clockRate = 90000u;
        RtpStreamClock c(anchor, /*srRtpTs=*/0u, clockRate);
        // 0.5s = clockRate / 2 RTP ticks.
        NtpTime r = c.toNtp(clockRate / 2u);
        CHECK(r.seconds() == 100u);
        CHECK(r.fraction() == 0x80000000u);
}

TEST_CASE("RtpStreamClock: toNtp handles uint32_t wraparound consistently") {
        // Anchor is at the top of the RTP-TS counter; the next
        // packet's RTP-TS wraps to a small value.  The mapping must
        // produce the same NTP whether the wrap happens on the
        // sender side or the receiver side.
        const NtpTime  anchor(1000u, 0u);
        const uint32_t clockRate = 48000u;  // audio
        const uint32_t anchorRtpTs = 0xFFFFFFF0u; // 16 ticks before wrap
        RtpStreamClock c(anchor, anchorRtpTs, clockRate);
        // 32 ticks later — wraps past 0 to 0x10.
        const uint32_t laterRtpTs = anchorRtpTs + 32u;
        NtpTime r = c.toNtp(laterRtpTs);
        // 32 ticks at 48 kHz = 666.6... µs.  Equivalent NTP fraction
        // is (32 << 32) / 48000 = 2861022.96... → floor = 2861022.
        CHECK(r.seconds() == 1000u);
        const uint64_t expectedFrac =
                (static_cast<uint64_t>(32) << 32) / 48000u;
        CHECK(static_cast<uint64_t>(r.fraction()) == expectedFrac);
}

TEST_CASE("RtpStreamClock: toRtpTs is the inverse of toNtp on the anchor") {
        const NtpTime  anchor(2'000'000'000u, 0u);
        RtpStreamClock c(anchor, /*srRtpTs=*/777u, /*clockRate=*/90000u);
        REQUIRE(c.isValid());
        CHECK(c.toRtpTs(anchor) == 777u);
}

TEST_CASE("RtpStreamClock: toRtpTs round-trips toNtp at one-second boundaries") {
        const NtpTime  anchor(2'000'000'000u, 0x40000000u);
        const uint32_t clockRate = 90000u;
        RtpStreamClock c(anchor, /*srRtpTs=*/1000u, clockRate);
        for (uint32_t t = 0; t < 10; t++) {
                const uint32_t rtpTs = 1000u + t * clockRate;
                const NtpTime  ntp = c.toNtp(rtpTs);
                const uint32_t back = c.toRtpTs(ntp);
                // Round-trip must hit within 1 RTP tick (the integer
                // truncation introduces at most 1 ULP).
                const uint32_t err = (back >= rtpTs) ? back - rtpTs : rtpTs - back;
                CHECK(err <= 1u);
        }
}

TEST_CASE("RtpStreamClock: writer-anchor → SR-pair → receiver-clock has < 1 sample of error") {
        // Simulate the end-to-end Phase 5 round-trip:
        //
        // 1. Writer sets an anchor (NtpA, RtpA) at openStream time.
        // 2. Writer's SR fires for the most-recently-emitted RTP-TS
        //    R, derived as NTP_SR = NtpA + (R - RtpA) / clockRate.
        // 3. Receiver builds an RtpStreamClock from that SR pair
        //    and asks "what is the NTP for some other RTP-TS Q?"
        // 4. Compute the "true" NTP for Q from the writer-side
        //    formula and compare.
        //
        // Both formulas evaluate to the same arithmetic, so the
        // round-trip error budget is dominated by NTP-fraction
        // truncation.  The test asserts < 1 RTP sample of error.
        const NtpTime  writerAnchor(3'913'056'000u, 0u);
        const uint32_t writerAnchorRtpTs = 5000u;
        const uint32_t clockRate = 48000u;  // audio
        RtpStreamClock writerClock(writerAnchor, writerAnchorRtpTs, clockRate);

        const uint32_t srRtpTs = writerAnchorRtpTs + clockRate * 7u; // 7s later
        const NtpTime  srNtp = writerClock.toNtp(srRtpTs);

        RtpStreamClock receiverClock(srNtp, srRtpTs, clockRate);
        REQUIRE(receiverClock.isValid());

        // Probe an arbitrary RTP-TS that's 250ms past the SR (close
        // to a real lip-sync query).
        const uint32_t qRtpTs = srRtpTs + clockRate / 4u;
        const NtpTime  receiverNtp = receiverClock.toNtp(qRtpTs);
        const NtpTime  writerNtp = writerClock.toNtp(qRtpTs);

        // Compare in fractional-second packed units.  One audio
        // sample at 48 kHz is 1/48000 s = 89478.485... NTP fractional
        // units; budget is "< 1 sample" so we expect the integer
        // diff to be smaller than that.
        const uint64_t recvPacked =
                (static_cast<uint64_t>(receiverNtp.seconds()) << 32) | receiverNtp.fraction();
        const uint64_t writerPacked =
                (static_cast<uint64_t>(writerNtp.seconds()) << 32) | writerNtp.fraction();
        const uint64_t diff = (recvPacked >= writerPacked) ? recvPacked - writerPacked
                                                           : writerPacked - recvPacked;
        const uint64_t oneSampleFraction =
                (static_cast<uint64_t>(1) << 32) / clockRate;
        CHECK(diff < oneSampleFraction);
}

TEST_CASE("RtpStreamClock: setSr / setClockRate update validity") {
        RtpStreamClock c;
        c.setSr(NtpTime(100u, 0u), 1u);
        CHECK(c.isValid() == false); // still no clock rate
        c.setClockRate(48000u);
        CHECK(c.isValid() == true);
        c.setClockRate(0u);
        CHECK(c.isValid() == false); // dropping the rate disables it
}

TEST_CASE("RtpStreamClock: getters surface the configured anchor") {
        const NtpTime  anchor(123u, 456u);
        RtpStreamClock c(anchor, 42u, 90000u);
        CHECK(c.srNtp() == anchor);
        CHECK(c.srRtpTs() == 42u);
        CHECK(c.clockRate() == 90000u);
}
