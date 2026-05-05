/**
 * @file      ndimediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <promeki/ndimediaio.h>

#include <cstring>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/audiopayload.h>
#include <promeki/buffer.h>
#include <promeki/clockdomain.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioport.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiostats.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/ndidiscovery.h>
#include <promeki/ndiformat.h>
#include <promeki/ndilib.h>
#include <promeki/rational.h>
#include <promeki/system.h>
#include <promeki/thread.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/videopayload.h>

#include <Processing.NDI.Lib.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_MEDIAIO_FACTORY(NdiFactory)

namespace {

        // Pull the canonical presentation timestamp from a payload
        // and convert it to NDI's 100ns-tick representation for the
        // sender-side `timecode` field.  PTS is the right channel
        // for "when should this frame be presented" — every backend
        // populates it consistently, unlike the optional
        // Metadata::CaptureTime which is receive-side-only.
        //
        // Returns the SDK's synthesize sentinel when the payload
        // carries no usable PTS, so the SDK falls back to its own
        // auto-incrementing clock.  Any clock domain is accepted —
        // the receiver sees only the resulting tick count.
        int64_t senderTimecodeFor(const MediaTimeStamp &pts) {
                if (!pts.isValid()) return NDIlib_send_timecode_synthesize;
                int64_t ns = pts.timeStamp().nanoseconds();
                if (ns <= 0) return NDIlib_send_timecode_synthesize;
                return ns / 100;
        }

        // Build a MediaTimeStamp in the NdiClock domain from an NDI
        // 100ns-tick timestamp.  Returns an invalid MediaTimeStamp
        // when the SDK signals "no timestamp available" via the
        // NDIlib_recv_timestamp_undefined sentinel — callers should
        // then leave the payload's PTS untouched.
        MediaTimeStamp ndiTimestampToPts(int64_t ndiTimestampTicks) {
                if (ndiTimestampTicks == NDIlib_recv_timestamp_undefined || ndiTimestampTicks <= 0) {
                        return MediaTimeStamp();
                }
                const int64_t    ns = ndiTimestampTicks * 100;
                TimeStamp::Value v{std::chrono::nanoseconds(ns)};
                TimeStamp        ts(v);
                return MediaTimeStamp(ts, NdiClock::domain());
        }

        // Build a MediaTimeStamp in the SystemMonotonic domain
        // marking "this machine first saw the frame" — i.e. the
        // moment the NDI capture thread dequeued the packet from
        // the SDK.  Lives in CaptureTime metadata alongside the PTS
        // so downstream stages can measure transit latency
        // (CaptureTime - PTS) and per-stream jitter.
        MediaTimeStamp localArrivalCaptureTime() {
                return MediaTimeStamp(TimeStamp::now(), ClockDomain(ClockDomain::SystemMonotonic));
        }

        // Strip everything from the first '.' onward so an FQDN like
        // "machine.local" compares equal to a bare "machine".  NDI's
        // canonical names use the bare hostname on every platform we
        // care about, but URL authors commonly type the FQDN — both
        // forms should resolve to the same machine.
        String stripDomain(const String &s) {
                size_t dot = s.find('.');
                return dot == String::npos ? s : s.left(dot);
        }

        // True when @p host names this machine (or is empty, which
        // means "this machine" by URL convention).  Compares
        // case-insensitively against System::hostname() — bare and
        // strip-domain forms both match.  Drives the sink-mode locality
        // check: NDI senders can only run on the local box, so a
        // non-local host in the URL is a hard error rather than a
        // silent fallback to the local hostname.
        bool isLocalNdiHost(const String &host) {
                if (host.isEmpty()) return true;
                const String me = System::hostname();
                if (me.isEmpty()) return false;
                if (host.toLower() == me.toLower()) return true;
                return stripDomain(host).toLower() == stripDomain(me).toLower();
        }

} // namespace

// ---------------------------------------------------------------------------
// NdiMediaIO
// ---------------------------------------------------------------------------

NdiMediaIO::NdiMediaIO(ObjectBase *parent) : DedicatedThreadMediaIO(parent) {}

NdiMediaIO::~NdiMediaIO() {
        // Mirror V4L2's pattern: drain through the strand so we don't
        // race the still-alive worker that may be processing an
        // earlier close.  isOpen() is the framework's "the user
        // called open() and didn't call close()" signal.
        if (isOpen()) (void)close().wait();
}

Error NdiMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        const MediaIO::Config &cfg = cmd.config;

        // Direction is config-driven via MediaConfig::OpenMode (same
        // shape as RtpMediaIO).  We dispatch to openSink / openSource
        // based on the mode; both wire into a single port-group below.
        Enum modeEnum      = cfg.get(MediaConfig::OpenMode).asEnum(MediaIOOpenMode::Type);
        const bool isWrite = modeEnum.value() == MediaIOOpenMode::Write.value();

        // The NDI runtime must be loaded before we can do anything.
        // The bootstrap singleton failure path is logged at first
        // construction; here we just translate to a clean error so the
        // pipeline reports it at open time rather than crashing at the
        // first SDK call.
        if (!NdiLib::instance().isLoaded()) {
                promekiErr("NdiMediaIO: NDI runtime is not loaded — "
                           "install libndi.so.6 or set PROMEKI_NDI_SDK_DIR");
                return Error::LibraryFailure;
        }

        if (isWrite) {
                _sinkMode = true;
                Error err = openSink(cfg, cmd.pendingMediaDesc);
                if (err.isError()) return err;
        } else {
                _sinkMode = false;
                Error err = openSource(cfg);
                if (err.isError()) return err;
        }

        // Resolved MediaDesc reflects what we are actually sending /
        // receiving.  Sink mode passes the caller's pendingMediaDesc
        // through unchanged (NDI doesn't negotiate at the sender).
        // Source mode would fold in the SDK's reported geometry once
        // the first frame arrives, but we don't have that until after
        // open returns — for now we hand back the descriptor we
        // populated from the config defaults inside openSource (the
        // capture thread fills in the real shape on first frame).
        MediaDesc resolved = cmd.pendingMediaDesc;
        if (!isWrite) {
                if (resolved.imageList().isEmpty() && _imageDesc.isValid()) {
                        resolved.imageList().pushToBack(_imageDesc);
                }
                if (resolved.audioList().isEmpty() && _audioChannels > 0 &&
                    _audioSampleRate > 0.0f) {
                        // We interleave the SDK's planar floats on
                        // receive (see captureLoop), so downstream
                        // sees native interleaved float — the SDL
                        // audio sink fast-path expects exactly that
                        // and downstream PcmAudioPayload::convert
                        // calls hit the registered identity converter
                        // instead of a needless via-float trip.
                        resolved.audioList().pushToBack(
                                AudioDesc(AudioFormat(AudioFormat::PCMI_Float32LE),
                                          _audioSampleRate,
                                          static_cast<unsigned int>(_audioChannels)));
                }
                if (_frameRate.isValid()) resolved.setFrameRate(_frameRate);
        }

        // Source mode hands a backend-driven clock to the port group
        // so downstream stages see NDI per-frame timestamps directly
        // (cross-stream / cross-machine alignment for free when the
        // sender's host clock is NTP/PTP-synced).  Sink mode falls
        // back to the framework's synthesised MediaIOClock since the
        // sender has no per-frame upstream tick to mirror.
        MediaIOPortGroup *group = nullptr;
        if (!isWrite) {
                _sourceClock = Clock::Ptr::takeOwnership(new NdiClock(_frameRate));
                group = addPortGroup(String("ndi"), _sourceClock);
        } else {
                group = addPortGroup(String("ndi"));
        }
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
        return Error::Ok;
}

Error NdiMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        (void)cmd;
        if (_send) closeSink();
        if (_recv) closeSource();
        return Error::Ok;
}

void NdiMediaIO::cancelBlockingWork() {
        // Wake up an in-flight executeCmd(Read) parked on the video
        // queue's bounded pop.  The strand worker checks this flag at
        // every poll-timeout boundary and bails to Error::Cancelled
        // so MediaIO::close() can drain in finite time.  Same pattern
        // as V4L2.
        _readCancelled.store(true, std::memory_order_release);
}

Error NdiMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        if (cmd.frame.isNull()) return Error::InvalidArgument;
        if (!_send) return Error::NotOpen;
        const Frame &frame = *cmd.frame;

        Error firstErr = Error::Ok;

        auto vids = frame.videoPayloads();
        if (!vids.isEmpty() && vids[0].isValid()) {
                // The NDI sender only accepts uncompressed video — the
                // SDK has its own compression pipeline.  Compressed
                // payloads are reported as Unsupported so the caller
                // knows to insert a decode stage upstream.
                auto uvp = sharedPointerCast<UncompressedVideoPayload>(vids[0]);
                if (!uvp.isValid()) {
                        promekiErr("NdiMediaIO: only uncompressed video is supported "
                                   "in sink mode");
                        firstErr = Error::NotSupported;
                } else {
                        Error e = sendVideo(*uvp);
                        if (e.isError() && firstErr.isOk()) firstErr = e;
                }
        }

        auto auds = frame.audioPayloads();
        if (!auds.isEmpty() && auds[0].isValid()) {
                auto pap = sharedPointerCast<PcmAudioPayload>(auds[0]);
                if (pap.isValid()) {
                        Error e = sendAudio(*pap);
                        if (e.isError() && firstErr.isOk()) firstErr = e;
                }
                // Compressed audio (Opus, etc.) is silently ignored
                // here — no NDI codec equivalent.  A future revision
                // may decode-then-send via PcmAudioPayload.
        }

        if (firstErr.isError()) {
                noteFrameDropped(portGroup(0));
                return firstErr;
        }

        cmd.currentFrame = FrameNumber{static_cast<int64_t>(_framesSent.load(std::memory_order_relaxed) + 1)};
        cmd.frameCount   = MediaIO::FrameCountInfinite;
        return Error::Ok;
}

Error NdiMediaIO::executeCmd(MediaIOCommandSetClock &cmd) {
        // Reject in source mode — the receive-side clock is driven by
        // the upstream sender's per-frame timestamps and the user
        // cannot meaningfully replace it.
        if (!_sinkMode) return Error::NotSupported;

        // Bind both gates to the same clock; setClock automatically
        // re-arms each so the next sendVideo / sendAudio anchors
        // against the new clock's now() rather than carrying a stale
        // anchor forward.  A null cmd.clock unbinds — both gates
        // become no-ops and the SDK's internal clock_video /
        // clock_audio takes over.
        _videoGate.setClock(cmd.clock);
        _audioGate.setClock(cmd.clock);
        return Error::Ok;
}

Error NdiMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(StatsFramesSent, _framesSent.load(std::memory_order_relaxed));
        cmd.stats.set(StatsAudioFramesSent, _audioFramesSent.load(std::memory_order_relaxed));
        cmd.stats.set(StatsBytesSent, _bytesSent.load(std::memory_order_relaxed));
        cmd.stats.set(StatsFramesReceived, _framesReceived.load(std::memory_order_relaxed));
        cmd.stats.set(StatsAudioFramesReceived, _audioFramesReceived.load(std::memory_order_relaxed));
        cmd.stats.set(StatsMetadataReceived, _metadataReceived.load(std::memory_order_relaxed));
        cmd.stats.set(StatsDroppedReceives, _droppedReceives.load(std::memory_order_relaxed));
        cmd.stats.set(StatsAudioSilenceFilled, _audioSilenceSamples.load(std::memory_order_relaxed));
        cmd.stats.set(StatsAudioGapEvents, _audioGapEvents.load(std::memory_order_relaxed));
        cmd.stats.set(MediaIOStats::QueueDepth, static_cast<int64_t>(_videoQueue.size()));
        cmd.stats.set(MediaIOStats::QueueCapacity, static_cast<int64_t>(VideoQueueDepth));
        return Error::Ok;
}

Error NdiMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        if (!_recv) return Error::NotOpen;

        // Mirror V4L2's blocking-pop pattern: poll at @c kReadPollMs
        // boundaries so cancelBlockingWork() can break us out
        // without bounding throughput in steady state.
        constexpr unsigned int                kReadPollMs = 100;
        UncompressedVideoPayload::Ptr         vp;
        for (;;) {
                auto popResult = _videoQueue.pop(kReadPollMs);
                if (popResult.second().isOk()) {
                        vp = std::move(popResult.first());
                        break;
                }
                if (_readCancelled.load(std::memory_order_acquire)) {
                        return Error::Cancelled;
                }
        }

        Frame::Ptr frame = Frame::Ptr::create();
        frame.modify()->addPayload(std::move(vp));

        // Drain whatever audio has accumulated since the last video
        // frame.  The natural sample count varies with NDI's send
        // rate — we don't round to a "samples per video frame" value;
        // downstream drift correction reads the actual delivered
        // count from the payload.
        {
                Mutex::Locker lk(_audioMutex);
                size_t        avail = _audioRing.available();
                if (avail > 0 && _audioRing.format().isValid()) {
                        AudioDesc   nativeDesc = _audioRing.format();
                        size_t      bufBytes   = nativeDesc.bufferSize(avail);
                        Buffer pcm        = Buffer(bufBytes);
                        auto [got, err]        = _audioRing.pop(pcm.data(), avail);
                        if (err.isOk() && got > 0) {
                                size_t     usedBytes = nativeDesc.bufferSize(got);
                                pcm.setSize(usedBytes);
                                BufferView view(pcm, 0, usedBytes);
                                auto       audioPayload = PcmAudioPayload::Ptr::create(nativeDesc, got, view);
                                // PTS = sender-anchored time of the
                                // FIRST sample in this drain — the
                                // canonical anchor for the payload.
                                // _audioFirstSampleTicks is set by
                                // ingestNdiAudio when the ring transitions
                                // from empty to non-empty (after any
                                // gap-bridging silence) so it correctly
                                // reflects coalesced multi-frame drains.
                                MediaTimeStamp audioPts = ndiTimestampToPts(_audioFirstSampleTicks);
                                if (audioPts.isValid()) {
                                        audioPayload.modify()->setPts(audioPts);
                                        audioPayload.modify()->setDts(audioPts);
                                }
                                audioPayload.modify()->metadata().set(
                                        Metadata::CaptureTime, localArrivalCaptureTime());
                                // Stamp the silence-fill / discontinuity
                                // markers accumulated for this drain
                                // before clearing the accumulator.
                                if (!_audioMarkersSinceDrain.isEmpty()) {
                                        audioPayload.modify()->metadata().set(
                                                Metadata::AudioMarkers, _audioMarkersSinceDrain);
                                }
                                _audioMarkersSinceDrain.clear();
                                // Ring is now empty — the next ingest
                                // sets _audioFirstSampleTicks afresh.
                                _audioFirstSampleTicks = 0;
                                frame.modify()->addPayload(std::move(audioPayload));
                        }
                }
        }

        // Carry over any metadata that arrived since the last read.
        // We hand it to the frame and clear the latch so subsequent
        // reads don't see the same XML over and over.
        {
                Mutex::Locker lk(_metadataMutex);
                if (_hasPendingMetadata) {
                        frame.modify()->metadata() = _pendingMetadata;
                        _hasPendingMetadata        = false;
                        _pendingMetadata           = Metadata();
                }
        }

        // Stamp the live frame rate on the frame so downstream stages
        // (e.g. the inspector's marker-based A/V sync check) see the
        // real rational rate rather than the open-time placeholder.
        // The capture thread keeps @c _frameRate updated from each
        // SDK frame's @c frame_rate_N / @c frame_rate_D, so by the
        // time we get here on a frame popped from the queue the rate
        // has been refreshed.  Skip for an invalid rate (which would
        // overwrite any FrameRate downstream may have derived).
        if (_frameRate.isValid()) {
                frame.modify()->metadata().set(Metadata::FrameRate, _frameRate);
        }

        cmd.frame        = frame;
        cmd.currentFrame = FrameNumber{static_cast<int64_t>(_framesReceived.load(std::memory_order_relaxed))};
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Pre-open negotiation
// ---------------------------------------------------------------------------

Error NdiMediaIO::proposeInput(const MediaDesc &offered, MediaDesc *preferred) const {
        if (preferred == nullptr) return Error::Invalid;

        // No image layer means audio-only — accept as-is; the audio
        // path converts to FLTP at send time and handles every PCM
        // shape the upstream might produce.
        if (offered.imageList().isEmpty()) {
                *preferred = offered;
                return Error::Ok;
        }

        const PixelFormat &pd = offered.imageList()[0].pixelFormat();
        if (pd.isValid() && NdiFormat::pixelFormatToFourcc(pd.id()) != 0) {
                // Offered format already maps to an NDI FourCC — no
                // bridge needed.
                *preferred = offered;
                return Error::Ok;
        }

        // Pick a same-color-family fallback so the planner's CSC
        // bridge stays cheap.  YCbCr sources go to UYVY; everything
        // else (RGB, sRGB, unknown) goes to BGRA.  Both are 8-bit
        // formats in NDI's accepted set and have paint engines, so
        // any downstream burn / inspector stages keep working.
        const bool isYuv = pd.isValid() && pd.colorModel().type() == ColorModel::TypeYCbCr;
        const PixelFormat target(isYuv ? PixelFormat::YUV8_422_UYVY_Rec709
                                       : PixelFormat::BGRA8_sRGB);

        MediaDesc        want = offered;
        ImageDesc::List &imgs = want.imageList();
        for (size_t i = 0; i < imgs.size(); ++i) {
                imgs[i].setPixelFormat(target);
        }
        *preferred = want;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Sink-mode helpers
// ---------------------------------------------------------------------------

Error NdiMediaIO::openSink(const MediaIO::Config &cfg, const MediaDesc &md) {
        // Capture all sender-relevant config keys up-front so
        // sendVideo / sendAudio don't need to re-fetch each frame.
        _sendName       = cfg.getAs<String>(MediaConfig::NdiSendName, String("promeki"));
        _sendGroups     = cfg.getAs<String>(MediaConfig::NdiSendGroups, String());
        _extraIps       = cfg.getAs<String>(MediaConfig::NdiExtraIps, String());
        _sendClockVideo = cfg.getAs<bool>(MediaConfig::NdiSendClockVideo, true);
        _sendClockAudio = cfg.getAs<bool>(MediaConfig::NdiSendClockAudio, true);

        // When the MediaIO was opened from an ndi:// URL, the URL host
        // names the machine the sender must run on.  NDI senders can
        // only advertise from the local box, so a non-local host is
        // a hard error rather than a silent fallback.  An empty host
        // (ndi:///<name> form) and a Url-less open (configured via
        // MediaConfig::NdiSendName directly) both pass through.
        const Url urlFromConfig = cfg.getAs<Url>(MediaConfig::Url, Url());
        if (urlFromConfig.isValid() && !isLocalNdiHost(urlFromConfig.host())) {
                promekiErr("NdiMediaIO: cannot open sink for URL '%s' — host '%s' is not "
                           "this machine ('%s'); NDI senders can only run on the local box",
                           urlFromConfig.toString().cstr(), urlFromConfig.host().cstr(),
                           System::hostname().cstr());
                return Error::InvalidArgument;
        }

        // The pending MediaDesc tells us what video / audio shape the
        // caller intends to send.  We pin those at open time so
        // executeCmd(Write) can fail fast when a frame's format
        // disagrees with what the SDK was set up to expect.
        if (!md.imageList().isEmpty()) {
                _imageDesc = md.imageList()[0];
        }
        _frameRate = md.frameRate();
        if (!_frameRate.isValid()) {
                _frameRate = FrameRate(FrameRate::FPS_30);
        }

        if (!md.audioList().isEmpty()) {
                const AudioDesc &ad = md.audioList()[0];
                _audioChannels   = ad.channels();
                _audioSampleRate = ad.sampleRate();
        }

        // Validate the video pixel format up front — the backend
        // doesn't do conversion, so a format that doesn't map to a
        // supported NDI FourCC has to be rejected with a clear
        // message naming the upstream-fix path.
        if (_imageDesc.isValid()) {
                uint32_t fourcc = NdiFormat::pixelFormatToFourcc(_imageDesc.pixelFormat().id());
                if (fourcc == 0) {
                        promekiErr("NdiMediaIO: pixel format %s has no NDI equivalent — "
                                   "convert via CSC to UYVY / NV12 / I420 / BGRA / RGBA / "
                                   "YUV{10,12,16}_422_SemiPlanar_LE_Rec709 first",
                                   _imageDesc.pixelFormat().name().cstr());
                        return Error::FormatMismatch;
                }
        }

        // Build the sender create struct.  The lifetime of the
        // strings has to outlive the SDK call — the SDK is documented
        // to copy these internally on send_create, so stack-locals
        // (or our own member Strings) are fine.
        NDIlib_send_create_t cs;
        cs.p_ndi_name    = _sendName.cstr();
        cs.p_groups      = _sendGroups.isEmpty() ? nullptr : _sendGroups.cstr();
        cs.clock_video   = _sendClockVideo;
        cs.clock_audio   = _sendClockAudio;

        const NDIlib_v6 *api = NdiLib::instance().api();
        if (!api || !api->send_create) {
                return Error::LibraryFailure;
        }
        _send = api->send_create(&cs);
        if (!_send) {
                promekiErr("NdiMediaIO: NDIlib_send_create returned NULL for name '%s'",
                           _sendName.cstr());
                return Error::LibraryFailure;
        }

        promekiInfo("NdiMediaIO: sender '%s' created (%dx%d @ %u/%u, video=%s, audio=%uch @ %.0fHz)",
                    _sendName.cstr(),
                    _imageDesc.size().width(), _imageDesc.size().height(),
                    _frameRate.numerator(), _frameRate.denominator(),
                    _imageDesc.pixelFormat().name().cstr(),
                    static_cast<unsigned>(_audioChannels),
                    static_cast<double>(_audioSampleRate));

        _framesSent.store(0, std::memory_order_relaxed);
        _audioFramesSent.store(0, std::memory_order_relaxed);
        _bytesSent.store(0, std::memory_order_relaxed);
        return Error::Ok;
}

void NdiMediaIO::closeSink() {
        if (!_send) return;
        const NDIlib_v6 *api = NdiLib::instance().api();
        if (api && api->send_destroy) {
                api->send_destroy(_send);
        }
        _send = nullptr;
}

Error NdiMediaIO::sendVideo(const UncompressedVideoPayload &vp) {
        if (!_send) return Error::NotOpen;
        const ImageDesc &desc = vp.desc();
        uint32_t fourcc = NdiFormat::pixelFormatToFourcc(desc.pixelFormat().id());
        if (fourcc == 0) {
                promekiErr("NdiMediaIO: unsupported per-frame pixel format %s",
                           desc.pixelFormat().name().cstr());
                return Error::FormatMismatch;
        }

        // The SDK reads bytes straight from the payload's first plane.
        // For semi-planar / planar payloads the SDK expects the planes
        // contiguous in memory at the natural stride — which is how
        // promeki lays them out when allocate() is called with the
        // full descriptor (single backing Buffer, planes packed).
        const BufferView &bv = vp.data();
        if (bv.size() == 0) {
                return Error::InvalidArgument;
        }
        // BufferView's single-slice convenience accessors give us a
        // contiguous data() pointer for the common case (the SDK
        // doesn't support scatter-gather sends).  Multi-slice views
        // would need a copy-into-contiguous step before send — we
        // reject them here rather than silently corrupting the wire.
        if (bv.count() != 1) {
                promekiErr("NdiMediaIO: multi-slice BufferView not supported — "
                           "the SDK requires a contiguous send buffer");
                return Error::NotSupported;
        }

        NDIlib_video_frame_v2_t f;
        f.xres                 = static_cast<int>(desc.size().width());
        f.yres                 = static_cast<int>(desc.size().height());
        f.FourCC               = static_cast<NDIlib_FourCC_video_type_e>(fourcc);
        f.frame_rate_N         = static_cast<int>(_frameRate.numerator());
        f.frame_rate_D         = static_cast<int>(_frameRate.denominator());
        f.picture_aspect_ratio = 0.0f; // 0 = use xres/yres ratio (square pixels).
        f.frame_format_type    = NDIlib_frame_format_type_progressive;
        // Forward upstream PTS into NDI's media-clock field so
        // receivers see real source timestamps rather than an SDK-
        // synthesized monotonic count.  Synthesize falls back when
        // the payload's PTS is unset.
        f.timecode             = senderTimecodeFor(vp.pts());
        // For uncompressed formats the SDK expects the luma-plane line
        // stride (chroma stride is derived from the FourCC's geometry).
        f.line_stride_in_bytes = static_cast<int>(desc.pixelFormat().lineStride(0, desc));
        f.p_data               = bv.data();
        f.p_metadata           = nullptr;
        f.timestamp            = NDIlib_recv_timestamp_undefined;

        const NDIlib_v6 *api = NdiLib::instance().api();
        if (!api || !api->send_send_video_v2) return Error::LibraryFailure;
        // Pace against the external clock (if any) immediately before
        // handing the buffer to the SDK so the deadline reflects when
        // bytes actually leave us.  No-op when no clock is bound.
        // First call arms the gate without sleeping, so frame 0 fires
        // immediately.
        if (_videoGate.hasClock() && _frameRate.isValid()) {
                _videoGate.setPeriod(_frameRate.frameDuration());
        }
        PacingResult pr = _videoGate.wait();
        if (pr.error.isError()) {
                promekiErr("NdiMediaIO: video pacing clock failure: %s", pr.error.name().cstr());
        }
        switch (pr.verdict) {
                case PacingVerdict::Skip:
                        // We're behind by >= one frame — drop this
                        // frame to bound the lag rather than emit
                        // stale content.  Audio cross-fade / SRC is a
                        // backend follow-up; video has no analog so a
                        // drop is the only option.
                        noteFrameDropped(portGroup(0));
                        return Error::Ok;
                case PacingVerdict::Reanchor:
                        promekiWarn("NdiMediaIO: video pacing re-anchored after %s lag",
                                    pr.slack.toString().cstr());
                        break;
                case PacingVerdict::OnTime:
                case PacingVerdict::Late:
                        break;
        }
        api->send_send_video_v2(_send, &f);

        _framesSent.fetch_add(1, std::memory_order_relaxed);
        _bytesSent.fetch_add(static_cast<int64_t>(bv.size()), std::memory_order_relaxed);
        return Error::Ok;
}

Error NdiMediaIO::sendAudio(const PcmAudioPayload &ap) {
        if (!_send) return Error::NotOpen;

        // NDI's wire format is 32-bit float planar (FLTP).  Convert
        // unconditionally to PCMP_Float32LE — the existing payload
        // converter handles every other PCM input shape (interleaved
        // / planar, integer / float, narrower / wider) so this stays
        // a one-liner.  When the input is already in the target
        // format the converter returns a CoW reference to the same
        // buffers (free on the hot path).
        PcmAudioPayload::Ptr fltp = ap.convert(AudioFormat(AudioFormat::PCMP_Float32LE));
        if (!fltp.isValid()) {
                promekiErr("NdiMediaIO: failed to convert audio to PCMP_Float32LE");
                return Error::ConversionFailed;
        }

        const AudioDesc &ad      = fltp->desc();
        const size_t     samples = fltp->sampleCount();
        if (samples == 0) return Error::Ok;

        // The SDK expects a single contiguous buffer covering all N
        // channel-planes back-to-back, with a per-plane stride.  Our
        // planar PCM uses one BufferView per channel — when they
        // share the same underlying Buffer (the common allocate()
        // result) plane 0's pointer + the per-plane stride covers
        // every channel.  Multi-buffer planar audio would need a
        // copy-coalesce step we'll add when we hit a backend that
        // produces it.
        const BufferView &bv = fltp->data();
        if (bv.count() == 0) return Error::InvalidArgument;
        if (bv.count() > 1) {
                // Multi-slice planar — not currently produced by any
                // promeki backend that targets us, but warn rather
                // than corrupt.
                promekiErr("NdiMediaIO: multi-slice planar audio not supported; "
                           "use the convert path to consolidate first");
                return Error::NotSupported;
        }

        NDIlib_audio_frame_v3_t f;
        f.sample_rate            = static_cast<int>(ad.sampleRate());
        f.no_channels            = static_cast<int>(ad.channels());
        f.no_samples             = static_cast<int>(samples);
        // Mirror sendVideo: forward upstream PTS as the NDI media
        // clock value when the input carries one, otherwise fall
        // back to the SDK's synthesised counter.  Read PTS from the
        // *original* payload — the FLTP converter constructs a fresh
        // payload that doesn't carry the source's PTS forward.
        f.timecode               = senderTimecodeFor(ap.pts());
        f.FourCC                 = NDIlib_FourCC_audio_type_FLTP;
        f.p_data                  = bv.data();
        f.channel_stride_in_bytes = static_cast<int>(samples * sizeof(float));
        f.p_metadata             = nullptr;
        f.timestamp              = NDIlib_recv_timestamp_undefined;

        const NDIlib_v6 *api = NdiLib::instance().api();
        if (!api || !api->send_send_audio_v3) return Error::LibraryFailure;
        // Pace against the external clock for the audio block's
        // duration (samples / sampleRate).  The audio gate's "period"
        // is set per-call here because real audio block sizes vary
        // — the underlying gate uses the supplied advance for both
        // accumulation and (when stale anchor) initial threshold
        // sizing.  v1 audio response to Skip is the same as video
        // (drop); cross-fade / SRC catch-up is a backend follow-up
        // that consumes the verdict's @c skippedTicks.
        const Duration audioAdvance = (_audioSampleRate > 0.0f && samples > 0)
                ? Duration::fromNanoseconds(static_cast<int64_t>(
                          static_cast<double>(samples) * 1.0e9 /
                          static_cast<double>(_audioSampleRate)))
                : Duration();
        if (_audioGate.hasClock() && !audioAdvance.isZero() && _audioGate.period().isZero()) {
                // Seed the period with the first observed block size
                // so skip / reanchor thresholds default to something
                // meaningful (they default to 0 with no period set,
                // which would disable Skip / Reanchor).
                _audioGate.setPeriod(audioAdvance);
        }
        PacingResult ar = _audioGate.wait(audioAdvance);
        if (ar.error.isError()) {
                promekiErr("NdiMediaIO: audio pacing clock failure: %s", ar.error.name().cstr());
        }
        switch (ar.verdict) {
                case PacingVerdict::Skip:
                        noteFrameDropped(portGroup(0));
                        return Error::Ok;
                case PacingVerdict::Reanchor:
                        promekiWarn("NdiMediaIO: audio pacing re-anchored after %s lag",
                                    ar.slack.toString().cstr());
                        break;
                case PacingVerdict::OnTime:
                case PacingVerdict::Late:
                        break;
        }
        api->send_send_audio_v3(_send, &f);

        _audioFramesSent.fetch_add(1, std::memory_order_relaxed);
        _bytesSent.fetch_add(static_cast<int64_t>(bv.size()), std::memory_order_relaxed);
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Source-mode helpers
// ---------------------------------------------------------------------------

namespace {

        // Convert the project's NdiBandwidth enum to the SDK's bandwidth
        // value.  Default to highest if the caller's enum value falls
        // through the table (forward-compat: a future enum value we
        // don't know about should still produce a reasonable receiver).
        int ndiBandwidthFor(const Enum &e) {
                if (e.value() == NdiBandwidth::Lowest.value())       return NDIlib_recv_bandwidth_lowest;
                if (e.value() == NdiBandwidth::AudioOnly.value())    return NDIlib_recv_bandwidth_audio_only;
                if (e.value() == NdiBandwidth::MetadataOnly.value()) return NDIlib_recv_bandwidth_metadata_only;
                return NDIlib_recv_bandwidth_highest;
        }

        // Convert the project's NdiColorFormat enum to the SDK's
        // color-format value.  Same forward-compat default story.
        NDIlib_recv_color_format_e ndiColorFormatFor(const Enum &e) {
                if (e.value() == NdiColorFormat::Fastest.value())  return NDIlib_recv_color_format_fastest;
                if (e.value() == NdiColorFormat::UyvyBgra.value()) return NDIlib_recv_color_format_UYVY_BGRA;
                if (e.value() == NdiColorFormat::UyvyRgba.value()) return NDIlib_recv_color_format_UYVY_RGBA;
                if (e.value() == NdiColorFormat::BgrxBgra.value()) return NDIlib_recv_color_format_BGRX_BGRA;
                if (e.value() == NdiColorFormat::RgbxRgba.value()) return NDIlib_recv_color_format_RGBX_RGBA;
                return NDIlib_recv_color_format_best;
        }

        // Translate the project's NdiReceiveBitDepth enum into the
        // BitDepth selector consumed by NdiFormat::fourccToPixelFormat.
        NdiFormat::BitDepth ndiBitDepthFor(const Enum &e) {
                if (e.value() == NdiReceiveBitDepth::Bits10.value()) return NdiFormat::BitDepth10;
                if (e.value() == NdiReceiveBitDepth::Bits12.value()) return NdiFormat::BitDepth12;
                if (e.value() == NdiReceiveBitDepth::Bits16.value()) return NdiFormat::BitDepth16;
                return NdiFormat::BitDepthAuto;
        }

} // namespace

Error NdiMediaIO::openSource(const MediaIO::Config &cfg) {
        // Resolve the canonical NDI source name.  Either the caller
        // set NdiSourceName directly, or the URL parser populated it
        // from an `ndi://<host>/<name>` URL via NdiFactory::urlToConfig
        // (which composes a full canonical "<host> (<name>)", filling
        // in the local hostname for the `ndi:///<name>` form).
        const String sourceName = cfg.getAs<String>(MediaConfig::NdiSourceName, String());
        if (sourceName.isEmpty()) {
                promekiErr("NdiMediaIO: NdiSourceName is empty — set it via the config "
                           "key or pass an ndi:// URL to MediaIO::createFromUrl");
                return Error::InvalidArgument;
        }

        // Wait for NdiDiscovery to confirm the source is currently
        // advertised.  This translates a configured-by-name reference
        // into the canonical NDIlib_source_t we hand to recv_create_v3.
        // URL-derived references arrive here as full canonicals
        // ("<host> (<name>)"); a directly-configured NdiSourceName may
        // be a full canonical or a source-only pattern (matches any
        // machine on the network advertising that name).  waitForSource
        // resolves either shape to the full canonical actually
        // registered by the SDK.  The wait timeout is the user's
        // NdiFindWait key — set it higher when chasing a slow-to-appear
        // source.
        const Duration findWait    = cfg.getAs<Duration>(MediaConfig::NdiFindWait, Duration::fromSeconds(3));
        const int      findWaitMs  = static_cast<int>(findWait.milliseconds());
        const String   canonical   = NdiDiscovery::instance().waitForSource(sourceName, findWaitMs);
        if (canonical.isEmpty()) {
                // Snapshot what discovery *did* see so the user can tell
                // the difference between "no NDI traffic at all" and
                // "I'm asking for the wrong name".
                const auto snapshot = NdiDiscovery::instance().sources();
                if (snapshot.isEmpty()) {
                        promekiErr("NdiMediaIO: NDI source '%s' not found within %d ms — "
                                   "discovery registry is empty (is any NDI sender running on this "
                                   "subnet, and is mDNS/Bonjour reachable?)",
                                   sourceName.cstr(), findWaitMs);
                } else {
                        String visible;
                        for (const auto &r : snapshot) {
                                if (!visible.isEmpty()) visible += ", ";
                                visible += "'" + r.canonicalName + "'";
                        }
                        promekiErr("NdiMediaIO: NDI source '%s' not found within %d ms — "
                                   "discovery sees: %s",
                                   sourceName.cstr(), findWaitMs, visible.cstr());
                }
                return Error::NotFound;
        }

        // Snapshot the discovery registry to pull the URL address
        // (optional but useful for diagnostics + for receivers behind
        // NAT that need an explicit endpoint hint).
        String urlAddress;
        for (const auto &r : NdiDiscovery::instance().sources()) {
                if (r.canonicalName == canonical) {
                        urlAddress = r.urlAddress;
                        break;
                }
        }

        _captureTimeoutMs = cfg.getAs<int>(MediaConfig::NdiCaptureTimeoutMs, 100);
        const Enum bandwidthEnum   = cfg.get(MediaConfig::NdiBandwidth).asEnum(NdiBandwidth::Type);
        const Enum colorFormatEnum = cfg.get(MediaConfig::NdiColorFormat).asEnum(NdiColorFormat::Type);
        const Enum bitDepthEnum    = cfg.get(MediaConfig::NdiReceiveBitDepth).asEnum(NdiReceiveBitDepth::Type);
        _bitDepthHint              = static_cast<int>(ndiBitDepthFor(bitDepthEnum));

        NDIlib_source_t srcRef;
        srcRef.p_ndi_name     = canonical.cstr();
        srcRef.p_url_address  = urlAddress.isEmpty() ? nullptr : urlAddress.cstr();

        NDIlib_recv_create_v3_t rc;
        rc.source_to_connect_to = srcRef;
        rc.color_format         = ndiColorFormatFor(colorFormatEnum);
        rc.bandwidth            = static_cast<NDIlib_recv_bandwidth_e>(ndiBandwidthFor(bandwidthEnum));
        rc.allow_video_fields   = false;
        rc.p_ndi_recv_name      = nullptr;

        const NDIlib_v6 *api = NdiLib::instance().api();
        if (!api || !api->recv_create_v3 || !api->recv_destroy ||
            !api->recv_capture_v3 || !api->recv_free_video_v2 ||
            !api->recv_free_audio_v3 || !api->recv_free_metadata) {
                promekiErr("NdiMediaIO: NDI function table missing required recv_* entries");
                return Error::LibraryFailure;
        }
        _recv = api->recv_create_v3(&rc);
        if (!_recv) {
                promekiErr("NdiMediaIO: NDIlib_recv_create_v3 returned NULL for source '%s'",
                           canonical.cstr());
                return Error::LibraryFailure;
        }

        // The image / audio descriptors are populated lazily by the
        // capture thread once the first frame arrives.  Until then we
        // provide a placeholder shape so the open path's port-group
        // wiring has something concrete to attach.
        if (!_imageDesc.isValid()) {
                _imageDesc = ImageDesc(Size2Du32(1920, 1080),
                                       PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
        }
        if (!_frameRate.isValid()) {
                _frameRate = FrameRate(FrameRate::FPS_30);
        }

        // Seed the audio shape from the config defaults so the
        // resolved MediaDesc declares an audio descriptor at open
        // time — without it the pipeline planner sees a video-only
        // source and never wires audio out to downstream sinks.  The
        // capture loop overwrites _audioRing's actual sample rate /
        // channel count when the first NDI audio frame arrives.
        _audioSampleRate = cfg.getAs<float>(MediaConfig::AudioRate, 48000.0f);
        _audioChannels   = static_cast<size_t>(cfg.getAs<int32_t>(MediaConfig::AudioChannels, 2));

        _stopFlag.store(false, std::memory_order_release);
        _readCancelled.store(false, std::memory_order_release);
        _framesReceived.store(0, std::memory_order_relaxed);
        _audioFramesReceived.store(0, std::memory_order_relaxed);
        _metadataReceived.store(0, std::memory_order_relaxed);
        _droppedReceives.store(0, std::memory_order_relaxed);

        _captureThread = std::thread([this] { captureLoop(); });
        promekiInfo("NdiMediaIO: receiver opened for source '%s' (bandwidth=%s, color=%s)",
                    sourceName.cstr(), bandwidthEnum.toString().cstr(),
                    colorFormatEnum.toString().cstr());
        return Error::Ok;
}

void NdiMediaIO::closeSource() {
        if (!_recv) return;
        _stopFlag.store(true, std::memory_order_release);
        if (_captureThread.joinable()) {
                _captureThread.join();
        }
        const NDIlib_v6 *api = NdiLib::instance().api();
        if (api && api->recv_destroy) {
                api->recv_destroy(_recv);
        }
        _recv = nullptr;
        // Drain the queue so any payloads accumulated on the way out
        // release their backing Buffers cleanly.  tryPop is the
        // non-blocking variant — pop(0) waits indefinitely.
        while (true) {
                auto popResult = _videoQueue.tryPop();
                if (popResult.second().isError()) break;
        }
        // Reset the per-stream audio timeline state so the next open
        // starts from a clean slate.  No mutex needed — the capture
        // thread is already joined.
        _audioFirstSampleTicks = 0;
        _audioNextSampleTicks  = 0;
        _audioMarkersSinceDrain.clear();
        _audioSilenceSamples.store(0, std::memory_order_relaxed);
        _audioGapEvents.store(0, std::memory_order_relaxed);
        _audioRing.clear();
}

void NdiMediaIO::ingestNdiAudio(int64_t timestampTicks, size_t samples, size_t channels, float rate,
                                const uint8_t *planarFloatData, size_t channelStrideBytes) {
        if (samples == 0 || channels == 0 || rate <= 0.0f || planarFloatData == nullptr) return;

        // Lazy-rebuild the ring on first use or on shape change.
        AudioDesc ringDesc(AudioFormat(AudioFormat::PCMI_Float32LE), rate,
                           static_cast<unsigned int>(channels));
        AudioDesc pushDesc(AudioFormat(AudioFormat::PCMP_Float32LE), rate,
                           static_cast<unsigned int>(channels));

        Mutex::Locker lk(_audioMutex);
        if (!_audioRing.format().isValid() ||
            _audioRing.format().sampleRate() != ringDesc.sampleRate() ||
            _audioRing.format().channels() != ringDesc.channels()) {
                // Reserve one second of headroom — well beyond the
                // inter-Read drain interval at any reasonable video
                // frame rate, while small enough to amount to ~ 384 KiB
                // at 48 kHz stereo float.  Shape-change clears the
                // timeline anchor so the next push relatches.
                _audioRing             = AudioBuffer(ringDesc, static_cast<size_t>(rate));
                _audioFirstSampleTicks = 0;
                _audioNextSampleTicks  = 0;
                _audioMarkersSinceDrain.clear();
        }

        // Gap detection.  NDI ticks are 100 ns; convert sample counts
        // and gap durations to/from ticks via the source rate.
        constexpr int64_t kTicksPerSecond = 10'000'000;
        constexpr int64_t kMaxGapTicks    = kTicksPerSecond; // 1 s — beyond this we treat the
                                                              // stream as restarted instead of
                                                              // bridging.
        // Treat |timestamp - prediction| <= kJitterTolTicks as
        // sender-side timestamp jitter rather than a real timeline
        // event.  Senders typically run their audio off a
        // sample-locked clock but stamp frames with a coarser system
        // clock, producing per-frame deviations of a few hundred
        // microseconds to a couple of milliseconds in either
        // direction.  Without this window every NDI source produces
        // a steady stream of "regressed by 0.x ms" warnings (and we
        // would also be inserting a few dozen samples of phantom
        // silence on every positive jitter, which is audibly worse
        // than ignoring the timestamp noise).  5 ms ≈ 240 samples at
        // 48 kHz — well below the threshold of audible drift, well
        // above any realistic timestamp quantization.
        constexpr int64_t kJitterTolTicks = 50'000;
        const double      ticksPerSample  = static_cast<double>(kTicksPerSecond) / static_cast<double>(rate);
        const int64_t     frameDurTicks   = static_cast<int64_t>(static_cast<double>(samples) * ticksPerSample);

        const bool haveSenderTs    = (timestampTicks > 0);

        // When the deviation falls inside the jitter window we
        // ignore the new timestamp altogether and keep predicting
        // the next-sample anchor from the running sample count.
        // Real gaps and real backwards jumps still re-anchor on the
        // observed timestamp.
        bool       insideJitterWindow = false;

        // Bridge any gap that opened between the previous frame's
        // expected next sample and this frame's first sample.
        if (haveSenderTs && _audioNextSampleTicks > 0) {
                const int64_t gapTicks = timestampTicks - _audioNextSampleTicks;
                const int64_t absGap   = gapTicks < 0 ? -gapTicks : gapTicks;
                if (absGap <= kJitterTolTicks) {
                        // Sub-jitter — silently absorb.  No silence
                        // injection, no warning, no timeline shift
                        // beyond the sample count itself.
                        insideJitterWindow = true;
                } else if (gapTicks > 0) {
                        if (gapTicks > kMaxGapTicks) {
                                // Treat this as a sender restart —
                                // the gap is too large to bridge with
                                // silence without injecting an
                                // audible run.  Drop pending samples
                                // and re-anchor on the new frame.
                                promekiWarn("NdiMediaIO: audio timeline gap %.3f s exceeds 1 s — "
                                            "discarding %zu buffered samples and re-anchoring",
                                            static_cast<double>(gapTicks) / kTicksPerSecond,
                                            _audioRing.available());
                                _audioRing.clear();
                                _audioFirstSampleTicks = 0;
                                _audioNextSampleTicks  = 0;
                                _audioMarkersSinceDrain.clear();
                        } else {
                                // Round down to whole samples — we
                                // can only fill discrete sample
                                // counts, and a sub-sample remainder
                                // doesn't accumulate (it gets
                                // absorbed into _audioNextSampleTicks
                                // for the next frame's gap calc).
                                const int64_t gapSamples =
                                        static_cast<int64_t>(static_cast<double>(gapTicks) / ticksPerSample);
                                if (gapSamples > 0) {
                                        const int64_t markerOffset =
                                                static_cast<int64_t>(_audioRing.available());
                                        Error se = _audioRing.pushSilence(static_cast<size_t>(gapSamples));
                                        if (se.isError()) {
                                                promekiWarn("NdiMediaIO: audio gap silence-fill (%lld samples) "
                                                            "failed (%s) — gap left unbridged",
                                                            static_cast<long long>(gapSamples), se.name().cstr());
                                        } else {
                                                _audioMarkersSinceDrain.append(
                                                        markerOffset, gapSamples, AudioMarkerType::SilenceFill);
                                                _audioSilenceSamples.fetch_add(gapSamples,
                                                                               std::memory_order_relaxed);
                                                _audioGapEvents.fetch_add(1, std::memory_order_relaxed);
                                                if (_audioFirstSampleTicks == 0) {
                                                        // Ring was empty — the
                                                        // synthesized silence is
                                                        // the first sample of the
                                                        // next drain, so anchor
                                                        // PTS at the gap start.
                                                        _audioFirstSampleTicks = _audioNextSampleTicks;
                                                }
                                                _audioNextSampleTicks += gapSamples *
                                                                         static_cast<int64_t>(ticksPerSample);
                                        }
                                }
                        }
                } else {
                        // Real backwards jump (beyond jitter
                        // tolerance).  Could be sender restart with
                        // a smaller-than-1s rewind, packet reorder,
                        // or clock-source change — any way we don't
                        // want to insert negative silence.  Push the
                        // samples and let _audioNextSampleTicks
                        // re-anchor below.
                        promekiWarn("NdiMediaIO: audio timestamp regressed by %.3f ms — "
                                    "treating as continuous",
                                    static_cast<double>(absGap) / 10'000.0);
                }
        }

        // Push the real audio samples.  Tightly-packed planar input
        // goes directly to the ring; padded planar is coalesced to a
        // scratch buffer first so the planar fast-path can take it.
        const size_t tightStride = samples * sizeof(float);
        Error        pe;
        if (channelStrideBytes == tightStride) {
                pe = _audioRing.push(planarFloatData, samples, pushDesc);
        } else {
                Buffer packed(channels * tightStride);
                if (!packed.isValid()) {
                        pe = Error::NoMem;
                } else {
                        uint8_t *dst = static_cast<uint8_t *>(packed.data());
                        for (size_t c = 0; c < channels; ++c) {
                                std::memcpy(dst + c * tightStride,
                                            planarFloatData + c * channelStrideBytes, tightStride);
                        }
                        pe = _audioRing.push(packed.data(), samples, pushDesc);
                }
        }
        if (pe.isError()) {
                promekiWarn("NdiMediaIO: audio ring push failed (%s) — %zu samples dropped",
                            pe.name().cstr(), samples);
                return;
        }

        // Update the timeline anchors.  When the ring was empty
        // before this push, the first real sample becomes the PTS
        // anchor.  For the next-sample anchor, we either advance
        // the running prediction (jitter case — keep the prior
        // anchor as the source of truth, the new timestamp is
        // assumed noisy) or re-anchor on the new timestamp (real
        // gap / regression / first frame).
        if (_audioFirstSampleTicks == 0 && haveSenderTs) {
                _audioFirstSampleTicks = timestampTicks;
        }
        if (haveSenderTs) {
                if (insideJitterWindow) {
                        _audioNextSampleTicks += frameDurTicks;
                } else {
                        _audioNextSampleTicks = timestampTicks + frameDurTicks;
                }
        }
}

void NdiMediaIO::captureLoop() {
        Thread::setCurrentThreadName("ndi-capture");

        const NDIlib_v6 *api = NdiLib::instance().api();
        if (!api) return;

        while (!_stopFlag.load(std::memory_order_acquire)) {
                NDIlib_video_frame_v2_t   vframe = {};
                NDIlib_audio_frame_v3_t   aframe = {};
                NDIlib_metadata_frame_t   mframe = {};
                NDIlib_frame_type_e       ft = api->recv_capture_v3(
                        _recv, &vframe, &aframe, &mframe,
                        static_cast<uint32_t>(_captureTimeoutMs));

                switch (ft) {
                        case NDIlib_frame_type_video: {
                                // Translate the SDK's FourCC into a
                                // promeki PixelFormat::ID, honoring the
                                // user-supplied bit-depth hint for
                                // P216 sources (the FourCC alone does
                                // not say whether the high bits are 10,
                                // 12, or 16-bit content).
                                PixelFormat::ID pid = NdiFormat::fourccToPixelFormat(
                                        vframe.FourCC,
                                        static_cast<NdiFormat::BitDepth>(_bitDepthHint));
                                if (pid == PixelFormat::Invalid) {
                                        promekiErr("NdiMediaIO: capture got unsupported FourCC %s",
                                                   NdiFormat::fourccToString(vframe.FourCC).cstr());
                                        api->recv_free_video_v2(_recv, &vframe);
                                        _droppedReceives.fetch_add(1, std::memory_order_relaxed);
                                        break;
                                }
                                ImageDesc desc(Size2Du32(static_cast<uint32_t>(vframe.xres),
                                                         static_cast<uint32_t>(vframe.yres)),
                                               PixelFormat(pid));
                                // Pin the SDK frame's bytes into a
                                // promeki Buffer so the payload can
                                // outlive recv_free_video_v2 below.
                                // Sum every plane's byte count to get
                                // the total — the SDK lays the planes
                                // out contiguously back-to-back, so a
                                // single Buffer covers the full
                                // payload.
                                const PixelFormat &pf = desc.pixelFormat();
                                size_t totalBytes = 0;
                                for (size_t p = 0; p < pf.planeCount(); ++p) {
                                        totalBytes += pf.planeSize(p, desc);
                                }
                                if (totalBytes == 0 && vframe.line_stride_in_bytes > 0) {
                                        totalBytes = static_cast<size_t>(vframe.line_stride_in_bytes) *
                                                     static_cast<size_t>(vframe.yres);
                                }
                                Buffer buf = Buffer(totalBytes);
                                std::memcpy(buf.data(), vframe.p_data, totalBytes);
                                BufferView                    view(buf, 0, totalBytes);
                                UncompressedVideoPayload::Ptr vp =
                                        UncompressedVideoPayload::Ptr::create(desc, view);
                                // Attach two timestamps:
                                //   PTS (sender-anchored) — the
                                //   moment the sender submitted the
                                //   frame, in NdiClock domain.  This
                                //   is the canonical media timestamp.
                                //   DTS == PTS because NDI does not
                                //   carry B-frame reorder.
                                //   CaptureTime (this-machine local)
                                //   — when our capture thread first
                                //   saw the packet, in SystemMonotonic.
                                //   Useful for measuring transit
                                //   latency (CaptureTime - PTS) and
                                //   per-stream jitter.
                                MediaTimeStamp pts = ndiTimestampToPts(vframe.timestamp);
                                if (pts.isValid()) {
                                        vp.modify()->setPts(pts);
                                        vp.modify()->setDts(pts);
                                }
                                vp.modify()->metadata().set(
                                        Metadata::CaptureTime, localArrivalCaptureTime());
                                // Update the cached image desc / frame
                                // rate so subsequent open-side queries
                                // reflect the source's actual shape.
                                _imageDesc = desc;
                                if (vframe.frame_rate_N > 0 && vframe.frame_rate_D > 0) {
                                        _frameRate = FrameRate(FrameRate::RationalType(
                                                static_cast<unsigned int>(vframe.frame_rate_N),
                                                static_cast<unsigned int>(vframe.frame_rate_D)));
                                }
                                // Drive the per-stream NdiClock from
                                // the SDK's per-frame timestamp.  This
                                // is the only place the receiver-side
                                // clock advances; downstream stages
                                // that pull from MediaIOPortGroup get
                                // sender-anchored absolute time for
                                // free.
                                if (_sourceClock.isValid()) {
                                        auto *nc = static_cast<NdiClock *>(_sourceClock.modify());
                                        nc->setLatestTimestamp(vframe.timestamp);
                                        nc->setFrameRate(_frameRate);
                                }

                                // Bounded queue: drop oldest on overflow
                                // so steady-state latency stays bounded
                                // and the consumer sees the freshest
                                // frame.  Same policy as V4L2.
                                while (_videoQueue.size() >= VideoQueueDepth) {
                                        auto dropped = _videoQueue.tryPop();
                                        if (dropped.second().isOk()) {
                                                _droppedReceives.fetch_add(1, std::memory_order_relaxed);
                                        } else {
                                                break;
                                        }
                                }
                                _videoQueue.push(std::move(vp));
                                _framesReceived.fetch_add(1, std::memory_order_relaxed);
                                api->recv_free_video_v2(_recv, &vframe);
                                break;
                        }
                        case NDIlib_frame_type_audio: {
                                // Hand the per-frame metadata over to
                                // ingestNdiAudio, which owns the gap
                                // detection / silence-fill /
                                // marker-list bookkeeping.  The SDK's
                                // @c -1 (NDIlib_recv_timestamp_undefined)
                                // is forwarded as-is — ingestNdiAudio
                                // skips gap detection on those frames.
                                const size_t samples  = static_cast<size_t>(aframe.no_samples);
                                const size_t channels = static_cast<size_t>(aframe.no_channels);
                                const float  rate     = static_cast<float>(aframe.sample_rate);
                                ingestNdiAudio(static_cast<int64_t>(aframe.timestamp), samples, channels, rate,
                                               aframe.p_data,
                                               static_cast<size_t>(aframe.channel_stride_in_bytes));
                                _audioFramesReceived.fetch_add(1, std::memory_order_relaxed);
                                api->recv_free_audio_v3(_recv, &aframe);
                                break;
                        }
                        case NDIlib_frame_type_metadata: {
                                // NDI metadata is XML.  We don't
                                // currently parse it into the project's
                                // structured Metadata — just stash the
                                // raw string under a single key so the
                                // read path can hand it through and
                                // future revisions can layer parsing
                                // on top.
                                if (mframe.p_data) {
                                        Mutex::Locker lk(_metadataMutex);
                                        _pendingMetadata.set(Metadata::ID("NdiXml"),
                                                             String(mframe.p_data));
                                        _hasPendingMetadata = true;
                                }
                                _metadataReceived.fetch_add(1, std::memory_order_relaxed);
                                api->recv_free_metadata(_recv, &mframe);
                                break;
                        }
                        case NDIlib_frame_type_error:
                                promekiErr("NdiMediaIO: capture got frame_type_error — receiver "
                                           "lost connection?");
                                _stopFlag.store(true, std::memory_order_release);
                                break;
                        case NDIlib_frame_type_status_change:
                        case NDIlib_frame_type_source_change:
                        case NDIlib_frame_type_none:
                        default:
                                // Nothing to free here — recv_capture_v3
                                // only fills the matching frame struct
                                // for the kind it returns.
                                break;
                }
        }
}

// ---------------------------------------------------------------------------
// NdiFactory
// ---------------------------------------------------------------------------

bool NdiFactory::canHandlePath(const String &path) const {
        // Recognise both ndi:// and ndi: forms; lower-case scheme only.
        return path.startsWith(String("ndi://")) || path.startsWith(String("ndi:"));
}

StringList NdiFactory::enumerate() const {
        // Snapshot the always-on discovery registry.  The 500ms
        // minimum-uptime gives a fresh process a chance to populate
        // the registry before the first probe; later calls (after
        // the discovery thread has been up for a while) return
        // instantly.  Each canonical name "MACHINE (Source)" is split
        // back into authority + path so the emitted URL round-trips
        // through urlToConfig as the same source.
        StringList urls;
        for (const auto &r : NdiDiscovery::instance().sources(500)) {
                const String &canon = r.canonicalName;
                size_t        open  = canon.find(" (");
                if (open == String::npos || !canon.endsWith(String(")"))) {
                        // Unexpected canonical shape — emit it under
                        // the "this machine" form so it at least parses
                        // and points at the right name on this box.
                        urls.pushToBack(String("ndi:///") + canon);
                        continue;
                }
                const String host   = canon.left(open);
                const String name   = canon.substr(open + 2, canon.size() - open - 3);
                urls.pushToBack(String("ndi://") + host + String("/") + name);
        }
        return urls;
}

Error NdiFactory::urlToConfig(const Url &url, Config *outConfig) const {
        if (outConfig == nullptr) return Error::InvalidArgument;
        // URL forms (the path component is always the bare NDI source
        // / sender name, never a canonical "Machine (Source)" string):
        //   ndi://<host>/<name> → name on <host>
        //   ndi:///<name>       → name on this machine
        //
        // Direction is taken from MediaConfig::OpenMode (Read or Write)
        // — the same URL is openable for either direction:
        //   Read  → resolves via NdiDiscovery against
        //           "<host-or-this-machine> (<name>)".
        //   Write → uses <name> as the NDI sender name; the URL host
        //           must be empty or match this machine, since NDI
        //           senders can only run on the local box.  The
        //           sink-open path enforces that — urlToConfig just
        //           plumbs both keys through.
        const String host = url.host();
        String       path = url.path();
        // url.path() includes the leading `/` for ndi://Host/Source
        // form; strip it so concatenation does the right thing.
        if (!path.isEmpty() && path[0] == '/') path = path.substr(1);
        if (path.isEmpty()) {
                promekiErr("NdiMediaIO: URL '%s' is missing the source name "
                           "(expected ndi://<host>/<name> or ndi:///<name>)",
                           url.toString().cstr());
                return Error::InvalidArgument;
        }
        // Sender name is the bare path — NDI prefixes the local
        // machine name itself when advertising the source.
        outConfig->set(MediaConfig::NdiSendName, path);
        // Source canonical: explicit host wins; otherwise this machine.
        // NDI's discovery registry uses the bare hostname as the
        // machine prefix, so System::hostname() is the right "any
        // host" stand-in here.
        const String resolvedHost = host.isEmpty() ? System::hostname() : host;
        const String canonical    = resolvedHost.isEmpty() ? path : (resolvedHost + " (" + path + ")");
        outConfig->set(MediaConfig::NdiSourceName, canonical);
        return Error::Ok;
}

NdiFactory::Config::SpecMap NdiFactory::configSpecs() const {
        Config::SpecMap specs;
        auto            s = [&specs](MediaConfig::ID id, const Variant &def) {
                const VariantSpec *gs = MediaConfig::spec(id);
                specs.insert(id, gs ? VariantSpec(*gs).setDefault(def) : VariantSpec().setDefault(def));
        };
        // Media descriptor knobs — same defaults as RtpMediaIO so
        // CLI invocations behave consistently.
        s(MediaConfig::FrameRate, FrameRate(FrameRate::FPS_30));
        s(MediaConfig::VideoSize, Size2Du32(1920, 1080));
        s(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709));
        s(MediaConfig::AudioRate, 48000.0f);
        s(MediaConfig::AudioChannels, int32_t(2));

        // NDI-specific defaults.
        s(MediaConfig::NdiSourceName, String());
        s(MediaConfig::NdiSendName, String("promeki"));
        s(MediaConfig::NdiSendGroups, String());
        s(MediaConfig::NdiExtraIps, String());
        s(MediaConfig::NdiBandwidth, NdiBandwidth::Highest);
        // Fastest returns the format already on the wire — UYVY for
        // 8-bit sources, P216 for 10/12/16-bit — both of which we
        // decode.  Best is the SDK's "with alpha, full 16-bit" path,
        // which on the Advanced SDK delivers PA16 (4:2:2:4 planar
        // 16-bit) — a FourCC we deliberately do not handle yet.
        // Callers that need PA16 must opt in explicitly once the
        // backend grows a decoder for it.
        s(MediaConfig::NdiColorFormat, NdiColorFormat::Fastest);
        s(MediaConfig::NdiSendClockVideo, true);
        s(MediaConfig::NdiSendClockAudio, true);
        s(MediaConfig::NdiCaptureTimeoutMs, int32_t(100));
        s(MediaConfig::NdiFindWait, Duration::fromSeconds(3));
        s(MediaConfig::NdiReceiveBitDepth, NdiReceiveBitDepth::Auto);
        return specs;
}

MediaIO *NdiFactory::create(const Config &config, ObjectBase *parent) const {
        auto *io = new NdiMediaIO(parent);
        io->setConfig(config);
        return io;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
