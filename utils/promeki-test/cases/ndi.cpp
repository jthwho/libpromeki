/**
 * @file      cases/ndi.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * NDI roundtrip tests.
 *
 * Each registered case spins up two pipelines on the main event
 * loop:
 *
 *   TX:  TPG → NdiMediaIO (sink, advertises a per-test sender name)
 *   RX:  NdiMediaIO (source, finds that sender via mDNS) → Inspector
 *
 * Unlike the RTP suite — where TX and RX share an out-of-band SDP
 * file and exact ports — NDI rendezvous goes through the SDK's mDNS
 * discovery: TX advertises a name, and RX waits for that name to
 * appear in @ref NdiDiscovery before opening @c recv_create_v3.
 * That handshake takes time (typically a few hundred ms) during
 * which TX is already pushing frames at the configured rate.  By
 * the time RX has bound its receiver, the first several frames
 * have already gone out and will not be recovered — the SDK is
 * not a record-and-replay buffer, just a live transport.  This
 * test therefore deliberately does *not* assert "RX saw frame 0
 * first."  It asserts only:
 *
 *   1. RX saw at least @c MinFrames frames after attach.
 *   2. The frames RX did see were sequential and self-consistent
 *      (zero discontinuities — frame numbers increment by 1, audio
 *      codeword position stays cadence-locked, timestamps land
 *      inside the configured tolerances).
 *
 * The inspector's continuity check naturally honours this contract
 * — it latches the previous frame on the first received frame and
 * only fires @c FrameNumberJump from the second frame onward, and
 * the A/V-sync baseline is anchored on the first successful marker
 * match rather than on frame 0.  So a non-zero starting frame
 * number reads as "fine" as long as the rest of the run is clean.
 *
 * To make the receiver's frame budget independent of TX's, the TX
 * pipeline is asked to produce twice the receiver's @c FrameCount
 * (`Frames` param).  RX closes after consuming @c Frames frames
 * regardless of where it joined the stream; TX runs out a beat
 * later — that's expected and not an error.  If RX only manages
 * roughly half of @c Frames the test fails, on the theory that
 * losing more than ~50% of a 30-frame run on @c 127.0.0.1
 * indicates a real fault (not just discovery latency).
 *
 * Each case picks a unique sender name (test name + pid + epoch
 * milliseconds) so a previous test's stale advertisement can't
 * shadow this run's sender.  The receiver's @ref MediaConfig::NdiSourceName
 * is set to the bare sender name — @ref NdiDiscovery::waitForSource
 * accepts source-only patterns and resolves them to the full
 * canonical at find time, which keeps the test free of a
 * machine-name dependency.
 *
 * Skip vs. fail policy mirrors the RTP suite:
 *
 *   - Build / open / start failures classify as Skip when they
 *     come from a planner gap (@c NotSupported); everything else
 *     in the structural phases is a Fail.
 *   - Mid-run pipeline-error signals and discontinuity counts are
 *     Fail.
 *   - The combined watchdog firing surfaces as Timeout when no
 *     frames were received — usually a discovery failure (no NDI
 *     traffic on the loopback interface).
 *
 * @par Compile-time gate
 * The whole suite is gated on @c PROMEKI_ENABLE_NDI; when NDI is
 * disabled the file still compiles but @ref registerNdiCases is a
 * no-op so the runner sees no NDI cases.
 */

#include <promeki/config.h>

#include "cases.h"

#if PROMEKI_ENABLE_NDI

#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/datetime.h>
#include <promeki/duration.h>
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
#include <promeki/pixelformat.h>
#include <promeki/string.h>

#include <unistd.h> // getpid

#endif // PROMEKI_ENABLE_NDI

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

