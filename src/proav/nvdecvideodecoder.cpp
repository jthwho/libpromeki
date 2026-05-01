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
 * cuvidParseVideoData — for us that's the MediaIO worker
 * thread — so we can build Images and append them to a plain
 * std::deque without extra synchronisation.
 */

#include <promeki/nvdecvideodecoder.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>

#if PROMEKI_ENABLE_NVDEC

#include <promeki/mediaconfig.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/buffer.h>
#include <promeki/cuda.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/videocodec.h>
#include <promeki/timecode.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/ciepoint.h>

#include <deque>
#include <mutex>
#include <cstring>
#include <dlfcn.h>

#include <cuda.h>
#include <cuda_runtime.h> // for cudaMemcpy2D host<-device
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
                        CUresult(CUDAAPI *CreateVideoParser)(CUvideoparser *, CUVIDPARSERPARAMS *);
                        CUresult(CUDAAPI *ParseVideoData)(CUvideoparser, CUVIDSOURCEDATAPACKET *);
                        CUresult(CUDAAPI *DestroyVideoParser)(CUvideoparser);
                        CUresult(CUDAAPI *CreateDecoder)(CUvideodecoder *, CUVIDDECODECREATEINFO *);
                        CUresult(CUDAAPI *DestroyDecoder)(CUvideodecoder);
                        CUresult(CUDAAPI *DecodePicture)(CUvideodecoder, CUVIDPICPARAMS *);
                        CUresult(CUDAAPI *MapVideoFrame64)(CUvideodecoder, int, unsigned long long *, unsigned int *,
                                                           CUVIDPROCPARAMS *);
                        CUresult(CUDAAPI *UnmapVideoFrame64)(CUvideodecoder, unsigned long long);
        };

        NvcuvidFns gCuvid{};
        bool       gCuvidLoaded = false;
        std::mutex gCuvidMutex;

        template <typename T> bool resolve(void *lib, const char *name, T &out) {
                out = reinterpret_cast<T>(dlsym(lib, name));
                if (!out) {
                        promekiErr("NVDEC: dlsym(%s) failed: %s", name, dlerror());
                        return false;
                }
                return true;
        }

        bool loadCuvidLocked() {
                if (gCuvidLoaded) return true;
                void *lib = dlopen("libnvcuvid.so.1", RTLD_NOW | RTLD_LOCAL);
                if (!lib) {
                        promekiErr("NVDEC: dlopen(libnvcuvid.so.1) failed: %s", dlerror());
                        return false;
                }
                bool ok = true;
                ok &= resolve(lib, "cuvidCreateVideoParser", gCuvid.CreateVideoParser);
                ok &= resolve(lib, "cuvidParseVideoData", gCuvid.ParseVideoData);
                ok &= resolve(lib, "cuvidDestroyVideoParser", gCuvid.DestroyVideoParser);
                ok &= resolve(lib, "cuvidCreateDecoder", gCuvid.CreateDecoder);
                ok &= resolve(lib, "cuvidDestroyDecoder", gCuvid.DestroyDecoder);
                ok &= resolve(lib, "cuvidDecodePicture", gCuvid.DecodePicture);
                ok &= resolve(lib, "cuvidMapVideoFrame64", gCuvid.MapVideoFrame64);
                ok &= resolve(lib, "cuvidUnmapVideoFrame64", gCuvid.UnmapVideoFrame64);
                if (!ok) return false;
                gCuvidLoaded = true;
                return true;
        }

        bool loadCuvid() {
                std::lock_guard<std::mutex> lock(gCuvidMutex);
                return loadCuvidLocked();
        }

        // ---------------------------------------------------------------------------
        // Minimal bit reader for SEI payload parsing.  SEI data is byte-aligned
        // big-endian RBSP; our parsers only need unsigned-read primitives up to
        // 32 bits wide.  Bounds checking is strict — any read past the end of
        // the payload returns 0 and flips an error flag the caller can check.
        // ---------------------------------------------------------------------------
        class BitReader {
                public:
                        BitReader(const uint8_t *data, size_t size) : _data(data), _size(size) {}

                        uint32_t readBits(int n) {
                                if (n <= 0 || n > 32) return 0;
                                uint32_t val = 0;
                                for (int i = 0; i < n; ++i) {
                                        const size_t byteIdx = _pos / 8;
                                        if (byteIdx >= _size) {
                                                _err = true;
                                                return 0;
                                        }
                                        const int bitIdx = 7 - (_pos % 8);
                                        val = (val << 1) | ((_data[byteIdx] >> bitIdx) & 0x1u);
                                        ++_pos;
                                }
                                return val;
                        }

                        bool   readFlag() { return readBits(1) != 0; }
                        bool   error() const { return _err; }
                        size_t bitsConsumed() const { return _pos; }

                private:
                        const uint8_t *_data;
                        size_t         _size;
                        size_t         _pos = 0;
                        bool           _err = false;
        };

        // Parse one H.264 clock_timestamp set (pic_timing SEI) into a
        // Timecode::Mode + digits.  Returns a default-constructed Timecode on
        // malformed input.  Follows ITU-T H.264 § D.1 / D.2.2 (pic_timing).
        //
        // The caller has already consumed the pic_struct nibble that precedes
        // the NumClockTS timestamp sets in H.264.  For HEVC time_code SEI the
        // structure is slightly different but the inner clock_timestamp fields
        // are byte-for-byte identical, so one parser serves both.
        Timecode parseClockTimestamp(BitReader &br) {
                const uint32_t countingType = br.readBits(5);
                (void)countingType;
                const bool fullTimestampFlag = br.readFlag();
                const bool discontinuityFlag = br.readFlag();
                (void)discontinuityFlag;
                const bool cntDroppedFlag = br.readFlag();
                uint32_t   nFrames = br.readBits(8);
                uint32_t   seconds = 0, minutes = 0, hours = 0;
                if (fullTimestampFlag) {
                        seconds = br.readBits(6);
                        minutes = br.readBits(6);
                        hours = br.readBits(5);
                } else {
                        const bool secondsFlag = br.readFlag();
                        if (secondsFlag) {
                                seconds = br.readBits(6);
                                const bool minutesFlag = br.readFlag();
                                if (minutesFlag) {
                                        minutes = br.readBits(6);
                                        const bool hoursFlag = br.readFlag();
                                        if (hoursFlag) hours = br.readBits(5);
                                }
                        }
                }
                if (br.error()) return Timecode();

                // Pick a Timecode mode from what we have.  We don't know the
                // bitstream's nominal frame rate without parsing the VUI
                // timing info (and even then it's ambiguous for 23.98/29.97),
                // so we default to NDF30 and flip to DF30 when cnt_dropped_flag
                // is set.  Callers who know the true rate can re-stamp the
                // Mode via Timecode::setMode() later.
                Timecode::Mode mode(cntDroppedFlag ? Timecode::DF30 : Timecode::NDF30);
                return Timecode(mode, static_cast<Timecode::DigitType>(hours),
                                static_cast<Timecode::DigitType>(minutes), static_cast<Timecode::DigitType>(seconds),
                                static_cast<Timecode::DigitType>(nFrames));
        }

        // Parse H.264 pic_timing SEI.  This SEI has a format that depends on
        // VUI flags we don't have at parse time — pic_struct_present_flag
        // gates the pic_struct / clock_timestamp block.  NVENC always emits
        // with pic_struct_present_flag = 1 when enableTimeCode is set, so we
        // assume the same and parse the full form.  If the bits don't look
        // sensible (overflow digits), we return an invalid Timecode.
        Timecode parseH264PicTiming(const uint8_t *payload, size_t size) {
                BitReader br(payload, size);
                // cpb_removal_delay / dpb_output_delay come first when
                // NalHrdBpPresent / VclHrdBpPresent is set.  Without the SPS
                // VUI we can't know their lengths; for NVENC output those
                // HRD blocks are absent, so the payload begins directly with
                // pic_struct (4 bits) and NumClockTS clock_timestamp sets.
                const uint32_t picStruct = br.readBits(4);
                // Table D-1 of H.264 maps pic_struct → NumClockTS (1..3).
                static const uint8_t kNumClockTsTable[9] = {1, 1, 1, 2, 2, 3, 3, 2, 3};
                const int            numClockTS = (picStruct < 9) ? kNumClockTsTable[picStruct] : 1;
                for (int i = 0; i < numClockTS; ++i) {
                        const bool clockTsFlag = br.readFlag();
                        if (!clockTsFlag) continue;
                        Timecode tc = parseClockTimestamp(br);
                        if (tc.isValid()) {
                                // time_offset follows as a signed 24-bit value
                                // per time_offset_length, which we don't know
                                // here — skip.  Any remaining TS sets are
                                // ignored because we only emit one Metadata::Timecode.
                                return tc;
                        }
                }
                return Timecode();
        }

        // Parse HEVC time_code SEI.  HEVC structure:
        //   num_clock_ts (2 bits)
        //   for each TS: clock_timestamp_flag (1) + clock_timestamp set.
        // Unlike H.264 the HEVC variant does not have a leading pic_struct
        // nibble and carries its own counts inline.
        Timecode parseHevcTimeCode(const uint8_t *payload, size_t size) {
                BitReader      br(payload, size);
                const uint32_t numClockTS = br.readBits(2);
                for (uint32_t i = 0; i < numClockTS; ++i) {
                        const bool clockTsFlag = br.readFlag();
                        if (!clockTsFlag) continue;
                        // HEVC: units_field_based_flag (1) + counting_type (5) +
                        //       full_timestamp_flag (1) + discontinuity_flag (1) +
                        //       cnt_dropped_flag (1) + n_frames (9) + ...
                        // vs. H.264's units_field_based_flag omitted and n_frames
                        // only 8 bits.  Handle both by re-reading.
                        const bool unitsFieldBased = br.readFlag();
                        (void)unitsFieldBased;
                        const uint32_t countingType = br.readBits(5);
                        (void)countingType;
                        const bool fullTimestampFlag = br.readFlag();
                        const bool discontinuityFlag = br.readFlag();
                        (void)discontinuityFlag;
                        const bool cntDroppedFlag = br.readFlag();
                        uint32_t   nFrames = br.readBits(9);
                        uint32_t   seconds = 0, minutes = 0, hours = 0;
                        if (fullTimestampFlag) {
                                seconds = br.readBits(6);
                                minutes = br.readBits(6);
                                hours = br.readBits(5);
                        } else {
                                const bool secondsFlag = br.readFlag();
                                if (secondsFlag) {
                                        seconds = br.readBits(6);
                                        const bool minutesFlag = br.readFlag();
                                        if (minutesFlag) {
                                                minutes = br.readBits(6);
                                                const bool hoursFlag = br.readFlag();
                                                if (hoursFlag) hours = br.readBits(5);
                                        }
                                }
                        }
                        if (br.error()) return Timecode();
                        Timecode::Mode mode(cntDroppedFlag ? Timecode::DF30 : Timecode::NDF30);
                        return Timecode(mode, static_cast<Timecode::DigitType>(hours),
                                        static_cast<Timecode::DigitType>(minutes),
                                        static_cast<Timecode::DigitType>(seconds),
                                        static_cast<Timecode::DigitType>(nFrames));
                }
                return Timecode();
        }

        // Parse mastering_display_colour_volume SEI (payloadType 137).  On
        // wire the fields are defined as big-endian (SMPTE ST 2086 / HEVC spec
        // D.2.28), but NVDEC's parser delivers the pSEIData buffer with the
        // integer fields already byte-swapped to host order — so we read the
        // u16 / u32 values using the host's native endianness rather than the
        // bitstream convention.  This was confirmed empirically against the
        // NVENC → NVDEC round-trip: payloads that matched the bitstream spec
        // when swapped.
        bool parseMasteringDisplaySei(const uint8_t *p, size_t size, MasteringDisplay &out) {
                if (size < 24) return false;
                auto u16 = [&](size_t o) {
                        uint16_t v;
                        std::memcpy(&v, p + o, sizeof(v));
                        return v;
                };
                auto u32 = [&](size_t o) {
                        uint32_t v;
                        std::memcpy(&v, p + o, sizeof(v));
                        return v;
                };
                // Display primaries are stored in (G, B, R) order in the SEI
                // payload — the spec's little inversion from the usual R/G/B.
                CIEPoint     g(u16(0) / 50000.0, u16(2) / 50000.0);
                CIEPoint     b(u16(4) / 50000.0, u16(6) / 50000.0);
                CIEPoint     r(u16(8) / 50000.0, u16(10) / 50000.0);
                CIEPoint     w(u16(12) / 50000.0, u16(14) / 50000.0);
                const double maxLuma = u32(16) / 10000.0;
                const double minLuma = u32(20) / 10000.0;
                out = MasteringDisplay(r, g, b, w, minLuma, maxLuma);
                return true;
        }

        // Parse content_light_level_info SEI (payloadType 144).  Two 16-bit
        // values — MaxCLL, MaxFALL — delivered in host byte order (see note on
        // parseMasteringDisplaySei for the endianness rationale).
        bool parseContentLightLevelSei(const uint8_t *p, size_t size, ContentLightLevel &out) {
                if (size < 4) return false;
                uint16_t maxCll, maxFall;
                std::memcpy(&maxCll, p + 0, sizeof(maxCll));
                std::memcpy(&maxFall, p + 2, sizeof(maxFall));
                out = ContentLightLevel(maxCll, maxFall);
                return true;
        }

        // Maps CUresult to the closest Error code; anything we don't
        // explicitly translate becomes LibraryFailure + a log line carrying
        // the CUDA error string.
        Error mapCu(CUresult r, const char *op) {
                if (r == CUDA_SUCCESS) return Error::Ok;
                const char *msg = nullptr;
                cuGetErrorString(r, &msg);
                promekiErr("NVDEC: %s failed (%d): %s", op, (int)r, msg ? msg : "");
                return Error::LibraryFailure;
        }

        // RAII wrapper that pushes a CUcontext onto the calling thread's
        // context stack on construction and pops it on destruction.  Needed
        // because the SharedThreadMediaIO strand dispatches commands across a thread pool —
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
                                if (ctx != nullptr && cuCtxPushCurrent(ctx) == CUDA_SUCCESS) {
                                        _pushed = true;
                                }
                        }
                        ~CudaCtxGuard() {
                                if (_pushed) {
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
                        // Caller-visible overrides for the VUI color
                        // description.  Default is Auto / Unknown, which
                        // means "use whatever the bitstream signals" —
                        // any other value wins over the bitstream-parsed
                        // counterpart and gets stamped verbatim onto
                        // every output Image's Metadata.
                        //
                        // Helper: VariantDatabase::getAs<Enum> returns a
                        // default-constructed Enum (value -1) when the
                        // key is absent, not the spec default, so we
                        // look up the spec default ourselves to get Auto
                        // / Unknown instead of an InvalidValue sentinel
                        // that would leak into the resolve() logic below.
                        auto readEnum = [&cfg](MediaConfig::ID key) -> Enum {
                                const VariantSpec *s = MediaConfig::spec(key);
                                if (!cfg.contains(key)) {
                                        return s ? s->defaultValue().get<Enum>() : Enum();
                                }
                                return cfg.getAs<Enum>(key);
                        };
                        _overridePrimaries = ColorPrimaries(readEnum(MediaConfig::VideoColorPrimaries).value());
                        _overrideTransfer =
                                TransferCharacteristics(readEnum(MediaConfig::VideoTransferCharacteristics).value());
                        _overrideMatrix = MatrixCoefficients(readEnum(MediaConfig::VideoMatrixCoefficients).value());
                        _overrideRange = VideoRange(readEnum(MediaConfig::VideoRange).value());
                }

                Error submitPacket(const CompressedVideoPayload &payload, Codec codec) {
                        if (Error err = ensureSession(codec); err.isError()) return err;
                        if (payload.planeCount() == 0) return Error::Ok;
                        auto view = payload.plane(0);
                        if (view.size() == 0) return Error::Ok;

                        // Stash the payload's per-image metadata (things
                        // like Timecode / MediaTimeStamp / user keys
                        // that the encoder copied onto the packet) so
                        // handleDisplay can re-attach them to the
                        // emitted payload.  The queue is strict FIFO —
                        // safe because display-order equals decode-order
                        // for pure I/P streams and we don't enable
                        // B-frames in the encoder.
                        _packetMetaQueue.push_back(payload.metadata());

                        // Push the encoded bytes into the parser.  The
                        // parser synchronously invokes our Sequence /
                        // Decode / Display callbacks during this call,
                        // so by the time we return any displayable
                        // frames for this packet are already on
                        // _outQueue.  The context guard ensures the
                        // CUDA primary context is current on whichever
                        // thread pool worker happens to be running this
                        // command.
                        CudaCtxGuard          guard(_cudaCtx);
                        CUVIDSOURCEDATAPACKET srcPkt{};
                        srcPkt.payload = static_cast<const unsigned char *>(view.data());
                        srcPkt.payload_size = static_cast<unsigned long>(view.size());
                        if (payload.pts().isValid()) {
                                srcPkt.timestamp = static_cast<CUvideotimestamp>(
                                        payload.pts().timeStamp().value().time_since_epoch().count());
                                srcPkt.flags |= CUVID_PKT_TIMESTAMP;
                        }
                        CUresult r = gCuvid.ParseVideoData(_parser, &srcPkt);
                        return mapCu(r, "cuvidParseVideoData");
                }

                UncompressedVideoPayload::Ptr receiveFrame() {
                        if (_outQueue.empty()) return UncompressedVideoPayload::Ptr();
                        UncompressedVideoPayload::Ptr p = std::move(_outQueue.front());
                        _outQueue.pop_front();
                        return p;
                }

                Error flush() {
                        if (_parser == nullptr) return Error::Ok;
                        CudaCtxGuard          guard(_cudaCtx);
                        CUVIDSOURCEDATAPACKET eosPkt{};
                        eosPkt.flags = CUVID_PKT_ENDOFSTREAM;
                        CUresult r = gCuvid.ParseVideoData(_parser, &eosPkt);
                        return mapCu(r, "cuvidParseVideoData(EOS)");
                }

                Error reset() {
                        destroySession();
                        return Error::Ok;
                }

                PixelFormat outputPixelFormat() const { return PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709); }

        private:
                Codec _codec;

                CUdevice  _device = 0;
                CUcontext _cudaCtx = nullptr;
                bool      _ctxRetained = false;

                CUvideoparser  _parser = nullptr;
                CUvideodecoder _decoder = nullptr;

                unsigned int _codedWidth = 0;
                unsigned int _codedHeight = 0;
                unsigned int _displayW = 0;
                unsigned int _displayH = 0;

                std::deque<UncompressedVideoPayload::Ptr> _outQueue;

                // Per-packet metadata FIFO.  submitPayload pushes one
                // entry per incoming CompressedVideoPayload; handleDisplay
                // pops one entry per emitted UncompressedVideoPayload.
                // Together they carry encoder-side per-image state
                // (Timecode, MediaTimeStamp) across the codec boundary.
                std::deque<Metadata> _packetMetaQueue;

                // Bitstream-parsed sequence metadata.  Filled in by
                // handleSequence from CUVIDEOFORMAT::video_signal_description
                // and applied to every output Image unless the caller
                // overrode the field via MediaConfig.  Values are the
                // raw H.273 numeric codepoints (0..255).
                ColorPrimaries          _bitstreamPrimaries{ColorPrimaries::Unspecified};
                TransferCharacteristics _bitstreamTransfer{TransferCharacteristics::Unspecified};
                MatrixCoefficients      _bitstreamMatrix{MatrixCoefficients::Unspecified};
                VideoRange              _bitstreamRange{VideoRange::Unknown};

                // Caller-supplied overrides from MediaConfig.  When set
                // to a non-Auto / non-Unknown value these supersede the
                // bitstream-parsed counterpart on the output Metadata.
                ColorPrimaries          _overridePrimaries{ColorPrimaries::Auto};
                TransferCharacteristics _overrideTransfer{TransferCharacteristics::Auto};
                MatrixCoefficients      _overrideMatrix{MatrixCoefficients::Auto};
                VideoRange              _overrideRange{VideoRange::Unknown};

                // Pending per-picture SEI — populated by handleSEI as the
                // parser feeds us messages for the next picture, then
                // drained in handleDisplay onto the matching output Image.
                // The parser's contract is that pfnGetSEIMsg fires before
                // pfnDisplayPicture for the same picIdx.
                std::deque<Metadata> _pendingSeiMeta;

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
                static int CUDAAPI onSEI(void *user, CUVIDSEIMESSAGEINFO *info) {
                        return static_cast<Impl *>(user)->handleSEI(info);
                }

                // ---- Session lifecycle --------------------------------
                Error ensureSession(Codec codec) {
                        if (_parser != nullptr) return Error::Ok;
                        if (!loadCuvid()) {
                                return Error::LibraryFailure;
                        }
                        if (Error err = retainCudaContext(); err.isError()) return err;

                        CUVIDPARSERPARAMS pp{};
                        pp.CodecType = (codec == Codec_H264) ? cudaVideoCodec_H264 : cudaVideoCodec_HEVC;
                        pp.ulMaxNumDecodeSurfaces = 20;
                        pp.ulClockRate = 10000000;
                        pp.ulMaxDisplayDelay = 0; // lowest latency
                        pp.pUserData = this;
                        pp.pfnSequenceCallback = &Impl::onSequence;
                        pp.pfnDecodePicture = &Impl::onDecode;
                        pp.pfnDisplayPicture = &Impl::onDisplay;
                        pp.pfnGetSEIMsg = &Impl::onSEI;

                        CUresult r = gCuvid.CreateVideoParser(&_parser, &pp);
                        if (r != CUDA_SUCCESS) {
                                return mapCu(r, "cuvidCreateVideoParser");
                        }
                        return Error::Ok;
                }

                Error retainCudaContext() {
                        if (_ctxRetained) return Error::Ok;
                        if (Error err = CudaBootstrap::ensureRegistered(); err.isError()) return err;
                        if (Error err = CudaDevice::setCurrent(0); err.isError()) return err;

                        CUresult r = cuInit(0);
                        if (r != CUDA_SUCCESS) return mapCu(r, "cuInit");
                        r = cuDeviceGet(&_device, 0);
                        if (r != CUDA_SUCCESS) return mapCu(r, "cuDeviceGet");
                        r = cuDevicePrimaryCtxRetain(&_cudaCtx, _device);
                        if (r != CUDA_SUCCESS) return mapCu(r, "cuDevicePrimaryCtxRetain");
                        if (_cudaCtx == nullptr) {
                                promekiErr("NVDEC: cuDevicePrimaryCtxRetain returned null ctx");
                                return Error::LibraryFailure;
                        }
                        // Don't push the context here — the worker may
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
                                if (_decoder) {
                                        gCuvid.DestroyDecoder(_decoder);
                                        _decoder = nullptr;
                                }
                                if (_parser) {
                                        gCuvid.DestroyVideoParser(_parser);
                                        _parser = nullptr;
                                }
                        }
                        _outQueue.clear();
                        _packetMetaQueue.clear();
                        if (_ctxRetained) {
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
                        if (_decoder) {
                                gCuvid.DestroyDecoder(_decoder);
                                _decoder = nullptr;
                        }

                        _codedWidth = fmt->coded_width;
                        _codedHeight = fmt->coded_height;

                        // CUVIDEOFORMAT::video_signal_description carries
                        // the parsed H.264/HEVC VUI fields (and AV1's
                        // sequence-header color description once CUVID
                        // supports it).  Cache them so handleDisplay
                        // can stamp them on every output Image.  Values
                        // are already the raw H.273 numeric codepoints.
                        const auto &vsd = fmt->video_signal_description;
                        _bitstreamPrimaries = ColorPrimaries(vsd.color_primaries);
                        _bitstreamTransfer = TransferCharacteristics(vsd.transfer_characteristics);
                        _bitstreamMatrix = MatrixCoefficients(vsd.matrix_coefficients);
                        _bitstreamRange = vsd.video_full_range_flag ? VideoRange::Full : VideoRange::Limited;
                        // Some encoders (notably NVENC HEVC at small
                        // resolutions) don't populate display_area at
                        // all — if both sides are zero, treat the full
                        // coded frame as the display region.  Without
                        // this fallback, cuvidCreateDecoder rejects the
                        // request with CUDA_ERROR_INVALID_VALUE.
                        if (fmt->display_area.right <= fmt->display_area.left ||
                            fmt->display_area.bottom <= fmt->display_area.top) {
                                _displayW = fmt->coded_width;
                                _displayH = fmt->coded_height;
                        } else {
                                _displayW = fmt->display_area.right - fmt->display_area.left;
                                _displayH = fmt->display_area.bottom - fmt->display_area.top;
                        }

                        CUVIDDECODECREATEINFO ci{};
                        ci.CodecType = fmt->codec;
                        ci.ChromaFormat = fmt->chroma_format;
                        ci.OutputFormat = cudaVideoSurfaceFormat_NV12;
                        ci.DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
                        ci.ulNumDecodeSurfaces = (fmt->min_num_decode_surfaces > 0) ? fmt->min_num_decode_surfaces : 8;
                        ci.ulNumOutputSurfaces = 2;
                        ci.ulCreationFlags = cudaVideoCreate_PreferCUVID;
                        ci.bitDepthMinus8 = fmt->bit_depth_luma_minus8;
                        ci.ulIntraDecodeOnly = 0;
                        ci.ulWidth = fmt->coded_width;
                        ci.ulHeight = fmt->coded_height;
                        ci.ulMaxWidth = fmt->coded_width;
                        ci.ulMaxHeight = fmt->coded_height;
                        ci.ulTargetWidth = _displayW;
                        ci.ulTargetHeight = _displayH;
                        // Leave display_area / target_rect at their
                        // default (zero) values — the driver then
                        // applies its own "whole frame" defaults, which
                        // avoids the HEVC-at-small-resolutions case
                        // where explicit zero-rects trigger INVALID_VALUE.

                        CUresult r = gCuvid.CreateDecoder(&_decoder, &ci);
                        if (r != CUDA_SUCCESS) {
                                mapCu(r, "cuvidCreateDecoder");
                                return 0; // parser treats 0 as "fail"
                        }

                        // Returning ulNumDecodeSurfaces here asks the
                        // parser to honour the same DPB size we just
                        // configured the decoder with.
                        return static_cast<int>(ci.ulNumDecodeSurfaces);
                }

                // Parser SEI callback.  Fires once per picture with
                // the concatenated list of SEI messages that were
                // present for that picture.  We extract timecode
                // (H.264 pic_timing / HEVC time_code), mastering
                // display colour volume, and content light level into a
                // per-picture Metadata that handleDisplay later merges
                // onto the output Image.
                int handleSEI(CUVIDSEIMESSAGEINFO *info) {
                        if (!info || info->sei_message_count == 0 || info->pSEIData == nullptr ||
                            info->pSEIMessage == nullptr) {
                                return 1;
                        }
                        const uint8_t *data = static_cast<const uint8_t *>(info->pSEIData);
                        size_t         offset = 0;
                        Metadata       sei;
                        for (unsigned int i = 0; i < info->sei_message_count; ++i) {
                                const CUSEIMESSAGE &m = info->pSEIMessage[i];
                                const size_t        sz = m.sei_message_size;
                                const uint8_t      *p = data + offset;
                                offset += sz;

                                switch (m.sei_message_type) {
                                        case 1: { // H.264 pic_timing
                                                Timecode tc = parseH264PicTiming(p, sz);
                                                if (tc.isValid()) sei.set(Metadata::Timecode, tc);
                                                break;
                                        }
                                        case 136: { // HEVC time_code
                                                Timecode tc = parseHevcTimeCode(p, sz);
                                                if (tc.isValid()) sei.set(Metadata::Timecode, tc);
                                                break;
                                        }
                                        case 137: { // mastering_display_colour_volume
                                                MasteringDisplay md;
                                                if (parseMasteringDisplaySei(p, sz, md)) {
                                                        sei.set(Metadata::MasteringDisplay, md);
                                                }
                                                break;
                                        }
                                        case 144: { // content_light_level_info
                                                ContentLightLevel cll;
                                                if (parseContentLightLevelSei(p, sz, cll)) {
                                                        sei.set(Metadata::ContentLightLevel, cll);
                                                }
                                                break;
                                        }
                                        default:
                                                // Other payload types (buffering_period,
                                                // user_data_unregistered, film_grain, …)
                                                // are out of scope for this pass.  They
                                                // stay on the bitstream but we don't
                                                // surface them in the decoded Image.
                                                break;
                                }
                        }
                        _pendingSeiMeta.push_back(std::move(sei));
                        return 1;
                }

                int handleDecode(CUVIDPICPARAMS *pic) {
                        if (_decoder == nullptr) return 0;
                        CUresult r = gCuvid.DecodePicture(_decoder, pic);
                        if (r != CUDA_SUCCESS) {
                                mapCu(r, "cuvidDecodePicture");
                                return 0;
                        }
                        return 1;
                }

                int handleDisplay(CUVIDPARSERDISPINFO *info) {
                        if (_decoder == nullptr) return 0;

                        CUVIDPROCPARAMS pp{};
                        pp.progressive_frame = info->progressive_frame;
                        pp.top_field_first = info->top_field_first;
                        pp.unpaired_field = (info->repeat_first_field < 0) ? 1 : 0;
                        pp.output_stream = nullptr; // default stream

                        unsigned long long devPtr = 0;
                        unsigned int       pitch = 0;
                        CUresult r = gCuvid.MapVideoFrame64(_decoder, info->picture_index, &devPtr, &pitch, &pp);
                        if (r != CUDA_SUCCESS) {
                                mapCu(r, "cuvidMapVideoFrame64");
                                return 0;
                        }

                        // Build a system-memory NV12 payload sized to
                        // the display rectangle, then cudaMemcpy2D
                        // luma + chroma planes down from device.
                        ImageDesc             desc(Size2Du32(_displayW, _displayH), outputPixelFormat());
                        auto                  img = UncompressedVideoPayload::allocate(desc);
                        const PixelMemLayout &outMl = desc.pixelFormat().memLayout();
                        bool                  copyOk = img.isValid() && img->planeCount() >= 2;
                        if (copyOk) {
                                UncompressedVideoPayload *imgRaw = img.modify();
                                void                     *yDst = imgRaw->data()[0].data();
                                void                     *uvDst = imgRaw->data()[1].data();
                                const size_t              yStride = outMl.lineStride(0, _displayW);
                                const size_t              uvStride = outMl.lineStride(1, _displayW);
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
                                const unsigned long long yDev = devPtr;
                                const unsigned long long uvDev =
                                        devPtr + static_cast<unsigned long long>(pitch) * _displayH;

                                cudaError_t ce = cudaMemcpy2D(yDst, yStride, reinterpret_cast<const void *>(yDev),
                                                              pitch, _displayW, _displayH, cudaMemcpyDeviceToHost);
                                if (ce != cudaSuccess) {
                                        promekiErr("NVDEC: cudaMemcpy2D(Y) failed: %s", cudaGetErrorString(ce));
                                        copyOk = false;
                                }
                                if (copyOk) {
                                        ce = cudaMemcpy2D(uvDst, uvStride, reinterpret_cast<const void *>(uvDev), pitch,
                                                          _displayW, _displayH / 2, cudaMemcpyDeviceToHost);
                                        if (ce != cudaSuccess) {
                                                promekiErr("NVDEC: cudaMemcpy2D(UV) failed: %s",
                                                           cudaGetErrorString(ce));
                                                copyOk = false;
                                        }
                                }
                        } else {
                                copyOk = false;
                        }

                        gCuvid.UnmapVideoFrame64(_decoder, devPtr);
                        if (!copyOk) return 0;

                        // Pair this display with the oldest queued
                        // packet metadata.  The FIFO may be empty if
                        // the caller flushed or if we hit an unexpected
                        // reorder — attach whatever's at the head and
                        // fall back to default-constructed Metadata
                        // otherwise so the Image still looks reasonable
                        // downstream.
                        if (!_packetMetaQueue.empty()) {
                                img.modify()->desc().metadata() = std::move(_packetMetaQueue.front());
                                _packetMetaQueue.pop_front();
                        }

                        // Merge bitstream-parsed per-picture SEI (the
                        // Timecode, MasteringDisplay, ContentLightLevel
                        // keys that handleSEI populated for this picture)
                        // into the output Image's metadata.  Explicit
                        // values from pfnGetSEIMsg override whatever the
                        // packet metadata carried — the bitstream is the
                        // authoritative source when it's present.
                        if (!_pendingSeiMeta.empty()) {
                                Metadata sei = std::move(_pendingSeiMeta.front());
                                _pendingSeiMeta.pop_front();
                                img.modify()->desc().metadata().merge(sei);
                        }

                        // Stamp the bitstream-parsed color description
                        // unless the caller supplied an explicit override
                        // via MediaConfig.  Both sides default to Auto /
                        // Unknown, so the common case just propagates
                        // whatever the sequence header said.
                        //
                        // The @c resolve helper picks override when it's
                        // a concrete value, otherwise falls back to
                        // bitstream.  @c -1 (InvalidValue) and @c 255
                        // (Auto) and @c 0 (Unknown / Unspecified for
                        // the color-description enums) all count as "no
                        // opinion, defer to the bitstream".
                        auto isSentinel = [](int v) {
                                return v == -1 || v == 0 || v == 255;
                        };
                        auto resolve = [&isSentinel](int override_, int bitstream) -> int {
                                return isSentinel(override_) ? bitstream : override_;
                        };
                        int v;
                        v = resolve(_overridePrimaries.value(), _bitstreamPrimaries.value());
                        if (!isSentinel(v)) {
                                // Construct the Enum via the (Type, int)
                                // ctor so the Variant stores a concrete
                                // Enum instance rather than slicing from
                                // a TypedEnum<> temporary — std::variant
                                // demands exact type match, and derived-
                                // to-base slicing through template
                                // arguments has produced default-valued
                                // Enum entries in practice.
                                img.modify()->desc().metadata().set(Metadata::VideoColorPrimaries,
                                                                    Enum(ColorPrimaries::Type, v));
                        }
                        v = resolve(_overrideTransfer.value(), _bitstreamTransfer.value());
                        if (!isSentinel(v)) {
                                img.modify()->desc().metadata().set(Metadata::VideoTransferCharacteristics,
                                                                    Enum(TransferCharacteristics::Type, v));
                        }
                        v = resolve(_overrideMatrix.value(), _bitstreamMatrix.value());
                        if (!isSentinel(v)) {
                                img.modify()->desc().metadata().set(Metadata::VideoMatrixCoefficients,
                                                                    Enum(MatrixCoefficients::Type, v));
                        }
                        // VideoRange uses 0=Unknown (not 255), so pass-through.
                        v = resolve(_overrideRange.value(), _bitstreamRange.value());
                        if (!isSentinel(v)) {
                                img.modify()->desc().metadata().set(Metadata::VideoRange, Enum(VideoRange::Type, v));
                        }

                        // Map NVDEC's per-picture progressive_frame /
                        // top_field_first bits onto @ref VideoScanMode and
                        // stamp the result on the decoded Image's
                        // metadata.  The parser exposes these off the
                        // display info rather than the sequence header
                        // because an interlaced stream can carry
                        // progressive pictures and vice versa — the
                        // per-picture Pic Timing SEI (H.264 pic_struct
                        // / HEVC pic_struct) is the authoritative signal.
                        //
                        // @c progressive_frame=1 → Progressive,
                        // @c progressive_frame=0 + top_first=1 → InterlacedEvenFirst,
                        // @c progressive_frame=0 + top_first=0 → InterlacedOddFirst.
                        VideoScanMode scan = VideoScanMode::Unknown;
                        if (info->progressive_frame) {
                                scan = VideoScanMode::Progressive;
                        } else {
                                scan = info->top_field_first ? VideoScanMode::InterlacedEvenFirst
                                                             : VideoScanMode::InterlacedOddFirst;
                        }
                        img.modify()->desc().metadata().set(Metadata::VideoScanMode,
                                                            Enum(VideoScanMode::Type, scan.value()));

                        _outQueue.push_back(std::move(img));
                        return 1;
                }
};

