/**
 * @file      nvencvideoencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * First-cut NVENC backend for H.264 / HEVC.  Scope for this
 * iteration is deliberately narrow so the foundation is obvious:
 *
 *   - NV12 (YUV8_420_SemiPlanar_Rec709) input only.
 *   - B-frames and look-ahead off by default (and not exposed).
 *   - Synchronous mode — @c nvEncEncodePicture returns output on the
 *     same slot it consumed.  No cross-slot reordering to worry about.
 *   - System-memory staging through @c nvEncCreateInputBuffer; NVENC
 *     performs the internal H2D copy.  A zero-copy path that takes a
 *     CUDA device pointer is a follow-up.
 *
 * All wider knobs (B-frames, lookahead, registered resources, 10-bit
 * P010, RGB input formats, HDR metadata) are intentional follow-ups.
 */

#include <promeki/nvencvideoencoder.h>

#if PROMEKI_ENABLE_NVENC

#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
#include <promeki/cuda.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/enums.h>
#include <promeki/metadata.h>
#include <promeki/videocodec.h>

#include <deque>
#include <mutex>
#include <vector>
#include <cstring>
#include <cstdint>
#include <dlfcn.h>

#include <cuda.h>          // Driver API — CUcontext / cuInit / cuDevicePrimaryCtxRetain.
#include <nvEncodeAPI.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Dynamic loader for libnvidia-encode.so.1.  The NVENC SDK ships only
// headers; the actual entry point lives in the driver user-mode library
// so we dlopen it once per process and cache the populated function
// list.  loadNvenc() is cheap on the hot path thanks to the
// once-style guard.
// ---------------------------------------------------------------------------

namespace {

NV_ENCODE_API_FUNCTION_LIST gNvenc{};
bool                        gNvencLoaded = false;
std::mutex                  gNvencMutex;

bool loadNvencLocked() {
        if(gNvencLoaded) return true;

        void *lib = dlopen("libnvidia-encode.so.1", RTLD_NOW | RTLD_LOCAL);
        if(!lib) {
                promekiErr("NVENC: dlopen(libnvidia-encode.so.1) failed: %s", dlerror());
                return false;
        }

        using CreateFn = NVENCSTATUS (NVENCAPI *)(NV_ENCODE_API_FUNCTION_LIST *);
        auto createFn = reinterpret_cast<CreateFn>(
                dlsym(lib, "NvEncodeAPICreateInstance"));
        if(!createFn) {
                promekiErr("NVENC: dlsym(NvEncodeAPICreateInstance) failed: %s", dlerror());
                dlclose(lib);
                return false;
        }

        NV_ENCODE_API_FUNCTION_LIST fl{};
        fl.version = NV_ENCODE_API_FUNCTION_LIST_VER;
        NVENCSTATUS st = createFn(&fl);
        if(st != NV_ENC_SUCCESS) {
                promekiErr("NVENC: NvEncodeAPICreateInstance failed (status %d)", (int)st);
                return false;
        }

        gNvenc = fl;
        gNvencLoaded = true;
        return true;
}

bool loadNvenc() {
        std::lock_guard<std::mutex> lock(gNvencMutex);
        return loadNvencLocked();
}

// ---------------------------------------------------------------------------
// MediaConfig → NVENC parameter translation.
// ---------------------------------------------------------------------------

NV_ENC_PARAMS_RC_MODE toNvencRc(const Enum &rc) {
        if(rc == VideoRateControl::CBR) return NV_ENC_PARAMS_RC_CBR;
        if(rc == VideoRateControl::CQP) return NV_ENC_PARAMS_RC_CONSTQP;
        return NV_ENC_PARAMS_RC_VBR;
}

// NVENC's modern preset range is P1..P7 (ultra-fast..ultra-slow).
// Map the library-level VideoEncoderPreset to a sensible midpoint for
// each band so callers get a predictable speed/quality point without
// having to learn NVENC's enum.
GUID toNvencPreset(const Enum &p) {
        if(p == VideoEncoderPreset::UltraLowLatency) return NV_ENC_PRESET_P1_GUID;
        if(p == VideoEncoderPreset::LowLatency)      return NV_ENC_PRESET_P3_GUID;
        if(p == VideoEncoderPreset::HighQuality)     return NV_ENC_PRESET_P6_GUID;
        return NV_ENC_PRESET_P4_GUID;  // Balanced
}

// The tuning-info axis is orthogonal to the preset in NVENC 12+:
// presets pick speed, tuning picks latency vs quality behaviour.
NV_ENC_TUNING_INFO toNvencTuning(const Enum &p) {
        if(p == VideoEncoderPreset::UltraLowLatency) return NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
        if(p == VideoEncoderPreset::LowLatency)      return NV_ENC_TUNING_INFO_LOW_LATENCY;
        if(p == VideoEncoderPreset::HighQuality)     return NV_ENC_TUNING_INFO_HIGH_QUALITY;
        return NV_ENC_TUNING_INFO_LOW_LATENCY;
}

} // namespace

