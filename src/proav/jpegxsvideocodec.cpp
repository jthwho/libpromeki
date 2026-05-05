/**
 * @file      jpegxsvideocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <cstdlib>
#include <cstring>
#include <vector>

#include <promeki/jpegxsvideocodec.h>
#include <promeki/mediaconfig.h>
#include <promeki/buffer.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/logger.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>

#include <SvtJpegxs.h>
#include <SvtJpegxsEnc.h>
#include <SvtJpegxsDec.h>

PROMEKI_NAMESPACE_BEGIN

// ===========================================================================
// SVT-JPEG-XS plumbing — folded in from the retired JpegXsImageCodec.
// The encoder / decoder session classes below are the only owners of
// the SVT state now.
// ===========================================================================
//
// Maps every accepted uncompressed PixelFormat to the triple
// (bit_depth, colour_format, compressed tag) the encode/decode paths
// need.  Anything not in the table is an unsupported format and the
// respective path bails out early.
//
// All inputs are planar at the SVT API level.  Interleaved RGB
// reaches the codec via a CSC fast path (RGB8_sRGB →
// RGB8_Planar_sRGB) before encode, and the reverse path on decode.
//
// FIXME: SVT-JPEG-XS supports COLOUR_FORMAT_PACKED_YUV444_OR_RGB
// for direct interleaved RGB encode (with internal AVX2/512
// deinterleave), but svt_jpeg_xs_encoder_send_picture() has a
// validation bug: it checks stride[c]/alloc_size[c] for all 3
// logical components even though the packed buffer only fills
// component 0.  Once fixed upstream, add RGB8_sRGB directly to
// classifyInput() to skip the CSC deinterleave on encode.
// See devplan/fixme.md for details.

namespace {

        struct JpegXsLayout {
                        int             bitDepth;     // 8, 10, 12
                        ColourFormat_t  colourFormat; // PLANAR_YUV420 / PLANAR_YUV422 / PLANAR_YUV444_OR_RGB
                        PixelFormat::ID compressed;   // Output tag for the compressed Image
                        bool            is420;
                        bool            valid = false;
        };

        static JpegXsLayout classifyInput(PixelFormat::ID id) {
                JpegXsLayout l;
                switch (id) {
                        case PixelFormat::YUV8_422_Planar_Rec709:
                                l = {8, COLOUR_FORMAT_PLANAR_YUV422, PixelFormat::JPEG_XS_YUV8_422_Rec709, false, true};
                                break;
                        case PixelFormat::YUV10_422_Planar_LE_Rec709:
                                l = {10, COLOUR_FORMAT_PLANAR_YUV422, PixelFormat::JPEG_XS_YUV10_422_Rec709, false,
                                     true};
                                break;
                        case PixelFormat::YUV12_422_Planar_LE_Rec709:
                                l = {12, COLOUR_FORMAT_PLANAR_YUV422, PixelFormat::JPEG_XS_YUV12_422_Rec709, false,
                                     true};
                                break;
                        case PixelFormat::YUV8_420_Planar_Rec709:
                                l = {8, COLOUR_FORMAT_PLANAR_YUV420, PixelFormat::JPEG_XS_YUV8_420_Rec709, true, true};
                                break;
                        case PixelFormat::YUV10_420_Planar_LE_Rec709:
                                l = {10, COLOUR_FORMAT_PLANAR_YUV420, PixelFormat::JPEG_XS_YUV10_420_Rec709, true,
                                     true};
                                break;
                        case PixelFormat::YUV12_420_Planar_LE_Rec709:
                                l = {12, COLOUR_FORMAT_PLANAR_YUV420, PixelFormat::JPEG_XS_YUV12_420_Rec709, true,
                                     true};
                                break;
                        case PixelFormat::RGB8_Planar_sRGB:
                                l = {8, COLOUR_FORMAT_PLANAR_YUV444_OR_RGB, PixelFormat::JPEG_XS_RGB8_sRGB, false,
                                     true};
                                break;
                        default: break;
                }
                return l;
        }

        // Maps a compressed (JPEG_XS_*) PixelFormat to the uncompressed planar
        // layout the decoder should produce.  Used when decode() is called
        // without an explicit output format or when the caller asks for the
        // "natural" target (matching bit depth and subsampling).
        static PixelFormat::ID defaultDecodeTarget(PixelFormat::ID id) {
                switch (id) {
                        case PixelFormat::JPEG_XS_YUV8_422_Rec709: return PixelFormat::YUV8_422_Planar_Rec709;
                        case PixelFormat::JPEG_XS_YUV10_422_Rec709: return PixelFormat::YUV10_422_Planar_LE_Rec709;
                        case PixelFormat::JPEG_XS_YUV12_422_Rec709: return PixelFormat::YUV12_422_Planar_LE_Rec709;
                        case PixelFormat::JPEG_XS_YUV8_420_Rec709: return PixelFormat::YUV8_420_Planar_Rec709;
                        case PixelFormat::JPEG_XS_YUV10_420_Rec709: return PixelFormat::YUV10_420_Planar_LE_Rec709;
                        case PixelFormat::JPEG_XS_YUV12_420_Rec709: return PixelFormat::YUV12_420_Planar_LE_Rec709;
                        case PixelFormat::JPEG_XS_RGB8_sRGB: return PixelFormat::RGB8_Planar_sRGB;
                        default: return PixelFormat::Invalid;
                }
        }

        // ---------------------------------------------------------------------------
        // Encoder Impl — persistent svt_jpeg_xs_encoder_api_t plus its derived
        // per-stream image config.  Re-init only when a new frame's parameters
        // (dimensions / bit depth / colour format / bpp / decomposition) differ
        // from the cached set; otherwise reuse the existing encoder state for
        // the next svt_jpeg_xs_encoder_send_picture call.
        // ---------------------------------------------------------------------------

        struct EncoderParams {
                        uint32_t       width = 0;
                        uint32_t       height = 0;
                        int            bitDepth = 0;
                        ColourFormat_t colourFormat = COLOUR_FORMAT_INVALID;
                        int            bpp = 0;
                        int            decomposition = 0;

                        bool operator==(const EncoderParams &o) const {
                                return width == o.width && height == o.height && bitDepth == o.bitDepth &&
                                       colourFormat == o.colourFormat && bpp == o.bpp &&
                                       decomposition == o.decomposition;
                        }
        };

} // namespace

struct JpegXsVideoEncoder::Impl {
                svt_jpeg_xs_encoder_api_t  enc{};
                svt_jpeg_xs_image_config_t imgCfg{};
                uint32_t                   bytesPerFrame = 0;
                EncoderParams              params;
                bool                       initialized = false;

                ~Impl() { closeIfOpen(); }

                void closeIfOpen() {
                        if (initialized) {
                                svt_jpeg_xs_encoder_close(&enc);
                                initialized = false;
                        }
                }

                // Encodes a single frame through the persistent encoder context.
                // Lazily re-initializes the encoder when input parameters change.
                CompressedVideoPayload::Ptr encodeFrame(const UncompressedVideoPayload &input, int bpp,
                                                        int decomposition, Error &errOut, String &errMsg);

                // Lazily (re)initializes the encoder when @p p differs from the
                // currently-cached params.  Returns Error::Ok on success.
                Error ensure(const EncoderParams &p, String &errMsg) {
                        if (initialized && params == p) return Error::Ok;
                        closeIfOpen();

                        SvtJxsErrorType_t err = svt_jpeg_xs_encoder_load_default_parameters(
                                SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
                        if (err != SvtJxsErrorNone) {
                                errMsg = "svt_jpeg_xs_encoder_load_default_parameters failed";
                                return Error::IOError;
                        }

                        enc.source_width = p.width;
                        enc.source_height = p.height;
                        enc.input_bit_depth = (uint8_t)p.bitDepth;
                        enc.colour_format = p.colourFormat;
                        enc.bpp_numerator = (uint32_t)p.bpp;
                        enc.bpp_denominator = 1;
                        enc.ndecomp_h = (uint32_t)p.decomposition;
                        enc.verbose = VERBOSE_NONE;

                        err = svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                                                                   &enc, &imgCfg, &bytesPerFrame);
                        if (err != SvtJxsErrorNone || bytesPerFrame == 0) {
                                errMsg = "svt_jpeg_xs_encoder_get_image_config failed";
                                return Error::IOError;
                        }

                        err = svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
                        if (err != SvtJxsErrorNone) {
                                errMsg = "svt_jpeg_xs_encoder_init failed";
                                return Error::IOError;
                        }

                        params = p;
                        initialized = true;
                        return Error::Ok;
                }
};

// ---------------------------------------------------------------------------
// Encode — one frame through the persistent encoder context.
// ---------------------------------------------------------------------------

CompressedVideoPayload::Ptr JpegXsVideoEncoder::Impl::encodeFrame(const UncompressedVideoPayload &input, int bpp,
                                                                  int decomposition, Error &errOut, String &errMsg) {
        errOut = Error::Ok;
        errMsg.clear();
        const ImageDesc      &idesc = input.desc();
        const PixelMemLayout &ml = idesc.pixelFormat().memLayout();
        JpegXsLayout          layout = classifyInput(idesc.pixelFormat().id());
        if (!layout.valid) {
                errOut = Error::PixelFormatNotSupported;
                errMsg = "JPEG XS encoder accepts planar YUV 4:2:2/4:2:0 and packed RGB inputs";
                return CompressedVideoPayload::Ptr();
        }

        const uint32_t width = (uint32_t)idesc.size().width();
        const uint32_t height = (uint32_t)idesc.size().height();
        const uint32_t pixelBytes = (layout.bitDepth > 8) ? 2u : 1u;

        EncoderParams p;
        p.width = width;
        p.height = height;
        p.bitDepth = layout.bitDepth;
        p.colourFormat = layout.colourFormat;
        p.bpp = bpp;
        p.decomposition = decomposition;
        if (Error e = ensure(p, errMsg); e.isError()) {
                errOut = e;
                return CompressedVideoPayload::Ptr();
        }

        svt_jpeg_xs_image_buffer_t inBuf = {};
        for (int c = 0; c < 3; c++) {
                const uint32_t planeBytes = (uint32_t)ml.lineStride(c, width);
                if (planeBytes % pixelBytes != 0) {
                        errOut = Error::IOError;
                        errMsg = "plane stride is not a multiple of pixel size";
                        return CompressedVideoPayload::Ptr();
                }
                inBuf.stride[c] = planeBytes / pixelBytes;
                inBuf.alloc_size[c] = planeBytes * (uint32_t)imgCfg.components[c].height;
                inBuf.data_yuv[c] = const_cast<uint8_t *>(input.plane(c).data());
        }

        std::vector<uint8_t>           bitstreamStorage(bytesPerFrame);
        svt_jpeg_xs_bitstream_buffer_t outBuf = {};
        outBuf.buffer = bitstreamStorage.data();
        outBuf.allocation_size = bytesPerFrame;
        outBuf.used_size = 0;

        svt_jpeg_xs_frame_t frame = {};
        frame.image = inBuf;
        frame.bitstream = outBuf;

        SvtJxsErrorType_t err = svt_jpeg_xs_encoder_send_picture(&enc, &frame, /*blocking*/ 1);
        if (err != SvtJxsErrorNone) {
                errOut = Error::IOError;
                errMsg = "svt_jpeg_xs_encoder_send_picture failed";
                return CompressedVideoPayload::Ptr();
        }

        svt_jpeg_xs_frame_t outFrame = {};
        err = svt_jpeg_xs_encoder_get_packet(&enc, &outFrame, /*blocking*/ 1);
        if (err != SvtJxsErrorNone || outFrame.bitstream.used_size == 0) {
                errOut = Error::IOError;
                errMsg = "svt_jpeg_xs_encoder_get_packet failed";
                return CompressedVideoPayload::Ptr();
        }

        // Copy the encoded bitstream into an owned Buffer and wrap it
        // as a CompressedVideoPayload.
        ImageDesc cdesc(Size2Du32(width, height), PixelFormat(layout.compressed));
        cdesc.metadata() = idesc.metadata();
        Buffer buf = Buffer(outFrame.bitstream.used_size);
        std::memcpy(buf.data(), outFrame.bitstream.buffer, outFrame.bitstream.used_size);
        buf.setSize(outFrame.bitstream.used_size);
        BufferView view(buf, 0, outFrame.bitstream.used_size);
        auto       cvp = CompressedVideoPayload::Ptr::create(cdesc, view);
        cvp.modify()->metadata() = idesc.metadata();
        return cvp;
}

