/**
 * @file      jpegvideocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <cstring>
#include <vector>
#include <algorithm>

#include <promeki/jpegvideocodec.h>
#include <promeki/mediapacket.h>
#include <promeki/mediaconfig.h>
#include <promeki/buffer.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/logger.h>
#include <promeki/enum.h>
#include <promeki/enums.h>

#include <jpeglib.h>

PROMEKI_NAMESPACE_BEGIN

// ===========================================================================
// libjpeg-turbo plumbing — folded in from the retired JpegImageCodec.
// The encoder / decoder session classes below are the only owners of
// the libjpeg state now.
// ===========================================================================

namespace {

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

static YCbCrInfo classifyYCbCr(PixelFormat::ID id) {
        switch(id) {
                // 4:2:2 interleaved YUYV (all matrix × range variants).
                case PixelFormat::YUV8_422_Rec709:
                case PixelFormat::YUV8_422_Rec709_Full:
                case PixelFormat::YUV8_422_Rec601:
                case PixelFormat::YUV8_422_Rec601_Full:
                        return { LayoutInterleavedYUYV, false };
                // 4:2:2 interleaved UYVY (Rec.601 / Rec.709 limited variants only).
                case PixelFormat::YUV8_422_UYVY_Rec709:
                case PixelFormat::YUV8_422_UYVY_Rec601:
                        return { LayoutInterleavedUYVY, false };
                // 4:2:2 planar (Rec.709 limited only; no other planar
                // variants exist as independent IDs).
                case PixelFormat::YUV8_422_Planar_Rec709:
                        return { LayoutPlanar422, false };
                // 4:2:0 planar (all matrix × range variants).
                case PixelFormat::YUV8_420_Planar_Rec709:
                case PixelFormat::YUV8_420_Planar_Rec709_Full:
                case PixelFormat::YUV8_420_Planar_Rec601:
                case PixelFormat::YUV8_420_Planar_Rec601_Full:
                        return { LayoutPlanar420, true };
                // 4:2:0 semi-planar (NV12, Rec.709 / Rec.601 limited only).
                case PixelFormat::YUV8_420_SemiPlanar_Rec709:
                case PixelFormat::YUV8_420_SemiPlanar_Rec601:
                        return { LayoutSemiPlanar420, true };
                default:
                        return { LayoutNone, false };
        }
}

// Maps an uncompressed input PixelFormat to the matching compressed
// JPEG output PixelFormat.  "Matching" means subsampling (4:2:2 vs
// 4:2:0), matrix (Rec.709 vs Rec.601), and range (limited vs full)
// all come from the same cell of the 2 × 2 × 2 grid.  Keeps the
// encoder honest about what's actually inside the resulting JFIF
// bitstream.
static PixelFormat::ID jpegPixelFormatFor(PixelFormat::ID srcDesc) {
        switch(srcDesc) {
                case PixelFormat::RGB8_sRGB:   return PixelFormat::JPEG_RGB8_sRGB;
                case PixelFormat::RGBA8_sRGB:  return PixelFormat::JPEG_RGBA8_sRGB;

                // 4:2:2 Rec.709 limited — legacy default.
                case PixelFormat::YUV8_422_Rec709:
                case PixelFormat::YUV8_422_UYVY_Rec709:
                case PixelFormat::YUV8_422_Planar_Rec709:
                        return PixelFormat::JPEG_YUV8_422_Rec709;

                // 4:2:0 Rec.709 limited.
                case PixelFormat::YUV8_420_Planar_Rec709:
                case PixelFormat::YUV8_420_SemiPlanar_Rec709:
                        return PixelFormat::JPEG_YUV8_420_Rec709;

                // 4:2:2 Rec.601 limited.
                case PixelFormat::YUV8_422_Rec601:
                case PixelFormat::YUV8_422_UYVY_Rec601:
                        return PixelFormat::JPEG_YUV8_422_Rec601;

                // 4:2:0 Rec.601 limited.
                case PixelFormat::YUV8_420_Planar_Rec601:
                case PixelFormat::YUV8_420_SemiPlanar_Rec601:
                        return PixelFormat::JPEG_YUV8_420_Rec601;

                // 4:2:2 Rec.709 full.
                case PixelFormat::YUV8_422_Rec709_Full:
                        return PixelFormat::JPEG_YUV8_422_Rec709_Full;

                // 4:2:0 Rec.709 full.
                case PixelFormat::YUV8_420_Planar_Rec709_Full:
                        return PixelFormat::JPEG_YUV8_420_Rec709_Full;

                // 4:2:2 Rec.601 full (strict JFIF).
                case PixelFormat::YUV8_422_Rec601_Full:
                        return PixelFormat::JPEG_YUV8_422_Rec601_Full;

                // 4:2:0 Rec.601 full (strict JFIF).
                case PixelFormat::YUV8_420_Planar_Rec601_Full:
                        return PixelFormat::JPEG_YUV8_420_Rec601_Full;

                default: return PixelFormat::JPEG_RGB8_sRGB;
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
// Encode — RGB/RGBA path
// ---------------------------------------------------------------------------
//
// The session owns a persistent jpeg_compress_struct (created once in
// the encoder ctor, destroyed in the dtor); the helpers below reset
// per-frame state via jpeg_mem_dest + jpeg_set_defaults rather than
// recreating the whole structure.  On error the per-frame setjmp
// branch calls jpeg_abort_compress to return cinfo to the
// "ready for next image" state without destroying it.

static Image encodeRGB(jpeg_compress_struct &cinfo, JpegErrorMgr &jerr,
                       const Image &input, int quality,
                       JpegVideoEncoder::Subsampling subsampling) {
        int width = (int)input.width();
        int height = (int)input.height();
        const PixelFormat &pd = input.pixelFormat();

        J_COLOR_SPACE colorSpace = JCS_RGB;
        int numComponents = 3;
        if(pd.id() == PixelFormat::RGBA8_sRGB) {
                colorSpace = JCS_EXT_RGBA;
                numComponents = 4;
        }

        if(setjmp(jerr.jmpBuf)) { jpeg_abort_compress(&cinfo); return Image(); }

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
                case JpegVideoEncoder::Subsampling444:
                        cinfo.comp_info[0].h_samp_factor = 1;
                        cinfo.comp_info[0].v_samp_factor = 1;
                        break;
                case JpegVideoEncoder::Subsampling422:
                        cinfo.comp_info[0].h_samp_factor = 2;
                        cinfo.comp_info[0].v_samp_factor = 1;
                        break;
                case JpegVideoEncoder::Subsampling420:
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

        PixelFormat::ID jpegPd = jpegPixelFormatFor(pd.id());
        Image result = Image::fromCompressedData(outBuffer, outSize, width, height,
                                                  jpegPd, input.metadata());
        free(outBuffer);
        return result;
}

// ---------------------------------------------------------------------------
// Encode — YCbCr raw data path (all layouts)
// ---------------------------------------------------------------------------

static Image encodeYCbCr(jpeg_compress_struct &cinfo, JpegErrorMgr &jerr,
                         const Image &input, int quality, YCbCrInfo info) {
        int width = (int)input.width();
        int height = (int)input.height();
        int chromaWidth = width / 2;

        if(setjmp(jerr.jmpBuf)) { jpeg_abort_compress(&cinfo); return Image(); }

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

        PixelFormat::ID jpegPd = jpegPixelFormatFor(input.pixelFormat().id());
        Image result = Image::fromCompressedData(outBuffer, outSize, width, height,
                                                  jpegPd, input.metadata());
        free(outBuffer);
        return result;
}

// ---------------------------------------------------------------------------
// Decode — RGB output path
// ---------------------------------------------------------------------------

static Image decodeToRGB(jpeg_decompress_struct &dinfo, JpegErrorMgr &jerr,
                         const Image &input, PixelFormat::ID outputPd) {
        if(setjmp(jerr.jmpBuf)) { jpeg_abort_decompress(&dinfo); return Image(); }

        const uint8_t *jpegData = static_cast<const uint8_t *>(input.data());
        jpeg_mem_src(&dinfo, jpegData, input.compressedSize());
        jpeg_read_header(&dinfo, TRUE);

        dinfo.out_color_space = (outputPd == PixelFormat::RGBA8_sRGB) ? JCS_EXT_RGBA : JCS_RGB;
        jpeg_start_decompress(&dinfo);

        Image output(dinfo.output_width, dinfo.output_height, outputPd);
        if(!output.isValid()) { jpeg_abort_decompress(&dinfo); return Image(); }

        size_t stride = output.lineStride();
        uint8_t *pixels = static_cast<uint8_t *>(output.data());
        while(dinfo.output_scanline < dinfo.output_height) {
                uint8_t *row = pixels + dinfo.output_scanline * stride;
                JSAMPROW rowPtr = row;
                jpeg_read_scanlines(&dinfo, &rowPtr, 1);
        }
        jpeg_finish_decompress(&dinfo);

        output.metadata() = input.metadata();
        return output;
}

// ---------------------------------------------------------------------------
// Decode — YCbCr raw data output path (all layouts)
// ---------------------------------------------------------------------------

static Image decodeToYCbCr(jpeg_decompress_struct &dinfo, JpegErrorMgr &jerr,
                           const Image &input, PixelFormat::ID outputPd, YCbCrInfo info) {
        if(setjmp(jerr.jmpBuf)) { jpeg_abort_decompress(&dinfo); return Image(); }

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
        if(!output.isValid()) { jpeg_abort_decompress(&dinfo); return Image(); }

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

                // For interleaved output, combine luma + chroma into packed lines.
                // chromaRow must track the *source* JPEG's subsampling (how many
                // chroma rows libjpeg delivered for this MCU group), not the
                // output layout.  Using info.is420 (the output layout's flag)
                // would walk off the end of cbRowBufs/crRowBufs whenever the
                // source is 4:2:0 but the output is 4:2:2 (e.g. a 4:2:0 USB
                // MJPEG webcam decoding to YUV8_422_Rec709) — chroma buffers
                // only hold chromaMcuRows entries while the luma loop runs for
                // mcuRows.  chromaMcuRows / mcuRows gives the correct chroma
                // upsample ratio for the usual Cb_v_samp=1 cases (4:4:4, 4:2:2,
                // 4:2:0), replicating each chroma row as needed.
                if(info.layout == LayoutInterleavedUYVY || info.layout == LayoutInterleavedYUYV) {
                        for(int r = 0; r < mcuRows; r++) {
                                int line = lumaStart + r;
                                if(line >= height) break;
                                int chromaRow = (mcuRows > 0)
                                        ? (r * chromaMcuRows / mcuRows)
                                        : 0;
                                if(chromaRow >= chromaMcuRows) chromaRow = chromaMcuRows - 1;
                                interleaveFn(dstY + line * strideOut,
                                             yRowBufs[r].data(),
                                             cbRowBufs[chromaRow].data(),
                                             crRowBufs[chromaRow].data(), width);
                        }
                }
        }

        jpeg_finish_decompress(&dinfo);

        output.metadata() = input.metadata();
        return output;
}

// Encodes one uncompressed Image to JPEG using libjpeg-turbo.  Returns
// an invalid Image on error; the caller sets the session's error state.
static Image encodeOneJpegFrame(jpeg_compress_struct &cinfo, JpegErrorMgr &jerr,
                                const Image &input, int quality,
                                JpegVideoEncoder::Subsampling subsampling) {
        YCbCrInfo info = classifyYCbCr(input.pixelFormat().id());
        if(info.layout != LayoutNone) return encodeYCbCr(cinfo, jerr, input, quality, info);
        return encodeRGB(cinfo, jerr, input, quality, subsampling);
}

// Decodes one compressed JPEG Image into uncompressed form with the
// requested target PixelFormat.  Returns invalid on error.
static Image decodeOneJpegFrame(jpeg_decompress_struct &dinfo, JpegErrorMgr &jerr,
                                const Image &input, PixelFormat::ID outPd) {
        if(outPd == PixelFormat::Invalid) {
                const auto &targets = input.pixelFormat().decodeTargets();
                if(targets.isEmpty()) return Image();
                outPd = targets[0];
        }

        if(outPd == PixelFormat::RGB8_sRGB || outPd == PixelFormat::RGBA8_sRGB) {
                return decodeToRGB(dinfo, jerr, input, outPd);
        }

        YCbCrInfo info = classifyYCbCr(outPd);
        if(info.layout != LayoutNone) {
                return decodeToYCbCr(dinfo, jerr, input, outPd, info);
        }
        return Image();
}

} // namespace

// ---------------------------------------------------------------------------
// JpegVideoEncoder — pImpl owning the persistent libjpeg-turbo state.
// ---------------------------------------------------------------------------

struct JpegVideoEncoder::Impl {
        jpeg_compress_struct cinfo{};
        JpegErrorMgr         jerr{};
        bool                 created = false;

        Impl() {
                cinfo.err = jpeg_std_error(&jerr.pub);
                jerr.pub.error_exit = jpegErrorExit;
                // libjpeg's create can longjmp on OOM during state
                // allocation; guard with a setjmp so the partially-
                // initialized struct doesn't leak.
                if(setjmp(jerr.jmpBuf)) { created = false; return; }
                jpeg_create_compress(&cinfo);
                created = true;
        }

        ~Impl() {
                if(created) jpeg_destroy_compress(&cinfo);
        }
};

JpegVideoEncoder::JpegVideoEncoder() : _impl(new Impl) {}

JpegVideoEncoder::~JpegVideoEncoder() {
        delete _impl;
}

List<int> JpegVideoEncoder::supportedInputList() {
        // Mirror the input-format coverage the classify / encode paths
        // above actually implement.
        return {
                static_cast<int>(PixelFormat::RGB8_sRGB),
                static_cast<int>(PixelFormat::RGBA8_sRGB),
                static_cast<int>(PixelFormat::YUV8_422_Rec709),
                static_cast<int>(PixelFormat::YUV8_422_UYVY_Rec709),
                static_cast<int>(PixelFormat::YUV8_422_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV8_420_SemiPlanar_Rec709),
                static_cast<int>(PixelFormat::YUV8_422_Rec601),
                static_cast<int>(PixelFormat::YUV8_422_UYVY_Rec601),
                static_cast<int>(PixelFormat::YUV8_420_Planar_Rec601),
                static_cast<int>(PixelFormat::YUV8_420_SemiPlanar_Rec601),
                static_cast<int>(PixelFormat::YUV8_422_Rec709_Full),
                static_cast<int>(PixelFormat::YUV8_422_Rec601_Full),
                static_cast<int>(PixelFormat::YUV8_420_Planar_Rec709_Full),
                static_cast<int>(PixelFormat::YUV8_420_Planar_Rec601_Full),
        };
}

void JpegVideoEncoder::configure(const MediaConfig &config) {
        // JpegQuality: clamped to the 1-100 range libjpeg-turbo accepts.
        if(config.contains(MediaConfig::JpegQuality)) {
                Variant v = config.get(MediaConfig::JpegQuality);
                if(v.isValid()) {
                        int q = v.get<int>();
                        if(q < 1) q = 1;
                        if(q > 100) q = 100;
                        _quality = q;
                }
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
                        _subsampling = static_cast<Subsampling>(e.value());
                }
        }
        _outputPd = config.getAs<PixelFormat>(MediaConfig::OutputPixelFormat, PixelFormat());
        _capacity = config.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;
}

Error JpegVideoEncoder::submitFrame(const Image::Ptr &frame, const MediaTimeStamp &pts) {
        clearError();
        if(!frame.isValid() || !frame->isValid()) {
                setError(Error::Invalid, "JpegVideoEncoder: invalid frame");
                return _lastError;
        }
        if(static_cast<int>(_queue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("JpegVideoEncoder: output queue exceeded capacity (%d)",
                            _capacity);
                _capacityWarned = true;
        }

        if(!_impl->created) {
                setError(Error::LibraryFailure,
                         "JpegVideoEncoder: libjpeg-turbo state not initialized");
                return _lastError;
        }
        Image encoded = encodeOneJpegFrame(_impl->cinfo, _impl->jerr,
                                           *frame, _quality, _subsampling);
        if(!encoded.isValid()) {
                setError(Error::ConversionFailed,
                         "JpegVideoEncoder: libjpeg-turbo encode failed");
                return _lastError;
        }

        // Wrap the encoded plane buffer as a VideoPacket — same
        // BufferView semantics we use elsewhere.  Every JPEG bitstream
        // is independently decodable so the packet is always a
        // keyframe.  The encoded Image carries the source frame's
        // metadata (timecode, etc.); propagate it onto the packet so
        // downstream stages can keep tracking each unit.
        auto pkt = VideoPacket::Ptr::create(encoded.plane(0), encoded.pixelFormat());
        pkt.modify()->setPts(pts);
        pkt.modify()->setDts(pts);
        pkt.modify()->addFlag(VideoPacket::Keyframe);
        pkt.modify()->metadata() = encoded.metadata();
        _queue.pushToBack(std::move(pkt));
        return Error::Ok;
}

VideoPacket::Ptr JpegVideoEncoder::receivePacket() {
        if(_queue.isEmpty()) return VideoPacket::Ptr();
        return _queue.popFromFront();
}

Error JpegVideoEncoder::flush() {
        // JPEG is intra-only: every submitFrame already produced its
        // packet, so flush has nothing to do beyond returning Ok.
        return Error::Ok;
}

Error JpegVideoEncoder::reset() {
        _queue.clear();
        _capacityWarned = false;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// JpegVideoDecoder — pImpl owning the persistent libjpeg-turbo state.
// ---------------------------------------------------------------------------

struct JpegVideoDecoder::Impl {
        jpeg_decompress_struct dinfo{};
        JpegErrorMgr           jerr{};
        bool                   created = false;

        Impl() {
                dinfo.err = jpeg_std_error(&jerr.pub);
                jerr.pub.error_exit = jpegErrorExit;
                if(setjmp(jerr.jmpBuf)) { created = false; return; }
                jpeg_create_decompress(&dinfo);
                created = true;
        }

        ~Impl() {
                if(created) jpeg_destroy_decompress(&dinfo);
        }
};

JpegVideoDecoder::JpegVideoDecoder() : _impl(new Impl) {}

JpegVideoDecoder::~JpegVideoDecoder() {
        delete _impl;
}

List<int> JpegVideoDecoder::supportedOutputList() {
        return {
                static_cast<int>(PixelFormat::RGB8_sRGB),
                static_cast<int>(PixelFormat::RGBA8_sRGB),
                static_cast<int>(PixelFormat::YUV8_422_Rec709),
                static_cast<int>(PixelFormat::YUV8_422_UYVY_Rec709),
                static_cast<int>(PixelFormat::YUV8_422_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV8_420_Planar_Rec709),
                static_cast<int>(PixelFormat::YUV8_420_SemiPlanar_Rec709),
        };
}

void JpegVideoDecoder::configure(const MediaConfig &config) {
        _outputPd = config.getAs<PixelFormat>(MediaConfig::OutputPixelFormat, PixelFormat());
        _capacity = config.getAs<int>(MediaConfig::Capacity, 8);
        if(_capacity < 1) _capacity = 1;
}

Error JpegVideoDecoder::submitPacket(const VideoPacket::Ptr &packet) {
        clearError();
        if(!packet.isValid() || !packet->isValid() || packet->size() == 0) {
                setError(Error::Invalid, "JpegVideoDecoder: empty packet");
                return _lastError;
        }
        if(static_cast<int>(_queue.size()) >= _capacity && !_capacityWarned) {
                promekiWarn("JpegVideoDecoder: output queue exceeded capacity (%d)",
                            _capacity);
                _capacityWarned = true;
        }

        // Wrap the packet bytes as a compressed Image.  Width / height
        // here are placeholders — the libjpeg-turbo decode path parses
        // the real dimensions out of the JPEG header itself; we only
        // need a non-zero size so Image::fromCompressedData allocates
        // the backing buffer correctly.  The PixelFormat on the
        // synthetic input mostly matters for its decodeTargets list,
        // which we override with _outputPd.id() below anyway.
        const PixelFormat inputPd = packet->pixelFormat().isValid()
                ? packet->pixelFormat()
                : PixelFormat(PixelFormat::JPEG_RGB8_sRGB);
        Image jpegImage = Image::fromCompressedData(packet->view().data(),
                                                    packet->size(),
                                                    1, 1, inputPd,
                                                    packet->metadata());
        if(!jpegImage.isValid()) {
                setError(Error::IOError, "JpegVideoDecoder: fromCompressedData failed");
                return _lastError;
        }

        // _outputPd.id() of Invalid (== 0) tells the decode helper to
        // use its first registered decodeTarget for the input
        // PixelFormat, matching the pre-Phase-4 behaviour.
        PixelFormat::ID outPd = _outputPd.isValid()
                ? _outputPd.id() : PixelFormat::Invalid;
        if(!_impl->created) {
                setError(Error::LibraryFailure,
                         "JpegVideoDecoder: libjpeg-turbo state not initialized");
                return _lastError;
        }
        Image decoded = decodeOneJpegFrame(_impl->dinfo, _impl->jerr, jpegImage, outPd);
        if(!decoded.isValid()) {
                setError(Error::ConversionFailed,
                         "JpegVideoDecoder: libjpeg-turbo decode failed");
                return _lastError;
        }
        _queue.pushToBack(Image::Ptr::create(std::move(decoded)));
        return Error::Ok;
}

Image::Ptr JpegVideoDecoder::receiveFrame() {
        if(_queue.isEmpty()) return Image::Ptr();
        return _queue.popFromFront();
}

Error JpegVideoDecoder::flush() { return Error::Ok; }

Error JpegVideoDecoder::reset() {
        _queue.clear();
        _capacityWarned = false;
        return Error::Ok;
}

// ---------------------------------------------------------------------------
// Backend registration — typed (codec, backend) pair on both the
// VideoEncoder and VideoDecoder registries.  Registered under the
// "Turbo" backend name for VideoCodec::JPEG.
// ---------------------------------------------------------------------------

namespace {

struct JpegVideoCodecRegistrar {
        JpegVideoCodecRegistrar() {
                auto bk = VideoCodec::registerBackend("Turbo");
                if(error(bk).isError()) return;
                const VideoCodec::Backend backend = value(bk);

                VideoEncoder::registerBackend({
                        .codecId         = VideoCodec::JPEG,
                        .backend         = backend,
                        .weight          = BackendWeight::Vendored,
                        .supportedInputs = JpegVideoEncoder::supportedInputList(),
                        .factory         = []() -> VideoEncoder * {
                                return new JpegVideoEncoder();
                        },
                });
                VideoDecoder::registerBackend({
                        .codecId          = VideoCodec::JPEG,
                        .backend           = backend,
                        .weight           = BackendWeight::Vendored,
                        .supportedOutputs = JpegVideoDecoder::supportedOutputList(),
                        .factory          = []() -> VideoDecoder * {
                                return new JpegVideoDecoder();
                        },
                });
        }
};

static JpegVideoCodecRegistrar _jpegVideoCodecRegistrar;

} // namespace

PROMEKI_NAMESPACE_END
