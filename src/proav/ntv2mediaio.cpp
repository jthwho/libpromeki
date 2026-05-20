/**
 * @file      ntv2mediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <promeki/ntv2mediaio.h>

#include <chrono>
#include <cstring>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/clock.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/logger.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioport.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/ntv2anc.h>
#include <promeki/ntv2clock.h>
#include <promeki/ntv2device.h>
#include <promeki/ntv2format.h>
#include <promeki/ntv2routing.h>
#include <promeki/ntv2vpid.h>
#include <promeki/sdiwireinference.h>
#include <promeki/system.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <promeki/url.h>
#include <promeki/videoformat.h>

#include <ntv2card.h>
#include <ntv2devicescanner.h>
#include <ntv2enums.h>
#include <ntv2formatdescriptor.h>
#include <ntv2publicinterface.h>
#include <ntv2utils.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(Ntv2Factory)

namespace {

        // Default Phase-1 capture pixel format when the caller doesn't
        // specify one through MediaConfig::VideoPixelFormat.  UYVY is
        // the lowest-common-denominator 8-bit YCbCr 4:2:2 packing every
        // AJA card supports, mirroring the demo capture's default.
        constexpr PixelFormat::ID kDefaultCapturePixelFormat = PixelFormat::YUV8_422_UYVY_Rec709;

} // namespace

// ============================================================================
// Ntv2MediaIO
// ============================================================================

Ntv2MediaIO::Ntv2MediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {}

Ntv2MediaIO::~Ntv2MediaIO() {
        if (isOpen()) (void)close().wait();
}

void Ntv2MediaIO::cancelBlockingWork() {
        _readCancelled.setValue(true);
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        Enum modeEnum      = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();
        _sinkMode          = isWrite;

        Error err = isWrite ? openSink(cfg, cmd.pendingMediaDesc)
                            : openSource(cfg, cmd.pendingMediaDesc);
        if (err.isError()) return err;

        // Build the resolved descriptor returned through the framework
        // cache.  Source mode populates ImageDesc + FrameRate at open;
        // audio remains absent in Phase 1 (Ntv2WithAudio support lands
        // in Phase 2).
        MediaDesc resolved = cmd.pendingMediaDesc;
        if (resolved.imageList().isEmpty() && _imageDesc.isValid()) {
                resolved.imageList().pushToBack(_imageDesc);
        }
        if (_frameRate.isValid()) resolved.setFrameRate(_frameRate);

        // Bind the device sample clock to the port group so downstream
        // consumers get per-card-shared timestamps without any extra
        // setup.  Two channels on the same card return the same
        // Clock::Ptr from sampleClock(), which makes
        // port_group_A->clock() == port_group_B->clock() true.  The
        // sink side gets the same clock — even without external
        // pacing, the playout-side counter is the authoritative wall
        // time at the SDI connector for the frames we just submitted.
        _sourceClock = _device->sampleClock();
        MediaIOPortGroup *group = addPortGroup(String("ntv2"), _sourceClock);
        if (group == nullptr) {
                if (isWrite) closeSink(); else closeSource();
                return Error::Invalid;
        }
        group->setFrameRate(_frameRate);
        group->setCanSeek(false);
        group->setFrameCount(MediaIO::FrameCountInfinite);
        if (isWrite) {
                if (addSink(group, resolved) == nullptr) {
                        closeSink();
                        return Error::Invalid;
                }
        } else {
                if (addSource(group, resolved) == nullptr) {
                        closeSource();
                        return Error::Invalid;
                }
        }

        cmd.frameCount = MediaIO::FrameCountInfinite;
        cmd.frameRate  = _frameRate;
        cmd.mediaDesc  = resolved;
        return Error::Ok;
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_device == nullptr) return Error::Ok;
        if (_sinkMode) closeSink(); else closeSource();
        return Error::Ok;
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (_device == nullptr) return Error::NotOpen;

        // Mirror the NDI / V4L2 blocking-pop pattern: short poll
        // boundaries so cancelBlockingWork breaks us out without
        // bounding throughput in steady state.
        constexpr unsigned int kReadPollMs = 100;
        Frame                  frame;
        for (;;) {
                auto popResult = _videoQueue.pop(kReadPollMs);
                if (popResult.second().isOk()) {
                        frame = std::move(popResult.first());
                        break;
                }
                if (_readCancelled.value()) {
                        return Error::Cancelled;
                }
        }

        cmd.frame        = frame;
        cmd.currentFrame = FrameNumber{_framesReceived.value()};
        return Error::Ok;
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (_device == nullptr || !_sinkMode) return Error::NotOpen;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;

        // Validate at least one uncompressed video payload is on the
        // frame — sink mode requires it (compressed needs a decode
        // stage upstream, same shape as NDI's sink path).  The full
        // frame (including any AncPayloads) is then queued so the
        // playout thread can emit both video and ANC together.
        auto vids = cmd.frame.videoPayloads();
        if (vids.isEmpty() || !vids[0].isValid()) {
                return Error::InvalidArgument;
        }
        if (!sharedPointerCast<UncompressedVideoPayload>(vids[0]).isValid()) {
                promekiErr("Ntv2MediaIO: sink only accepts uncompressed video — "
                           "insert a decoder upstream");
                return Error::NotSupported;
        }

        // Bounded queue: drop the oldest queued frame when full so
        // back-pressure surfaces as visible drops rather than blocking
        // the strand worker.  WriteQueueDepth (4) sits comfortably
        // above the card's 3-frame pre-buffer.
        while (_writeQueue.size() >= WriteQueueDepth) {
                auto dropped = _writeQueue.tryPop();
                if (dropped.second().isOk()) {
                        _framesDroppedSink.fetchAndAdd(1);
                        noteFrameDropped(portGroup(0));
                } else {
                        break;
                }
        }
        _writeQueue.push(cmd.frame);

        cmd.currentFrame = FrameNumber{_framesPlayed.value() + 1};
        cmd.frameCount   = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandSetClock &cmd) {
        // Source-mode capture is paced by the incoming SDI signal —
        // there is no host-controllable cadence to rebind, so reject
        // the request rather than silently accept it (a "successful"
        // bind that does nothing would be more confusing).
        if (!_sinkMode) return Error::NotSupported;

        if (cmd.clock.isValid()) {
                // PacingGate::setClock rearms internally — the next
                // playout-worker wait latches a fresh anchor against
                // the new clock's now().  We refresh the period and
                // thresholds too in case the frame rate or config
                // changed since the last bind.
                _paceGate.setClock(cmd.clock);
                if (_frameRate.isValid()) {
                        _paceGate.setPeriod(_frameRate.frameDuration());
                }
                const int skipMs = config().getAs<int32_t>(
                        MediaConfig::Ntv2PaceSkipThresholdMs, int32_t(0));
                if (skipMs > 0) {
                        _paceGate.setSkipThreshold(Duration::fromMilliseconds(skipMs));
                }
                const int reanchorMs = config().getAs<int32_t>(
                        MediaConfig::Ntv2PaceReanchorThresholdMs, int32_t(0));
                if (reanchorMs > 0) {
                        _paceGate.setReanchorThreshold(Duration::fromMilliseconds(reanchorMs));
                }
                _paceClockExternal.setValue(true);
                promekiInfo("Ntv2MediaIO: external pacing clock bound (channel %d, period=%s)",
                            _channel,
                            _frameRate.isValid() ? _frameRate.frameDuration().toString().cstr() : "n/a");
        } else {
                // Unbind: revert to the card's self-paced behaviour
                // (Phase-2 default).  Detach the clock from the gate
                // too so any stale anchor state is dropped — the next
                // bind starts cleanly.
                _paceClockExternal.setValue(false);
                _paceGate.setClock(Clock::Ptr());
                promekiInfo("Ntv2MediaIO: external pacing clock unbound; falling back to card-paced playout (channel %d)",
                            _channel);
        }
        return Error::Ok;
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesReceived, _framesReceived.value());
        cmd.stats.set(StatsFramesDropped, _framesDropped.value());
        cmd.stats.set(StatsAudioBytesReceived, _audioBytesReceived.value());
        cmd.stats.set(StatsFramesPlayed, _framesPlayed.value());
        cmd.stats.set(StatsFramesDroppedSink, _framesDroppedSink.value());
        cmd.stats.set(StatsAncPacketsReceived, _ancPacketsReceived.value());
        cmd.stats.set(StatsAncPacketsSent, _ancPacketsSent.value());
        cmd.stats.set(StatsSignalLoss, _signalLossCount.value());
        cmd.stats.set(StatsSignalReacquired, _signalReacquiredCount.value());
        cmd.stats.set(StatsPacingTicksOnTime, _paceGate.ticksOnTime());
        cmd.stats.set(StatsPacingTicksLate, _paceGate.ticksLate());
        cmd.stats.set(StatsPacingTicksSkipped, _paceGate.ticksSkipped());
        cmd.stats.set(StatsPacingReanchors, _paceGate.reanchors());
        cmd.stats.set(StatsDeviceLost, _deviceLostCount.value());
        const int64_t qDepth    = _sinkMode ? static_cast<int64_t>(_writeQueue.size())
                                            : static_cast<int64_t>(_videoQueue.size());
        const int64_t qCapacity = _sinkMode ? static_cast<int64_t>(WriteQueueDepth)
                                            : static_cast<int64_t>(VideoQueueDepth);
        cmd.stats.set(MediaIOStats::QueueDepth, qDepth);
        cmd.stats.set(MediaIOStats::QueueCapacity, qCapacity);
        return Error::Ok;
}

// ---- openSource ----

Error Ntv2MediaIO::openSource(const MediaIO::Config &cfg, const MediaDesc &md) {
        const int     deviceIndex    = cfg.getAs<int32_t>(MediaConfig::Ntv2DeviceIndex, int32_t(-1));
        const String  deviceName     = cfg.getAs<String>(MediaConfig::Ntv2DeviceName, String());
        const int     requestedCh    = cfg.getAs<int32_t>(MediaConfig::Ntv2Channel, int32_t(1));
        const int     requestedAudio = cfg.getAs<int32_t>(MediaConfig::Ntv2AudioSystem, int32_t(-1));
        const bool    retailServices = cfg.getAs<bool>(MediaConfig::Ntv2RetailServices, false);
        const bool    multiFormat    = cfg.getAs<bool>(MediaConfig::Ntv2MultiFormatMode, true);
        _vbiTimeoutMs                = cfg.getAs<int32_t>(MediaConfig::Ntv2VbiTimeoutMs, int32_t(50));
        _signalPollIntervalVbi       = cfg.getAs<int32_t>(MediaConfig::Ntv2SignalPollIntervalVbi,
                                                          int32_t(15));

        // ---- Acquire the device ----
        Error err = Ntv2DeviceRegistry::instance().acquire(
                deviceIndex, deviceName, retailServices, multiFormat, &_device);
        if (err.isError()) return err;
        if (_device == nullptr) return Error::DeviceNotFound;

        // ---- Reserve channel + ports + audio system ----
        _channel = requestedCh;
        err      = _device->reserveChannel(_channel, this);
        if (err.isError()) {
                Ntv2DeviceRegistry::instance().release(_device);
                _device = nullptr;
                return err;
        }

        // SDI port reservation: Phase 1 expects exactly one SDI port
        // for single-link capture.  When the caller didn't pass an
        // SdiInputSignal key, default to the SDI port matching the
        // channel index.  More elaborate (HDMI, dual-link, quad-link)
        // routing arrives in Phases 3 / 5.
        SdiSignalConfig sdiSignal = cfg.getAs<SdiSignalConfig>(MediaConfig::SdiInputSignal,
                                                               SdiSignalConfig());
        if (sdiSignal.ports().isEmpty()) {
                sdiSignal = SdiSignalConfig::singleLink(
                        SdiLinkStandard::Auto,
                        VideoPortRef(VideoConnectorKind::Sdi, _channel));
        }
        _reservedPorts = sdiSignal.ports();
        err = _device->reservePorts(_reservedPorts, this);
        if (err.isError()) {
                _device->releaseChannel(_channel, this);
                Ntv2DeviceRegistry::instance().release(_device);
                _device = nullptr;
                return err;
        }

        // Audio system: -1 auto-pairs with the channel; 0 disables;
        // 1..N is explicit.  Phase 1 leaves audio passthrough turned
        // off — we reserve the system here so the device clock can
        // pick it up but don't yet thread audio frames into the
        // captured payload.
        if (requestedAudio < 0) {
                _audioSystem = _channel;
        } else {
                _audioSystem = requestedAudio;
        }
        if (_audioSystem > 0 && _audioSystem <= _device->caps().audioSystemCount()) {
                err = _device->reserveAudioSystem(_audioSystem, this);
                if (err.isError()) {
                        // Audio reservation conflict is not fatal —
                        // fall back to disabled audio for this channel
                        // (the user can request a different system
                        // explicitly).
                        promekiWarn("Ntv2MediaIO: audio system %d unavailable; capture continues video-only",
                                    _audioSystem);
                        _audioSystem = 0;
                }
        } else if (_audioSystem > _device->caps().audioSystemCount()) {
                promekiWarn("Ntv2MediaIO: audio system %d exceeds device capacity (%d); audio disabled",
                            _audioSystem, _device->caps().audioSystemCount());
                _audioSystem = 0;
        }

        // ---- Apply reference clock (best-effort) ----
        const VideoReferenceConfig refCfg = cfg.getAs<VideoReferenceConfig>(
                MediaConfig::VideoReference, VideoReferenceConfig());
        _device->setReference(refCfg, this);

        // ---- Set up the SDI receiver path ----
        //
        // The device mutex is held across the cross-channel register
        // pokes below (channel enable, format/route programming,
        // bi-SDI direction).  Every error branch sets @c configErr and
        // breaks out of the scope so the mutex releases naturally
        // before any @c closeSource() call — @c closeSource also takes
        // the mutex internally, and @ref Mutex is non-recursive.
        CNTV2Card        &card    = _device->card();
        const NTV2Channel ntv2Ch  = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));
        Error             configErr = Error::Ok;
        Size2Du32         detectedSize;
        FrameRate         detectedRate;
        VideoScanMode     detectedScan = VideoScanMode::Unknown;
        PixelFormat       pf;
        NTV2VideoFormat   videoFormat = NTV2_FORMAT_UNKNOWN;
        do {
                Mutex::Locker lk(_device->mutex());

                card.EnableChannel(ntv2Ch);
                card.EnableInputInterrupt(ntv2Ch);
                card.SubscribeInputVerticalEvent(ntv2Ch);

                if (_device->caps().hasBiDirectionalSdi()) {
                        // Default the channel's SDI connector to receive direction.
                        card.SetSDITransmitEnable(ntv2Ch, false);
                        card.WaitForOutputVerticalInterrupt(NTV2_CHANNEL1, 10);
                }

                // ---- Determine the input video format ----
                const NTV2InputSource ntv2Src = static_cast<NTV2InputSource>(
                        Ntv2Format::portToInputSource(_reservedPorts[0]));
                videoFormat = card.GetInputVideoFormat(ntv2Src);
                if (videoFormat == NTV2_FORMAT_UNKNOWN) {
                        promekiErr("Ntv2MediaIO: no input signal on %s",
                                   _reservedPorts[0].toString().cstr());
                        configErr = Error::NotReady;
                        break;
                }
                if (!card.features().CanDoVideoFormat(videoFormat)) {
                        promekiErr("Ntv2MediaIO: device cannot handle detected video format");
                        configErr = Error::NotSupported;
                        break;
                }

                if (Ntv2Format::fromNtv2VideoFormat(videoFormat, &detectedSize, &detectedRate,
                                                   &detectedScan)
                            .isError()) {
                        promekiErr("Ntv2MediaIO: detected NTV2 video format not in mapping table");
                        configErr = Error::NotSupported;
                        break;
                }

                // Pixel format: the caller's preference if given,
                // otherwise UYVY as the safe default.  Formats the card
                // doesn't support get rejected outright; the planner
                // can wire a CSC bridge upstream.
                pf = md.imageList().isEmpty() ? PixelFormat() : md.imageList()[0].pixelFormat();
                if (!pf.isValid()) {
                        pf = PixelFormat(kDefaultCapturePixelFormat);
                }
                if (!_device->caps().supportsPixelFormat(pf.id())) {
                        promekiErr("Ntv2MediaIO: pixel format '%s' unsupported by this card; "
                                   "ask planner for a CSC bridge first",
                                   pf.name().cstr());
                        configErr = Error::FormatMismatch;
                        break;
                }
                const NTV2FrameBufferFormat fbf = static_cast<NTV2FrameBufferFormat>(
                        Ntv2Format::toNtv2PixelFormat(pf.id()));

                // Push the format settings to the card.  SetVideoFormat
                // implicitly programs the framestore standard /
                // geometry; we explicitly disable VANC because this
                // Phase doesn't handle it.
                card.SetVideoFormat(videoFormat, false, false, ntv2Ch);
                card.SetVANCMode(NTV2_VANCMODE_OFF, ntv2Ch);
                card.SetFrameBufferFormat(ntv2Ch, fbf);

                // ---- Signal routing ----
                // Resolve the link standard from the caller's config
                // (sdiSignal) and assemble the Ntv2Routing::Config so
                // the routing helper can insert on-board CSC widgets
                // when the framestore and wire colour families differ
                // (and the user hasn't disabled the CSC widgets via
                // @c Ntv2DisableOnBoardCsc).  SDI is YCbCr on the
                // wire today; HDMI / dual-link RGB paths will flip
                // signalRgb in a future iteration.
                Ntv2Routing::Config rcfg;
                rcfg.standard         = sdiSignal.standard();
                rcfg.channelStart     = _channel;
                rcfg.portStart        = _reservedPorts[0].index();
                rcfg.can12gRouting    = card.features().CanDo12gRouting();
                rcfg.framebufferRgb   = pf.colorModel().type() != ColorModel::TypeYCbCr;
                rcfg.signalRgb        = false;
                rcfg.allowOnBoardCsc =
                        !cfg.getAs<bool>(MediaConfig::Ntv2DisableOnBoardCsc, false)
                        && _device->caps().cscCount() > 0;
                configErr = routeSdiInput(rcfg);
                if (configErr.isError()) break;
        } while (false);
        if (configErr.isError()) {
                closeSource();
                return configErr;
        }

        // Populate the cached descriptors.
        _imageDesc = ImageDesc(detectedSize, pf);
        _imageDesc.setVideoScanMode(detectedScan);
        _frameRate = detectedRate;

        promekiInfo("Ntv2MediaIO: channel %d on '%s' opened — %ux%u @ %u/%u (%s scan), pf=%s",
                    _channel, _device->displayName().cstr(),
                    detectedSize.width(), detectedSize.height(),
                    detectedRate.numerator(), detectedRate.denominator(),
                    detectedScan.toString().cstr(), pf.name().cstr());

        // ---- Optional ANC capture engine ----
        // Honour the user's Ntv2WithAnc preference but downgrade to
        // off when the card lacks the custom-ANC firmware.  The
        // resident F1/F2 host buffers are allocated once here so the
        // capture loop doesn't take the allocator on the hot path.
        const bool requestAnc = cfg.getAs<bool>(MediaConfig::Ntv2WithAnc, true);
        _ancEnabled           = requestAnc && _device->caps().canDoCustomAnc();
        if (requestAnc && !_ancEnabled) {
                promekiInfo("Ntv2MediaIO: ANC requested but card '%s' lacks the custom-ANC engine — ANC disabled",
                            _device->displayName().cstr());
        }
        if (_ancEnabled) {
                _ancF1Buf = Buffer(Ntv2Anc::kPreferredBufBytes);
                _ancF2Buf = Buffer(Ntv2Anc::kPreferredBufBytes);
                if (!_ancF1Buf.isValid() || !_ancF2Buf.isValid()) {
                        promekiWarn("Ntv2MediaIO: ANC buffer allocation failed; capture continues without ANC");
                        _ancEnabled = false;
                        _ancF1Buf   = Buffer();
                        _ancF2Buf   = Buffer();
                } else {
                        // Buffer(N) reserves N bytes but leaves size()
                        // at zero; AJA's NTV2Buffer copies the size on
                        // wrap, so we have to mark the buffers fully
                        // populated for SetAncBuffers to convey the
                        // right capacity to the driver.
                        _ancF1Buf.setSize(Ntv2Anc::kPreferredBufBytes);
                        _ancF2Buf.setSize(Ntv2Anc::kPreferredBufBytes);
                }
        }

        // ---- Start AutoCirculate + spawn capture thread ----
        const NTV2AudioSystem ntv2Aud = _audioSystem > 0
                ? static_cast<NTV2AudioSystem>(_audioSystem - 1)
                : NTV2_AUDIOSYSTEM_INVALID;
        ULWord acOptions = AUTOCIRCULATE_WITH_RP188;
        if (_ancEnabled) acOptions |= AUTOCIRCULATE_WITH_ANC;
        card.AutoCirculateStop(ntv2Ch);
        if (!card.AutoCirculateInitForInput(ntv2Ch, /*inNumChannels*/ 0, ntv2Aud, acOptions)) {
                promekiErr("Ntv2MediaIO: AutoCirculateInitForInput failed for channel %d", _channel);
                closeSource();
                return Error::DeviceError;
        }
        if (!card.AutoCirculateStart(ntv2Ch)) {
                promekiErr("Ntv2MediaIO: AutoCirculateStart failed for channel %d", _channel);
                closeSource();
                return Error::DeviceError;
        }

        _stopFlag.setValue(false);
        _readCancelled.setValue(false);
        _framesReceived.setValue(0);
        _framesDropped.setValue(0);
        _audioBytesReceived.setValue(0);
        _ancPacketsReceived.setValue(0);
        _signalLossCount.setValue(0);
        _signalReacquiredCount.setValue(0);
        _deviceLostCount.setValue(0);
        _deviceLost.setValue(false);
        // We just verified a valid input video format above, so the
        // signal-present latch starts true.  The capture worker's
        // periodic poll toggles it on detected loss / recovery.
        _signalPresent = true;

        // VPID (Phase 6.4): seed the source-side decoded values
        // before the capture worker starts so the very first
        // captured frame already carries the wire's colour claim.
        // The periodic signal-poll re-reads VPID so mid-stream
        // HDR transitions also reflect in subsequent frames.
        _vpidEnabled  = cfg.getAs<bool>(MediaConfig::Ntv2VpidEnable, true);
        _vpidLastValid = false;
        pollSourceVpid();

        // Indexed thread name so several capture workers across one or
        // more cards stay distinguishable in `top -H`, `htop`, `ps -L`,
        // and the library's logger.  Linux truncates names to 15
        // chars; "ntv2cap:D:C" fits comfortably through D=99, C=8.
        _captureWorker.setName(
                String::format("ntv2cap:{}:{}", _device->deviceIndex(), _channel));
        _captureWorker.start();
        return Error::Ok;
}