// ---------------------------------------------------------------------------
// NvencVideoEncoder::Impl — everything that needs NVENC + CUDA types.
// ---------------------------------------------------------------------------

class NvencVideoEncoder::Impl {
        public:
                explicit Impl(Codec codec) : _codec(codec) {}

                ~Impl() { destroySession(); }

                // Copies caller-visible configuration.  Actual encoder
                // creation happens lazily in ensureSession() once we
                // know the input dimensions from the first frame.
                void configure(const MediaConfig &cfg) {
                        _cfg = cfg;
                        // If a session already exists, most knobs can only be
                        // reapplied on the next IDR — defer to next frame.
                        _needReconfigure = _sessionOpen;
                }

                Error submitFrame(const Image &frame, const MediaTimeStamp &pts,
                                  bool forceKey) {
                        if(!frame.isValid() || frame.planes().isEmpty()) {
                                return setError(Error::Invalid, "invalid frame");
                        }
                        if(frame.pixelDesc().id() != PixelDesc::YUV8_420_SemiPlanar_Rec709) {
                                return setError(Error::PixelFormatNotSupported,
                                        "NvencVideoEncoder v1 only accepts YUV8_420_SemiPlanar_Rec709 (NV12)");
                        }
                        if(Error err = ensureSession(frame.width(), frame.height()); err.isError()) {
                                return err;
                        }

                        Slot *slot = acquireFreeSlot();
                        if(!slot) return setError(Error::TryAgain, "no free NVENC slot");

                        // Copy NV12 payload into the locked NVENC input
                        // buffer.  uploadNV12 also records the locked
                        // pitch onto @p slot so we pass the correct
                        // value to nvEncEncodePicture below.
                        if(Error err = uploadNV12(frame, slot); err.isError()) {
                                _freeSlots.push_back(slot);
                                return err;
                        }
                        slot->imageMeta = frame.metadata();

                        NV_ENC_PIC_PARAMS pic{};
                        pic.version        = NV_ENC_PIC_PARAMS_VER;
                        pic.inputBuffer    = slot->in;
                        pic.outputBitstream = slot->out;
                        pic.bufferFmt      = NV_ENC_BUFFER_FORMAT_NV12;
                        pic.inputWidth     = _width;
                        pic.inputHeight    = _height;
                        pic.inputPitch     = slot->pitch;
                        pic.pictureStruct  = NV_ENC_PIC_STRUCT_FRAME;
                        pic.frameIdx       = _frameIdx;
                        pic.inputTimeStamp = _frameIdx;
                        pic.encodePicFlags = forceKey ? NV_ENC_PIC_FLAG_FORCEIDR : 0;

                        NVENCSTATUS st = gNvenc.nvEncEncodePicture(_encoder, &pic);
                        if(st != NV_ENC_SUCCESS && st != NV_ENC_ERR_NEED_MORE_INPUT) {
                                _freeSlots.push_back(slot);
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncEncodePicture failed (%d)", (int)st));
                        }

                        slot->pts = pts;
                        slot->hasOutput = (st == NV_ENC_SUCCESS);
                        _inFlight.push_back(slot);
                        ++_frameIdx;
                        return Error::Ok;
                }

