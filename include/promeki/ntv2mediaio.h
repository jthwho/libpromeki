/**
 * @file      ntv2mediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/atomic.h>
#include <promeki/audiobuffer.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/clock.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiostats.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/ntv2routing.h>
#include <promeki/pacinggate.h>
#include <promeki/queue.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/string.h>
#include <promeki/thread.h>
#include <promeki/uncompressedvideopayload.h>

PROMEKI_NAMESPACE_BEGIN

class Ntv2Device;

/**
 * @brief MediaIO backend wrapping one logical channel on an AJA card.
 * @ingroup proav
 *
 * One instance represents one capture or playout channel: a
 * framebuffer (the @c NTV2Channel resource on the card) plus the
 * SDI / HDMI ports assigned to it plus optionally one audio system.
 * The card-level state (acquisition, OEM-task mode, reference
 * clock) lives on a shared @ref Ntv2Device vended by the
 * @ref Ntv2DeviceRegistry.
 *
 * @par Mode support
 *
 * - **Source mode** — single-link SDI capture on one logical channel
 *   (Phase 1).
 * - **Sink mode** — single-link SDI playout on one logical channel
 *   (Phase 2).  The card paces itself off its own reference; external
 *   pacing through @ref MediaIOCommandSetClock is rejected with
 *   @c Error::NotSupported and lands in Phase 6.
 *
 * Dual / quad / 12G link standards and ANC capture / inject ship in
 * later phases (see @c devplan/proav/ntv2.md).
 *
 * @par Threading
 *
 * Inherits the dedicated worker thread from
 * @ref DedicatedThreadMediaIO for command dispatch.  Source opens
 * spawn a capture thread that drives @c AutoCirculateTransfer and
 * feeds the read queue; sink opens spawn a playout thread that
 * drains a bounded write queue and submits to AutoCirculate.  Both
 * worker threads honour @ref cancelBlockingWork so a parallel
 * @c close request unwinds in finite time.
 */
class Ntv2MediaIO : public DedicatedThreadMediaIO {
                PROMEKI_OBJECT(Ntv2MediaIO, DedicatedThreadMediaIO)
        public:
                /** @brief int64_t — total NTV2 video frames captured. */
                static inline const MediaIOStats::ID StatsFramesReceived{"Ntv2FramesReceived"};
                /** @brief int64_t — AutoCirculate-reported drops on the capture side. */
                static inline const MediaIOStats::ID StatsFramesDropped{"Ntv2FramesDropped"};
                /** @brief int64_t — audio bytes received this session. */
                static inline const MediaIOStats::ID StatsAudioBytesReceived{"Ntv2AudioBytesReceived"};
                /** @brief int64_t — total NTV2 video frames submitted to AutoCirculate for playout. */
                static inline const MediaIOStats::ID StatsFramesPlayed{"Ntv2FramesPlayed"};
                /**
                 * @brief int64_t — sink-side frame drops caused by a full
                 *        playout queue (back-pressure from the card).
                 */
                static inline const MediaIOStats::ID StatsFramesDroppedSink{"Ntv2FramesDroppedSink"};
                /**
                 * @brief int64_t — total ANC packets observed on the
                 *        capture path (sum across every captured frame).
                 *
                 * Zero when ANC is disabled or the card lacks the
                 * custom ANC extractor.
                 */
                static inline const MediaIOStats::ID StatsAncPacketsReceived{"Ntv2AncPacketsReceived"};
                /**
                 * @brief int64_t — total ANC packets handed to
                 *        AutoCirculate for emission on the sink path.
                 */
                static inline const MediaIOStats::ID StatsAncPacketsSent{"Ntv2AncPacketsSent"};
                /**
                 * @brief int64_t — input-signal loss events observed on
                 *        the capture path.
                 *
                 * Incremented once each time the capture worker's
                 * periodic poll observes a transition from "signal
                 * present" to "signal absent".  Recovery back to
                 * "present" does not increment this counter (see
                 * @ref StatsSignalReacquired).
                 */
                static inline const MediaIOStats::ID StatsSignalLoss{"Ntv2SignalLoss"};
                /**
                 * @brief int64_t — input-signal re-acquire events on
                 *        the capture path (transitions from absent to
                 *        present after at least one previous loss).
                 */
                static inline const MediaIOStats::ID StatsSignalReacquired{"Ntv2SignalReacquired"};
                /** @brief int64_t — sink @ref PacingGate `OnTime` verdicts. */
                static inline const MediaIOStats::ID StatsPacingTicksOnTime{"Ntv2PacingTicksOnTime"};
                /** @brief int64_t — sink @ref PacingGate `Late` verdicts. */
                static inline const MediaIOStats::ID StatsPacingTicksLate{"Ntv2PacingTicksLate"};
                /** @brief int64_t — sink @ref PacingGate `Skip` verdicts (frames dropped by the gate). */
                static inline const MediaIOStats::ID StatsPacingTicksSkipped{"Ntv2PacingTicksSkipped"};
                /** @brief int64_t — sink @ref PacingGate timeline reanchors. */
                static inline const MediaIOStats::ID StatsPacingReanchors{"Ntv2PacingReanchors"};