void Ntv2MediaIO::closeSource() {
        if (_device == nullptr) return;

        _stopFlag.setValue(true);
        if (_captureWorker.isRunning()) _captureWorker.wait();

        // Stop AutoCirculate + tear down channel state under the
        // device mutex so we don't race other channels' routing
        // operations on the shared card.
        if (_device->card().IsOpen()) {
                Mutex::Locker     lk(_device->mutex());
                const NTV2Channel ntv2Ch = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));
                _device->card().AutoCirculateStop(ntv2Ch);
                _device->card().UnsubscribeInputVerticalEvent(ntv2Ch);
        }

        // Drop any queued frames so the backing Buffers release while
        // the allocator is still healthy.
        while (true) {
                auto popResult = _videoQueue.tryPop();
                if (popResult.second().isError()) break;
        }

        if (_audioSystem > 0) {
                _device->releaseAudioSystem(_audioSystem, this);
                _audioSystem = 0;
        }
        _device->releasePortsOwnedBy(this);
        _device->releaseChannel(_channel, this);

        Ntv2DeviceRegistry::instance().release(_device);
        _device = nullptr;
        // Keep _sourceClock alive past close — downstream port-group
        // consumers may still hold a Clock::Ptr and would crash on
        // raw() if we tore it down here.

        // Drop the ANC scratch buffers; harmless on the disabled path
        // where they were never allocated.
        _ancEnabled = false;
        _ancF1Buf   = Buffer();
        _ancF2Buf   = Buffer();
}

