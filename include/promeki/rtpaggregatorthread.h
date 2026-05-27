/**
 * @file      rtpaggregatorthread.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <functional>
#include <promeki/function.h>
#include <promeki/atomic.h>
#include <promeki/audiobuffer.h>
#include <promeki/audiodesc.h>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
#include <promeki/rtpstreamclock.h>
#include <promeki/rxpayloadbundle.h>
#include <promeki/string.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief View of one video reader stream the aggregator consumes.
 * @ingroup network
 *
 * Pierced into @ref RtpMediaIO::VideoReaderStream at thread-spawn
 * time so the aggregator can stay decoupled from the surrounding
 * RtpMediaIO type.  The unit test stands the aggregator up against
 * synthetic queues + a synthetic @c lastPacketArrivalNs counter
 * by populating this struct directly.
 *
 * All pointer fields are non-owning — the underlying objects must
 * outlive the aggregator thread.
 */
struct RtpAggregatorVideoStream {
                /// @brief Video bundle queue the aggregator pops in
                ///        @ref RtpAggregatorThread::Mode::Video.
                ///        @c nullptr when no video reader is active.
                Queue<RxVideoFrame> *payloadQueue = nullptr;

                /// @brief Wire-silence indicator the watchdog
                ///        consults to decide whether a missed video
                ///        pop is real wire silence (sender stalled)
                ///        or an aggregator-internal stall (a slow
                ///        strand backing up @c pushFrame).  The
                ///        depacketizer thread bumps it on every
                ///        observed packet.  Zero until the first
                ///        packet lands.
                const Atomic<int64_t> *lastPacketArrivalNs = nullptr;
};

/**
 * @brief View of one audio reader stream the aggregator consumes.
 * @ingroup network
 *
 * Same intent as @ref RtpAggregatorVideoStream — the surrounding
 * RtpMediaIO populates each pointer at spawn time and the
 * aggregator reads through it at run time.
 *
 * @c readerAudioDesc / @c streamClock / @c hasSr are mutated
 * concurrently by the audio depacketizer thread (which lazily
 * resolves the wire desc on the first packet and refreshes the
 * stream clock from observed SRs).  The aggregator reads them on
 * its own thread; both sides use the publication semantics
 * already encoded in the underlying types (e.g. @c hasSr is a
 * plain @c bool published before the @c streamClock fields it
 * gates).
 */
struct RtpAggregatorAudioStream {
                /// @brief Audio bundle queue the aggregator pops.
                Queue<RxAudioChunk> *payloadQueue = nullptr;

                /// @brief @c true once the surrounding
                ///        RtpMediaIO has activated this stream
                ///        (i.e. opened its session).  Set at
                ///        spawn time and never modified after.
                bool active = false;

                /// @brief Wire format of the L16 / L24 PCM stream,
                ///        populated by the audio depacketizer
                ///        thread on the first packet.  The
                ///        aggregator uses it to size each emitted
                ///        @c PcmAudioPayload.
                const AudioDesc *readerAudioDesc = nullptr;

                /// @brief Per-stream RTP <-> NTP clock mapping;
                ///        valid once the depacketizer has folded
                ///        in the first observed SR.  When valid,
                ///        the aggregator targets a wallclock-
                ///        anchored audio cursor instead of a
                ///        first-arrival anchor.
                const RtpStreamClock *streamClock = nullptr;

                /// @brief @c true once the first SR for this
                ///        stream has seeded @ref streamClock.
                ///        Read alongside @ref streamClock to
                ///        decide whether wallclock alignment is
                ///        in effect.
                const bool *hasSr = nullptr;

                /// @brief SDP-derived clock domain.  Stamped onto
                ///        every @c MediaTimeStamp the aggregator
                ///        produces in audio-only mode.
                ClockDomain clockDomain;
};

/**
 * @brief View of one data reader stream the aggregator consumes.
 * @ingroup network
 *
 * Mirrors @ref RtpAggregatorVideoStream / @ref RtpAggregatorAudioStream
 * for JSON metadata reader streams.
 */
