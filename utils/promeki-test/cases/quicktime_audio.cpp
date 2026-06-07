/**
 * @file      cases/quicktime_audio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * QuickTime compressed-audio roundtrip tests.
 *
 * For each container-standardised audio codec (AAC, AC-3, MP3, FLAC,
 * Opus) this suite drives a full planner pipeline:
 *
 *   TPG (UYVY video + audio) → QuickTime sink (QuickTimeAudioCodec=<codec>)
 *   → QuickTime source → (AudioDecoder) → Inspector
 *
 * The sink's @ref MediaConfig::QuickTimeAudioCodec selects the codec; the
 * planner then splices an @ref AudioEncoder on the audio axis (video rides
 * through as uncompressed UYVY), the muxer writes the codec's sample entry
 * + config box, and on read-back the @ref AudioDecoder for that codec is
 * auto-selected.  A pass requires the Inspector to see frames and zero
 * discontinuities end-to-end — proving the encode → mux → demux → decode
 * chain holds together through the real pipeline (not just the unit-level
 * writer/reader and codec round-trips).
 *
 * Checks are narrowed to @c Continuity / @c Timestamp / @c AudioSamples:
 * the lossy codecs can't preserve the per-frame codeword the @c AudioData
 * / @c AvSync checks need (bit-exact fidelity is covered by
 * @c tests/unit/ffmpegaudiocodec.cpp).
 *
 * Skip / fail policy mirrors @c audio.cpp: @c Error::NotSupported on
 * build / open / start is a Skip (the codec backend isn't in this build);
 * anything else, including a non-zero discontinuity count, is a Fail.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/audiocodec.h>
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

#include <functional>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                struct Case {
                                String         name; // dotted identifier
                                AudioCodec::ID codec;
                };

                List<Case> buildMatrix() {
                        List<Case> m;
                        m.pushToBack({String("quicktime_audio.aac"), AudioCodec::AAC});
                        m.pushToBack({String("quicktime_audio.ac3"), AudioCodec::AC3});
                        m.pushToBack({String("quicktime_audio.mp3"), AudioCodec::MP3});
                        m.pushToBack({String("quicktime_audio.flac"), AudioCodec::FLAC});
                        m.pushToBack({String("quicktime_audio.opus"), AudioCodec::Opus});
                        return m;
                }

                MediaPipelineConfig::Stage makeQtSinkStage(const String &path, AudioCodec::ID codec) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("out");
                        s.path = path; // ".mov" → QuickTime backend, OpenMode=Write
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        // Classic layout is the broadly compatible on-disk form;
                        // the audio codec selection drives the encoder splice.
                        s.config.set(MediaConfig::QuickTimeLayout, QuickTimeLayout::Classic);
                        s.config.set(MediaConfig::QuickTimeAudioCodec, AudioCodec(codec));
                        return s;
                }

                MediaPipelineConfig::Stage makeQtSourceStage(const String &path) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("in");
                        s.path = path;
                        s.role = MediaPipelineConfig::StageRole::Source;
                        return s;
                }

                // Inspector narrowed to the codec-agnostic continuity checks.
                MediaPipelineConfig::Stage makeNarrowedInspectorStage() {
                        MediaPipelineConfig::Stage s = makeInspectorStage();
                        EnumList                   tests = EnumList::forType<InspectorTest>();
                        tests.append(InspectorTest::Continuity);
                        tests.append(InspectorTest::Timestamp);
                        tests.append(InspectorTest::AudioSamples);
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

                MediaPipelineConfig buildWriteConfig(const String &path, AudioCodec::ID codec, int frames,
                                                     uint32_t streamId) {
                        MediaPipelineConfig cfg;
                        // UYVY video (accepted by the QuickTime muxer uncompressed,
                        // so no video encoder is spliced) + audio, so the file is a
                        // normal A/V .mov and only the audio axis gets a codec.
                        cfg.addStage(makeTpgStage(streamId, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709),
                                                  /*videoEnabled=*/true, /*audioEnabled=*/true));
                        cfg.addStage(makeQtSinkStage(path, codec));
                        cfg.addRoute(String("tpg"), String("out"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                // Explicit AudioDecoder transform (mirrors roundtrip.cpp's
                // makeDecoderStage for video).  It auto-detects the codec from
                // the incoming CompressedAudioPayload and echoes the
                // uncompressed video through, so the Inspector sees decoded PCM.
                MediaPipelineConfig::Stage makeAudioDecoderStage() {
                        MediaPipelineConfig::Stage s;
                        s.name = String("dec");
                        s.type = String("AudioDecoder");
                        s.role = MediaPipelineConfig::StageRole::Transform;
                        s.config = MediaIOFactory::defaultConfig("AudioDecoder");
                        s.config.set(MediaConfig::Type, String("AudioDecoder"));
                        return s;
                }

                MediaPipelineConfig buildReadConfig(const String &path) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeQtSourceStage(path));
                        cfg.addStage(makeAudioDecoderStage());
                        cfg.addStage(makeNarrowedInspectorStage());
                        cfg.addRoute(String("in"), String("dec"));
                        cfg.addRoute(String("dec"), String("insp"));
                        return cfg;
                }

                void runQtAudioCase(const Case &c, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        // Backend gate: if the codec can't encode+decode in this
                        // build, Skip cleanly rather than reporting a planner gap.
                        AudioCodec codec(c.codec);
                        if (!codec.canEncode() || !codec.canDecode()) {
                                ctx.setSkip(String("codec '") + codec.name() +
                                            String("' has no encoder/decoder backend in this build"));
                                return;
                        }

                        const int32_t  frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t  timeoutMs = ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 15000);
                        const FilePath testFolder = ctx.testFolder();
                        const String   path = (testFolder / String("qt_audio.mov")).toString();
                        const uint32_t streamId = 0x9C700000u ^ static_cast<uint32_t>(c.name.hash());

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
                                        ctx.setFail(String("write pipeline error: ") + p.errorDetail);
                                        return;
                                }
                        }

                        // ---- Read phase ----
                        // Pre-built Inspector injected by name so we can read its
                        // snapshot after the pipeline closes; its config must match
                        // the "insp" stage buildReadConfig stamped on.
                        InspectorMediaIO *insp = new InspectorMediaIO();
                        insp->setConfig(makeNarrowedInspectorStage().config);
                        insp->setName(String("insp"));
                        int64_t framesProcessed = 0;
                        int64_t totalDiscontinuities = 0;
                        int64_t audioSamplesTotal = 0;
                        bool    earlyExit = false;
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
                                totalDiscontinuities = snap.totalDiscontinuities;
                                audioSamplesTotal = snap.audioSamplesTotal;
                                ctx.setDetail(String("framesProcessed"), framesProcessed);
                                ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);
                                ctx.setDetail(String("audioSamplesTotal"), audioSamplesTotal);

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
                                        ctx.setFail(String("read pipeline error: ") + p.errorDetail);
                                        earlyExit = true;
                                }
                        }
                        delete insp;
                        if (earlyExit) return;

                        if (framesProcessed <= 0) {
                                ctx.setFail(String("inspector saw no frames"));
                                return;
                        }
                        if (audioSamplesTotal <= 0) {
                                ctx.setFail(String("inspector saw no decoded audio samples"));
                                return;
                        }
                        if (totalDiscontinuities != 0) {
                                ctx.setFail(String::number(totalDiscontinuities) +
                                            String(" discontinuities in QuickTime ") + codec.name() +
                                            String(" audio round-trip"));
                                return;
                        }
                        ctx.setPass();
                }

        } // namespace

        void registerQuickTimeAudioCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        String desc = String("QuickTime compressed-audio roundtrip (TPG → QuickTime[") +
                                      AudioCodec(c.codec).name() + String("] → decode → Inspector)");
                        TestRunner::registerCase(
                                TestCase(c.name, desc, [c](TestContext &ctx) { runQtAudioCase(c, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