Error Ntv2MediaIO::routeSdiInput(const Ntv2Routing::Config &routingCfg) {
        if (routingCfg.channelStart < 1 || routingCfg.channelStart > 8
            || routingCfg.portStart < 1 || routingCfg.portStart > 8) {
                return Error::InvalidArgument;
        }

        Ntv2Routing::ConnectionList conns = Ntv2Routing::sdiInputConnections(routingCfg);
        if (conns.isEmpty()) {
                promekiErr("Ntv2MediaIO: link standard '%s' not supported for input routing yet",
                           routingCfg.standard.toString().cstr());
                return Error::NotSupported;
        }

        // Toggle the framestore-grouping bit before applying any
        // crosspoints — the AJA helpers compute crosspoint ids
        // assuming the right grouping mode is already programmed.
        CNTV2Card        &card   = _device->card();
        const NTV2Channel ntv2Ch =
                static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(routingCfg.channelStart));
        if (Ntv2Routing::needsTsi(routingCfg.standard, routingCfg.can12gRouting)) {
                card.SetTsiFrameEnable(true, ntv2Ch);
        } else if (Ntv2Routing::needsSquares(routingCfg.standard)) {
                card.Set4kSquaresEnable(true, ntv2Ch);
        }

        for (size_t i = 0; i < conns.size(); ++i) {
                const Ntv2Routing::Connection &c = conns[i];
                if (!card.Connect(static_cast<NTV2InputCrosspointID>(c.input),
                                  static_cast<NTV2OutputCrosspointID>(c.output),
                                  /*inValidate*/ false)) {
                        promekiErr("Ntv2MediaIO: signal routing Connect(in=0x%x, out=0x%x) failed "
                                   "(standard=%s, channel=%d, port=%d)",
                                   unsigned(c.input), unsigned(c.output),
                                   routingCfg.standard.toString().cstr(), routingCfg.channelStart,
                                   routingCfg.portStart);
                        return Error::DeviceError;
                }
        }
        return Error::Ok;
}

// ---- Capture loop ----

