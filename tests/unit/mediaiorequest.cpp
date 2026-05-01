/**
 * @file      tests/unit/mediaiorequest.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <promeki/duration.h>
#include <promeki/elapsedtimer.h>
#include <promeki/eventloop.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediaconfig.h>
#include <promeki/videoformat.h>
#include <promeki/pixelformat.h>

#include "mediaio_test_helpers.h"

using namespace promeki;
using promeki::tests::PausedTestMediaIO;

// ============================================================================
// MediaIORequest — basic shape
// ============================================================================
//
// The handle is a thin wrapper around shared state.  Default construction
// yields an "invalid" request that wait() reports as Error::Invalid; the
// resolved() factory builds an already-resolved request useful for
// short-circuit returns from the public API.
// ============================================================================

TEST_CASE("MediaIORequest default-construct is invalid") {
        MediaIORequest req;
        CHECK_FALSE(req.isValid());
        CHECK_FALSE(req.isReady());
        CHECK_FALSE(req.isCancelled());
        CHECK(req.wait() == Error::Invalid);
}

TEST_CASE("MediaIORequest::resolved short-circuits without dispatch") {
        auto req = MediaIORequest::resolved(Error::AlreadyOpen);
        CHECK(req.isValid());
        CHECK(req.isReady());
        CHECK(req.wait() == Error::AlreadyOpen);
}

TEST_CASE("MediaIORequest::then on already-resolved fires immediately") {
        auto              req = MediaIORequest::resolved(Error::Ok);
        std::atomic<bool> fired{false};
        Error             observed = Error::NotImplemented;
        req.then([&](Error e) {
                observed = e;
                fired.store(true);
        });
        // No EventLoop on this thread → callback runs synchronously.
        CHECK(fired.load());
        CHECK(observed == Error::Ok);
}

// ============================================================================
// Cancellation contract — three-state behavior per the devplan.
//
// Phase 9 implements states 1 and 2:
//   1. Not yet dispatched → strategy resolves with Error::Cancelled and
//      executeCmd is never called.  Verified by submitting a slow command
//      ahead of a cancelled one on the shared strand, then cancelling
//      before the strand reaches it.
//   2. Dispatched and executing on a non-blocking strategy → late
//      cancellation is ignored; the command runs to completion.  Verified
//      by cancelling after the strand has already started the command.
//
// State 3 (DedicatedThreadMediaIO + cancelBlockingWork) is Phase 10
// territory and not exercised here.
// ============================================================================

TEST_CASE("MediaIORequest::cancel before dispatch resolves with Error::Cancelled") {
        // PausedTestMediaIO queues commands without dispatching them
        // until the test calls processOne() / processAll().  This
        // makes the "cancelled before dispatch" branch deterministic
        // — the strand-style runner sees the cancellation flag set
        // and short-circuits to Error::Cancelled exactly the way
        // SharedThreadMediaIO would.
        PausedTestMediaIO io;
        MediaIORequest    openReq = io.open();
        io.processOne();
        REQUIRE(openReq.wait().isOk());

        MediaIORequest req = io.sendParams("nonexistent-op-for-cancel-test");
        REQUIRE(io.pending() == 1);
        req.cancel();
        CHECK(req.isCancelled());
        io.processOne();

        Error result = req.wait();
        CHECK(result == Error::Cancelled);
}

TEST_CASE("MediaIORequest::cancel is idempotent") {
        auto req = MediaIORequest::resolved(Error::Ok);
        req.cancel();
        req.cancel();
        req.cancel();
        CHECK(req.isCancelled());
        // Cancelling an already-resolved request does not change the
        // resolved value — the result was already produced.
        CHECK(req.wait() == Error::Ok);
}

// ============================================================================
// Per-command stats — every command carries a MediaIOStats container
// populated by the framework (queue wait, dispatch duration) plus any
// backend-specific keys the executeCmd hook chose to write.  A request's
// stats() accessor returns that snapshot.
// ============================================================================

TEST_CASE("MediaIORequest::stats carries framework-measured timing for every command") {
        // PausedTestMediaIO mirrors the per-command telemetry path
        // every strategy class records (QueueWaitDuration around the
        // submit→dispatch hop, ExecuteDuration around the executeCmd
        // hop).  The hooks default to Error::NotSupported on Params,
        // matching the historical TPG-backed assertion.
        PausedTestMediaIO io;
        io.onParams = [](MediaIOCommandParams &) {
                return Error::NotSupported;
        };

        MediaIORequest openReq = io.open();
        io.processOne();
        REQUIRE(openReq.wait().isOk());

        MediaIORequest req = io.sendParams("nonexistent-op-for-stats-test");
        io.processOne();
        Error err = req.wait();
        CHECK(err == Error::NotSupported);

        // Per-command stats are present on every request.  Both the
        // queue-wait and dispatch-duration keys are framework-set, so
        // they should both have entries (values >= 0; precise timing
        // depends on the test machine).
        MediaIOStats stats = req.stats();
        CHECK(stats.contains(MediaIOStats::QueueWaitDuration));
        CHECK(stats.contains(MediaIOStats::ExecuteDuration));
        CHECK(stats.getAs<Duration>(MediaIOStats::QueueWaitDuration).nanoseconds() >= 0);
        CHECK(stats.getAs<Duration>(MediaIOStats::ExecuteDuration).nanoseconds() >= 0);
}

// ============================================================================
// stats() on a sentinel-path request (no underlying command) returns
// an empty container — there's no command to read telemetry from.
// ============================================================================

TEST_CASE("MediaIORequest::stats is empty on resolved-error sentinel") {
        auto         req = MediaIORequest::resolved(Error::Ok);
        MediaIOStats stats = req.stats();
        CHECK_FALSE(stats.contains(MediaIOStats::QueueWaitDuration));
        CHECK_FALSE(stats.contains(MediaIOStats::ExecuteDuration));
}

// ============================================================================
// resolved(MediaIOCommand::Ptr) — the typed sentinel factory.
//
// Pre-populate a CmdRead, mark it resolved, and confirm the request
// fires immediately and the typed payload is reachable via
// commandAs<>.  This is the path public APIs take when they can fully
// service a request without going through the strand and want to
// surface a typed Output.
// ============================================================================

TEST_CASE("MediaIORequest::resolved(Ptr) carries a typed payload") {
        auto *cmdRead = new MediaIOCommandRead();
        cmdRead->result = Error::Ok;
        cmdRead->currentFrame = FrameNumber(42);
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdRead);

        auto req = MediaIORequest::resolved(cmd);
        CHECK(req.isValid());
        CHECK(req.isReady());
        CHECK(req.wait() == Error::Ok);

        const auto *back = req.commandAs<MediaIOCommandRead>();
        REQUIRE(back != nullptr);
        CHECK(back->currentFrame == FrameNumber(42));
}

// ============================================================================
// wait(timeoutMs) returns Timeout when the deadline expires before
// the underlying command resolves.
//
// Build a CmdParams the backend doesn't recognize and submit it on
// the strand, but ask wait() with a 0-millisecond budget.  Since
// 0 means "wait forever" by convention, we use a tiny non-zero
// budget instead and expect Timeout most of the time.  When the
// strand does happen to win, accept the resolved error; the
// purpose of the test is the Timeout branch.
//
// Sentinel-path requests (resolved(Error)) ignore timeout and
// return the resolved value immediately — verified separately.
// ============================================================================

TEST_CASE("MediaIORequest::wait(timeout) returns Timeout when deadline expires") {
        // PausedTestMediaIO holds the command in queue until the test
        // calls processOne().  wait(1) before drain therefore
        // deterministically reports Timeout, then processAll() drains
        // before close so no commands linger in flight.
        PausedTestMediaIO io;
        MediaIORequest    openReq = io.open();
        io.processOne();
        REQUIRE(openReq.wait().isOk());

        MediaIORequest req = io.sendParams("nonexistent-op-for-timeout-test");
        Error          early = req.wait(1);
        CHECK(early == Error::Timeout);
        io.processAll();
        (void)req.wait();

        MediaIORequest closeReq = io.close();
        io.processOne();
        (void)closeReq.wait();
}

// ============================================================================
// wait() ignores timeout for sentinel-path resolved requests — they
// return their baked-in result immediately regardless of the budget.
// ============================================================================

TEST_CASE("MediaIORequest::wait sentinel ignores timeout") {
        auto req = MediaIORequest::resolved(Error::EndOfFile);
        // 1ms budget — for a real cmd would Timeout; sentinel path
        // resolves immediately.
        Error e = req.wait(1);
        CHECK(e == Error::EndOfFile);
}

// ============================================================================
// then() async marshalling: the callback fires on the EventLoop active
// at the time of the then() registration, even when the request
// resolves on a different thread (the strand worker).
//
// This is the contract the per-port pump relies on — pump callables
// posted from within a then() chain land on the consumer thread
// rather than the strand worker, which is what makes the always-
// async API safe to chain across threads.
// ============================================================================

TEST_CASE("MediaIORequest::then marshalls async resolution through the calling EventLoop") {
        EventLoop       loop;
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
        cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
        cfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));

        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);
        REQUIRE(io->open().wait().isOk());

        // Capture the test-thread id so we can confirm the callback
        // ran here, not on the strand worker.
        const std::thread::id testThreadId = std::this_thread::get_id();

        std::atomic<bool> fired{false};
        std::atomic<bool> onTestThread{false};
        Error             observed = Error::NotImplemented;

        // Read on the source — the request resolves on the strand
        // worker.  We attach .then() on this thread, so the callback
        // must be marshalled back through `loop` and only run when
        // we processEvents().
        MediaIORequest req = io->source(0)->readFrame();
        req.then([&](Error e) {
                observed = e;
                onTestThread.store(std::this_thread::get_id() == testThreadId);
                fired.store(true);
        });

        // Pump the loop until the callback fires.  Without the
        // pump, the callback should NOT have run even after the
        // strand resolved the request — it's queued on `loop`'s
        // pending callable list.
        ElapsedTimer t;
        t.start();
        while (!fired.load() && t.elapsed() < 5000) {
                loop.processEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(fired.load());
        CHECK(observed == Error::Ok);
        CHECK(onTestThread.load());

        (void)io->close().wait();
        delete io;
}
