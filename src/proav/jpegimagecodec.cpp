/**
 * @file      jpegimagecodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <cstring>
#include <promeki/jpegimagecodec.h>
#include <promeki/image.h>
#include <promeki/pixeldesc.h>
#include <promeki/buffer.h>
#include <promeki/mediaconfig.h>
#include <promeki/enum.h>
#include <promeki/enums.h>
#include <jpeglib.h>

PROMEKI_NAMESPACE_BEGIN

// Note: the legacy PROMEKI_REGISTER_IMAGE_CODEC string-keyed registry
// was retired in task 37 — JPEG codec discovery flows through
// JpegVideoEncoder / JpegVideoDecoder + the typed VideoCodec::JPEG
// factory hooks instead.

struct JpegErrorMgr {
        jpeg_error_mgr  pub;
        jmp_buf         jmpBuf;
};

static void jpegErrorExit(j_common_ptr cinfo) {
        JpegErrorMgr *mgr = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
        longjmp(mgr->jmpBuf, 1);
}

// ---------------------------------------------------------------------------
// Input format classification
// ---------------------------------------------------------------------------

enum YCbCrLayout {
        LayoutNone = 0,
        LayoutInterleavedUYVY,   // UYVY 8-bit interleaved 4:2:2
        LayoutInterleavedYUYV,   // YUYV 8-bit interleaved 4:2:2
        LayoutPlanar422,         // 3-plane 4:2:2
        LayoutPlanar420,         // 3-plane 4:2:0
        LayoutSemiPlanar420,     // NV12: Y + interleaved CbCr 4:2:0
};

struct YCbCrInfo {
        YCbCrLayout layout;
        bool        is420;       // true if 4:2:0, false if 4:2:2
};

static YCbCrInfo classifyYCbCr(PixelDesc::ID id) {
        switch(id) {
                // 4:2:2 interleaved YUYV (all matrix × range variants).
                case PixelDesc::YUV8_422_Rec709:
                case PixelDesc::YUV8_422_Rec709_Full:
                case PixelDesc::YUV8_422_Rec601:
                case PixelDesc::YUV8_422_Rec601_Full:
                        return { LayoutInterleavedYUYV, false };
                // 4:2:2 interleaved UYVY (Rec.601 / Rec.709 limited variants only).
                case PixelDesc::YUV8_422_UYVY_Rec709:
                case PixelDesc::YUV8_422_UYVY_Rec601:
                        return { LayoutInterleavedUYVY, false };
                // 4:2:2 planar (Rec.709 limited only; no other planar
                // variants exist as independent IDs).
                case PixelDesc::YUV8_422_Planar_Rec709:
                        return { LayoutPlanar422, false };
                // 4:2:0 planar (all matrix × range variants).
                case PixelDesc::YUV8_420_Planar_Rec709:
                case PixelDesc::YUV8_420_Planar_Rec709_Full:
                case PixelDesc::YUV8_420_Planar_Rec601:
                case PixelDesc::YUV8_420_Planar_Rec601_Full:
                        return { LayoutPlanar420, true };
                // 4:2:0 semi-planar (NV12, Rec.709 / Rec.601 limited only).
                case PixelDesc::YUV8_420_SemiPlanar_Rec709:
                case PixelDesc::YUV8_420_SemiPlanar_Rec601:
                        return { LayoutSemiPlanar420, true };
                default:
                        return { LayoutNone, false };
        }
}

// Maps an uncompressed input PixelDesc to the matching compressed
// JPEG output PixelDesc.  "Matching" means subsampling (4:2:2 vs
// 4:2:0), matrix (Rec.709 vs Rec.601), and range (limited vs full)
// all come from the same cell of the 2 × 2 × 2 grid.  When the
// input is full-range uncompressed YCbCr, the output tag is the
// full-range JPEG sub-format; when the input is limited-range
// uncompressed YCbCr, the output is the limited-range sub-format,
// etc.  Keeps the encoder honest about what's actually inside the
// resulting JFIF bitstream.
static PixelDesc::ID jpegPixelDescFor(PixelDesc::ID srcDesc) {
        switch(srcDesc) {
                case PixelDesc::RGB8_sRGB:   return PixelDesc::JPEG_RGB8_sRGB;
                case PixelDesc::RGBA8_sRGB:  return PixelDesc::JPEG_RGBA8_sRGB;

                // 4:2:2 Rec.709 limited — legacy default.
                case PixelDesc::YUV8_422_Rec709:
                case PixelDesc::YUV8_422_UYVY_Rec709:
                case PixelDesc::YUV8_422_Planar_Rec709:
                        return PixelDesc::JPEG_YUV8_422_Rec709;

                // 4:2:0 Rec.709 limited.
                case PixelDesc::YUV8_420_Planar_Rec709:
                case PixelDesc::YUV8_420_SemiPlanar_Rec709:
                        return PixelDesc::JPEG_YUV8_420_Rec709;

                // 4:2:2 Rec.601 limited.
                case PixelDesc::YUV8_422_Rec601:
                case PixelDesc::YUV8_422_UYVY_Rec601:
                        return PixelDesc::JPEG_YUV8_422_Rec601;

                // 4:2:0 Rec.601 limited.
                case PixelDesc::YUV8_420_Planar_Rec601:
                case PixelDesc::YUV8_420_SemiPlanar_Rec601:
                        return PixelDesc::JPEG_YUV8_420_Rec601;

                // 4:2:2 Rec.709 full.
                case PixelDesc::YUV8_422_Rec709_Full:
                        return PixelDesc::JPEG_YUV8_422_Rec709_Full;

                // 4:2:0 Rec.709 full.
                case PixelDesc::YUV8_420_Planar_Rec709_Full:
                        return PixelDesc::JPEG_YUV8_420_Rec709_Full;

                // 4:2:2 Rec.601 full (strict JFIF).
                case PixelDesc::YUV8_422_Rec601_Full:
                        return PixelDesc::JPEG_YUV8_422_Rec601_Full;

                // 4:2:0 Rec.601 full (strict JFIF).
                case PixelDesc::YUV8_420_Planar_Rec601_Full:
                        return PixelDesc::JPEG_YUV8_420_Rec601_Full;

                default: return PixelDesc::JPEG_RGB8_sRGB;
        }
}

// ---------------------------------------------------------------------------
// Interleave / deinterleave helpers
// ---------------------------------------------------------------------------

static void deinterleaveUYVY(const uint8_t *src, uint8_t *y, uint8_t *cb, uint8_t *cr, size_t width) {
        size_t n = width / 2;
        for(size_t i = 0; i < n; i++) {
                cb[i]        = src[4*i + 0];
                y[2*i]       = src[4*i + 1];
                cr[i]        = src[4*i + 2];
                y[2*i + 1]   = src[4*i + 3];
        }
}

static void deinterleaveYUYV(const uint8_t *src, uint8_t *y, uint8_t *cb, uint8_t *cr, size_t width) {
        size_t n = width / 2;
        for(size_t i = 0; i < n; i++) {
                y[2*i]       = src[4*i + 0];
                cb[i]        = src[4*i + 1];
                y[2*i + 1]   = src[4*i + 2];
                cr[i]        = src[4*i + 3];
        }
}

static void deinterleaveNV12(const uint8_t *src, uint8_t *cb, uint8_t *cr, size_t chromaWidth) {
        for(size_t i = 0; i < chromaWidth; i++) {
                cb[i] = src[2*i + 0];
                cr[i] = src[2*i + 1];
        }
}

static void interleaveUYVY(uint8_t *dst, const uint8_t *y, const uint8_t *cb, const uint8_t *cr, size_t width) {
        size_t n = width / 2;
        for(size_t i = 0; i < n; i++) {
                dst[4*i + 0] = cb[i];
                dst[4*i + 1] = y[2*i];
                dst[4*i + 2] = cr[i];
                dst[4*i + 3] = y[2*i + 1];
        }
}

static void interleaveYUYV(uint8_t *dst, const uint8_t *y, const uint8_t *cb, const uint8_t *cr, size_t width) {
        size_t n = width / 2;
        for(size_t i = 0; i < n; i++) {
                dst[4*i + 0] = y[2*i];
                dst[4*i + 1] = cb[i];
                dst[4*i + 2] = y[2*i + 1];
                dst[4*i + 3] = cr[i];
        }
}

static void interleaveNV12(uint8_t *dst, const uint8_t *cb, const uint8_t *cr, size_t chromaWidth) {
        for(size_t i = 0; i < chromaWidth; i++) {
                dst[2*i + 0] = cb[i];
                dst[2*i + 1] = cr[i];
        }
}

// ---------------------------------------------------------------------------
// Codec methods
// ---------------------------------------------------------------------------

JpegImageCodec::~JpegImageCodec() = default;
String JpegImageCodec::name() const { return "JPEG"; }
String JpegImageCodec::description() const { return "JPEG image codec (libjpeg-turbo)"; }
bool JpegImageCodec::canEncode() const { return true; }
bool JpegImageCodec::canDecode() const { return true; }

void JpegImageCodec::configure(const MediaConfig &config) {
        // JpegQuality: clamped by setQuality to the 1-100 range, so we
        // don't need to validate the integer ourselves.
        if(config.contains(MediaConfig::JpegQuality)) {
                Variant v = config.get(MediaConfig::JpegQuality);
                if(v.isValid()) setQuality(v.get<int>());
        }
        // JpegSubsampling: accept any form Variant::asEnum can resolve
        // (Enum, registered name string, or integer ordinal) so the
        // same MediaConfig works whether it was authored programmatically
        // or parsed from text.
        if(config.contains(MediaConfig::JpegSubsampling)) {
                Variant v = config.get(MediaConfig::JpegSubsampling);
                Error subErr;
                Enum e = v.asEnum(ChromaSubsampling::Type, &subErr);
                if(!subErr.isError() && e.hasListedValue()) {
                        setSubsampling(static_cast<Subsampling>(e.value()));
                }
        }
}

void JpegImageCodec::setQuality(int quality) {
        if(quality < 1) quality = 1;
        if(quality > 100) quality = 100;
        _quality = quality;
}

// ---------------------------------------------------------------------------
// Encode — RGB/RGBA path
// ---------------------------------------------------------------------------

static Image encodeRGB(const Image &input, int quality, JpegImageCodec::Subsampling subsampling) {
        int width = (int)input.width();
        int height = (int)input.height();
        const PixelDesc &pd = input.pixelDesc();

        J_COLOR_SPACE colorSpace = JCS_RGB;
        int numComponents = 3;
        if(pd.id() == PixelDesc::RGBA8_sRGB) {
                colorSpace = JCS_EXT_RGBA;
                numComponents = 4;
        }

        jpeg_compress_struct cinfo;
        JpegErrorMgr jerr;
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = jpegErrorExit;
        if(setjmp(jerr.jmpBuf)) { jpeg_destroy_compress(&cinfo); return Image(); }

        jpeg_create_compress(&cinfo);
        unsigned char *outBuffer = nullptr;
        unsigned long outSize = 0;
        jpeg_mem_dest(&cinfo, &outBuffer, &outSize);

        cinfo.image_width = width;
        cinfo.image_height = height;
        cinfo.input_components = numComponents;
        cinfo.in_color_space = colorSpace;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, quality, TRUE);

        switch(subsampling) {
                case JpegImageCodec::Subsampling444:
                        cinfo.comp_info[0].h_samp_factor = 1;
                        cinfo.comp_info[0].v_samp_factor = 1;
                        break;
                case JpegImageCodec::Subsampling422:
                        cinfo.comp_info[0].h_samp_factor = 2;
                        cinfo.comp_info[0].v_samp_factor = 1;
                        break;
                case JpegImageCodec::Subsampling420:
                        cinfo.comp_info[0].h_samp_factor = 2;
                        cinfo.comp_info[0].v_samp_factor = 2;
                        break;
        }
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;

        jpeg_start_compress(&cinfo, TRUE);
        size_t stride = input.lineStride();
        const uint8_t *pixels = static_cast<const uint8_t *>(input.data());
        while(cinfo.next_scanline < cinfo.image_height) {
                const uint8_t *row = pixels + cinfo.next_scanline * stride;
                JSAMPROW rowPtr = const_cast<JSAMPROW>(row);
                jpeg_write_scanlines(&cinfo, &rowPtr, 1);
        }
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);

        PixelDesc::ID jpegPd = jpegPixelDescFor(pd.id());
        Image result = Image::fromCompressedData(outBuffer, outSize, width, height,
                                                  jpegPd, input.metadata());
        free(outBuffer);
        return result;
}

// ---------------------------------------------------------------------------
// Encode — YCbCr raw data path (all layouts)
// ---------------------------------------------------------------------------

static Image encodeYCbCr(const Image &input, int quality, YCbCrInfo info) {
        int width = (int)input.width();
        int height = (int)input.height();
        int chromaWidth = width / 2;

        jpeg_compress_struct cinfo;
        JpegErrorMgr jerr;
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = jpegErrorExit;
        if(setjmp(jerr.jmpBuf)) { jpeg_destroy_compress(&cinfo); return Image(); }

        jpeg_create_compress(&cinfo);
        unsigned char *outBuffer = nullptr;
        unsigned long outSize = 0;
        jpeg_mem_dest(&cinfo, &outBuffer, &outSize);

        cinfo.image_width = width;
        cinfo.image_height = height;
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_YCbCr;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, quality, TRUE);

        // Sampling factors
        cinfo.comp_info[0].h_samp_factor = 2;
        cinfo.comp_info[0].v_samp_factor = info.is420 ? 2 : 1;
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;
        cinfo.raw_data_in = TRUE;

        jpeg_start_compress(&cinfo, TRUE);

        int mcuRows = DCTSIZE * cinfo.max_v_samp_factor;
        int chromaMcuRows = DCTSIZE;

        // Row buffers for the MCU block
        std::vector<std::vector<uint8_t>> yRowBufs(mcuRows, std::vector<uint8_t>(width));
        std::vector<std::vector<uint8_t>> cbRowBufs(chromaMcuRows, std::vector<uint8_t>(chromaWidth));
        std::vector<std::vector<uint8_t>> crRowBufs(chromaMcuRows, std::vector<uint8_t>(chromaWidth));

        std::vector<JSAMPROW> yRowPtrs(mcuRows);
        std::vector<JSAMPROW> cbRowPtrs(chromaMcuRows);
        std::vector<JSAMPROW> crRowPtrs(chromaMcuRows);
        for(int i = 0; i < mcuRows; i++)      yRowPtrs[i]  = yRowBufs[i].data();
        for(int i = 0; i < chromaMcuRows; i++) cbRowPtrs[i] = cbRowBufs[i].data();
        for(int i = 0; i < chromaMcuRows; i++) crRowPtrs[i] = crRowBufs[i].data();
        JSAMPARRAY jpegPlanes[3] = { yRowPtrs.data(), cbRowPtrs.data(), crRowPtrs.data() };

        // Source plane pointers and strides
        const uint8_t *srcY = nullptr, *srcCb = nullptr, *srcCr = nullptr, *srcCbCr = nullptr;
        size_t strideY = 0, strideCb = 0, strideCr = 0, strideCbCr = 0, strideInterleaved = 0;

        switch(info.layout) {
                case LayoutPlanar422:
                case LayoutPlanar420:
                        srcY  = static_cast<const uint8_t *>(input.data(0));
                        srcCb = static_cast<const uint8_t *>(input.data(1));
                        srcCr = static_cast<const uint8_t *>(input.data(2));
                        strideY  = input.lineStride(0);
                        strideCb = input.lineStride(1);
                        strideCr = input.lineStride(2);
                        break;
                case LayoutSemiPlanar420:
                        srcY    = static_cast<const uint8_t *>(input.data(0));
                        srcCbCr = static_cast<const uint8_t *>(input.data(1));
                        strideY    = input.lineStride(0);
                        strideCbCr = input.lineStride(1);
                        break;
                case LayoutInterleavedUYVY:
                case LayoutInterleavedYUYV:
                        srcY = static_cast<const uint8_t *>(input.data(0));
                        strideInterleaved = input.lineStride(0);
                        break;
                default:
                        break;
        }

        auto deinterleave = (info.layout == LayoutInterleavedUYVY) ? deinterleaveUYVY : deinterleaveYUYV;

        while(cinfo.next_scanline < cinfo.image_height) {
                int lumaStart = cinfo.next_scanline;

                // Fill luma rows
                for(int r = 0; r < mcuRows; r++) {
                        int line = lumaStart + r;
                        if(line >= height) line = height - 1;
                        switch(info.layout) {
                                case LayoutPlanar422:
                                case LayoutPlanar420:
                                        std::memcpy(yRowBufs[r].data(), srcY + line * strideY, width);
                                        break;
                                case LayoutSemiPlanar420:
                                        std::memcpy(yRowBufs[r].data(), srcY + line * strideY, width);
                                        break;
                                case LayoutInterleavedUYVY:
                                case LayoutInterleavedYUYV:
                                        deinterleave(srcY + line * strideInterleaved,
                                                     yRowBufs[r].data(),
                                                     cbRowBufs[std::min(r, chromaMcuRows - 1)].data(),
                                                     crRowBufs[std::min(r, chromaMcuRows - 1)].data(), width);
                                        break;
                                default: break;
                        }
                }

                // Fill chroma rows (only for non-interleaved — interleaved already filled above)
                if(info.layout != LayoutInterleavedUYVY && info.layout != LayoutInterleavedYUYV) {
                        for(int r = 0; r < chromaMcuRows; r++) {
                                int chromaLine;
                                if(info.is420) {
                                        chromaLine = lumaStart / 2 + r;
                                        int maxChromaLine = (height / 2) - 1;
                                        if(chromaLine > maxChromaLine) chromaLine = maxChromaLine;
                                } else {
                                        chromaLine = lumaStart + r;
                                        if(chromaLine >= height) chromaLine = height - 1;
                                }

                                switch(info.layout) {
                                        case LayoutPlanar422:
                                        case LayoutPlanar420:
                                                std::memcpy(cbRowBufs[r].data(), srcCb + chromaLine * strideCb, chromaWidth);
                                                std::memcpy(crRowBufs[r].data(), srcCr + chromaLine * strideCr, chromaWidth);
                                                break;
                                        case LayoutSemiPlanar420:
                                                deinterleaveNV12(srcCbCr + chromaLine * strideCbCr,
                                                                 cbRowBufs[r].data(), crRowBufs[r].data(), chromaWidth);
                                                break;
                                        default: break;
                                }
                        }
                }

                jpeg_write_raw_data(&cinfo, jpegPlanes, mcuRows);
        }

        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);

        PixelDesc::ID jpegPd = jpegPixelDescFor(input.pixelDesc().id());
        Image result = Image::fromCompressedData(outBuffer, outSize, width, height,
                                                  jpegPd, input.metadata());
        free(outBuffer);
        return result;
}

// ---------------------------------------------------------------------------
// Encode entry point
// ---------------------------------------------------------------------------

Image JpegImageCodec::encode(const Image &input) {
        clearError();
        if(!input.isValid()) {
                setError(Error::Invalid, "Input image is not valid");
                return Image();
        }

        YCbCrInfo info = classifyYCbCr(input.pixelDesc().id());
        if(info.layout != LayoutNone) {
                Image result = encodeYCbCr(input, _quality, info);
                if(!result.isValid()) setError(Error::IOError, "JPEG YCbCr compression failed");
                return result;
        }

        Image result = encodeRGB(input, _quality, _subsampling);
        if(!result.isValid()) setError(Error::IOError, "JPEG RGB compression failed");
        return result;
}

// ---------------------------------------------------------------------------
// Decode — RGB output path
// ---------------------------------------------------------------------------

static Image decodeToRGB(const Image &input, PixelDesc::ID outputPd) {
        jpeg_decompress_struct dinfo;
        JpegErrorMgr jerr;
        dinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = jpegErrorExit;
        if(setjmp(jerr.jmpBuf)) { jpeg_destroy_decompress(&dinfo); return Image(); }

        jpeg_create_decompress(&dinfo);
        const uint8_t *jpegData = static_cast<const uint8_t *>(input.data());
        jpeg_mem_src(&dinfo, jpegData, input.compressedSize());
        jpeg_read_header(&dinfo, TRUE);

        dinfo.out_color_space = (outputPd == PixelDesc::RGBA8_sRGB) ? JCS_EXT_RGBA : JCS_RGB;
        jpeg_start_decompress(&dinfo);

        Image output(dinfo.output_width, dinfo.output_height, outputPd);
        if(!output.isValid()) { jpeg_destroy_decompress(&dinfo); return Image(); }

        size_t stride = output.lineStride();
        uint8_t *pixels = static_cast<uint8_t *>(output.data());
        while(dinfo.output_scanline < dinfo.output_height) {
                uint8_t *row = pixels + dinfo.output_scanline * stride;
                JSAMPROW rowPtr = row;
                jpeg_read_scanlines(&dinfo, &rowPtr, 1);
        }
        jpeg_finish_decompress(&dinfo);
        jpeg_destroy_decompress(&dinfo);

        output.metadata() = input.metadata();
        return output;
}

// ---------------------------------------------------------------------------
// Decode — YCbCr raw data output path (all layouts)
// ---------------------------------------------------------------------------

static Image decodeToYCbCr(const Image &input, PixelDesc::ID outputPd, YCbCrInfo info) {
        jpeg_decompress_struct dinfo;
        JpegErrorMgr jerr;
        dinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = jpegErrorExit;
        if(setjmp(jerr.jmpBuf)) { jpeg_destroy_decompress(&dinfo); return Image(); }

        jpeg_create_decompress(&dinfo);
        const uint8_t *jpegData = static_cast<const uint8_t *>(input.data());
        jpeg_mem_src(&dinfo, jpegData, input.compressedSize());
        jpeg_read_header(&dinfo, TRUE);

        dinfo.raw_data_out = TRUE;
        dinfo.out_color_space = JCS_YCbCr;
        jpeg_start_decompress(&dinfo);

        int width = dinfo.output_width;
        int height = dinfo.output_height;
        int chromaWidth = width / 2;

        Image output(width, height, outputPd);
        if(!output.isValid()) { jpeg_destroy_decompress(&dinfo); return Image(); }

        int mcuRows = DCTSIZE * dinfo.max_v_samp_factor;
        int chromaMcuRows = DCTSIZE;

        std::vector<std::vector<uint8_t>> yRowBufs(mcuRows, std::vector<uint8_t>(width));
        std::vector<std::vector<uint8_t>> cbRowBufs(chromaMcuRows, std::vector<uint8_t>(chromaWidth));
        std::vector<std::vector<uint8_t>> crRowBufs(chromaMcuRows, std::vector<uint8_t>(chromaWidth));

        std::vector<JSAMPROW> yRowPtrs(mcuRows);
        std::vector<JSAMPROW> cbRowPtrs(chromaMcuRows);
        std::vector<JSAMPROW> crRowPtrs(chromaMcuRows);
        for(int i = 0; i < mcuRows; i++)      yRowPtrs[i]  = yRowBufs[i].data();
        for(int i = 0; i < chromaMcuRows; i++) cbRowPtrs[i] = cbRowBufs[i].data();
        for(int i = 0; i < chromaMcuRows; i++) crRowPtrs[i] = crRowBufs[i].data();
        JSAMPARRAY jpegPlanes[3] = { yRowPtrs.data(), cbRowPtrs.data(), crRowPtrs.data() };

        // Output plane pointers and strides
        uint8_t *dstY = nullptr, *dstCb = nullptr, *dstCr = nullptr, *dstCbCr = nullptr;
        size_t strideY = 0, strideCb = 0, strideCr = 0, strideCbCr = 0, strideOut = 0;

        switch(info.layout) {
                case LayoutPlanar422:
                case LayoutPlanar420:
                        dstY  = static_cast<uint8_t *>(output.data(0));
                        dstCb = static_cast<uint8_t *>(output.data(1));
                        dstCr = static_cast<uint8_t *>(output.data(2));
                        strideY  = output.lineStride(0);
                        strideCb = output.lineStride(1);
                        strideCr = output.lineStride(2);
                        break;
                case LayoutSemiPlanar420:
                        dstY    = static_cast<uint8_t *>(output.data(0));
                        dstCbCr = static_cast<uint8_t *>(output.data(1));
                        strideY    = output.lineStride(0);
                        strideCbCr = output.lineStride(1);
                        break;
                case LayoutInterleavedUYVY:
                case LayoutInterleavedYUYV:
                        dstY = static_cast<uint8_t *>(output.data(0));
                        strideOut = output.lineStride(0);
                        break;
                default: break;
        }

        auto interleaveFn = (info.layout == LayoutInterleavedUYVY) ? interleaveUYVY : interleaveYUYV;

        while(dinfo.output_scanline < dinfo.output_height) {
                unsigned int lumaStart = dinfo.output_scanline;
                jpeg_read_raw_data(&dinfo, jpegPlanes, mcuRows);

                // Write luma rows
                for(int r = 0; r < mcuRows; r++) {
                        int line = lumaStart + r;
                        if(line >= height) break;
                        switch(info.layout) {
                                case LayoutPlanar422:
                                case LayoutPlanar420:
                                case LayoutSemiPlanar420:
                                        std::memcpy(dstY + line * strideY, yRowBufs[r].data(), width);
                                        break;
                                case LayoutInterleavedUYVY:
                                case LayoutInterleavedYUYV:
                                        // Chroma for this luma row is at r/vFactor in the chroma MCU
                                        // For interleaved we reconstruct per-line
                                        break;
                                default: break;
                        }
                }

                // Write chroma rows
                for(int r = 0; r < chromaMcuRows; r++) {
                        int chromaLine;
                        if(info.is420) {
                                chromaLine = (int)(lumaStart / 2) + r;
                                if(chromaLine >= height / 2) break;
                        } else {
                                chromaLine = (int)lumaStart + r;
                                if(chromaLine >= height) break;
                        }

                        switch(info.layout) {
                                case LayoutPlanar422:
                                case LayoutPlanar420:
                                        std::memcpy(dstCb + chromaLine * strideCb, cbRowBufs[r].data(), chromaWidth);
                                        std::memcpy(dstCr + chromaLine * strideCr, crRowBufs[r].data(), chromaWidth);
                                        break;
                                case LayoutSemiPlanar420:
                                        interleaveNV12(dstCbCr + chromaLine * strideCbCr,
                                                       cbRowBufs[r].data(), crRowBufs[r].data(), chromaWidth);
                                        break;
                                default: break;
                        }
                }

                // For interleaved output, combine luma + chroma into packed lines
                if(info.layout == LayoutInterleavedUYVY || info.layout == LayoutInterleavedYUYV) {
                        for(int r = 0; r < mcuRows; r++) {
                                int line = lumaStart + r;
                                if(line >= height) break;
                                int chromaRow = info.is420 ? r / 2 : r;
                                interleaveFn(dstY + line * strideOut,
                                             yRowBufs[r].data(),
                                             cbRowBufs[chromaRow].data(),
                                             crRowBufs[chromaRow].data(), width);
                        }
                }
        }

        jpeg_finish_decompress(&dinfo);
        jpeg_destroy_decompress(&dinfo);

        output.metadata() = input.metadata();
        return output;
}

// ---------------------------------------------------------------------------
// Decode entry point
// ---------------------------------------------------------------------------

Image JpegImageCodec::decode(const Image &input, int outputFormat) {
        clearError();
        if(!input.isValid() || !input.isCompressed()) {
                setError(Error::Invalid, "Input image is not valid or not compressed");
                return Image();
        }

        PixelDesc::ID outPd = static_cast<PixelDesc::ID>(outputFormat);
        if(outPd == PixelDesc::Invalid) {
                const auto &targets = input.pixelDesc().decodeTargets();
                if(targets.isEmpty()) {
                        setError(Error::PixelFormatNotSupported, "No decode targets for this JPEG format");
                        return Image();
                }
                outPd = targets[0];
        }

        if(outPd == PixelDesc::RGB8_sRGB || outPd == PixelDesc::RGBA8_sRGB) {
                Image result = decodeToRGB(input, outPd);
                if(!result.isValid()) setError(Error::IOError, "JPEG RGB decompression failed");
                return result;
        }

        YCbCrInfo info = classifyYCbCr(outPd);
        if(info.layout != LayoutNone) {
                Image result = decodeToYCbCr(input, outPd, info);
                if(!result.isValid()) setError(Error::IOError, "JPEG YCbCr decompression failed");
                return result;
        }

        setError(Error::PixelFormatNotSupported,
                 String::sprintf("Unsupported decode target: %d", (int)outPd));
        return Image();
}

PROMEKI_NAMESPACE_END
