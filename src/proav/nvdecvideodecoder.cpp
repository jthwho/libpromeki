/**
 * @file      nvdecvideodecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 *
 * First-cut NVDEC backend for H.264 / HEVC.  Mirrors the scope of
 * @ref NvencVideoEncoder: Annex-B byte stream in, NV12 Image out in
 * host memory, single CUDA context shared with NVENC via
 * cuDevicePrimaryCtxRetain, and dlopen of libnvcuvid.so.1 so we
 * never link against the driver library at build time.
 *
 * The NVDEC model is a three-callback pipeline driven by the
 * CUvideoparser:
 *
 *   pfnSequenceCallback  → parser finished header parsing; we know
 *                          the codec/resolution and can create the
 *                          CUvideodecoder here.
 *   pfnDecodePicture     → parser has a picture ready to be decoded
 *                          in decode-order; we call cuvidDecodePicture.
 *   pfnDisplayPicture    → a previously-decoded picture is ready in
 *                          display-order; we map it out via
 *                          cuvidMapVideoFrame64, copy NV12 to host
 *                          memory, unmap, and queue the resulting
 *                          Image for receiveFrame().
 *
 * All three callbacks fire synchronously on whatever thread called
 * cuvidParseVideoData — for us that's the MediaIOTask worker
 * thread — so we can build Images and append them to a plain
 * std::deque without extra synchronisation.
 */

#include <promeki/nvdecvideodecoder.h>

#if PROMEKI_ENABLE_NVDEC

#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
#include <promeki/cuda.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/videocodec.h>

#include <deque>
#include <mutex>
#include <cstring>
#include <dlfcn.h>

#include <cuda.h>
#include <cuda_runtime.h>   // for cudaMemcpy2D host<-device
#include <nvcuvid.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Dynamic loader for libnvcuvid.so.1.
// ---------------------------------------------------------------------------

