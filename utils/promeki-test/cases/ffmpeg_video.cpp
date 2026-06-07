/**
 * @file      cases/ffmpeg_video.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * FFmpeg container (FfmpegMediaIO) compressed-video roundtrip tests.
 *
 * Mirrors quicktime_video.cpp but drives the generic FFmpeg container
 * backend instead of the native QuickTime one, writing into a Matroska
 * (@c .mkv) file — a format no native backend owns, so the @c .mkv
 * extension auto-routes to the fallback FFmpeg backend on both the
 * write and read sides.  This exercises the libavformat muxer +
 * demuxer end-to-end through the real planner pipeline:
 *
 *   TPG (uncompressed video) → FFmpeg sink (FfmpegVideoCodec=<codec>, .mkv)
 *   → FFmpeg source (.mkv) → VideoDecoder → Inspector
 *
 * The matrix is H.264 + HEVC: these exercise the muxer's avcC / hvcC
 * configuration-record build and the Annex-B to length-prefixed (AVCC)
 * sample conversion end-to-end, and their encode/decode backends
 * (x264 / x265 / NVENC + the FFmpeg decoder) are deterministic on a
 * host that has them — a case Skips cleanly where the encoder is not
 * built.
 *
 * Checks are narrowed to @c Continuity / @c Timestamp: these codecs are
 * lossy, so bit-exact fidelity (covered by tests/unit/ffmpegvideocodec.cpp)
 * is out of scope here.  @c Error::NotSupported on build / open / start
 * is a Skip; anything else, including a non-zero discontinuity count, is
 * a Fail.
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
                        // H.264 / HEVC exercise the avcC / hvcC extradata + the
                        // Annex-B to AVCC sample conversion in the muxer
                        // end-to-end.  They run when an encoder backend is built
                        // (x264 / x265 / NVENC) and Skip cleanly otherwise.
                        //
                        // (ProRes is deliberately absent: FFmpeg's Matroska
                        // ProRes path uses a frame 'icpf'-atom wrapping
                        // convention this backend does not yet reproduce —
                        // tracked as a follow-up.  ProRes round-trips natively
                        // through the QuickTime backend, covered by
                        // quicktime_video.cpp.)
                        m.pushToBack({String("ffmpeg_video.h264"), VideoCodec::H264});
                        m.pushToBack({String("ffmpeg_video.hevc"), VideoCodec::HEVC});
                        return m;
                }

                MediaPipelineConfig::Stage makeFfmpegSinkStage(const String &path, VideoCodec::ID codec) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("out");
                        s.path = path; // ".mkv" → FFmpeg fallback backend, OpenMode=Write
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        s.config.set(MediaConfig::FfmpegVideoCodec, VideoCodec(codec));
                        return s;
                }

                MediaPipelineConfig::Stage makeFfmpegSourceStage(const String &path) {
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
                        // Uncompressed UYVY video, no audio: the FfmpegVideoCodec
                        // selection drives the encoder splice on the video axis.
                        // 720p (down from the 4K TPG default) keeps the real
                        // ProRes encode/decode inside the watchdog window.
                        MediaPipelineConfig::Stage tpg = makeTpgStage(
                                streamId, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709),
                                /*videoEnabled=*/true, /*audioEnabled=*/false);
                        tpg.config.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte720p59_94));
                        cfg.addStage(tpg);
                        cfg.addStage(makeFfmpegSinkStage(path, codec));
                        cfg.addRoute(String("tpg"), String("out"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                MediaPipelineConfig buildReadConfig(const String &path, VideoCodec::ID codec) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeFfmpegSourceStage(path));
                        cfg.addStage(makeDecoderStage(VideoCodec(codec)));
                        cfg.addStage(makeNarrowedInspectorStage());
                        cfg.addRoute(String("in"), String("dec"));
                        cfg.addRoute(String("dec"), String("insp"));
                        return cfg;
                }

                void runFfmpegVideoCase(const Case &c, TestContext &ctx) {
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
                        const String   path = (testFolder / String("ffmpeg_video.mkv")).toString();
                        const uint32_t streamId = 0xFF710000u ^ static_cast<uint32_t>(c.name.hash());

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
                                            String(" discontinuities in FFmpeg ") + codec.name() +
                                            String(" video round-trip"));
                                return;
                        }
                        ctx.setPass();
                }

        } // namespace

        void registerFfmpegVideoCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        String desc = String("FFmpeg compressed-video roundtrip (TPG → FFmpeg[") +
                                      VideoCodec(c.codec).name() + String("].mkv → decode → Inspector)");
                        TestRunner::registerCase(
                                TestCase(c.name, desc, [c](TestContext &ctx) { runFfmpegVideoCase(c, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