void Ntv2MediaIO::captureLoop() {
        if (_device == nullptr || !_imageDesc.isValid()) return;
        CNTV2Card        &card  = _device->card();
        const NTV2Channel ntv2Ch = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));

        // NTV2FormatDescriptor gives us the AJA-correct write size for
        // this format combination (raster + pixel format) — using it
        // beats hand-computing bytes-per-line because some formats
        // (V210, DPX) have non-trivial line strides.
        NTV2FormatDescriptor fmtDesc(static_cast<NTV2VideoFormat>(
                                             Ntv2Format::toNtv2VideoFormat(_imageDesc, _frameRate)),
                                     static_cast<NTV2FrameBufferFormat>(
                                             Ntv2Format::toNtv2PixelFormat(_imageDesc.pixelFormat().id())));
        const ULWord writeSize = fmtDesc.GetVideoWriteSize();
        if (writeSize == 0) {
                promekiErr("Ntv2MediaIO: capture loop got zero write size from NTV2FormatDescriptor");
                return;
        }

        AUTOCIRCULATE_TRANSFER xfer;

        // Signal-loss poll bookkeeping.  We count VBIs across the
        // outer loop and consult GetInputVideoFormat every Nth tick
        // (N = _signalPollIntervalVbi; 0 disables the poll).  The
        // captured input source is cached so we don't reach into
        // _reservedPorts on every iteration.
        const NTV2InputSource ntv2Src =
                _reservedPorts.isEmpty()
                        ? NTV2_INPUTSOURCE_INVALID
                        : static_cast<NTV2InputSource>(
                                Ntv2Format::portToInputSource(_reservedPorts[0]));
        int vbiTickCount = 0;

        while (!_stopFlag.value() && !_deviceLost.value()) {
                AUTOCIRCULATE_STATUS acStatus;
                card.AutoCirculateGetStatus(ntv2Ch, acStatus);
                if (acStatus.IsRunning() && acStatus.HasAvailableInputFrame()) {
                        Buffer buf(writeSize);
                        if (!buf.isValid()) {
                                _framesDropped.fetchAndAdd(1);
                                continue;
                        }
                        xfer.SetVideoBuffer(static_cast<ULWord *>(buf.data()), writeSize);
                        if (_ancEnabled) {
                                // Bind the resident host F1/F2 buffers
                                // for the driver to fill.  We re-bind
                                // every iteration because xfer is a
                                // stack object and SetAncBuffers
                                // mutates internal NTV2Buffer state.
                                xfer.SetAncBuffers(static_cast<ULWord *>(_ancF1Buf.data()),
                                                   static_cast<ULWord>(_ancF1Buf.size()),
                                                   static_cast<ULWord *>(_ancF2Buf.data()),
                                                   static_cast<ULWord>(_ancF2Buf.size()));
                        }
                        if (!card.AutoCirculateTransfer(ntv2Ch, xfer)) {
                                _framesDropped.fetchAndAdd(1);
                                continue;
                        }
                        // Anchor host-side wall clock immediately
                        // after the transfer returns so the wall ↔
                        // VBI back-shift below has the smallest
                        // possible IOCTL-return error.
                        const TimeStamp postXferStrand = TimeStamp::now();

                        buf.setSize(writeSize);
                        BufferView view(buf, 0, writeSize);
                        UncompressedVideoPayload::Ptr vp =
                                UncompressedVideoPayload::Ptr::create(_imageDesc, view);

                        // Per-frame stamps from the AutoCirculate
                        // driver beat anything we could measure
                        // host-side:
                        //
                        //   acAudioClockTimeStamp — 64-bit AJA audio
                        //     sample counter (kRegAud1Counter, extended
                        //     by the driver) at the VBI when the frame
                        //     started ingesting into SDRAM.  Drops
                        //     straight into the device clock's domain
                        //     since our Ntv2DeviceClock reads the same
                        //     register.
                        //
                        //   acFrameTime    — host wall clock (100-ns
                        //     ticks since the Unix epoch on Linux) at
                        //     the same VBI moment.
                        //   acCurrentTime — host wall clock when the
                        //     driver handled AutoCirculateTransfer.
                        //
                        // Both are valid only when AUTOCIRCULATE_WITH_RP188
                        // (or any other "produce a FRAME_STAMP" option)
                        // is set, which we always set at open.
                        const FRAME_STAMP &fs = xfer.GetFrameInfo();
                        if (_sourceClock.isValid() && fs.acAudioClockTimeStamp != 0) {
                                auto *clk = static_cast<Ntv2DeviceClock *>(_sourceClock.modify());
                                MediaTimeStamp pts = clk->mediaTimeStampFromSamples(
                                        static_cast<uint64_t>(fs.acAudioClockTimeStamp));
                                vp.modify()->setPts(pts);
                                vp.modify()->setDts(pts);
                        } else if (_sourceClock.isValid()) {
                                // No driver-reported frame stamp — fall
                                // back to a strand-side now() read so
                                // PTS isn't left empty.  Should never
                                // happen on a healthy AJA driver.
                                Result<MediaTimeStamp> pts = _sourceClock->now();
                                if (pts.second().isOk()) {
                                        vp.modify()->setPts(pts.first());
                                        vp.modify()->setDts(pts.first());
                                }
                        }

                        // CaptureTime: back-shift the host wall anchor
                        // by the driver-reported (acCurrentTime - acFrameTime)
                        // delta so the recorded time tracks the VBI
                        // instant rather than the strand-arrival moment
                        // (which can be milliseconds later under load).
                        // Errors that would otherwise carry "now" by
                        // mistake are bounded to (IOCTL-return latency)
                        // — typically microseconds.
                        MediaTimeStamp captureTime;
                        if (fs.acFrameTime != 0 && fs.acCurrentTime >= fs.acFrameTime) {
                                const int64_t deltaNs =
                                        (static_cast<int64_t>(fs.acCurrentTime)
                                         - static_cast<int64_t>(fs.acFrameTime)) * 100;
                                TimeStamp vbiHost =
                                        postXferStrand - Duration::fromNanoseconds(deltaNs);
                                captureTime = MediaTimeStamp(vbiHost,
                                                             ClockDomain(ClockDomain::SystemMonotonic));
                        } else {
                                captureTime = MediaTimeStamp(postXferStrand,
                                                             ClockDomain(ClockDomain::SystemMonotonic));
                        }
                        vp.modify()->metadata().set(Metadata::CaptureTime, captureTime);

                        // Assemble the outbound Frame.  Video is the
                        // base payload; ANC (when enabled and the
                        // driver actually filled the buffer this
                        // frame) attaches as a peer payload so
                        // downstream stages can read it via
                        // Frame::ancPayloads.
                        Frame frame;
                        frame.addPayload(std::move(vp));
                        if (_frameRate.isValid()) {
                                frame.metadata().set(Metadata::FrameRate, _frameRate);
                        }
                        // VPID-derived colour information rides on
                        // the framestore's @ref PixelFormat (upgraded
                        // by @ref pollSourceVpid when the wire claims
                        // PQ / HLG).  Downstream consumers query
                        // @c imageDesc.pixelFormat().colorModel() via
                        // @ref ColorModel::toH273 for the H.273
                        // codepoints — no per-frame metadata stamping
                        // needed.
                        if (_ancEnabled) {
                                const ULWord f1Sz = xfer.GetCapturedAncByteCount(false);
                                const ULWord f2Sz = xfer.GetCapturedAncByteCount(true);
                                if (f1Sz > 0 || f2Sz > 0) {
                                        AncPayload::Ptr ancPayload = Ntv2Anc::ntv2AncToPackets(
                                                static_cast<const uint8_t *>(_ancF1Buf.data()),
                                                static_cast<size_t>(f1Sz),
                                                static_cast<const uint8_t *>(_ancF2Buf.data()),
                                                static_cast<size_t>(f2Sz), _imageDesc.size(),
                                                _imageDesc.videoScanMode(), _frameRate);
                                        if (ancPayload.isValid() && !ancPayload->packets().isEmpty()) {
                                                _ancPacketsReceived.fetchAndAdd(
                                                        static_cast<int64_t>(ancPayload->packets().size()));
                                                frame.addPayload(ancPayload);
                                        }
                                }
                        }

                        // Bounded queue: drop oldest on overflow so
                        // steady-state latency stays bounded.  Same
                        // policy as NDI / V4L2.
                        while (_videoQueue.size() >= VideoQueueDepth) {
                                auto dropped = _videoQueue.tryPop();
                                if (dropped.second().isOk()) {
                                        _framesDropped.fetchAndAdd(1);
                                } else {
                                        break;
                                }
                        }
                        _videoQueue.push(std::move(frame));
                        _framesReceived.fetchAndAdd(1);
                } else {
                        // No frame ready — block on the VBI interrupt
                        // up to _vbiTimeoutMs ms so we wake quickly
                        // when a frame arrives without spinning in
                        // steady state.
                        card.WaitForInputVerticalInterrupt(ntv2Ch, static_cast<UWord>(_vbiTimeoutMs));
                        if (_sourceClock.isValid()) {
                                // Forward VBI notification to the
                                // device clock for VBI-fallback mode.
                                // No-op in sample-counter mode.
                                auto *clk = static_cast<Ntv2DeviceClock *>(_sourceClock.modify());
                                clk->noteVbi(TimeStamp::now());
                        }
                        ++vbiTickCount;

                        // Periodic input-signal poll: every N VBIs
                        // re-query GetInputVideoFormat and surface
                        // present↔absent transitions as MediaIO
                        // errorOccurred(Error::SignalLoss) on loss
                        // and as an info log on re-acquire.  Skips
                        // entirely when the cadence is zero or no
                        // input port was reserved (defensive — every
                        // open path reserves at least one).
                        if (_signalPollIntervalVbi > 0
                            && ntv2Src != NTV2_INPUTSOURCE_INVALID
                            && vbiTickCount >= _signalPollIntervalVbi) {
                                vbiTickCount = 0;

                                // Driver-restart detection — if the
                                // AJA driver has been unloaded or the
                                // card has been hot-unplugged the
                                // CNTV2Card handle goes invalid.
                                // Surface that as a single
                                // errorOccurredSignal(DeviceError) +
                                // exit the loop cleanly rather than
                                // looping forever on dead IOCTLs.
                                if (!card.IsOpen()) {
                                        _deviceLostCount.fetchAndAdd(1);
                                        _deviceLost.setValue(true);
                                        promekiErr(
                                                "Ntv2MediaIO: device handle invalid mid-capture "
                                                "(driver restart / hot-unplug?) on channel %d",
                                                _channel);
                                        errorOccurredSignal.emit(Error(Error::DeviceError));
                                        break;
                                }
                                const NTV2VideoFormat fmt =
                                        card.GetInputVideoFormat(ntv2Src);
                                const bool nowPresent = fmt != NTV2_FORMAT_UNKNOWN;
                                if (nowPresent != _signalPresent) {
                                        _signalPresent = nowPresent;
                                        if (!nowPresent) {
                                                _signalLossCount.fetchAndAdd(1);
                                                promekiWarn(
                                                        "Ntv2MediaIO: input signal lost on %s "
                                                        "(channel %d)",
                                                        _reservedPorts[0].toString().cstr(),
                                                        _channel);
                                                errorOccurredSignal.emit(Error(Error::SignalLoss));
                                        } else {
                                                _signalReacquiredCount.fetchAndAdd(1);
                                                promekiInfo(
                                                        "Ntv2MediaIO: input signal re-acquired on %s "
                                                        "(channel %d)",
                                                        _reservedPorts[0].toString().cstr(),
                                                        _channel);
                                        }
                                }
                                // Re-read VPID on the same cadence so
                                // mid-stream HDR / WCG transitions
                                // (a producer flipping from SDR to PQ
                                // on the same physical port) are
                                // reflected in subsequent captured
                                // frames' metadata.
                                if (nowPresent) pollSourceVpid();
                        }
                }
        }
}

// ============================================================================
// Sink-mode negotiation
// ============================================================================

Error Ntv2MediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;

        // Pure audio descriptors pass through — the AJA audio path
        // converts to its native wire format internally.  Nothing to
        // negotiate at the image layer.
        if (offered.imageList().isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        // The on-board CSC widgets can bridge a framestore-vs-wire
        // colour-family mismatch (RGB ↔ YCbCr) inside the routing
        // fabric, which is much cheaper than a host-side CSC stage.
        // We accept an upstream's preferred PixelFormat in either
        // colour family as long as it maps to a supported NTV2
        // frame-buffer format and the user hasn't opted out of
        // on-board CSC via @c Ntv2DisableOnBoardCsc.
        //
        // When CSC is disabled or the offered format isn't directly
        // mappable, we fall back to a target.  Two rules:
        //   - CSC enabled  → target matches the offered format's
        //     family (upstream keeps working in its native family;
        //     on-board CSC bridges to the wire family if needed).
        //   - CSC disabled → target matches the wire family so no
        //     on-board CSC is needed at routing time; the planner
        //     either gets a native-wire-family producer upstream or
        //     splices a software CSC.
        //
        // Wire colour family is YCbCr for SDI today (the only
        // transport this backend wires); HDMI / dual-link RGB will
        // flip @c wireYuv via the SdiSignalConfig once those paths
        // ship.
        const PixelFormat &pf = offered.imageList()[0].pixelFormat();
        const bool offeredYuv = pf.isValid() && pf.colorModel().type() == ColorModel::TypeYCbCr;
        const bool wireYuv    = true;
        const bool cscDisabled = config().getAs<bool>(MediaConfig::Ntv2DisableOnBoardCsc, false);
        const bool needsCsc   = (offeredYuv != wireYuv);

        if (pf.isValid() && Ntv2Format::toNtv2PixelFormat(pf.id()) != int(NTV2_FBF_INVALID)
            && (!needsCsc || !cscDisabled)) {
                *preferred = offered;
                return Error::Ok;
        }

        // Fallback target — see decision matrix above.
        bool targetYuv = offeredYuv;
        if (cscDisabled) targetYuv = wireYuv;
        const PixelFormat target(targetYuv ? PixelFormat::YUV8_422_UYVY_Rec709
                                           : PixelFormat::RGB8_sRGB);

        MediaDesc        want = offered;
        ImageDesc::List &imgs = want.imageList();
        for (size_t i = 0; i < imgs.size(); ++i) {
                imgs[i].setPixelFormat(target);
        }
        *preferred = want;
        return Error::Ok;
}

// ============================================================================
// Sink-mode lifecycle
// ============================================================================