namespace {

// The set of symbols we need from libnvcuvid.  Keeping the table as
// plain function-pointer members lets us build Nvdec* for every call
// site without worrying about the SDK's own header-level #defines.
struct NvcuvidFns {
        CUresult (CUDAAPI *CreateVideoParser)(CUvideoparser *, CUVIDPARSERPARAMS *);
        CUresult (CUDAAPI *ParseVideoData)(CUvideoparser, CUVIDSOURCEDATAPACKET *);
        CUresult (CUDAAPI *DestroyVideoParser)(CUvideoparser);
        CUresult (CUDAAPI *CreateDecoder)(CUvideodecoder *, CUVIDDECODECREATEINFO *);
        CUresult (CUDAAPI *DestroyDecoder)(CUvideodecoder);
        CUresult (CUDAAPI *DecodePicture)(CUvideodecoder, CUVIDPICPARAMS *);
        CUresult (CUDAAPI *MapVideoFrame64)(CUvideodecoder, int, unsigned long long *,
                                            unsigned int *, CUVIDPROCPARAMS *);
        CUresult (CUDAAPI *UnmapVideoFrame64)(CUvideodecoder, unsigned long long);
};

NvcuvidFns   gCuvid{};
bool         gCuvidLoaded = false;
std::mutex   gCuvidMutex;

template<typename T>
bool resolve(void *lib, const char *name, T &out) {
        out = reinterpret_cast<T>(dlsym(lib, name));
        if(!out) {
                promekiErr("NVDEC: dlsym(%s) failed: %s", name, dlerror());
                return false;
        }
        return true;
}

bool loadCuvidLocked() {
        if(gCuvidLoaded) return true;
        void *lib = dlopen("libnvcuvid.so.1", RTLD_NOW | RTLD_LOCAL);
        if(!lib) {
                promekiErr("NVDEC: dlopen(libnvcuvid.so.1) failed: %s", dlerror());
                return false;
        }
        bool ok = true;
        ok &= resolve(lib, "cuvidCreateVideoParser",  gCuvid.CreateVideoParser);
        ok &= resolve(lib, "cuvidParseVideoData",     gCuvid.ParseVideoData);
        ok &= resolve(lib, "cuvidDestroyVideoParser", gCuvid.DestroyVideoParser);
        ok &= resolve(lib, "cuvidCreateDecoder",      gCuvid.CreateDecoder);
        ok &= resolve(lib, "cuvidDestroyDecoder",     gCuvid.DestroyDecoder);
        ok &= resolve(lib, "cuvidDecodePicture",      gCuvid.DecodePicture);
        ok &= resolve(lib, "cuvidMapVideoFrame64",    gCuvid.MapVideoFrame64);
        ok &= resolve(lib, "cuvidUnmapVideoFrame64",  gCuvid.UnmapVideoFrame64);
        if(!ok) return false;
        gCuvidLoaded = true;
        return true;
}

bool loadCuvid() {
        std::lock_guard<std::mutex> lock(gCuvidMutex);
        return loadCuvidLocked();
}

// Maps CUresult to the closest Error code; anything we don't
// explicitly translate becomes LibraryFailure + a log line carrying
// the CUDA error string.
Error mapCu(CUresult r, const char *op) {
        if(r == CUDA_SUCCESS) return Error::Ok;
        const char *msg = nullptr;
        cuGetErrorString(r, &msg);
        promekiErr("NVDEC: %s failed (%d): %s", op, (int)r, msg ? msg : "");
        return Error::LibraryFailure;
}

// RAII wrapper that pushes a CUcontext onto the calling thread's
// context stack on construction and pops it on destruction.  Needed
// because MediaIOTask dispatches commands across a thread pool —
// consecutive executeCmd() calls on the same task may land on
// different worker threads, each of which has its own independent
// context stack.  cuvidCreateDecoder / cuvidDecodePicture /
// cuvidMapVideoFrame64 all use whichever context is current on the
// calling thread, so scoping a push/pop around each API call makes
// the backend robust to thread hopping without us having to track
// which pool thread is current.
class CudaCtxGuard {
        public:
                explicit CudaCtxGuard(CUcontext ctx) {
                        if(ctx != nullptr && cuCtxPushCurrent(ctx) == CUDA_SUCCESS) {
                                _pushed = true;
                        }
                }
                ~CudaCtxGuard() {
                        if(_pushed) {
                                CUcontext popped = nullptr;
                                cuCtxPopCurrent(&popped);
                        }
                }
                CudaCtxGuard(const CudaCtxGuard &) = delete;
                CudaCtxGuard &operator=(const CudaCtxGuard &) = delete;

        private:
                bool _pushed = false;
};

} // namespace

// ---------------------------------------------------------------------------
// NvdecVideoDecoder::Impl
// ---------------------------------------------------------------------------

class NvdecVideoDecoder::Impl {
        public:
                explicit Impl(Codec codec) : _codec(codec) {}

                ~Impl() { destroySession(); }

                void configure(const MediaConfig &cfg) {
                        (void)cfg;
                        // No user-tunable NVDEC state today.  We keep
                        // the override hook for symmetry with the
                        // VideoDecoder base class; later revisions may
                        // accept explicit max-width / max-height hints
                        // or a preferred output PixelDesc.
                }

                Error submitPacket(const MediaPacket &pkt, Codec codec) {
                        if(Error err = ensureSession(codec); err.isError()) return err;
                        if(pkt.size() == 0 || !pkt.buffer()) return Error::Ok;

                        // Stash the packet's per-image metadata (things
                        // like Timecode / MediaTimeStamp / user keys
                        // that the encoder copied onto the packet) so
                        // handleDisplay can re-attach them to the
                        // emitted Image.  The queue is strict FIFO —
                        // safe because display-order equals decode-order
                        // for pure I/P streams and we don't enable
                        // B-frames in the encoder.
                        _packetMetaQueue.push_back(pkt.metadata());

                        // Push the encoded bytes into the parser.  The
                        // parser synchronously invokes our Sequence /
                        // Decode / Display callbacks during this call,
                        // so by the time we return any displayable
                        // frames for this packet are already on
                        // _outQueue.  The context guard ensures the
                        // CUDA primary context is current on whichever
                        // thread pool worker happens to be running this
                        // command.
                        CudaCtxGuard guard(_cudaCtx);
                        CUVIDSOURCEDATAPACKET srcPkt{};
                        srcPkt.payload     = static_cast<const unsigned char *>(pkt.view().data());
                        srcPkt.payload_size = static_cast<unsigned long>(pkt.size());
                        if(pkt.pts().isValid()) {
                                srcPkt.timestamp = static_cast<CUvideotimestamp>(
                                        pkt.pts().timeStamp().value().time_since_epoch().count());
                                srcPkt.flags    |= CUVID_PKT_TIMESTAMP;
                        }
                        CUresult r = gCuvid.ParseVideoData(_parser, &srcPkt);
                        return mapCu(r, "cuvidParseVideoData");
                }

