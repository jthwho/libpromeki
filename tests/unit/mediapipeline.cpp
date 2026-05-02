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
#include <promeki/duration.h>
#include <promeki/elapsedtimer.h>
#include <promeki/eventloop.h>
#include <promeki/framecount.h>
#include <promeki/logger.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/objectbase.tpp>
#include <promeki/pipelineevent.h>
#include <promeki/thread.h>
#include <promeki/videoformat.h>

using namespace promeki;

namespace {

        // Builds a pipeline config that doesn't touch the filesystem:
        // TPG → CSC (pixel-format conversion stage configured as a
        // terminating transform).  Exercises the DAG plumbing without
        // needing a real output sink.
        MediaPipelineConfig makeTpgToCsc() {
                MediaPipelineConfig cfg;

                MediaPipelineConfig::Stage src;
                src.name = "src";
                src.type = "TPG";
                src.role = MediaPipelineConfig::StageRole::Source;
                src.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
                src.config.set(MediaConfig::VideoEnabled, true);
                src.config.set(MediaConfig::AudioEnabled, false);
                cfg.addStage(src);

                MediaPipelineConfig::Stage csc;
                csc.name = "csc";
                csc.type = "CSC";
                csc.role = MediaPipelineConfig::StageRole::Transform;
                cfg.addStage(csc);

                cfg.addRoute("src", "csc");
                return cfg;
        }

} // namespace

TEST_CASE("MediaPipeline_DefaultStateIsEmpty") {
        EventLoop     loop; // gives the pipeline an owner event loop.
        MediaPipeline p;
        CHECK(p.state() == MediaPipeline::State::Empty);
        CHECK(p.stageNames().isEmpty());
}

TEST_CASE("MediaPipeline_BuildRejectsInvalidConfig") {
        EventLoop           loop;
        MediaPipeline       p;
        MediaPipelineConfig cfg; // empty
        Error               err = p.build(cfg);
        CHECK(err.isError());
        CHECK(p.state() == MediaPipeline::State::Empty);
}

TEST_CASE("MediaPipeline_BuildRejectsFanIn") {
        EventLoop                  loop;
        MediaPipelineConfig        cfg = makeTpgToCsc();
        MediaPipelineConfig::Stage extraSrc;
        extraSrc.name = "src2";
        extraSrc.type = "TPG";
        extraSrc.role = MediaPipelineConfig::StageRole::Source;
        cfg.addStage(extraSrc);
        cfg.addRoute("src2", "csc"); // csc now has two incoming routes

        MediaPipeline p;
        CHECK(p.build(cfg) == Error::NotSupported);
}

TEST_CASE("MediaPipeline_BuildSucceedsAndInstantiatesStages") {
        EventLoop           loop;
        MediaPipeline       p;
        MediaPipelineConfig cfg = makeTpgToCsc();
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

TEST_CASE("MediaPipeline_StageNameSeedsMediaIOName") {
        // The pipeline must always seed MediaConfig::Name to the stage
        // name so logs, stats, and downstream tooling key off the same
        // identifier the pipeline graph uses, even when the stage
        // config supplies a different (or no) Name.
        EventLoop           loop;
        MediaPipeline       p;
        MediaPipelineConfig cfg = makeTpgToCsc();
        // Deliberately stamp a different Name on the stage config —
        // the pipeline must override it with the stage name.
        cfg.stages()[0].config.set(MediaConfig::Name, String("not-the-stage-name"));
        REQUIRE(p.build(cfg).isOk());

        MediaIO *src = p.stage("src");
        REQUIRE(src != nullptr);
        CHECK(src->name() == "src");
        CHECK(src->config().getAs<String>(MediaConfig::Name) == "src");

        MediaIO *csc = p.stage("csc");
        REQUIRE(csc != nullptr);
        CHECK(csc->name() == "csc");
}

TEST_CASE("MediaPipeline_OpenAndClose") {
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        CHECK(p.state() == MediaPipeline::State::Open);
        CHECK(p.stage("src")->isOpen());
        CHECK(p.stage("csc")->isOpen());

        CHECK(p.close().isOk());
        CHECK(p.state() == MediaPipeline::State::Closed);
}

TEST_CASE("MediaPipeline_StatsAfterOpen") {
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());

        MediaPipelineStats snapshot = p.stats();
        CHECK(snapshot.containsStage("src"));
        CHECK(snapshot.containsStage("csc"));
        // The cumulative aggregate should be populated (even if
        // counters are zero) — populateStandardStats overlays it on
        // every Stats command.
        const MediaPipelineStageStats *src = snapshot.findStage("src");
        REQUIRE(src != nullptr);
        CHECK(src->cumulative.contains(MediaIOStats::FramesDropped));

        (void)p.close();
}

