/**
 * @file      v4l2videoencoder.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/v4l2videoencoder.h>

#if PROMEKI_ENABLE_V4L2

#include <cstring>
#include <cstdint>
#include <linux/videodev2.h>

#include <promeki/v4l2m2mcodec.h>
#include <promeki/v4l2rawformat.h>
#include <promeki/v4l2codecparams.h>
#include <promeki/v4l2captionsei.h>
#include <promeki/h264profilelevel.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/videoencodersei.h>
#include <promeki/anctranslator.h>
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
#include <promeki/enums_codec.h>
#include <promeki/enums_video.h>
#include <promeki/backendweight.h>
#include <promeki/size2d.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Codec family → V4L2 coded FourCC + the compressed PixelFormat stamped
        // on the emitted payload.  Anything not listed is unsupported.
        struct CodecMapping {
                        uint32_t        fourcc;
                        PixelFormat::ID codedFormat;
                        bool            valid = false;
        };

        CodecMapping mapCodec(VideoCodec::ID id) {
                switch (id) {
                        case VideoCodec::H264: return {V4L2_PIX_FMT_H264, PixelFormat::H264, true};
                        case VideoCodec::HEVC: return {V4L2_PIX_FMT_HEVC, PixelFormat::HEVC, true};
                        default: return {};
                }
        }

        // Map the generic rate-control mode onto the V4L2 bitrate-mode enum.
        int32_t v4l2BitrateMode(const Enum &rc) {
                if (rc == RateControlMode::CBR) return V4L2_MPEG_VIDEO_BITRATE_MODE_CBR;
                if (rc == RateControlMode::CQP) return V4L2_MPEG_VIDEO_BITRATE_MODE_CQ;
                return V4L2_MPEG_VIDEO_BITRATE_MODE_VBR;
        }

} // namespace

// ---------------------------------------------------------------------------
// V4l2VideoEncoder::Impl — owns the mem2mem engine and session state.
// ---------------------------------------------------------------------------

struct V4l2VideoEncoder::Impl {
                // Ceiling on the in-flight source-Frame map.  Comfortably above
                // any encoder reorder depth (DPB), so a well-behaved driver
                // never hits it; it only bounds the leak if a driver fails to
                // copy the buffer timestamp OUTPUT→CAPTURE (so pumpCapture can
                // never match and erase the entry).
                static constexpr size_t kMaxInflight = 64;

                VideoCodec::ID codecId = VideoCodec::H264;
                CodecMapping   mapping;

                V4l2M2mCodec codec;
                bool         opened = false;

                uint32_t             width = 0;
                uint32_t             height = 0;
                PixelFormat::ID      inputFmtId = PixelFormat::Invalid;
                const V4l2RawFormat *rawFmt = nullptr; ///< Selected raw input descriptor.

                MediaConfig cfg;
                FrameRate   sessionFrameRate;

                // Monotonic submission counter used as the V4L2 buffer timestamp
                // so emitted coded buffers can be matched back to their source
                // Frame (mirrors the x264 backend's i_pts trick).
                int64_t inputCounter = 0;

                struct SourceInfo {
                                Frame          source;
                                MediaTimeStamp pts;
                                Metadata       imageMeta;
                };
                Map<int64_t, SourceInfo> inflight;

                Deque<Frame> ready;
                bool         eosPending = false;

                // Caption SEI: the codec can't inject user-data SEI, so CEA-708
                // ANC on the source Frame is spliced into the bitstream in
                // software (see injectCaptions).  Default-constructed translator
                // is sufficient — the Cea708 → HlsSei builder is pure.
                bool          captionsEnabled = true;
                AncTranslator ancTranslator;

                Error  lastError;
                String lastErrorMessage;

                explicit Impl(VideoCodec::ID id) : codecId(id), mapping(mapCodec(id)) {}
                ~Impl() { codec.close(); }

                Error setError(Error err, const String &msg) {
                        lastError = err;
                        lastErrorMessage = msg;
                        promekiErr("V4l2VideoEncoder: %s", msg.cstr());
                        return err;
                }
                void clearError() {
                        lastError = Error::Ok;
                        lastErrorMessage = String();
                }

                // Reads a VUI colour config key as a raw H.273 numeric value,
                // falling back to the VariantSpec default when absent (mirrors
                // the x264 backend).
                uint32_t readColorEnum(MediaConfig::ID key) const {
                        const VariantSpec *s = MediaConfig::spec(key);
                        if (!cfg.contains(key)) {
                                Enum def = s ? s->defaultValue().get<Enum>() : Enum();
                                return static_cast<uint32_t>(def.value());
                        }
                        return static_cast<uint32_t>(cfg.getAs<Enum>(key).value());
                }

                // Computes the V4L2 colorimetry for the OUTPUT (raw) format from
                // the config colour overrides resolved against the input format,
                // so the codec writes a matching VUI into the bitstream.
                void applyColorimetry(const PixelFormat &pf, V4l2M2mCodec::OpenParams &op) const {
                        const uint32_t prim = readColorEnum(MediaConfig::VideoColorPrimaries);
                        const uint32_t trans = readColorEnum(MediaConfig::VideoTransferCharacteristics);
                        const uint32_t matrix = readColorEnum(MediaConfig::VideoMatrixCoefficients);
                        const uint32_t range = readColorEnum(MediaConfig::VideoRange);
                        const PixelFormat::H273ColorDescription col = pf.resolveH273(prim, trans, matrix, range);
                        const PixelFormat::VuiColorSignal       sig = PixelFormat::vuiColorSignal(col);
                        if (!sig.signalTypePresent) return;
                        const V4l2Colorimetry vc =
                                v4l2ColorimetryFromH273(sig.primaries, sig.transfer, sig.matrix, sig.fullRange);
                        op.colorspace = vc.colorspace;
                        op.ycbcrEnc = vc.ycbcrEnc;
                        op.xferFunc = vc.xferFunc;
                        op.quantization = vc.quantization;
                }

                // Open the device and program codec controls the first time a
                // frame's geometry is known.  Rejects mid-stream geometry /
                // format changes (the V4L2 stateful encoder requires a fresh
                // session for those).
                Error ensureSession(const PixelFormat &pf, uint32_t w, uint32_t h) {
                        if (opened) {
                                if (w != width || h != height) {
                                        return setError(Error::Invalid,
                                                        "V4l2VideoEncoder does not support mid-stream "
                                                        "resolution changes");
                                }
                                if (pf.id() != inputFmtId) {
                                        return setError(Error::Invalid,
                                                        "V4l2VideoEncoder does not support mid-stream "
                                                        "format changes");
                                }
                                return Error::Ok;
                        }

                        if (!mapping.valid) {
                                return setError(Error::NotSupported, "V4l2VideoEncoder: unsupported codec family");
                        }
                        const V4l2RawFormat *rf = v4l2RawFormatForPixelFormat(pf.id());
                        if (!rf) {
                                return setError(Error::PixelFormatNotSupported,
                                                String::sprintf("V4l2VideoEncoder: unsupported input format %s "
                                                                "(supported: NV12, NV16, P010)",
                                                                pf.name().cstr()));
                        }
                        rawFmt = rf;

                        V4l2M2mCodec::OpenParams op;
                        op.role = V4l2M2mCodec::Role::Encoder;
                        op.devPath = cfg.getAs<String>(MediaConfig::V4l2DevicePath, String());
                        op.outputFourcc = rf->fourcc;
                        op.captureFourcc = mapping.fourcc;
                        op.size = Size2Du32(w, h);
                        applyColorimetry(pf, op);
                        if (Error err = codec.open(op); err.isError()) {
                                return setError(err, String::sprintf("V4l2VideoEncoder: open failed (%s)",
                                                                     err.name().cstr()));
                        }

                        width = w;
                        height = h;
                        inputFmtId = pf.id();

                        programControls();

                        if (Error err = codec.start(); err.isError()) {
                                codec.close();
                                return setError(err, "V4l2VideoEncoder: STREAMON failed");
                        }
                        opened = true;
                        promekiInfo("V4l2VideoEncoder: %s %ux%u %s on %s (%s)",
                                    mapping.fourcc == V4L2_PIX_FMT_H264 ? "H264" : "HEVC", width, height,
                                    rawFmt ? rawFmt->name : "?", codec.devPath().cstr(),
                                    codec.driverName().cstr());
                        clearError();
                        return Error::Ok;
                }

                // Best-effort MediaConfig → V4L2 control mapping.  Drivers that
                // lack a given knob simply ignore it (setControl optional=true).
                void programControls() {
                        const Enum    rcEnum = cfg.getAs<Enum>(MediaConfig::VideoRcMode);
                        const int32_t bitrateKbps = cfg.getAs<int32_t>(MediaConfig::BitrateKbps);
                        const int32_t maxBitrateKbps = cfg.getAs<int32_t>(MediaConfig::MaxBitrateKbps);
                        const int32_t gopLength = cfg.getAs<int32_t>(MediaConfig::GopLength);
                        const int32_t idrInterval = cfg.getAs<int32_t>(MediaConfig::IdrInterval);

                        codec.setControl(V4L2_CID_MPEG_VIDEO_BITRATE_MODE, v4l2BitrateMode(rcEnum));
                        if (bitrateKbps > 0) {
                                codec.setControl(V4L2_CID_MPEG_VIDEO_BITRATE, bitrateKbps * 1000);
                                codec.setControl(V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 1);
                        }
                        if (maxBitrateKbps > 0) {
                                codec.setControl(V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, maxBitrateKbps * 1000);
                        }
                        const int32_t keyint = (idrInterval > 0) ? idrInterval : gopLength;
                        if (keyint > 0) {
                                codec.setControl(V4L2_CID_MPEG_VIDEO_GOP_SIZE, keyint);
                        }
                        // Self-contained streams: emit SPS/PPS joined with the
                        // first coded frame so every output is independently
                        // initialisable.
                        codec.setControl(V4L2_CID_MPEG_VIDEO_HEADER_MODE,
                                         V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);

                        programProfileLevel();
                        programHdrMetadata();
                }

                // Sets the HDR10 static-metadata compound controls (mastering
                // display + content light level) from the session config so the
                // codec writes the HDR SEI.  Session-level: HDR static metadata
                // is constant for the stream.  Best-effort — SoCs without the
                // controls ignore them.
                void programHdrMetadata() {
                        const MasteringDisplay md =
                                cfg.getAs<MasteringDisplay>(MediaConfig::HdrMasteringDisplay);
                        if (md.isValid()) {
                                struct v4l2_ctrl_hdr10_mastering_display m = v4l2MakeMasteringDisplay(md);
                                codec.setControlCompound(V4L2_CID_COLORIMETRY_HDR10_MASTERING_DISPLAY, &m,
                                                         sizeof(m));
                        }
                        const ContentLightLevel cll =
                                cfg.getAs<ContentLightLevel>(MediaConfig::HdrContentLightLevel);
                        if (cll.isValid()) {
                                struct v4l2_ctrl_hdr10_cll_info c = v4l2MakeCllInfo(cll);
                                codec.setControlCompound(V4L2_CID_COLORIMETRY_HDR10_CLL_INFO, &c, sizeof(c));
                        }
                }

                // Maps the codec-agnostic VideoProfile / VideoLevel config keys
                // onto the codec-specific V4L2 profile / level controls.
                // Best-effort: unsupported knobs are ignored (optional control).
                void programProfileLevel() {
                        const int levelIdc = H264ProfileLevel::levelIdc(cfg.getAs<String>(MediaConfig::VideoLevel));
                        if (codecId == VideoCodec::H264) {
                                H264Profile prof = H264ProfileLevel::profileFromWire(
                                        cfg.getAs<String>(MediaConfig::VideoProfile));
                                if (prof == H264Profile::Auto && rawFmt) {
                                        const int chromaIdc = (rawFmt->chromaVDiv == 2) ? 1 : 2; // 420→1, 422→2
                                        const int bitDepth = (rawFmt->bytesPerSample == 2) ? 10 : 8;
                                        prof = H264ProfileLevel::autoProfile(chromaIdc, bitDepth);
                                }
                                const int v = v4l2H264Profile(prof.value());
                                if (v >= 0) codec.setControl(V4L2_CID_MPEG_VIDEO_H264_PROFILE, v);
                                const int lv = v4l2H264Level(levelIdc);
                                if (lv >= 0) codec.setControl(V4L2_CID_MPEG_VIDEO_H264_LEVEL, lv);
                        } else if (codecId == VideoCodec::HEVC) {
                                const int v = v4l2HevcProfile(cfg.getAs<String>(MediaConfig::VideoProfile));
                                if (v >= 0) codec.setControl(V4L2_CID_MPEG_VIDEO_HEVC_PROFILE, v);
                                const int lv = v4l2HevcLevel(levelIdc);
                                if (lv >= 0) codec.setControl(V4L2_CID_MPEG_VIDEO_HEVC_LEVEL, lv);
                        }
                }

                // Splices any CEA-708 caption SEI carried on the source Frame
                // into the codec's coded access unit — the hardware encoder
                // can't inject user-data SEI itself.  Returns @p coded
                // unchanged when captions are disabled or none are present.
                Buffer injectCaptions(const Buffer &coded, const Frame &source) {
                        if (!captionsEnabled || !source.isValid()) return coded;
                        List<VideoEncoderSei::SeiPayload> seis =
                                VideoEncoderSei::captions(source, /*videoStreamIndex=*/0, ancTranslator);
                        List<Buffer> nals;
                        for (const VideoEncoderSei::SeiPayload &sp : seis) {
                                if (sp.bytes.size() == 0) continue;
                                nals.pushToBack(v4l2BuildSeiNal(sp.type, BufferView(sp.bytes, 0, sp.bytes.size()),
                                                                isHevc()));
                        }
                        if (nals.isEmpty()) return coded;
                        Buffer out;
                        if (v4l2InjectSeiNals(BufferView(coded, 0, coded.size()), nals, isHevc(), out).isError()) {
                                return coded;
                        }
                        return out;
                }

                bool isHevc() const { return mapping.fourcc == V4L2_PIX_FMT_HEVC; }

                // Non-blocking drain of every coded buffer currently ready.
                // Sets sawEos when the post-STOP LAST sentinel is observed.
                void pumpCapture(bool &sawEos) {
                        for (;;) {
                                Buffer  coded;
                                int64_t key = 0;
                                bool    keyframe = false;
                                bool    eos = false;
                                Error   err = codec.dequeueCapture(coded, key, keyframe, eos);
                                if (eos) sawEos = true;
                                if (err.isError()) break;
                                if (err == Error::NotReady) break;
                                if (coded.size() == 0) {
                                        if (eos) break;
                                        continue;
                                }
                                SourceInfo si;
                                if (inflight.contains(key)) {
                                        si = inflight.value(key);
                                        inflight.remove(key);
                                }
                                coded = injectCaptions(coded, si.source);
                                BufferView view(coded, 0, coded.size());
                                ImageDesc  cdesc(Size2Du32(width, height), PixelFormat(mapping.codedFormat));
                                auto       pkt = CompressedVideoPayload::Ptr::create(cdesc, view);
                                pkt.modify()->setPts(si.pts);
                                pkt.modify()->setDts(si.pts);
                                pkt.modify()->setFrameType(keyframe ? FrameType::IDR : FrameType::P);
                                pkt.modify()->setFlag(MediaPayload::Keyframe, keyframe);
                                if (!si.imageMeta.isEmpty()) pkt.modify()->metadata() = si.imageMeta;
                                if (sessionFrameRate.isValid()) {
                                        Rational<int> r(static_cast<int>(sessionFrameRate.numerator()),
                                                        static_cast<int>(sessionFrameRate.denominator()));
                                        pkt.modify()->setDuration(Duration::fromSamples(int64_t(1), r));
                                }
                                ready.pushToBack(VideoEncoder::buildOutputFrame(si.source, std::move(pkt)));
                                if (eos) break;
                        }
                }

                Error submit(const Frame &source, const UncompressedVideoPayload &payload,
                             const MediaTimeStamp &pts, bool forceKey) {
                        const ImageDesc &idesc = payload.desc();
                        if (Error err = ensureSession(idesc.pixelFormat(), idesc.size().width(),
                                                      idesc.size().height());
                            err.isError()) {
                                return err;
                        }

                        // Acquire a free OUTPUT buffer, draining coded output to
                        // free one if the queue is momentarily full.
                        int                              index = -1;
                        List<V4l2M2mCodec::OutPlane>     planes;
                        Error                            aerr = codec.acquireOutput(index, planes);
                        for (int spins = 0; aerr == Error::NotReady && spins < 64; ++spins) {
                                bool sawEos = false;
                                pumpCapture(sawEos);
                                bool ow = false, cr = false;
                                codec.poll(ow, cr, 100);
                                aerr = codec.acquireOutput(index, planes);
                        }
                        if (aerr.isError() || aerr == Error::NotReady) {
                                return setError(aerr.isError() ? aerr : Error::Timeout,
                                                "V4l2VideoEncoder: no free OUTPUT buffer");
                        }

                        if (forceKey) {
                                codec.setControl(V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 1);
                        }

                        List<size_t> bytesused;
                        v4l2PackSemiPlanar(payload, *rawFmt, planes, bytesused);

                        while (inflight.size() >= kMaxInflight) inflight.remove(inflight.begin());
                        inflight.insert(inputCounter, SourceInfo{source, pts, idesc.metadata()});
                        if (Error err = codec.submitOutput(index, bytesused, inputCounter); err.isError()) {
                                inflight.remove(inputCounter);
                                return setError(err, "V4l2VideoEncoder: submitOutput failed");
                        }
                        ++inputCounter;

                        bool sawEos = false;
                        pumpCapture(sawEos);
                        return Error::Ok;
                }

                Frame receiveFrame() {
                        if (ready.isEmpty()) {
                                bool sawEos = false;
                                pumpCapture(sawEos);
                        }
                        if (!ready.isEmpty()) return ready.popFromFront();
                        if (eosPending) {
                                eosPending = false;
                                ImageDesc cdesc(Size2Du32(0, 0), PixelFormat(mapping.codedFormat));
                                auto      pkt = CompressedVideoPayload::Ptr::create(cdesc);
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
                        // Issue STOP, then drain until the LAST sentinel (or the
                        // codec stops producing).
                        codec.sendStop();
                        bool sawEos = false;
                        for (int spins = 0; spins < 256 && !sawEos; ++spins) {
                                pumpCapture(sawEos);
                                if (sawEos) break;
                                bool ow = false, cr = false;
                                Error perr = codec.poll(ow, cr, 200);
                                if (perr == Error::Timeout) {
                                        // One more non-blocking sweep, then give up
                                        // waiting (some drivers never set LAST).
                                        pumpCapture(sawEos);
                                        break;
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
                        width = height = 0;
                        inputFmtId = PixelFormat::Invalid;
                        rawFmt = nullptr;
                        clearError();
                        return Error::Ok;
                }
};

// ---------------------------------------------------------------------------
// V4l2VideoEncoder thin shims that forward to Impl.
// ---------------------------------------------------------------------------

V4l2VideoEncoder::V4l2VideoEncoder(VideoCodec::ID codecId) : _impl(ImplPtr::create(codecId)) {}

V4l2VideoEncoder::~V4l2VideoEncoder() = default;

List<int> V4l2VideoEncoder::supportedInputList() {
        return v4l2SupportedRawPixelFormats();
}

void V4l2VideoEncoder::onConfigure(const MediaConfig &config) {
        _impl->cfg = config;
        const FrameRate fallback(FrameRate::RationalType(30, 1));
        FrameRate       fr = config.getAs<FrameRate>(MediaConfig::FrameRate, fallback);
        _impl->sessionFrameRate = fr.isValid() ? fr : fallback;
        _impl->captionsEnabled = config.getAs<bool>(MediaConfig::VideoSeiCaptionsEnabled, true);
}

Error V4l2VideoEncoder::submitFrame(const Frame &frame) {
        _impl->clearError();
        UncompressedVideoPayload::Ptr payload = selectInputPayload(frame);
        if (!payload.isValid() || !payload->isValid()) {
                promekiWarnThrottled(1000,
                                     "V4l2VideoEncoder::submitFrame: no uncompressed video payload on frame");
                _lastError = Error::Invalid;
                _lastErrorMessage = "V4l2VideoEncoder: no uncompressed video payload on frame";
                return _lastError;
        }
        Error err = _impl->submit(frame, *payload, payload->pts(), _requestKey);
        _requestKey = false;
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

Frame V4l2VideoEncoder::receiveFrame() {
        return _impl->receiveFrame();
}

Error V4l2VideoEncoder::flush() {
        _impl->clearError();
        Error err = _impl->flush();
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

Error V4l2VideoEncoder::reset() {
        _impl->clearError();
        Error err = _impl->reset();
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

void V4l2VideoEncoder::requestKeyframe() {
        _requestKey = true;
}

// ---------------------------------------------------------------------------
// Backend registration — probes for hardware at static-init time and only
// registers codecs a real mem2mem encoder node can service.  Registered
// under the "V4L2" backend name for H.264 and HEVC.  Weight sits above the
// x264 software encoder so a board with a hardware codec prefers it.
// ---------------------------------------------------------------------------

namespace {

        struct V4l2EncoderRegistrar {
                        V4l2EncoderRegistrar() {
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
                                        String dev = V4l2M2mCodec::findDevice(V4l2M2mCodec::Role::Encoder,
                                                                              V4L2_PIX_FMT_NV12, e.fourcc);
                                        if (dev.isEmpty()) continue;
                                        const VideoCodec::ID id = e.id;
                                        VideoEncoder::registerBackend({
                                                .codecId = id,
                                                .backend = backend,
                                                .weight = BackendWeight::Vendored + 20,
                                                .supportedInputs = V4l2VideoEncoder::supportedInputList(),
                                                .factory = [id]() -> VideoEncoder * {
                                                        return new V4l2VideoEncoder(id);
                                                },
                                        });
                                        promekiInfo("V4l2VideoEncoder: registered %s encoder on %s",
                                                    e.fourcc == V4L2_PIX_FMT_H264 ? "H264" : "HEVC", dev.cstr());
                                }
                        }
        };

        static V4l2EncoderRegistrar _v4l2EncoderRegistrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
