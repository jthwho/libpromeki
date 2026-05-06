/**
 * @file      cases/framebridge.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Single-process FrameBridge roundtrip tests.
 *
 * FrameBridge is designed for cross-process frame transport via shared
 * memory + UNIX-domain handshake, so the canonical use case is two
 * processes negotiating a bridge by name.  These tests instead run a
 * sink and a source in the same process to verify the bridge works at
 * all — that the shared-memory ring buffer, metadata reservation, and
 * sync handshake are wired up end-to-end.  They aren't a substitute
 * for cross-process testing (the IPC handshake, mode bits, and
 * group-access keys can only be exercised across a process boundary),
 * but they catch any single-process regressions cheaply.
 *
 * Pipeline:
 *
 *   TX:  TPG → FrameBridge (sink, name pinned)
 *   RX:  FrameBridge (source, same name) → Inspector
 *
 * The dual-phase runner sequences TX-open before RX-open so the sink
 * has created the shared-memory segment + listening socket before the
 * source attaches.  @ref MediaConfig::FrameBridgeWaitForConsumer
 * defaults to @c true, so TX's first @c writeFrame blocks until RX
 * attaches — combined with the runner starting RX before TX, that
 * gives us a clean "no frame dropped before the consumer was ready"
 * guarantee.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/framecount.h>
#include <promeki/inspectormediaio.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/string.h>

#include <functional>
#include <unistd.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                struct Case {
                                String name;
                                bool   audio = true;
                };

                List<Case> buildMatrix() {
                        List<Case> matrix;
                        // One smoke case for now — the bridge is a
                        // single dispatch path regardless of the
                        // payload shape, so a single case (video +
                        // audio + metadata) exercises everything the
                        // single-process side of the bridge can
                        // surface.  Add format-specific or
                        // ring-depth-stress variants here as the need
                        // arises.
                        matrix.pushToBack({String("framebridge.smoke"), true});
                        return matrix;
                }

                // -------------------------------------------------------------------
                // Stage helpers
                // -------------------------------------------------------------------

                MediaPipelineConfig::Stage makeFrameBridgeSinkStage(const String &bridgeName) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("fbout");
                        s.type = String("FrameBridge");
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        s.config = MediaIOFactory::defaultConfig("FrameBridge");
                        s.config.set(MediaConfig::Type, String("FrameBridge"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Write);
                        s.config.set(MediaConfig::FrameBridgeName, bridgeName);
                        // Block writeFrame until a consumer attaches —
                        // combined with the dual-phase runner starting
                        // RX before TX, that means the sink will park
                        // on its very first frame until RX is ready,
                        // and no frame is silently dropped on the
                        // floor by a "free-running" producer.
                        s.config.set(MediaConfig::FrameBridgeWaitForConsumer, true);
                        return s;
                }

                MediaPipelineConfig::Stage makeFrameBridgeSourceStage(const String &bridgeName) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("fbin");
                        s.type = String("FrameBridge");
                        s.role = MediaPipelineConfig::StageRole::Source;
                        s.config = MediaIOFactory::defaultConfig("FrameBridge");
                        s.config.set(MediaConfig::Type, String("FrameBridge"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Read);
                        s.config.set(MediaConfig::FrameBridgeName, bridgeName);
                        return s;
                }

                MediaPipelineConfig buildTxConfig(const Case &c, const String &bridgeName, int frames,
                                                  uint32_t streamId) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeTpgStage(streamId, PixelFormat(),
                                                  /*videoEnabled=*/true, /*audioEnabled=*/c.audio));
                        cfg.addStage(makeFrameBridgeSinkStage(bridgeName));
                        cfg.addRoute(String("tpg"), String("fbout"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                MediaPipelineConfig buildRxConfig(const String &bridgeName, int frames) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeFrameBridgeSourceStage(bridgeName));
                        cfg.addStage(makeInspectorStage());
                        cfg.addRoute(String("fbin"), String("insp"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                // -------------------------------------------------------------------
                // Test body
                // -------------------------------------------------------------------

                void runFrameBridgeCase(const Case &c, size_t caseIndex, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 10000);

                        // Per-case unique bridge name so successive
                        // cases never reuse a name the kernel hasn't
                        // finished cleaning up between runs.  PID +
                        // index keeps it stable for diagnostics while
                        // staying disjoint across concurrent runs of
                        // the test binary.
                        const String bridgeName = String("promeki-test-fb-") +
                                                  String::number(static_cast<int64_t>(getpid())) + String("-") +
                                                  String::number(static_cast<int64_t>(caseIndex));

                        const uint32_t streamId =
                                0xFB000000u ^ static_cast<uint32_t>(std::hash<std::string>{}(c.name.str()));

                        ctx.setDetail(String("bridgeName"), bridgeName);
                        ctx.setDetail(String("frames"), int64_t(frames));

                        InspectorMediaIO *insp = new InspectorMediaIO();
                        {
                                MediaIO::Config inspCfg = MediaIOFactory::defaultConfig("Inspector");
                                inspCfg.set(MediaConfig::Type, String("Inspector"));
                                insp->setConfig(inspCfg);
                                insp->setName(String("insp"));
                        }

                        int64_t framesProcessed = 0;
                        int64_t framesWithPictureData = 0;
                        int64_t framesWithAudioTimestamp = 0;
                        int64_t totalDiscontinuities = 0;
                        DualPhaseOutcome dp;
                        bool             injectFailed = false;
                        Error            injectError;

                        {
                                MediaPipelineConfig txCfg = buildTxConfig(c, bridgeName, frames, streamId);
                                MediaPipelineConfig rxCfg = buildRxConfig(bridgeName, frames);

                                MediaPipeline txPipe;
                                MediaPipeline rxPipe;

                                Error ie = rxPipe.injectStage(insp);
                                if (ie.isError()) {
                                        injectFailed = true;
                                        injectError = ie;
                                } else {
                                        // FrameBridge needs TX's
                                        // listening socket up before
                                        // the RX-side handshake — see
                                        // @ref DualPhaseSequence::TxStartFirst.
                                        // The bridge's AcceptWorker
                                        // services handshakes off the
                                        // strand the moment openOutput
                                        // returns, so RX autoplan's
                                        // brief-open probe of the
                                        // source no longer needs a
                                        // running producer to ACPT.
                                        runDualPhase(txPipe, txCfg, rxPipe, rxCfg, loop,
                                                     (unsigned int)timeoutMs, dp,
                                                     DualPhaseSequence::TxStartFirst);

                                        InspectorSnapshot snap = insp->snapshot();
                                        framesProcessed = snap.framesProcessed.value();
                                        framesWithPictureData = snap.framesWithPictureData.value();
                                        framesWithAudioTimestamp = snap.framesWithAudioTimestamp.value();
                                        totalDiscontinuities = snap.totalDiscontinuities;
                                }
                        }

                        delete insp;

                        if (injectFailed) {
                                ctx.setFail(String("injectStage: ") + injectError.desc());
                                return;
                        }

                        JsonObject pipelineDump;
                        if (dp.tx.resolvedConfig.size() > 0) pipelineDump.set("tx", dp.tx.resolvedConfig);
                        if (dp.rx.resolvedConfig.size() > 0) pipelineDump.set("rx", dp.rx.resolvedConfig);
                        if (pipelineDump.size() > 0) ctx.setPipelineConfig(pipelineDump);

                        ctx.setDetail(String("framesProcessed"), framesProcessed);
                        ctx.setDetail(String("framesWithPictureData"), framesWithPictureData);
                        ctx.setDetail(String("framesWithAudioTimestamp"), framesWithAudioTimestamp);
                        ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);

                        // Same drain-structural-failures-first pattern as
                        // the RTP suite — see notes there.
                        auto isPlannerGap = [](const Error &e) { return e == Error::NotSupported; };
                        auto structuralPhase = [&](const PhaseOutcome &p, const char *side) -> bool {
                                if (!p.built && p.buildError.isError()) {
                                        String msg = String(side) + String(" build failed: ") + p.buildError.desc();
                                        if (isPlannerGap(p.buildError))
                                                ctx.setSkip(msg);
                                        else
                                                ctx.setFail(msg);
                                        return true;
                                }
                                if (!p.opened && p.openError.isError()) {
                                        String msg = String(side) + String(" open failed: ") + p.openError.desc();
                                        if (isPlannerGap(p.openError))
                                                ctx.setSkip(msg);
                                        else
                                                ctx.setFail(msg);
                                        return true;
                                }
                                if (!p.started && p.startError.isError()) {
                                        String msg = String(side) + String(" start failed: ") +
                                                     p.startError.desc();
                                        if (isPlannerGap(p.startError))
                                                ctx.setSkip(msg);
                                        else
                                                ctx.setFail(msg);
                                        return true;
                                }
                                if (p.sawError) {
                                        ctx.setFail(String(side) + String(" pipeline error: ") + p.errorDetail);
                                        return true;
                                }
                                return false;
                        };
                        if (structuralPhase(dp.tx, "tx")) return;
                        if (structuralPhase(dp.rx, "rx")) return;

                        const bool watchdogFired = dp.tx.timedOut || dp.rx.timedOut;
                        if (watchdogFired && framesProcessed == 0) {
                                ctx.setTimeout(String("watchdog fired with no frames received past ") +
                                               String::number(timeoutMs) + String(" ms"));
                                return;
                        }
                        if (framesProcessed <= 0) {
                                ctx.setFail(String("inspector saw no frames over FrameBridge"));
                                return;
                        }
                        // FrameBridge is a lossless local IPC; the sync
                        // mode default makes the producer block until
                        // every frame is acknowledged, so we expect
                        // every frame to make the round-trip cleanly.
                        // Anything less is a real failure.
                        if (framesProcessed < frames) {
                                ctx.setFail(String("inspector received ") + String::number(framesProcessed) +
                                            String(" of ") + String::number(frames) +
                                            String(" expected frames"));
                                return;
                        }
                        if (totalDiscontinuities != 0) {
                                ctx.setFail(String::number(totalDiscontinuities) +
                                            String(" discontinuities detected in FrameBridge round-trip"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerFrameBridgeCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        size_t idx = i;
                        String desc = String("FrameBridge single-process roundtrip "
                                             "(TPG → FrameBridge → FrameBridge → Inspector)");
                        TestRunner::registerCase(TestCase(
                                c.name, desc, [c, idx](TestContext &ctx) { runFrameBridgeCase(c, idx, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
