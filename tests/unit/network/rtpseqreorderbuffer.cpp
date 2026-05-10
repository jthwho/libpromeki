/**
 * @file      rtpseqreorderbuffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/duration.h>
#include <promeki/queue.h>
#include <promeki/rtppacket.h>
#include <promeki/rtpseqreorderbuffer.h>
#include <promeki/timestamp.h>
#include <thread>

using namespace promeki;

namespace {

// Convenience: build an RtpPacket with a specific seq number for
// identification.  The reorder buffer doesn't peek at the wire seq
// (that's the tracker's job upstream); but it's handy for tests to
// assert the right packet emerged.
RtpPacket make(uint16_t seq) {
        RtpPacket pkt(RtpPacket::HeaderSize);
        pkt.setVersion(2);
        pkt.setSequenceNumber(seq);
        return pkt;
}

} // namespace

TEST_CASE("RtpSeqReorderBuffer: in-order pass-through emits immediately") {
        RtpSeqReorderBuffer rb;
        Queue<RtpPacket>    out;
        for (uint32_t s = 100; s < 105; ++s) {
                rb.insert(make(static_cast<uint16_t>(s)), s, TimeStamp::now(), out);
                CHECK(out.size() == s - 99);
        }
        // Buffer must be drained — every insert was the next-expected.
        CHECK(rb.size() == 0u);
        auto stats = rb.snapshot();
        CHECK(stats.inserted == 5u);
        CHECK(stats.emittedInOrder == 5u);
        CHECK(stats.emittedOnDeadline == 0u);
}

TEST_CASE("RtpSeqReorderBuffer: small reorder window is reordered in place") {
        RtpSeqReorderBuffer rb;
        Queue<RtpPacket>    out;
        // Receive in arrival order: 100, 102, 101, 103, 104.
        // After 100 in: emit 100.  After 102: buffered (gap).
        // After 101 in: drain 101 + 102 in order.  After 103: emit.
        // After 104: emit.  Net: out has {100,101,102,103,104}.
        rb.insert(make(100), 100, TimeStamp::now(), out);
        rb.insert(make(102), 102, TimeStamp::now(), out);
        CHECK(rb.size() == 1u);
        rb.insert(make(101), 101, TimeStamp::now(), out);
        CHECK(rb.size() == 0u);
        rb.insert(make(103), 103, TimeStamp::now(), out);
        rb.insert(make(104), 104, TimeStamp::now(), out);
        CHECK(out.size() == 5u);
        for (uint16_t expected = 100; expected <= 104; ++expected) {
                auto r = out.tryPop();
                CHECK(r.second() == Error::Ok);
                CHECK(r.first().sequenceNumber() == expected);
        }
}

TEST_CASE("RtpSeqReorderBuffer: deadline-driven gap-fill emits behind a missing seq") {
        RtpSeqReorderBuffer::Config cfg;
        cfg.maxWindow = 8;
        cfg.playoutDelay = Duration::fromMilliseconds(20);
        RtpSeqReorderBuffer rb(cfg);
        Queue<RtpPacket>    out;

        // 100 arrives and emits.  101 is a real loss; 102 buffers
        // for 20 ms and is then emitted as deadline-fill.
        rb.insert(make(100), 100, TimeStamp::now(), out);
        rb.insert(make(102), 102, TimeStamp::now(), out);
        CHECK(rb.size() == 1u);
        // Sleep for slightly longer than playoutDelay, then "kick"
        // the buffer with another insert past the deadline.  Many
        // practical receivers run a periodic deadline tick; in
        // these tests we drive it by re-entering insert.
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        rb.insert(make(103), 103, TimeStamp::now(), out);
        // 102 was forced out by deadline, then 103 emits in order
        // (deadline-fill advanced the cursor past 101 and 102).
        CHECK(rb.size() == 0u);
        auto stats = rb.snapshot();
        CHECK(stats.emittedOnDeadline == 1u);
        CHECK(stats.emittedInOrder == 2u); // 100 and 103

        // Out queue carries 100, 102, 103 in that order — 101 is gone.
        CHECK(out.size() == 3u);
        CHECK(out.tryPop().first().sequenceNumber() == 100);
        CHECK(out.tryPop().first().sequenceNumber() == 102);
        CHECK(out.tryPop().first().sequenceNumber() == 103);
}

TEST_CASE("RtpSeqReorderBuffer: drop-oldest on overflow") {
        RtpSeqReorderBuffer::Config cfg;
        cfg.maxWindow = 3;
        RtpSeqReorderBuffer rb(cfg);
        Queue<RtpPacket>    out;
        // Insert seq 100 first to anchor the cursor.  Then create a
        // gap by inserting seq 110, 111, 112 — buffer fills to 3.
        // Inserting 113 evicts 110 (oldest), advancing the cursor
        // past 109 to 111, which then triggers the in-order tail
        // drain of {111, 112, 113}.
        rb.insert(make(100), 100, TimeStamp::now(), out); // emits
        rb.insert(make(110), 110, TimeStamp::now(), out);
        rb.insert(make(111), 111, TimeStamp::now(), out);
        rb.insert(make(112), 112, TimeStamp::now(), out);
        CHECK(rb.size() == 3u);
        rb.insert(make(113), 113, TimeStamp::now(), out);
        // After overflow eviction the cursor advanced past 110 to
        // 111, so the buffered tail drained in-order.
        CHECK(rb.size() == 0u);
        auto stats = rb.snapshot();
        CHECK(stats.droppedOnOverflow == 1u);
        // Output: 100, 111, 112, 113 (110 was evicted before any
        // emission, so it never reaches the depacketizer).
        CHECK(out.size() == 4u);
        CHECK(out.tryPop().first().sequenceNumber() == 100);
        CHECK(out.tryPop().first().sequenceNumber() == 111);
        CHECK(out.tryPop().first().sequenceNumber() == 112);
        CHECK(out.tryPop().first().sequenceNumber() == 113);
}

TEST_CASE("RtpSeqReorderBuffer: silent dup discard for already-emitted seq") {
        RtpSeqReorderBuffer rb;
        Queue<RtpPacket>    out;
        rb.insert(make(100), 100, TimeStamp::now(), out);
        rb.insert(make(101), 101, TimeStamp::now(), out);
        // 100 again — already emitted (cursor is past 100).
        rb.insert(make(100), 100, TimeStamp::now(), out);
        auto stats = rb.snapshot();
        CHECK(stats.droppedAsDuplicate == 1u);
        // Output still has only 2 emissions.
        CHECK(out.size() == 2u);
}

TEST_CASE("RtpSeqReorderBuffer: silent dup discard for already-buffered seq") {
        RtpSeqReorderBuffer rb;
        Queue<RtpPacket>    out;
        // 100 emits.  102 buffers (gap before 101).  Inserting 102
        // again is a dup — silent.
        rb.insert(make(100), 100, TimeStamp::now(), out);
        rb.insert(make(102), 102, TimeStamp::now(), out);
        rb.insert(make(102), 102, TimeStamp::now(), out);
        auto stats = rb.snapshot();
        CHECK(stats.droppedAsDuplicate == 1u);
        CHECK(rb.size() == 1u);
}

TEST_CASE("RtpSeqReorderBuffer: flush drains buffered tail in seq order") {
        RtpSeqReorderBuffer rb;
        Queue<RtpPacket>    out;
        // 100 emits, then 102 + 103 buffer.
        rb.insert(make(100), 100, TimeStamp::now(), out);
        rb.insert(make(102), 102, TimeStamp::now(), out);
        rb.insert(make(103), 103, TimeStamp::now(), out);
        CHECK(rb.size() == 2u);
        rb.flush(out);
        CHECK(rb.size() == 0u);
        // Output: 100, then flushed-in-order 102, 103.
        CHECK(out.size() == 3u);
        CHECK(out.tryPop().first().sequenceNumber() == 100);
        CHECK(out.tryPop().first().sequenceNumber() == 102);
        CHECK(out.tryPop().first().sequenceNumber() == 103);
        auto stats = rb.snapshot();
        // Flushed entries count as deadline-fill (the gap was
        // closed by force at flush time).
        CHECK(stats.emittedOnDeadline == 2u);
}

TEST_CASE("RtpSeqReorderBuffer: clear discards buffered tail without emitting") {
        RtpSeqReorderBuffer rb;
        Queue<RtpPacket>    out;
        rb.insert(make(100), 100, TimeStamp::now(), out);
        rb.insert(make(102), 102, TimeStamp::now(), out);
        CHECK(rb.size() == 1u);
        rb.clear();
        CHECK(rb.size() == 0u);
        // Output still has only the in-order emission of 100.
        CHECK(out.size() == 1u);
        // After clear, the cursor is reset.  Inserting 200 next
        // anchors the cursor at 200 and emits in-order.
        rb.insert(make(200), 200, TimeStamp::now(), out);
        CHECK(out.size() == 2u);
}

TEST_CASE("RtpSeqReorderBuffer: zero playoutDelay never deadline-fills") {
        RtpSeqReorderBuffer rb; // default playoutDelay = 0
        Queue<RtpPacket>    out;
        rb.insert(make(100), 100, TimeStamp::now(), out);
        rb.insert(make(102), 102, TimeStamp::now(), out);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // Even after 20 ms, the deadline path is not active because
        // playoutDelay is zero.  102 stays buffered until either an
        // in-order fill or the buffer overflows.
        rb.insert(make(103), 103, TimeStamp::now(), out);
        auto stats = rb.snapshot();
        CHECK(stats.emittedOnDeadline == 0u);
        CHECK(rb.size() == 2u);
}

TEST_CASE("RtpSeqReorderBuffer: out-of-window backwards arrival is dropped as dup") {
        RtpSeqReorderBuffer rb;
        Queue<RtpPacket>    out;
        rb.insert(make(100), 100, TimeStamp::now(), out);
        rb.insert(make(101), 101, TimeStamp::now(), out);
        rb.insert(make(102), 102, TimeStamp::now(), out);
        // The cursor is now at 103; arriving 99 is strictly behind
        // — duplicate / pre-window seq.
        rb.insert(make(99), 99, TimeStamp::now(), out);
        auto stats = rb.snapshot();
        CHECK(stats.droppedAsDuplicate == 1u);
}
