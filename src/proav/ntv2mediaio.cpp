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
#include <promeki/ntv2clock.h>
#include <promeki/ntv2device.h>
#include <promeki/ntv2format.h>
#include <promeki/system.h>
#include <promeki/thread.h>
#include <promeki/timestamp.h>
#include <promeki/url.h>

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

        // Captures-arrival CaptureTime — wall-clock anchor in
        // SystemMonotonic so downstream stages can measure transit
        // latency (CaptureTime - PTS).  Mirrors the NDI backend's
        // localArrivalCaptureTime() helper.
        MediaTimeStamp localArrivalCaptureTime() {
                return MediaTimeStamp(TimeStamp::now(), ClockDomain(ClockDomain::SystemMonotonic));
        }

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
        constexpr unsigned int        kReadPollMs = 100;
        UncompressedVideoPayload::Ptr vp;
        for (;;) {
                auto popResult = _videoQueue.pop(kReadPollMs);
                if (popResult.second().isOk()) {
                        vp = std::move(popResult.first());
                        break;
                }
                if (_readCancelled.value()) {
                        return Error::Cancelled;
                }
        }

        Frame frame = Frame();
        frame.addPayload(std::move(vp));
        if (_frameRate.isValid()) {
                frame.metadata().set(Metadata::FrameRate, _frameRate);
        }

        cmd.frame        = frame;
        cmd.currentFrame = FrameNumber{_framesReceived.value()};
        return Error::Ok;
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (_device == nullptr || !_sinkMode) return Error::NotOpen;
        if (!cmd.frame.isValid()) return Error::InvalidArgument;

        // Pick the first uncompressed video payload off the frame.
        // Sink mode accepts only uncompressed video — compressed
        // payloads require a decode stage upstream, same shape as
        // NDI's sink path.
        auto vids = cmd.frame.videoPayloads();
        if (vids.isEmpty() || !vids[0].isValid()) {
                return Error::InvalidArgument;
        }
        auto uvp = sharedPointerCast<UncompressedVideoPayload>(vids[0]);
        if (!uvp.isValid()) {
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
        _writeQueue.push(uvp);

        cmd.currentFrame = FrameNumber{_framesPlayed.value() + 1};
        cmd.frameCount   = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandSetClock &cmd) {
        (void)cmd;
        // External pacing arrives in Phase 6.  For now the card paces
        // itself off its own reference and the port group's bound
        // sample clock is read-only.
        return Error::NotSupported;
}

Error Ntv2MediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesReceived, _framesReceived.value());
        cmd.stats.set(StatsFramesDropped, _framesDropped.value());
        cmd.stats.set(StatsAudioBytesReceived, _audioBytesReceived.value());
        cmd.stats.set(StatsFramesPlayed, _framesPlayed.value());
        cmd.stats.set(StatsFramesDroppedSink, _framesDroppedSink.value());
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
        CNTV2Card &card = _device->card();
        Mutex::Locker lk(_device->mutex());

        const NTV2Channel ntv2Ch = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));
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
        NTV2VideoFormat videoFormat = card.GetInputVideoFormat(ntv2Src);
        if (videoFormat == NTV2_FORMAT_UNKNOWN) {
                promekiErr("Ntv2MediaIO: no input signal on %s", _reservedPorts[0].toString().cstr());
                closeSource();
                return Error::NotReady;
        }
        if (!card.features().CanDoVideoFormat(videoFormat)) {
                promekiErr("Ntv2MediaIO: device cannot handle detected video format");
                closeSource();
                return Error::NotSupported;
        }

        // Translate back to libpromeki types for the resolved descriptor.
        Size2Du32     detectedSize;
        FrameRate     detectedRate;
        VideoScanMode detectedScan = VideoScanMode::Unknown;
        if (Ntv2Format::fromNtv2VideoFormat(videoFormat, &detectedSize, &detectedRate, &detectedScan).isError()) {
                promekiErr("Ntv2MediaIO: detected NTV2 video format not in Phase-1 mapping table");
                closeSource();
                return Error::NotSupported;
        }

        // Pixel format: the caller's preference if given, otherwise
        // UYVY as the safe default.  We refuse formats the card
        // doesn't support outright; the planner can wire a CSC bridge
        // upstream.
        PixelFormat pf = md.imageList().isEmpty() ? PixelFormat() : md.imageList()[0].pixelFormat();
        if (!pf.isValid()) {
                pf = PixelFormat(kDefaultCapturePixelFormat);
        }
        if (!_device->caps().supportsPixelFormat(pf.id())) {
                promekiErr("Ntv2MediaIO: pixel format '%s' unsupported by this card; "
                           "ask planner for a CSC bridge first",
                           pf.name().cstr());
                closeSource();
                return Error::FormatMismatch;
        }
        const NTV2FrameBufferFormat fbf = static_cast<NTV2FrameBufferFormat>(
                Ntv2Format::toNtv2PixelFormat(pf.id()));

        // Push the format settings to the card.  SetVideoFormat
        // implicitly programs the framestore standard / geometry; we
        // explicitly disable VANC because this Phase doesn't handle it.
        card.SetVideoFormat(videoFormat, false, false, ntv2Ch);
        card.SetVANCMode(NTV2_VANCMODE_OFF, ntv2Ch);
        card.SetFrameBufferFormat(ntv2Ch, fbf);

        // ---- Signal routing (Phase 1: single-link SDI only) ----
        err = routeSdiInput(_channel);
        if (err.isError()) {
                closeSource();
                return err;
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

        // ---- Start AutoCirculate + spawn capture thread ----
        const NTV2AudioSystem ntv2Aud = _audioSystem > 0
                ? static_cast<NTV2AudioSystem>(_audioSystem - 1)
                : NTV2_AUDIOSYSTEM_INVALID;
        const ULWord acOptions = AUTOCIRCULATE_WITH_RP188;
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
}