struct RtpAggregatorDataStream {
                /// @brief Data bundle queue the aggregator pops.
                Queue<RxDataMessage> *payloadQueue = nullptr;

                /// @brief @c true once the surrounding
                ///        RtpMediaIO has activated this stream.
                bool active = false;

                /// @brief SDP-derived clock domain for any
                ///        @c MediaTimeStamp produced in
                ///        @ref RtpAggregatorThread::Mode::DataOnly.
                ClockDomain clockDomain;
};

/**
 * @brief View of one ANC reader stream the aggregator consumes.
 * @ingroup network
 *
 * Mirrors @ref RtpAggregatorDataStream for RFC 8331 / ST 2110-40 ANC
 * streams.  When populated, the aggregator drains @ref RxAncFrame
 * bundles whose @c captureTime falls inside the current video
 * window (@c Mode::Video) and attaches their packet list to the
 * outgoing @c Frame as an @ref AncPayload.  @c Mode::AncOnly emits
 * one Frame per ANC bundle.
 */
struct RtpAggregatorAncStream {
                /// @brief ANC bundle queue the aggregator pops.
                Queue<RxAncFrame> *payloadQueue = nullptr;

                /// @brief @c true once the surrounding
                ///        RtpMediaIO has activated this stream.
                bool active = false;

                /// @brief SDP-derived clock domain for any
                ///        @c MediaTimeStamp produced in
                ///        @ref RtpAggregatorThread::Mode::AncOnly.
                ClockDomain clockDomain;
};

/**
 * @brief Static dependencies handed to @ref RtpAggregatorThread at
 *        construction time.
 * @ingroup network
 *
 * The aggregator does not need a full @c RtpMediaIO to function —
 * it needs the per-kind bundle queues, the per-frame target
 * cadence, the watchdog opt-in, and a way to push completed
 * Frames back to the consumer.  This struct bundles those
 * dependencies so the unit test can construct the aggregator
 * against a synthetic harness without touching the surrounding
 * RtpMediaIO.
 *
 * @par Lifetime
 * Every pointer in @ref video / @ref audio / @ref data and the
 * @ref readerQueue pointer must outlive the aggregator thread.
 * @ref pushFrame is invoked on every successfully assembled
 * Frame.
 *
 * @par Population
 * @ref RtpMediaIO populates this at the end of
 * @c executeCmd(MediaIOCommandOpen) — when the per-stream
 * payload queues / depacketizer threads exist — and hands it to
 * the aggregator's constructor.
 */
struct RtpAggregatorContext {
                /// @brief Target frame cadence (1 / fps).  Drives
                ///        the per-frame audio drain window in
                ///        @ref RtpAggregatorThread::Mode::Video,
                ///        the audio-only emission boundary in
                ///        @ref RtpAggregatorThread::Mode::AudioOnly,
                ///        and the watchdog activation threshold.
                ///        An invalid FrameRate disables the
                ///        watchdog and switches the audio-only
                ///        path to per-chunk emission.
                FrameRate frameRate;

                /// @brief Opt-in for the audio-only-continuation
                ///        watchdog in @c Mode::Video.  Default
                ///        @c false because downstream consumers
                ///        that hard-require a video payload reject
                ///        watchdog Frames; deployments that prefer
                ///        smooth audio over a video gap enable it
                ///        explicitly.
                bool videoWatchdogEnabled = false;

                /// @brief Per-kind reader stream views.  The
                ///        aggregator's mode dictates which sub-
                ///        contexts are populated; in @c Mode::Video
                ///        all three may be (depending on what the
                ///        SDP / openReaderStream wired up); in
                ///        @c Mode::AudioOnly only @ref audio; in
                ///        @c Mode::DataOnly only @ref data.
                RtpAggregatorVideoStream video;
                RtpAggregatorAudioStream audio;
                RtpAggregatorDataStream  data;
                RtpAggregatorAncStream   anc;