// ---------------------------------------------------------------------------
// Decoder Impl — persistent svt_jpeg_xs_decoder_api_t plus the cached
// image config.  Lazy first-frame init since SVT-JPEG-XS's decoder_init
// parses the bitstream's picture header as a side effect.  Subsequent
// frames reuse the existing decoder context; mid-stream parameter
// changes require a reset() to force re-init.
// ---------------------------------------------------------------------------

struct JpegXsVideoDecoder::Impl {
                svt_jpeg_xs_decoder_api_t  dec{};
                svt_jpeg_xs_image_config_t cfg{};
                bool                       initialized = false;

                ~Impl() { closeIfOpen(); }

                void closeIfOpen() {
                        if (initialized) {
                                svt_jpeg_xs_decoder_close(&dec);
                                initialized = false;
                        }
                }

                // Decodes a single frame through the persistent decoder context.
                // Lazily initializes from the first incoming bitstream.
                UncompressedVideoPayload::Ptr decodeFrame(const CompressedVideoPayload &input,
                                                          PixelFormat::ID outputFormat, Error &errOut, String &errMsg);

                Error ensure(const uint8_t *bitstream, size_t bitstreamSize, String &errMsg) {
                        if (initialized) return Error::Ok;
                        dec = {};
                        dec.use_cpu_flags = CPU_FLAGS_ALL;
                        dec.verbose = VERBOSE_NONE;
                        dec.threads_num = 0;
                        dec.packetization_mode = 0;
                        dec.proxy_mode = proxy_mode_full;
                        cfg = {};

                        SvtJxsErrorType_t err =
                                svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec,
                                                         bitstream, bitstreamSize, &cfg);
                        if (err != SvtJxsErrorNone) {
                                errMsg = "svt_jpeg_xs_decoder_init failed";
                                return Error::IOError;
                        }
                        initialized = true;
                        return Error::Ok;
                }
};