Error Ntv2MediaIO::routeSdiInput(int channel) {
        if (channel < 1 || channel > 8) return Error::InvalidArgument;

        // The crosspoint enums are sequential per-channel:
        //   NTV2_XptFrameBuffer{N}Input = NTV2_FIRST_INPUT_CROSSPOINT + (N-1)
        //   NTV2_XptSDIIn{N}             = NTV2_XptSDIIn1 + (N-1)
        const NTV2InputCrosspointID  fbInput = static_cast<NTV2InputCrosspointID>(
                NTV2_XptFrameBuffer1Input + (channel - 1));
        const NTV2OutputCrosspointID sdiOut  = static_cast<NTV2OutputCrosspointID>(
                NTV2_XptSDIIn1 + (channel - 1));
        if (!_device->card().Connect(fbInput, sdiOut, /*inValidate*/ false)) {
                promekiErr("Ntv2MediaIO: signal routing Connect(FB%d_Input, SDIIn%d) failed",
                           channel, channel);
                return Error::DeviceError;
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

        while (!_stopFlag.value()) {
                AUTOCIRCULATE_STATUS acStatus;
                card.AutoCirculateGetStatus(ntv2Ch, acStatus);
                if (acStatus.IsRunning() && acStatus.HasAvailableInputFrame()) {
                        Buffer buf(writeSize);
                        if (!buf.isValid()) {
                                _framesDropped.fetchAndAdd(1);
                                continue;
                        }
                        xfer.SetVideoBuffer(static_cast<ULWord *>(buf.data()), writeSize);
                        if (!card.AutoCirculateTransfer(ntv2Ch, xfer)) {
                                _framesDropped.fetchAndAdd(1);
                                continue;
                        }
                        buf.setSize(writeSize);
                        BufferView view(buf, 0, writeSize);
                        UncompressedVideoPayload::Ptr vp =
                                UncompressedVideoPayload::Ptr::create(_imageDesc, view);

                        // PTS in the device-clock domain — gives
                        // downstream stages a cross-channel-comparable
                        // anchor on this card.  CaptureTime stays in
                        // SystemMonotonic for transit-latency math.
                        if (_sourceClock.isValid()) {
                                Result<MediaTimeStamp> pts = _sourceClock->now();
                                if (pts.second().isOk()) {
                                        vp.modify()->setPts(pts.first());
                                        vp.modify()->setDts(pts.first());
                                }
                        }
                        vp.modify()->metadata().set(Metadata::CaptureTime, localArrivalCaptureTime());

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
                        _videoQueue.push(std::move(vp));
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

        const PixelFormat &pf = offered.imageList()[0].pixelFormat();
        if (pf.isValid() && Ntv2Format::toNtv2PixelFormat(pf.id()) != int(NTV2_FBF_INVALID)) {
                // Already a format the card can DMA directly.
                *preferred = offered;
                return Error::Ok;
        }

        // No direct mapping — pick a same-color-family fallback so the
        // planner inserts a CSC bridge instead of failing open with
        // FormatMismatch.  YCbCr sources land on UYVY (the demo's
        // default and lowest-common-denominator NTV2 frame-buffer
        // format); everything else (RGB, sRGB, unknown) lands on
        // packed RGB8.
        const bool isYuv = pf.isValid() && pf.colorModel().type() == ColorModel::TypeYCbCr;
        const PixelFormat target(isYuv ? PixelFormat::YUV8_422_UYVY_Rec709
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

        // SDI output port: Phase 2 expects exactly one for single-link
        // playout.  When the caller didn't pass an SdiOutputSignal key,
        // default to the SDI port matching the channel index.
        SdiSignalConfig sdiSignal = cfg.getAs<SdiSignalConfig>(MediaConfig::SdiOutputSignal,
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

        // ---- Program the card for output ----
        CNTV2Card &card = _device->card();
        Mutex::Locker lk(_device->mutex());

        const NTV2Channel ntv2Ch = static_cast<NTV2Channel>(Ntv2Format::toNtv2Channel(_channel));
        card.EnableChannel(ntv2Ch);
        card.SubscribeOutputVerticalEvent(ntv2Ch);

        if (_device->caps().hasBiDirectionalSdi()) {
                // Switch the channel's SDI connector to transmit and
                // wait a couple of VBIs for the downstream receiver to
                // re-lock.  Mirrors what the demo player does.
                card.SetSDITransmitEnable(ntv2Ch, true);
                card.WaitForOutputVerticalInterrupt(NTV2_CHANNEL1, 10);
        }

        card.SetVideoFormat(static_cast<NTV2VideoFormat>(videoFormat), false, false, ntv2Ch);
        card.SetVANCMode(NTV2_VANCMODE_OFF, ntv2Ch);
        card.SetFrameBufferFormat(ntv2Ch, fbf);

        // ---- Signal routing (Phase 2: single-link SDI only) ----
        const bool rgbFraming = pf.colorModel().type() != ColorModel::TypeYCbCr;
        err = routeSdiOutput(_channel, rgbFraming);
        if (err.isError()) {
                closeSink();
                return err;
        }

        promekiInfo("Ntv2MediaIO: channel %d on '%s' opened for playout — %ux%u @ %u/%u, pf=%s",
                    _channel, _device->displayName().cstr(),
                    _imageDesc.size().width(), _imageDesc.size().height(),
                    _frameRate.numerator(), _frameRate.denominator(), pf.name().cstr());

        // ---- AutoCirculate output init ----
        const NTV2AudioSystem ntv2Aud = _audioSystem > 0
                ? static_cast<NTV2AudioSystem>(_audioSystem - 1)
                : NTV2_AUDIOSYSTEM_INVALID;
        const ULWord acOptions = AUTOCIRCULATE_WITH_RP188;
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
}

Error Ntv2MediaIO::routeSdiOutput(int channel, bool rgbFraming) {
        if (channel < 1 || channel > 8) return Error::InvalidArgument;

        // SDIOut{N}_Input enums are sequential 2-apart
        // (see NTV2_XptSDIOut1Input + 2*(N-1) in the SDK header).
        const NTV2InputCrosspointID sdiOutInput = static_cast<NTV2InputCrosspointID>(
                NTV2_XptSDIOut1Input + 2 * (channel - 1));

        // FrameBuffer{N}YUV values are not sequential — the SDK assigns
        // them ad-hoc across the legacy crosspoint space.  Look them
        // up by index.  The RGB variants are the same enum bit OR-ed
        // with 0x80.
        constexpr int kFrameBufferYUV[8] = {
                NTV2_XptFrameBuffer1YUV, NTV2_XptFrameBuffer2YUV,
                NTV2_XptFrameBuffer3YUV, NTV2_XptFrameBuffer4YUV,
                NTV2_XptFrameBuffer5YUV, NTV2_XptFrameBuffer6YUV,
                NTV2_XptFrameBuffer7YUV, NTV2_XptFrameBuffer8YUV,
        };
        int fbOut = kFrameBufferYUV[channel - 1];
        if (rgbFraming) fbOut |= 0x80;

        if (!_device->card().Connect(sdiOutInput,
                                     static_cast<NTV2OutputCrosspointID>(fbOut),
                                     /*inValidate*/ false)) {
                promekiErr("Ntv2MediaIO: signal routing Connect(SDIOut%d_Input, FrameBuffer%d%s) failed",
                           channel, channel, rgbFraming ? "RGB" : "YUV");
                return Error::DeviceError;
        }
        return Error::Ok;
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

        while (!_stopFlag.value()) {
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
                        UncompressedVideoPayload::Ptr vp = std::move(popResult.first());
                        if (!vp.isValid()) continue;

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

        return specs;
}

MediaIO *Ntv2Factory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new Ntv2MediaIO(parent);
        io->setConfig(config);
        return io;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
