/**
 * @file      cases/pipeline_common.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Pipeline helpers shared across promeki-test case suites.
 *
 * Every functional test in this util drives a @ref MediaPipeline with
 * @ref TpgMediaIO as the source and @ref InspectorMediaIO as the
 * terminal sink so the inspector's discontinuity counter — picture
 * data, audio data, A/V sync, continuity, timestamp, and audio sample
 * checks — can validate the round-trip end-to-end.  The stage
 * builders here keep that policy in one place — TPG with picture
 * data + audio enabled, encoder with intra-only GOP and a high enough
 * bitrate to preserve TPG's stamp band, etc.  The @ref runPhase
 * helper drives one pipeline through build / open / start / exec /
 * close and applies a per-phase watchdog so a deadlocked stage can't
 * stall the whole matrix.
 */

#pragma once

#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/namespace.h>
#include <promeki/pixelformat.h>
#include <promeki/string.h>
#include <promeki/videocodec.h>
#include <promeki/videoformat.h>

#include <cstdint>

PROMEKI_NAMESPACE_BEGIN

namespace promekitest {

        /// @brief Default raster + frame rate every pipeline-driven
        ///        test uses unless the suite overrides it.  720p59.94
        ///        stays in every codec's happy path and keeps disk
        ///        I/O bounded for file-based suites.
        inline constexpr VideoFormat::WellKnownFormat kDefaultVideoFormat = VideoFormat::Smpte720p59_94;

        /**
         * @brief Builds the TPG source stage every test starts from.
         *
         * Picture-data band, audio, and timecode are all enabled so
         * the inspector has every signal it needs to detect a
         * round-trip discontinuity.  Audio waveform fidelity is
         * validated through the @c AudioData / AvSync inspector tests
         * (enabled by default in @ref InspectorMediaIO) — the LTC
         * waveform check is no longer used since AvSync covers the
         * same fault domain with a tighter signal.  When
         * @p tpgPixelFormat is valid and uncompressed, TPG produces
         * frames in that format directly; otherwise it stays on the
         * backend default and the planner can splice a CSC if needed.
         *
         * @param streamId        Stream ID stamped into TPG's picture
         *                        data band; must be unique across cases
         *                        in a single run so the inspector can
         *                        tell them apart.
         * @param tpgPixelFormat  Optional pixel format override.
         *                        Invalid (default-constructed) means
         *                        "let TPG pick".
         * @param videoEnabled    If false, TPG runs as an audio-only
         *                        source.  Useful for backends that
         *                        only consume one essence (audio-only
         *                        file formats, audio-only RTP feeds).
         * @param audioEnabled    If false, TPG runs as a video-only
         *                        source.
         */
        MediaPipelineConfig::Stage makeTpgStage(uint32_t streamId,
                                                const PixelFormat &tpgPixelFormat = PixelFormat(),
                                                bool videoEnabled = true, bool audioEnabled = true);

        /**
         * @brief Builds the @ref VideoEncoder stage.
         *
         * Bitrate is pinned to 50 Mbit/s and GOP length to 1 — both
         * chosen so lossy codecs (H.264, HEVC) keep TPG's picture-data
         * band readable and every output frame is self-contained.  When
         * @p variant is a valid compressed @ref PixelFormat the encoder
         * is asked to produce that specific compressed variant
         * (e.g. @c JPEG_YUV8_420_Rec709 vs @c JPEG_YUV8_422_Rec601).
         *
         * @param codec   The @ref VideoCodec to encode with; pinning
         *                a backend in the @c VideoCodec wrapper
         *                propagates through to backend selection.
         * @param variant Optional compressed @ref PixelFormat to steer
         *                the encoder's output variant.
         */
        MediaPipelineConfig::Stage makeEncoderStage(const VideoCodec &codec,
                                                    const PixelFormat &variant = PixelFormat());

        /**
         * @brief Builds the @ref VideoDecoder stage.
         *
         * @param codec The @ref VideoCodec to decode with; backend pin
         *              propagates from the wrapper.
         */
        MediaPipelineConfig::Stage makeDecoderStage(const VideoCodec &codec);

        /**
         * @brief Builds the terminal @ref InspectorMediaIO stage.
         *
         * The inspector validates frame numbers, picture data, audio
         * data, A/V sync, and audio/video MediaTimeStamp continuity;
         * @ref InspectorSnapshot's @c totalDiscontinuities is the
         * strongest assertion every test makes.
         */
        MediaPipelineConfig::Stage makeInspectorStage();

        /**
         * @brief Outcome of a single @ref runPhase invocation.
         *
         * Captures the per-step error returned by build / open / start /
         * close, plus a flag for any pipeline-error signal observed
         * during exec().  The @c timedOut flag is set when the watchdog
         * fired before @c closedSignal — the caller interprets that as
         * a deadlock and continues to the next case.
         */
        struct PhaseOutcome {
                        bool       built = false;
                        bool       opened = false;
                        bool       started = false;
                        bool       sawError = false;
                        bool       timedOut = false;
                        Error      buildError;
                        Error      openError;
                        Error      startError;
                        Error      closeError;
                        String     errorDetail; // first stage+error string from pipelineErrorSignal
                        JsonObject resolvedConfig;
                        ///< Post-autoplan resolved @ref MediaPipelineConfig
                        ///< serialized to JSON.  Populated immediately after a
                        ///< successful @c build() so tests can record the exact
                        ///< stage list (including planner-injected CSC /
                        ///< SRC / FrameBridge stages) the pipeline ran with.
                        ///< Empty when the build itself failed.
        };