                Image receiveFrame() {
                        if(_outQueue.empty()) return Image();
                        Image img = std::move(_outQueue.front());
                        _outQueue.pop_front();
                        return img;
                }

                Error flush() {
                        if(_parser == nullptr) return Error::Ok;
                        CudaCtxGuard guard(_cudaCtx);
                        CUVIDSOURCEDATAPACKET eosPkt{};
                        eosPkt.flags = CUVID_PKT_ENDOFSTREAM;
                        CUresult r = gCuvid.ParseVideoData(_parser, &eosPkt);
                        return mapCu(r, "cuvidParseVideoData(EOS)");
                }

                Error reset() {
                        destroySession();
                        return Error::Ok;
                }

                PixelDesc outputPixelDesc() const {
                        return PixelDesc(PixelDesc::YUV8_420_SemiPlanar_Rec709);
                }

        private:
                Codec            _codec;

                CUdevice         _device      = 0;
                CUcontext        _cudaCtx     = nullptr;
                bool             _ctxRetained = false;

                CUvideoparser    _parser      = nullptr;
                CUvideodecoder   _decoder     = nullptr;

                unsigned int     _codedWidth  = 0;
                unsigned int     _codedHeight = 0;
                unsigned int     _displayW    = 0;
                unsigned int     _displayH    = 0;

                std::deque<Image> _outQueue;

                // Per-packet metadata FIFO.  submitPacket pushes one
                // entry per incoming MediaPacket; handleDisplay pops
                // one entry per emitted Image.  Together they carry
                // encoder-side per-image state (Timecode, MediaTimeStamp)
                // across the codec boundary.
                std::deque<Metadata> _packetMetaQueue;

                // ---- Callback thunks ----------------------------------
                static int CUDAAPI onSequence(void *user, CUVIDEOFORMAT *fmt) {
                        return static_cast<Impl *>(user)->handleSequence(fmt);
                }
                static int CUDAAPI onDecode(void *user, CUVIDPICPARAMS *pic) {
                        return static_cast<Impl *>(user)->handleDecode(pic);
                }
                static int CUDAAPI onDisplay(void *user, CUVIDPARSERDISPINFO *info) {
                        return static_cast<Impl *>(user)->handleDisplay(info);
                }

                // ---- Session lifecycle --------------------------------
                Error ensureSession(Codec codec) {
                        if(_parser != nullptr) return Error::Ok;
                        if(!loadCuvid()) {
                                return Error::LibraryFailure;
                        }
                        if(Error err = retainCudaContext(); err.isError()) return err;

                        CUVIDPARSERPARAMS pp{};
                        pp.CodecType = (codec == Codec_H264)
                                ? cudaVideoCodec_H264
                                : cudaVideoCodec_HEVC;
                        pp.ulMaxNumDecodeSurfaces = 20;
                        pp.ulClockRate            = 10000000;
                        pp.ulMaxDisplayDelay      = 0;   // lowest latency
                        pp.pUserData              = this;
                        pp.pfnSequenceCallback    = &Impl::onSequence;
                        pp.pfnDecodePicture       = &Impl::onDecode;
                        pp.pfnDisplayPicture      = &Impl::onDisplay;

                        CUresult r = gCuvid.CreateVideoParser(&_parser, &pp);
                        if(r != CUDA_SUCCESS) {
                                return mapCu(r, "cuvidCreateVideoParser");
                        }
                        return Error::Ok;
                }