                /// @brief Pointer to the consumer-side reader
                ///        queue.  Used only by
                ///        @ref RtpAggregatorThread::requestStop to
                ///        cancel any @c pushFrame invocation
                ///        currently parked on a block-on-full
                ///        push; may be @c nullptr if the harness
                ///        does not need that wakeup path (e.g.
                ///        unit tests that drive the aggregator
                ///        synchronously from the same thread).
                Queue<Frame> *readerQueue = nullptr;

                /// @brief Sink for successfully assembled Frames.
                ///        Must be set.  Invoked on the aggregator
                ///        thread; implementations are responsible
                ///        for any onward push (drop-oldest /
                ///        block-on-full) into the strand-side
                ///        queue.
                Function<void(Frame)> pushFrame;
};

/**
 * @brief Per-RtpMediaIO single-consumer / single-producer
 *        aggregator that turns per-stream RX bundles into
 *        consumer-ready Frames.
 * @ingroup network
 *
 * One thread per RtpMediaIO instance in reader mode.  Mode is
 * selected at construction time from the populated reader
 * lists:
 *
 * - @ref Mode::Video — the steady-state mode for video + audio
 *   (+ data) reader streams.  The aggregator pops a video
 *   bundle, computes its captureTime-window
 *   @c [T, T + frameDuration), drains audio chunks whose
 *   @c captureTime falls inside that window into an internal
 *   @ref AudioBuffer FIFO, slices @c samplesPerFrame samples
 *   onto the Frame, drains pending data messages, and pushes
 *   the combined Frame onto the consumer.  When the video pop
 *   stalls — and real wire silence is observed via the
 *   per-stream @c lastPacketArrivalNs — the watchdog (opt-in)
 *   emits audio-only-continuation Frames at the SDP-advertised
 *   video rate so playback continues.  The continuation cursor
 *   advances by exactly one frame per emission so consumers see
 *   monotonic captureTime stamps even across the stall.
 *
 * - @ref Mode::AudioOnly — pop audio chunks; when a frame rate
 *   is advertised, accumulate @c samplesPerFrame in the FIFO
 *   and emit one Frame per cadence boundary.  Otherwise emit
 *   one Frame per @c RxAudioChunk (the chunk's natural
 *   cadence).
 *
 * - @ref Mode::DataOnly — pop data messages; emit one Frame per
 *   message.
 *
 * @par Pending-metadata slot
 * In @c Mode::Video, a data message popped past the current
 * frame's window is parked in an internal pending slot (since
 * @ref Queue::tryPop has no put-back) so the next iteration
 * consumes it instead of dropping it on the floor.
 *
 * @par Threading
 * One worker thread.  The aggregator is the sole consumer of
 * every per-stream payload queue handed in via
 * @ref RtpAggregatorContext, and the sole producer that calls
 * @ref RtpAggregatorContext::pushFrame.  The audio FIFO is
 * single-threaded — only this aggregator pushes / pops it — so
 * no cross-thread mutex is required across streams.
 *
 * @par Shutdown
 * @ref requestStop sets the stop flag and calls
 * @c Queue::cancelWaiters on every input queue (and on
 * @ref RtpAggregatorContext::readerQueue when set) so any
 * blocking pop / push wakes promptly.  The destructor calls
 * @ref requestStop and joins the worker.  No sentinel Frames
 * pushed.
 */
class RtpAggregatorThread : public Thread {
        public:
                /// @brief Mode selector.  Picked at construction
                ///        time from the populated reader lists.
                enum class Mode {
                        Video,      ///< Video-clocked aggregation.
                        AudioOnly,  ///< Audio-clocked aggregation.
                        DataOnly,   ///< Data-clocked aggregation.
                        AncOnly     ///< ANC-clocked aggregation (one
                                    ///<  Frame per RxAncFrame).
                };

                /// @brief Watchdog activation threshold expressed
                ///        in frame durations.  Conservative —
                ///        short enough to mask brief sender
                ///        hiccups but long enough to avoid
                ///        spurious continuation emission on
                ///        normal jitter.  @c 4 × @c frameDuration
                ///        is ~133 ms at 30 fps.
                static constexpr int kStallNFrames = 4;

