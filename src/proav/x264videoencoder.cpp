/**
 * @file      x264videoencoder.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/x264videoencoder.h>

#if PROMEKI_ENABLE_X264

#include <promeki/mediaconfig.h>
#include <promeki/frame.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/framerate.h>
#include <promeki/pixelaspect.h>
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
#include <promeki/enums_mediaio.h>
#include <promeki/enums_video.h>
#include <promeki/h264profilelevel.h>
#include <promeki/videoencodersei.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/anctranslator.h>

#include <cstring>
#include <cstdint>
#include <cstdlib>

#include <x264.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // -------------------------------------------------------------------
        // Input format classification.  Maps each accepted planar-YUV
        // PixelFormat to the x264 colour space + bit depth + chroma_format_idc
        // the session setup and profile auto-selection need.  Anything not
        // listed is rejected as PixelFormatNotSupported.  Mirrors
        // X264VideoEncoder::supportedInputList exactly.
        // -------------------------------------------------------------------

        struct X264Input {
                        int  csp = 0;        // X264_CSP_* (| X264_CSP_HIGH_DEPTH for 10-bit)
                        int  bitDepth = 0;   // 8 / 10
                        int  chromaIDC = 0;  // 1=4:2:0, 2=4:2:2, 3=4:4:4
                        bool valid = false;
        };

        X264Input classifyInput(PixelFormat::ID id) {
                switch (id) {
                        case PixelFormat::YUV8_420_Planar_Rec709: return {X264_CSP_I420, 8, 1, true};
                        case PixelFormat::YUV8_422_Planar_Rec709: return {X264_CSP_I422, 8, 2, true};
                        case PixelFormat::YUV8_444_Planar_Rec709: return {X264_CSP_I444, 8, 3, true};
                        case PixelFormat::YUV10_420_Planar_LE_Rec709:
                                return {X264_CSP_I420 | X264_CSP_HIGH_DEPTH, 10, 1, true};
                        case PixelFormat::YUV10_422_Planar_LE_Rec709:
                                return {X264_CSP_I422 | X264_CSP_HIGH_DEPTH, 10, 2, true};
                        case PixelFormat::YUV10_444_Planar_LE_Rec709:
                                return {X264_CSP_I444 | X264_CSP_HIGH_DEPTH, 10, 3, true};
                        default: return {};
                }
        }

        // Map the generic VideoEncoderPreset onto an x264 preset name.  The
        // speed/quality curve is named differently per backend; this is the
        // x264 vocabulary half of the mapping.
        const char *x264PresetName(const Enum &p) {
                if (p == VideoEncoderPreset::UltraLowLatency) return "ultrafast";
                if (p == VideoEncoderPreset::LowLatency) return "veryfast";
                if (p == VideoEncoderPreset::HighQuality) return "slow";
                if (p == VideoEncoderPreset::Lossless) return "veryslow";
                return "medium"; // Balanced / default
        }

        // Latency-oriented presets get the zerolatency tune (no lookahead /
        // no B-frame reorder delay); everything else uses x264's default
        // tuning.  Returning nullptr means "no tune".
        const char *x264TuneName(const Enum &p) {
                if (p == VideoEncoderPreset::UltraLowLatency || p == VideoEncoderPreset::LowLatency) {
                        return "zerolatency";
                }
                return nullptr;
        }

        FrameType x264TypeToFrameType(int t) {
                switch (t) {
                        case X264_TYPE_IDR: return FrameType::IDR;
                        case X264_TYPE_I: return FrameType::I;
                        case X264_TYPE_P: return FrameType::P;
                        case X264_TYPE_B:
                        case X264_TYPE_BREF: return FrameType::B;
                        default: return FrameType::Unknown;
                }
        }

} // namespace

// ---------------------------------------------------------------------------
// X264VideoEncoder::Impl — owns the x264_t and all x264.h types.
// ---------------------------------------------------------------------------

struct X264VideoEncoder::Impl {
                x264_t      *enc = nullptr;
                x264_param_t param{};
                bool         opened = false;

                uint32_t        width = 0;
                uint32_t        height = 0;
                PixelFormat::ID inputFmtId = PixelFormat::Invalid;
                X264Input       fmt;

                MediaConfig cfg;
                FrameRate   sessionFrameRate;
                String      paramSets; // Annex-B SPS+PPS blob.

                // Resolved raster scan mode for the session (config →
                // first-frame ImageDesc → Progressive).  Drives x264's
                // b_interlaced / b_tff and the per-frame pic_struct.
                VideoScanMode effectiveScanMode{VideoScanMode::Progressive};

                // SEI configuration, cached at session init.  HDR static
                // metadata falls back to these config-supplied values when a
                // frame carries none of its own (same precedence as NVENC).
                bool              seiCaptionsEnabled = true;
                MasteringDisplay  cfgMasteringDisplay;
                ContentLightLevel cfgContentLightLevel;
                // Free-standing translator used to convert source-Frame ANC
                // (CEA-708) onto the H.264 HlsSei carrier for caption SEI.  The
                // Cea708 → HlsSei builder is pure, so a default-constructed
                // config suffices and a shared instance avoids per-frame setup.
                AncTranslator ancTranslator;

                // Monotonic submission counter used as the x264 picture pts so
                // emitted pictures (which may reorder under B-frames) can be
                // matched back to their source Frame.
                int64_t inputCounter = 0;

                struct SourceInfo {
                                Frame          source;
                                MediaTimeStamp pts;
                                // Per-image metadata (Timecode, user keys, …)
                                // captured at submit so the emitted packet can
                                // echo it across the codec boundary — same as
                                // NVENC.  In-band SMPTE timecode SEI is not
                                // available (see attachSei / ensureSession), so
                                // timecode rides here as metadata instead.
                                Metadata imageMeta;
                };
                Map<int64_t, SourceInfo> inflight; // input-counter → source.

                Deque<Frame> ready;       // emitted output Frames awaiting receiveFrame.
                bool         eosPending = false;

                Error  lastError;
                String lastErrorMessage;

                ~Impl() { closeEncoder(); }

                void closeEncoder() {
                        if (enc) {
                                x264_encoder_close(enc);
                                enc = nullptr;
                        }
                        opened = false;
                }

                Error setError(Error err, const String &msg) {
                        lastError = err;
                        lastErrorMessage = msg;
                        promekiErr("X264VideoEncoder: %s", msg.cstr());
                        return err;
                }

                void clearError() {
                        lastError = Error::Ok;
                        lastErrorMessage = String();
                }

                // Reads a VUI colour-description config key as a raw H.273
                // numeric value, falling back to the VariantSpec default when
                // the key is absent (so missing keys land on Auto/Unknown, not
                // on the default-Enum's -1 sentinel).  Mirrors the NVENC path.
                uint32_t readColorEnum(MediaConfig::ID key) const {
                        const VariantSpec *s = MediaConfig::spec(key);
                        if (!cfg.contains(key)) {
                                Enum def = s ? s->defaultValue().get<Enum>() : Enum();
                                return static_cast<uint32_t>(def.value());
                        }
                        return static_cast<uint32_t>(cfg.getAs<Enum>(key).value());
                }

                Error ensureSession(const PixelFormat &pf, uint32_t w, uint32_t h, VideoScanMode firstFrameScanMode) {
                        if (opened) {
                                if (w != width || h != height) {
                                        return setError(Error::Invalid,
                                                        "X264VideoEncoder does not support mid-stream "
                                                        "resolution changes");
                                }
                                if (pf.id() != inputFmtId) {
                                        return setError(Error::Invalid,
                                                        "X264VideoEncoder does not support mid-stream "
                                                        "format changes");
                                }
                                return Error::Ok;
                        }

                        const X264Input in = classifyInput(pf.id());
                        if (!in.valid) {
                                return setError(Error::PixelFormatNotSupported,
                                                String::sprintf("X264VideoEncoder: unsupported input format %s",
                                                                pf.name().cstr()));
                        }
                        fmt = in;
                        inputFmtId = pf.id();
                        width = w;
                        height = h;

                        // --- Preset + tune ---
                        const Enum  presetEnum = cfg.getAs<Enum>(MediaConfig::VideoPreset);
                        const char *preset = x264PresetName(presetEnum);
                        const char *tune = x264TuneName(presetEnum);
                        if (x264_param_default_preset(&param, preset, tune) < 0) {
                                return setError(Error::LibraryFailure,
                                                String::sprintf("x264_param_default_preset(%s,%s) failed", preset,
                                                                tune ? tune : "none"));
                        }

                        // --- Geometry / bit depth ---
                        param.i_width = static_cast<int>(width);
                        param.i_height = static_cast<int>(height);
                        param.i_csp = fmt.csp;
                        param.i_bitdepth = fmt.bitDepth;
                        param.i_log_level = X264_LOG_WARNING;
                        // 0 = auto (x264 picks ~1.5x core count); MediaConfig::CodecThreads
                        // caps it to avoid oversubscription with parallel sessions.
                        const int32_t codecThreads = cfg.getAs<int32_t>(MediaConfig::CodecThreads, 0);
                        param.i_threads = codecThreads > 0 ? codecThreads : 0;

                        // --- Frame rate / timebase (1 tick = 1 frame) ---
                        const FrameRate fallback(FrameRate::RationalType(30, 1));
                        FrameRate       fr = cfg.getAs<FrameRate>(MediaConfig::FrameRate, fallback);
                        if (!fr.isValid()) fr = fallback;
                        sessionFrameRate = fr;
                        param.i_fps_num = static_cast<uint32_t>(fr.numerator());
                        param.i_fps_den = static_cast<uint32_t>(fr.denominator());
                        param.b_vfr_input = 0;
                        param.i_timebase_num = static_cast<uint32_t>(fr.denominator());
                        param.i_timebase_den = static_cast<uint32_t>(fr.numerator());

                        // --- GOP ---
                        const int32_t gopLength = cfg.getAs<int32_t>(MediaConfig::GopLength);
                        const int32_t idrInterval = cfg.getAs<int32_t>(MediaConfig::IdrInterval);
                        const int32_t keyint = (idrInterval > 0) ? idrInterval : gopLength;
                        if (keyint > 0) param.i_keyint_max = keyint;

                        // --- B-frames / lookahead ---
                        const int32_t bFrames = cfg.getAs<int32_t>(MediaConfig::BFrames);
                        if (bFrames >= 0) param.i_bframe = bFrames;
                        const int32_t laFrames = cfg.getAs<int32_t>(MediaConfig::LookaheadFrames);
                        if (laFrames > 0) param.rc.i_lookahead = laFrames;

                        // --- Output framing ---
                        param.b_annexb = 1;
                        param.b_repeat_headers = cfg.getAs<bool>(MediaConfig::VideoRepeatHeaders) ? 1 : 0;

                        // --- Scan mode / interlaced ---
                        // Resolve once now that the first frame has arrived:
                        // config VideoScanMode wins, else the input ImageDesc's
                        // scan mode, else Progressive.  Interlaced sessions use
                        // x264's native MBAFF (b_interlaced) and emit the
                        // pic_struct via x264's own pic_timing SEI (b_pic_struct);
                        // field order rides through b_tff + the per-frame
                        // pic_struct set in submit().
                        {
                                const VariantSpec *s = MediaConfig::spec(MediaConfig::VideoScanMode);
                                Enum               e = cfg.contains(MediaConfig::VideoScanMode)
                                                               ? cfg.getAs<Enum>(MediaConfig::VideoScanMode)
                                                               : (s ? s->defaultValue().get<Enum>() : Enum());
                                const VideoScanMode cfgScan(e.value());
                                if (cfgScan != VideoScanMode::Unknown) {
                                        effectiveScanMode = cfgScan;
                                } else if (firstFrameScanMode != VideoScanMode::Unknown) {
                                        effectiveScanMode = firstFrameScanMode;
                                } else {
                                        effectiveScanMode = VideoScanMode::Progressive;
                                }
                        }
                        if (effectiveScanMode.isInterlaced()) {
                                param.b_interlaced = 1;
                                // InterlacedOddFirst == bottom-field-first; every
                                // other interlaced mode follows the HD/SDI top-
                                // first norm.
                                param.b_tff = (effectiveScanMode == VideoScanMode::InterlacedOddFirst) ? 0 : 1;
                                // pic_struct_present_flag in the SPS VUI; x264
                                // then writes a pic_timing SEI per picture carrying
                                // the field order from the per-frame pic_struct.
                                param.b_pic_struct = 1;
                                // x264 does not implement weighted-P prediction in
                                // interlaced mode and would otherwise auto-disable
                                // it with a warning; turn it off up front.
                                param.analyse.i_weighted_pred = X264_WEIGHTP_NONE;
                        }

                        // --- SEI feature config ---
                        // NOTE: in-band SMPTE timecode (pic_timing clock_timestamp,
                        // SEI payloadType 1) is intentionally NOT injected.  x264
                        // owns the pic_timing SEI (x264_sei_pic_timing_write) and
                        // hardcodes clock_timestamp_flag = 0, so a hand-built
                        // clock_timestamp SEI would either duplicate pic_timing
                        // (malformed) or, without b_pic_struct, lack the required
                        // pic_struct_present_flag.  Timecode therefore rides as
                        // Metadata::Timecode on the emitted payload (see
                        // SourceInfo::imageMeta) — carried losslessly to the
                        // matching decoder — rather than in the bitstream.
                        seiCaptionsEnabled = cfg.getAs<bool>(MediaConfig::VideoSeiCaptionsEnabled, true);
                        cfgMasteringDisplay = cfg.getAs<MasteringDisplay>(MediaConfig::HdrMasteringDisplay);
                        cfgContentLightLevel = cfg.getAs<ContentLightLevel>(MediaConfig::HdrContentLightLevel);

                        // --- Rate control ---
                        const Enum    rcEnum = cfg.getAs<Enum>(MediaConfig::VideoRcMode);
                        const int32_t bitrate = cfg.getAs<int32_t>(MediaConfig::BitrateKbps);
                        const int32_t maxBitrate = cfg.getAs<int32_t>(MediaConfig::MaxBitrateKbps);
                        const int32_t qp = cfg.getAs<int32_t>(MediaConfig::VideoQp);
                        const bool    lossless =
                                (presetEnum == VideoEncoderPreset::Lossless) || (rcEnum == RateControlMode::CQP && qp == 0);
                        if (lossless) {
                                // libx264 lossless works at any chroma format
                                // (no 4:4:4 restriction, unlike NVENC).
                                param.rc.i_rc_method = X264_RC_CQP;
                                param.rc.i_qp_constant = 0;
                        } else if (rcEnum == RateControlMode::CQP) {
                                param.rc.i_rc_method = X264_RC_CQP;
                                param.rc.i_qp_constant = (qp > 0) ? qp : 23;
                        } else if (bitrate > 0) {
                                // CBR / VBR / ABR with an explicit bitrate target.
                                param.rc.i_rc_method = X264_RC_ABR;
                                param.rc.i_bitrate = bitrate;
                                if (rcEnum == RateControlMode::CBR) {
                                        // Constrained VBV pinned to the average ==
                                        // CBR behaviour.
                                        param.rc.i_vbv_max_bitrate = bitrate;
                                        param.rc.i_vbv_buffer_size = bitrate;
                                } else if (maxBitrate > 0) {
                                        param.rc.i_vbv_max_bitrate = maxBitrate;
                                        param.rc.i_vbv_buffer_size = maxBitrate;
                                }
                        }
                        // else: no explicit bitrate and not CQP — keep the
                        // preset's default CRF rate control (nominal QP 23).

                        // --- VUI colour signalling (H.273) ---
                        const uint32_t prim = readColorEnum(MediaConfig::VideoColorPrimaries);
                        const uint32_t trans = readColorEnum(MediaConfig::VideoTransferCharacteristics);
                        const uint32_t matrix = readColorEnum(MediaConfig::VideoMatrixCoefficients);
                        const uint32_t range = readColorEnum(MediaConfig::VideoRange);
                        const PixelFormat::H273ColorDescription col = pf.resolveH273(prim, trans, matrix, range);
                        const PixelFormat::VuiColorSignal       sig = PixelFormat::vuiColorSignal(col);
                        if (sig.signalTypePresent) {
                                param.vui.b_fullrange = sig.fullRange ? 1 : 0;
                                if (sig.colorDescriptionPresent) {
                                        param.vui.i_colorprim = static_cast<int>(sig.primaries);
                                        param.vui.i_transfer = static_cast<int>(sig.transfer);
                                        param.vui.i_colmatrix = static_cast<int>(sig.matrix);
                                }
                        }

                        // --- Sample aspect ratio (VUI / SAR) ---
                        // x264 reduces the ratio and maps it to the H.264
                        // Table E-1 aspect_ratio_idc (or Extended_SAR) itself;
                        // we just hand it the width:height.  Square (1:1) is
                        // the x264 default and needs no signalling.
                        const PixelAspect par = cfg.getAs<PixelAspect>(MediaConfig::VideoPixelAspect);
                        if (par.isValid() && !par.isSquare()) {
                                param.vui.i_sar_width = static_cast<int>(par.width());
                                param.vui.i_sar_height = static_cast<int>(par.height());
                        }

                        // --- Level ---
                        const int levelIdc = H264ProfileLevel::levelIdc(cfg.getAs<String>(MediaConfig::VideoLevel));
                        if (levelIdc > 0) param.i_level_idc = levelIdc;

                        // --- Profile (must be applied last) ---
                        H264Profile prof = H264ProfileLevel::profileFromWire(cfg.getAs<String>(MediaConfig::VideoProfile));
                        if (prof == H264Profile::Auto) {
                                prof = H264ProfileLevel::autoProfile(fmt.chromaIDC, fmt.bitDepth);
                        }
                        if (lossless) {
                                // H.264 transform-bypass lossless is only legal
                                // in High 4:4:4 Predictive (profile_idc 244) —
                                // x264 forces that profile internally for any
                                // chroma when qp==0, so request it explicitly to
                                // keep x264_param_apply_profile from rejecting a
                                // lower profile ("doesn't support lossless").
                                prof = H264Profile::High444;
                        }
                        const String wire = H264ProfileLevel::profileToWire(prof);
                        if (!wire.isEmpty()) {
                                if (x264_param_apply_profile(&param, wire.cstr()) < 0) {
                                        promekiWarn("X264VideoEncoder: profile '%s' is incompatible with the input "
                                                    "format (%s); letting x264 auto-select.",
                                                    wire.cstr(), pf.name().cstr());
                                }
                        }

                        enc = x264_encoder_open(&param);
                        if (!enc) {
                                return setError(Error::LibraryFailure, "x264_encoder_open failed");
                        }
                        opened = true;
                        captureHeaders();
                        clearError();
                        return Error::Ok;
                }

                // Pull the Annex-B SPS+PPS out of the freshly-opened encoder and
                // stash them so every emitted packet can republish them via
                // Metadata::CodecParameterSets (out-of-band parameter sets for
                // MP4 avcC / RTP sprop / … without waiting for the first IDR).
                // Best-effort: a failure just leaves the blob empty and
                // downstream falls back to in-band parameter sets.
                void captureHeaders() {
                        paramSets = String();
                        x264_nal_t *nal = nullptr;
                        int         numNal = 0;
                        if (x264_encoder_headers(enc, &nal, &numNal) < 0 || numNal <= 0 || !nal) {
                                promekiWarn("X264VideoEncoder: x264_encoder_headers failed; "
                                            "Metadata::CodecParameterSets will be empty.");
                                return;
                        }
                        size_t total = 0;
                        for (int i = 0; i < numNal; ++i) {
                                if (nal[i].i_type == NAL_SPS || nal[i].i_type == NAL_PPS) {
                                        total += static_cast<size_t>(nal[i].i_payload);
                                }
                        }
                        if (total == 0) return;
                        List<char> blob;
                        blob.reserve(total);
                        for (int i = 0; i < numNal; ++i) {
                                if (nal[i].i_type != NAL_SPS && nal[i].i_type != NAL_PPS) continue;
                                const char *p = reinterpret_cast<const char *>(nal[i].p_payload);
                                blob.pushToBack(p, p + nal[i].i_payload);
                        }
                        paramSets = String(blob.data(), blob.size());
                }

                // Concatenate one encode call's NAL units into an owned Buffer
                // and wrap it as a CompressedVideoPayload, stamping pts/dts,
                // frame type, keyframe flag, duration, and out-of-band
                // parameter sets.
                CompressedVideoPayload::Ptr buildPacket(x264_nal_t *nal, int numNal, const x264_picture_t &picOut,
                                                        const MediaTimeStamp &pts, const Metadata &imageMeta) {
                        size_t total = 0;
                        for (int i = 0; i < numNal; ++i) total += static_cast<size_t>(nal[i].i_payload);
                        if (total == 0) return CompressedVideoPayload::Ptr();

                        Buffer buf(total);
                        size_t off = 0;
                        for (int i = 0; i < numNal; ++i) {
                                std::memcpy(static_cast<uint8_t *>(buf.data()) + off, nal[i].p_payload,
                                            nal[i].i_payload);
                                off += static_cast<size_t>(nal[i].i_payload);
                        }
                        buf.setSize(total);

                        BufferView view(buf, 0, total);
                        ImageDesc  cdesc(Size2Du32(width, height), PixelFormat(PixelFormat::H264));
                        auto       pkt = CompressedVideoPayload::Ptr::create(cdesc, view);
                        pkt.modify()->setPts(pts);
                        pkt.modify()->setDts(pts);
                        pkt.modify()->setFrameType(x264TypeToFrameType(picOut.i_type));
                        pkt.modify()->setFlag(MediaPayload::Keyframe, picOut.b_keyframe != 0);

                        // Carry per-image metadata (Timecode, user keys, …)
                        // across the codec boundary, then stamp the codec's own
                        // keys on top.
                        Metadata &pmeta = pkt.modify()->metadata();
                        if (!imageMeta.isEmpty()) pmeta = imageMeta;
                        if (sessionFrameRate.isValid()) {
                                Rational<int> r(static_cast<int>(sessionFrameRate.numerator()),
                                                static_cast<int>(sessionFrameRate.denominator()));
                                pkt.modify()->setDuration(Duration::fromSamples(int64_t(1), r));
                        }
                        if (!paramSets.isEmpty()) {
                                pmeta.set(Metadata::CodecParameterSets, paramSets);
                        }
                        return pkt;
                }

                // Drain one emitted x264 picture (display order pts == the
                // input counter we stamped) back to its source Frame, build the
                // output Frame, and enqueue it.
                void emit(x264_nal_t *nal, int numNal, const x264_picture_t &picOut) {
                        SourceInfo si;
                        const int64_t key = picOut.i_pts;
                        if (inflight.contains(key)) {
                                si = inflight.value(key);
                                inflight.remove(key);
                        }
                        auto pkt = buildPacket(nal, numNal, picOut, si.pts, si.imageMeta);
                        if (!pkt.isValid()) return;
                        ready.pushToBack(VideoEncoder::buildOutputFrame(si.source, std::move(pkt)));
                }

                // Assemble caption (4) + HDR mastering (137) + CLL (144) SEI
                // for this frame and hand them to libx264 via extra_sei.
                //
                // libx264 takes ownership of the payload array and each payload
                // buffer and reclaims them through extra_sei.sei_free after the
                // frame is encoded (which, with lookahead / B-frames, may be
                // after x264_encoder_encode returns) — so everything here is
                // malloc'd and never touched again on the success path.
                //
                // HDR static metadata is attached to every frame (constant, but
                // a decoder seeking to any mid-stream IDR must still see it);
                // keyframe-only emission is a future optimisation.
                void attachSei(x264_picture_t &picIn, const Frame &source, const ImageDesc &idesc) {
                        List<VideoEncoderSei::SeiPayload> seis;
                        if (seiCaptionsEnabled) {
                                // Stream index 0: this backend is single-stream,
                                // so any ANC paired to video stream 0 or unbound
                                // (-1) is in scope.
                                for (auto &p : VideoEncoderSei::captions(source, /*videoStreamIndex=*/0, ancTranslator)) {
                                        if (p.bytes.size() > 0) seis.pushToBack(std::move(p));
                                }
                        }
                        const MasteringDisplay md =
                                idesc.metadata().getAs<MasteringDisplay>(Metadata::MasteringDisplay, cfgMasteringDisplay);
                        if (md.isValid()) {
                                VideoEncoderSei::SeiPayload sp = VideoEncoderSei::masteringDisplay(md);
                                if (sp.bytes.size() > 0) seis.pushToBack(std::move(sp));
                        }
                        const ContentLightLevel cll = idesc.metadata().getAs<ContentLightLevel>(
                                Metadata::ContentLightLevel, cfgContentLightLevel);
                        if (cll.isValid()) {
                                VideoEncoderSei::SeiPayload sp = VideoEncoderSei::contentLightLevel(cll);
                                if (sp.bytes.size() > 0) seis.pushToBack(std::move(sp));
                        }
                        if (seis.isEmpty()) return;

                        auto *arr = static_cast<x264_sei_payload_t *>(
                                std::malloc(seis.size() * sizeof(x264_sei_payload_t)));
                        if (!arr) return;
                        int n = 0;
                        for (const VideoEncoderSei::SeiPayload &s : seis) {
                                const size_t sz = s.bytes.size();
                                auto        *buf = static_cast<uint8_t *>(std::malloc(sz));
                                if (!buf) continue;
                                std::memcpy(buf, s.bytes.data(), sz);
                                arr[n].payload_size = static_cast<int>(sz);
                                arr[n].payload_type = s.type;
                                arr[n].payload = buf;
                                ++n;
                        }
                        if (n == 0) {
                                std::free(arr);
                                return;
                        }
                        picIn.extra_sei.num_payloads = n;
                        picIn.extra_sei.payloads = arr;
                        picIn.extra_sei.sei_free = std::free;
                }

                Error submit(const Frame &source, const UncompressedVideoPayload &payload, const MediaTimeStamp &pts,
                             bool forceKey) {
                        const ImageDesc &idesc = payload.desc();
                        if (Error err = ensureSession(idesc.pixelFormat(), idesc.size().width(), idesc.size().height(),
                                                      idesc.videoScanMode());
                            err.isError()) {
                                return err;
                        }

                        x264_picture_t picIn;
                        x264_picture_init(&picIn);
                        picIn.i_type = forceKey ? X264_TYPE_IDR : X264_TYPE_AUTO;
                        picIn.i_pts = inputCounter;
                        picIn.img.i_csp = fmt.csp;
                        picIn.img.i_plane = static_cast<int>(payload.planeCount());

                        // Per-frame field order for interlaced sessions.  A
                        // per-frame Metadata::VideoScanMode override (e.g. mixed
                        // cadence) takes precedence over the session default.
                        if (effectiveScanMode.isInterlaced()) {
                                VideoScanMode frameScan = effectiveScanMode;
                                if (idesc.metadata().contains(Metadata::VideoScanMode)) {
                                        const VideoScanMode m(
                                                idesc.metadata().getAs<Enum>(Metadata::VideoScanMode).value());
                                        if (m.isInterlaced()) frameScan = m;
                                }
                                picIn.i_pic_struct = (frameScan == VideoScanMode::InterlacedOddFirst)
                                                             ? PIC_STRUCT_BOTTOM_TOP
                                                             : PIC_STRUCT_TOP_BOTTOM;
                        }

                        const PixelMemLayout &ml = idesc.pixelFormat().memLayout();
                        const size_t          imgWidth = idesc.size().width();
                        const int             planes = picIn.img.i_plane;
                        for (int c = 0; c < planes && c < 4; ++c) {
                                picIn.img.i_stride[c] = static_cast<int>(ml.lineStride(c, imgWidth));
                                picIn.img.plane[c] = const_cast<uint8_t *>(payload.plane(c).data());
                        }

                        attachSei(picIn, source, idesc);

                        inflight.insert(inputCounter, SourceInfo{source, pts, idesc.metadata()});

                        x264_picture_t picOut;
                        x264_nal_t    *nal = nullptr;
                        int            numNal = 0;
                        const int      frameSize = x264_encoder_encode(enc, &nal, &numNal, &picIn, &picOut);
                        ++inputCounter;
                        if (frameSize < 0) {
                                inflight.remove(picIn.i_pts);
                                return setError(Error::LibraryFailure, "x264_encoder_encode failed");
                        }
                        if (numNal > 0) emit(nal, numNal, picOut);
                        return Error::Ok;
                }

                Error flush() {
                        if (!opened) {
                                // Nothing to drain — still report EOS so the
                                // caller's drain loop terminates.
                                eosPending = true;
                                return Error::Ok;
                        }
                        while (x264_encoder_delayed_frames(enc) > 0) {
                                x264_picture_t picOut;
                                x264_nal_t    *nal = nullptr;
                                int            numNal = 0;
                                const int frameSize = x264_encoder_encode(enc, &nal, &numNal, nullptr, &picOut);
                                if (frameSize < 0) {
                                        return setError(Error::LibraryFailure, "x264_encoder_encode (drain) failed");
                                }
                                if (numNal > 0) emit(nal, numNal, picOut);
                        }
                        eosPending = true;
                        return Error::Ok;
                }

                Frame receiveFrame() {
                        if (!ready.isEmpty()) return ready.popFromFront();
                        if (eosPending) {
                                eosPending = false;
                                ImageDesc cdesc(Size2Du32(0, 0), PixelFormat(PixelFormat::H264));
                                auto      pkt = CompressedVideoPayload::Ptr::create(cdesc);
                                pkt.modify()->markEndOfStream();
                                Frame out;
                                out.addPayload(pkt);
                                return out;
                        }
                        return Frame();
                }

                Error reset() {
                        closeEncoder();
                        inflight.clear();
                        ready.clear();
                        eosPending = false;
                        inputCounter = 0;
                        paramSets = String();
                        width = height = 0;
                        inputFmtId = PixelFormat::Invalid;
                        effectiveScanMode = VideoScanMode::Progressive;
                        clearError();
                        return Error::Ok;
                }
};