                Error retainCudaContext() {
                        if(_ctxRetained) return Error::Ok;
                        if(Error err = CudaBootstrap::ensureRegistered(); err.isError()) return err;
                        if(Error err = CudaDevice::setCurrent(0); err.isError()) return err;

                        CUresult r = cuInit(0);
                        if(r != CUDA_SUCCESS) return mapCu(r, "cuInit");
                        r = cuDeviceGet(&_device, 0);
                        if(r != CUDA_SUCCESS) return mapCu(r, "cuDeviceGet");
                        r = cuDevicePrimaryCtxRetain(&_cudaCtx, _device);
                        if(r != CUDA_SUCCESS) return mapCu(r, "cuDevicePrimaryCtxRetain");
                        if(_cudaCtx == nullptr) {
                                promekiErr("NVDEC: cuDevicePrimaryCtxRetain returned null ctx");
                                return Error::LibraryFailure;
                        }
                        // Don't push the context here — MediaIOTask may
                        // dispatch our subsequent executeCmd() calls to
                        // different pool threads.  Each entry point
                        // that needs the context (submitPacket, flush)
                        // instead uses a CudaCtxGuard to push/pop
                        // around the cuvid call on the current thread.
                        _ctxRetained = true;
                        return Error::Ok;
                }

                void destroySession() {
                        {
                                // cuvidDestroyDecoder / cuvidDestroyVideoParser
                                // both touch the decoder's CUDA state,
                                // so they need the context current too.
                                CudaCtxGuard guard(_cudaCtx);
                                if(_decoder) {
                                        gCuvid.DestroyDecoder(_decoder);
                                        _decoder = nullptr;
                                }
                                if(_parser) {
                                        gCuvid.DestroyVideoParser(_parser);
                                        _parser = nullptr;
                                }
                        }
                        _outQueue.clear();
                        _packetMetaQueue.clear();
                        if(_ctxRetained) {
                                cuDevicePrimaryCtxRelease(_device);
                                _ctxRetained = false;
                                _cudaCtx = nullptr;
                        }
                        _codedWidth = _codedHeight = _displayW = _displayH = 0;
                }

                // ---- Callbacks ----------------------------------------
                int handleSequence(CUVIDEOFORMAT *fmt) {
                        // Tear down any previous decoder — a sequence
                        // callback can fire mid-stream if the bitstream
                        // resolution or format changes, and the cheap
                        // correct move is to rebuild instead of paying
                        // the complexity cost of cuvidReconfigureDecoder
                        // for our first cut.
                        if(_decoder) {
                                gCuvid.DestroyDecoder(_decoder);
                                _decoder = nullptr;
                        }

                        _codedWidth  = fmt->coded_width;
                        _codedHeight = fmt->coded_height;
                        // Some encoders (notably NVENC HEVC at small
                        // resolutions) don't populate display_area at
                        // all — if both sides are zero, treat the full
                        // coded frame as the display region.  Without
                        // this fallback, cuvidCreateDecoder rejects the
                        // request with CUDA_ERROR_INVALID_VALUE.
                        if(fmt->display_area.right  <= fmt->display_area.left ||
                           fmt->display_area.bottom <= fmt->display_area.top) {
                                _displayW = fmt->coded_width;
                                _displayH = fmt->coded_height;
                        } else {
                                _displayW = fmt->display_area.right  - fmt->display_area.left;
                                _displayH = fmt->display_area.bottom - fmt->display_area.top;
                        }

                        CUVIDDECODECREATEINFO ci{};
                        ci.CodecType         = fmt->codec;
                        ci.ChromaFormat      = fmt->chroma_format;
                        ci.OutputFormat      = cudaVideoSurfaceFormat_NV12;
                        ci.DeinterlaceMode   = cudaVideoDeinterlaceMode_Weave;
                        ci.ulNumDecodeSurfaces = (fmt->min_num_decode_surfaces > 0)
                                ? fmt->min_num_decode_surfaces
                                : 8;
                        ci.ulNumOutputSurfaces = 2;
                        ci.ulCreationFlags   = cudaVideoCreate_PreferCUVID;
                        ci.bitDepthMinus8    = fmt->bit_depth_luma_minus8;
                        ci.ulIntraDecodeOnly = 0;
                        ci.ulWidth           = fmt->coded_width;
                        ci.ulHeight          = fmt->coded_height;
                        ci.ulMaxWidth        = fmt->coded_width;
                        ci.ulMaxHeight       = fmt->coded_height;
                        ci.ulTargetWidth     = _displayW;
                        ci.ulTargetHeight    = _displayH;
                        // Leave display_area / target_rect at their
                        // default (zero) values — the driver then
                        // applies its own "whole frame" defaults, which
                        // avoids the HEVC-at-small-resolutions case
                        // where explicit zero-rects trigger INVALID_VALUE.

                        CUresult r = gCuvid.CreateDecoder(&_decoder, &ci);
                        if(r != CUDA_SUCCESS) {
                                mapCu(r, "cuvidCreateDecoder");
                                return 0;   // parser treats 0 as "fail"
                        }

                        // Returning ulNumDecodeSurfaces here asks the
                        // parser to honour the same DPB size we just
                        // configured the decoder with.
                        return static_cast<int>(ci.ulNumDecodeSurfaces);
                }

