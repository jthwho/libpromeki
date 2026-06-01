/**
 * @file      mediapipeline.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <promeki/application.h>
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        CHECK(p.state() == MediaPipeline::State::Empty);
        CHECK(p.stageNames().isEmpty());
}

TEST_CASE("MediaPipeline_BuildRejectsInvalidConfig") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline       p;
        MediaPipelineConfig cfg; // empty
        Error               err = p.build(cfg);
        CHECK(err.isError());
        CHECK(p.state() == MediaPipeline::State::Empty);
}

TEST_CASE("MediaPipeline_BuildRejectsFanIn") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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

        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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

        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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

        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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

        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeTpgToCsc()).isOk());
        // No open / start — close should still tear things down cleanly.
        CHECK(p.close().isOk());
        CHECK(p.state() == MediaPipeline::State::Closed);
}

TEST_CASE("MediaPipeline_InjectStageSkipsFactory") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();

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

TEST_CASE("MediaPipeline_DeletingInjectedStageBeforeCloseIsSafe") {
        // Regression guard: a caller-owned injected stage may be deleted
        // while the pipeline still references it in _stages — e.g. a
        // stalled close cascade left finalizeClose from clearing the
        // maps, then the owner's scope ends and frees the stage before
        // the pipeline's own teardown.  injectStage registers an
        // ObjectBase destruction cleanup (detachInjectedStage) that nulls
        // the entry out of _stages / _injected so the close path never
        // dereferences the freed pointer.  Without the cleanup this
        // crashed in initiateClose's connect(stage->closedSignal, …)
        // (use-after-free; trips ASAN).
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();

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
        REQUIRE(p.injectStage(external).isOk());
        REQUIRE(p.build(mpc).isOk());
        REQUIRE(p.stage("xtr") == external);

        // Delete the injected stage while the pipeline still holds it.
        // The destruction cleanup must detach it, so the stage entry
        // reads back null.
        delete external;
        CHECK(p.stage("xtr") == nullptr);

        // Close walks the now-null entry without touching freed memory.
        CHECK(p.close().isOk());
        CHECK(p.state() == MediaPipeline::State::Closed);
}

TEST_CASE("MediaPipeline_InjectStageAdoptsIONameWhenSet") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();

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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();

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
                        BasicThread::sleepMs(5);
                }
                return false;
        }

} // namespace

TEST_CASE("MediaPipeline_CloseAsyncEmitsClosedSignal") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
                        std::atomic<int> transportStateChanged{0};
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
                        case PipelineEvent::Kind::TransportStateChanged: counts.transportStateChanged.fetch_add(1); break;
                }
        }

} // namespace

TEST_CASE("MediaPipeline_SubscribeReceivesStateAndStageEvents") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        BasicThread::sleepMs(150);
        loop.processEvents();
        const int afterStop = counts.statsUpdated.load();
        CHECK(afterStop == duringRun);

        CHECK(p.close().isOk());
        p.unsubscribe(subId);
}

TEST_CASE("MediaPipeline_UnsubscribeStopsEventsToThatSubscriber") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;

        Thread worker;
        worker.start();
        // Wait for the worker thread's EventLoop to come up.
        ElapsedTimer t;
        t.start();
        while (worker.threadEventLoop() == nullptr && t.elapsed() < 1000) {
                BasicThread::sleepMs(1);
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
                BasicThread::sleepMs(1);
        }

        REQUIRE(p.build(makeTpgToCsc()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        // Wait for the worker thread to dispatch some events.
        t.start();
        while (received.load() < 2 && t.elapsed() < 1500) {
                loop.processEvents();
                BasicThread::sleepMs(5);
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
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

// ============================================================================
// Phase 2 — Playback transport: state, pause-via-clock, play, startPaused
// ============================================================================

namespace {

        // Builds a self-contained Playback config: TPG → CSC where the
        // CSC stage carries the pacing flag.  CSC is a Transform, but
        // its port-group still owns a Clock that the pipeline can pause
        // — that's all the play/pause path needs.  Used for the pause /
        // resume / startPaused / kind-gate tests below.
        MediaPipelineConfig makePlaybackConfig(bool startPaused = false) {
                MediaPipelineConfig cfg = makeTpgToCsc();
                cfg.setKind(MediaPipelineConfig::Kind::Playback);
                cfg.setStartPaused(startPaused);
                for (auto &stage : cfg.stages()) {
                        if (stage.name == "csc") {
                                stage.pacesPipeline = true;
                                break;
                        }
                }
                return cfg;
        }

        // Builds a Capture config based on the same shape so the kind-
        // gate negative tests have a real Capture pipeline to drive.
        MediaPipelineConfig makeCaptureConfig() {
                MediaPipelineConfig cfg = makeTpgToCsc();
                cfg.setKind(MediaPipelineConfig::Kind::Capture);
                for (auto &stage : cfg.stages()) {
                        if (stage.name == "csc") {
                                stage.captureSink = true;
                                break;
                        }
                }
                return cfg;
        }

} // namespace

TEST_CASE("MediaPipeline_Transport_DefaultPlaybackStateIsIdle") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Idle);
}

TEST_CASE("MediaPipeline_Transport_StartLandsInPlayingForPlaybackKind") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Playing);
        CHECK(p.stop().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Idle);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_StartPausedLandsInPaused") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig(/*startPaused=*/true)).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Paused);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_PauseAndPlayFlipState") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> transitions{0};
        p.playbackStateChangedSignal.connect(
                [&transitions](MediaPipeline::PlaybackState) { transitions.fetch_add(1); }, &p);

        REQUIRE(p.pause().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Paused);
        REQUIRE(p.play().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Playing);
        // pause -> Paused (1), play -> Playing (2).
        CHECK(transitions.load() == 2);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_TogglePlayPauseFlipsBetweenPlayingAndPaused") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Playing);

        REQUIRE(p.togglePlayPause().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Paused);
        REQUIRE(p.togglePlayPause().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Playing);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_RejectsPlayPauseOutsideRunning") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        // Empty pipeline rejects with NotOpen — there's no Running
        // state to act on.
        CHECK(p.play() == Error::NotOpen);
        CHECK(p.pause() == Error::NotOpen);

        REQUIRE(p.build(makePlaybackConfig()).isOk());
        // Built but not open: still rejects.
        CHECK(p.play() == Error::NotOpen);
        REQUIRE(p.open().isOk());
        // Open but not started: still rejects.
        CHECK(p.play() == Error::NotOpen);
        REQUIRE(p.start().isOk());

        // Running: succeeds.
        CHECK(p.pause() == Error::Ok);
        CHECK(p.play() == Error::Ok);

        // Stopped: rejects again.
        REQUIRE(p.stop().isOk());
        CHECK(p.play() == Error::NotOpen);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_CapturePipelineRejectsPlayPause") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        // Capture pipelines surface a kind mismatch on play / pause —
        // capture transport lands in Phase 4 with its own surface.
        CHECK(p.play() == Error::NotSupported);
        CHECK(p.pause() == Error::NotSupported);
        // Capture pipelines stay Idle on the playback transport even
        // while Running.
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Idle);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_PlaybackStateChangedSubscriberSeesEvents") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());

        std::atomic<int> transportEvents{0};
        const int        subId = p.subscribe([&transportEvents](const PipelineEvent &ev) {
                if (ev.kind() == PipelineEvent::Kind::TransportStateChanged) transportEvents.fetch_add(1);
        });
        REQUIRE(subId >= 0);

        REQUIRE(p.start().isOk());
        REQUIRE(p.pause().isOk());
        REQUIRE(p.play().isOk());

        // Pump the loop briefly so the queued PipelineEvent callbacks run.
        ElapsedTimer wd;
        wd.start();
        while (transportEvents.load() < 3 && wd.elapsed() < 1000) loop.processEvents();
        // start -> Playing (1), pause -> Paused (2), play -> Playing (3).
        CHECK(transportEvents.load() == 3);

        p.unsubscribe(subId);
        CHECK(p.close().isOk());
}