                /** @brief Constructs an Ntv2MediaIO. */
                Ntv2MediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes the channel if still open. */
                ~Ntv2MediaIO() override;

                /**
                 * @brief Tells the planner which sink-side video shapes the card accepts.
                 *
                 * The NTV2 frame-buffer format set is the small fixed
                 * table in @ref Ntv2Format::toNtv2PixelFormat — UYVY,
                 * YUYV, RGB8/BGR8, ARGB/ABGR/RGBA.  When the offered
                 * format already maps to one of those, @p preferred
                 * returns @p offered unchanged.  Otherwise the override
                 * asks for a same-color-family fallback (YCbCr →
                 * @c YUV8_422_UYVY_Rec709, anything else →
                 * @c RGB8_sRGB) so the planner splices a CSC bridge
                 * upstream rather than failing open with
                 * @c FormatMismatch.
                 *
                 * Audio-only descriptors pass through unchanged (the
                 * AJA audio path accepts the wire format the SDK
                 * pumps).  Non-image, non-audio descriptors are
                 * left alone.
                 */
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                /**
                 * @brief Binds an external pacing clock to a sink channel.
                 *
                 * In source mode, returns @c Error::NotSupported — the
                 * capture cadence is driven by the incoming SDI signal
                 * and the user can't meaningfully replace it.
                 *
                 * In sink mode, a non-null @c cmd.clock binds the sink
                 * to that clock via a @ref PacingGate so subsequent
                 * @c executeCmd(Write) calls advance the timeline by
                 * one frame interval and submit to AutoCirculate at
                 * the gate's deadline (capped at the card's own VBI
                 * cadence by the AutoCirculate pre-buffer).  A null
                 * @c cmd.clock unbinds and reverts to the card's
                 * self-paced behaviour — playout flows as fast as
                 * AutoCirculate can accept frames again.  Pacing
                 * thresholds come from @ref MediaConfig::Ntv2PaceSkipThresholdMs
                 * and @ref MediaConfig::Ntv2PaceReanchorThresholdMs.
                 */
                Error executeCmd(MediaIOCommandSetClock &cmd) override;
                void  cancelBlockingWork() override;

        private:
                Error openSource(const MediaIO::Config &cfg, const MediaDesc &md);
                void  closeSource();
                void  captureLoop();

                Error openSink(const MediaIO::Config &cfg, const MediaDesc &md);
                void  closeSink();
                void  playoutLoop();

                // Apply the per-channel SDI signal routing for the
                // resolved link standard.  Single-link, quad-link 2SI,
                // quad-link Squares, and 12G single-link all flow
                // through @ref Ntv2Routing::sdiInputConnections /
                // @ref Ntv2Routing::sdiOutputConnections — the helper
                // applies each crosspoint via @c CNTV2Card::Connect and
                // toggles the TSI / Squares enable bit beforehand when
                // the standard requires it.  Dual-link and SL_24G are
                // not yet implemented; an empty connection list bubbles
                // up as @c Error::NotSupported.
                //
                // The on-board CSC widgets bridge framebuffer-vs-wire
                // colour-family mismatches inside the routing fabric
                // when @c routingCfg.allowOnBoardCsc is @c true and the
                // wire and framebuffer differ in colour family —
                // wired up by the caller from
                // @c MediaConfig::Ntv2DisableOnBoardCsc and the
                // framebuffer / wire colour models.
                Error routeSdiInput(const Ntv2Routing::Config &routingCfg);
                Error routeSdiOutput(const Ntv2Routing::Config &routingCfg);