                MediaPacket::Ptr receivePacket() {
                        // If we previously emitted an EOS packet for a
                        // completed flush, keep reporting end-of-stream
                        // until the caller issues reset() / new config.
                        if(_eosPending) {
                                _eosPending = false;
                                auto pkt = MediaPacket::Ptr::create();
                                pkt.modify()->setPixelDesc(outputPixelDesc());
                                pkt.modify()->addFlag(MediaPacket::EndOfStream);
                                return pkt;
                        }

                        if(_inFlight.empty()) return MediaPacket::Ptr();

                        // In sync mode with no B-frames / no look-ahead
                        // the slot at the head always carries output once
                        // encodePicture returned OK on it.  Slots marked
                        // !hasOutput correspond to NEED_MORE_INPUT frames
                        // the encoder hasn't flushed yet — we leave them
                        // in the queue and try again later (or on flush).
                        if(!_inFlight.front()->hasOutput) return MediaPacket::Ptr();

                        Slot *slot = _inFlight.front();
                        _inFlight.pop_front();

                        auto pkt = lockAndBuildPacket(slot);
                        _freeSlots.push_back(slot);
                        return pkt;
                }

                Error flush() {
                        if(!_sessionOpen) {
                                // Nothing to drain — still report EOS so
                                // the caller's drain loop terminates.
                                _eosPending = true;
                                return Error::Ok;
                        }

                        // Submit an EOS pseudo-frame; NVENC will emit any
                        // buffered output on the subsequent lockBitstream
                        // calls against slots still in _inFlight.
                        NV_ENC_PIC_PARAMS pic{};
                        pic.version        = NV_ENC_PIC_PARAMS_VER;
                        pic.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
                        pic.pictureStruct  = NV_ENC_PIC_STRUCT_FRAME;
                        NVENCSTATUS st = gNvenc.nvEncEncodePicture(_encoder, &pic);
                        if(st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncEncodePicture(EOS) failed (%d)", (int)st));
                        }

                        // Any previously-NEED_MORE_INPUT slots are now
                        // guaranteed to have bitstream data — mark them.
                        for(Slot *s : _inFlight) s->hasOutput = true;
                        _eosPending = true;
                        return Error::Ok;
                }

                Error reset() {
                        destroySession();
                        return Error::Ok;
                }

                PixelDesc outputPixelDesc() const {
                        return PixelDesc(_codec == Codec_H264
                                ? PixelDesc::H264
                                : PixelDesc::HEVC);
                }

                Error lastError() const { return _lastError; }
                const String &lastErrorMessage() const { return _lastErrorMessage; }
                void clearError() {
                        _lastError = Error::Ok;
                        _lastErrorMessage = String();
                }

        private:
                static constexpr size_t kNumSlots = 4;

                struct Slot {
                        NV_ENC_INPUT_PTR   in         = nullptr;
                        NV_ENC_OUTPUT_PTR  out        = nullptr;
                        MediaTimeStamp     pts;
                        // Metadata attached to the source Image when it
                        // was submitted.  Copied verbatim onto the
                        // emitted MediaPacket so per-image state that
                        // can't live in the codec bitstream (Timecode,
                        // MediaTimeStamp, user keys) survives the
                        // encode/decode round trip.
                        Metadata           imageMeta;
                        // Pitch returned by nvEncLockInputBuffer for
                        // this slot's input surface.  NVENC allocates
                        // the pitched buffer; we have to pass the same
                        // pitch back to nvEncEncodePicture so rows
                        // aren't mis-interpreted when the allocated
                        // pitch is not equal to the image width.
                        uint32_t           pitch      = 0;
                        bool               hasOutput  = false;
                };