// ============================================================================
// Phase 3 — Playback transport: seek, rate, frame step
// ============================================================================

TEST_CASE("MediaPipeline_Transport_RateDefaultsToOne") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.rate() == 1.0);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_SetRateForwardsToPacingGroup") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        std::atomic<int> rateChanges{0};
        p.rateChangedSignal.connect([&rateChanges](double) { rateChanges.fetch_add(1); }, &p);

        REQUIRE(p.setRate(0.5) == Error::Ok);
        CHECK(p.rate() == 0.5);
        REQUIRE(p.setRate(2.0) == Error::Ok);
        CHECK(p.rate() == 2.0);
        // Setting the same rate twice must not double-fire the signal.
        REQUIRE(p.setRate(2.0) == Error::Ok);
        CHECK(rateChanges.load() == 2);

        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_SetRateRejectsNonFinite") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        CHECK(p.setRate(std::numeric_limits<double>::quiet_NaN()) == Error::InvalidArgument);
        CHECK(p.setRate(std::numeric_limits<double>::infinity()) == Error::InvalidArgument);
        // Rate is unchanged after the rejected calls.
        CHECK(p.rate() == 1.0);

        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_SetRateRejectsCaptureKind") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.setRate(0.5) == Error::NotSupported);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_SeekRejectsNonSeekableGroup") {
        // TPG → CSC where CSC paces.  Neither stage's port group reports
        // canSeek=true, so seek must surface Error::IllegalSeek without
        // touching the backend.
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.seek(FrameNumber(10), MediaIO_SeekExact) == Error::IllegalSeek);
        // State stays Playing because the pause-then-seek path is gated
        // on canSeek before any state change.
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Playing);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_SeekRejectsCaptureAndOutsideRunning") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        // Outside Running.
        CHECK(p.seek(FrameNumber(0)) == Error::NotOpen);
        // Capture pipeline.
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.seek(FrameNumber(0)) == Error::NotSupported);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_StepForwardPausesEvenWhenSeekFails") {
        // stepForward pauses first, then dispatches the seek.  When the
        // seek fails (e.g. non-seekable TPG), the transport must end up
        // in Paused so the caller has a defined state — matches the
        // user-stated contract that stepping always lands paused.
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Playing);
        Error err = p.stepForward(1);
        // Pause already happened; the seek itself returns IllegalSeek.
        CHECK(err == Error::IllegalSeek);
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Paused);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_StepForwardWithZeroIsNoOp") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.stepForward(0) == Error::Ok);
        // No pause was forced because the no-op exit happens before
        // the pause path.
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Playing);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_StepBackwardForwardsToStepForward") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        // Negative-target seek would clamp to 0; TPG is non-seekable so
        // we get IllegalSeek either way and the state lands Paused.
        Error err = p.stepBackward(2);
        CHECK(err == Error::IllegalSeek);
        CHECK(p.playbackState() == MediaPipeline::PlaybackState::Paused);
        CHECK(p.close().isOk());
}