Error Ntv2MediaIO::openSink(const MediaIO::Config &cfg, const MediaDesc &md) {
        const int     deviceIndex    = cfg.getAs<int32_t>(MediaConfig::Ntv2DeviceIndex, int32_t(-1));
        const String  deviceName     = cfg.getAs<String>(MediaConfig::Ntv2DeviceName, String());
        const int     requestedCh    = cfg.getAs<int32_t>(MediaConfig::Ntv2Channel, int32_t(1));
        const int     requestedAudio = cfg.getAs<int32_t>(MediaConfig::Ntv2AudioSystem, int32_t(-1));
        const bool    retailServices = cfg.getAs<bool>(MediaConfig::Ntv2RetailServices, false);
        const bool    multiFormat    = cfg.getAs<bool>(MediaConfig::Ntv2MultiFormatMode, true);
        _vbiTimeoutMs                = cfg.getAs<int32_t>(MediaConfig::Ntv2VbiTimeoutMs, int32_t(50));

        // ---- Acquire the device ----
        Error err = Ntv2DeviceRegistry::instance().acquire(
                deviceIndex, deviceName, retailServices, multiFormat, &_device);
        if (err.isError()) return err;
        if (_device == nullptr) return Error::DeviceNotFound;

        // ---- Reserve channel + ports + audio system ----
        _channel = requestedCh;
        err      = _device->reserveChannel(_channel, this);
        if (err.isError()) {
                Ntv2DeviceRegistry::instance().release(_device);
                _device = nullptr;
                return err;
        }

        // SDI output ports.  Three intake shapes:
        //   1. SdiOutputFanout set → fanout: first group is the
        //      primary destination, subsequent groups are mirrors.
        //      All groups share the SdiLinkStandard.
        //   2. SdiOutputSignal set → single destination, no fanout.
        //   3. Neither → default to the SDI port matching _channel.
        SdiOutputFanoutConfig fanout =
                cfg.getAs<SdiOutputFanoutConfig>(MediaConfig::SdiOutputFanout,
                                                 SdiOutputFanoutConfig());
        SdiSignalConfig sdiSignal;
        List<int>       mirrorPortStarts;
        SdiSignalConfig::PortList allPorts;
        if (fanout.groupCount() > 0) {
                if (!fanout.isValid()) {
                        promekiErr("Ntv2MediaIO: SdiOutputFanout has mismatched group sizes "
                                   "for standard '%s'",
                                   fanout.standard().toString().cstr());
                        Ntv2DeviceRegistry::instance().release(_device);
                        _device = nullptr;
                        return Error::InvalidArgument;
                }
                sdiSignal = fanout.primary();
                for (size_t i = 0; i < fanout.groups().size(); ++i) {
                        const SdiOutputFanoutConfig::PortList &grp = fanout.groups().at(i);
                        for (size_t j = 0; j < grp.size(); ++j) allPorts.pushToBack(grp.at(j));
                        if (i >= 1 && !grp.isEmpty()) {
                                mirrorPortStarts.pushToBack(grp.at(0).index());
                        }
                }
        } else {
                sdiSignal = cfg.getAs<SdiSignalConfig>(MediaConfig::SdiOutputSignal,
                                                       SdiSignalConfig());
                if (sdiSignal.ports().isEmpty()) {
                        sdiSignal = SdiSignalConfig::singleLink(
                                SdiLinkStandard::Auto,
                                VideoPortRef(VideoConnectorKind::Sdi, _channel));
                }
                allPorts = sdiSignal.ports();
        }
        _reservedPorts = allPorts;
        err = _device->reservePorts(_reservedPorts, this);
        if (err.isError()) {
                _device->releaseChannel(_channel, this);
                Ntv2DeviceRegistry::instance().release(_device);
                _device = nullptr;
                return err;
        }

        // Audio system: same auto-pair / explicit / disabled rules as
        // source mode.  Phase 2 reserves the system so the device
        // clock can pick it up; audio frame submission is deferred to
        // a follow-up so the playout path stays focused on video.
        if (requestedAudio < 0) {
                _audioSystem = _channel;
        } else {
                _audioSystem = requestedAudio;
        }
        if (_audioSystem > 0 && _audioSystem <= _device->caps().audioSystemCount()) {
                Error audErr = _device->reserveAudioSystem(_audioSystem, this);
                if (audErr.isError()) {
                        promekiWarn("Ntv2MediaIO: audio system %d unavailable; playout continues video-only",
                                    _audioSystem);
                        _audioSystem = 0;
                }
        } else if (_audioSystem > _device->caps().audioSystemCount()) {
                promekiWarn("Ntv2MediaIO: audio system %d exceeds device capacity (%d); audio disabled",
                            _audioSystem, _device->caps().audioSystemCount());
                _audioSystem = 0;
        }

        // ---- Apply reference clock (best-effort) ----
        const VideoReferenceConfig refCfg = cfg.getAs<VideoReferenceConfig>(
                MediaConfig::VideoReference, VideoReferenceConfig());
        _device->setReference(refCfg, this);

        // ---- Resolve the requested raster / rate / pixel format ----
        if (md.imageList().isEmpty()) {
                promekiErr("Ntv2MediaIO: sink open requires an ImageDesc in pendingMediaDesc");
                closeSink();
                return Error::InvalidArgument;
        }
        _imageDesc = md.imageList()[0];
        _frameRate = md.frameRate();
        if (!_frameRate.isValid()) {
                promekiErr("Ntv2MediaIO: sink open requires a FrameRate in pendingMediaDesc");
                closeSink();
                return Error::InvalidArgument;
        }

        const PixelFormat &pf = _imageDesc.pixelFormat();
        if (!pf.isValid() || !_device->caps().supportsPixelFormat(pf.id())) {
                promekiErr("Ntv2MediaIO: pixel format '%s' unsupported by this card; "
                           "ask planner for a CSC bridge first",
                           pf.name().cstr());
                closeSink();
                return Error::FormatMismatch;
        }
        const NTV2FrameBufferFormat fbf = static_cast<NTV2FrameBufferFormat>(
                Ntv2Format::toNtv2PixelFormat(pf.id()));

        const int videoFormat = Ntv2Format::toNtv2VideoFormat(_imageDesc, _frameRate);
        if (videoFormat == NTV2_FORMAT_UNKNOWN) {
                promekiErr("Ntv2MediaIO: %ux%u@%u/%u not in Phase-2 video-format mapping table",
                           _imageDesc.size().width(), _imageDesc.size().height(),
                           _frameRate.numerator(), _frameRate.denominator());
                closeSink();
                return Error::NotSupported;
        }

        // On a sink there is nothing to detect — the wire standard
        // is forced by the chosen raster + rate + cable count + the
        // wire payload format.  When the caller leaves the
        // SdiLinkStandard as Auto (either because they didn't supply
        // an SdiOutputSignal at all or because they explicitly want
        // the library to pick), infer it now so the routing helper
        // and downstream programming pick up a concrete standard.
        //
        // The wire payload is the framebuffer's natural wire format
        // (@ref sdiWireFormatFor), unless on-board CSC is in play —
        // in which case the wire colour family flips to YCbCr 4:2:2
        // 10-bit (the only family single-link SDI carries natively).
        if (sdiSignal.standard() == SdiLinkStandard::Auto) {
                const VideoFormat fmt(_imageDesc.size(), _frameRate, _imageDesc.videoScanMode());
                const bool framebufferRgb = pf.colorModel().type() != ColorModel::TypeYCbCr;
                const bool onBoardCscEnabled =
                        !cfg.getAs<bool>(MediaConfig::Ntv2DisableOnBoardCsc, false)
                        && _device->caps().cscCount() > 0;
                SdiWireFormat wireFormat = sdiWireFormatFor(pf);
                if (framebufferRgb && onBoardCscEnabled) {
                        // On-board CSC will reduce RGB → YCbCr 4:2:2
                        // 10-bit on the wire before it ever hits the
                        // SDI cable, so the wire bandwidth is the
                        // YCbCr payload's rate, not the RGB framestore's.
                        wireFormat = SdiWireFormat::YCbCr_422_10;
                }
                const SdiLinkStandard inferred = inferSdiLinkStandard(
                        fmt, wireFormat, static_cast<int>(_reservedPorts.size()));
                if (inferred == SdiLinkStandard::Auto) {
                        promekiErr("Ntv2MediaIO: cannot infer SdiLinkStandard for '%s' (pf=%s, wire=%s) "
                                   "over %zu cable(s); set MediaConfig::SdiOutputSignal explicitly",
                                   fmt.toString().cstr(), pf.name().cstr(),
                                   wireFormat.toString().cstr(), _reservedPorts.size());
                        closeSink();
                        return Error::InvalidArgument;
                }
                promekiInfo("Ntv2MediaIO: sink open inferred SdiLinkStandard '%s' from '%s' "
                            "(pf=%s, wire=%s) over %zu cable(s)",
                            inferred.toString().cstr(), fmt.toString().cstr(),
                            pf.name().cstr(), wireFormat.toString().cstr(),
                            _reservedPorts.size());
                sdiSignal.setStandard(inferred);
        }

        // ---- Program the card for output ----
        //
        // Same non-recursive-mutex pattern as openSource: scope the
        // device mutex around the cross-channel register pokes and
        // break out via a local Error variable so the mutex releases
        // before any closeSink() rollback (closeSink also takes the
        // mutex internally).
        CNTV2Card        &card    = _device->card();
        const NTV2Channel ntv2Ch  = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));
        const bool        rgbFraming = pf.colorModel().type() != ColorModel::TypeYCbCr;
        Error             configErr = Error::Ok;
        do {
                Mutex::Locker lk(_device->mutex());

                card.EnableChannel(ntv2Ch);
                card.SubscribeOutputVerticalEvent(ntv2Ch);

                if (_device->caps().hasBiDirectionalSdi()) {
                        // Switch every reserved SDI connector to
                        // transmit — for fanout that's all the
                        // mirror ports too, not just the primary
                        // channel.  Wait a couple of VBIs for the
                        // downstream receivers to re-lock.
                        for (size_t i = 0; i < _reservedPorts.size(); ++i) {
                                const VideoPortRef &p = _reservedPorts[i];
                                if (p.kind() != VideoConnectorKind::Sdi) continue;
                                const NTV2Channel txCh =
                                        static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(p.index()));
                                card.SetSDITransmitEnable(txCh, true);
                        }
                        card.WaitForOutputVerticalInterrupt(NTV2_CHANNEL1, 10);
                }

                card.SetVideoFormat(static_cast<NTV2VideoFormat>(videoFormat), false, false, ntv2Ch);
                card.SetVANCMode(NTV2_VANCMODE_OFF, ntv2Ch);
                card.SetFrameBufferFormat(ntv2Ch, fbf);

                // Same link-standard resolution as openSource — the
                // routing helper handles single-link, quad-link
                // Squares / 2SI, and 12G single-link via the
                // configured SdiLinkStandard, plus on-board CSC
                // insertion when the framestore colour family
                // differs from the wire's, plus mirror groups when
                // SdiOutputFanout was set.
                Ntv2Routing::Config rcfg;
                rcfg.standard         = sdiSignal.standard();
                rcfg.channelStart     = _channel;
                rcfg.portStart        = _reservedPorts[0].index();
                rcfg.mirrorPortStarts = mirrorPortStarts;
                rcfg.can12gRouting    = card.features().CanDo12gRouting();
                rcfg.framebufferRgb   = rgbFraming;
                rcfg.signalRgb        = false;
                rcfg.allowOnBoardCsc =
                        !cfg.getAs<bool>(MediaConfig::Ntv2DisableOnBoardCsc, false)
                        && _device->caps().cscCount() > 0;
                configErr = routeSdiOutput(rcfg);
                if (configErr.isError()) break;
        } while (false);
        if (configErr.isError()) {
                closeSink();
                return configErr;
        }

        promekiInfo("Ntv2MediaIO: channel %d on '%s' opened for playout — %ux%u @ %u/%u, pf=%s",
                    _channel, _device->displayName().cstr(),
                    _imageDesc.size().width(), _imageDesc.size().height(),
                    _frameRate.numerator(), _frameRate.denominator(), pf.name().cstr());

        // ---- VPID stamping (Phase 6.4) ----
        // Resolve the byte-4 transfer / colorimetry / luminance /
        // RGB-range fields from the framestore ImageDesc + config
        // overrides and write the per-channel VPID overrides to the
        // card.  Best-effort; a failed override logs a warning and
        // playout continues with the card's auto-derived VPID.
        _vpidEnabled = cfg.getAs<bool>(MediaConfig::Ntv2VpidEnable, true);
        applySinkVpid(cfg, _imageDesc);

        // ---- Optional ANC insertion engine ----
        // Same rule as the source path: respect the user's preference,
        // downgrade to off on cards without the custom-ANC firmware.
        const bool requestAnc = cfg.getAs<bool>(MediaConfig::Ntv2WithAnc, true);
        _ancEnabled           = requestAnc && _device->caps().canDoCustomAnc();
        if (requestAnc && !_ancEnabled) {
                promekiInfo("Ntv2MediaIO: ANC insertion requested but card '%s' lacks the custom-ANC engine — ANC disabled",
                            _device->displayName().cstr());
        }
        if (_ancEnabled) {
                _ancF1Buf = Buffer(Ntv2Anc::kPreferredBufBytes);
                _ancF2Buf = Buffer(Ntv2Anc::kPreferredBufBytes);
                if (!_ancF1Buf.isValid() || !_ancF2Buf.isValid()) {
                        promekiWarn("Ntv2MediaIO: ANC buffer allocation failed; playout continues without ANC");
                        _ancEnabled = false;
                        _ancF1Buf   = Buffer();
                        _ancF2Buf   = Buffer();
                } else {
                        // Mark the resident buffers fully populated so
                        // SetAncBuffers conveys the right capacity to
                        // the driver (Buffer(N) reserves but leaves
                        // size() at zero).
                        _ancF1Buf.setSize(Ntv2Anc::kPreferredBufBytes);
                        _ancF2Buf.setSize(Ntv2Anc::kPreferredBufBytes);
                }
        }

        // Cache the F2 start line for interlaced playout — the AJA
        // encoder uses it to bucket per-packet lines into the right
        // field.  Zero for progressive (ignored by the encoder).
        {
                const NTV2Standard         std = ::GetNTV2StandardFromVideoFormat(
                        static_cast<NTV2VideoFormat>(videoFormat));
                const NTV2SmpteLineNumber  smpte(std);
                const bool isInterlaced = _imageDesc.videoScanMode() == VideoScanMode::Interlaced
                                       || _imageDesc.videoScanMode() == VideoScanMode::InterlacedEvenFirst
                                       || _imageDesc.videoScanMode() == VideoScanMode::InterlacedOddFirst;
                _f2StartLine = isInterlaced
                        ? static_cast<uint16_t>(smpte.GetLastLine(NTV2_FIELD0) + 1)
                        : 0;
        }

        // ---- AutoCirculate output init ----
        const NTV2AudioSystem ntv2Aud = _audioSystem > 0
                ? static_cast<NTV2AudioSystem>(_audioSystem - 1)
                : NTV2_AUDIOSYSTEM_INVALID;
        ULWord acOptions = AUTOCIRCULATE_WITH_RP188;
        if (_ancEnabled) acOptions |= AUTOCIRCULATE_WITH_ANC;
        card.AutoCirculateStop(ntv2Ch);
        if (!card.AutoCirculateInitForOutput(ntv2Ch, /*inFrameCount*/ 0, ntv2Aud, acOptions)) {
                promekiErr("Ntv2MediaIO: AutoCirculateInitForOutput failed for channel %d", _channel);
                closeSink();
                return Error::DeviceError;
        }

        _stopFlag.setValue(false);
        _readCancelled.setValue(false);
        _framesPlayed.setValue(0);
        _framesDroppedSink.setValue(0);
        _ancPacketsSent.setValue(0);
        _deviceLostCount.setValue(0);
        _deviceLost.setValue(false);

        // External pacing (Phase 6) starts unbound — the card paces
        // itself off its own reference and a later
        // executeCmd(SetClock) installs an external pacing clock via
        // PacingGate::setClock.  Setting the period preemptively
        // means a SetClock can skip re-deriving it.
        _paceClockExternal.setValue(false);
        _paceGate.setClock(Clock::Ptr());
        if (_frameRate.isValid()) {
                _paceGate.setPeriod(_frameRate.frameDuration());
        }

        // Indexed thread name (see capture-side comment).
        _playoutWorker.setName(
                String::format("ntv2pb:{}:{}", _device->deviceIndex(), _channel));
        _playoutWorker.start();
        return Error::Ok;
}

