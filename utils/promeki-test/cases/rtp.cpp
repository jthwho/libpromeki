/**
 * @file      cases/rtp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * RTP transport roundtrip tests.
 *
 * Each registered case spins up two pipelines on the main event
 * loop:
 *
 *   TX:  TPG → RtpMediaIO (sink, SDP written to file)
 *   RX:  RtpMediaIO (source, configured via that SDP) → Inspector
 *
 * The TX side writes the @ref MediaConfig::RtpSaveSdpPath file on
 * @c open and the RX side consumes that file via
 * @ref MediaConfig::RtpSdp.  Driving the receiver from the on-disk
 * SDP is deliberate — it exercises the real interop path that an
 * external receiver (ffplay, GStreamer, ST 2110 hardware) would use,
 * so a regression in either the SDP writer or the SDP parser shows
 * up as a test failure rather than slipping through a back-channel.
 *
 * Both pipelines run on @c 127.0.0.1 with per-case unique even ports
 * for video / audio so successive cases don't collide.  The Inspector
 * sits at the end of the RX side and applies its full default check
 * set — picture-data continuity, audio data, A/V sync, frame-stamp
 * timestamps — so a broken depacketizer or a dropped audio packet
 * surfaces as a discontinuity, not just a missing frame.
 *
 * The matrix today covers the wire formats RtpMediaIO actually
 * implements: MJPEG (RFC 2435) variants and 8-bit interleaved
 * uncompressed (RFC 4175).  10/12-bit uncompressed is excluded
 * pending the ST 2110-20 pgroup work the backend's docstring calls
 * out as deferred.  Audio is L16 (RFC 3551 / AES67) on every case.
 *
 * Skip vs. fail policy:
 *
 *   - Build / open / start failures classify as Skip — the most
 *     common cause is "this build doesn't have the payload class
 *     wired up" (e.g. a future raw-10-bit case before pgroup support
 *     lands), and we don't want those false-failing the matrix.
 *   - Mid-run pipeline-error signals, frames-processed below the
 *     expected count, and discontinuity-count overruns are Fail.
 *   - The combined watchdog firing surfaces as Timeout on the side
 *     that hadn't yet closed — usually the RX side, which will sit
 *     on @c recvfrom forever if the TX never sent anything.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <promeki/application.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/filepath.h>
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
#include <promeki/socketaddress.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // -------------------------------------------------------------------
                // Case description
                // -------------------------------------------------------------------

                struct Case {
                                String      name;        // dotted identifier registered with TestRunner
                                String      family;      // "jpeg" / "raw" — used to label the case sub-group
                                PixelFormat pixelFormat; // wire format on the RTP path (compressed for JPEG, uncompressed for raw)
                                VideoCodec  codec;       // valid for compressed paths only — drives an encoder stage on TX and a decoder stage on RX
                                bool        audio = true;
                };

                // Hand-curated matrix.  Each entry maps to a real wire
                // format the RTP backend ships today.  The JPEG cases
                // run TPG → VideoEncoder(JPEG, sub-format) → RTP sink;
                // the RTP sink dispatches RFC 2435 because its input
                // arrives in a JPEG-family @ref PixelFormat.  The raw
                // cases skip the encoder — RtpMediaIO dispatches RFC
                // 4175 directly when the input pixel format is an
                // uncompressed 8-bit interleaved family member.
                List<Case> buildMatrix() {
                        List<Case>       matrix;
                        const VideoCodec jpeg(VideoCodec::JPEG);

                        // JPEG (RFC 2435).  Two YUV422 variants exercise the two
                        // matrices the JPEG family commonly carries; the
                        // YUV420 entry catches a 4:2:0 packing path that
                        // the others don't.
                        matrix.pushToBack({String("rtp.jpeg.yuv8_422_rec709"), String("jpeg"),
                                           PixelFormat(PixelFormat::JPEG_YUV8_422_Rec709), jpeg, true});
                        matrix.pushToBack({String("rtp.jpeg.yuv8_420_rec709"), String("jpeg"),
                                           PixelFormat(PixelFormat::JPEG_YUV8_420_Rec709), jpeg, true});
                        matrix.pushToBack({String("rtp.jpeg.yuv8_422_rec601_full"), String("jpeg"),
                                           PixelFormat(PixelFormat::JPEG_YUV8_422_Rec601_Full), jpeg, true});

                        // RFC 4175 uncompressed.  8-bit interleaved is
                        // what the backend currently supports — see the
                        // @ref RtpMediaIO docstring for why 10/12-bit
                        // is gated on ST 2110-20 pgroup support.
                        matrix.pushToBack({String("rtp.raw.rgb8_srgb"), String("raw"),
                                           PixelFormat(PixelFormat::RGB8_sRGB), VideoCodec(), true});
                        matrix.pushToBack({String("rtp.raw.yuv8_422_rec709"), String("raw"),
                                           PixelFormat(PixelFormat::YUV8_422_Rec709), VideoCodec(), true});
                        return matrix;
                }

                // -------------------------------------------------------------------
                // Port assignment
                // -------------------------------------------------------------------

                // Each case gets its own (video, audio) port pair so
                // the kernel doesn't have to recycle a freshly closed
                // socket between back-to-back cases.  The video port
                // and audio port are kept ≥ 4 apart so the implicit
                // RTCP port (RTP+1) on each stream never collides with
                // the next stream's RTP port (a classic gotcha covered
                // in @ref RtpMediaIO 's port docstring).  We start at
                // 51000 to stay well clear of any well-known service.
                struct PortPair {
                                int video;
                                int audio;
                };
                PortPair allocatePorts(size_t caseIndex) {
                        // Stride of 8 leaves room for video RTP/RTCP + audio
                        // RTP/RTCP + a 4-port gap before the next case.
                        const int basePort = 51000 + static_cast<int>(caseIndex) * 8;
                        PortPair  p;
                        p.video = basePort;
                        p.audio = basePort + 4;
                        return p;
                }

                // -------------------------------------------------------------------
                // Stage helpers
                // -------------------------------------------------------------------

                // RtpMediaIO is bidirectional — the same backend can
                // be a sink or a source depending on @c MediaConfig::OpenMode.
                // The pipeline stage instantiator at @c MediaPipeline::instantiateStage
                // does not auto-derive @c OpenMode from @ref MediaPipelineConfig::StageRole
                // when the stage carries an explicit @c type, so we have
                // to stamp it ourselves here.

                MediaPipelineConfig::Stage makeRtpSinkStage(const PortPair &ports, const PixelFormat &pd,
                                                            const String &sdpPath) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("rtpout");
                        s.type = String("Rtp");
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        s.config = MediaIOFactory::defaultConfig("Rtp");
                        s.config.set(MediaConfig::Type, String("Rtp"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Write);
                        s.config.set(MediaConfig::RtpSaveSdpPath, sdpPath);
                        // Loopback addresses; the RX side consumes the SDP we
                        // just wrote so it picks up these addresses without
                        // any explicit mirroring on its end — that's the
                        // test's whole point.
                        auto vAddr = SocketAddress::fromString(String("127.0.0.1:") +
                                                               String::number(ports.video));
                        auto aAddr = SocketAddress::fromString(String("127.0.0.1:") +
                                                               String::number(ports.audio));
                        s.config.set(MediaConfig::VideoRtpDestination, vAddr.first());
                        s.config.set(MediaConfig::AudioRtpDestination, aAddr.first());
                        // Pin the encoded video format so the
                        // RFC 2435 / RFC 4175 dispatch matches the matrix
                        // entry exactly.  The planner inserts a CSC stage
                        // ahead of the sink if TPG's output doesn't
                        // already match.
                        if (pd.isValid()) {
                                s.config.set(MediaConfig::VideoPixelFormat, pd);
                        }
                        return s;
                }

                MediaPipelineConfig::Stage makeRtpSourceStage(const String &sdpPath) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("rtpin");
                        s.type = String("Rtp");
                        s.role = MediaPipelineConfig::StageRole::Source;
                        s.config = MediaIOFactory::defaultConfig("Rtp");
                        s.config.set(MediaConfig::Type, String("Rtp"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Read);
                        // The whole point of these tests: the RX side
                        // learns destinations + payload types + clock
                        // rates exclusively from the SDP file the TX
                        // side just wrote on disk.  Don't echo any
                        // *RtpDestination keys here — they would
                        // override the SDP-discovered values and
                        // bypass the SDP parsing path.
                        s.config.set(MediaConfig::RtpSdp, sdpPath);
                        return s;
                }

                // -------------------------------------------------------------------
                // Pipeline assembly
                // -------------------------------------------------------------------

                MediaPipelineConfig buildTxConfig(const Case &c, const PortPair &ports, const String &sdpPath,
                                                  int frames, uint32_t streamId) {
                        MediaPipelineConfig cfg;

                        // Raw cases let TPG emit the matrix's pixel
                        // format directly so the RTP sink dispatches
                        // RFC 4175 raw on its own.  JPEG cases keep
                        // TPG on the backend default and route through
                        // a VideoEncoder stage that compresses to the
                        // matrix's compressed sub-format — the RTP
                        // sink then dispatches RFC 2435 because its
                        // input arrives in a JPEG-family
                        // @ref PixelFormat.
                        const PixelFormat tpgPd = c.codec.isValid() ? PixelFormat() : c.pixelFormat;
                        cfg.addStage(makeTpgStage(streamId, tpgPd));
                        String prev = String("tpg");
                        if (c.codec.isValid()) {
                                cfg.addStage(makeEncoderStage(c.codec, c.pixelFormat));
                                cfg.addRoute(prev, String("enc"));
                                prev = String("enc");
                        }
                        cfg.addStage(makeRtpSinkStage(ports, c.pixelFormat, sdpPath));
                        cfg.addRoute(prev, String("rtpout"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                MediaPipelineConfig buildRxConfig(const Case &c, const String &sdpPath, int frames) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeRtpSourceStage(sdpPath));
                        String prev = String("rtpin");
                        if (c.codec.isValid()) {
                                cfg.addStage(makeDecoderStage(c.codec));
                                cfg.addRoute(prev, String("dec"));
                                prev = String("dec");
                        }
                        cfg.addStage(makeInspectorStage());
                        cfg.addRoute(prev, String("insp"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                // -------------------------------------------------------------------
                // Test body
                // -------------------------------------------------------------------

                void runRtpCase(const Case &c, size_t caseIndex, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t  frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t  timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 10000);
                        const PortPair ports = allocatePorts(caseIndex);
                        const String   sdpPath = (ctx.testFolder() / String("session.sdp")).toString();

                        ctx.setDetail(String("family"), c.family);
                        ctx.setDetail(String("pixelFormat"), c.pixelFormat.name());
                        ctx.setDetail(String("videoPort"), int64_t(ports.video));
                        ctx.setDetail(String("audioPort"), int64_t(ports.audio));
                        ctx.setDetail(String("sdpPath"), sdpPath);

                        // Stream IDs unique per case so any cross-case
                        // packet leakage shows up as a frame-number jump
                        // at the inspector rather than silently flowing
                        // through.
                        const uint32_t streamId =
                                0xA770'0000u ^ static_cast<uint32_t>(std::hash<std::string>{}(c.name.str()));

                        // Inject the inspector so we can pull a
                        // snapshot once the close cascade completes.
                        // Lifetime is tricky: the pipeline does not own
                        // injected stages, but it DOES still hold raw
                        // pointers to them through close-cascade — so
                        // the inspector must out-live the pipeline.  We
                        // use a heap allocation and gate the delete on
                        // exiting the pipeline scope so a watchdog that
                        // tears down a still-active pipeline doesn't
                        // race a user-after-free.
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

                        // Outcome captured outside the pipeline scope so
                        // we can evaluate pass / fail after the pipelines
                        // have safely destroyed themselves.
                        DualPhaseOutcome dp;
                        bool             injectFailed = false;
                        Error            injectError;

                        {
                                MediaPipelineConfig txCfg = buildTxConfig(c, ports, sdpPath, frames, streamId);
                                MediaPipelineConfig rxCfg = buildRxConfig(c, sdpPath, frames);

                                MediaPipeline txPipe;
                                MediaPipeline rxPipe;

                                Error ie = rxPipe.injectStage(insp);
                                if (ie.isError()) {
                                        injectFailed = true;
                                        injectError = ie;
                                } else {
                                        runDualPhase(txPipe, txCfg, rxPipe, rxCfg, loop,
                                                     (unsigned int)timeoutMs, dp);

                                        // Take the inspector snapshot
                                        // BEFORE the pipelines run their
                                        // destructors — once close has
                                        // completed (or the watchdog has
                                        // bailed) the counters are stable
                                        // and the pipeline is about to
                                        // detach from the injected stage.
                                        InspectorSnapshot snap = insp->snapshot();
                                        framesProcessed = snap.framesProcessed.value();
                                        framesWithPictureData = snap.framesWithPictureData.value();
                                        framesWithAudioTimestamp = snap.framesWithAudioTimestamp.value();
                                        totalDiscontinuities = snap.totalDiscontinuities;
                                }
                        } // <-- txPipe, rxPipe destruct here, releasing
                          //     any references to insp

                        delete insp;

                        if (injectFailed) {
                                ctx.setFail(String("injectStage: ") + injectError.desc());
                                return;
                        }

                        // Persist BOTH pipeline configs under a single
                        // object so result.json carries a complete
                        // picture: tx + rx graphs after autoplan,
                        // including any planner-injected CSC /
                        // FrameBridge stages.
                        JsonObject pipelineDump;
                        if (dp.tx.resolvedConfig.size() > 0) pipelineDump.set("tx", dp.tx.resolvedConfig);
                        if (dp.rx.resolvedConfig.size() > 0) pipelineDump.set("rx", dp.rx.resolvedConfig);
                        if (pipelineDump.size() > 0) ctx.setPipelineConfig(pipelineDump);

                        ctx.setDetail(String("framesProcessed"), framesProcessed);
                        ctx.setDetail(String("framesWithPictureData"), framesWithPictureData);
                        ctx.setDetail(String("framesWithAudioTimestamp"), framesWithAudioTimestamp);
                        ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);

                        // Translate phase-level errors into a pass /
                        // skip / fail / timeout result.  RTP build /
                        // open / start failures are overwhelmingly
                        // "this payload class isn't wired up" (e.g.
                        // raw 10/12-bit before pgroup support lands)
                        // so they classify as Skip — Fail is reserved
                        // for "the path SAID it works but the
                        // round-trip lost frames or corrupted them".
                        // Drain the deterministic, structural failures
                        // first so they get reported with the right
                        // classification before the watchdog-driven
                        // outcomes (which can mask packet loss as a
                        // generic "TIME").
                        //
                        // The dual-phase runner aborts the sequence as
                        // soon as one side fails, so the OTHER side's
                        // un-attempted steps land with default-Ok
                        // errors.  Skip a step here if its error is Ok
                        // — a real failure on this side will populate
                        // the corresponding error field, and a step
                        // that was simply never reached has nothing
                        // to report.
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
                                        ctx.setFail(String(side) + String(" pipeline error: ") +
                                                    p.errorDetail);
                                        return true;
                                }
                                return false;
                        };
                        if (structuralPhase(dp.tx, "tx")) return;
                        if (structuralPhase(dp.rx, "rx")) return;

                        // A watchdog firing with zero frames received
                        // is a genuine deadlock — TX never emitted, or
                        // RX never bound — and surfaces as Timeout.
                        // Anything else (some frames received, just
                        // not all) is more useful as a content-level
                        // Fail with the actual count.
                        const bool watchdogFired = dp.tx.timedOut || dp.rx.timedOut;
                        if (watchdogFired && framesProcessed == 0) {
                                ctx.setTimeout(String("watchdog fired with no frames received past ") +
                                               String::number(timeoutMs) + String(" ms"));
                                return;
                        }

                        if (framesProcessed <= 0) {
                                ctx.setFail(String("inspector saw no frames over RTP"));
                                return;
                        }
                        // The RX side caps at @c frames; UDP loopback
                        // can drop the very first packet or two if a
                        // start-time race wins, so we tolerate up to a
                        // 1-frame deficit before flagging a fail.  The
                        // discontinuity counter still fires on any
                        // out-of-order or duplicated frame, so a
                        // tolerance here doesn't blunt the test's
                        // fault-detection.
                        if (framesProcessed + 1 < frames) {
                                ctx.setFail(String("inspector received ") + String::number(framesProcessed) +
                                            String(" of ") + String::number(frames) +
                                            String(" expected frames"));
                                return;
                        }
                        if (totalDiscontinuities != 0) {
                                ctx.setFail(String::number(totalDiscontinuities) +
                                            String(" discontinuities detected in RTP round-trip"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerRtpCases() {
                List<Case> matrix = buildMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        Case   c = matrix[i];
                        size_t idx = i;
                        String desc = String("RTP roundtrip: ") + c.pixelFormat.name() +
                                      String(" (TPG → RTP sink → SDP file → RTP source → Inspector)");
                        TestRunner::registerCase(TestCase(
                                c.name, desc, [c, idx](TestContext &ctx) { runRtpCase(c, idx, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
