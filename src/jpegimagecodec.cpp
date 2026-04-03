/**
 * @file      jpegimagecodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <promeki/proav/jpegimagecodec.h>
#include <promeki/proav/image.h>
#include <promeki/core/pixeldesc.h>
#include <promeki/core/buffer.h>
#include <promeki/thirdparty/jpeglib.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_IMAGE_CODEC(JpegImageCodec, "jpeg")

// Custom error handler that uses longjmp instead of calling exit()
struct JpegErrorMgr {
        jpeg_error_mgr  pub;
        jmp_buf         jmpBuf;
};

static void jpegErrorExit(j_common_ptr cinfo) {
        JpegErrorMgr *mgr = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
        longjmp(mgr->jmpBuf, 1);
}

// Maps an uncompressed PixelDesc to its JPEG counterpart.
static PixelDesc::ID jpegPixelDescFor(PixelDesc::ID srcDesc) {
        switch(srcDesc) {
                case PixelDesc::RGB8_sRGB_Full:  return PixelDesc::JPEG_RGB8_sRGB_Full;
                case PixelDesc::RGBA8_sRGB_Full: return PixelDesc::JPEG_RGBA8_sRGB_Full;
                default:               return PixelDesc::JPEG_RGB8_sRGB_Full;
        }
}

JpegImageCodec::~JpegImageCodec() = default;

String JpegImageCodec::name() const {
        return "jpeg";
}

String JpegImageCodec::description() const {
        return "JPEG image codec (libjpeg-turbo)";
}

bool JpegImageCodec::canEncode() const {
        return true;
}

bool JpegImageCodec::canDecode() const {
        return false;
}

void JpegImageCodec::setQuality(int quality) {
        if(quality < 1) quality = 1;
        if(quality > 100) quality = 100;
        _quality = quality;
}

Image JpegImageCodec::encode(const Image &input) {
        clearError();

        if(!input.isValid()) {
                setError(Error::Invalid, "Input image is not valid");
                return Image();
        }

        int width = (int)input.width();
        int height = (int)input.height();
        const PixelDesc &pd = input.pixelDesc();

        // Determine JPEG input color space and component count
        J_COLOR_SPACE colorSpace = JCS_RGB;
        int numComponents = 3;
        if(pd.isValid() && pd.id() == PixelDesc::RGBA8_sRGB_Full) {
                colorSpace = JCS_EXT_RGBA;
                numComponents = 4;
        }

        // Set up libjpeg compression
        jpeg_compress_struct cinfo;
        JpegErrorMgr jerr;
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = jpegErrorExit;

        if(setjmp(jerr.jmpBuf)) {
                jpeg_destroy_compress(&cinfo);
                setError(Error::IOError, "JPEG compression failed");
                return Image();
        }

        jpeg_create_compress(&cinfo);

        // Use memory destination
        unsigned char *outBuffer = nullptr;
        unsigned long outSize = 0;
        jpeg_mem_dest(&cinfo, &outBuffer, &outSize);

        cinfo.image_width = width;
        cinfo.image_height = height;
        cinfo.input_components = numComponents;
        cinfo.in_color_space = colorSpace;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, _quality, TRUE);

        // Set chroma subsampling factors
        switch(_subsampling) {
                case Subsampling444:
                        cinfo.comp_info[0].h_samp_factor = 1;
                        cinfo.comp_info[0].v_samp_factor = 1;
                        break;
                case Subsampling422:
                        // Y: H=2 V=1, Cb/Cr: H=1 V=1
                        // Matches RFC 2435 type 1 for RTP JPEG compatibility
                        cinfo.comp_info[0].h_samp_factor = 2;
                        cinfo.comp_info[0].v_samp_factor = 1;
                        break;
                case Subsampling420:
                        cinfo.comp_info[0].h_samp_factor = 2;
                        cinfo.comp_info[0].v_samp_factor = 2;
                        break;
        }
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;

        jpeg_start_compress(&cinfo, TRUE);

        // Write scanlines
        size_t stride = input.lineStride();
        const uint8_t *pixels = static_cast<const uint8_t *>(input.data());
        while(cinfo.next_scanline < cinfo.image_height) {
                const uint8_t *row = pixels + cinfo.next_scanline * stride;
                JSAMPROW rowPtr = const_cast<JSAMPROW>(row);
                jpeg_write_scanlines(&cinfo, &rowPtr, 1);
        }

        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);

        // Wrap compressed data in a JPEG Image
        PixelDesc::ID jpegPd = jpegPixelDescFor(pd.isValid() ? pd.id() : PixelDesc::RGB8_sRGB_Full);
        Image result = Image::fromCompressedData(outBuffer, outSize, width, height,
                                                  jpegPd, input.metadata());
        free(outBuffer);
        return result;
}

Image JpegImageCodec::decode(const Image &input, int outputFormat) {
        (void)input;
        (void)outputFormat;
        setError(Error::NotImplemented, "JPEG decoding is not yet implemented");
        return Image();
}

PROMEKI_NAMESPACE_END