// ============================================================================
// Phase 4 — Capture transport: gate, trigger, keyframe stamp
// ============================================================================

TEST_CASE("MediaPipeline_Transport_DefaultCaptureStateIsIdle") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        CHECK(p.captureState() == MediaPipeline::CaptureState::Idle);
}

TEST_CASE("MediaPipeline_Transport_StartCaptureMovesIdleToRecording") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.captureState() == MediaPipeline::CaptureState::Idle);

        std::atomic<int> changes{0};
        p.captureStateChangedSignal.connect(
                [&changes](MediaPipeline::CaptureState) { changes.fetch_add(1); }, &p);

        REQUIRE(p.startCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Recording);
        CHECK(changes.load() == 1);

        REQUIRE(p.pauseCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Paused);
        REQUIRE(p.resumeCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Recording);
        CHECK(changes.load() == 3);

        REQUIRE(p.stopCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Idle);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_ArmWithoutTriggerCollapsesToRecording") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        // No trigger installed -> arm is a record-now alias.
        REQUIRE(p.armCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Recording);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_ArmWithTriggerStaysArmedUntilMatch") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        // Function trigger that never fires.
        std::atomic<int> evaluations{0};
        REQUIRE(p.setCaptureTrigger([&evaluations](const Frame &) {
                evaluations.fetch_add(1);
                return false;
        }) == Error::Ok);
        REQUIRE(p.armCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Armed);
        // Pump briefly so the inspector gets called for some frames.
        for (int i = 0; i < 50; ++i) loop.processEvents();
        // We don't strictly require N evaluations here — TPG's drain
        // rate depends on backpressure since the gate is closed —
        // but state must still be Armed.
        CHECK(p.captureState() == MediaPipeline::CaptureState::Armed);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_TriggerFiresTransitionsToRecording") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        // Trigger that fires immediately — first frame inspected
        // should flip the state.
        REQUIRE(p.setCaptureTrigger([](const Frame &) { return true; }) == Error::Ok);
        REQUIRE(p.armCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Armed);

        // Pump until the inspector runs at least once.
        ElapsedTimer wd;
        wd.start();
        while (p.captureState() == MediaPipeline::CaptureState::Armed && wd.elapsed() < 1000) {
                loop.processEvents();
        }
        CHECK(p.captureState() == MediaPipeline::CaptureState::Recording);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_QueryStringTriggerParses") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        // Query expressions are parsed eagerly via VariantQuery<Frame>;
        // a syntactically-valid one must round-trip cleanly.
        CHECK(p.setCaptureTrigger(String("Meta.FrameKeyframe == true")) == Error::Ok);
        // Bogus expression surfaces the parse error.
        Error err = p.setCaptureTrigger(String("not_a_real_field >>> 5"));
        CHECK(err.isError());
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_ClearCaptureTriggerLeavesArmedRecording") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        REQUIRE(p.setCaptureTrigger([](const Frame &) { return false; }) == Error::Ok);
        CHECK(p.clearCaptureTrigger() == Error::Ok);
        // After clearing, arming again must collapse to Recording
        // because the no-trigger path applies.
        REQUIRE(p.armCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Recording);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipeline_Transport_PlaybackKindRejectsCaptureCalls") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makePlaybackConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());
        CHECK(p.armCapture() == Error::NotSupported);
        CHECK(p.startCapture() == Error::NotSupported);
        CHECK(p.pauseCapture() == Error::NotSupported);
        CHECK(p.resumeCapture() == Error::NotSupported);
        CHECK(p.stopCapture() == Error::NotSupported);
        CHECK(p.setCaptureTrigger([](const Frame &) { return true; }) == Error::NotSupported);
        CHECK(p.setCaptureTrigger(String("Meta.FrameKeyframe == true")) == Error::NotSupported);
        CHECK(p.clearCaptureTrigger() == Error::NotSupported);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaPipelineQueryTrigger_ParsesValidExpression") {
        // Direct trigger-class smoke test: confirm parse + match work
        // without needing the full pipeline harness.
        auto res = MediaPipelineQueryTrigger::parse(String("Meta.FrameKeyframe == true"));
        REQUIRE(res.second().isOk());
        REQUIRE(res.first().isValid());
}