void Ntv2MediaIO::closeSink() {
        if (_device == nullptr) return;

        _stopFlag.setValue(true);
        if (_playoutWorker.isRunning()) _playoutWorker.wait();

        if (_device->card().IsOpen()) {
                Mutex::Locker     lk(_device->mutex());
                const NTV2Channel ntv2Ch = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));
                _device->card().AutoCirculateStop(ntv2Ch);
                _device->card().UnsubscribeOutputVerticalEvent(ntv2Ch);
        }

        // Release any frames still queued so their backing Buffers
        // free while the allocator is still healthy.
        while (true) {
                auto popResult = _writeQueue.tryPop();
                if (popResult.second().isError()) break;
        }

        if (_audioSystem > 0) {
                _device->releaseAudioSystem(_audioSystem, this);
                _audioSystem = 0;
        }
        _device->releasePortsOwnedBy(this);
        _device->releaseChannel(_channel, this);

        Ntv2DeviceRegistry::instance().release(_device);
        _device = nullptr;

        // Drop ANC scratch buffers on close; harmless on the disabled
        // path where they were never allocated.
        _ancEnabled  = false;
        _f2StartLine = 0;
        _ancF1Buf    = Buffer();
        _ancF2Buf    = Buffer();

        // Unbind the external pacing clock so the gate doesn't carry
        // a stale Clock::Ptr into the next open.  The PacingGate
        // counters intentionally survive — they're consulted via the
        // Stats command and reset across opens by openSink resetting
        // the underlying frame-counter atomics.
        _paceClockExternal.setValue(false);
        _paceGate.setClock(Clock::Ptr());
}

Error Ntv2MediaIO::routeSdiOutput(const Ntv2Routing::Config &routingCfg) {
        if (routingCfg.channelStart < 1 || routingCfg.channelStart > 8
            || routingCfg.portStart < 1 || routingCfg.portStart > 8) {
                return Error::InvalidArgument;
        }

        Ntv2Routing::ConnectionList conns = Ntv2Routing::sdiOutputConnections(routingCfg);
        if (conns.isEmpty()) {
                promekiErr("Ntv2MediaIO: link standard '%s' not supported for output routing yet",
                           routingCfg.standard.toString().cstr());
                return Error::NotSupported;
        }

        CNTV2Card        &card   = _device->card();
        const NTV2Channel ntv2Ch =
                static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(routingCfg.channelStart));
        if (Ntv2Routing::needsTsi(routingCfg.standard, routingCfg.can12gRouting)) {
                card.SetTsiFrameEnable(true, ntv2Ch);
        } else if (Ntv2Routing::needsSquares(routingCfg.standard)) {
                card.Set4kSquaresEnable(true, ntv2Ch);
        }

        for (size_t i = 0; i < conns.size(); ++i) {
                const Ntv2Routing::Connection &c = conns[i];
                if (!card.Connect(static_cast<NTV2InputCrosspointID>(c.input),
                                  static_cast<NTV2OutputCrosspointID>(c.output),
                                  /*inValidate*/ false)) {
                        promekiErr("Ntv2MediaIO: signal routing Connect(in=0x%x, out=0x%x) failed "
                                   "(standard=%s, channel=%d, port=%d)",
                                   unsigned(c.input), unsigned(c.output),
                                   routingCfg.standard.toString().cstr(), routingCfg.channelStart,
                                   routingCfg.portStart);
                        return Error::DeviceError;
                }
        }
        return Error::Ok;
}

// ---- VPID stamping (Phase 6.4) ----