        /**
         * @brief Drives one @ref MediaPipeline through its full lifecycle.
         *
         * Builds @p pipe from @p cfg with autoplan, connects the
         * pipeline-error and closed signals, opens, starts, and runs
         * @c loop->exec().  A one-shot timer fires after @p timeoutMs
         * to break any deadlock — the caller treats the resulting
         * @c timedOut flag as a separate Timeout outcome rather than a
         * functional Fail.
         *
         * @param pipe       The pipeline to drive (caller-owned).
         * @param cfg        The configuration to build into @p pipe.
         * @param loop       Event loop the pipeline runs on (typically
         *                   @c Application::mainEventLoop()).
         * @param timeoutMs  Per-phase watchdog in milliseconds; 0
         *                   disables the watchdog.
         */
        PhaseOutcome runPhase(MediaPipeline &pipe, const MediaPipelineConfig &cfg, EventLoop *loop,
                              unsigned int timeoutMs);

        /**
         * @brief Drives two @ref MediaPipeline objects concurrently.
         *
         * Built for transport-level loopback tests (RTP, NDI, future
         * SRT) where a transmitter and a receiver run in the same
         * process and exchange data over a network loopback.  The
         * sequencing is fixed:
         *
         *   1. Build TX.
         *   2. Open TX.  Any open-time side-effects — writing an SDP
         *      file, allocating sender sockets, registering with a
         *      discovery service — are complete after this returns.
         *   3. Build RX.  Deferred until after TX open because the
         *      RX-side planner often consumes the artifact TX produced
         *      (e.g. RtpMediaIO's RX planner parses the on-disk SDP
         *      to learn payload types and clock rates).
         *   4. Open RX.
         *   5. Start RX before TX so it is bound and listening before
         *      the first packet arrives — UDP loopback drops anything
         *      that lands at an unbound port.
         *   6. Start TX.
         *   7. Run @c loop->exec() until @em both pipelines have
         *      emitted their @c closedSignal (or the watchdog fires).
         *
         * Both pipelines drive the same event loop, so the caller
         * must not invoke @c runPhase or @c runDualPhase recursively.
         *
         * @param txPipe     Transmit-side pipeline (caller-owned).
         * @param txCfg      Transmit-side configuration.
         * @param rxPipe     Receive-side pipeline (caller-owned).
         * @param rxCfg      Receive-side configuration.
         * @param loop       Shared event loop.
         * @param timeoutMs  Watchdog timeout in milliseconds for the
         *                   combined run.
         * @return A pair of outcomes — the @c .tx field describes the
         *         transmit pipeline's lifecycle, the @c .rx field the
         *         receive pipeline's.
         */
        struct DualPhaseOutcome {
                        PhaseOutcome tx;
                        PhaseOutcome rx;
                        /// Internal: number of pipelines that have
                        /// fired @c closedSignal so far.  Lives here
                        /// (rather than as a stack local in
                        /// @ref runDualPhase) so the close-signal
                        /// lambdas can safely reference it after
                        /// @ref runDualPhase returns — close cascades
                        /// can emit one final @c closedSignal during
                        /// pipeline destruction, and the lambdas keep
                        /// running until the pipeline is gone.
                        int closedCount = 0;
        };

        /**
         * @brief Sequencing strategy for @ref runDualPhase.
         *
         * Different transports impose different ordering constraints
         * between the TX and RX pipelines:
         *
         *   - @c RxStartFirst — open both, start RX, then start TX.
         *     Right for UDP-style transports (RTP) where the RX must
         *     be bound and listening before TX emits anything.
         *
         *   - @c TxStartFirst — open and start TX, then build / open /
         *     start RX.  Right for transports where the TX must have
         *     opened (and therefore listed its socket / written its
         *     SDP / etc.) before the RX-side planner can build —
         *     FrameBridge in particular wants its listening socket
         *     up before the RX planner's brief-open probe runs.
         */
        enum class DualPhaseSequence {
                RxStartFirst,
                TxStartFirst,
        };

        /**
         * @brief Runs both pipelines and writes the result into @p out.
         *
         * @p out is taken by reference (rather than returned by value)
         * so the signal handlers connected by this function — which
         * capture @p out by reference and live as long as the
         * pipelines themselves — never see a dangling reference.  In
         * particular, the close-cascade signals can fire as the
         * pipelines tear down (after this function has returned), and
         * those callbacks must still find @p out alive in the caller's
         * scope.  The contract is therefore: @p out's lifetime must
         * extend at least until both @p txPipe and @p rxPipe are
         * destroyed.
         *
         * @param sequence    Sequencing strategy; see @ref DualPhaseSequence.
         * @param txAutoplan  Pass to @c MediaPipeline::build for the TX pipeline.
         * @param rxAutoplan  Pass to @c MediaPipeline::build for the RX pipeline.
         */
        void runDualPhase(MediaPipeline &txPipe, const MediaPipelineConfig &txCfg, MediaPipeline &rxPipe,
                          const MediaPipelineConfig &rxCfg, EventLoop *loop, unsigned int timeoutMs,
                          DualPhaseOutcome &out,
                          DualPhaseSequence sequence = DualPhaseSequence::RxStartFirst,
                          bool txAutoplan = true, bool rxAutoplan = true);

} // namespace promekitest

PROMEKI_NAMESPACE_END