TEST_CASE("MediaPipeline_DescribeIncludesLiveState") {
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());

        StringList lines = p.describe();
        CHECK(!lines.isEmpty());
        bool hasLive = false;
        for (size_t i = 0; i < lines.size(); ++i) {
                if (lines[i].contains("Live state")) hasLive = true;
        }
        CHECK(hasLive);

        (void)p.close();
}

TEST_CASE("MediaPipeline_BuildFromJsonRoundTrip") {
        EventLoop           loop;
        MediaPipelineConfig orig = makeTpgToCsc();
        JsonObject          j = orig.toJson();
        Error               err;
        MediaPipelineConfig round = MediaPipelineConfig::fromJson(j, &err);
        REQUIRE(err.isOk());
        CHECK(round == orig);

        MediaPipeline p;
        REQUIRE(p.build(round).isOk());
        CHECK(p.stageNames().size() == 2);
        CHECK(p.stage("src") != nullptr);
        CHECK(p.stage("csc") != nullptr);
}

TEST_CASE("MediaPipeline_StartDrainsFramesThroughChain") {
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.state() == MediaPipeline::State::Running);

        // Pump the EventLoop briefly so the signal-driven drain has a
        // chance to run; TPG is infinite so we just want some frames
        // through the chain before we stop.
        for (int i = 0; i < 50; ++i) {
                loop.processEvents();
        }

        CHECK(p.stop().isOk());
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_FrameCountCapClosesPipelineCleanly") {
        // TPG → CSC with a small frame-count cap on an intra-only path
        // (TPG produces uncompressed frames, so every frame is a safe
        // cut point).  The pipeline should reach its cap, close
        // itself, and transition to State::Closed without any external
        // stop/close call.
        MediaPipelineConfig cfg = makeTpgToCsc();
        cfg.setFrameCount(FrameCount(3));

        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(cfg).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        ElapsedTimer watchdog;
        watchdog.start();
        while (p.state() != MediaPipeline::State::Closed && watchdog.elapsed() < 3000) {
                loop.processEvents();
        }
        CHECK(p.state() == MediaPipeline::State::Closed);

        // No explicit close was needed — the pipeline self-closed on
        // reaching the cap.  A second close is a no-op and should
        // still return cleanly.
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_DefaultFrameCountRunsUncapped") {
        // A config with no frameCount set must leave the pipeline
        // unbounded — drainSource's cap check collapses to zero at
        // build time and the pipeline keeps running until we stop it.
        // This guards the fast path (no cap configured) against a
        // regression where unknown / infinite / empty FrameCount
        // states accidentally trip the cutoff.
        MediaPipelineConfig cfg = makeTpgToCsc();
        CHECK(cfg.frameCount().isUnknown()); // default-constructed

        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(cfg).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        // Pump briefly and then force-stop; the pipeline must still be
        // running (Running state) because the cap never fires.
        for (int i = 0; i < 50; ++i) loop.processEvents();
        CHECK(p.state() == MediaPipeline::State::Running);

        CHECK(p.stop().isOk());
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_FrameCountInfinityRunsUncapped") {
        // FrameCount::infinity() is semantically the same "no cap"
        // as the default; the build-time normalization has to fold
        // it to zero just like the unknown case.
        MediaPipelineConfig cfg = makeTpgToCsc();
        cfg.setFrameCount(FrameCount::infinity());

        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(cfg).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        for (int i = 0; i < 50; ++i) loop.processEvents();
        CHECK(p.state() == MediaPipeline::State::Running);

        CHECK(p.stop().isOk());
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_FrameCountEmptyRunsUncapped") {
        // FrameCount::empty() also collapses to "no cap" — isFinite()
        // is true but isEmpty() excludes it from the limit branch.
        MediaPipelineConfig cfg = makeTpgToCsc();
        cfg.setFrameCount(FrameCount::empty());

        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(cfg).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        for (int i = 0; i < 50; ++i) loop.processEvents();
        CHECK(p.state() == MediaPipeline::State::Running);

        CHECK(p.stop().isOk());
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_CloseFromBuiltStateIsSafe") {
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        // No open / start — close should still tear things down cleanly.
        CHECK(p.close().isOk());
        CHECK(p.state() == MediaPipeline::State::Closed);
}