                Codec          _codec;
                MediaConfig    _cfg;
                bool           _needReconfigure = false;

                // CUDA primary context; owned (retained) by this instance.
                CUdevice       _device = 0;
                CUcontext      _cudaCtx = nullptr;
                bool           _ctxRetained = false;

                // NVENC session state.
                void          *_encoder   = nullptr;
                bool           _sessionOpen = false;
                uint32_t       _width    = 0;
                uint32_t       _height   = 0;
                uint64_t       _frameIdx = 0;

                std::vector<Slot> _slots;
                std::deque<Slot*> _freeSlots;
                std::deque<Slot*> _inFlight;

                bool           _eosPending = false;

                Error          _lastError;
                String         _lastErrorMessage;

                Error setError(Error err, const String &msg) {
                        _lastError = err;
                        _lastErrorMessage = msg;
                        promekiErr("NvencVideoEncoder: %s", msg.cstr());
                        return err;
                }

                GUID codecGuid() const {
                        return _codec == Codec_H264
                                ? NV_ENC_CODEC_H264_GUID
                                : NV_ENC_CODEC_HEVC_GUID;
                }

                Error ensureSession(uint32_t w, uint32_t h) {
                        if(_sessionOpen) {
                                if(w != _width || h != _height) {
                                        return setError(Error::Invalid,
                                                "NvencVideoEncoder v1 does not support mid-stream resolution changes");
                                }
                                if(_needReconfigure) {
                                        // Minimal dynamic-reconfig support: apply the
                                        // new bitrate / RC mode at the next keyframe.
                                        // Anything more invasive is a destroy-and-recreate
                                        // path we do not exercise yet.
                                        _needReconfigure = false;
                                }
                                return Error::Ok;
                        }

                        if(!loadNvenc()) {
                                return setError(Error::LibraryFailure,
                                        "failed to load libnvidia-encode.so.1 (install libnvidia-encode-NNN matching your driver)");
                        }

                        if(Error err = retainCudaContext(); err.isError()) return err;

                        // Open encoder session bound to the CUDA primary context.
                        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS sp{};
                        sp.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
                        sp.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
                        sp.device     = _cudaCtx;
                        sp.apiVersion = NVENCAPI_VERSION;
                        NVENCSTATUS st = gNvenc.nvEncOpenEncodeSessionEx(&sp, &_encoder);
                        if(st != NV_ENC_SUCCESS || _encoder == nullptr) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncOpenEncodeSessionEx failed (%d)", (int)st));
                        }

                        _width  = w;
                        _height = h;

                        // Start from the preset config and patch in the
                        // library-level knobs that MediaConfig captures.
                        const GUID encGuid    = codecGuid();
                        const Enum presetEnum = _cfg.getAs<Enum>(MediaConfig::VideoPreset);
                        const GUID presetGuid = toNvencPreset(presetEnum);
                        const NV_ENC_TUNING_INFO tuning = toNvencTuning(presetEnum);

                        NV_ENC_PRESET_CONFIG presetCfg{};
                        presetCfg.version = NV_ENC_PRESET_CONFIG_VER;
                        presetCfg.presetCfg.version = NV_ENC_CONFIG_VER;
                        st = gNvenc.nvEncGetEncodePresetConfigEx(_encoder, encGuid, presetGuid,
                                                                 tuning, &presetCfg);
                        if(st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncGetEncodePresetConfigEx failed (%d)", (int)st));
                        }

                        NV_ENC_CONFIG encCfg = presetCfg.presetCfg;
                        encCfg.version  = NV_ENC_CONFIG_VER;

