/**
 * @file      v4l2videodecoder.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/v4l2videodecoder.h>

#if PROMEKI_ENABLE_V4L2

#include <cstring>
#include <cstdint>
#include <linux/videodev2.h>

#include <promeki/v4l2m2mcodec.h>
#include <promeki/v4l2rawformat.h>
#include <promeki/v4l2captionsei.h>
#include <promeki/videoencodersei.h>
#include <promeki/anctranslator.h>
#include <promeki/ancpacket.h>
#include <promeki/ancformat.h>
#include <promeki/enums_anc.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/framerate.h>
#include <promeki/duration.h>
#include <promeki/rational.h>
#include <promeki/logger.h>
#include <promeki/list.h>
#include <promeki/deque.h>
#include <promeki/map.h>
#include <promeki/string.h>
#include <promeki/mediatimestamp.h>
#include <promeki/metadata.h>
#include <promeki/videocodec.h>
#include <promeki/backendweight.h>
#include <promeki/size2d.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        struct CodecMapping {
                        uint32_t fourcc;
                        bool     valid = false;
        };

        CodecMapping mapCodec(VideoCodec::ID id) {
                switch (id) {
                        case VideoCodec::H264: return {V4L2_PIX_FMT_H264, true};
                        case VideoCodec::HEVC: return {V4L2_PIX_FMT_HEVC, true};
                        default: return {};
                }
        }

} // namespace

// ---------------------------------------------------------------------------
// V4l2VideoDecoder::Impl — owns the mem2mem engine and session state.
// ---------------------------------------------------------------------------

struct V4l2VideoDecoder::Impl {
                // Ceiling on the in-flight source-Frame map.  Comfortably above
                // any decoder reorder depth (DPB), so a well-behaved driver
                // never hits it; it only bounds the leak if a driver fails to
                // copy the buffer timestamp OUTPUT→CAPTURE (so buildOutput can
                // never match and erase the entry).
                static constexpr size_t kMaxInflight = 64;

                VideoCodec::ID codecId = VideoCodec::H264;
                CodecMapping   mapping;

                V4l2M2mCodec codec;
                bool         opened = false;

                MediaConfig cfg;
                FrameRate   sessionFrameRate;

                // Monotonic submission counter used as the V4L2 OUTPUT buffer
                // timestamp so decoded CAPTURE frames (which a temporal codec
                // may reorder) can be matched back to their source Frame.
                int64_t inputCounter = 0;

                struct SourceInfo {
                                Frame          source;
                                MediaTimeStamp pts;
                                Metadata       meta;
                };
                Map<int64_t, SourceInfo> inflight;

                Deque<Frame> ready;

                // Caption SEI: a hardware decoder discards SEI it doesn't act
                // on, so CEA-708 caption SEI is parsed out of the input
                // bitstream in software (see attachCaptions).
                bool          captionsEnabled = true;
                AncTranslator ancTranslator;

                bool isHevc() const { return mapping.fourcc == V4L2_PIX_FMT_HEVC; }

                // Recovers CEA-708 caption SEI from the source compressed access
                // unit and attaches it to the decoded output Frame.
                void attachCaptions(Frame &outFrame, const Frame &source) {
                        if (!captionsEnabled || !source.isValid()) return;
                        CompressedVideoPayload::Ptr cin = VideoDecoder::selectInputPayload(source);
                        if (!cin.isValid() || !cin->isValid()) return;
                        List<Buffer> bodies = v4l2ExtractSeiPayloads(
                                cin->data(), VideoEncoderSei::TypeUserDataRegistered, isHevc());
                        for (const Buffer &body : bodies) {
                                AncPacket hls(AncFormat(AncFormat::Cea708), AncTransport::HlsSei, body);
                                AncTranslator::PacketsResult r = ancTranslator.translate(hls, AncTransport::St291);
                                if (error(r).isError()) continue; // not a caption SEI / unparseable
                                for (const AncPacket &pkt : value(r)) {
                                        VideoDecoder::attachExtractedAnc(outFrame, pkt, /*videoStreamIndex=*/0);
                                }
                        }
                }
                bool         eosPending = false;

                Error  lastError;
                String lastErrorMessage;

                explicit Impl(VideoCodec::ID id) : codecId(id), mapping(mapCodec(id)) {}
                ~Impl() { codec.close(); }

                Error setError(Error err, const String &msg) {
                        lastError = err;
                        lastErrorMessage = msg;
                        promekiErr("V4l2VideoDecoder: %s", msg.cstr());
                        return err;
                }
                void clearError() {
                        lastError = Error::Ok;
                        lastErrorMessage = String();
                }

                // Open the device and stream the OUTPUT (coded) queue on the
                // first submitted access unit.  The CAPTURE (raw) queue is left
                // down until the driver raises its first SOURCE_CHANGE.
                Error ensureSession(const ImageDesc &cdesc) {
                        if (opened) return Error::Ok;
                        if (!mapping.valid) {
                                return setError(Error::NotSupported, "V4l2VideoDecoder: unsupported codec family");
                        }

                        V4l2M2mCodec::OpenParams op;
                        op.role = V4l2M2mCodec::Role::Decoder;
                        op.devPath = cfg.getAs<String>(MediaConfig::V4l2DevicePath, String());
                        op.outputFourcc = mapping.fourcc;             // coded in
                        op.captureFourcc = V4L2_PIX_FMT_NV12;         // raw out (pinned if supported)
                        op.size = cdesc.size().isValid() ? cdesc.size() : Size2Du32(1920, 1080);
                        if (Error err = codec.open(op); err.isError()) {
                                return setError(err, String::sprintf("V4l2VideoDecoder: open failed (%s)",
                                                                     err.name().cstr()));
                        }
                        if (Error err = codec.startOutput(); err.isError()) {
                                codec.close();
                                return setError(err, "V4l2VideoDecoder: OUTPUT STREAMON failed");
                        }
                        opened = true;
                        promekiInfo("V4l2VideoDecoder: %s on %s (%s)",
                                    mapping.fourcc == V4L2_PIX_FMT_H264 ? "H264" : "HEVC",
                                    codec.devPath().cstr(), codec.driverName().cstr());
                        clearError();
                        return Error::Ok;
                }

                // Assemble one decoded CAPTURE frame into an output Frame whose
                // PixelFormat tracks the raw format the driver negotiated (NV12
                // for 8-bit 4:2:0, NV16 for 4:2:2, P010 for 10-bit, …),
                // threading pts / source / metadata back from the matching
                // submitted access unit.
                void buildOutput(const List<V4l2M2mCodec::CapturePlane> &planes, int64_t key) {
                        if (planes.isEmpty()) return;
                        SourceInfo si;
                        if (inflight.contains(key)) {
                                si = inflight.value(key);
                                inflight.remove(key);
                        }
                        const uint32_t w = codec.captureWidth();
                        const uint32_t h = codec.captureHeight();
                        if (w == 0 || h == 0) return;

                        const V4l2RawFormat *rf = v4l2RawFormatForFourcc(codec.captureFourcc());
                        if (!rf) {
                                promekiWarnThrottled(1000,
                                                     "V4l2VideoDecoder: driver CAPTURE format not supported "
                                                     "for output mapping");
                                return;
                        }

                        auto img = UncompressedVideoPayload::allocate(
                                ImageDesc(w, h, PixelFormat(rf->pixelFormatId)));
                        if (!img.isValid()) return;

                        UncompressedVideoPayload *m = img.modify();
                        v4l2UnpackSemiPlanar(planes, *rf, m);

                        m->setPts(si.pts);
                        if (!si.meta.isEmpty()) m->metadata() = si.meta;
                        if (sessionFrameRate.isValid()) {
                                Rational<int> r(static_cast<int>(sessionFrameRate.numerator()),
                                                static_cast<int>(sessionFrameRate.denominator()));
                                m->setDuration(Duration::fromSamples(int64_t(1), r));
                        }
                        Frame outFrame = VideoDecoder::buildOutputFrame(si.source, std::move(img));
                        attachCaptions(outFrame, si.source);
                        ready.pushToBack(std::move(outFrame));
                }

                // Non-blocking: drain pending events (configuring CAPTURE on the
                // first SOURCE_CHANGE) and every decoded frame currently ready.
                void pump(bool &sawEos) {
                        for (;;) {
                                bool  sc = false, eos = false;
                                Error e = codec.dequeueEvent(sc, eos);
                                if (e != Error::Ok) break;
                                if (sc && !codec.captureConfigured()) {
                                        if (Error se = codec.setupCapture(); se.isError()) {
                                                setError(se, "V4l2VideoDecoder: setupCapture failed");
                                        } else {
                                                promekiInfo("V4l2VideoDecoder: source change → %ux%u",
                                                            codec.captureWidth(), codec.captureHeight());
                                        }
                                }
                                if (eos) sawEos = true;
                        }
                        if (!codec.captureConfigured()) return;
                        for (;;) {
                                List<V4l2M2mCodec::CapturePlane> planes;
                                int                              idx = -1;
                                int64_t                          pts = 0;
                                bool                             eos = false;
                                Error e = codec.dequeueRawFrame(planes, idx, pts, eos);
                                if (eos) sawEos = true;
                                if (e == Error::Ok) {
                                        buildOutput(planes, pts);
                                        if (!eos) codec.requeueRawFrame(idx);
                                        if (eos) break;
                                        continue;
                                }
                                break; // NotReady / error.
                        }
                }

                // Block up to timeoutMs for activity, then pump.
                void waitAndPump(int timeoutMs, bool &sawEos) {
                        bool ow = false, cr = false, evt = false;
                        codec.pollEvents(ow, cr, evt, timeoutMs);
                        pump(sawEos);
                }

                Error submit(const Frame &source, const CompressedVideoPayload &payload,
                             const MediaTimeStamp &pts) {
                        if (Error err = ensureSession(payload.desc()); err.isError()) return err;
                        if (payload.planeCount() == 0) return Error::Ok;

                        auto         view = payload.plane(0);
                        const size_t codedSize = view.size();
                        if (codedSize == 0) return Error::Ok;

                        // Acquire a free OUTPUT (coded) buffer, draining decoded
                        // output to free one if the queue is momentarily full.
                        int                          index = -1;
                        List<V4l2M2mCodec::OutPlane> planes;
                        Error                        a = codec.acquireOutput(index, planes);
                        for (int spins = 0; a != Error::Ok && spins < 64; ++spins) {
                                bool sawEos = false;
                                waitAndPump(50, sawEos);
                                a = codec.acquireOutput(index, planes);
                        }
                        if (a != Error::Ok) {
                                return setError(Error::Timeout, "V4l2VideoDecoder: no free OUTPUT buffer");
                        }
                        if (planes.isEmpty() || planes[0].data == nullptr) {
                                return setError(Error::DeviceError, "V4l2VideoDecoder: OUTPUT buffer has no plane");
                        }

                        const size_t n = (codedSize < planes[0].capacity) ? codedSize : planes[0].capacity;
                        if (n < codedSize) {
                                // The coded access unit does not fit the OUTPUT buffer the
                                // driver sized; truncating it would corrupt the decode.
                                // Bump V4l2M2mCodec::OpenParams::codedBufferSize for streams
                                // with large access units (e.g. high-bitrate 4K IDRs).
                                promekiWarnThrottled(1000,
                                                     "V4l2VideoDecoder: coded access unit (%zu bytes) exceeds "
                                                     "OUTPUT buffer capacity (%zu bytes); decode will fail",
                                                     codedSize, planes[0].capacity);
                        }
                        std::memcpy(planes[0].data, view.data(), n);
                        List<size_t> used;
                        used.pushToBack(n);

                        while (inflight.size() >= kMaxInflight) inflight.remove(inflight.begin());
                        inflight.insert(inputCounter, SourceInfo{source, pts, payload.metadata()});
                        if (Error err = codec.submitOutput(index, used, inputCounter); err.isError()) {
                                inflight.remove(inputCounter);
                                return setError(err, "V4l2VideoDecoder: submitOutput failed");
                        }
                        ++inputCounter;

                        bool sawEos = false;
                        pump(sawEos);
                        return Error::Ok;
                }

                Frame receiveFrame() {
                        if (ready.isEmpty()) {
                                bool sawEos = false;
                                // A short wait lets the codec surface the first
                                // SOURCE_CHANGE / decoded frame after submit.
                                waitAndPump(opened ? 50 : 0, sawEos);
                        }
                        if (!ready.isEmpty()) return ready.popFromFront();
                        if (eosPending) {
                                eosPending = false;
                                ImageDesc cdesc(Size2Du32(0, 0),
                                                PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709));
                                auto      pkt = UncompressedVideoPayload::Ptr::create(cdesc);
                                pkt.modify()->markEndOfStream();
                                Frame out;
                                out.addPayload(pkt);
                                return out;
                        }
                        return Frame();
                }

                Error flush() {
                        if (!opened) {
                                eosPending = true;
                                return Error::Ok;
                        }
                        codec.sendStop();
                        bool sawEos = false;
                        for (int spins = 0; spins < 256 && !sawEos; ++spins) {
                                waitAndPump(200, sawEos);
                                if (sawEos) break;
                                // Give up if the codec produces nothing for a
                                // full timeout and the queues are idle.
                                if (ready.isEmpty() && spins > 4) {
                                        bool ow = false, cr = false, evt = false;
                                        if (codec.pollEvents(ow, cr, evt, 200) == Error::Timeout) {
                                                pump(sawEos);
                                                break;
                                        }
                                }
                        }
                        eosPending = true;
                        return Error::Ok;
                }

                Error reset() {
                        codec.close();
                        opened = false;
                        inflight.clear();
                        ready.clear();
                        eosPending = false;
                        inputCounter = 0;
                        clearError();
                        return Error::Ok;
                }
};