TEST_CASE("MediaPipeline_InjectStageSkipsFactory") {
        EventLoop loop;

        // Build a stand-alone CSC that we'll inject under a
        // fabricated "External" type name — without injection the
        // factory would fail because "External" is not a registered
        // backend.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("CSC");
        cfg.set(MediaConfig::Type, String("CSC"));
        MediaIO *external = MediaIO::create(cfg);
        REQUIRE(external != nullptr);

        MediaPipelineConfig        mpc;
        MediaPipelineConfig::Stage src;
        src.name = "src";
        src.type = "TPG";
        src.role = MediaPipelineConfig::StageRole::Source;
        src.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p29_97));
        src.config.set(MediaConfig::VideoEnabled, true);
        src.config.set(MediaConfig::AudioEnabled, false);
        mpc.addStage(src);

        MediaPipelineConfig::Stage ext;
        ext.name = "xtr";
        ext.type = "External"; // not registered — must be injected
        ext.role = MediaPipelineConfig::StageRole::Transform;
        mpc.addStage(ext);

        mpc.addRoute("src", "xtr");

        MediaPipeline p;
        external->setName(String("xtr"));
        CHECK(p.injectStage(external).isOk());
        REQUIRE(p.build(mpc).isOk());
        CHECK(p.stage("xtr") == external);

        // Close must not delete the caller-owned injected MediaIO;
        // deleting it here after close confirms it's still alive.
        CHECK(p.close().isOk());
        delete external;
}

TEST_CASE("MediaPipeline_InjectStageAdoptsIONameWhenSet") {
        EventLoop       loop;
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("CSC");
        cfg.set(MediaConfig::Type, String("CSC"));
        cfg.set(MediaConfig::Name, String("my-csc"));
        MediaIO *io = MediaIO::create(cfg);
        REQUIRE(io != nullptr);

        MediaPipeline p;
        CHECK(p.injectStage(io).isOk());
        // Name unchanged because it was unique.
        CHECK(io->name() == "my-csc");
        delete io;
}

TEST_CASE("MediaPipeline_InjectStageRenamesOnCollision") {
        EventLoop loop;

        MediaIO::Config c1 = MediaIOFactory::defaultConfig("CSC");
        c1.set(MediaConfig::Type, String("CSC"));
        c1.set(MediaConfig::Name, String("bridge"));
        MediaIO *io1 = MediaIO::create(c1);
        REQUIRE(io1 != nullptr);

        MediaIO::Config c2 = MediaIOFactory::defaultConfig("CSC");
        c2.set(MediaConfig::Type, String("CSC"));
        c2.set(MediaConfig::Name, String("bridge"));
        MediaIO *io2 = MediaIO::create(c2);
        REQUIRE(io2 != nullptr);

        MediaPipeline p;
        CHECK(p.injectStage(io1).isOk());
        CHECK(io1->name() == "bridge");

        // Same name → collision; must be renamed and the IO updated.
        CHECK(p.injectStage(io2).isOk());
        CHECK(io2->name() == "bridge2");

        delete io1;
        delete io2;
}

