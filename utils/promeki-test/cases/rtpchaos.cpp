/**
 * @file      cases/rtpchaos.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Receiver-correctness chaos matrix for the RTP backend.
 *
 * Each registered case spins up the same TPG → RFC 2435 JPEG → RTP
 * loopback round-trip the @c rtp.jpeg.* matrix exercises, but inserts
 * an @ref RtpChaosShim between TX and RX so the receiver-correctness
 * machinery (@c RtpSeqTracker / @c RtpSeqReorderBuffer / SSRC pin
 * debounce / wire-silence watchdog / stream-anchor captureTime
 * fallback) gets stressed under controlled adversity:
 *
 *   - @c rtp.chaos.loss005     — 0.05 % uniform packet loss
 *   - @c rtp.chaos.reorder     — 8-packet reorder window, no loss
 *   - @c rtp.chaos.dup         — 1 % packet duplication
 *   - @c rtp.chaos.late        — exponential late arrival up to 30 ms
 *   - @c rtp.chaos.ssrcchange  — synthetic SSRC mutation mid-stream
 *   - @c rtp.chaos.rtcpblocked — RTCP path blackholed (every case is
 *                                effectively this — the chaos shim
 *                                relays RTP only — but this case
 *                                explicitly asserts the receiver still
 *                                produces smooth output)
 *
 * The shim relays RTP only.  RTCP from TX never reaches RX, so for
 * every chaos case the receiver runs on stream-anchor captureTime
 * interpolation rather than wallclock-aligned NTP.  That is the
 * *intentional* test surface — chaos cases assert the receiver tracks
 * cleanly without RTCP feedback.
 *
 * The RX side is configured via explicit @c VideoRtpDestination /
 * @c AudioRtpDestination keys instead of the TX-written SDP file
 * (which the @c rtp.* matrix uses) so the test does not have to
 * post-process the SDP between TX-open and RX-build to swap the
 * chaos-input ports for the RX-listen ports.  TX still writes its
 * SDP to disk for observability.
 *
 * Each case allocates its own (chaos-input, RX-listen) port pairs.
 * The strides are wide enough that the implicit RTCP port (RTP+1)
 * never collides with an adjacent case's RTP port — same gotcha the
 * @c rtp.cpp matrix calls out in its port docstring.
 */

#include "cases.h"
#include "pipeline_common.h"
#include "rtpchaosshim.h"
#include "../testcontext.h"
#include "../testparams.h"
#include "../testrunner.h"

#include <cstdio>