                        const int32_t bitrateKbps    = _cfg.getAs<int32_t>(MediaConfig::BitrateKbps);
                        const int32_t maxBitrateKbps = _cfg.getAs<int32_t>(MediaConfig::MaxBitrateKbps);
                        const int32_t gopLength      = _cfg.getAs<int32_t>(MediaConfig::GopLength);
                        const int32_t idrInterval    = _cfg.getAs<int32_t>(MediaConfig::IdrInterval);
                        const int32_t qp             = _cfg.getAs<int32_t>(MediaConfig::VideoQp);
                        const Enum rcEnum = _cfg.getAs<Enum>(MediaConfig::VideoRcMode);

                        encCfg.rcParams.rateControlMode = toNvencRc(rcEnum);
                        encCfg.rcParams.averageBitRate  = static_cast<uint32_t>(bitrateKbps) * 1000u;
                        encCfg.rcParams.maxBitRate      = static_cast<uint32_t>(maxBitrateKbps) * 1000u;
                        if(encCfg.rcParams.rateControlMode == NV_ENC_PARAMS_RC_CONSTQP) {
                                encCfg.rcParams.constQP.qpIntra  = qp;
                                encCfg.rcParams.constQP.qpInterP = qp;
                                encCfg.rcParams.constQP.qpInterB = qp;
                        }
                        if(gopLength > 0) encCfg.gopLength = gopLength;
                        // v1: no B-frames.  frameIntervalP = 1 means I/P only.
                        encCfg.frameIntervalP = 1;

                        const int effectiveIdr = (idrInterval > 0) ? idrInterval : encCfg.gopLength;
                        if(_codec == Codec_H264) {
                                encCfg.encodeCodecConfig.h264Config.idrPeriod = effectiveIdr;
                        } else {
                                encCfg.encodeCodecConfig.hevcConfig.idrPeriod = effectiveIdr;
                        }

                        // Honour the caller-supplied FrameRate — NVENC
                        // uses this for rate-control math (CBR / VBR
                        // bits-per-frame target, HRD buffer math) and
                        // for SPS/VUI timing info in H.264 / HEVC.  The
                        // library hook is MediaConfig::FrameRate; the
                        // MediaIOTask_VideoEncoder stamps it from the
                        // pending MediaDesc before calling configure().
                        const FrameRate fallback(FrameRate::RationalType(30, 1));
                        FrameRate fr = _cfg.getAs<FrameRate>(MediaConfig::FrameRate, fallback);
                        if(!fr.isValid()) fr = fallback;

                        NV_ENC_INITIALIZE_PARAMS init{};
                        init.version         = NV_ENC_INITIALIZE_PARAMS_VER;
                        init.encodeGUID      = encGuid;
                        init.presetGUID      = presetGuid;
                        init.tuningInfo      = tuning;
                        init.encodeWidth     = _width;
                        init.encodeHeight    = _height;
                        init.darWidth        = _width;
                        init.darHeight       = _height;
                        init.frameRateNum    = fr.numerator();
                        init.frameRateDen    = fr.denominator();
                        init.enablePTD       = 1;
                        init.encodeConfig    = &encCfg;

                        st = gNvenc.nvEncInitializeEncoder(_encoder, &init);
                        if(st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncInitializeEncoder failed (%d)", (int)st));
                        }

                        if(Error err = allocateSlots(); err.isError()) return err;

