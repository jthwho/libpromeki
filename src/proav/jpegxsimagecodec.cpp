/**
 * @file      jpegxsimagecodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <vector>

#include <promeki/jpegxsimagecodec.h>
#include <promeki/image.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
#include <promeki/mediaconfig.h>

#include <SvtJpegxs.h>
#include <SvtJpegxsEnc.h>
#include <SvtJpegxsDec.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_IMAGE_CODEC(JpegXsImageCodec, "jpegxs")

// ---------------------------------------------------------------------------
// Pixel format classification
// ---------------------------------------------------------------------------
//
// JPEG XS is strictly planar at the API level (stride[] is in samples,
// plane pointers are data_yuv[0..N-1]) so the codec only accepts
// planar YUV inputs.  This table maps every accepted uncompressed
// PixelDesc to the triple (bit_depth, colour_format, compressed tag)
// the rest of the file needs.  Anything not in the table is an
// unsupported format and the encode/decode path bails out early.

struct JpegXsLayout {
        int                     bitDepth;     // 8, 10, 12
        ColourFormat_t          colourFormat; // PLANAR_YUV420 / PLANAR_YUV422
        PixelDesc::ID           compressed;   // Output tag for the compressed Image
        bool                    is420;
        bool                    valid = false;
};

static JpegXsLayout classifyInput(PixelDesc::ID id) {
        JpegXsLayout l;
        switch(id) {
                case PixelDesc::YUV8_422_Planar_Rec709:
                        l = { 8,  COLOUR_FORMAT_PLANAR_YUV422,
                              PixelDesc::JPEG_XS_YUV8_422_Rec709,  false, true };
                        break;
                case PixelDesc::YUV10_422_Planar_LE_Rec709:
                        l = { 10, COLOUR_FORMAT_PLANAR_YUV422,
                              PixelDesc::JPEG_XS_YUV10_422_Rec709, false, true };
                        break;
                case PixelDesc::YUV12_422_Planar_LE_Rec709:
                        l = { 12, COLOUR_FORMAT_PLANAR_YUV422,
                              PixelDesc::JPEG_XS_YUV12_422_Rec709, false, true };
                        break;
                case PixelDesc::YUV8_420_Planar_Rec709:
                        l = { 8,  COLOUR_FORMAT_PLANAR_YUV420,
                              PixelDesc::JPEG_XS_YUV8_420_Rec709,  true,  true };
                        break;
                case PixelDesc::YUV10_420_Planar_LE_Rec709:
                        l = { 10, COLOUR_FORMAT_PLANAR_YUV420,
                              PixelDesc::JPEG_XS_YUV10_420_Rec709, true,  true };
                        break;
                case PixelDesc::YUV12_420_Planar_LE_Rec709:
                        l = { 12, COLOUR_FORMAT_PLANAR_YUV420,
                              PixelDesc::JPEG_XS_YUV12_420_Rec709, true,  true };
                        break;
                default: break;
        }
        return l;
}

// Maps a compressed (JPEG_XS_*) PixelDesc to the uncompressed planar
// layout the decoder should produce.  Used when decode() is called
// without an explicit output format or when the caller asks for the
// "natural" target (matching bit depth and subsampling).
static PixelDesc::ID defaultDecodeTarget(PixelDesc::ID id) {
        switch(id) {
                case PixelDesc::JPEG_XS_YUV8_422_Rec709:  return PixelDesc::YUV8_422_Planar_Rec709;
                case PixelDesc::JPEG_XS_YUV10_422_Rec709: return PixelDesc::YUV10_422_Planar_LE_Rec709;
                case PixelDesc::JPEG_XS_YUV12_422_Rec709: return PixelDesc::YUV12_422_Planar_LE_Rec709;
                case PixelDesc::JPEG_XS_YUV8_420_Rec709:  return PixelDesc::YUV8_420_Planar_Rec709;
                case PixelDesc::JPEG_XS_YUV10_420_Rec709: return PixelDesc::YUV10_420_Planar_LE_Rec709;
                case PixelDesc::JPEG_XS_YUV12_420_Rec709: return PixelDesc::YUV12_420_Planar_LE_Rec709;
                default: return PixelDesc::Invalid;
        }
}

// ---------------------------------------------------------------------------
// Codec metadata
// ---------------------------------------------------------------------------

JpegXsImageCodec::~JpegXsImageCodec() = default;
String JpegXsImageCodec::name() const { return "jpegxs"; }
String JpegXsImageCodec::description() const { return "JPEG XS image codec (SVT-JPEG-XS)"; }
bool JpegXsImageCodec::canEncode() const { return true; }
bool JpegXsImageCodec::canDecode() const { return true; }

void JpegXsImageCodec::setBpp(int bpp) {
        _bpp = (bpp > 0) ? bpp : DefaultBpp;
}

void JpegXsImageCodec::setDecomposition(int depth) {
        if(depth < 0) depth = 0;
        if(depth > 5) depth = 5;
        _decomposition = depth;
}

void JpegXsImageCodec::configure(const MediaConfig &config) {
        // JpegXsBpp: accept int or float-ish values.  Anything <= 0
        // falls back to the default via setBpp.
        if(config.contains(MediaConfig::JpegXsBpp)) {
                Variant v = config.get(MediaConfig::JpegXsBpp);
                if(v.isValid()) setBpp(v.get<int>());
        }
        // JpegXsDecomposition: int only, clamped by setDecomposition.
        if(config.contains(MediaConfig::JpegXsDecomposition)) {
                Variant v = config.get(MediaConfig::JpegXsDecomposition);
                if(v.isValid()) setDecomposition(v.get<int>());
        }
}

// ---------------------------------------------------------------------------
// Encode
// ---------------------------------------------------------------------------
//
// SVT-JPEG-XS uses a producer/consumer queue internally but also
// accepts a purely synchronous single-frame mode: send a frame with
// blocking_flag=1, then get_packet with blocking_flag=1.  We use that
// mode here because the ImageCodec interface is synchronous — one
// Image in, one Image out.  The encoder handle is created and torn
// down per encode() call; SVT's initialisation is cheap relative to
// the encode itself and this keeps the class stateless w.r.t. frame
// size / format, so callers can mix different images through the
// same codec instance without a separate "reset" step.

Image JpegXsImageCodec::encode(const Image &input) {
        clearError();
        if(!input.isValid()) {
                setError(Error::Invalid, "Input image is not valid");
                return Image();
        }

        JpegXsLayout layout = classifyInput(input.pixelDesc().id());
        if(!layout.valid) {
                setError(Error::PixelFormatNotSupported,
                         "JPEG XS encoder only accepts planar YUV 4:2:2 / 4:2:0 inputs");
                return Image();
        }

        const uint32_t width  = (uint32_t)input.width();
        const uint32_t height = (uint32_t)input.height();
        const uint32_t pixelBytes = (layout.bitDepth > 8) ? 2u : 1u;

        svt_jpeg_xs_encoder_api_t enc;
        SvtJxsErrorType_t err = svt_jpeg_xs_encoder_load_default_parameters(
                SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
        if(err != SvtJxsErrorNone) {
                setError(Error::IOError, "svt_jpeg_xs_encoder_load_default_parameters failed");
                return Image();
        }

        enc.source_width      = width;
        enc.source_height     = height;
        enc.input_bit_depth   = (uint8_t)layout.bitDepth;
        enc.colour_format     = layout.colourFormat;
        enc.bpp_numerator     = (uint32_t)_bpp;
        enc.bpp_denominator   = 1;
        enc.ndecomp_h         = (uint32_t)_decomposition;
        enc.verbose           = VERBOSE_NONE;

        // Ask the library how many bytes the compressed bitstream can
        // reach at the configured bpp so we can pre-allocate exactly
        // that much.  This also returns the per-plane layout the
        // encoder expects to see in the input buffer, which we use to
        // cross-check against what the promeki Image is actually
        // laid out as.
        svt_jpeg_xs_image_config_t imgCfg;
        uint32_t bytesPerFrame = 0;
        err = svt_jpeg_xs_encoder_get_image_config(
                SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                &enc, &imgCfg, &bytesPerFrame);
        if(err != SvtJxsErrorNone || bytesPerFrame == 0) {
                setError(Error::IOError, "svt_jpeg_xs_encoder_get_image_config failed");
                return Image();
        }

        err = svt_jpeg_xs_encoder_init(
                SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc);
        if(err != SvtJxsErrorNone) {
                setError(Error::IOError, "svt_jpeg_xs_encoder_init failed");
                return Image();
        }

        // Build SVT image buffer.  stride[] is in SAMPLES (not bytes),
        // so for 10/12-bit we divide the promeki Image's byte stride
        // by pixel_size.  alloc_size is advisory — the encoder only
        // reads, but it does perform a sanity check that the buffer is
        // big enough for (stride * height * pixel_size).
        svt_jpeg_xs_image_buffer_t inBuf = {};
        for(int c = 0; c < 3; c++) {
                const uint32_t planeBytes = (uint32_t)input.lineStride(c);
                if(planeBytes % pixelBytes != 0) {
                        svt_jpeg_xs_encoder_close(&enc);
                        setError(Error::IOError, "plane stride is not a multiple of pixel size");
                        return Image();
                }
                inBuf.stride[c]     = planeBytes / pixelBytes;
                inBuf.alloc_size[c] = planeBytes * (uint32_t)imgCfg.components[c].height;
                inBuf.data_yuv[c]   = const_cast<void *>(input.data(c));
        }

        // Output bitstream buffer — allocate the worst-case frame size
        // the library told us about.  The encoder fills used_size with
        // the actual bitstream length.
        std::vector<uint8_t> bitstreamStorage(bytesPerFrame);
        svt_jpeg_xs_bitstream_buffer_t outBuf = {};
        outBuf.buffer          = bitstreamStorage.data();
        outBuf.allocation_size = bytesPerFrame;
        outBuf.used_size       = 0;

        svt_jpeg_xs_frame_t frame = {};
        frame.image     = inBuf;
        frame.bitstream = outBuf;

        err = svt_jpeg_xs_encoder_send_picture(&enc, &frame, /*blocking*/ 1);
        if(err != SvtJxsErrorNone) {
                svt_jpeg_xs_encoder_close(&enc);
                setError(Error::IOError, "svt_jpeg_xs_encoder_send_picture failed");
                return Image();
        }

        svt_jpeg_xs_frame_t outFrame = {};
        err = svt_jpeg_xs_encoder_get_packet(&enc, &outFrame, /*blocking*/ 1);
        if(err != SvtJxsErrorNone || outFrame.bitstream.used_size == 0) {
                svt_jpeg_xs_encoder_close(&enc);
                setError(Error::IOError, "svt_jpeg_xs_encoder_get_packet failed");
                return Image();
        }

        Image result = Image::fromCompressedData(
                outFrame.bitstream.buffer,
                outFrame.bitstream.used_size,
                (int)width, (int)height,
                layout.compressed, input.metadata());

        svt_jpeg_xs_encoder_close(&enc);
        return result;
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------
//
// SVT decoder API: svt_jpeg_xs_decoder_init parses the bitstream and
// fills a svt_jpeg_xs_image_config_t with the intrinsic frame
// geometry (width/height/bit_depth/format).  Per the API header,
// stride is "not supported in decoder yet" — the decoder assumes
// tightly-packed plane buffers of width*pixel_size.  We therefore
// allocate our own temp plane storage matching that expectation and
// then copy each row into the promeki Image we hand back, which
// may have its own padding.