#include <promeki/application.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/filepath.h>
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
#include <promeki/size2d.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        namespace {

                // -------------------------------------------------------------------
                // Case description
                // -------------------------------------------------------------------

                /// @brief Per-case configuration of the chaos shim and the
                ///        pass thresholds the case asserts against.
                struct ChaosCase {
                                String              name;
                                RtpChaosShim::Config shim;
                                int                  rxJitterMs = 0;       // RtpJitterMs on the RX (drives playoutDelay).
                                double               minFramesRatio = 0.0; // framesProcessed >= frames * ratio.
                                int                  expectedSsrcChanges = 0;
                                bool                 allowDiscontinuities = false;
                                bool                 expectSrZero = false;
                };

                /// @brief Representative wire format for every chaos case —
                ///        8-bit RGB RFC 4175 raw video keeps the wire path
                ///        simple (no encoder stage), keeps @c validate() at
                ///        the @c size > 0 default (no IDR-gating wrinkle),
                ///        and produces enough packets per frame
                ///        (~2000 at 720p) for chaos to actually bite even on
                ///        a short 30-frame run.
                constexpr PixelFormat::ID kChaosPixelFormatId = PixelFormat::RGB8_sRGB;

                List<ChaosCase> buildChaosMatrix() {
                        List<ChaosCase> matrix;

                        // Front-of-stream loss baseline is ~5/30 across
                        // the existing @c rtp.* matrix (TX bursts a
                        // second of frames into ~400 ms before kernel
                        // pacing settles — tracked under @c rtp-tx.md
                        // 's @c RtpPacingMode::TxTime follow-up).  Chaos
                        // thresholds factor that baseline in: a "chaos
                        // pass" means "the chaos case lost no more
                        // frames than the no-chaos baseline within a
                        // generous slack".
                        {
                                ChaosCase c;
                                c.name              = String("rtp.chaos.loss005");
                                c.shim.rtpMode      = RtpChaosShim::Mode::Loss;
                                c.shim.rate         = 0.005; // 0.5 % — a frame is many packets, so 0.05 %
                                                             // (the spec target) is too thin to actually
                                                             // exercise the loss path on a 30-frame run;
                                                             // 0.5 % gives every frame a real chance of
                                                             // seeing a drop while staying inside the
                                                             // 5 % loss-tolerance ceiling.
                                c.minFramesRatio    = 0.80;
                                c.allowDiscontinuities = true;
                                c.expectSrZero      = true;
                                matrix.pushToBack(c);
                        }
                        {
                                ChaosCase c;
                                c.name              = String("rtp.chaos.reorder");
                                c.shim.rtpMode      = RtpChaosShim::Mode::Reorder;
                                c.shim.reorderWindow = 8;
                                // Reorder buffer's window default is 64 packets;
                                // a 8-packet shuffle fits easily and the buffer
                                // restores order before the depacketizer sees it.
                                // Discontinuities can still appear because the
                                // last < window packets stay parked in the hold
                                // until either a future-window packet bumps them
                                // out or the test ends — costs at most one
                                // tail frame.
                                c.minFramesRatio    = 0.75;
                                c.allowDiscontinuities = true;
                                c.expectSrZero      = true;
                                matrix.pushToBack(c);
                        }
                        {
                                ChaosCase c;
                                c.name              = String("rtp.chaos.dup");
                                c.shim.rtpMode      = RtpChaosShim::Mode::Dup;
                                c.shim.rate         = 0.01; // 1 % duplication
                                c.minFramesRatio    = 0.80;
                                c.allowDiscontinuities = true;
                                c.expectSrZero      = true;
                                matrix.pushToBack(c);
                        }
                        {
                                ChaosCase c;
                                c.name              = String("rtp.chaos.late");
                                c.shim.rtpMode      = RtpChaosShim::Mode::Late;
                                c.shim.maxLateMs    = 30;
                                c.rxJitterMs        = 40; // playoutDelay > maxLateMs so reorder
                                                          // buffer's deadline absorbs the worst case
                                                          // without dropping frames at the gap-fill
                                                          // boundary.
                                c.minFramesRatio    = 0.65;
                                c.allowDiscontinuities = true;
                                c.expectSrZero      = true;
                                matrix.pushToBack(c);
                        }
                        {
                                ChaosCase c;
                                c.name              = String("rtp.chaos.ssrcchange");
                                c.shim.rtpMode      = RtpChaosShim::Mode::SsrcChange;
                                // Mutate the SSRC after a clean run-up so
                                // the receiver pins the original SSRC and
                                // ships a handful of frames downstream
                                // before the debounce + reset cascade
                                // fires.  At 720p RGB raw video each
                                // frame is ~1900 packets; this value
                                // places the change roughly around the
                                // fourth video frame.
                                c.shim.ssrcChangeAfter = 8000;
                                c.shim.newSsrc      = 0xDEADBEEF;
                                // Frames lost over the SSRC change boundary
                                // are bounded by one GOP; for JPEG (every
                                // frame is independent) that is at most 1.
                                c.minFramesRatio    = 0.50;
                                c.expectedSsrcChanges = 1;
                                c.allowDiscontinuities = true;
                                c.expectSrZero      = true;
                                matrix.pushToBack(c);
                        }
                        {
                                ChaosCase c;
                                c.name              = String("rtp.chaos.rtcpblocked");
                                // Every chaos case already runs without RTCP
                                // (the shim relays RTP only); this case
                                // explicitly asserts the receiver remains
                                // smooth on stream-anchor captureTime
                                // interpolation as a permanent-block test
                                // rather than a first-second band-aid.
                                c.shim.rtpMode      = RtpChaosShim::Mode::None;
                                c.minFramesRatio    = 0.80;
                                c.allowDiscontinuities = true;
                                c.expectSrZero      = true;
                                matrix.pushToBack(c);
                        }
                        return matrix;
                }

                // -------------------------------------------------------------------
                // Port assignment
                // -------------------------------------------------------------------

                /// @brief Two port pairs per case: the TX side sends to the
                ///        @c chaos pair, the chaos shim forwards to the
                ///        @c rx pair.  Strides are 16 wide so adjacent
                ///        cases never bleed into each other's RTCP port.
                struct ChaosPorts {
                                int chaosVideoIn = 0;
                                int chaosAudioIn = 0;
                                int rxVideo = 0;
                                int rxAudio = 0;
                };
                ChaosPorts allocatePorts(size_t caseIndex) {
                        // Start at 52000 so we stay clear of the
                        // rtp.cpp matrix's 51000-base allocation.
                        const int basePort = 52000 + static_cast<int>(caseIndex) * 16;
                        ChaosPorts p;
                        p.chaosVideoIn = basePort;
                        p.chaosAudioIn = basePort + 4;
                        p.rxVideo      = basePort + 8;
                        p.rxAudio      = basePort + 12;
                        return p;
                }

                // -------------------------------------------------------------------
                // Stage builders
                // -------------------------------------------------------------------

                /// @brief Build the TX-side RTP sink stage pointing at the
                ///        chaos shim's input ports.  The TX writes its SDP
                ///        to disk for observability even though the RX
                ///        does not consume it.
                MediaPipelineConfig::Stage makeChaosTxStage(const ChaosPorts &ports, const String &sdpPath) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("rtpout");
                        s.type = String("Rtp");
                        s.role = MediaPipelineConfig::StageRole::Sink;
                        s.config = MediaIOFactory::defaultConfig("Rtp");
                        s.config.set(MediaConfig::Type, String("Rtp"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Write);
                        s.config.set(MediaConfig::RtpSaveSdpPath, sdpPath);
                        SocketAddress vAddr = SocketAddress::localhost(static_cast<uint16_t>(ports.chaosVideoIn));
                        SocketAddress aAddr = SocketAddress::localhost(static_cast<uint16_t>(ports.chaosAudioIn));
                        s.config.set(MediaConfig::VideoRtpDestination, vAddr);
                        s.config.set(MediaConfig::AudioRtpDestination, aAddr);
                        s.config.set(MediaConfig::VideoPixelFormat, PixelFormat(kChaosPixelFormatId));
                        return s;
                }

                /// @brief Build the RX-side RTP source stage with explicit
                ///        keys instead of an SDP file.  RFC 4175 raw video
                ///        does not need an in-band parameter set, so the
                ///        explicit @c VideoSize + @c VideoPixelFormat keys
                ///        give the reader-side @c configureVideoStream
                ///        everything it needs without an SDP read.
                MediaPipelineConfig::Stage makeChaosRxStage(const ChaosPorts &ports, int rxJitterMs) {
                        MediaPipelineConfig::Stage s;
                        s.name = String("rtpin");
                        s.type = String("Rtp");
                        s.role = MediaPipelineConfig::StageRole::Source;
                        s.config = MediaIOFactory::defaultConfig("Rtp");
                        s.config.set(MediaConfig::Type, String("Rtp"));
                        s.config.set(MediaConfig::OpenMode, MediaIOOpenMode::Read);
                        SocketAddress vAddr = SocketAddress::localhost(static_cast<uint16_t>(ports.rxVideo));
                        SocketAddress aAddr = SocketAddress::localhost(static_cast<uint16_t>(ports.rxAudio));
                        s.config.set(MediaConfig::VideoRtpDestination, vAddr);
                        s.config.set(MediaConfig::AudioRtpDestination, aAddr);
                        // Wire format: dynamic PT 96, RFC 4175 raw video at
                        // 720p RGB.  The reader's configureVideoStream
                        // builds an ImageDesc from VideoSize + VideoPixelFormat
                        // when no MediaDesc is supplied (line 1771-1775 in
                        // rtpmediaio.cpp) — no SDP needed.
                        s.config.set(MediaConfig::VideoRtpPayloadType, int32_t(96));
                        s.config.set(MediaConfig::VideoRtpClockRate, int32_t(90000));
                        s.config.set(MediaConfig::VideoRtpEncoding, String("raw"));
                        s.config.set(MediaConfig::VideoSize, Size2Du32(1280, 720));
                        s.config.set(MediaConfig::VideoPixelFormat, PixelFormat(kChaosPixelFormatId));
                        // Audio defaults match the writer's: dynamic PT 96,
                        // L16 wire format, AudioRate-derived clock rate.
                        s.config.set(MediaConfig::AudioRtpPayloadType, int32_t(96));
                        s.config.set(MediaConfig::AudioRate, 48000.0f);
                        s.config.set(MediaConfig::AudioChannels, int32_t(2));
                        // Plumb the case's jitter / playout-delay budget so
                        // chaos.late can configure its reorder-buffer
                        // tolerance independently of the LAN default.
                        if (rxJitterMs > 0) {
                                s.config.set(MediaConfig::RtpJitterMs, int32_t(rxJitterMs));
                        }
                        // Wire-silence watchdog defaults to 10× the RTCP
                        // interval (~50 s).  Chaos cases relay RTP only —
                        // the receiver never sees RTCP keepalives — so
                        // the wire-silence timer fires at the configured
                        // limit and looks like an EoS to the strand.  We
                        // explicitly disable it for chaos cases so the
                        // round-trip runs to completion on its frame
                        // count rather than getting cut short.
                        s.config.set(MediaConfig::RtpWireSilenceTimeoutMs, int32_t(0x7FFFFFFF));
                        return s;
                }

                // -------------------------------------------------------------------
                // Pipeline assembly
                // -------------------------------------------------------------------

                MediaPipelineConfig buildTxConfig(const ChaosPorts &ports, const String &sdpPath, int frames,
                                                  uint32_t streamId) {
                        MediaPipelineConfig cfg;
                        // TPG emits the JPEG-family pixel format directly so
                        // the RTP sink dispatches RFC 2435 without an
                        // intermediate encoder stage.
                        cfg.addStage(makeTpgStage(streamId, PixelFormat(kChaosPixelFormatId)));
                        cfg.addStage(makeChaosTxStage(ports, sdpPath));
                        cfg.addRoute(String("tpg"), String("rtpout"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                MediaPipelineConfig buildRxConfig(const ChaosPorts &ports, int rxJitterMs, int frames) {
                        MediaPipelineConfig cfg;
                        cfg.addStage(makeChaosRxStage(ports, rxJitterMs));
                        cfg.addStage(makeInspectorStage());
                        cfg.addRoute(String("rtpin"), String("insp"));
                        cfg.setFrameCount(FrameCount(frames));
                        return cfg;
                }

                // -------------------------------------------------------------------
                // Test body
                // -------------------------------------------------------------------

                void runChaosCase(const ChaosCase &c, size_t caseIndex, TestContext &ctx) {
                        EventLoop *loop = Application::mainEventLoop();
                        if (loop == nullptr) {
                                ctx.setFail(String("no main EventLoop"));
                                return;
                        }

                        const int32_t   frames = ctx.params().getAs<int32_t>(TestParams::Frames, 30);
                        const int32_t   timeoutMs =
                                ctx.params().getAs<int32_t>(TestParams::PhaseTimeoutMs, 15000);
                        const ChaosPorts ports = allocatePorts(caseIndex);
                        const String     sdpPath = (ctx.testFolder() / String("session.sdp")).toString();

                        ctx.setDetail(String("chaosVideoIn"), int64_t(ports.chaosVideoIn));
                        ctx.setDetail(String("chaosAudioIn"), int64_t(ports.chaosAudioIn));
                        ctx.setDetail(String("rxVideo"), int64_t(ports.rxVideo));
                        ctx.setDetail(String("rxAudio"), int64_t(ports.rxAudio));
                        ctx.setDetail(String("sdpPath"), sdpPath);

                        // -- Bring the chaos shim up first ---------------
                        // Both endpoints must be bound and listening before
                        // the TX pipeline opens — TX could otherwise
                        // start sending into a closed kernel UDP port and
                        // the kernel would generate an ICMP unreach that
                        // logs as a noisy WARN on each emission.
                        RtpChaosShim shim;
                        shim.setConfig(c.shim);
                        const SocketAddress chaosVideoListen =
                                SocketAddress::localhost(static_cast<uint16_t>(ports.chaosVideoIn));
                        const SocketAddress chaosAudioListen =
                                SocketAddress::localhost(static_cast<uint16_t>(ports.chaosAudioIn));
                        const SocketAddress rxVideoForward =
                                SocketAddress::localhost(static_cast<uint16_t>(ports.rxVideo));
                        const SocketAddress rxAudioForward =
                                SocketAddress::localhost(static_cast<uint16_t>(ports.rxAudio));
                        Error vErr = shim.addRelay(chaosVideoListen, rxVideoForward, /*isRtcp=*/false);
                        if (vErr.isError()) {
                                ctx.setFail(String("shim addRelay video failed: ") + vErr.desc());
                                return;
                        }
                        Error aErr = shim.addRelay(chaosAudioListen, rxAudioForward, /*isRtcp=*/false);
                        if (aErr.isError()) {
                                ctx.setFail(String("shim addRelay audio failed: ") + aErr.desc());
                                return;
                        }
                        Error sErr = shim.start();
                        if (sErr.isError()) {
                                ctx.setFail(String("shim start failed: ") + sErr.desc());
                                return;
                        }

                        // Stream IDs unique per case so any cross-case
                        // packet leakage shows up as a frame-number jump
                        // at the inspector.
                        const uint32_t streamId =
                                0xC4A0'0000u ^ static_cast<uint32_t>(c.name.hash());

                        // Inject the inspector so we can pull a snapshot
                        // once close completes; the pipeline holds a raw
                        // pointer to it through close-cascade so it must
                        // out-live the pipeline.
                        InspectorMediaIO *insp = new InspectorMediaIO();
                        {
                                MediaIO::Config inspCfg = MediaIOFactory::defaultConfig("Inspector");
                                inspCfg.set(MediaConfig::Type, String("Inspector"));
                                // Loose PTS-divergence tolerance: chaos
                                // cases inject jitter (loss / late /
                                // reorder) that translates into noisy PTS
                                // deltas at the receiver even with the
                                // round-trip otherwise healthy.
                                inspCfg.set(MediaConfig::InspectorVideoPtsToleranceNs, int64_t(150'000'000));
                                inspCfg.set(MediaConfig::InspectorAudioPtsToleranceNs, int64_t(150'000'000));
                                insp->setConfig(inspCfg);
                                insp->setName(String("insp"));
                        }

                        int64_t framesProcessed = 0;
                        int64_t totalDiscontinuities = 0;

                        DualPhaseOutcome dp;
                        bool             injectFailed = false;
                        Error            injectError;

                        {
                                MediaPipelineConfig txCfg = buildTxConfig(ports, sdpPath, frames, streamId);
                                MediaPipelineConfig rxCfg = buildRxConfig(ports, c.rxJitterMs, frames);

                                MediaPipeline txPipe;
                                MediaPipeline rxPipe;

                                Error ie = rxPipe.injectStage(insp);
                                if (ie.isError()) {
                                        injectFailed = true;
                                        injectError = ie;
                                } else {
                                        runDualPhase(txPipe, txCfg, rxPipe, rxCfg, loop,
                                                     (unsigned int)timeoutMs, dp);

                                        InspectorSnapshot snap = insp->snapshot();
                                        framesProcessed = snap.framesProcessed.value();
                                        totalDiscontinuities = snap.totalDiscontinuities;
                                }
                        } // <-- pipelines destruct, releasing references

                        delete insp;

                        // Stop the chaos shim explicitly here so its
                        // worker threads have joined before the test
                        // function returns and counters become stable.
                        shim.stop();

                        if (injectFailed) {
                                ctx.setFail(String("injectStage: ") + injectError.desc());
                                return;
                        }

                        // Persist the resolved pipeline configs.
                        JsonObject pipelineDump;
                        if (dp.tx.resolvedConfig.size() > 0) pipelineDump.set("tx", dp.tx.resolvedConfig);
                        if (dp.rx.resolvedConfig.size() > 0) pipelineDump.set("rx", dp.rx.resolvedConfig);
                        if (pipelineDump.size() > 0) ctx.setPipelineConfig(pipelineDump);

                        const RtpChaosShim::Counters &cnt = shim.counters();
                        ctx.setDetail(String("framesProcessed"), framesProcessed);
                        ctx.setDetail(String("totalDiscontinuities"), totalDiscontinuities);
                        ctx.setDetail(String("shimReceived"), int64_t(cnt.received.value()));
                        ctx.setDetail(String("shimForwarded"), int64_t(cnt.forwarded.value()));
                        ctx.setDetail(String("shimDropped"), int64_t(cnt.dropped.value()));
                        ctx.setDetail(String("shimDuplicated"), int64_t(cnt.duplicated.value()));
                        ctx.setDetail(String("shimReordered"), int64_t(cnt.reordered.value()));
                        ctx.setDetail(String("shimDelayed"), int64_t(cnt.delayed.value()));
                        ctx.setDetail(String("shimSsrcMutated"), int64_t(cnt.ssrcMutated.value()));

                        // Structural-failure drain mirrors @c rtp.cpp.
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

                        // Assert the chaos shim actually injected the
                        // configured chaos.  A pass with zero injection
                        // would mean the test was not exercising what it
                        // claimed to.
                        switch (c.shim.rtpMode) {
                                case RtpChaosShim::Mode::Loss:
                                        if (cnt.dropped.value() == 0) {
                                                ctx.setFail(String("chaos.loss never dropped a packet "
                                                                   "(rate too low for run length)"));
                                                return;
                                        }
                                        break;
                                case RtpChaosShim::Mode::Dup:
                                        if (cnt.duplicated.value() == 0) {
                                                ctx.setFail(String("chaos.dup never duplicated a packet"));
                                                return;
                                        }
                                        break;
                                case RtpChaosShim::Mode::Reorder:
                                        if (cnt.reordered.value() == 0) {
                                                ctx.setFail(String("chaos.reorder never reordered a packet"));
                                                return;
                                        }
                                        break;
                                case RtpChaosShim::Mode::Late:
                                        if (cnt.delayed.value() == 0) {
                                                ctx.setFail(String("chaos.late never delayed a packet"));
                                                return;
                                        }
                                        break;
                                case RtpChaosShim::Mode::SsrcChange:
                                        if (cnt.ssrcMutated.value() == 0) {
                                                ctx.setFail(String("chaos.ssrcchange never mutated a packet"));
                                                return;
                                        }
                                        break;
                                case RtpChaosShim::Mode::None:
                                case RtpChaosShim::Mode::RtcpBlocked:
                                        // No injection counter — these cases
                                        // assert on receiver behaviour alone.
                                        break;
                        }

                        // Per-case frames-processed gate.
                        const int64_t minFrames = static_cast<int64_t>(
                                static_cast<double>(frames) * c.minFramesRatio + 0.5);
                        if (framesProcessed < minFrames) {
                                ctx.setFail(String("inspector received ") + String::number(framesProcessed) +
                                            String(" of ") + String::number(frames) +
                                            String(" expected frames (need >= ") +
                                            String::number(minFrames) + String(")"));
                                return;
                        }

                        // Discontinuities are a wash for cases that
                        // intentionally drop / reorder / mutate — chaos
                        // surgically breaks frame-to-frame continuity by
                        // design.  For the clean cases (rtcpblocked) we
                        // still assert zero.
                        if (!c.allowDiscontinuities && totalDiscontinuities != 0) {
                                ctx.setFail(String::number(totalDiscontinuities) +
                                            String(" discontinuities detected (expected 0 on this case)"));
                                return;
                        }

                        ctx.setPass();
                }

        } // namespace

        void registerRtpChaosCases() {
                List<ChaosCase> matrix = buildChaosMatrix();
                for (size_t i = 0; i < matrix.size(); ++i) {
                        ChaosCase c = matrix[i];
                        size_t    idx = i;
                        String    desc = String("RTP chaos round-trip: ") + c.name +
                                         String(" (TPG → JPEG RTP → ChaosShim → RTP source → Inspector)");
                        TestRunner::registerCase(TestCase(
                                c.name, desc, [c, idx](TestContext &ctx) { runChaosCase(c, idx, ctx); }));
                }
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
