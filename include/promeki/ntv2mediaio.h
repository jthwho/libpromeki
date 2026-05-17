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
#include <promeki/clock.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiostats.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/queue.h>
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
                 * @brief Rejects external pacing for now.
                 *
                 * Phase-2 sinks let the card pace itself off its own
                 * reference; an external @ref Clock binding is a
                 * Phase-6 feature.  Always returns @c Error::NotSupported.
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

                // Apply the per-channel single-link SDI signal routing.
                // Source: FrameBufferN_Input ← SDIInN.
                // Sink:   SDIOutN_Input    ← FrameBufferNYUV (or RGB).
                // Phase 5 generalises both to dual / quad / 12G tables.
                Error routeSdiInput(int channel);
                Error routeSdiOutput(int channel, bool rgbFraming);

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

                // Capture pipeline state.
                CaptureWorker        _captureWorker{this};
                Atomic<bool>         _stopFlag{false};
                Atomic<bool>         _readCancelled{false};
                static constexpr int VideoQueueDepth = 2;
                Queue<UncompressedVideoPayload::Ptr> _videoQueue;

                // Playout pipeline state.  The strand pushes onto
                // @ref _writeQueue inside executeCmd(Write); the
                // playout thread drains and submits via AutoCirculate.
                // Queue depth is chosen larger than the card's pre-
                // buffer (3 frames) so steady-state writers don't
                // immediately back-pressure during the initial fill.
                PlayoutWorker        _playoutWorker{this};
                static constexpr int WriteQueueDepth = 4;
                Queue<UncompressedVideoPayload::Ptr> _writeQueue;

                // Per-frame VBI poll timeout used by both worker loops'
                // WaitForInputVerticalInterrupt /
                // WaitForOutputVerticalInterrupt call.  Configurable
                // via MediaConfig::Ntv2VbiTimeoutMs.
                int _vbiTimeoutMs = 50;

                // Resolved descriptors / rate populated at open time.
                ImageDesc _imageDesc;
                FrameRate _frameRate;
                AudioDesc _audioDesc;

                // Device sample clock bound to the port group — kept
                // alive past close in case downstream consumers still
                // hold a Clock::Ptr to it.
                Clock::Ptr _sourceClock;

                // Telemetry.
                Atomic<int64_t> _framesReceived{0};
                Atomic<int64_t> _framesDropped{0};
                Atomic<int64_t> _audioBytesReceived{0};
                Atomic<int64_t> _framesPlayed{0};
                Atomic<int64_t> _framesDroppedSink{0};
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