Image JpegXsImageCodec::decode(const Image &input, int outputFormat) {
        clearError();
        if(!input.isValid() || !input.isCompressed()) {
                setError(Error::Invalid, "Input image is not valid or not compressed");
                return Image();
        }

        svt_jpeg_xs_decoder_api_t dec = {};
        dec.use_cpu_flags     = CPU_FLAGS_ALL;
        dec.verbose           = VERBOSE_NONE;
        dec.threads_num       = 0;
        dec.packetization_mode = 0;
        dec.proxy_mode        = proxy_mode_full;

        svt_jpeg_xs_image_config_t cfg = {};
        SvtJxsErrorType_t err = svt_jpeg_xs_decoder_init(
                SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR,
                &dec,
                static_cast<const uint8_t *>(input.data()),
                input.compressedSize(),
                &cfg);
        if(err != SvtJxsErrorNone) {
                setError(Error::IOError, "svt_jpeg_xs_decoder_init failed");
                return Image();
        }

        // Figure out what uncompressed PixelDesc the caller wants.
        // Default: the natural match for the incoming compressed tag.
        PixelDesc::ID outPd = (outputFormat != 0)
                ? static_cast<PixelDesc::ID>(outputFormat)
                : defaultDecodeTarget(input.pixelDesc().id());
        if(outPd == PixelDesc::Invalid) {
                svt_jpeg_xs_decoder_close(&dec);
                setError(Error::PixelFormatNotSupported,
                         "No decode target for this JPEG XS bitstream");
                return Image();
        }

        // Validate the requested output matches the bitstream geometry.
        // A caller asking for e.g. 4:2:0 when the stream is 4:2:2 would
        // get garbage, so reject the mismatch instead of silently
        // producing broken output.
        JpegXsLayout outLayout = classifyInput(outPd);
        if(!outLayout.valid ||
           outLayout.bitDepth != cfg.bit_depth ||
           outLayout.colourFormat != cfg.format) {
                svt_jpeg_xs_decoder_close(&dec);
                setError(Error::PixelFormatNotSupported,
                         "Requested decode target does not match JPEG XS bitstream layout");
                return Image();
        }

        Image output((int)cfg.width, (int)cfg.height, outPd);
        if(!output.isValid()) {
                svt_jpeg_xs_decoder_close(&dec);
                setError(Error::IOError, "Failed to allocate decode output Image");
                return Image();
        }

        // Allocate tightly packed temp buffers the decoder can fill
        // directly.  stride is width*pixel_size bytes per the API's
        // current "decoder ignores stride" contract.
        const uint32_t pixelBytes = (cfg.bit_depth > 8) ? 2u : 1u;
        std::vector<std::vector<uint8_t>> tmpPlanes(cfg.components_num);
        svt_jpeg_xs_image_buffer_t imgBuf = {};
        for(int c = 0; c < cfg.components_num; c++) {
                tmpPlanes[c].resize(cfg.components[c].byte_size);
                imgBuf.data_yuv[c]   = tmpPlanes[c].data();
                imgBuf.stride[c]     = cfg.components[c].width;
                imgBuf.alloc_size[c] = cfg.components[c].byte_size;
        }

        svt_jpeg_xs_bitstream_buffer_t bs = {};
        // send_frame takes a non-const buffer pointer; the SVT source
        // only reads from it but the prototype lacks const-correctness.
        bs.buffer          = const_cast<uint8_t *>(
                                static_cast<const uint8_t *>(input.data()));
        bs.allocation_size = input.compressedSize();
        bs.used_size       = input.compressedSize();

        svt_jpeg_xs_frame_t frame = {};
        frame.image     = imgBuf;
        frame.bitstream = bs;

        err = svt_jpeg_xs_decoder_send_frame(&dec, &frame, /*blocking*/ 1);
        if(err != SvtJxsErrorNone) {
                svt_jpeg_xs_decoder_close(&dec);
                setError(Error::IOError, "svt_jpeg_xs_decoder_send_frame failed");
                return Image();
        }

        svt_jpeg_xs_frame_t outFrame = {};
        err = svt_jpeg_xs_decoder_get_frame(&dec, &outFrame, /*blocking*/ 1);
        if(err != SvtJxsErrorNone) {
                svt_jpeg_xs_decoder_close(&dec);
                setError(Error::IOError, "svt_jpeg_xs_decoder_get_frame failed");
                return Image();
        }

        // Copy each tightly packed plane into the corresponding output
        // Image plane.  The output Image's row stride may be larger
        // than the plane width (alignment padding), so we can't do a
        // single memcpy per plane.
        for(int c = 0; c < cfg.components_num; c++) {
                const uint32_t planeWidthBytes = cfg.components[c].width * pixelBytes;
                const uint32_t planeHeight     = cfg.components[c].height;
                const uint8_t *src = tmpPlanes[c].data();
                uint8_t *dst = static_cast<uint8_t *>(output.data(c));
                const size_t dstStride = output.lineStride(c);
                for(uint32_t y = 0; y < planeHeight; y++) {
                        std::memcpy(dst + y * dstStride, src + y * planeWidthBytes, planeWidthBytes);
                }
        }

        output.metadata() = input.metadata();
        svt_jpeg_xs_decoder_close(&dec);
        return output;
}

PROMEKI_NAMESPACE_END
