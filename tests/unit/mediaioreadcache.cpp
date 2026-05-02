/**
 * @file      tests/unit/mediaioreadcache.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <chrono>
#include <thread>

#include <doctest/doctest.h>
#include <promeki/elapsedtimer.h>
#include <promeki/eventloop.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosource.h>
#include <promeki/objectbase.tpp>
#include <promeki/pixelformat.h>
#include <promeki/thread.h>
#include <promeki/videoformat.h>

using namespace promeki;

// ============================================================================
// MediaIOReadCache is the prefetch buffer that sits between
// MediaIOSource::readFrame and MediaIO::submit.  The cache itself is a
// private member of MediaIOSource, so these tests drive it through the
// source's public surface (setPrefetchDepth, readFrame, readyReads,
// frameAvailable, cancelPending) plus a handful of group-level
// observers (pendingReads).
//
// The TPG backend is used throughout — it's an infinite synthetic
// source with no external dependencies, so we can poke the cache at
// will without sequencing fixtures.
// ============================================================================

namespace {

MediaIO *makeTpg() {
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        cfg.set(MediaConfig::VideoEnabled, true);
        cfg.set(MediaConfig::AudioEnabled, false);
        cfg.set(MediaConfig::TimecodeEnabled, false);
        cfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());
        REQUIRE(io->sourceCount() > 0);
        return io;
}

template <typename Pred> bool waitFor(Pred pred, int64_t timeoutMs = 2000) {
        ElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
                if (pred()) return true;
                Thread::sleepMs(1);
        }
        return pred();
}

template <typename Pred> bool pumpUntil(EventLoop &loop, Pred pred, int64_t timeoutMs = 2000) {
        ElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
                loop.processEvents();
                if (pred()) return true;
                Thread::sleepMs(1);
        }
        return pred();
}

} // namespace

// ============================================================================
// setPrefetchDepth clamps to a minimum of 1.
// ============================================================================

TEST_CASE("MediaIOReadCache: setPrefetchDepth clamps to >= 1") {
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        src->setPrefetchDepth(8);
        CHECK(src->prefetchDepth() == 8);

        src->setPrefetchDepth(1);
        CHECK(src->prefetchDepth() == 1);

        src->setPrefetchDepth(0);
        CHECK(src->prefetchDepth() == 1);

        src->setPrefetchDepth(-5);
        CHECK(src->prefetchDepth() == 1);

        src->setPrefetchDepth(4);
        CHECK(src->prefetchDepth() == 4);

        (void)io->close().wait();
        delete io;
}

// ============================================================================
// readFrame on a cold cache submits one cmd, vends it, and tops up.
//
// With prefetchDepth=4, the first readFrame should:
//   1. Submit one cmd (since the cache is empty), vend it, and
//   2. Top up to depth-1 (because the vended cmd is still in flight)
//      OR depth (if the vended cmd resolved before top-up).
//
// Either way, after the call returns, readyReads + 1 (the vended cmd)
// should equal at most depth, and pendingReads should reflect what's
// still in flight.
// ============================================================================

TEST_CASE("MediaIOReadCache: readFrame on cold cache vends and tops up") {
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        src->setPrefetchDepth(4);
        CHECK(src->readyReads() == 0);

        MediaIORequest req = src->readFrame();
        CHECK(req.isValid());
        // After the call returns the cache is topped up.  We don't
        // know whether the vended cmd already finished (cache may
        // hold depth slots) or is still in flight (depth-1 slots),
        // so just bound from above: never more than the configured
        // depth.
        CHECK(src->readyReads() <= 4);

        // Drive the request to completion and confirm the cmd
        // produced a frame.
        REQUIRE(req.wait().isOk());
        const auto *cr = req.commandAs<MediaIOCommandRead>();
        REQUIRE(cr != nullptr);
        CHECK(cr->frame.isValid());

        (void)io->close().wait();
        delete io;
}

// ============================================================================
// Prefetch keeps the in-flight + cached total at <= depth.
//
// After several drained reads the cache should never exceed the
// configured depth.  This is the core invariant of the cache.
// ============================================================================

TEST_CASE("MediaIOReadCache: total in-flight never exceeds depth") {
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        src->setPrefetchDepth(3);

        for (int i = 0; i < 10; ++i) {
                MediaIORequest req = src->readFrame();
                REQUIRE(req.wait().isOk());
                // Sum of: cmds the cache currently holds (some
                // in-flight, some completed) + cmds the source's
                // group reports as pending.  Pending counts may
                // briefly diverge from the cache during a top-up,
                // but the cache's count is never above depth.
                CHECK(src->readyReads() <= 3);
                CHECK(src->pendingReads() <= 3);
        }

        (void)io->close().wait();
        delete io;
}

// ============================================================================
// frameAvailable transitions to true once the head completes.
//
// Submit a read, then poll frameAvailable() on the source — it
// should become true within a reasonable budget once the strand
// completes the head.
// ============================================================================

TEST_CASE("MediaIOReadCache: frameAvailable becomes true once head completes") {
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        src->setPrefetchDepth(2);
        // Initially nothing is in the cache.
        CHECK_FALSE(src->frameAvailable());

        MediaIORequest req = src->readFrame();
        // The vend pop drained the head, and the cache topped up to
        // depth-1.  The new head is in flight; wait until it
        // becomes ready.
        REQUIRE(req.wait().isOk());
        // After the head completes the cache should report ready.
        CHECK(waitFor([&]() { return src->frameAvailable(); }, 1000));

        (void)io->close().wait();
        delete io;
}

// ============================================================================
// frameReady fires on the cache-head ready edge — exactly once
// between consecutive readFrame pops.
//
// We attach to the source's frameReady signal, perform a sequence
// of reads, and check the signal count exceeds zero and tracks the
// number of "head-becomes-ready" transitions (which equals the
// number of completed head pops).  The signal is coalesced — re-
// arming only happens once between pops — so the count must equal
// at most the number of vended frames.
// ============================================================================

TEST_CASE("MediaIOReadCache: frameReady fires on head-ready edge, coalesced per pop") {
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        std::atomic<int> readyCount{0};
        src->frameReadySignal.connect([&]() { readyCount.fetch_add(1); }, src);

        src->setPrefetchDepth(3);

        constexpr int kReads = 6;
        for (int i = 0; i < kReads; ++i) {
                MediaIORequest req = src->readFrame();
                REQUIRE(req.wait().isOk());
        }
        // Give the strand a moment to settle so the last few edges
        // have fired.
        (void)waitFor([&]() { return readyCount.load() >= 1; }, 1000);

        // Coalescing rule: frameReady fires at most once per
        // readFrame pop, so the total count cannot exceed the
        // number of pops.  It also must be > 0 — we did read frames.
        CHECK(readyCount.load() > 0);
        CHECK(readyCount.load() <= kReads);

        (void)io->close().wait();
        delete io;
}

// ============================================================================
// cancelPending drains the cache and silences in-flight prefetch.
//
// After a few read submissions, cancelPending should return the
// number of dropped queue entries and leave the cache empty.  The
// counter on the group eventually decrements as each cancelled cmd
// flows through completeCommand.
// ============================================================================

TEST_CASE("MediaIOReadCache: cancelPending drains the cache and clears readyReads") {
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        src->setPrefetchDepth(4);
        // Prime the cache by pulling the cold-start vend so the
        // top-up runs.  Wait for the request to complete so the
        // strand has time to enqueue the prefetched siblings.
        MediaIORequest first = src->readFrame();
        REQUIRE(first.wait().isOk());
        // Now wait until the cache fills back up (the strand may
        // still be working on the top-up).
        (void)waitFor([&]() { return src->readyReads() > 0; }, 1000);

        const int beforeReady = src->readyReads();
        size_t    dropped = src->cancelPending();
        CHECK(dropped == static_cast<size_t>(beforeReady));
        CHECK(src->readyReads() == 0);

        (void)io->close().wait();
        delete io;
}

// ============================================================================
// pushSyntheticResult: closing the source emits a single trailing
// EndOfFile through the cache so polling consumers observe the
// shutdown via the same readFrame path as a normal frame delivery.
//
// This exercises the synthetic-EOS path in MediaIO::completeCommand
// (which calls cache.pushSyntheticResult) plus
// MediaIOSource::readFrame's "drain cache before isOpen check" rule.
// ============================================================================

TEST_CASE("MediaIOReadCache: synthetic EOS surfaces through readFrame after close") {
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        // Pull one frame so the cache has been exercised at least
        // once before the close.
        REQUIRE(src->readFrame().wait().isOk());

        // Close should push the synthetic EOS.  The close itself
        // resolves Ok.
        REQUIRE(io->close().wait().isOk());

        // Drain the cache: every still-queued cmd resolves first
        // (with Ok or Cancelled depending on timing), then the
        // synthetic EOS.  Loop until we observe EndOfFile.  Bound
        // the loop generously — there can be several real reads
        // ahead of the EOS depending on prefetch depth.
        bool sawEof = false;
        for (int i = 0; i < 32 && !sawEof; ++i) {
                MediaIORequest r = src->readFrame();
                Error          e = r.wait();
                if (e == Error::EndOfFile) sawEof = true;
        }
        CHECK(sawEof);

        delete io;
}

// ============================================================================
// frameReady fires once for the synthetic EOS after close.
//
// Attaching to frameReady before close, then closing the MediaIO,
// must produce at least one new frameReady edge so polling consumers
// observe the EOS through the signal path rather than having to
// implement timeouts.
// ============================================================================

TEST_CASE("MediaIOReadCache: pushSyntheticResult re-arms frameReady on close") {
        EventLoop      loop;
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        std::atomic<int> readyCount{0};
        src->frameReadySignal.connect([&]() { readyCount.fetch_add(1); }, src);

        // Drain whatever the source may have prefetched on its
        // own at open.  We just want a clean baseline.
        (void)src->cancelPending();
        const int baseline = readyCount.load();

        // Close pushes the synthetic EOS into the cache, which
        // re-evaluates the armed flag and emits frameReady.
        // frameReady is a signal — it goes through the EventLoop
        // affinity machinery, so we need to pump.
        REQUIRE(io->close().wait().isOk());
        REQUIRE(pumpUntil(loop, [&]() { return readyCount.load() > baseline; }, 1000));

        // Drain the EOS so the test ends in a clean state.
        for (int i = 0; i < 32; ++i) {
                if (src->readFrame().wait() == Error::EndOfFile) break;
        }

        delete io;
}

// ============================================================================
// readFrame post-close: EOS first, then NotOpen on every subsequent call.
//
// After close, the cache holds (in order): any reads still queued
// from before close, then a single synthetic EOS pushed by
// MediaIO::completeCommand.  Vending past the EOS empties the cache;
// since submitOneLocked refuses to issue fresh prefetch against a
// closed MediaIO, the cache stays empty and readFrame's isOpen
// short-circuit fires NotOpen.
// ============================================================================

TEST_CASE("MediaIOReadCache: readFrame after close + drained EOS returns NotOpen") {
        MediaIO       *io = makeTpg();
        MediaIOSource *src = io->source(0);

        REQUIRE(src->readFrame().wait().isOk());
        REQUIRE(io->close().wait().isOk());

        // Drain everything until we observe the EOS.  No NotOpens
        // are allowed before the EOS — the cache must surface
        // every queued cmd plus the EOS first.
        bool sawEof = false;
        for (int i = 0; i < 32 && !sawEof; ++i) {
                MediaIORequest r = src->readFrame();
                Error          e = r.wait();
                if (e == Error::EndOfFile) {
                        sawEof = true;
                } else {
                        // Real cmds resolve as Ok or Cancelled
                        // depending on race with close.  NotOpen
                        // would mean the EOS was lost.
                        CHECK(e != Error::NotOpen);
                }
        }
        REQUIRE(sawEof);

        // After EOS the cache is empty and submitOneLocked refuses
        // to top up because isOpen() is false — every subsequent
        // call short-circuits to NotOpen.
        for (int i = 0; i < 3; ++i) {
                MediaIORequest tail = src->readFrame();
                CHECK(tail.wait() == Error::NotOpen);
        }

        delete io;
}
