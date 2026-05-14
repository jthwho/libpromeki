/**
 * @file      cases/audio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * AudioFile roundtrip tests.
 *
 * Each case writes a TPG-generated audio stream to a file in one
 * of the formats @ref AudioFileMediaIO ships (WAV, BWF, AIFF, OGG)
 * and then reads it back through @ref InspectorMediaIO so the
 * Inspector can validate continuity, sample-count cadence, and PTS
 * jitter end-to-end.
 *
 * The pipeline is:
 *
 *   TPG (audio-only) → AudioFile (sink) → AudioFile (source) → Inspector
 *
 * Video is explicitly disabled at TPG so the sink only sees audio
 * essence.  The Inspector still applies its default check set
 * minus the image-data tests (which would just report "no video"
 * for every frame); audio-data + AvSync + sample-count tests do
 * the heavy lifting.
 *
 * Skip / fail policy mirrors @c roundtrip.cpp:
 *   - Build / open / start failures with @c Error::NotSupported
 *     classify as Skip (the format isn't wired up in this build —
 *     OGG, for example, requires the libsndfile Vorbis component).
 *   - Anything else, including a non-zero discontinuity count, is
 *     Fail.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/dir.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/filepath.h>
#include <promeki/framecount.h>
#include <promeki/inspectormediaio.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/objectbase.tpp>
#include <promeki/string.h>

#include <functional>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                struct Case {
                                String name;       // dotted identifier
                                String extension;  // "wav", "aiff", "bwf", "ogg"
                };

                List<Case> buildMatrix() {
                        List<Case> matrix;
                        // libsndfile's WAV / BWF / AIFF / OGG support
                        // is the slate of formats AudioFileFactory
                        // advertises in its @c extensions() list.  We
                        // exercise one case per format — these aren't
                        // codec-style variants where each one stresses
                        // a different code path, so a single
                        // representative file per backend dispatch is
                        // enough.
                        matrix.pushToBack({String("audio.wav"), String("wav")});
                        matrix.pushToBack({String("audio.bwf"), String("bwf")});
                        matrix.pushToBack({String("audio.aiff"), String("aiff")});
                        matrix.pushToBack({String("audio.ogg"), String("ogg")});
                        return matrix;
                }

                // -------------------------------------------------------------------
                // Stage helpers
                // -------------------------------------------------------------------

                MediaPipelineConfig::Stage makeAudioFileSinkStage(const String &path) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("out");
                        // Empty type + a path triggers the pipeline's
                        // path-based factory dispatch via
                        // @ref MediaIO::createForFileWrite, which
                        // resolves the extension to AudioFile and
                        // stamps OpenMode = Write.
                        s.path = path;
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        return s;
                }

                MediaPipelineConfig::Stage makeAudioFileSourceStage(const String &path) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("in");
                        s.path = path;
                        s.role = MediaPipelineConfig::StageRole::Source;
                        return s;
                }

                bool ensureParentDir(const String &path, String *errOut) {
                        FilePath fp(path);
                        FilePath parent = fp.parent();
                        Dir      d(parent);
                        if (d.exists()) return true;
                        Error e = d.mkpath();
                        if (e.isError()) {
                                if (errOut)
                                        *errOut = String("mkpath '") + parent.toString() + String("': ") + e.desc();
                                return false;
                        }
                        return true;
                }

                MediaPipelineConfig buildWriteConfig(const String &path, int frames, uint32_t streamId) {
                        MediaPipelineConfig cfg;
                        // TPG audio-only: video off, audio on.  Frame
                        // boundaries still come from the configured
                        // VideoFormat's frame rate, since the rest of
                        // the pipeline tracks a per-frame cadence even
                        // for audio-only flows.
                        cfg.addStage(makeTpgStage(streamId, PixelFormat(), /*videoEnabled=*/false,
                                                  /*audioEnabled=*/true));
                        cfg.addStage(makeAudioFileSinkStage(path));
                        cfg.addRoute(String("tpg"), String("out"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                MediaPipelineConfig buildReadConfig(const String &path) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeAudioFileSourceStage(path));
                        cfg.addStage(makeInspectorStage());
                        cfg.addRoute(String("in"), String("insp"));
                        return cfg;
                }

                // -------------------------------------------------------------------
                // Test body
                // -------------------------------------------------------------------

                void runAudioCase(const Case &c, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 10000);
                        const FilePath testFolder = ctx.testFolder();
                        const String   path =
                                (testFolder / (String("audio.") + c.extension)).toString();

                        const uint32_t streamId =
                                0xAFD00000u ^ static_cast<uint32_t>(c.name.hash());

                        ctx.setDetail(String("extension"), c.extension);
                        ctx.setDetail(String("path"), path);

                        String parentErr;
                        if (!ensureParentDir(path, &parentErr)) {
                                ctx.setFail(parentErr);
                                return;
                        }

                        // ---- Write phase ----
                        JsonObject pipelineDump;
                        {
                                MediaPipelineConfig cfg = buildWriteConfig(path, frames, streamId);
                                MediaPipeline       pipe;
                                PhaseOutcome        p = runPhase(pipe, cfg, loop, (unsigned int)timeoutMs);
                                if (p.resolvedConfig.size() > 0) {
                                        pipelineDump.set("write", p.resolvedConfig);
                                        ctx.setPipelineConfig(pipelineDump);
                                }
                                ctx.setDetail(String("framesWritten"), int64_t(frames));

                                auto isPlannerGap = [](const Error &e) { return e == Error::NotSupported; };
                                if (!p.built) {
                                        if (isPlannerGap(p.buildError))
                                                ctx.setSkip(String("write build failed: ") + p.buildError.desc());
                                        else
                                                ctx.setFail(String("write build failed: ") + p.buildError.desc());
                                        return;
                                }
                                if (!p.opened) {
                                        if (isPlannerGap(p.openError))
                                                ctx.setSkip(String("write open failed: ") + p.openError.desc());
                                        else
                                                ctx.setFail(String("write open failed: ") + p.openError.desc());
                                        return;
                                }
                                if (!p.started) {
                                        if (isPlannerGap(p.startError))
                                                ctx.setSkip(String("write start failed: ") + p.startError.desc());
                                        else
                                                ctx.setFail(String("write start failed: ") + p.startError.desc());
                                        return;
                                }
                                if (p.timedOut) {
                                        ctx.setTimeout(String("write phase deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        return;
                                }
                                if (p.sawError) {
                                        ctx.setFail(String("write pipeline error: ") + p.errorDetail);
                                        return;
                                }
                        }

                        // ---- Read phase ----
                        InspectorMediaIO *insp = new InspectorMediaIO();
                        {
                                MediaIO::Config inspCfg = MediaIOFactory::defaultConfig("Inspector");
                                inspCfg.set(MediaConfig::Type, String("Inspector"));
                                insp->setConfig(inspCfg);
                                insp->setName(String("insp"));
                        }

                        int64_t framesProcessed = 0;
                        int64_t framesWithAudioTimestamp = 0;
                        int64_t totalDiscontinuities = 0;
                        int64_t audioSamplesTotal = 0;
                        bool    earlyExitSet = false;
                        {
                                MediaPipelineConfig cfg = buildReadConfig(path);
                                MediaPipeline       pipe;
                                Error               ie = pipe.injectStage(insp);
                                if (ie.isError()) {
                                        ctx.setFail(String("injectStage: ") + ie.desc());
                                        delete insp;
                                        return;
                                }

                                PhaseOutcome p = runPhase(pipe, cfg, loop, (unsigned int)timeoutMs);
                                if (p.resolvedConfig.size() > 0) {
                                        pipelineDump.set("read", p.resolvedConfig);
                                        ctx.setPipelineConfig(pipelineDump);
                                }

                                InspectorSnapshot snap = insp->snapshot();
                                framesProcessed = snap.framesProcessed.value();
                                framesWithAudioTimestamp = snap.framesWithAudioTimestamp.value();
                                totalDiscontinuities = snap.totalDiscontinuities;
                                audioSamplesTotal = snap.audioSamplesTotal;
                                ctx.setDetail(String("framesProcessed"), framesProcessed);
                                ctx.setDetail(String("framesWithAudioTimestamp"), framesWithAudioTimestamp);
                                ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);
                                ctx.setDetail(String("audioSamplesTotal"), audioSamplesTotal);

                                auto isPlannerGap = [](const Error &e) { return e == Error::NotSupported; };
                                if (!p.built) {
                                        if (isPlannerGap(p.buildError))
                                                ctx.setSkip(String("read build failed: ") + p.buildError.desc());
                                        else
                                                ctx.setFail(String("read build failed: ") + p.buildError.desc());
                                        earlyExitSet = true;
                                } else if (!p.opened) {
                                        if (isPlannerGap(p.openError))
                                                ctx.setSkip(String("read open failed: ") + p.openError.desc());
                                        else
                                                ctx.setFail(String("read open failed: ") + p.openError.desc());
                                        earlyExitSet = true;
                                } else if (!p.started) {
                                        if (isPlannerGap(p.startError))
                                                ctx.setSkip(String("read start failed: ") + p.startError.desc());
                                        else
                                                ctx.setFail(String("read start failed: ") + p.startError.desc());
                                        earlyExitSet = true;
                                } else if (p.timedOut) {
                                        ctx.setTimeout(String("read phase deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        earlyExitSet = true;
                                } else if (p.sawError) {
                                        ctx.setFail(String("read pipeline error: ") + p.errorDetail);
                                        earlyExitSet = true;
                                }
                        }

                        delete insp;
                        if (earlyExitSet) return;

                        if (framesProcessed <= 0) {
                                ctx.setFail(String("inspector saw no audio frames"));
                                return;
                        }
                        if (totalDiscontinuities != 0) {
                                ctx.setFail(String::number(totalDiscontinuities) +
                                            String(" discontinuities detected in audio round-trip"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerAudioCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        String desc = String("AudioFile roundtrip: ") + c.extension +
                                      String(" (TPG audio → AudioFile → AudioFile → Inspector)");
                        TestRunner::registerCase(TestCase(c.name, desc,
                                                          [c](TestContext &ctx) { runAudioCase(c, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