                        _sessionOpen = true;
                        clearError();
                        return Error::Ok;
                }

                Error retainCudaContext() {
                        if(_ctxRetained) return Error::Ok;

                        if(Error err = CudaBootstrap::ensureRegistered(); err.isError()) {
                                return setError(err, "CudaBootstrap::ensureRegistered failed");
                        }
                        // The library doesn't pick a device for the user
                        // — honour whatever the current thread already
                        // selected via CudaDevice::setCurrent (defaulting
                        // to device 0 when nothing was selected yet).
                        if(Error err = CudaDevice::setCurrent(0); err.isError()) {
                                return setError(err, "CudaDevice::setCurrent failed");
                        }

                        CUresult cr = cuInit(0);
                        if(cr != CUDA_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("cuInit failed (%d)", (int)cr));
                        }
                        cr = cuDeviceGet(&_device, 0);
                        if(cr != CUDA_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("cuDeviceGet failed (%d)", (int)cr));
                        }
                        cr = cuDevicePrimaryCtxRetain(&_cudaCtx, _device);
                        if(cr != CUDA_SUCCESS || _cudaCtx == nullptr) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("cuDevicePrimaryCtxRetain failed (%d)", (int)cr));
                        }
                        _ctxRetained = true;
                        return Error::Ok;
                }

                Error allocateSlots() {
                        _slots.resize(kNumSlots);
                        for(size_t i = 0; i < kNumSlots; ++i) {
                                Slot &s = _slots[i];

                                NV_ENC_CREATE_INPUT_BUFFER cin{};
                                cin.version    = NV_ENC_CREATE_INPUT_BUFFER_VER;
                                cin.width      = _width;
                                cin.height     = _height;
                                cin.bufferFmt  = NV_ENC_BUFFER_FORMAT_NV12;
                                NVENCSTATUS st = gNvenc.nvEncCreateInputBuffer(_encoder, &cin);
                                if(st != NV_ENC_SUCCESS) {
                                        return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncCreateInputBuffer failed (%d)", (int)st));
                                }
                                s.in = cin.inputBuffer;

                                NV_ENC_CREATE_BITSTREAM_BUFFER cb{};
                                cb.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
                                st = gNvenc.nvEncCreateBitstreamBuffer(_encoder, &cb);
                                if(st != NV_ENC_SUCCESS) {
                                        return setError(Error::LibraryFailure,
                                                String::sprintf("nvEncCreateBitstreamBuffer failed (%d)", (int)st));
                                }
                                s.out = cb.bitstreamBuffer;

                                _freeSlots.push_back(&s);
                        }
                        return Error::Ok;
                }

                Slot *acquireFreeSlot() {
                        if(_freeSlots.empty()) {
                                // Out of slots — try to drain one by
                                // pulling the head of the in-flight queue
                                // and returning it to the pool silently.
                                // The caller's output is still queued via
                                // _pendingPackets; we let them consume it
                                // on the next receivePacket.
                                if(_inFlight.empty()) return nullptr;
                                Slot *head = _inFlight.front();
                                _inFlight.pop_front();
                                if(head->hasOutput) {
                                        if(auto pkt = lockAndBuildPacket(head)) {
                                                _pendingPackets.push_back(pkt);
                                        }
                                }
                                _freeSlots.push_back(head);
                        }
                        Slot *s = _freeSlots.front();
                        _freeSlots.pop_front();
                        s->hasOutput = false;
                        return s;
                }

                Error uploadNV12(const Image &frame, Slot *slot) {
                        NV_ENC_LOCK_INPUT_BUFFER lk{};
                        lk.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
                        lk.inputBuffer = slot->in;
                        NVENCSTATUS st = gNvenc.nvEncLockInputBuffer(_encoder, &lk);
                        if(st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncLockInputBuffer failed (%d)", (int)st));
                        }

                        // NV12 layout: plane 0 is Y (height rows at width
                        // bytes each), plane 1 is interleaved UV at
                        // width bytes × height/2 rows.  NVENC exposes a
                        // single contiguous surface via bufferDataPtr
                        // with pitch in bytes; Y occupies the first
                        // height * pitch bytes, UV the next height/2 *
                        // pitch bytes.
                        auto *dst   = static_cast<uint8_t *>(lk.bufferDataPtr);
                        const auto pitch = lk.pitch;
                        slot->pitch = pitch;

                        const uint8_t *yPlane  = static_cast<const uint8_t *>(frame.plane(0)->data());
                        const uint8_t *uvPlane = frame.planes().size() > 1
                                ? static_cast<const uint8_t *>(frame.plane(1)->data())
                                : yPlane + (size_t)_width * _height;
                        const size_t srcYStride  = frame.lineStride(0);
                        const size_t srcUVStride = frame.planes().size() > 1
                                ? frame.lineStride(1)
                                : _width;

                        for(uint32_t row = 0; row < _height; ++row) {
                                std::memcpy(dst + row * pitch,
                                            yPlane + row * srcYStride,
                                            _width);
                        }
                        uint8_t *uvDst = dst + _height * pitch;
                        for(uint32_t row = 0; row < _height / 2; ++row) {
                                std::memcpy(uvDst + row * pitch,
                                            uvPlane + row * srcUVStride,
                                            _width);
                        }

                        st = gNvenc.nvEncUnlockInputBuffer(_encoder, slot->in);
                        if(st != NV_ENC_SUCCESS) {
                                return setError(Error::LibraryFailure,
                                        String::sprintf("nvEncUnlockInputBuffer failed (%d)", (int)st));
                        }
                        return Error::Ok;
                }

                MediaPacket::Ptr lockAndBuildPacket(Slot *slot) {
                        if(!_pendingPackets.empty()) {
                                auto pkt = _pendingPackets.front();
                                _pendingPackets.pop_front();
                                return pkt;
                        }

                        NV_ENC_LOCK_BITSTREAM lb{};
                        lb.version = NV_ENC_LOCK_BITSTREAM_VER;
                        lb.outputBitstream = slot->out;
                        NVENCSTATUS st = gNvenc.nvEncLockBitstream(_encoder, &lb);
                        if(st != NV_ENC_SUCCESS) {
                                setError(Error::LibraryFailure,
                                        String::sprintf("nvEncLockBitstream failed (%d)", (int)st));
                                return MediaPacket::Ptr();
                        }

                        auto buf = Buffer::Ptr::create(lb.bitstreamSizeInBytes);
                        std::memcpy(buf.modify()->data(), lb.bitstreamBufferPtr,
                                    lb.bitstreamSizeInBytes);
                        buf.modify()->setSize(lb.bitstreamSizeInBytes);
                        const bool isKey = (lb.pictureType == NV_ENC_PIC_TYPE_IDR
                                         || lb.pictureType == NV_ENC_PIC_TYPE_I);

                        gNvenc.nvEncUnlockBitstream(_encoder, slot->out);

                        auto pkt = MediaPacket::Ptr::create(buf, outputPixelDesc());
                        pkt.modify()->setPts(slot->pts);
                        pkt.modify()->setDts(slot->pts);
                        if(isKey) pkt.modify()->addFlag(MediaPacket::Keyframe);
                        // Carry per-image metadata across the codec
                        // boundary: things like Timecode and user keys
                        // that don't live in the H.264 / HEVC bitstream
                        // ride along on the MediaPacket and get
                        // re-applied to the decoded Image by the
                        // matching VideoDecoder.
                        if(!slot->imageMeta.isEmpty()) {
                                pkt.modify()->metadata() = slot->imageMeta;
                                slot->imageMeta = Metadata();
                        }
                        return pkt;
                }

                void destroySession() {
                        if(_sessionOpen) {
                                for(Slot &s : _slots) {
                                        if(s.in)  gNvenc.nvEncDestroyInputBuffer(_encoder, s.in);
                                        if(s.out) gNvenc.nvEncDestroyBitstreamBuffer(_encoder, s.out);
                                }
                                _slots.clear();
                                _freeSlots.clear();
                                _inFlight.clear();
                                _pendingPackets.clear();
                                if(_encoder) gNvenc.nvEncDestroyEncoder(_encoder);
                                _encoder = nullptr;
                                _sessionOpen = false;
                        }
                        if(_ctxRetained) {
                                cuDevicePrimaryCtxRelease(_device);
                                _ctxRetained = false;
                                _cudaCtx = nullptr;
                        }
                        _width = _height = 0;
                        _frameIdx = 0;
                        _eosPending = false;
                }

                std::deque<MediaPacket::Ptr> _pendingPackets;
};