Error Ntv2MediaIO::applySinkVpid(const MediaIO::Config &cfg, const ImageDesc &md) {
        if (!_vpidEnabled) return Error::Ok;
        if (_device == nullptr) return Error::NotOpen;

        // Resolve the four byte-4 fields with a three-tier precedence
        // (most specific wins):
        //   1. Explicit MediaConfig override (Ntv2Vpid*Override).
        //   2. Per-frame metadata on the ImageDesc / PixelFormat
        //      (H.273 derivation from the framestore's ColorModel).
        //   3. AJA's auto-derived default ("Auto" sentinel left in
        //      place → write enable=false so the card picks).
        //
        // The first two land as a concrete enum value + enable=true;
        // tier 3 leaves the override disabled.  This way users get a
        // useful default without the override clobbering the card's
        // own format-derived signalling for legacy SDR paths.

        const TransferCharacteristics overrideTx = cfg.getAs<TransferCharacteristics>(
                MediaConfig::Ntv2VpidTransferOverride, TransferCharacteristics::Auto);
        const ColorPrimaries overrideCp = cfg.getAs<ColorPrimaries>(
                MediaConfig::Ntv2VpidColorimetryOverride, ColorPrimaries::Auto);
        const VideoRange overrideRng = cfg.getAs<VideoRange>(
                MediaConfig::Ntv2VpidRangeOverride, VideoRange::Unknown);

        // Derive H.273 codepoints from the framestore's PixelFormat
        // ColorModel (covers BT709 / SRGB / Rec.601 / BT.2020 / DCI
        // primaries / linear variants) so a caller that didn't
        // bother to fill the override keys still gets correct
        // signalling for the common SDR rasters.
        const ColorModel::H273 h273 =
                md.pixelFormat().isValid()
                        ? ColorModel::toH273(md.pixelFormat().colorModel().id())
                        : ColorModel::H273{};

        const TransferCharacteristics resolvedTx =
                (overrideTx == TransferCharacteristics::Auto)
                        ? TransferCharacteristics(h273.transfer)
                        : overrideTx;
        const ColorPrimaries resolvedCp =
                (overrideCp == ColorPrimaries::Auto)
                        ? ColorPrimaries(h273.primaries)
                        : overrideCp;
        // Range "Unknown" means "let the framestore decide" — today
        // the framestore range is implicit (limited for the YCbCr
        // family, full for the RGB family).  ColorModel::toH273
        // doesn't carry a range field, so we infer from the matrix
        // value: RGB matrix (0) → full, otherwise → limited.
        const VideoRange resolvedRng =
                (overrideRng != VideoRange::Unknown)
                        ? overrideRng
                        : (h273.matrix == 0 ? VideoRange::Full : VideoRange::Limited);

        const bool needTxOverride  = (overrideTx != TransferCharacteristics::Auto)
                                  || (h273.transfer != 0 && h273.transfer != 2);
        const bool needCpOverride  = (overrideCp != ColorPrimaries::Auto)
                                  || (h273.primaries != 0 && h273.primaries != 2);
        const bool needRngOverride = (overrideRng != VideoRange::Unknown);

        CNTV2Card        &card   = _device->card();
        const NTV2Channel ntv2Ch = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));
        Mutex::Locker     lk(_device->mutex());

        // Each set is best-effort — the AJA SDK returns false on
        // unsupported cards or unknown channels, which we surface
        // as a warning so the operator sees the failure without
        // breaking the open path.  A card without VPID overrides
        // for some fields (older Corvid variants) still benefits
        // from the partial overrides that did succeed.
        if (needTxOverride) {
                const NTV2VPIDTransferCharacteristics v =
                        static_cast<NTV2VPIDTransferCharacteristics>(Ntv2Vpid::toVpidTransfer(resolvedTx));
                if (!card.SetSDIOutVPIDTransferCharacteristics(true, v, ntv2Ch)) {
                        promekiWarn("Ntv2MediaIO: SetSDIOutVPIDTransferCharacteristics(%s) "
                                    "rejected by card on channel %d",
                                    resolvedTx.toString().cstr(), _channel);
                }
                // Mirror to the per-link VPID-luminance flag for
                // ICtCp clarity — the luminance bit is the only
                // way receivers know to apply BT.2100 vs ordinary
                // YCbCr decoding.  Derived from the resolved matrix
                // (BT.2020 NCL / CL / SMPTE2085) per H.273.
                const MatrixCoefficients matrix(h273.matrix);
                const NTV2VPIDLuminance lum =
                        static_cast<NTV2VPIDLuminance>(Ntv2Vpid::toVpidLuminance(matrix));
                card.SetSDIOutVPIDLuminance(true, lum, ntv2Ch);
        } else {
                card.SetSDIOutVPIDTransferCharacteristics(false, NTV2_VPID_TC_Unspecified, ntv2Ch);
        }
        if (needCpOverride) {
                const NTV2VPIDColorimetry v =
                        static_cast<NTV2VPIDColorimetry>(Ntv2Vpid::toVpidColorimetry(resolvedCp));
                if (!card.SetSDIOutVPIDColorimetry(true, v, ntv2Ch)) {
                        promekiWarn("Ntv2MediaIO: SetSDIOutVPIDColorimetry(%s) rejected by card on channel %d",
                                    resolvedCp.toString().cstr(), _channel);
                }
        } else {
                card.SetSDIOutVPIDColorimetry(false, NTV2_VPID_Color_Unknown, ntv2Ch);
        }
        if (needRngOverride) {
                const NTV2VPIDRGBRange v =
                        static_cast<NTV2VPIDRGBRange>(Ntv2Vpid::toVpidRgbRange(resolvedRng));
                if (!card.SetSDIOutVPIDRGBRange(true, v, ntv2Ch)) {
                        promekiWarn("Ntv2MediaIO: SetSDIOutVPIDRGBRange(%s) rejected by card on channel %d",
                                    resolvedRng.toString().cstr(), _channel);
                }
        } else {
                card.SetSDIOutVPIDRGBRange(false, NTV2_VPID_Range_Narrow, ntv2Ch);
        }

        promekiInfo("Ntv2MediaIO: VPID stamping channel %d → transfer=%s%s colorimetry=%s%s range=%s%s",
                    _channel,
                    resolvedTx.toString().cstr(), needTxOverride ? "" : "(auto)",
                    resolvedCp.toString().cstr(), needCpOverride ? "" : "(auto)",
                    resolvedRng.toString().cstr(), needRngOverride ? "" : "(auto)");
        return Error::Ok;
}

void Ntv2MediaIO::pollSourceVpid() {
        if (!_vpidEnabled) return;
        if (_device == nullptr || _reservedPorts.isEmpty()) return;
        if (_reservedPorts[0].kind() != VideoConnectorKind::Sdi) return;
        if (!_device->card().IsOpen()) return;

        // Read the per-channel VPID registers the card has decoded
        // from the live input stream.  AJA's read path is cheap
        // (single register read, no IOCTL round-trip for register-
        // mapped fields) so we don't bother caching beyond the
        // dedupe in the capture-loop call site.
        CNTV2Card        &card   = _device->card();
        const NTV2Channel ntv2Ch = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));
        // GetVPID* read the per-channel decoded byte-4 values.  No
        // need to lock the device mutex here — these are pure
        // reads that don't touch shared routing or framestore
        // state.
        NTV2VPIDTransferCharacteristics tx = NTV2_VPID_TC_Unspecified;
        NTV2VPIDColorimetry             cp = NTV2_VPID_Color_Unknown;
        NTV2VPIDRGBRange                rg = NTV2_VPID_Range_Narrow;
        card.GetVPIDTransferCharacteristics(tx, ntv2Ch);
        card.GetVPIDColorimetry(cp, ntv2Ch);
        card.GetVPIDRGBRange(rg, ntv2Ch);

        _vpidLastTransfer    = static_cast<int>(tx);
        _vpidLastColorimetry = static_cast<int>(cp);
        _vpidLastRange       = static_cast<int>(rg);
        _vpidLastValid       = true;

        // HDR PixelFormat upgrade (Path C — Phase 6.4 follow-up):
        // when the wire claims PQ or HLG, upgrade the framestore's
        // PixelFormat to the matching BT.2100 HDR variant so the
        // colour-description claim travels with the image through
        // every downstream consumer via @ref ColorModel::toH273 —
        // no per-frame metadata stamping needed.  Restricted to the
        // 10- / 12-bit UYVY layouts NTV2 SDI actually carries; if a
        // future layout needs HDR, add the matching PixelFormat ID
        // to the catalog and extend the switch below.
        if (!_imageDesc.isValid()) return;
        const TransferCharacteristics wireTx = Ntv2Vpid::fromVpidTransfer(_vpidLastTransfer);
        const bool isHdr = (wireTx == TransferCharacteristics::SMPTE2084
                         || wireTx == TransferCharacteristics::ARIB_STD_B67);
        if (!isHdr) return;
        const bool isPq = (wireTx == TransferCharacteristics::SMPTE2084);
        PixelFormat::ID upgraded = PixelFormat::Invalid;
        switch (_imageDesc.pixelFormat().id()) {
                case PixelFormat::YUV10_422_UYVY_LE_Rec709:
                case PixelFormat::YUV10_422_UYVY_LE_Rec2020:
                case PixelFormat::YUV10_422_UYVY_LE_Rec2020_PQ:
                case PixelFormat::YUV10_422_UYVY_LE_Rec2020_HLG:
                        upgraded = isPq ? PixelFormat::YUV10_422_UYVY_LE_Rec2020_PQ
                                        : PixelFormat::YUV10_422_UYVY_LE_Rec2020_HLG;
                        break;
                case PixelFormat::YUV12_422_UYVY_LE_Rec709:
                case PixelFormat::YUV12_422_UYVY_LE_Rec2020:
                case PixelFormat::YUV12_422_UYVY_LE_Rec2020_PQ:
                case PixelFormat::YUV12_422_UYVY_LE_Rec2020_HLG:
                        upgraded = isPq ? PixelFormat::YUV12_422_UYVY_LE_Rec2020_PQ
                                        : PixelFormat::YUV12_422_UYVY_LE_Rec2020_HLG;
                        break;
                default:
                        // Layout has no HDR variant — leave it; consumers
                        // that care about HDR signalling on uncommon
                        // layouts can read the wire claim via the
                        // legacy @c Metadata::Video* keys path.
                        return;
        }
        if (upgraded != _imageDesc.pixelFormat().id()) {
                _imageDesc.setPixelFormat(PixelFormat(upgraded));
                promekiInfo("Ntv2MediaIO: VPID upgraded framestore PixelFormat to %s (channel %d, wire=%s)",
                            _imageDesc.pixelFormat().name().cstr(), _channel, wireTx.toString().cstr());
        }
}

// ---- Playout loop ----