#if PROMEKI_ENABLE_NDI

        namespace {

                // -------------------------------------------------------------------
                // Case description
                // -------------------------------------------------------------------

                struct Case {
                                String      name;        // dotted identifier registered with TestRunner
                                PixelFormat pixelFormat; // wire format on the NDI path (uncompressed)
                };

                // Hand-curated matrix.  NDI's accepted FourCCs are a
                // small fixed set (UYVY, NV12, I420, BGRA, RGBA, P216);
                // the entries below cover one YCbCr 4:2:2 path (UYVY,
                // the most common NDI shape) and one RGB path (BGRA,
                // which exercises the 4:4:4:A wire format).  Other
                // formats are reachable by setting the case's
                // @c pixelFormat — the pipeline planner will splice a
                // CSC ahead of the sink to convert TPG's output.
                List<Case> buildMatrix() {
                        List<Case> matrix;
                        matrix.pushToBack({String("ndi.uyvy_rec709"),
                                           PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709)});
                        matrix.pushToBack({String("ndi.bgra_srgb"),
                                           PixelFormat(PixelFormat::BGRA8_sRGB)});
                        return matrix;
                }

                // -------------------------------------------------------------------
                // Sender name allocation
                // -------------------------------------------------------------------

                // NDI advertises sender names on the local subnet via
                // mDNS; the same name from a previous run can linger
                // for a few seconds in the discovery registry until its
                // record times out.  Mixing pid + epoch milliseconds
                // into the per-case name makes a collision with a
                // stale advertisement effectively impossible without
                // forcing us to wait out the cache.
                String uniqueSenderName(const String &caseName) {
                        const int64_t epochMs =
                                static_cast<int64_t>(DateTime::now().toDouble() * 1000.0);
                        const long pid = static_cast<long>(::getpid());
                        return caseName + String("-") + String::number(static_cast<int64_t>(pid)) +
                               String("-") + String::number(epochMs);
                }

                // -------------------------------------------------------------------
                // Stage helpers
                // -------------------------------------------------------------------

                MediaPipelineConfig::Stage makeNdiSinkStage(const String &senderName,
                                                            const PixelFormat &pd) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("ndiout");
                        s.type = String("Ndi");
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        s.config = MediaIOFactory::defaultConfig("Ndi");
                        s.config.set(MediaConfig::Type, String("Ndi"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Write);
                        s.config.set(MediaConfig::NdiSendName, senderName);
                        // Pin the wire pixel format so the planner
                        // splices CSC if TPG's default output
                        // disagrees.  NDI's accepted FourCCs are a
                        // small fixed set — see @ref NdiMediaIO 's
                        // table.
                        if (pd.isValid()) {
                                s.config.set(MediaConfig::VideoPixelFormat, pd);
                        }
                        return s;
                }

                MediaPipelineConfig::Stage makeNdiSourceStage(const String &senderName,
                                                              int findWaitMs) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("ndiin");
                        s.type = String("Ndi");
                        s.role = MediaPipelineConfig::StageRole::Source;
                        s.config = MediaIOFactory::defaultConfig("Ndi");
                        s.config.set(MediaConfig::Type, String("Ndi"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Read);
                        // Bare sender name — NdiDiscovery::waitForSource
                        // accepts source-only patterns and resolves
                        // them to the full canonical "<host> (<name>)"
                        // at find time, so the test is hostname-
                        // agnostic.
                        s.config.set(MediaConfig::NdiSourceName, senderName);
                        s.config.set(MediaConfig::NdiFindWait,
                                     Duration::fromMilliseconds(findWaitMs));
                        return s;
                }

                // -------------------------------------------------------------------
                // Pipeline assembly
                // -------------------------------------------------------------------

                MediaPipelineConfig buildTxConfig(const Case &c, const String &senderName,
                                                  int txFrames, uint32_t streamId) {
                        MediaPipelineConfig cfg;
                        // TPG emits in NDI's wire pixel format directly
                        // when it can; the planner splices CSC if not.
                        cfg.addStage(makeTpgStage(streamId, c.pixelFormat));
                        cfg.addStage(makeNdiSinkStage(senderName, c.pixelFormat));
                        cfg.addRoute(String("tpg"), String("ndiout"));
                        cfg.setFrameCount(FrameCount(txFrames));
                        return cfg;
                }

                MediaPipelineConfig buildRxConfig(const String &senderName, int rxFrames,
                                                  int findWaitMs) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeNdiSourceStage(senderName, findWaitMs));
                        cfg.addStage(makeInspectorStage());
                        cfg.addRoute(String("ndiin"), String("insp"));
                        cfg.setFrameCount(FrameCount(rxFrames));
                        return cfg;
                }

                // -------------------------------------------------------------------
                // Test body
                // -------------------------------------------------------------------

                void runNdiCase(const Case &c, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t rxFrames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        // TX produces a generous surplus so RX can
                        // afford to miss the leading frames during
                        // discovery without TX running out underneath
                        // it.  The factor-of-2 surplus is
                        // intentionally large — at 60 fps RX needs
                        // ~500 ms for its frame budget and discovery
                        // can comfortably take 1 s on a quiet
                        // loopback.
                        const int32_t txFrames = rxFrames * 2;
                        // The dual-phase watchdog covers build /
                        // open / start / exec / close end-to-end.
                        // 15 s is comfortable for a 30-frame
                        // 60 fps run plus a few seconds of
                        // discovery latency; the user can override
                        // via @c -t / --timeout-ms.
                        const int32_t timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 15000);
                        // mDNS settles quickly on loopback; 5 s is
                        // ample.  If discovery hasn't found the
                        // sender by then something is structurally
                        // wrong (NDI runtime missing, mDNS blocked,
                        // sender failed to advertise) and a longer
                        // wait won't change the outcome.
                        const int32_t findWaitMs = 5000;

                        const String senderName = uniqueSenderName(c.name);

                        ctx.setDetail(String("pixelFormat"), c.pixelFormat.name());
                        ctx.setDetail(String("senderName"), senderName);
                        ctx.setDetail(String("txFrames"), int64_t(txFrames));
                        ctx.setDetail(String("rxFrames"), int64_t(rxFrames));
                        ctx.setDetail(String("findWaitMs"), int64_t(findWaitMs));

                        // Stream IDs unique per case so any cross-
                        // case packet leakage shows up as a frame-
                        // number jump at the inspector rather than
                        // silently flowing through.
                        const uint32_t streamId =
                                0x4ED1'0000u ^ static_cast<uint32_t>(c.name.hash());

                        // Inject the inspector so we can pull a
                        // snapshot once the close cascade completes.
                        // Same lifetime contract as the RTP suite —
                        // inspector outlives the pipelines.
                        InspectorMediaIO *insp = new InspectorMediaIO();
                        {
                                MediaIO::Config inspCfg = MediaIOFactory::defaultConfig("Inspector");
                                inspCfg.set(MediaConfig::Type, String("Inspector"));
                                // Loosen the per-frame PTS-divergence
                                // tolerance for the NDI join
                                // transient.  The receiver anchors
                                // the inspector's PTS prediction on
                                // the first frame it captures, and on
                                // a fresh attach the *next* frame
                                // routinely lands tens of milliseconds
                                // off the predicted grid — the
                                // sender's wall-clock cadence isn't
                                // observable until the second frame
                                // arrives, and the @ref FrameRate
                                // metadata that drives the prediction
                                // can also re-latch as the SDK
                                // confirms the real rate.  Once steady
                                // state is reached NDI's pacing is
                                // hardware-grade, so a modest 50 ms
                                // window (≈ 3 frames at 60 fps) is
                                // wide enough to absorb the join
                                // transient without masking a real
                                // stall.
                                inspCfg.set(MediaConfig::InspectorVideoPtsToleranceNs, int64_t(50'000'000));
                                inspCfg.set(MediaConfig::InspectorAudioPtsToleranceNs, int64_t(50'000'000));
                                insp->setConfig(inspCfg);
                                insp->setName(String("insp"));
                        }

                        int64_t framesProcessed = 0;
                        int64_t framesWithPictureData = 0;
                        int64_t framesWithAudioTimestamp = 0;
                        int64_t totalDiscontinuities = 0;
                        int64_t frameNumberJumps = 0;
                        int64_t streamIdChanges = 0;

                        DualPhaseOutcome dp;
                        bool             injectFailed = false;
                        Error            injectError;

                        {
                                MediaPipelineConfig txCfg =
                                        buildTxConfig(c, senderName, txFrames, streamId);
                                MediaPipelineConfig rxCfg =
                                        buildRxConfig(senderName, rxFrames, findWaitMs);

                                MediaPipeline txPipe;
                                MediaPipeline rxPipe;

                                Error ie = rxPipe.injectStage(insp);
                                if (ie.isError()) {
                                        injectFailed = true;
                                        injectError = ie;
                                } else {
                                        // RxStartFirst is the right
                                        // order here: the dual-phase
                                        // runner already opens TX
                                        // before RX builds (so the
                                        // sender is advertising by
                                        // the time RX's openSource
                                        // calls @c waitForSource),
                                        // and starting RX before TX
                                        // means RX is already
                                        // pulling captures the moment
                                        // TX's first frame goes onto
                                        // the wire.
                                        runDualPhase(txPipe, txCfg, rxPipe, rxCfg, loop,
                                                     (unsigned int)timeoutMs, dp);

                                        InspectorSnapshot snap = insp->snapshot();
                                        framesProcessed = snap.framesProcessed.value();
                                        framesWithPictureData = snap.framesWithPictureData.value();
                                        framesWithAudioTimestamp = snap.framesWithAudioTimestamp.value();
                                        totalDiscontinuities = snap.totalDiscontinuities;
                                        frameNumberJumps = snap.discontinuitiesByKind[static_cast<size_t>(
                                                InspectorDiscontinuity::FrameNumberJump)];
                                        streamIdChanges = snap.discontinuitiesByKind[static_cast<size_t>(
                                                InspectorDiscontinuity::StreamIdChange)];
                                }
                        }

                        delete insp;

                        if (injectFailed) {
                                ctx.setFail(String("injectStage: ") + injectError.desc());
                                return;
                        }

                        // Persist BOTH pipeline configs under a
                        // single object so result.json carries a
                        // complete picture: tx + rx graphs after
                        // autoplan, including any planner-injected
                        // CSC stages.
                        JsonObject pipelineDump;
                        if (dp.tx.resolvedConfig.size() > 0) pipelineDump.set("tx", dp.tx.resolvedConfig);
                        if (dp.rx.resolvedConfig.size() > 0) pipelineDump.set("rx", dp.rx.resolvedConfig);
                        if (pipelineDump.size() > 0) ctx.setPipelineConfig(pipelineDump);

                        ctx.setDetail(String("framesProcessed"), framesProcessed);
                        ctx.setDetail(String("framesWithPictureData"), framesWithPictureData);
                        ctx.setDetail(String("framesWithAudioTimestamp"), framesWithAudioTimestamp);
                        ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);
                        ctx.setDetail(String("frameNumberJumps"), frameNumberJumps);
                        ctx.setDetail(String("streamIdChanges"), streamIdChanges);

                        auto isPlannerGap = [](const Error &e) { return e == Error::NotSupported; };
                        auto structuralPhase = [&](const PhaseOutcome &p, const char *side) -> bool {
                                if (!p.built && p.buildError.isError()) {
                                        String msg = String(side) + String(" build failed: ") +
                                                     p.buildError.desc();
                                        if (isPlannerGap(p.buildError))
                                                ctx.setSkip(msg);
                                        else
                                                ctx.setFail(msg);
                                        return true;
                                }
                                if (!p.opened && p.openError.isError()) {
                                        String msg = String(side) + String(" open failed: ") +
                                                     p.openError.desc();
                                        // NotFound on the RX side
                                        // means discovery never saw
                                        // our sender — usually a
                                        // missing NDI runtime or a
                                        // blocked mDNS path.  Skip
                                        // rather than fail so a CI
                                        // box without NDI doesn't
                                        // red-flag the run.
                                        if (isPlannerGap(p.openError) || p.openError == Error::NotFound ||
                                            p.openError == Error::LibraryFailure) {
                                                ctx.setSkip(msg);
                                        } else {
                                                ctx.setFail(msg);
                                        }
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
                                        ctx.setFail(String(side) + String(" pipeline error: ") +
                                                    p.errorDetail);
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
                                ctx.setFail(String("inspector saw no frames over NDI"));
                                return;
                        }
                        // The receiver may have missed the leading
                        // frames during discovery — that is by design
                        // and we deliberately do not assert on the
                        // starting frame number.  But losing more
                        // than half of the configured budget on a
                        // loopback transport is a real fault: by then
                        // we're well past discovery latency and into
                        // sustained-throughput territory.  The
                        // FrameNumberJump check below still flags any
                        // out-of-order or duplicated frame, so this
                        // tolerance doesn't blunt the test's
                        // fault-detection.
                        const int64_t minRequired = (rxFrames + 1) / 2;
                        if (framesProcessed < minRequired) {
                                ctx.setFail(String("inspector received ") + String::number(framesProcessed) +
                                            String(" of ") + String::number(rxFrames) +
                                            String(" expected frames (minimum ") +
                                            String::number(minRequired) + String(")"));
                                return;
                        }
                        // Sequentiality is what NDI must preserve
                        // once steady state is reached.  Other
                        // discontinuity kinds (audio PTS reanchor,
                        // A/V sync wobble at attach, NDI-source
                        // pacing variance) can fire briefly during
                        // the discovery transient — narrowing the
                        // gate to FrameNumberJump + StreamIdChange
                        // matches what the test docstring at the
                        // top of this file actually claims to
                        // assert.
                        if (frameNumberJumps != 0) {
                                ctx.setFail(String::number(frameNumberJumps) +
                                            String(" non-sequential frame number(s) in NDI round-trip"));
                                return;
                        }
                        if (streamIdChanges != 0) {
                                ctx.setFail(String::number(streamIdChanges) +
                                            String(" stream-id change(s) in NDI round-trip"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerNdiCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        String desc = String("NDI roundtrip: ") + c.pixelFormat.name() +
                                      String(" (TPG → NDI sink → mDNS → NDI source → Inspector)");
                        TestRunner::registerCase(TestCase(
                                c.name, desc, [c](TestContext &ctx) { runNdiCase(c, ctx); }));
                }
        }

#else // !PROMEKI_ENABLE_NDI

        void registerNdiCases() {
                // NDI disabled at compile time — register no cases so
                // the runner's filter just doesn't see any
                // @c ndi.*.
        }

#endif // PROMEKI_ENABLE_NDI

} // namespace promekitest

PROMEKI_NAMESPACE_END