// ---------------------------------------------------------------------------
// Decode — one frame through the persistent decoder context.
// ---------------------------------------------------------------------------

UncompressedVideoPayload::Ptr JpegXsVideoDecoder::Impl::decodeFrame(const CompressedVideoPayload &input,
                                                                    PixelFormat::ID outputFormat, Error &errOut,
                                                                    String &errMsg) {
        errOut = Error::Ok;
        errMsg.clear();

        auto           bsView = input.plane(0);
        const uint8_t *bsData = bsView.data();
        const size_t   bsSize = bsView.size();

        if (Error e = ensure(bsData, bsSize, errMsg); e.isError()) {
                errOut = e;
                return UncompressedVideoPayload::Ptr();
        }
        const svt_jpeg_xs_image_config_t &cfg = this->cfg;

        PixelFormat::ID outPd = (outputFormat != PixelFormat::Invalid)
                                        ? outputFormat
                                        : defaultDecodeTarget(input.desc().pixelFormat().id());
        if (outPd == PixelFormat::Invalid) {
                errOut = Error::PixelFormatNotSupported;
                errMsg = "No decode target for this JPEG XS bitstream";
                return UncompressedVideoPayload::Ptr();
        }

        JpegXsLayout outLayout = classifyInput(outPd);
        if (!outLayout.valid || outLayout.bitDepth != cfg.bit_depth || outLayout.colourFormat != cfg.format) {
                errOut = Error::PixelFormatNotSupported;
                errMsg = "Requested decode target does not match JPEG XS bitstream layout";
                return UncompressedVideoPayload::Ptr();
        }

        ImageDesc outDesc((int)cfg.width, (int)cfg.height, PixelFormat(outPd));
        auto      output = UncompressedVideoPayload::allocate(outDesc);
        if (!output.isValid()) {
                errOut = Error::IOError;
                errMsg = "Failed to allocate decode output payload";
                return UncompressedVideoPayload::Ptr();
        }
        const PixelMemLayout &outMl = outDesc.pixelFormat().memLayout();

        // Allocate tightly packed temp buffers the decoder can fill
        // directly.  stride is width*pixel_size bytes per the API's
        // current "decoder ignores stride" contract.
        const uint32_t                    pixelBytes = (cfg.bit_depth > 8) ? 2u : 1u;
        std::vector<std::vector<uint8_t>> tmpPlanes(cfg.components_num);
        svt_jpeg_xs_image_buffer_t        imgBuf = {};
        for (int c = 0; c < cfg.components_num; c++) {
                tmpPlanes[c].resize(cfg.components[c].byte_size);
                imgBuf.data_yuv[c] = tmpPlanes[c].data();
                imgBuf.stride[c] = cfg.components[c].width;
                imgBuf.alloc_size[c] = cfg.components[c].byte_size;
        }

        svt_jpeg_xs_bitstream_buffer_t bs = {};
        bs.buffer = const_cast<uint8_t *>(bsData);
        bs.allocation_size = bsSize;
        bs.used_size = bsSize;

        svt_jpeg_xs_frame_t frame = {};
        frame.image = imgBuf;
        frame.bitstream = bs;

        SvtJxsErrorType_t err = svt_jpeg_xs_decoder_send_frame(&dec, &frame, /*blocking*/ 1);
        if (err != SvtJxsErrorNone) {
                errOut = Error::IOError;
                errMsg = "svt_jpeg_xs_decoder_send_frame failed";
                return UncompressedVideoPayload::Ptr();
        }

        svt_jpeg_xs_frame_t outFrame = {};
        err = svt_jpeg_xs_decoder_get_frame(&dec, &outFrame, /*blocking*/ 1);
        if (err != SvtJxsErrorNone) {
                errOut = Error::IOError;
                errMsg = "svt_jpeg_xs_decoder_get_frame failed";
                return UncompressedVideoPayload::Ptr();
        }

        // Copy each tightly packed plane into the corresponding output
        // payload plane.  The output's row stride may be larger than
        // the plane width (alignment padding), so we can't do a
        // single memcpy per plane.
        UncompressedVideoPayload *outRaw = output.modify();
        for (int c = 0; c < cfg.components_num; c++) {
                const uint32_t planeWidthBytes = cfg.components[c].width * pixelBytes;
                const uint32_t planeHeight = cfg.components[c].height;
                const uint8_t *src = tmpPlanes[c].data();
                uint8_t       *dst = outRaw->data()[c].data();
                const size_t   dstStride = outMl.lineStride(c, outDesc.size().width());
                for (uint32_t y = 0; y < planeHeight; y++) {
                        std::memcpy(dst + y * dstStride, src + y * planeWidthBytes, planeWidthBytes);
                }
        }

        outRaw->desc().metadata() = input.metadata();
        return output;
}