                /// @brief Pop poll cap for the cancellation path
                ///        in milliseconds.  Cancellation is
                ///        responsive within this bound regardless
                ///        of the watchdog cadence.
                static constexpr unsigned int kPopCapMs = 50;

                /// @brief Pop poll floor when no frame rate has
                ///        been advertised (watchdog disabled).
                ///        Just keeps cancellation responsive.
                static constexpr unsigned int kPopFloorMs = 5;

                /**
                 * @brief Constructs an unstarted aggregator.
                 *
                 * @param ctx  Static dependencies — see
                 *             @ref RtpAggregatorContext.
                 * @param mode Aggregation mode.  Caller is
                 *             responsible for ensuring the
                 *             matching sub-context is populated
                 *             (e.g. @c Mode::Video requires
                 *             @c ctx.video.payloadQueue).
                 * @param name Short OS thread name (≤ 15 chars on
                 *             Linux).  Defaults to
                 *             @c "RtpAggregator".
                 */
                RtpAggregatorThread(RtpAggregatorContext ctx, Mode mode,
                                    const String &name = String("RtpAggregator"));

                /// @brief Destructor.  Issues @ref requestStop and
                ///        joins the worker.
                ~RtpAggregatorThread() override;

                RtpAggregatorThread(const RtpAggregatorThread &) = delete;
                RtpAggregatorThread &operator=(const RtpAggregatorThread &) = delete;

                /**
                 * @brief Marks the worker for shutdown and wakes
                 *        any pending @c pop / @c pushBlocking
                 *        calls.
                 *
                 * Idempotent.  Cancels every input queue and the
                 * @ref RtpAggregatorContext::readerQueue (when
                 * set) so a blocked-on-full @c pushFrame returns
                 * promptly.
                 */
                void requestStop();

                /// @brief @c true once @ref requestStop has been
                ///        called.
                bool isStopRequested() const { return _stopRequested.value(); }

                /// @brief Returns the aggregation mode selected at
                ///        construction time.
                Mode mode() const { return _mode; }

                /**
                 * @brief Drives one iteration of the active mode
                 *        synchronously without spawning the
                 *        worker.  Intended for unit tests that
                 *        prefer to drive the aggregator from the
                 *        test thread.
                 *
                 * Each invocation does at most one full iteration
                 * of the mode's loop body — pop one bundle (with
                 * the configured pop timeout in milliseconds),
                 * process it, push the resulting Frame.
                 *
                 * @param popMs Pop timeout in milliseconds.
                 */
                void runOnce(unsigned int popMs = kPopCapMs);

                /// @brief Direct access to the aggregator-owned
                ///        audio FIFO.  Intended for unit tests
                ///        that need to inspect the FIFO depth
                ///        between iterations.
                const AudioBuffer &audioFifo() const { return _audioFifo; }

                /// @brief Most recent emitted-Frame cursor used by
                ///        the watchdog cadence.  Zero until the
                ///        first Frame is emitted.  Intended for
                ///        unit tests.
                TimeStamp emittedFrameCursor() const { return _emittedFrameCursor; }

                /// @brief @c true while the aggregator is currently
                ///        in a watchdog-driven continuation
                ///        interval (cleared on the next successful
                ///        video pop).  Intended for unit tests.
                bool inWatchdog() const { return _inWatchdog; }

        protected:
                /// @brief Thread entry point — dispatches to the
                ///        active mode's loop body.
                void run() override;

        private:
                Duration frameDuration() const;

                void runVideoMode();
                void runAudioOnlyMode();
                void runDataOnlyMode();
                void runAncOnlyMode();

                /**
                 * @brief Pulls audio chunks whose @c captureTime
                 *        is below @p windowEnd into the
                 *        aggregator's FIFO.
                 */
                void drainAudioIntoFifoBefore(const TimeStamp &windowEnd);

                /**
                 * @brief Returns @c true and sets @p out to the
                 *        most recent data message inside
                 *        @c [-∞, windowEnd).  Messages popped
                 *        past the window are parked in
                 *        @ref _pendingData.
                 */
                bool drainDataBefore(const TimeStamp &windowEnd, RxDataMessage &out);