TEST_CASE("MediaPipeline_InjectStageAutoNamesByRole") {
        EventLoop loop;

        // Inspector advertises canBeSink=true via its factory, so the
        // unnamed inject path must produce "sink<N>" auto-names.
        MediaIO::Config cfg = MediaIOFactory::defaultConfig("Inspector");
        cfg.set(MediaConfig::Type, String("Inspector"));
        // Leave MediaConfig::Name unset so the auto-name path runs.
        MediaIO *a = MediaIO::create(cfg);
        REQUIRE(a != nullptr);
        REQUIRE(a->name().isEmpty());

        MediaIO *b = MediaIO::create(cfg);
        REQUIRE(b != nullptr);

        MediaPipeline p;
        CHECK(p.injectStage(a).isOk());
        CHECK(a->name() == "sink1");
        CHECK(p.injectStage(b).isOk());
        CHECK(b->name() == "sink2");

        delete a;
        delete b;
}

// ============================================================================
// Close cascade (sync + async)
// ============================================================================

namespace {

        // Pumps the EventLoop until @p pred returns true, or @p timeoutMs elapses.
        // Returns true if the predicate fired, false on timeout.
        //
        // The 10s default accommodates cold-start latency: when these
        // tests run early in a fresh process the SharedThread pool's
        // first worker spawn + the close cascade across two strand-
        // backed stages can take several seconds.  Subsequent runs see
        // the pool warm and complete in tens of milliseconds.
        template <typename Pred> bool pumpUntil(EventLoop &loop, Pred pred, int64_t timeoutMs = 10000) {
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

TEST_CASE("MediaPipeline_CloseAsyncEmitsClosedSignal") {
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> closedCount{0};
        std::atomic<int> finishedCount{0};
        p.closedSignal.connect([&closedCount](Error) { closedCount.fetch_add(1); }, &p);
        p.finishedSignal.connect([&finishedCount](bool) { finishedCount.fetch_add(1); }, &p);

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
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> closedCount{0};
        p.closedSignal.connect([&closedCount](Error) { closedCount.fetch_add(1); }, &p);

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
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> stageClosedCount{0};
        std::atomic<int> finishedCount{0};
        std::atomic<int> closedCount{0};
        p.stageClosedSignal.connect([&stageClosedCount](const String &) { stageClosedCount.fetch_add(1); }, &p);
        p.finishedSignal.connect(
                [&finishedCount, &stageClosedCount](bool) {
                        // Every stage must be closed by the time finishedSignal fires.
                        CHECK(stageClosedCount.load() >= 2);
                        finishedCount.fetch_add(1);
                },
                &p);
        p.closedSignal.connect(
                [&closedCount, &stageClosedCount](Error) {
                        CHECK(stageClosedCount.load() >= 2);
                        closedCount.fetch_add(1);
                },
                &p);

        CHECK(p.close(false).isOk());
        REQUIRE(pumpUntil(loop, [&]() { return closedCount.load() > 0; }));
        CHECK(stageClosedCount.load() == 2); // src + csc
        CHECK(finishedCount.load() == 1);
        CHECK(closedCount.load() == 1);
}

TEST_CASE("MediaPipeline_DoubleCloseRejected") {
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> closedCount{0};
        p.closedSignal.connect([&closedCount](Error) { closedCount.fetch_add(1); }, &p);

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
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());

        std::atomic<int> closedCount{0};
        p.closedSignal.connect([&closedCount](Error) { closedCount.fetch_add(1); }, &p);

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
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int>  errorCount{0};
        std::atomic<int>  closedCount{0};
        std::atomic<bool> cleanFinish{false};
        p.pipelineErrorSignal.connect([&errorCount](const String &, Error) { errorCount.fetch_add(1); }, &p);
        p.finishedSignal.connect([&cleanFinish](bool clean) { cleanFinish.store(clean); }, &p);
        p.closedSignal.connect([&closedCount](Error) { closedCount.fetch_add(1); }, &p);

        CHECK(p.close(false).isOk());
        REQUIRE(pumpUntil(loop, [&]() { return closedCount.load() > 0; }));
        CHECK(errorCount.load() == 0);
        CHECK(cleanFinish.load() == true);
}

// ============================================================================
// PipelineEvent subscription
// ============================================================================

namespace {