// ---------------------------------------------------------------------------
// Thin NvdecVideoDecoder façade that forwards to Impl.
// ---------------------------------------------------------------------------

NvdecVideoDecoder::NvdecVideoDecoder(Codec codec) : _impl(ImplPtr::create(codec)), _codec(codec) {}

NvdecVideoDecoder::~NvdecVideoDecoder() = default;

List<int> NvdecVideoDecoder::supportedOutputList() {
        return {static_cast<int>(PixelFormat::YUV8_420_SemiPlanar_Rec709)};
}

void NvdecVideoDecoder::configure(const MediaConfig &config) {
        _impl->configure(config);
}

Error NvdecVideoDecoder::submitPayload(const CompressedVideoPayload::Ptr &payload) {
        clearError();
        if (!payload.isValid()) {
                _lastError = Error::Invalid;
                _lastErrorMessage = "NVDEC: null payload Ptr";
                return _lastError;
        }
        Error err = _impl->submitPacket(*payload, _codec);
        if (err.isError()) {
                _lastError = err;
                _lastErrorMessage = String("NVDEC submitPacket failed");
        }
        return err;
}

UncompressedVideoPayload::Ptr NvdecVideoDecoder::receiveVideoPayload() {
        return _impl->receiveFrame();
}

Error NvdecVideoDecoder::flush() {
        clearError();
        Error err = _impl->flush();
        if (err.isError()) {
                _lastError = err;
                _lastErrorMessage = String("NVDEC flush failed");
        }
        return err;
}

