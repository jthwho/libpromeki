/**
 * @file      tests/mediaioportconnection.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <promeki/eventloop.h>
#include <promeki/frame.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaioportconnection.h>
#include <promeki/objectbase.tpp>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediaconfig.h>
#include <promeki/elapsedtimer.h>
#include <promeki/enums.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/dir.h>
#include <promeki/file.h>
#include <promeki/framerate.h>
#include <promeki/pixelformat.h>
#include <promeki/thread.h>
#include <promeki/videoformat.h>

#include "mediaio_test_helpers.h"

using namespace promeki;
using promeki::tests::PausedTestMediaIO;

namespace {

template <typename Pred> bool pumpUntil(EventLoop &loop, Pred pred, int64_t timeoutMs = 2000) {
        ElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs) {
                loop.processEvents();
                if (pred()) return true;
                Thread::sleepMs(5);
        }
        return false;
}

} // namespace

// ============================================================================
// MediaIOPortConnection drives a TPG source into an Inspector sink
//
// Validates the signal-driven drain pump: frameReady on the source
// triggers a non-blocking read; the result is forwarded to the sink
// via a non-blocking write, which in turn frees up sink capacity and
// fires frameWanted to keep the loop running.
// ============================================================================

TEST_CASE("MediaIOPortConnection forwards frames TPG -> Inspector") {
        EventLoop loop;

        // Source: TPG generating a small 30 fps RGBA stream.
        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());
        REQUIRE(src->sourceCount() > 0);

        // Sink: Inspector running the cheapest test set so the per-frame
        // cost stays small.  Disabled-by-default tests keep the loop
        // close to a pure consumer.
        MediaIO::Config sinkCfg = MediaIOFactory::defaultConfig("Inspector");
        sinkCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        sinkCfg.set(MediaConfig::InspectorDropFrames, false);
        sinkCfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
        sinkCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        MediaIO *sink = MediaIO::create(sinkCfg);
        REQUIRE(sink != nullptr);
        // The inspector needs to know the upstream's mediaDesc to size
        // its check buffers — pass it through the pre-open hint.
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());
        REQUIRE(sink->sinkCount() > 0);

        MediaIOPortConnection conn(src->source(0), sink->sink(0));
        REQUIRE(conn.start().isOk());
        REQUIRE(conn.isRunning());

        // The TPG is an infinite source; pump until at least 5 frames
        // have crossed the wire.  The pump runs on signal callbacks
        // delivered through this loop, so processEvents() drives the
        // forward progress.
        REQUIRE(pumpUntil(loop, [&]() { return conn.framesTransferred() >= 5; }, 5000));
        CHECK(conn.framesTransferred() >= 5);

        conn.stop();
        CHECK_FALSE(conn.isRunning());

        (void)src->close().wait();
        (void)sink->close().wait();
        delete src;
        delete sink;
}

// ============================================================================
// upstreamDone fires when the source closes mid-stream
// ============================================================================

TEST_CASE("MediaIOPortConnection emits upstreamDone when source closes") {
        EventLoop loop;

        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO::Config sinkCfg = MediaIOFactory::defaultConfig("Inspector");
        sinkCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        sinkCfg.set(MediaConfig::InspectorDropFrames, false);
        sinkCfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
        sinkCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        MediaIO *sink = MediaIO::create(sinkCfg);
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        MediaIOPortConnection conn(src->source(0), sink->sink(0));
        std::atomic<int>      doneCount{0};
        conn.upstreamDoneSignal.connect([&doneCount]() { doneCount.fetch_add(1); }, &conn);
        REQUIRE(conn.start().isOk());

        // Let a few frames flow first to confirm the loop is active.
        REQUIRE(pumpUntil(loop, [&]() { return conn.framesTransferred() >= 3; }, 5000));

        // Closing the source pushes a synthetic EOF onto its read
        // queue which pump() picks up and translates to upstreamDone.
        REQUIRE(src->close().wait().isOk());

        REQUIRE(pumpUntil(loop, [&]() { return doneCount.load() > 0; }, 2000));
        CHECK(conn.upstreamDone());

        conn.stop();
        (void)sink->close().wait();
        delete src;
        delete sink;
}

// ============================================================================
// Fan-out: one source feeding two sinks
//
// Validates that a single MediaIOPortConnection forwards each source
// frame to both sinks with independent back-pressure and per-sink
// stats.
// ============================================================================

TEST_CASE("MediaIOPortConnection fans one source out to multiple sinks") {
        EventLoop loop;

        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        auto makeSink = [&](MediaIO *upstream) {
                MediaIO::Config cfg = MediaIOFactory::defaultConfig("Inspector");
                cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                cfg.set(MediaConfig::InspectorDropFrames, false);
                cfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
                cfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
                MediaIO *s = MediaIO::create(cfg);
                REQUIRE(s != nullptr);
                REQUIRE(s->setPendingMediaDesc(upstream->mediaDesc()).isOk());
                REQUIRE(s->open().wait().isOk());
                return s;
        };

        MediaIO *sinkA = makeSink(src);
        MediaIO *sinkB = makeSink(src);

        MediaIOPortConnection conn(src->source(0));
        REQUIRE(conn.addSink(sinkA->sink(0)).isOk());
        REQUIRE(conn.addSink(sinkB->sink(0)).isOk());
        REQUIRE(conn.start().isOk());

        REQUIRE(pumpUntil(loop,
                          [&]() {
                                  return conn.framesWritten(sinkA->sink(0)) >= 5 &&
                                         conn.framesWritten(sinkB->sink(0)) >= 5;
                          },
                          5000));
        CHECK(conn.framesTransferred() >= 5);
        CHECK(conn.framesWritten(sinkA->sink(0)) >= 5);
        CHECK(conn.framesWritten(sinkB->sink(0)) >= 5);

        conn.stop();
        (void)src->close().wait();
        (void)sinkA->close().wait();
        (void)sinkB->close().wait();
        delete src;
        delete sinkA;
        delete sinkB;
}

// ============================================================================
// Per-sink frame-count cap fires sinkLimitReached and stops dispatching
//
// Cap one sink at 4 frames; the other runs uncapped.  Verifies the
// capped sink sees ~4 frames (cut at next safe cut point), the
// uncapped sink keeps receiving, and sinkLimitReached fires for the
// capped sink only.  Once every sink stops the connection emits
// allSinksDone (validated indirectly: the capped sink's count
// stabilises while the uncapped one keeps going).
// ============================================================================

TEST_CASE("MediaIOPortConnection honours per-sink frame-count cap") {
        EventLoop loop;

        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        auto makeSink = [&]() {
                MediaIO::Config cfg = MediaIOFactory::defaultConfig("Inspector");
                cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                cfg.set(MediaConfig::InspectorDropFrames, false);
                cfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
                cfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
                MediaIO *s = MediaIO::create(cfg);
                REQUIRE(s != nullptr);
                REQUIRE(s->setPendingMediaDesc(src->mediaDesc()).isOk());
                REQUIRE(s->open().wait().isOk());
                return s;
        };

        MediaIO *capped = makeSink();
        MediaIO *uncapped = makeSink();

        MediaIOPortConnection conn(src->source(0));
        REQUIRE(conn.addSink(capped->sink(0), FrameCount(4)).isOk());
        REQUIRE(conn.addSink(uncapped->sink(0)).isOk());

        std::atomic<int>          limitedCount{0};
        std::atomic<MediaIOSink *> limitedSink{nullptr};
        conn.sinkLimitReachedSignal.connect(
                [&](MediaIOSink *s) {
                        limitedCount.fetch_add(1);
                        limitedSink.store(s);
                },
                &conn);

        REQUIRE(conn.start().isOk());

        REQUIRE(pumpUntil(loop, [&]() { return limitedCount.load() >= 1; }, 5000));
        CHECK(limitedSink.load() == capped->sink(0));
        CHECK(conn.sinkStopped(capped->sink(0)));
        CHECK_FALSE(conn.sinkStopped(uncapped->sink(0)));

        const int64_t cappedAtLimit = conn.framesWritten(capped->sink(0));
        CHECK(cappedAtLimit >= 4);

        // Confirm the uncapped sink keeps receiving past the capped
        // sink's stop and the capped sink's count stays put.
        REQUIRE(pumpUntil(loop,
                          [&]() { return conn.framesWritten(uncapped->sink(0)) >= cappedAtLimit + 3; },
                          5000));
        CHECK(conn.framesWritten(capped->sink(0)) == cappedAtLimit);

        conn.stop();
        (void)src->close().wait();
        (void)capped->close().wait();
        (void)uncapped->close().wait();
        delete src;
        delete capped;
        delete uncapped;
}

// ============================================================================
// allSinksDone fires after every sink has stopped via per-sink caps
// ============================================================================

TEST_CASE("MediaIOPortConnection emits allSinksDone when every sink is capped") {
        EventLoop loop;

        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        auto makeSink = [&]() {
                MediaIO::Config cfg = MediaIOFactory::defaultConfig("Inspector");
                cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
                cfg.set(MediaConfig::InspectorDropFrames, false);
                cfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
                cfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
                MediaIO *s = MediaIO::create(cfg);
                REQUIRE(s != nullptr);
                REQUIRE(s->setPendingMediaDesc(src->mediaDesc()).isOk());
                REQUIRE(s->open().wait().isOk());
                return s;
        };

        MediaIO *a = makeSink();
        MediaIO *b = makeSink();

        MediaIOPortConnection conn(src->source(0));
        REQUIRE(conn.addSink(a->sink(0), FrameCount(2)).isOk());
        REQUIRE(conn.addSink(b->sink(0), FrameCount(3)).isOk());

        std::atomic<int> doneCount{0};
        conn.allSinksDoneSignal.connect([&doneCount]() { doneCount.fetch_add(1); }, &conn);

        REQUIRE(conn.start().isOk());
        REQUIRE(pumpUntil(loop, [&]() { return doneCount.load() >= 1; }, 5000));
        CHECK(conn.allSinksDone());
        CHECK(conn.sinkStopped(a->sink(0)));
        CHECK(conn.sinkStopped(b->sink(0)));

        conn.stop();
        (void)src->close().wait();
        (void)a->close().wait();
        (void)b->close().wait();
        delete src;
        delete a;
        delete b;
}

// ============================================================================
// addSink is rejected after start
// ============================================================================

TEST_CASE("MediaIOPortConnection rejects addSink after start") {
        EventLoop loop;

        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO::Config sinkCfg = MediaIOFactory::defaultConfig("Inspector");
        sinkCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        sinkCfg.set(MediaConfig::InspectorDropFrames, false);
        sinkCfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
        sinkCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        MediaIO *sink = MediaIO::create(sinkCfg);
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        MediaIO *late = MediaIO::create(sinkCfg);
        REQUIRE(late != nullptr);
        REQUIRE(late->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(late->open().wait().isOk());

        MediaIOPortConnection conn(src->source(0));
        REQUIRE(conn.addSink(sink->sink(0)).isOk());
        REQUIRE(conn.start().isOk());

        Error e = conn.addSink(late->sink(0));
        CHECK(e == Error::Busy);

        conn.stop();
        (void)src->close().wait();
        (void)sink->close().wait();
        (void)late->close().wait();
        delete src;
        delete sink;
        delete late;
}

// ============================================================================
// Cascade through a transform stage
//
// Regression for the wait(0)-blocks-EventLoop / missing-symmetric-edge
// pair: a pure-source TPG cannot trigger them because its strand
// returns frames synchronously and never drives the
// transform-style @c TryAgain path.  Two connections sharing a CSC in
// the middle do — the second connection's first read submits to the
// CSC strand before any upstream Write has reached it, so
// @c executeCmd(Read) returns @c Error::TryAgain and the connection
// must yield until the upstream's Write fires
// @c MediaIOSource::frameReadySignal on CSC's source port.  If the
// connection blocks on @c wait(0) (broken contract: timeoutMs==0
// means "wait indefinitely") or the @c MediaIO::completeCommand
// Write→source-frameReady kick is missing, this test deadlocks.
// ============================================================================

TEST_CASE("MediaIOPortConnection cascades through a transform (TPG -> CSC -> Inspector)") {
        EventLoop loop;

        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO::Config cscCfg = MediaIOFactory::defaultConfig("CSC");
        cscCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        MediaIO *csc = MediaIO::create(cscCfg);
        REQUIRE(csc != nullptr);
        REQUIRE(csc->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(csc->open().wait().isOk());

        MediaIO::Config sinkCfg = MediaIOFactory::defaultConfig("Inspector");
        sinkCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        sinkCfg.set(MediaConfig::InspectorDropFrames, false);
        sinkCfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
        sinkCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        MediaIO *sink = MediaIO::create(sinkCfg);
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(csc->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        MediaIOPortConnection upstream(src->source(0), csc->sink(0));
        MediaIOPortConnection downstream(csc->source(0), sink->sink(0));
        REQUIRE(upstream.start().isOk());
        REQUIRE(downstream.start().isOk());

        // Drive the loop until at least 5 frames have crossed both
        // connections end-to-end.  A fixed 5 s deadline is generous
        // enough for CI noise but tight enough to catch a deadlocked
        // pump (the original bug surfaced as an indefinite hang).
        REQUIRE(pumpUntil(loop, [&]() { return downstream.framesTransferred() >= 5; }, 5000));
        CHECK(upstream.framesTransferred() >= 5);
        CHECK(downstream.framesTransferred() >= 5);

        upstream.stop();
        downstream.stop();
        (void)src->close().wait();
        (void)csc->close().wait();
        (void)sink->close().wait();
        delete src;
        delete csc;
        delete sink;
}

// ============================================================================
// Transform-output drain releases upstream back-pressure
//
// Regression for the symmetric Read→sink-frameWanted edge:
// @c MediaIOSink::writesAccepted folds the backend's
// @c pendingInternalWrites (e.g. CSC's output queue size) into its
// capacity calculation, so a transform whose output has filled but is
// not being drained back-pressures its upstream sink to zero
// capacity.  Without the fix, the upstream connection has no signal
// to wake on once the downstream drains — @c MediaIO::completeCommand
// never fires @c frameWanted on the sink purely because of a Read.
// This test creates that exact stall by holding the downstream
// connection paused while the upstream fills the transform, then
// lets the downstream run and verifies the upstream resumes.
// ============================================================================

TEST_CASE("MediaIOPortConnection unblocks upstream when downstream drains the transform") {
        EventLoop loop;

        MediaIO::Config srcCfg = MediaIOFactory::defaultConfig("TPG");
        srcCfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
        srcCfg.set(MediaConfig::VideoEnabled, true);
        srcCfg.set(MediaConfig::AudioEnabled, false);
        srcCfg.set(MediaConfig::TimecodeEnabled, false);
        srcCfg.set(MediaConfig::VideoBurnEnabled, false);
        MediaIO *src = MediaIO::create(srcCfg);
        REQUIRE(src != nullptr);
        REQUIRE(src->open().wait().isOk());

        MediaIO::Config cscCfg = MediaIOFactory::defaultConfig("CSC");
        cscCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
        MediaIO *csc = MediaIO::create(cscCfg);
        REQUIRE(csc != nullptr);
        REQUIRE(csc->setPendingMediaDesc(src->mediaDesc()).isOk());
        REQUIRE(csc->open().wait().isOk());

        MediaIO::Config sinkCfg = MediaIOFactory::defaultConfig("Inspector");
        sinkCfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
        sinkCfg.set(MediaConfig::InspectorDropFrames, false);
        sinkCfg.set(MediaConfig::InspectorTests, EnumList::forType<InspectorTest>());
        sinkCfg.set(MediaConfig::InspectorLogIntervalSec, 0.0);
        MediaIO *sink = MediaIO::create(sinkCfg);
        REQUIRE(sink != nullptr);
        REQUIRE(sink->setPendingMediaDesc(csc->mediaDesc()).isOk());
        REQUIRE(sink->open().wait().isOk());

        MediaIOPortConnection upstream(src->source(0), csc->sink(0));
        MediaIOPortConnection downstream(csc->source(0), sink->sink(0));

        // Start only the upstream so the transform's output queue
        // backs up — every successful Write to CSC accumulates one
        // entry in @c CscMediaIO::_outputQueue, which counts against
        // @c writesAccepted.  Eventually the transform refuses
        // further writes and the upstream connection parks at the
        // back-pressure gate inside @ref pump.
        REQUIRE(upstream.start().isOk());

        // Drive forward until the upstream stops making progress at
        // a steady frame count (i.e. it is back-pressured).  A real
        // pipeline would observe the sink's @c writesAccepted hit
        // zero; we proxy that observation by watching for a
        // multi-pass plateau on @c framesTransferred.
        const int64_t plateauPolls = 20;
        int64_t       lastFrames = 0;
        int64_t       stableCount = 0;
        ElapsedTimer  guard;
        guard.start();
        while (guard.elapsed() < 5000 && stableCount < plateauPolls) {
                loop.processEvents();
                int64_t now = upstream.framesTransferred();
                if (now == lastFrames && now > 0) {
                        ++stableCount;
                } else {
                        stableCount = 0;
                        lastFrames = now;
                }
                Thread::sleepMs(2);
        }
        REQUIRE(stableCount >= plateauPolls);
        const int64_t backpressureFrames = upstream.framesTransferred();
        REQUIRE(backpressureFrames > 0);

        // Now start the downstream.  Each Read it issues drains a
        // slot from CSC's output queue → @c writesAccepted on
        // CSC's sink ticks back up → upstream's
        // @c frameWantedSignal slot kicks pump.  Without the
        // Read→sink-frameWanted edge in
        // @ref MediaIO::completeCommand, no signal would fire and
        // both connections would sit idle forever.
        REQUIRE(downstream.start().isOk());

        REQUIRE(pumpUntil(loop,
                          [&]() {
                                  return upstream.framesTransferred() >= backpressureFrames + 5 &&
                                         downstream.framesTransferred() >= 5;
                          },
                          5000));
        CHECK(upstream.framesTransferred() >= backpressureFrames + 5);
        CHECK(downstream.framesTransferred() >= 5);

        upstream.stop();
        downstream.stop();
        (void)src->close().wait();
        (void)csc->close().wait();
        (void)sink->close().wait();
        delete src;
        delete csc;
        delete sink;
}

// ============================================================================
// Mid-flight close racing the connection's read
//
// Regression for the close-cascade watchdog escalation seen in the
// real-world pipeline (TPG → CSC → VideoEncoder → VideoDecoder →
// file).  When a downstream stage's @c close() is called from the
// cascade, the stage immediately latches @c isClosing = true while
// its strand is still draining work behind the queued Close cmd.  A
// connection on that source can race in between latch and drain:
//   - the read cache happens to be empty at the moment the
//     connection's pump calls @ref MediaIOSource::readFrame;
//   - @c MediaIOSource::readFrame sees @c io->isClosing() with an
//     empty cache and returns @c Error::NotOpen synchronously
//     (the cache's @c submitOneLocked refuses to start a fresh
//     prefetch once @c isClosing is latched, so the cache stays
//     empty until the strand processes the Close cmd and pushes
//     the synthetic EOS);
//   - prior to the fix, the connection treated @c NotOpen as a
//     generic error, fired @c errorOccurredSignal, and stopped
//     itself — so the synthetic EOS pushed later by the strand
//     had no consumer, @c upstreamDoneSignal never fired, and the
//     pipeline-level close cascade stalled until the watchdog
//     escalated to forced close.
//
// The fix is to surface @c NotOpen the same way as @c EndOfFile: a
// clean upstream-done signal that lets the cascade propagate.
//
// To trigger the race deterministically the test uses
// @ref PausedTestMediaIO so the strand cmd queue is driven manually.
// We close the upstream while a prefetch is queued but not yet
// dispatched, then process the prefetch (cache temporarily holds a
// completed read), let the connection consume it (cache becomes
// empty, no fresh prefetch since @c isClosing is latched), and pump
// the EventLoop without processing the Close.  The next pump call
// hits @c readFrame with @c isClosing + empty cache → @c NotOpen,
// which is precisely the path the fix targets.
// ============================================================================

namespace {

class CascadeCloseSourceMediaIO : public PausedTestMediaIO {
                PROMEKI_OBJECT(CascadeCloseSourceMediaIO, PausedTestMediaIO)
        public:
                CascadeCloseSourceMediaIO(ObjectBase *parent = nullptr) : PausedTestMediaIO(parent) {}

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override {
                        MediaIOPortGroup *group = addPortGroup("paused-src");
                        if (group == nullptr) return Error::Invalid;
                        if (addSource(group, MediaDesc()) == nullptr) return Error::Invalid;
                        return PausedTestMediaIO::executeCmd(cmd);
                }
                Error executeCmd(MediaIOCommandRead &cmd) override {
                        cmd.frame = Frame::Ptr::takeOwnership(new Frame());
                        cmd.currentFrame = FrameNumber(_frameNum++);
                        return Error::Ok;
                }

        private:
                int64_t _frameNum = 1;
};

class CascadeCloseSinkMediaIO : public PausedTestMediaIO {
                PROMEKI_OBJECT(CascadeCloseSinkMediaIO, PausedTestMediaIO)
        public:
                CascadeCloseSinkMediaIO(ObjectBase *parent = nullptr) : PausedTestMediaIO(parent) {}

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override {
                        MediaIOPortGroup *group = addPortGroup("paused-sink");
                        if (group == nullptr) return Error::Invalid;
                        if (addSink(group, MediaDesc()) == nullptr) return Error::Invalid;
                        return PausedTestMediaIO::executeCmd(cmd);
                }
                Error executeCmd(MediaIOCommandWrite &cmd) override {
                        (void)cmd;
                        return Error::Ok;
                }
};

} // namespace

TEST_CASE("MediaIOPortConnection treats NotOpen mid-flight as upstreamDone") {
        EventLoop loop;

        CascadeCloseSourceMediaIO source;
        MediaIORequest             srcOpen = source.open();
        source.processAll();
        REQUIRE(srcOpen.wait().isOk());

        CascadeCloseSinkMediaIO sink;
        MediaIORequest          sinkOpen = sink.open();
        sink.processAll();
        REQUIRE(sinkOpen.wait().isOk());

        MediaIOPortConnection conn(source.source(0), sink.sink(0));
        std::atomic<int>      doneCount{0};
        std::atomic<int>      errorCount{0};
        conn.upstreamDoneSignal.connect([&doneCount]() { doneCount.fetch_add(1); }, &conn);
        conn.errorOccurredSignal.connect([&errorCount](Error) { errorCount.fetch_add(1); }, &conn);

        REQUIRE(conn.start().isOk());

        // Drive a few frames end-to-end so the cache and connection
        // are in steady state.  Each iteration: pump submits one
        // Read cmd to the source's queue (via the cache); we
        // dispatch it manually; the connection consumes the result
        // and submits one Write cmd to the sink; we dispatch that
        // as well.
        const int warmupFrames = 3;
        for (int i = 0; i < warmupFrames; ++i) {
                REQUIRE(pumpUntil(loop, [&]() { return source.pending() > 0; }, 1000));
                source.processAll();
                REQUIRE(pumpUntil(loop, [&]() { return sink.pending() > 0; }, 1000));
                sink.processAll();
        }
        REQUIRE(pumpUntil(loop, [&]() { return conn.framesTransferred() >= warmupFrames; }, 1000));

        // The cache will have queued a fresh prefetch into the
        // source after the last warmup pump — wait for it so we
        // know the strand has at least one Read cmd in the queue
        // ahead of the Close we're about to submit.
        REQUIRE(pumpUntil(loop, [&]() { return source.pending() > 0; }, 1000));
        REQUIRE(source.pending() >= 1);

        // Close the source.  This latches isClosing on the calling
        // thread and submits a Close cmd to the strand queue
        // BEHIND the in-flight prefetch.  Because we drive the
        // strand manually, the cmds will not run until processOne
        // is called.
        MediaIORequest closeReq = source.close();
        CHECK(source.isClosing());
        CHECK(source.pending() >= 2);

        // Process the queued Read cmd only — leave the Close cmd
        // queued so the synthetic EOS does not hit the cache yet.
        // The Read completes, cache holds the result, and the
        // connection's pump consumes the frame and submits a
        // write.  Critically, the cache cannot prefetch a
        // replacement because submitOneLocked refuses while
        // isClosing is set, so after the consume the cache is
        // empty.
        REQUIRE(source.processOne());
        REQUIRE(pumpUntil(loop, [&]() { return sink.pending() > 0; }, 1000));
        sink.processAll();

        // Now the connection's pump should re-enter readFrame on
        // an empty cache with isClosing latched — that is the
        // @c Error::NotOpen path the fix routes to upstreamDone.
        REQUIRE(pumpUntil(loop, [&]() { return doneCount.load() > 0 || errorCount.load() > 0; }, 1000));
        CHECK(doneCount.load() == 1);
        CHECK(errorCount.load() == 0);
        CHECK(conn.upstreamDone());

        // Drain the Close cmd to clean up.
        source.processAll();
        REQUIRE(closeReq.wait().isOk());

        conn.stop();
        MediaIORequest sinkCloseReq = sink.close();
        sink.processAll();
        (void)sinkCloseReq.wait();
}

// ============================================================================
// Leaf-source slow-first-read regression
//
// Production V4L2 / RTP / FrameBridge sources block in executeCmd(Read)
// while the upstream device warms up.  An earlier version of those
// backends returned Error::TryAgain on a deadline timeout, which the
// pump (in its transform-stage TryAgain branch) interprets as "wait
// for the next upstream Write to fire frameReady" — but a leaf source
// has no upstream Write, so exactly one Read cmd ever ran and the
// pipeline stalled.  The fix in V4L2/RTP/FrameBridge is to never
// return TryAgain: block in executeCmd(Read) until either a frame is
// produced or cancelBlockingWork() unwinds the wait at close time.
//
// This test documents that contract by standing up a fake leaf source
// whose first Read sleeps for an interval significantly longer than
// any plausible pump timeout, and then asserts that the pump still
// drives multiple frames end-to-end (not just one).  A regression
// would surface as the second Read never being submitted.
// ============================================================================

namespace {

// Arbitrary fake mediaDesc that exists only so the connection has
// something to bind sink to source against.  No real format is needed
// because the test sink just counts frames.
MediaDesc makeFakeMediaDesc() {
        MediaDesc md;
        md.setFrameRate(FrameRate(FrameRate::RationalType(30, 1)));
        return md;
}

class SlowFirstReadMediaIO : public DedicatedThreadMediaIO {
                PROMEKI_OBJECT(SlowFirstReadMediaIO, DedicatedThreadMediaIO)
        public:
                SlowFirstReadMediaIO(int64_t firstReadDelayMs, ObjectBase *parent = nullptr)
                    : DedicatedThreadMediaIO(parent), _firstReadDelayMs(firstReadDelayMs) {}

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override {
                        (void)cmd;
                        MediaIOPortGroup *group = addPortGroup("slow-src");
                        if (group == nullptr) return Error::Invalid;
                        group->setFrameRate(FrameRate(FrameRate::RationalType(30, 1)));
                        group->setFrameCount(MediaIO::FrameCountInfinite);
                        if (addSource(group, makeFakeMediaDesc()) == nullptr) return Error::Invalid;
                        return Error::Ok;
                }
                Error executeCmd(MediaIOCommandClose &cmd) override {
                        (void)cmd;
                        return Error::Ok;
                }
                Error executeCmd(MediaIOCommandRead &cmd) override {
                        // Simulate the V4L2 startup pause: first Read
                        // blocks while the "device" warms up.  A bug
                        // that bails the pump on the first Read shows
                        // up here as no second Read ever arriving.
                        if (_firstRead) {
                                _firstRead = false;
                                Thread::sleepMs(_firstReadDelayMs);
                        }
                        cmd.frame = Frame::Ptr::takeOwnership(new Frame());
                        cmd.currentFrame = FrameNumber(_frameNum++);
                        return Error::Ok;
                }

        private:
                int64_t _firstReadDelayMs;
                bool    _firstRead = true;
                int64_t _frameNum = 1;
};

class CountingSinkMediaIO : public ::promeki::tests::InlineTestMediaIO {
                PROMEKI_OBJECT(CountingSinkMediaIO, InlineTestMediaIO)
        public:
                CountingSinkMediaIO(ObjectBase *parent = nullptr) : InlineTestMediaIO(parent) {}

                std::atomic<int> writes{0};

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override {
                        (void)cmd;
                        MediaIOPortGroup *group = addPortGroup("counting-sink");
                        if (group == nullptr) return Error::Invalid;
                        if (addSink(group, makeFakeMediaDesc()) == nullptr) return Error::Invalid;
                        return Error::Ok;
                }
                Error executeCmd(MediaIOCommandWrite &cmd) override {
                        (void)cmd;
                        writes.fetch_add(1, std::memory_order_relaxed);
                        return Error::Ok;
                }
};

} // namespace

TEST_CASE("MediaIOPortConnection drives multiple frames past a slow first read") {
        EventLoop loop;

        // 250 ms is comfortably longer than any historical "is the
        // device dead?" pump-side timeout (the V4L2 backend's own
        // pre-fix value was 200 ms) so the bug pattern would always
        // trip on the first Read if it were still present.
        SlowFirstReadMediaIO source(/*firstReadDelayMs=*/250);
        REQUIRE(source.open().wait().isOk());
        REQUIRE(source.sourceCount() > 0);

        CountingSinkMediaIO sink;
        REQUIRE(sink.open().wait().isOk());
        REQUIRE(sink.sinkCount() > 0);

        MediaIOPortConnection conn(source.source(0), sink.sink(0));
        REQUIRE(conn.start().isOk());

        // 5 s budget covers the 250 ms cold start plus enough wall
        // time for several follow-up frames at any reasonable cadence.
        const int kTargetFrames = 5;
        REQUIRE(pumpUntil(
                loop, [&]() { return sink.writes.load(std::memory_order_relaxed) >= kTargetFrames; }, 5000));
        CHECK(sink.writes.load(std::memory_order_relaxed) >= kTargetFrames);
        CHECK(conn.framesTransferred() >= kTargetFrames);

        conn.stop();
        (void)source.close().wait();
        (void)sink.close().wait();
}