// ---------------------------------------------------------------------------
// JpegXsVideoEncoder
// ---------------------------------------------------------------------------

JpegXsVideoEncoder::JpegXsVideoEncoder() : _impl(ImplPtr::create()) {}

JpegXsVideoEncoder::~JpegXsVideoEncoder() = default;

List<int> JpegXsVideoEncoder::supportedInputList() {
        return {
                static_cast<int>(PixelFormat::YUV8_422_Rec709),
                static_cast<int>(PixelFormat::YUV10_422_Rec709),
                static_cast<int>(PixelFormat::YUV12_422_UYVY_LE_Rec709),
                static_cast<int>(PixelFormat::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV10_420_Planar_LE_Rec709),
                static_cast<int>(PixelFormat::YUV12_420_Planar_LE_Rec709),
                static_cast<int>(PixelFormat::RGB8_sRGB),
        };
}

void JpegXsVideoEncoder::configure(const MediaConfig &config) {
        // JpegXsBpp: anything <= 0 falls back to the default.
        if (config.contains(MediaConfig::JpegXsBpp)) {
                Variant v = config.get(MediaConfig::JpegXsBpp);
                if (v.isValid()) {
                        int bpp = v.get<int>();
                        _bpp = (bpp > 0) ? bpp : DefaultBpp;
                }
        }
        // JpegXsDecomposition: SVT-JPEG-XS accepts 0-5; clamp.
        if (config.contains(MediaConfig::JpegXsDecomposition)) {
                Variant v = config.get(MediaConfig::JpegXsDecomposition);
                if (v.isValid()) {
                        int depth = v.get<int>();
                        if (depth < 0) depth = 0;
                        if (depth > 5) depth = 5;
                        _decomposition = depth;
                }
        }
        _outputPd = config.getAs<PixelFormat>(MediaConfig::OutputPixelFormat, PixelFormat());
        _capacity = config.getAs<int>(MediaConfig::Capacity, 8);
        if (_capacity < 1) _capacity = 1;
}