// ---------------------------------------------------------------------------
// X264VideoEncoder thin shims that forward to Impl.
// ---------------------------------------------------------------------------

X264VideoEncoder::X264VideoEncoder() : _impl(ImplPtr::create()) {}

X264VideoEncoder::~X264VideoEncoder() = default;

List<int> X264VideoEncoder::supportedInputList() {
        // Must mirror classifyInput exactly.  Planar YUV only: the encoder
        // feeds libx264 the planes zero-copy, so interleaved / semi-planar
        // layouts are intentionally excluded (the planner inserts a CSC).
        return {
                static_cast<int>(PixelFormat::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV8_422_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV8_444_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV10_420_Planar_LE_Rec709),
                static_cast<int>(PixelFormat::YUV10_422_Planar_LE_Rec709),
                static_cast<int>(PixelFormat::YUV10_444_Planar_LE_Rec709),
        };
}

void X264VideoEncoder::onConfigure(const MediaConfig &config) {
        _impl->cfg = config;
}

Error X264VideoEncoder::submitFrame(const Frame &frame) {
        _impl->clearError();
        UncompressedVideoPayload::Ptr payload = selectInputPayload(frame);
        if (!payload.isValid() || !payload->isValid()) {
                promekiWarnThrottled(1000,
                                     "X264VideoEncoder::submitFrame: no uncompressed video payload on frame");
                _lastError = Error::Invalid;
                _lastErrorMessage = "X264VideoEncoder: no uncompressed video payload on frame";
                return _lastError;
        }
        Error err = _impl->submit(frame, *payload, payload->pts(), _requestKey);
        _requestKey = false;
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

Frame X264VideoEncoder::receiveFrame() {
        return _impl->receiveFrame();
}

Error X264VideoEncoder::flush() {
        _impl->clearError();
        Error err = _impl->flush();
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

Error X264VideoEncoder::reset() {
        _impl->clearError();
        Error err = _impl->reset();
        _lastError = _impl->lastError;
        _lastErrorMessage = _impl->lastErrorMessage;
        return err;
}

void X264VideoEncoder::requestKeyframe() {
        _requestKey = true;
}

// ---------------------------------------------------------------------------
// Backend registration — typed (codec, backend) pair on the VideoEncoder
// registry.  Registered under the "x264" backend name for VideoCodec::H264.
// No decoder: libx264 is encode-only.
//
// Weight: BackendWeight::Vendored is the lowest standard band, so any
// hardware H.264 backend (NVENC, registered at the same Vendored tier)
// must outrank it.  x264 registers one band below Vendored so a GPU box
// transparently prefers hardware and headless / ARM boxes fall back to
// this software encoder.  Explicit pinning (H264:x264 / H264:Nvidia) or
// MediaConfig::CodecBackend always overrides the weight.
// ---------------------------------------------------------------------------

namespace {

        struct X264Registrar {
                        X264Registrar() {
                                auto bk = VideoCodec::registerBackend("x264");
                                if (error(bk).isError()) return;
                                const VideoCodec::Backend backend = value(bk);

                                VideoEncoder::registerBackend({
                                        .codecId = VideoCodec::H264,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored - 10,
                                        .supportedInputs = X264VideoEncoder::supportedInputList(),
                                        .factory = []() -> VideoEncoder * { return new X264VideoEncoder(); },
                                });
                        }
        };

        static X264Registrar _x264Registrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_X264