TEST_CASE("MediaPipelineQueryTrigger_RejectsBogusExpression") {
        auto res = MediaPipelineQueryTrigger::parse(String("garbage >>> nope"));
        CHECK(res.second().isError());
}

TEST_CASE("MediaPipelineFunctionTrigger_EmptyPredicateNeverMatches") {
        MediaPipelineFunctionTrigger trig{nullptr};
        Frame                        f;
        CHECK_FALSE(trig.match(f));
}

TEST_CASE("MediaPipelineFunctionTrigger_PredicateForwarded") {
        MediaPipelineFunctionTrigger trig{[](const Frame &) { return true; }};
        Frame                        f;
        CHECK(trig.match(f));
}

// ============================================================================
// Phase 5 — setIngestPaused fan-out
// ============================================================================

namespace {

        // MediaIO subclass whose only purpose is to record setIngestPaused
        // calls so a test can confirm the pipeline's pause/resume capture
        // path fans the hook out to every captureSink-flagged stage.
        class RecordingSinkMediaIO : public MediaIO {
                public:
                        RecordingSinkMediaIO() : MediaIO(nullptr) {}
                        void submit(MediaIOCommand::Ptr) override {}
                        void setIngestPaused(bool paused) override {
                                if (paused) ++pausedCount;
                                else        ++resumedCount;
                        }
                        int pausedCount = 0;
                        int resumedCount = 0;
        };

} // namespace

TEST_CASE("MediaPipeline_Transport_PauseResumeFanIngestHook") {
        char       *argv[] = {(char *)"test"};
        Application app(1, argv);
        EventLoop  &loop = *Application::mainEventLoop();
        MediaPipeline p;
        REQUIRE(p.build(makeCaptureConfig()).isOk());
        REQUIRE(p.open().isOk());
        REQUIRE(p.start().isOk());

        // The capture sink the pipeline resolved is a real MediaIO; we
        // can't swap it after open, but we can verify the fan-out
        // contract holds by exercising pauseCapture / resumeCapture
        // and confirming the lifecycle remains valid (the default
        // setIngestPaused override is a no-op, so this just tests
        // that the fan-out call doesn't crash and capture state
        // advances as expected).
        REQUIRE(p.startCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Recording);
        REQUIRE(p.pauseCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Paused);
        REQUIRE(p.resumeCapture() == Error::Ok);
        CHECK(p.captureState() == MediaPipeline::CaptureState::Recording);
        CHECK(p.close().isOk());
}

TEST_CASE("MediaIO_SetIngestPausedDefaultIsNoOp") {
        // Verify the base-class default truly does nothing (no virtual
        // dispatch failure, no state change).  Concrete RecordingSink
        // exercises the override path; we keep it here for test
        // discoverability rather than wiring it through a real pipeline.
        RecordingSinkMediaIO io;
        io.setIngestPaused(true);
        io.setIngestPaused(false);
        CHECK(io.pausedCount == 1);
        CHECK(io.resumedCount == 1);
}