                // Sink VPID stamping (Phase 6.4): resolves
                // transfer / colorimetry / luminance / RGB range
                // from the open-time @ref ImageDesc colour model +
                // any @c MediaConfig::Ntv2Vpid*Override + per-frame
                // @c Metadata::Video* keys when present, then writes
                // the AJA per-channel VPID overrides via
                // @c CNTV2Card::SetSDIOutVPID*.  Called from
                // @ref openSink with @p md = the resolved sink
                // ImageDesc; the per-frame nudge path is currently
                // not wired (HDR signalling is generally a session-
                // level decision — switching mid-stream wants a
                // device-wide re-init).  Quietly skips when
                // @c Ntv2VpidEnable is false.
                Error applySinkVpid(const MediaIO::Config &cfg, const ImageDesc &md);

                // Source VPID detection (Phase 6.4): reads the
                // incoming SDI VPID via @c CNTV2Card::ReadSDIInVPID
                // for the channel's primary input port, parses byte
                // 4 (transfer / colorimetry / RGB range), and
                // updates @ref _vpidLast* state.  Called at open
                // time (so the first captured Frame already carries
                // the wire's colour claim) and during the periodic
                // signal-loss poll (so mid-stream HDR transitions
                // are picked up).  No-op when @c Ntv2VpidEnable is
                // false or the channel has no SDI input port.
                void  pollSourceVpid();

                // Worker thread classes — capture and playout each have
                // their own @ref Thread subclass so the library's
                // OS-name / TID-tracking / clean-join machinery wraps
                // them instead of bare @c std::thread.  The bodies
                // delegate straight back to captureLoop / playoutLoop
                // on the owning MediaIO.
                class CaptureWorker : public Thread {
                                public:
                                        CaptureWorker(Ntv2MediaIO *owner) : _owner(owner) {}

                                protected:
                                        void run() override { _owner->captureLoop(); }

                                private:
                                        Ntv2MediaIO *_owner;
                };

                class PlayoutWorker : public Thread {
                                public:
                                        PlayoutWorker(Ntv2MediaIO *owner) : _owner(owner) {}

                                protected:
                                        void run() override { _owner->playoutLoop(); }

                                private:
                                        Ntv2MediaIO *_owner;
                };

                // Device handle owned by the registry; non-null between
                // openSource / openSink and the matching close.
                // Released on close.
                Ntv2Device *_device = nullptr;

                // 1-based logical channel index — matches the SDK's
                // NTV2_CHANNEL1..  numbering minus one for the enum
                // cast.
                int _channel = 0;

                // 1-based audio system index, or 0 when audio is
                // disabled for this channel.
                int _audioSystem = 0;

                // Direction flag set at openSource / openSink so the
                // common close path can route to the right teardown.
                bool _sinkMode = false;

                // Per-port reservation list captured at open so the
                // close path can hand it back to the device.
                List<VideoPortRef> _reservedPorts;

                // Capture pipeline state.  The queue carries fully-
                // built Frames (video + optional ANC) so the capture
                // thread keeps ownership of payload assembly and the
                // strand's executeCmd(Read) is a pure drain.
                CaptureWorker        _captureWorker{this};
                Atomic<bool>         _stopFlag{false};
                Atomic<bool>         _readCancelled{false};
                static constexpr int VideoQueueDepth = 2;
                Queue<Frame>         _videoQueue;

                // Playout pipeline state.  The strand pushes onto
                // @ref _writeQueue inside executeCmd(Write); the
                // playout thread drains and submits via AutoCirculate.
                // Queue depth is chosen larger than the card's pre-
                // buffer (3 frames) so steady-state writers don't
                // immediately back-pressure during the initial fill.
                // Like the capture path, the queue carries full Frames
                // so the playout thread can find any ANC payloads
                // attached alongside the video.
                PlayoutWorker        _playoutWorker{this};
                static constexpr int WriteQueueDepth = 4;
                Queue<Frame>         _writeQueue;

                // ANC capture / insertion state.  Buffers stay
                // resident for the lifetime of the open so we don't
                // pay an allocator round-trip on every frame.  Empty
                // when ANC is disabled (Ntv2WithAnc=false or card
                // missing the custom-ANC engine).
                bool   _ancEnabled = false;
                Buffer _ancF1Buf;
                Buffer _ancF2Buf;
                // Cached f2 start-line for interlaced playout — set
                // at open from the resolved video standard.
                uint16_t _f2StartLine = 0;

                // Per-frame VBI poll timeout used by both worker loops'
                // WaitForInputVerticalInterrupt /
                // WaitForOutputVerticalInterrupt call.  Configurable
                // via MediaConfig::Ntv2VbiTimeoutMs.
                int _vbiTimeoutMs = 50;

                // Input-signal poll cadence (in VBIs) used by the
                // capture worker — every Nth VBI the worker re-queries
                // GetInputVideoFormat to catch a signal-loss or
                // re-acquire event.  Zero disables the poll.  Set from
                // @c MediaConfig::Ntv2SignalPollIntervalVbi at open.
                int _signalPollIntervalVbi = 15;