Error JpegXsVideoEncoder::submitPayload(const UncompressedVideoPayload::Ptr &payload) {
        clearError();
        if (!payload.isValid() || !payload->isValid()) {
                setError(Error::Invalid, "JpegXsVideoEncoder: invalid payload");
                return _lastError;
        }
        if (static_cast<int>(_queue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("JpegXsVideoEncoder: output queue exceeded capacity (%d)", _capacity);
                _capacityWarned = true;
        }

        Error  codecErr;
        String codecMsg;
        auto   cvp = _impl->encodeFrame(*payload, _bpp, _decomposition, codecErr, codecMsg);
        if (!cvp.isValid()) {
                setError(codecErr.isError() ? codecErr : Error::ConversionFailed,
                         codecMsg.isEmpty() ? String("JpegXsVideoEncoder: encode failed") : codecMsg);
                return _lastError;
        }

        auto *raw = cvp.modify();
        raw->setPts(payload->pts());
        raw->setDts(payload->pts());
        raw->addFlag(MediaPayload::Keyframe);
        _queue.pushToBack(std::move(cvp));
        return Error::Ok;
}

CompressedVideoPayload::Ptr JpegXsVideoEncoder::receiveCompressedPayload() {
        if (_queue.isEmpty()) return CompressedVideoPayload::Ptr();
        return _queue.popFromFront();
}

Error JpegXsVideoEncoder::flush() {
        return Error::Ok;
}

Error JpegXsVideoEncoder::reset() {
        _queue.clear();
        _capacityWarned = false;
        // Drop the persistent encoder context so the next submitFrame
        // re-initializes from scratch with the next input's parameters.
        _impl->closeIfOpen();
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// JpegXsVideoDecoder
// ---------------------------------------------------------------------------

JpegXsVideoDecoder::JpegXsVideoDecoder() : _impl(ImplPtr::create()) {}

JpegXsVideoDecoder::~JpegXsVideoDecoder() = default;

List<int> JpegXsVideoDecoder::supportedOutputList() {
        // Mirror the decodeTargets actually populated on each JPEG XS
        // compressed PixelFormat — the SVT-JPEG-XS decoder only emits
        // planar YUV at matching bit depth + subsampling (or planar
        // RGB for the JPEG_XS_RGB8_sRGB variant).
        return {
                static_cast<int>(PixelFormat::YUV8_422_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV10_422_Planar_LE_Rec709),
                static_cast<int>(PixelFormat::YUV12_422_Planar_LE_Rec709),
                static_cast<int>(PixelFormat::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV10_420_Planar_LE_Rec709),
                static_cast<int>(PixelFormat::YUV12_420_Planar_LE_Rec709),
                static_cast<int>(PixelFormat::RGB8_Planar_sRGB),
        };
}

void JpegXsVideoDecoder::configure(const MediaConfig &config) {
        _outputPd = config.getAs<PixelFormat>(MediaConfig::OutputPixelFormat, PixelFormat());
        _capacity = config.getAs<int>(MediaConfig::Capacity, 8);
        if (_capacity < 1) _capacity = 1;
}

Error JpegXsVideoDecoder::submitPayload(const CompressedVideoPayload::Ptr &payload) {
        clearError();
        if (!payload.isValid() || !payload->isValid() || payload->size() == 0) {
                setError(Error::Invalid, "JpegXsVideoDecoder: empty payload");
                return _lastError;
        }
        if (static_cast<int>(_queue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("JpegXsVideoDecoder: output queue exceeded capacity (%d)", _capacity);
                _capacityWarned = true;
        }

        Error  codecErr;
        String codecMsg;
        auto   uvp = _impl->decodeFrame(*payload, _outputPd.isValid() ? _outputPd.id() : PixelFormat::Invalid, codecErr,
                                        codecMsg);
        if (!uvp.isValid()) {
                setError(codecErr.isError() ? codecErr : Error::ConversionFailed,
                         codecMsg.isEmpty() ? String("JpegXsVideoDecoder: decode failed") : codecMsg);
                return _lastError;
        }
        uvp.modify()->setPts(payload->pts());
        _queue.pushToBack(std::move(uvp));
        return Error::Ok;
}

UncompressedVideoPayload::Ptr JpegXsVideoDecoder::receiveVideoPayload() {
        if (_queue.isEmpty()) return UncompressedVideoPayload::Ptr();
        return _queue.popFromFront();
}

Error JpegXsVideoDecoder::flush() {
        return Error::Ok;
}

Error JpegXsVideoDecoder::reset() {
        _queue.clear();
        _capacityWarned = false;
        // Drop the persistent decoder context so the next submitPacket
        // re-parses the bitstream header and rebuilds the SVT state.
        _impl->closeIfOpen();
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Backend registration — typed (codec, backend) pair on both the
// VideoEncoder and VideoDecoder registries.  Registered under the
// "Svt" backend name for VideoCodec::JPEG_XS.
// ---------------------------------------------------------------------------

namespace {

        struct JpegXsVideoCodecRegistrar {
                        JpegXsVideoCodecRegistrar() {
                                auto bk = VideoCodec::registerBackend("Svt");
                                if (error(bk).isError()) return;
                                const VideoCodec::Backend backend = value(bk);

                                VideoEncoder::registerBackend({
                                        .codecId = VideoCodec::JPEG_XS,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedInputs = JpegXsVideoEncoder::supportedInputList(),
                                        .factory = []() -> VideoEncoder * { return new JpegXsVideoEncoder(); },
                                });
                                VideoDecoder::registerBackend({
                                        .codecId = VideoCodec::JPEG_XS,
                                        .backend = backend,
                                        .weight = BackendWeight::Vendored,
                                        .supportedOutputs = JpegXsVideoDecoder::supportedOutputList(),
                                        .factory = []() -> VideoDecoder * { return new JpegXsVideoDecoder(); },
                                });
                        }
        };

        static JpegXsVideoCodecRegistrar _jpegXsVideoCodecRegistrar;

} // namespace

PROMEKI_NAMESPACE_END
