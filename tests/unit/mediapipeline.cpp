/**
 * @file      mediapipeline.cpp
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
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/pipelinestats.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

// Builds a pipeline config that doesn't touch the filesystem:
// TPG → Converter (pass-through RGBA output).  Exercises the DAG
// plumbing without needing a real output sink.
MediaPipelineConfig makeTpgToConverter() {
        MediaPipelineConfig cfg;

        MediaPipelineConfig::Stage src;
        src.name = "src";
        src.type = "TPG";
        src.mode = MediaIO::Output;
        src.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        src.config.set(MediaConfig::VideoEnabled, true);
        src.config.set(MediaConfig::AudioEnabled, false);
        cfg.addStage(src);

        MediaPipelineConfig::Stage csc;
        csc.name = "csc";
        csc.type = "Converter";
        csc.mode = MediaIO::InputAndOutput;
        cfg.addStage(csc);

        cfg.addRoute("src", "csc");
        return cfg;
}

} // namespace

TEST_CASE("MediaPipeline_DefaultStateIsEmpty") {
        EventLoop loop; // gives the pipeline an owner event loop.
        MediaPipeline p;
        CHECK(p.state() == MediaPipeline::State::Empty);
        CHECK(p.stageNames().isEmpty());
}

TEST_CASE("MediaPipeline_BuildRejectsInvalidConfig") {
        EventLoop loop;
        MediaPipeline p;
        MediaPipelineConfig cfg; // empty
        Error err = p.build(cfg);
        CHECK(err.isError());
        CHECK(p.state() == MediaPipeline::State::Empty);
}

TEST_CASE("MediaPipeline_BuildRejectsFanIn") {
        EventLoop loop;
        MediaPipelineConfig cfg = makeTpgToConverter();
        MediaPipelineConfig::Stage extraSrc;
        extraSrc.name = "src2";
        extraSrc.type = "TPG";
        extraSrc.mode = MediaIO::Output;
        cfg.addStage(extraSrc);
        cfg.addRoute("src2", "csc"); // csc now has two incoming routes

        MediaPipeline p;
        CHECK(p.build(cfg) == Error::NotSupported);
}

TEST_CASE("MediaPipeline_BuildSucceedsAndInstantiatesStages") {
        EventLoop loop;
        MediaPipeline p;
        MediaPipelineConfig cfg = makeTpgToConverter();
        REQUIRE(p.build(cfg).isOk());
        CHECK(p.state() == MediaPipeline::State::Built);

        StringList names = p.stageNames();
        REQUIRE(names.size() == 2);
        CHECK(names[0] == "src");
        CHECK(names[1] == "csc");

        CHECK(p.stage("src") != nullptr);
        CHECK(p.stage("csc") != nullptr);
        CHECK(p.stage("ghost") == nullptr);
}

TEST_CASE("MediaPipeline_OpenAndClose") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());
        CHECK(p.state() == MediaPipeline::State::Open);
        CHECK(p.stage("src")->isOpen());
        CHECK(p.stage("csc")->isOpen());

        CHECK(p.close().isOk());
        CHECK(p.state() == MediaPipeline::State::Closed);
}

TEST_CASE("MediaPipeline_StatsAfterOpen") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());

        MediaPipelineStats snapshot = p.stats();
        CHECK(snapshot.containsStage("src"));
        CHECK(snapshot.containsStage("csc"));
        // Aggregate should be populated (even if counters are zero).
        CHECK(snapshot.aggregate().contains(MediaIOStats::FramesDropped));

        (void)p.close();
}

TEST_CASE("MediaPipeline_DescribeIncludesLiveState") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());

        StringList lines = p.describe();
        CHECK(!lines.isEmpty());
        bool hasLive = false;
        for(size_t i = 0; i < lines.size(); ++i) {
                if(lines[i].contains("Live state")) hasLive = true;
        }
        CHECK(hasLive);

        (void)p.close();
}

TEST_CASE("MediaPipeline_BuildFromJsonRoundTrip") {
        EventLoop loop;
        MediaPipelineConfig orig = makeTpgToConverter();
        JsonObject j = orig.toJson();
        Error err;
        MediaPipelineConfig round = MediaPipelineConfig::fromJson(j, &err);
        REQUIRE(err.isOk());
        CHECK(round == orig);

        MediaPipeline p;
        REQUIRE(p.build(round).isOk());
        CHECK(p.stageNames().size() == 2);
        CHECK(p.stage("src") != nullptr);
        CHECK(p.stage("csc") != nullptr);
}

TEST_CASE("MediaPipeline_StatsContainPipelineBucket") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());

        MediaPipelineStats snap = p.stats();
        const PipelineStats &pp = snap.pipeline();
        // Counters are registered even before start(), so the state
        // is reported and the counters sit at their zero defaults.
        CHECK(pp.contains(PipelineStats::State));
        CHECK(pp.get(PipelineStats::State).get<String>() == "Open");
        CHECK(pp.get(PipelineStats::FramesProduced).get<int64_t>() == 0);
        CHECK(pp.get(PipelineStats::WriteRetries).get<int64_t>()   == 0);
        CHECK(pp.get(PipelineStats::PipelineErrors).get<int64_t>() == 0);
        CHECK(pp.get(PipelineStats::SourcesAtEof).get<int64_t>()   == 0);
        CHECK(pp.get(PipelineStats::PausedEdges).get<int64_t>()    == 0);

        (void)p.close();
}

TEST_CASE("MediaPipeline_FramesProducedCounterAdvances") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        // Pump the loop, spinning until the drain has produced at
        // least one frame or we hit a generous timeout.  TPG is a
        // real strand-driven backend so we need to give it real time
        // to emit @c frameReady; processEvents() alone doesn't yield
        // to that thread.
        const int64_t deadlineMs = 1000;
        ElapsedTimer t;
        t.start();
        int64_t produced = 0;
        while(t.elapsed() < deadlineMs) {
                loop.processEvents();
                produced = p.stats().pipeline()
                        .get(PipelineStats::FramesProduced).get<int64_t>();
                if(produced > 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        CHECK(produced > 0);
        CHECK(p.stats().pipeline().get(PipelineStats::State).get<String>()
                == "Running");

        CHECK(p.stop().isOk());
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_StartDrainsFramesThroughChain") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.state() == MediaPipeline::State::Running);

        // Pump the EventLoop briefly so the signal-driven drain has a
        // chance to run; TPG is infinite so we just want some frames
        // through the chain before we stop.
        for(int i = 0; i < 50; ++i) {
                loop.processEvents();
        }

        CHECK(p.stop().isOk());
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_CloseFromBuiltStateIsSafe") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        // No open / start — close should still tear things down cleanly.
        CHECK(p.close().isOk());
        CHECK(p.state() == MediaPipeline::State::Closed);
}

TEST_CASE("MediaPipeline_InjectStageSkipsFactory") {
        EventLoop loop;

        // Build a stand-alone Converter that we'll inject under a
        // fabricated "External" type name — without injection the
        // factory would fail because "External" is not a registered
        // backend.
        MediaIO::Config cfg = MediaIO::defaultConfig("Converter");
        cfg.set(MediaConfig::Type, String("Converter"));
        MediaIO *external = MediaIO::create(cfg);
        REQUIRE(external != nullptr);

        MediaPipelineConfig mpc;
        MediaPipelineConfig::Stage src;
        src.name = "src";
        src.type = "TPG";
        src.mode = MediaIO::Output;
        src.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        src.config.set(MediaConfig::VideoEnabled, true);
        src.config.set(MediaConfig::AudioEnabled, false);
        mpc.addStage(src);

        MediaPipelineConfig::Stage ext;
        ext.name = "xtr";
        ext.type = "External"; // not registered — must be injected
        ext.mode = MediaIO::InputAndOutput;
        mpc.addStage(ext);

        mpc.addRoute("src", "xtr");

        MediaPipeline p;
        CHECK(p.injectStage("xtr", external).isOk());
        REQUIRE(p.build(mpc).isOk());
        CHECK(p.stage("xtr") == external);

        // Close must not delete the caller-owned injected MediaIO;
        // deleting it here after close confirms it's still alive.
        CHECK(p.close().isOk());
        delete external;
}

// ============================================================================
// Close cascade (sync + async)
// ============================================================================

namespace {

// Pumps the EventLoop until @p pred returns true, or @p timeoutMs elapses.
// Returns true if the predicate fired, false on timeout.
template <typename Pred>
bool pumpUntil(EventLoop &loop, Pred pred, int64_t timeoutMs = 2000) {
        ElapsedTimer t;
        t.start();
        while(t.elapsed() < timeoutMs) {
                loop.processEvents();
                if(pred()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
}

} // namespace

TEST_CASE("MediaPipeline_CloseAsyncEmitsClosedSignal") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> closedCount{0};
        std::atomic<int> finishedCount{0};
        p.closedSignal.connect([&closedCount](Error) {
                closedCount.fetch_add(1);
        }, &p);
        p.finishedSignal.connect([&finishedCount](bool) {
                finishedCount.fetch_add(1);
        }, &p);

        CHECK(p.close(false).isOk());
        CHECK(p.isClosing());

        REQUIRE(pumpUntil(loop, [&]() { return closedCount.load() > 0; }));
        CHECK(closedCount.load() == 1);
        CHECK(finishedCount.load() == 1);
        CHECK(p.state() == MediaPipeline::State::Closed);
        CHECK_FALSE(p.isClosing());
}

TEST_CASE("MediaPipeline_CloseSyncPumpsEventsFromOwnEventLoop") {
        // When called from the pipeline's own EventLoop, close(true)
        // must pump events while waiting for the future so the
        // cross-thread closedSignal dispatches actually run.
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> closedCount{0};
        p.closedSignal.connect([&closedCount](Error) {
                closedCount.fetch_add(1);
        }, &p);

        CHECK(p.close(true).isOk());
        // close(true) can't return without finalizeClose having run;
        // the signal must have been delivered synchronously during
        // the pump loop.
        CHECK(closedCount.load() == 1);
        CHECK(p.state() == MediaPipeline::State::Closed);
}

TEST_CASE("MediaPipeline_CloseFinishedFiresAtEndOfCascade") {
        // Before the refactor, finishedSignal fired when sources hit
        // EOF but sinks might still be writing.  With the cascade,
        // finishedSignal only fires alongside closedSignal after
        // every stage has emitted its own closedSignal.
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> stageClosedCount{0};
        std::atomic<int> finishedCount{0};
        std::atomic<int> closedCount{0};
        p.stageClosedSignal.connect([&stageClosedCount](const String &) {
                stageClosedCount.fetch_add(1);
        }, &p);
        p.finishedSignal.connect([&finishedCount, &stageClosedCount](bool) {
                // Every stage must be closed by the time finishedSignal fires.
                CHECK(stageClosedCount.load() >= 2);
                finishedCount.fetch_add(1);
        }, &p);
        p.closedSignal.connect([&closedCount, &stageClosedCount](Error) {
                CHECK(stageClosedCount.load() >= 2);
                closedCount.fetch_add(1);
        }, &p);

        CHECK(p.close(false).isOk());
        REQUIRE(pumpUntil(loop, [&]() { return closedCount.load() > 0; }));
        CHECK(stageClosedCount.load() == 2); // src + csc
        CHECK(finishedCount.load() == 1);
        CHECK(closedCount.load() == 1);
}

TEST_CASE("MediaPipeline_DoubleCloseRejected") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> closedCount{0};
        p.closedSignal.connect([&closedCount](Error) {
                closedCount.fetch_add(1);
        }, &p);

        CHECK(p.close(false).isOk());
        CHECK(p.isClosing());
        // While the first close is in flight, a second async close
        // should be a no-op (caller sees Ok, the existing cascade
        // just carries through).  We can't check the return value
        // directly without leaking an implementation detail; just
        // verify it doesn't crash and exactly one closed signal fires.
        CHECK(p.close(false).isOk());

        REQUIRE(pumpUntil(loop, [&]() { return closedCount.load() > 0; }));
        CHECK(closedCount.load() == 1);
}

TEST_CASE("MediaPipeline_CloseOnBuiltStateCascadesWithoutDrain") {
        // Built but not opened: every stage is not-open, so initiateClose
        // must finalize synchronously via the alreadyClosed path.
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());

        std::atomic<int> closedCount{0};
        p.closedSignal.connect([&closedCount](Error) {
                closedCount.fetch_add(1);
        }, &p);

        CHECK(p.close(false).isOk());
        // With no event loop involvement needed (signals fire
        // synchronously on the strand-less path), finalize should
        // have run before close(false) returned.
        CHECK(closedCount.load() == 1);
        CHECK(p.state() == MediaPipeline::State::Closed);
}

TEST_CASE("MediaPipeline_CloseFromRunningEmitsNoSpuriousError") {
        // After the cascade triggers close on the source, its strand
        // emits one last frameReady (the one paired with the synthetic
        // EOS push).  Without the drainSource guard on upstreamDone, a
        // second drainSource invocation for that kick would observe a
        // stage whose _mode had already reset to NotOpen and surface
        // the NotOpen readFrame return as a pipelineError.  Verifies
        // that the full async close cycle emits zero pipeline errors.
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> errorCount{0};
        std::atomic<int> closedCount{0};
        std::atomic<bool> cleanFinish{false};
        p.pipelineErrorSignal.connect([&errorCount](const String &, Error) {
                errorCount.fetch_add(1);
        }, &p);
        p.finishedSignal.connect([&cleanFinish](bool clean) {
                cleanFinish.store(clean);
        }, &p);
        p.closedSignal.connect([&closedCount](Error) {
                closedCount.fetch_add(1);
        }, &p);

        CHECK(p.close(false).isOk());
        REQUIRE(pumpUntil(loop, [&]() { return closedCount.load() > 0; }));
        CHECK(errorCount.load() == 0);
        CHECK(cleanFinish.load() == true);
}

TEST_CASE("MediaPipeline_CloseOnClosedStateIsNoOp") {
        EventLoop loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToConverter()).isOk());
        CHECK(p.close().isOk());
        CHECK(p.state() == MediaPipeline::State::Closed);

        // Second close on an already-Closed pipeline: does nothing,
        // emits no signals, returns Ok.
        std::atomic<int> closedCount{0};
        p.closedSignal.connect([&closedCount](Error) {
                closedCount.fetch_add(1);
        }, &p);
        CHECK(p.close().isOk());
        CHECK(closedCount.load() == 0);
}