                // Latched signal-presence state.  The capture worker
                // toggles this and emits @c errorOccurredSignal +
                // increments @c _signalLossCount on every transition
                // from "present" to "absent"; on the inverse
                // transition it logs a re-acquire info line +
                // increments @c _signalReacquiredCount.  Treat as
                // strictly thread-confined to the capture worker —
                // the stats getters read the counters via @ref Atomic.
                bool _signalPresent = true;

                // VPID (SMPTE ST 352) state.  When @c _vpidEnabled is
                // true the sink open path writes byte-4 overrides for
                // transfer / colorimetry / luminance / RGB range, and
                // the source open path reads input VPID and stamps
                // the detected colour description on captured Frames.
                // _vpidLast{Transfer,Colorimetry,Range} are the most
                // recent decoded values from the source side and are
                // re-stamped on every captured frame so consumers see
                // the wire's claim — thread-confined to the capture
                // worker (they're read/written only there).
                bool _vpidEnabled = true;
                int  _vpidLastTransfer    = 0; // NTV2VPIDTransferCharacteristics
                int  _vpidLastColorimetry = 0; // NTV2VPIDColorimetry
                int  _vpidLastRange       = 0; // NTV2VPIDRGBRange
                bool _vpidLastValid       = false;

                // Resolved descriptors / rate populated at open time.
                ImageDesc _imageDesc;
                FrameRate _frameRate;
                AudioDesc _audioDesc;

                // Device sample clock bound to the port group — kept
                // alive past close in case downstream consumers still
                // hold a Clock::Ptr to it.
                Clock::Ptr _sourceClock;

                // Sink-side external pacing (Phase 6).  When a non-
                // null Clock::Ptr is bound through executeCmd(SetClock)
                // the playout worker waits on @ref _paceGate before
                // submitting each frame to AutoCirculate.  Without an
                // external clock the gate stays unbound and the
                // playout worker falls through to the card's self-
                // pacing (which is the Phase-2 default).
                PacingGate _paceGate;
                // Read by the playout worker on every loop iteration
                // to decide whether to wait on @ref _paceGate.  Flipped
                // by executeCmd(SetClock).  Atomic so the playout
                // thread reads the latest bind state without a mutex.
                Atomic<bool> _paceClockExternal{false};

                // Telemetry.
                Atomic<int64_t> _framesReceived{0};
                Atomic<int64_t> _framesDropped{0};
                Atomic<int64_t> _audioBytesReceived{0};
                Atomic<int64_t> _framesPlayed{0};
                Atomic<int64_t> _framesDroppedSink{0};
                Atomic<int64_t> _ancPacketsReceived{0};
                Atomic<int64_t> _ancPacketsSent{0};
                Atomic<int64_t> _signalLossCount{0};
                Atomic<int64_t> _signalReacquiredCount{0};
};

/**
 * @brief @ref MediaIOFactory for the AJA NTV2 backend.
 * @ingroup proav
 *
 * URL scheme: @c ntv2.  Forms:
 *
 *  - @c ntv2://0/1                    — channel 1 on device index 0.
 *  - @c ntv2://kona5/2                — channel 2 on the first Kona 5.
 *  - @c ntv2://serial:8675309/1       — channel 1 on the card with serial 8675309.
 *  - @c ntv2:///1                     — channel 1 on device 0 (implicit).
 *
 * The URL never names physical ports — port assignment lives in
 * the @ref MediaConfig::SdiInputSignal / @ref MediaConfig::SdiOutputSignal
 * / @ref MediaConfig::HdmiInputSignal / @ref MediaConfig::HdmiOutputSignal
 * keys, because one logical channel can own anywhere from 1 to 4
 * physical ports depending on the link standard.
 */
class Ntv2Factory : public MediaIOFactory {
        public:
                Ntv2Factory() = default;

                String name() const override { return String("Ntv2"); }
                String displayName() const override { return String("AJA NTV2"); }
                String description() const override {
                        return String("AJA NTV2 SDI / HDMI capture and playout via libajantv2");
                }

                bool canBeSource() const override { return true; }
                bool canBeSink() const override { return true; }

                StringList schemes() const override { return {String("ntv2")}; }
                bool       canHandlePath(const String &path) const override;
                StringList enumerate() const override;
                Error      urlToConfig(const Url &url, Config *outConfig) const override;

                Config::SpecMap configSpecs() const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