                int handleDecode(CUVIDPICPARAMS *pic) {
                        if(_decoder == nullptr) return 0;
                        CUresult r = gCuvid.DecodePicture(_decoder, pic);
                        if(r != CUDA_SUCCESS) {
                                mapCu(r, "cuvidDecodePicture");
                                return 0;
                        }
                        return 1;
                }

                int handleDisplay(CUVIDPARSERDISPINFO *info) {
                        if(_decoder == nullptr) return 0;

                        CUVIDPROCPARAMS pp{};
                        pp.progressive_frame = info->progressive_frame;
                        pp.top_field_first   = info->top_field_first;
                        pp.unpaired_field    = (info->repeat_first_field < 0) ? 1 : 0;
                        pp.output_stream     = nullptr;  // default stream

                        unsigned long long devPtr = 0;
                        unsigned int       pitch  = 0;
                        CUresult r = gCuvid.MapVideoFrame64(_decoder, info->picture_index,
                                                            &devPtr, &pitch, &pp);
                        if(r != CUDA_SUCCESS) {
                                mapCu(r, "cuvidMapVideoFrame64");
                                return 0;
                        }

                        // Build a system-memory NV12 Image sized to
                        // the display rectangle, then cudaMemcpy2D
                        // luma + chroma planes down from device.
                        ImageDesc desc(Size2Du32(_displayW, _displayH),
                                       outputPixelDesc());
                        Image img(desc);
                        bool copyOk = true;
                        if(img.planes().size() >= 2) {
                                void *yDst  = img.data(0);
                                void *uvDst = img.data(1);
                                const size_t yStride  = img.lineStride(0);
                                const size_t uvStride = img.lineStride(1);
                                // cuvidMapVideoFrame64 returns a
                                // device pointer whose Y plane
                                // occupies `pitch * ulTargetHeight`
                                // bytes (the post-processed output
                                // height we configured, == _displayH),
                                // with the interleaved UV plane
                                // immediately following.  Using
                                // _codedHeight here instead (as some
                                // older samples do) reaches past the
                                // mapped region on narrow-crop streams
                                // and trips cudaMemcpy2D with
                                // "invalid argument".
                                const unsigned long long yDev  = devPtr;
                                const unsigned long long uvDev = devPtr
                                        + static_cast<unsigned long long>(pitch) * _displayH;

                                cudaError_t ce = cudaMemcpy2D(yDst, yStride,
                                                              reinterpret_cast<const void *>(yDev),
                                                              pitch,
                                                              _displayW, _displayH,
                                                              cudaMemcpyDeviceToHost);
                                if(ce != cudaSuccess) {
                                        promekiErr("NVDEC: cudaMemcpy2D(Y) failed: %s",
                                                   cudaGetErrorString(ce));
                                        copyOk = false;
                                }
                                if(copyOk) {
                                        ce = cudaMemcpy2D(uvDst, uvStride,
                                                          reinterpret_cast<const void *>(uvDev),
                                                          pitch,
                                                          _displayW, _displayH / 2,
                                                          cudaMemcpyDeviceToHost);
                                        if(ce != cudaSuccess) {
                                                promekiErr("NVDEC: cudaMemcpy2D(UV) failed: %s",
                                                           cudaGetErrorString(ce));
                                                copyOk = false;
                                        }
                                }
                        } else {
                                copyOk = false;
                        }

                        gCuvid.UnmapVideoFrame64(_decoder, devPtr);
                        if(!copyOk) return 0;

                        // Pair this display with the oldest queued
                        // packet metadata.  The FIFO may be empty if
                        // the caller flushed or if we hit an unexpected
                        // reorder — attach whatever's at the head and
                        // fall back to default-constructed Metadata
                        // otherwise so the Image still looks reasonable
                        // downstream.
                        if(!_packetMetaQueue.empty()) {
                                img.metadata() = std::move(_packetMetaQueue.front());
                                _packetMetaQueue.pop_front();
                        }

                        _outQueue.push_back(std::move(img));
                        return 1;
                }
};

