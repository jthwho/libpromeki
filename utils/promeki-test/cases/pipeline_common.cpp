/**
 * @file      cases/pipeline_common.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "pipeline_common.h"

#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiosource.h>
#include <promeki/objectbase.tpp>
#include <promeki/stringlist.h>
#include <promeki/thread.h>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        MediaPipelineConfig::Stage makeTpgStage(uint32_t streamId, const PixelFormat &tpgPixelFormat,
                                                bool videoEnabled, bool audioEnabled) {
                MediaPipelineConfig::Stage s;
                s.name = String("tpg");
                s.type = String("TPG");
                s.role = MediaPipelineConfig::StageRole::Source;
                s.config = MediaIOFactory::defaultConfig("TPG");
                s.config.set(MediaConfig::Type, String("TPG"));
                s.config.set(MediaConfig::VideoFormat, VideoFormat(kDefaultVideoFormat));
                s.config.set(MediaConfig::VideoEnabled, videoEnabled);
                s.config.set(MediaConfig::AudioEnabled, audioEnabled);
                s.config.set(MediaConfig::TimecodeEnabled, true);
                s.config.set(MediaConfig::TpgDataEncoderEnabled, videoEnabled);
                s.config.set(MediaConfig::StreamID, streamId);
                if (videoEnabled && tpgPixelFormat.isValid() && !tpgPixelFormat.isCompressed()) {
                        s.config.set(MediaConfig::VideoPixelFormat, tpgPixelFormat);
                }
                return s;
        }

        MediaPipelineConfig::Stage makeEncoderStage(const VideoCodec &codec, const PixelFormat &variant) {
                MediaPipelineConfig::Stage s;
                s.name = String("enc");
                s.type = String("VideoEncoder");
                s.role = MediaPipelineConfig::StageRole::Transform;
                s.config = MediaIOFactory::defaultConfig("VideoEncoder");
                s.config.set(MediaConfig::Type, String("VideoEncoder"));
                s.config.set(MediaConfig::VideoCodec, codec);
                // 50 Mbit/s keeps lossy codecs (H.264, HEVC) from
                // smearing the picture-data band into unreadable grey
                // at the test's 720p raster; the legacy
                // roundtrip-functest landed on the same number.
                s.config.set(MediaConfig::BitrateKbps, int32_t(50000));
                // All-intra so every frame is self-contained and the
                // round-trip pairing between write and read stays 1:1.
                s.config.set(MediaConfig::GopLength, int32_t(1));
                if (variant.isValid() && variant.isCompressed()) {
                        s.config.set(MediaConfig::VideoPixelFormat, variant);
                }
                return s;
        }

        MediaPipelineConfig::Stage makeDecoderStage(const VideoCodec &codec) {
                MediaPipelineConfig::Stage s;
                s.name = String("dec");
                s.type = String("VideoDecoder");
                s.role = MediaPipelineConfig::StageRole::Transform;
                s.config = MediaIOFactory::defaultConfig("VideoDecoder");
                s.config.set(MediaConfig::Type, String("VideoDecoder"));
                s.config.set(MediaConfig::VideoCodec, codec);
                return s;
        }

        MediaPipelineConfig::Stage makeInspectorStage() {
                MediaPipelineConfig::Stage s;
                s.name = String("insp");
                s.type = String("Inspector");
                s.role = MediaPipelineConfig::StageRole::Sink;
                s.config = MediaIOFactory::defaultConfig("Inspector");
                s.config.set(MediaConfig::Type, String("Inspector"));
                return s;
        }

        PhaseOutcome runPhase(MediaPipeline &pipe, const MediaPipelineConfig &cfg, EventLoop *loop,
                              unsigned int timeoutMs) {
                PhaseOutcome p;
                p.buildError = pipe.build(cfg, /*autoplan=*/true);
                if (p.buildError.isError()) return p;
                p.built = true;

                // Snapshot the resolved pipeline graph as JSON.
                // MediaPipeline::config() is updated in build() to the
                // post-autoplan configuration, so the captured object
                // reflects every planner-injected stage (CSC, SRC,
                // FrameBridge, ...) and the final per-stage MediaConfig.
                p.resolvedConfig = pipe.config().toJson();

                pipe.pipelineErrorSignal.connect(
                        [&p](const String &stageName, Error err) {
                                if (!p.sawError) {
                                        p.errorDetail = stageName + String(": ") + err.desc();
                                        p.firstError = err;
                                }
                                p.sawError = true;
                        },
                        &pipe);
                pipe.closedSignal.connect(
                        [&p, loop](Error err) {
                                p.closeError = err;
                                // Gate the quit on the loop being
                                // inside exec() — same reason as
                                // runDualPhase: if a build/open/start
                                // failure exits this function before
                                // exec() is reached, the pipe's
                                // destructor still fires closedSignal,
                                // and an unconditional quit here would
                                // queue a stale QuitItem the *next*
                                // test's exec() would consume on
                                // entry.
                                if (loop->isRunning()) loop->quit(0);
                        },
                        &pipe);

                p.openError = pipe.open();
                if (p.openError.isError()) return p;
                p.opened = true;

                p.startError = pipe.start();
                if (p.startError.isError()) {
                        (void)pipe.close();
                        return p;
                }
                p.started = true;

                int watchdogId = -1;
                if (timeoutMs > 0) {
                        const unsigned int ms = timeoutMs;
                        watchdogId = loop->startTimer(
                                ms,
                                [&p, loop, ms]() {
                                        if (p.timedOut) return;
                                        p.timedOut = true;
                                        promekiWarn("promeki-test: phase watchdog fired after %u ms", ms);
                                        loop->quit(0);
                                },
                                /*singleShot=*/true);
                }

                loop->exec();
                if (watchdogId >= 0) loop->stopTimer(watchdogId);

                // Best-effort cancellation on a hung pipeline so the
                // stack-local destructor doesn't park the test thread.
                if (p.timedOut) {
                        StringList names = pipe.stageNames();
                        for (size_t i = 0; i < names.size(); ++i) {
                                MediaIO *io = pipe.stage(names[i]);
                                if (io == nullptr) continue;
                                if (auto *src = io->source(0)) src->cancelPending();
                        }
                }
                return p;
        }

        void runDualPhase(MediaPipeline &txPipe, const MediaPipelineConfig &txCfg, MediaPipeline &rxPipe,
                          const MediaPipelineConfig &rxCfg, EventLoop *loop, unsigned int timeoutMs,
                          DualPhaseOutcome &out, DualPhaseSequence sequence, bool txAutoplan, bool rxAutoplan) {
                // ---- Connect signals up front so error signals
                //       fired during build / open / start are still
                //       captured (they get queued on the strand and
                //       delivered when the loop next pumps).
                //       Every captured reference here points into
                //       @p out, which the caller is contractually
                //       required to keep alive until both pipelines
                //       have been destroyed — see @ref runDualPhase
                //       's docstring. ----
                txPipe.pipelineErrorSignal.connect(
                        [&out](const String &stageName, Error err) {
                                if (!out.tx.sawError) {
                                        out.tx.errorDetail = stageName + String(": ") + err.desc();
                                        out.tx.firstError = err;
                                }
                                out.tx.sawError = true;
                        },
                        &txPipe);
                rxPipe.pipelineErrorSignal.connect(
                        [&out](const String &stageName, Error err) {
                                if (!out.rx.sawError) {
                                        out.rx.errorDetail = stageName + String(": ") + err.desc();
                                        out.rx.firstError = err;
                                }
                                out.rx.sawError = true;
                        },
                        &rxPipe);
                txPipe.closedSignal.connect(
                        [&out, loop](Error err) {
                                out.tx.closeError = err;
                                // Gate the loop-quit on the loop
                                // actually being inside @c exec() —
                                // when build / open / start fails
                                // before runDualPhase enters
                                // @c loop->exec(), the cleanup
                                // @c close() calls below still fire
                                // closedSignal asynchronously, and
                                // an unconditional @c quit here would
                                // enqueue a stale QuitItem that the
                                // next test's @c exec() picks up and
                                // exits on immediately.
                                if (++out.closedCount == 2 && loop->isRunning()) loop->quit(0);
                        },
                        &txPipe);
                rxPipe.closedSignal.connect(
                        [&out, loop](Error err) {
                                out.rx.closeError = err;
                                if (++out.closedCount == 2 && loop->isRunning()) loop->quit(0);
                        },
                        &rxPipe);

                // ---- Build TX, then open TX, BEFORE we even build RX.
                //       RTP-style backends consume an SDP file the TX
                //       side writes at open time, and the planner reads
                //       the SDP at build time — so the RX build must
                //       not run until after TX open has put the file on
                //       disk.  This sequencing is a contract baked into
                //       the runner so individual tests don't have to
                //       relitigate it. ----
                out.tx.buildError = txPipe.build(txCfg, txAutoplan);
                if (out.tx.buildError.isError()) return;
                out.tx.built = true;
                out.tx.resolvedConfig = txPipe.config().toJson();

                out.tx.openError = txPipe.open();
                if (out.tx.openError.isError()) {
                        (void)txPipe.close();
                        return;
                }
                out.tx.opened = true;

                // For TxStartFirst transports (FrameBridge): start
                // TX before building RX.  The RX-side planner can
                // then probe the source via a brief open / close
                // cycle — that probe runs the handshake, which the
                // FrameBridge AcceptWorker services off the strand
                // the moment openOutput returns.  Sink-side
                // waitForConsumer keeps TX's first frame parked
                // until RX attaches, so no frames are lost while RX
                // is building.
                if (sequence == DualPhaseSequence::TxStartFirst) {
                        out.tx.startError = txPipe.start();
                        if (out.tx.startError.isError()) {
                                (void)txPipe.close();
                                return;
                        }
                        out.tx.started = true;
                }

                out.rx.buildError = rxPipe.build(rxCfg, rxAutoplan);
                if (out.rx.buildError.isError()) {
                        (void)txPipe.close();
                        return;
                }
                out.rx.built = true;
                out.rx.resolvedConfig = rxPipe.config().toJson();

                out.rx.openError = rxPipe.open();
                if (out.rx.openError.isError()) {
                        (void)txPipe.close();
                        (void)rxPipe.close();
                        return;
                }
                out.rx.opened = true;

                // ---- Start order depends on the transport.  For
                //       RxStartFirst (RTP, default): RX binds its
                //       sockets and begins listening, then TX starts
                //       emitting.  For TxStartFirst (FrameBridge): TX
                //       was already started above; only RX remains. ----
                out.rx.startError = rxPipe.start();
                if (out.rx.startError.isError()) {
                        (void)txPipe.close();
                        (void)rxPipe.close();
                        return;
                }
                out.rx.started = true;

                if (sequence == DualPhaseSequence::RxStartFirst) {
                        out.tx.startError = txPipe.start();
                        if (out.tx.startError.isError()) {
                                (void)txPipe.close();
                                (void)rxPipe.close();
                                return;
                        }
                        out.tx.started = true;
                }

                // ---- Watchdog covering the combined run ----
                int watchdogId = -1;
                if (timeoutMs > 0) {
                        const unsigned int ms = timeoutMs;
                        watchdogId = loop->startTimer(
                                ms,
                                [&out, loop, ms]() {
                                        if (out.tx.timedOut || out.rx.timedOut) return;
                                        out.tx.timedOut = true;
                                        out.rx.timedOut = true;
                                        promekiWarn("promeki-test: dual-phase watchdog fired after %u ms", ms);
                                        loop->quit(0);
                                },
                                /*singleShot=*/true);
                }

                loop->exec();
                if (watchdogId >= 0) loop->stopTimer(watchdogId);

                // Best-effort drain of either side if the watchdog cut
                // it off before its closedSignal fired.  We cancel any
                // pending source requests so the pipeline destructors
                // don't park the test thread waiting on a strand.
                auto drainIfHung = [](MediaPipeline &pipe) {
                        StringList names = pipe.stageNames();
                        for (size_t i = 0; i < names.size(); ++i) {
                                MediaIO *io = pipe.stage(names[i]);
                                if (io == nullptr) continue;
                                if (auto *src = io->source(0)) src->cancelPending();
                        }
                };
                if (out.tx.timedOut) drainIfHung(txPipe);
                if (out.rx.timedOut) drainIfHung(rxPipe);
        }

} // namespace promekitest

PROMEKI_NAMESPACE_END