Error NvdecVideoDecoder::reset() {
        clearError();
        return _impl->reset();
}

// ---------------------------------------------------------------------------
// Backend registration — typed (codec, backend) pair on the
// VideoDecoder registry.  Registered under the "Nvidia" backend name
// for H264 / HEVC.
// ---------------------------------------------------------------------------

namespace {

        struct NvdecRegistrar {
                        NvdecRegistrar() {
                                auto bk = VideoCodec::registerBackend("Nvidia");
                                if (error(bk).isError()) return;
                                const VideoCodec::Backend backend = value(bk);

                                const List<int> nvdecOutputs = NvdecVideoDecoder::supportedOutputList();

                                VideoDecoder::registerBackend({
                                        .codecId = VideoCodec::H264,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedOutputs = nvdecOutputs,
                                        .factory = []() -> VideoDecoder * {
                                                return new NvdecVideoDecoder(NvdecVideoDecoder::Codec_H264);
                                        },
                                });
                                VideoDecoder::registerBackend({
                                        .codecId = VideoCodec::HEVC,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedOutputs = nvdecOutputs,
                                        .factory = []() -> VideoDecoder * {
                                                return new NvdecVideoDecoder(NvdecVideoDecoder::Codec_HEVC);
                                        },
                                });
                        }
        };

        static NvdecRegistrar _nvdecRegistrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NVDEC