// ---------------------------------------------------------------------------
// NvencVideoEncoder thin shims that forward to Impl.
// ---------------------------------------------------------------------------

NvencVideoEncoder::NvencVideoEncoder(Codec codec)
        : _impl(new Impl(codec)), _codec(codec) {}

NvencVideoEncoder::~NvencVideoEncoder() { delete _impl; }

String NvencVideoEncoder::name() const {
        return _codec == Codec_H264 ? String("H264") : String("HEVC");
}

String NvencVideoEncoder::description() const {
        return _codec == Codec_H264
                ? String("NVIDIA NVENC H.264 hardware encoder")
                : String("NVIDIA NVENC HEVC hardware encoder");
}

PixelDesc NvencVideoEncoder::outputPixelDesc() const {
        return _impl->outputPixelDesc();
}

List<int> NvencVideoEncoder::supportedInputs() const {
        return { static_cast<int>(PixelDesc::YUV8_420_SemiPlanar_Rec709) };
}

void NvencVideoEncoder::configure(const MediaConfig &config) {
        _impl->configure(config);
}

Error NvencVideoEncoder::submitFrame(const Image &frame, const MediaTimeStamp &pts) {
        _impl->clearError();
        Error err = _impl->submitFrame(frame, pts, _requestKey);
        _requestKey = false;
        // Propagate the last error onto the public-facing storage so
        // callers reading lastError() / lastErrorMessage() see the same
        // code the Impl recorded internally.
        _lastError        = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

MediaPacket::Ptr NvencVideoEncoder::receivePacket() {
        return _impl->receivePacket();
}

Error NvencVideoEncoder::flush() {
        _impl->clearError();
        Error err = _impl->flush();
        _lastError        = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

Error NvencVideoEncoder::reset() {
        _impl->clearError();
        Error err = _impl->reset();
        _lastError        = _impl->lastError();
        _lastErrorMessage = _impl->lastErrorMessage();
        return err;
}

void NvencVideoEncoder::requestKeyframe() { _requestKey = true; }

// ---------------------------------------------------------------------------
// Factory registration.  Two surfaces:
//   1. The legacy string-keyed VideoEncoder::registerEncoder registry —
//      what MediaIOTask_VideoEncoder still looks up via MediaConfig::VideoCodec.
//   2. The typed VideoCodec::H264 / VideoCodec::HEVC factory hooks —
//      the long-term path; populated by re-registering each codec's
//      Data record with createEncoder filled in.
// Both surfaces will collapse into one once MediaConfig::VideoCodec
// flips from TypeString to TypeVideoCodec.
// ---------------------------------------------------------------------------

namespace {

struct NvencRegistrar {
        NvencRegistrar() {
                auto h264Factory = []() -> VideoEncoder * {
                        return new NvencVideoEncoder(NvencVideoEncoder::Codec_H264);
                };
                auto hevcFactory = []() -> VideoEncoder * {
                        return new NvencVideoEncoder(NvencVideoEncoder::Codec_HEVC);
                };

                VideoEncoder::registerEncoder("H264", h264Factory);
                VideoEncoder::registerEncoder("HEVC", hevcFactory);

                // Wire the typed VideoCodec entries' encoder hooks.
                // registerData() overwrites the entry in place under
                // the same ID, so the well-known codec keeps its
                // name / desc / fourccs / compressedPixelDescs
                // populated by videocodec.cpp and gains the factory.
                if(VideoCodec h264(VideoCodec::H264); h264.isValid()) {
                        VideoCodec::Data d = *h264.data();
                        d.createEncoder = h264Factory;
                        VideoCodec::registerData(std::move(d));
                }
                if(VideoCodec hevc(VideoCodec::HEVC); hevc.isValid()) {
                        VideoCodec::Data d = *hevc.data();
                        d.createEncoder = hevcFactory;
                        VideoCodec::registerData(std::move(d));
                }
        }
};

static NvencRegistrar _nvencRegistrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NVENC