// ---------------------------------------------------------------------------
// Thin NvdecVideoDecoder façade that forwards to Impl.
// ---------------------------------------------------------------------------

NvdecVideoDecoder::NvdecVideoDecoder(Codec codec)
        : _impl(new Impl(codec)), _codec(codec) {}

NvdecVideoDecoder::~NvdecVideoDecoder() { delete _impl; }

String NvdecVideoDecoder::name() const {
        return _codec == Codec_H264 ? String("H264") : String("HEVC");
}

String NvdecVideoDecoder::description() const {
        return _codec == Codec_H264
                ? String("NVIDIA NVDEC H.264 hardware decoder")
                : String("NVIDIA NVDEC HEVC hardware decoder");
}

PixelDesc NvdecVideoDecoder::inputPixelDesc() const {
        return PixelDesc(_codec == Codec_H264
                ? PixelDesc::H264
                : PixelDesc::HEVC);
}

List<int> NvdecVideoDecoder::supportedOutputs() const {
        return { static_cast<int>(PixelDesc::YUV8_420_SemiPlanar_Rec709) };
}

void NvdecVideoDecoder::configure(const MediaConfig &config) {
        _impl->configure(config);
}

Error NvdecVideoDecoder::submitPacket(const MediaPacket &packet) {
        clearError();
        Error err = _impl->submitPacket(packet, _codec);
        if(err.isError()) {
                _lastError        = err;
                _lastErrorMessage = String("NVDEC submitPacket failed");
        }
        return err;
}

Image NvdecVideoDecoder::receiveFrame() {
        return _impl->receiveFrame();
}

Error NvdecVideoDecoder::flush() {
        clearError();
        Error err = _impl->flush();
        if(err.isError()) {
                _lastError        = err;
                _lastErrorMessage = String("NVDEC flush failed");
        }
        return err;
}

Error NvdecVideoDecoder::reset() {
        clearError();
        return _impl->reset();
}

// ---------------------------------------------------------------------------
// Factory registration.
// ---------------------------------------------------------------------------

namespace {

struct NvdecRegistrar {
        NvdecRegistrar() {
                auto h264Factory = []() -> VideoDecoder * {
                        return new NvdecVideoDecoder(NvdecVideoDecoder::Codec_H264);
                };
                auto hevcFactory = []() -> VideoDecoder * {
                        return new NvdecVideoDecoder(NvdecVideoDecoder::Codec_HEVC);
                };

                VideoDecoder::registerDecoder("H264", h264Factory);
                VideoDecoder::registerDecoder("HEVC", hevcFactory);

                // Same dual-surface story as the NVENC registrar:
                // registerData() merges the decoder factory into the
                // existing typed VideoCodec::H264 / VideoCodec::HEVC
                // entries while preserving every other field
                // populated by videocodec.cpp.
                if(VideoCodec h264(VideoCodec::H264); h264.isValid()) {
                        VideoCodec::Data d = *h264.data();
                        d.createDecoder = h264Factory;
                        VideoCodec::registerData(std::move(d));
                }
                if(VideoCodec hevc(VideoCodec::HEVC); hevc.isValid()) {
                        VideoCodec::Data d = *hevc.data();
                        d.createDecoder = hevcFactory;
                        VideoCodec::registerData(std::move(d));
                }
        }
};

static NvdecRegistrar _nvdecRegistrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NVDEC