void Ntv2MediaIO::playoutLoop() {
        if (_device == nullptr || !_imageDesc.isValid()) return;
        CNTV2Card        &card  = _device->card();
        const NTV2Channel ntv2Ch = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));

        AUTOCIRCULATE_TRANSFER xfer;
        bool                   started      = false;
        int                    queuedFrames = 0;
        constexpr int          kPrebuffer   = 3;

        // Match the capture worker's driver-restart poll cadence —
        // every Nth loop iteration we consult @c CNTV2Card::IsOpen
        // to catch the driver going away mid-stream.  Reuses
        // @ref _signalPollIntervalVbi which is "VBIs between polls"
        // on the source side; here it's "loop iterations between
        // polls" but the practical cadence is similar (one iteration
        // per VBI in steady state).
        int devicePollTickCount = 0;

        while (!_stopFlag.value() && !_deviceLost.value()) {
                if (_signalPollIntervalVbi > 0
                    && ++devicePollTickCount >= _signalPollIntervalVbi) {
                        devicePollTickCount = 0;
                        if (!card.IsOpen()) {
                                _deviceLostCount.fetchAndAdd(1);
                                _deviceLost.setValue(true);
                                promekiErr(
                                        "Ntv2MediaIO: device handle invalid mid-playout "
                                        "(driver restart / hot-unplug?) on channel %d",
                                        _channel);
                                errorOccurredSignal.emit(Error(Error::DeviceError));
                                break;
                        }
                }
                AUTOCIRCULATE_STATUS acStatus;
                card.AutoCirculateGetStatus(ntv2Ch, acStatus);
                if (acStatus.CanAcceptMoreOutputFrames()) {
                        // Pop the next frame; short timeout so the
                        // stop flag is observed promptly even when
                        // the writer is idle.
                        auto popResult = _writeQueue.pop(static_cast<unsigned int>(_vbiTimeoutMs));
                        if (popResult.second().isError()) {
                                // Queue empty within the poll window —
                                // loop back and re-check the stop flag
                                // / try the queue again.  We rely on
                                // AutoCirculate's own internal buffering
                                // to bridge brief writer stalls; if the
                                // writer disappears entirely the card
                                // will repeat the last good frame.
                                continue;
                        }
                        Frame outFrame = std::move(popResult.first());
                        if (!outFrame.isValid()) continue;

                        // Strand validated at executeCmd(Write) that the
                        // frame has at least one uncompressed video
                        // payload; if it slipped through invalid we
                        // drop the whole frame here.
                        auto vids = outFrame.videoPayloads();
                        if (vids.isEmpty() || !vids[0].isValid()) {
                                _framesDroppedSink.fetchAndAdd(1);
                                continue;
                        }
                        auto vp = sharedPointerCast<UncompressedVideoPayload>(vids[0]);
                        if (!vp.isValid()) {
                                _framesDroppedSink.fetchAndAdd(1);
                                continue;
                        }

                        // BufferView's single-slice convenience path
                        // hands the SDK a contiguous pointer.  Multi-
                        // slice views (cross-Buffer scatter) are not
                        // accepted by AutoCirculateTransfer's flat
                        // pointer API; reject loudly.
                        const BufferView &bv = vp->data();
                        if (bv.size() == 0) continue;
                        if (bv.count() != 1) {
                                promekiErr("Ntv2MediaIO: multi-slice BufferView not supported by playout — "
                                           "SDK requires contiguous DMA buffer");
                                _framesDroppedSink.fetchAndAdd(1);
                                continue;
                        }

                        xfer.SetVideoBuffer(
                                reinterpret_cast<ULWord *>(const_cast<uint8_t *>(bv.data())),
                                static_cast<ULWord>(bv.size()));
                        if (_ancEnabled) {
                                // Pull ANC payloads off the frame and
                                // encode into the resident F1 / F2
                                // GUMP buffers.  Multiple AncPayloads
                                // on one frame (rare but legal) get
                                // merged — only the last one's encoded
                                // bytes survive in the per-pass call,
                                // so we union packets first.
                                AncPayload::PtrList ancs = outFrame.ancPayloads();
                                AncPacket::List     merged;
                                int64_t             ancPktCount = 0;
                                for (size_t i = 0; i < ancs.size(); ++i) {
                                        if (!ancs[i].isValid()) continue;
                                        const AncPacket::List &p = ancs[i]->packets();
                                        for (size_t j = 0; j < p.size(); ++j) merged.pushToBack(p[j]);
                                }
                                ancPktCount = static_cast<int64_t>(merged.size());
                                if (ancPktCount > 0) {
                                        AncDesc    desc(_imageDesc.size(), _imageDesc.videoScanMode(),
                                                        _frameRate);
                                        AncPayload combined(desc, std::move(merged));
                                        const bool isProgressive =
                                                _imageDesc.videoScanMode() != VideoScanMode::Interlaced
                                                && _imageDesc.videoScanMode()
                                                           != VideoScanMode::InterlacedEvenFirst
                                                && _imageDesc.videoScanMode()
                                                           != VideoScanMode::InterlacedOddFirst;
                                        Error encErr = Ntv2Anc::packetsToNtv2Anc(
                                                combined, static_cast<uint8_t *>(_ancF1Buf.data()),
                                                _ancF1Buf.size(),
                                                static_cast<uint8_t *>(_ancF2Buf.data()),
                                                _ancF2Buf.size(), isProgressive, _f2StartLine);
                                        if (encErr.isOk()) {
                                                xfer.SetAncBuffers(
                                                        static_cast<ULWord *>(_ancF1Buf.data()),
                                                        static_cast<ULWord>(_ancF1Buf.size()),
                                                        isProgressive ? nullptr
                                                                      : static_cast<ULWord *>(
                                                                              _ancF2Buf.data()),
                                                        static_cast<ULWord>(
                                                                isProgressive ? 0 : _ancF2Buf.size()));
                                                _ancPacketsSent.fetchAndAdd(ancPktCount);
                                        }
                                }
                        }
                        // External pacing (Phase 6): when a caller-
                        // supplied Clock is bound through SetClock,
                        // wait on @ref _paceGate until the frame's
                        // deadline before submitting.  Skip verdicts
                        // drop the frame so cumulative lag against
                        // the external timeline doesn't grow without
                        // bound; Reanchor verdicts proceed but log a
                        // warning so an operator can see the gap.
                        // Without an external clock the gate is
                        // unbound and we fall straight through to the
                        // card's self-pacing (Phase-2 default).
                        if (_paceClockExternal.value() && _paceGate.hasClock()) {
                                PacingResult pr = _paceGate.wait();
                                if (pr.error.isError()) {
                                        promekiErr(
                                                "Ntv2MediaIO: external pacing clock failure: %s "
                                                "(channel %d) — falling through to card pacing",
                                                pr.error.name().cstr(), _channel);
                                } else if (pr.verdict == PacingVerdict::Skip) {
                                        _framesDroppedSink.fetchAndAdd(1);
                                        continue;
                                } else if (pr.verdict == PacingVerdict::Reanchor) {
                                        promekiWarn(
                                                "Ntv2MediaIO: external pacing re-anchored after %s lag "
                                                "(channel %d)",
                                                pr.slack.toString().cstr(), _channel);
                                }
                        }
                        if (card.AutoCirculateTransfer(ntv2Ch, xfer)) {
                                _framesPlayed.fetchAndAdd(1);
                                ++queuedFrames;
                                if (!started && queuedFrames >= kPrebuffer) {
                                        // Mirror the player demo:
                                        // pre-buffer a few frames into
                                        // the card before starting
                                        // AutoCirculate so playout
                                        // begins on a full pipe.
                                        card.AutoCirculateStart(ntv2Ch);
                                        started = true;
                                }
                        } else {
                                _framesDroppedSink.fetchAndAdd(1);
                        }
                } else {
                        // Card has no room — sleep until the next
                        // output VBI when at least one frame buffer
                        // typically frees up.
                        card.WaitForOutputVerticalInterrupt(ntv2Ch, static_cast<UWord>(_vbiTimeoutMs));
                }
        }
}

// ============================================================================
// Ntv2Factory
// ============================================================================

bool Ntv2Factory::canHandlePath(const String &path) const {
        return path.startsWith(String("ntv2://")) || path.startsWith(String("ntv2:"));
}

StringList Ntv2Factory::enumerate() const {
        StringList urls;
        const ULWord count = static_cast<ULWord>(CNTV2DeviceScanner::GetNumDevices());
        for (ULWord i = 0; i < count; ++i) {
                CNTV2Card card;
                if (!CNTV2DeviceScanner::GetDeviceAtIndex(i, card)) continue;
                // Emit one URL per channel the card exposes, so the
                // probe output lines up with real openable
                // destinations.  Without per-card channel counts the
                // user has to guess.
                const int channelCount = static_cast<int>(card.features().GetNumFrameStores());
                for (int ch = 1; ch <= channelCount; ++ch) {
                        urls.pushToBack(String::format("ntv2://{}/{}", i, ch));
                }
        }
        return urls;
}

Error Ntv2Factory::urlToConfig(const Url &url, Config *outConfig) const {
        if (outConfig == nullptr) return Error::InvalidArgument;

        // URL forms:
        //   ntv2://<device>/<channel>
        //   ntv2:///<channel>           (device 0 implicit)
        // where <device> is either an integer index, a name shorthand,
        // or "serial:NNN".  Path may be empty (defaults channel = 1).
        const String host = url.host();
        String       path = url.path();
        if (!path.isEmpty() && path[0] == '/') path = path.substr(1);

        // Channel: first decimal token in the path.  Defaults to 1.
        int    channel        = 1;
        if (!path.isEmpty()) {
                Error           parseErr;
                int             parsed = path.toInt(&parseErr);
                if (parseErr.isOk() && parsed >= 1) channel = parsed;
        }
        outConfig->set(MediaConfig::Ntv2Channel, int32_t(channel));

        // Device: integer → Ntv2DeviceIndex; anything else (including
        // empty == "device 0") → Ntv2DeviceName for the registry's
        // resolveDeviceIndex to chew on.
        if (host.isEmpty()) {
                outConfig->set(MediaConfig::Ntv2DeviceIndex, int32_t(0));
        } else {
                Error parseErr;
                int   parsedIdx = host.toInt(&parseErr);
                if (parseErr.isOk() && parsedIdx >= 0) {
                        outConfig->set(MediaConfig::Ntv2DeviceIndex, int32_t(parsedIdx));
                } else {
                        outConfig->set(MediaConfig::Ntv2DeviceIndex, int32_t(-1));
                        outConfig->set(MediaConfig::Ntv2DeviceName, host);
                }
        }
        return Error::Ok;
}

Ntv2Factory::Config::SpecMap Ntv2Factory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };

        // Media descriptor defaults — broadcast HD as the sane default
        // matching the demos.  Callers override via --dc.
        s(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_59_94));
        s(MediaConfig::VideoSize, Size2Du32(1920, 1080));
        s(MediaConfig::VideoPixelFormat, PixelFormat(kDefaultCapturePixelFormat));
        s(MediaConfig::AudioRate, 48000.0f);
        s(MediaConfig::AudioChannels, int32_t(2));

        // NTV2-specific defaults.
        s(MediaConfig::Ntv2DeviceIndex, int32_t(0));
        s(MediaConfig::Ntv2DeviceName, String());
        s(MediaConfig::Ntv2Channel, int32_t(1));
        s(MediaConfig::Ntv2AudioSystem, int32_t(-1));
        s(MediaConfig::Ntv2WithAnc, true);
        s(MediaConfig::Ntv2RetailServices, false);
        s(MediaConfig::Ntv2MultiFormatMode, true);
        s(MediaConfig::Ntv2BufferLockMode, true);
        s(MediaConfig::Ntv2VbiTimeoutMs, int32_t(50));
        s(MediaConfig::Ntv2SignalPollIntervalVbi, int32_t(15));
        s(MediaConfig::Ntv2PaceSkipThresholdMs, int32_t(0));
        s(MediaConfig::Ntv2PaceReanchorThresholdMs, int32_t(0));
        s(MediaConfig::Ntv2VpidEnable, true);
        s(MediaConfig::Ntv2VpidTransferOverride, TransferCharacteristics::Auto);
        s(MediaConfig::Ntv2VpidColorimetryOverride, ColorPrimaries::Auto);
        s(MediaConfig::Ntv2VpidRangeOverride, VideoRange::Unknown);

        return specs;
}

MediaIO *Ntv2Factory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new Ntv2MediaIO(parent);
        io->setConfig(config);
        return io;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