        struct EventCounts {
                        std::atomic<int> stateChanged{0};
                        std::atomic<int> stageState{0};
                        std::atomic<int> stageError{0};
                        std::atomic<int> statsUpdated{0};
                        std::atomic<int> planResolved{0};
                        std::atomic<int> log{0};
                        std::atomic<int> total{0};
        };

        void countEvent(EventCounts &counts, const PipelineEvent &ev) {
                counts.total.fetch_add(1);
                switch (ev.kind()) {
                        case PipelineEvent::Kind::StateChanged: counts.stateChanged.fetch_add(1); break;
                        case PipelineEvent::Kind::StageState: counts.stageState.fetch_add(1); break;
                        case PipelineEvent::Kind::StageError: counts.stageError.fetch_add(1); break;
                        case PipelineEvent::Kind::StatsUpdated: counts.statsUpdated.fetch_add(1); break;
                        case PipelineEvent::Kind::PlanResolved: counts.planResolved.fetch_add(1); break;
                        case PipelineEvent::Kind::Log: counts.log.fetch_add(1); break;
                }
        }

} // namespace

TEST_CASE("MediaPipeline_SubscribeReceivesStateAndStageEvents") {
        EventLoop     loop;
        MediaPipeline p;

        EventCounts counts;
        const int   subId = p.subscribe([&counts](const PipelineEvent &ev) { countEvent(counts, ev); });
        REQUIRE(subId >= 0);

        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        REQUIRE(pumpUntil(loop, [&]() { return counts.stageState.load() >= 4; }, 1500));
        CHECK(counts.stateChanged.load() >= 3); // Built, Open, Running
        CHECK(counts.stageState.load() >= 4);   // 2 opened + 2 started

        CHECK(p.stop().isOk());
        REQUIRE(pumpUntil(loop, [&]() { return counts.stageState.load() >= 6; }, 1500));

        CHECK(p.close().isOk());
        REQUIRE(pumpUntil(loop, [&]() { return counts.stageState.load() >= 8; }, 1500));
        CHECK(counts.stateChanged.load() >= 5);

        p.unsubscribe(subId);
}

TEST_CASE("MediaPipeline_SubscribeBeforeBuildReceivesPlanResolved") {
        EventLoop     loop;
        MediaPipeline p;
        EventCounts   counts;
        const int     subId = p.subscribe([&counts](const PipelineEvent &ev) { countEvent(counts, ev); });
        REQUIRE(subId >= 0);

        REQUIRE(p.build(makeTpgToCsc(), /*autoplan=*/true).isOk());
        REQUIRE(pumpUntil(
                loop, [&]() { return counts.planResolved.load() >= 1 && counts.stateChanged.load() >= 1; }, 1500));
        CHECK(counts.planResolved.load() == 1);

        CHECK(p.close().isOk());
        REQUIRE(pumpUntil(loop, [&]() { return counts.stateChanged.load() >= 2; }, 1500));
        p.unsubscribe(subId);
}

TEST_CASE("MediaPipeline_SetStatsIntervalEmitsTickEvents") {
        EventLoop     loop;
        MediaPipeline p;
        EventCounts   counts;
        const int     subId = p.subscribe([&counts](const PipelineEvent &ev) { countEvent(counts, ev); });
        REQUIRE(subId >= 0);

        p.setStatsInterval(Duration::fromMilliseconds(50));
        CHECK(p.statsInterval().milliseconds() == 50);

        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        const bool got = pumpUntil(loop, [&]() { return counts.statsUpdated.load() >= 2; }, 800);
        CHECK(got);
        const int duringRun = counts.statsUpdated.load();

        CHECK(p.stop().isOk());
        loop.processEvents();
        Thread::sleepMs(150);
        loop.processEvents();
        const int afterStop = counts.statsUpdated.load();
        CHECK(afterStop == duringRun);

        CHECK(p.close().isOk());
        p.unsubscribe(subId);
}

TEST_CASE("MediaPipeline_UnsubscribeStopsEventsToThatSubscriber") {
        EventLoop     loop;
        MediaPipeline p;

        EventCounts countsA;
        EventCounts countsB;
        const int   idA = p.subscribe([&countsA](const PipelineEvent &ev) { countEvent(countsA, ev); });
        const int   idB = p.subscribe([&countsB](const PipelineEvent &ev) { countEvent(countsB, ev); });
        REQUIRE(idA >= 0);
        REQUIRE(idB >= 0);

        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(pumpUntil(
                loop, [&]() { return countsA.stageState.load() >= 2 && countsB.stageState.load() >= 2; }, 1500));

        const int aBaseline = countsA.total.load();
        p.unsubscribe(idA);

        REQUIRE(p.start().isOk());
        REQUIRE(pumpUntil(loop, [&]() { return countsB.stageState.load() >= 4; }, 1500));
        CHECK(countsB.total.load() > aBaseline);
        CHECK(countsA.total.load() == aBaseline);

        CHECK(p.close().isOk());
        loop.processEvents();
        p.unsubscribe(idB);
}

TEST_CASE("MediaPipeline_SubscriberOnAnotherThreadReceivesOnThatThread") {
        EventLoop     loop;
        MediaPipeline p;

        Thread worker;
        worker.start();
        // Wait for the worker thread's EventLoop to come up.
        ElapsedTimer t;
        t.start();
        while (worker.threadEventLoop() == nullptr && t.elapsed() < 1000) {
                Thread::sleepMs(1);
        }
        REQUIRE(worker.threadEventLoop() != nullptr);

        std::atomic<int>             received{0};
        std::atomic<std::thread::id> deliveryThread;
        deliveryThread.store(std::this_thread::get_id());

        // Install the subscription from the worker thread so its EventLoop
        // is the captured target.
        std::atomic<int> subId{-1};
        worker.threadEventLoop()->postCallable([&]() {
                int id = p.subscribe([&](const PipelineEvent &) {
                        deliveryThread.store(std::this_thread::get_id());
                        received.fetch_add(1);
                });
                subId.store(id);
        });
        // Spin until the post has executed.
        while (subId.load() < 0) {
                Thread::sleepMs(1);
        }

        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        // Wait for the worker thread to dispatch some events.
        t.start();
        while (received.load() < 2 && t.elapsed() < 1500) {
                loop.processEvents();
                Thread::sleepMs(5);
        }
        CHECK(received.load() >= 2);
        CHECK(deliveryThread.load() == worker.id());
        CHECK(deliveryThread.load() != std::this_thread::get_id());

        CHECK(p.close().isOk());
        worker.threadEventLoop()->postCallable([&]() { p.unsubscribe(subId.load()); });
        worker.quit();
        worker.wait();
}

TEST_CASE("MediaPipeline_LogEntryMirroredAsLogEvent") {
        EventLoop         loop;
        MediaPipeline     p;
        EventCounts       counts;
        std::atomic<bool> seenMessage{false};
        const int         subId = p.subscribe([&counts, &seenMessage](const PipelineEvent &ev) {
                countEvent(counts, ev);
                if (ev.kind() == PipelineEvent::Kind::Log) {
                        if (ev.payload().get<String>().contains("pipeline-event-test-marker")) {
                                seenMessage.store(true);
                        }
                }
        });
        REQUIRE(subId >= 0);

        Logger::defaultLogger().log(Logger::Info, "test", 0, String("pipeline-event-test-marker"));
        Logger::defaultLogger().sync();
        REQUIRE(pumpUntil(loop, [&]() { return seenMessage.load(); }, 1500));
        CHECK(counts.log.load() >= 1);

        p.unsubscribe(subId);
}

TEST_CASE("MediaPipeline_CloseOnClosedStateIsNoOp") {
        EventLoop     loop;
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        CHECK(p.close().isOk());
        CHECK(p.state() == MediaPipeline::State::Closed);

        // Second close on an already-Closed pipeline: does nothing,
        // emits no signals, returns Ok.
        std::atomic<int> closedCount{0};
        p.closedSignal.connect([&closedCount](Error) { closedCount.fetch_add(1); }, &p);
        CHECK(p.close().isOk());
        CHECK(closedCount.load() == 0);
}
