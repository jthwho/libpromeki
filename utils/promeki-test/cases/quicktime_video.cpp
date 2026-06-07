/**
 * @file      cases/quicktime_video.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * QuickTime compressed-video roundtrip tests.
 *
 * For each container-standardised video codec the writer can mux from an
 * uncompressed source, this suite drives a full planner pipeline:
 *
 *   TPG (uncompressed video) → QuickTime sink (QuickTimeVideoCodec=<codec>)
 *   → QuickTime source → VideoDecoder → Inspector
 *
 * The sink's @ref MediaConfig::QuickTimeVideoCodec selects the codec; the
 * planner then splices a @ref VideoEncoder on the video axis (inserting a
 * CSC ahead of it to reach the encoder's accepted input), the muxer writes
 * the codec's sample entry, and on read-back the @ref VideoDecoder for that
 * codec is auto-selected.  A pass requires the Inspector to see frames and
 * zero discontinuities end-to-end — proving the encode → mux → demux →
 * decode chain holds together through the real pipeline.
 *
 * The matrix is ProRes (422 + 4444): both its encoder and decoder are the
 * single vendored FFmpeg backend, so the case is deterministic and needs
 * no GPU.  (H.264 / HEVC ride the very same QuickTimeVideoCodec mechanism,
 * but their encode backend auto-selects NVENC where present, which a
 * portable functional test can't depend on.)
 *
 * Checks are narrowed to @c Continuity / @c Timestamp: ProRes is lossy, so
 * bit-exact fidelity (covered by tests/unit/ffmpegvideocodec.cpp) is out of
 * scope here.  Skip / fail policy mirrors quicktime_audio.cpp:
 * @c Error::NotSupported on build / open / start is a Skip; anything else,
 * including a non-zero discontinuity count, is a Fail.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/dir.h>
#include <promeki/enumlist.h>
#include <promeki/enums_mediaio.h>
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
#include <promeki/pixelformat.h>
#include <promeki/string.h>
#include <promeki/videocodec.h>
#include <promeki/videoformat.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                struct Case {
                                String         name; // dotted identifier
                                VideoCodec::ID codec;
                };

                List<Case> buildMatrix() {
                        List<Case> m;
                        // ProRes: encoder + decoder are both the single FFmpeg
                        // backend, so these are fully deterministic everywhere.
                        m.pushToBack({String("quicktime_video.prores_422_proxy"), VideoCodec::ProRes_422_Proxy});
                        m.pushToBack({String("quicktime_video.prores_422_lt"), VideoCodec::ProRes_422_LT});
                        m.pushToBack({String("quicktime_video.prores_422"), VideoCodec::ProRes_422});
                        m.pushToBack({String("quicktime_video.prores_422_hq"), VideoCodec::ProRes_422_HQ});
                        m.pushToBack({String("quicktime_video.prores_4444"), VideoCodec::ProRes_4444});
                        m.pushToBack({String("quicktime_video.prores_4444_xq"), VideoCodec::ProRes_4444_XQ});
                        // H.264 / HEVC ride the same QuickTimeVideoCodec mechanism;
                        // their encode/decode backends auto-select (NVENC/NVDEC
                        // where present), so the case Skips cleanly via the
                        // NotSupported→Skip runtime handling on a box without them.
                        m.pushToBack({String("quicktime_video.h264"), VideoCodec::H264});
                        m.pushToBack({String("quicktime_video.hevc"), VideoCodec::HEVC});
                        return m;
                }

                MediaPipelineConfig::Stage makeQtSinkStage(const String &path, VideoCodec::ID codec) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("out");
                        s.path = path; // ".mov" → QuickTime backend, OpenMode=Write
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        s.config.set(MediaConfig::QuickTimeLayout, QuickTimeLayout::Classic);
                        s.config.set(MediaConfig::QuickTimeVideoCodec, VideoCodec(codec));
                        return s;
                }

                MediaPipelineConfig::Stage makeQtSourceStage(const String &path) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("in");
                        s.path = path;
                        s.role = MediaPipelineConfig::StageRole::Source;
                        return s;
                }

                MediaPipelineConfig::Stage makeNarrowedInspectorStage() {
                        MediaPipelineConfig::Stage s = makeInspectorStage();
                        EnumList                   tests = EnumList::forType<InspectorTest>();
                        tests.append(InspectorTest::Continuity);
                        tests.append(InspectorTest::Timestamp);
                        s.config.set(MediaConfig::InspectorTests, tests);
                        return s;
                }

                bool ensureParentDir(const String &path, String *errOut) {
                        FilePath fp(path);
                        FilePath parent = fp.parent();
                        Dir      d(parent);
                        if (d.exists()) return true;
                        Error e = d.mkpath();
                        if (e.isError()) {
                                if (errOut) *errOut = String("mkpath '") + parent.toString() + String("': ") + e.desc();
                                return false;
                        }
                        return true;
                }

                MediaPipelineConfig buildWriteConfig(const String &path, VideoCodec::ID codec, int frames,
                                                     uint32_t streamId) {
                        MediaPipelineConfig cfg;
                        // Uncompressed UYVY video, no audio: the QuickTimeVideoCodec
                        // selection drives the encoder splice on the video axis.
                        // Override the shared TPG default (4K 2160p59.94) down to
                        // 720p — this suite actually encodes the video (ProRes), so
                        // a 4K raster makes the per-phase encode/decode wall-clock
                        // blow past the watchdog; 720p keeps it fast and reliable.
                        MediaPipelineConfig::Stage tpg = makeTpgStage(
                                streamId, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709),
                                /*videoEnabled=*/true, /*audioEnabled=*/false);
                        tpg.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
                        cfg.addStage(tpg);
                        cfg.addStage(makeQtSinkStage(path, codec));
                        cfg.addRoute(String("tpg"), String("out"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                MediaPipelineConfig buildReadConfig(const String &path, VideoCodec::ID codec) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeQtSourceStage(path));
                        cfg.addStage(makeDecoderStage(VideoCodec(codec)));
                        cfg.addStage(makeNarrowedInspectorStage());
                        cfg.addRoute(String("in"), String("dec"));
                        cfg.addRoute(String("dec"), String("insp"));
                        return cfg;
                }

                void runQtVideoCase(const Case &c, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        // Backend gate: if the codec can't encode+decode in this
                        // build, Skip cleanly rather than reporting a planner gap.
                        VideoCodec codec(c.codec);
                        if (!codec.canEncode() || !codec.canDecode()) {
                                ctx.setSkip(String("codec '") + codec.name() +
                                            String("' has no encoder/decoder backend in this build"));
                                return;
                        }

                        const int32_t  frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t  timeoutMs = ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 15000);
                        const FilePath testFolder = ctx.testFolder();
                        const String   path = (testFolder / String("qt_video.mov")).toString();
                        const uint32_t streamId = 0x9C710000u ^ static_cast<uint32_t>(c.name.hash());

                        ctx.setDetail(String("codec"), codec.name());
                        ctx.setDetail(String("path"), path);

                        String parentErr;
                        if (!ensureParentDir(path, &parentErr)) {
                                ctx.setFail(parentErr);
                                return;
                        }

                        auto       isPlannerGap = [](const Error &e) { return e == Error::NotSupported; };
                        JsonObject pipelineDump;

                        // ---- Write phase ----
                        {
                                MediaPipelineConfig cfg = buildWriteConfig(path, c.codec, frames, streamId);
                                MediaPipeline       pipe;
                                PhaseOutcome        p = runPhase(pipe, cfg, loop, (unsigned int)timeoutMs);
                                if (p.resolvedConfig.size() > 0) {
                                        pipelineDump.set("write", p.resolvedConfig);
                                        ctx.setPipelineConfig(pipelineDump);
                                }
                                if (!p.built) {
                                        (isPlannerGap(p.buildError) ? ctx.setSkip(String("write build: ") + p.buildError.desc())
                                                                    : ctx.setFail(String("write build: ") + p.buildError.desc()));
                                        return;
                                }
                                if (!p.opened) {
                                        (isPlannerGap(p.openError) ? ctx.setSkip(String("write open: ") + p.openError.desc())
                                                                   : ctx.setFail(String("write open: ") + p.openError.desc()));
                                        return;
                                }
                                if (!p.started) {
                                        (isPlannerGap(p.startError) ? ctx.setSkip(String("write start: ") + p.startError.desc())
                                                                    : ctx.setFail(String("write start: ") + p.startError.desc()));
                                        return;
                                }
                                if (p.timedOut) {
                                        ctx.setTimeout(String("write phase deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        return;
                                }
                                if (p.sawError) {
                                        // A NotSupported runtime error means the
                                        // auto-selected encode backend (e.g. NVENC)
                                        // isn't usable on this host — Skip, don't Fail.
                                        (isPlannerGap(p.firstError)
                                                 ? ctx.setSkip(String("write runtime: ") + p.errorDetail)
                                                 : ctx.setFail(String("write pipeline error: ") + p.errorDetail));
                                        return;
                                }
                        }

                        // ---- Read phase ----
                        InspectorMediaIO *insp = new InspectorMediaIO();
                        insp->setConfig(makeNarrowedInspectorStage().config);
                        insp->setName(String("insp"));
                        int64_t framesProcessed = 0;
                        int64_t totalDiscontinuities = 0;
                        bool    earlyExit = false;
                        {
                                MediaPipelineConfig cfg = buildReadConfig(path, c.codec);
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
                                totalDiscontinuities = snap.totalDiscontinuities;
                                ctx.setDetail(String("framesProcessed"), framesProcessed);
                                ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);

                                if (!p.built) {
                                        (isPlannerGap(p.buildError) ? ctx.setSkip(String("read build: ") + p.buildError.desc())
                                                                    : ctx.setFail(String("read build: ") + p.buildError.desc()));
                                        earlyExit = true;
                                } else if (!p.opened) {
                                        (isPlannerGap(p.openError) ? ctx.setSkip(String("read open: ") + p.openError.desc())
                                                                   : ctx.setFail(String("read open: ") + p.openError.desc()));
                                        earlyExit = true;
                                } else if (!p.started) {
                                        (isPlannerGap(p.startError) ? ctx.setSkip(String("read start: ") + p.startError.desc())
                                                                    : ctx.setFail(String("read start: ") + p.startError.desc()));
                                        earlyExit = true;
                                } else if (p.timedOut) {
                                        ctx.setTimeout(String("read phase deadlocked past ") +
                                                       String::number(timeoutMs) + String(" ms"));
                                        earlyExit = true;
                                } else if (p.sawError) {
                                        (isPlannerGap(p.firstError)
                                                 ? ctx.setSkip(String("read runtime: ") + p.errorDetail)
                                                 : ctx.setFail(String("read pipeline error: ") + p.errorDetail));
                                        earlyExit = true;
                                }
                        }
                        delete insp;
                        if (earlyExit) return;

                        if (framesProcessed <= 0) {
                                ctx.setFail(String("inspector saw no frames"));
                                return;
                        }
                        if (totalDiscontinuities != 0) {
                                ctx.setFail(String::number(totalDiscontinuities) +
                                            String(" discontinuities in QuickTime ") + codec.name() +
                                            String(" video round-trip"));
                                return;
                        }
                        ctx.setPass();
                }

        } // namespace

        void registerQuickTimeVideoCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        String desc = String("QuickTime compressed-video roundtrip (TPG → QuickTime[") +
                                      VideoCodec(c.codec).name() + String("] → decode → Inspector)");
                        TestRunner::registerCase(
                                TestCase(c.name, desc, [c](TestContext &ctx) { runQtVideoCase(c, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