// ---------------------------------------------------------------------------
// V4l2VideoDecoder thin shims that forward to Impl.
// ---------------------------------------------------------------------------

V4l2VideoDecoder::V4l2VideoDecoder(VideoCodec::ID codecId) : _impl(ImplPtr::create(codecId)) {}

V4l2VideoDecoder::~V4l2VideoDecoder() = default;

List<int> V4l2VideoDecoder::supportedOutputList() {
        return v4l2SupportedRawPixelFormats();
}

void V4l2VideoDecoder::onConfigure(const MediaConfig &config) {
        _impl->cfg = config;
        const FrameRate fallback(FrameRate::RationalType(30, 1));
        FrameRate       fr = config.getAs<FrameRate>(MediaConfig::FrameRate, fallback);
        _impl->sessionFrameRate = fr.isValid() ? fr : fallback;
        _impl->captionsEnabled = config.getAs<bool>(MediaConfig::VideoSeiCaptionsEnabled, true);
}

Error V4l2VideoDecoder::submitFrame(const Frame &frame) {
        _impl->clearError();
        CompressedVideoPayload::Ptr payload = selectInputPayload(frame);
        if (!payload.isValid() || !payload->isValid()) {
                promekiWarnThrottled(1000,
                                     "V4l2VideoDecoder::submitFrame: no compressed video payload on frame");
                _lastError = Error::Invalid;
                _lastErrorMessage = "V4l2VideoDecoder: no compressed video payload on frame";
                return _lastError;
        }
        Error err = _impl->submit(frame, *payload, payload->pts());
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

Frame V4l2VideoDecoder::receiveFrame() {
        return _impl->receiveFrame();
}

Error V4l2VideoDecoder::flush() {
        _impl->clearError();
        Error err = _impl->flush();
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

Error V4l2VideoDecoder::reset() {
        _impl->clearError();
        Error err = _impl->reset();
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

// ---------------------------------------------------------------------------
// Backend registration — probes for hardware at static-init time and only
// registers codecs a real mem2mem decoder node can service.  Registered
// under the "V4L2" backend name for H.264 and HEVC.
// ---------------------------------------------------------------------------

namespace {

        struct V4l2DecoderRegistrar {
                        V4l2DecoderRegistrar() {
                                auto bk = VideoCodec::registerBackend("V4L2");
                                if (error(bk).isError()) return;
                                const VideoCodec::Backend backend = value(bk);

                                struct Entry {
                                                VideoCodec::ID id;
                                                uint32_t       fourcc;
                                };
                                const Entry entries[] = {
                                        {VideoCodec::H264, V4L2_PIX_FMT_H264},
                                        {VideoCodec::HEVC, V4L2_PIX_FMT_HEVC},
                                };
                                for (const Entry &e : entries) {
                                        String dev = V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Decoder,
                                                                              e.fourcc, V4L2_PIX_FMT_NV12);
                                        if (dev.isEmpty()) continue;
                                        const VideoCodec::ID id = e.id;
                                        VideoDecoder::registerBackend({
                                                .codecId = id,
                                                .backend = backend,
                                                .weight = BackendWeight::Vendored + 20,
                                                .supportedOutputs = V4l2VideoDecoder::supportedOutputList(),
                                                .factory = [id]() -> VideoDecoder * {
                                                        return new V4l2VideoDecoder(id);
                                                },
                                        });
                                        promekiInfo("V4l2VideoDecoder: registered %s decoder on %s",
                                                    e.fourcc == V4L2_PIX_FMT_H264 ? "H264" : "HEVC", dev.cstr());
                                }
                        }
        };

        static V4l2DecoderRegistrar _v4l2DecoderRegistrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