                /**
                 * @brief Returns @c true and sets @p out to the most
                 *        recent ANC frame inside
                 *        @c [-∞, windowEnd).  Frames popped past the
                 *        window are parked in @ref _pendingAnc.
                 */
                bool drainAncBefore(const TimeStamp &windowEnd, RxAncFrame &out);

                /// @brief One iteration of @c Mode::Video — pops
                ///        one video bundle (with the configured
                ///        pop timeout), assembles + emits the
                ///        Frame.  Returns @c true on a successful
                ///        emission, @c false on timeout / cancel.
                bool stepVideoMode(unsigned int popMs);

                /// @brief One iteration of @c Mode::AudioOnly.
                bool stepAudioOnlyMode(unsigned int popMs);

                /// @brief One iteration of @c Mode::DataOnly.
                bool stepDataOnlyMode(unsigned int popMs);

                /// @brief One iteration of @c Mode::AncOnly.
                bool stepAncOnlyMode(unsigned int popMs);

                void emitFrameForVideo(RxVideoFrame video, const Duration &fd);
                void emitWatchdogFrame(const Duration &fd);
                void emitAudioOnlyAtFrameRate(const RxAudioChunk &c);
                void emitAudioOnlyPerChunk(const RxAudioChunk &c);

                /// @brief Lazily configures @ref _audioFifo from
                ///        the first chunk's wire desc.  Returns
                ///        @c false if the desc is invalid.
                bool ensureAudioFifo(const AudioDesc &wireDesc);

                /// @brief Cadence selection helper.  Returns the
                ///        pop timeout in milliseconds for
                ///        @c Mode::Video given the current frame
                ///        duration and watchdog opt-in.
                unsigned int videoPopMs(const Duration &fd) const;

                RtpAggregatorContext _ctx;
                Mode                 _mode;
                Atomic<bool>         _stopRequested;

                /// @brief Aggregator-owned audio FIFO — the only
                ///        thread that pushes / pops it is the
                ///        aggregator thread itself, so no
                ///        cross-thread synchronization is needed.
                AudioBuffer _audioFifo;

                /// @brief RTP timestamp of the sample at the
                ///        front of @ref _audioFifo.  Tracked so
                ///        the wallclock-aligned drain can drop
                ///        pre-window samples without inspecting
                ///        the FIFO contents.
                uint32_t _audioFifoFrontRtpTs = 0;

                /// @brief @c true once @ref _audioFifoFrontRtpTs
                ///        has been seeded (i.e. at least one
                ///        chunk has been pushed).
                bool _audioFifoHasFront = false;

                /// @brief Per-frame index used for
                ///        @c samplesPerFrame computations.
                FrameNumber _videoFrameIndex{0};

                /// @brief @c captureTime of the last emitted
                ///        Frame.  Watchdog continuation emissions
                ///        advance it by exactly one frame so
                ///        consumers see monotonic stamps across
                ///        the stall.  Invalid until the first
                ///        emission — callers detect that via
                ///        @c isValid().
                TimeStamp _emittedFrameCursor;

                /// @brief @c true while a watchdog interval is
                ///        active.  Cleared on the next successful
                ///        video pop.
                bool _inWatchdog = false;

                /// @brief Pending data message — populated when
                ///        @ref drainDataBefore pops a message past
                ///        the current window and parks it for the
                ///        next iteration.
                RxDataMessage _pendingData;

                /// @brief @c true while @ref _pendingData holds a
                ///        deferred message.
                bool _hasPendingData = false;

                /// @brief Pending ANC frame — same put-back pattern
                ///        as @ref _pendingData.
                RxAncFrame _pendingAnc;

                /// @brief @c true while @ref _pendingAnc holds a
                ///        deferred ANC frame.
                bool _hasPendingAnc = false;

                /// @brief Tracks whether @c Mode::Video has seen
                ///        at least one successful video pop.  The
                ///        watchdog stays disarmed until then so a
                ///        slow open does not look like a sender
                ///        stall.
                bool _firstVideoSeen = false;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
