/**
 * @file      jpegencodernode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <promeki/proav/jpegencodernode.h>
#include <promeki/proav/frame.h>
#include <promeki/proav/image.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/buffer.h>
#include <promeki/core/metadata.h>
#include <promeki/thirdparty/jpeglib.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(JpegEncoderNode)

// Custom error handler that uses longjmp instead of calling exit()
struct JpegErrorMgr {
        jpeg_error_mgr  pub;
        jmp_buf         jmpBuf;
};

static void jpegErrorExit(j_common_ptr cinfo) {
        JpegErrorMgr *mgr = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
        longjmp(mgr->jmpBuf, 1);
}

// Maps an uncompressed pixel format ID to its JPEG counterpart.
static int jpegPixelFormatFor(int srcFormat) {
        switch(srcFormat) {
                case PixelFormat::RGB8:  return PixelFormat::JPEG_RGB8;
                case PixelFormat::RGBA8: return PixelFormat::JPEG_RGBA8;
                default:                 return PixelFormat::JPEG_RGB8;
        }
}

JpegEncoderNode::JpegEncoderNode(ObjectBase *parent) : MediaNode(parent) {
        setName("JpegEncoderNode");
        auto input = MediaPort::Ptr::create("input", MediaPort::Input, MediaPort::Image);
        auto output = MediaPort::Ptr::create("output", MediaPort::Output, MediaPort::Image);
        addInputPort(input);
        addOutputPort(output);
}

Error JpegEncoderNode::configure() {
        if(state() != Idle) return Error(Error::Invalid);
        setState(Configured);
        return Error(Error::Ok);
}

void JpegEncoderNode::process() {
        Frame::Ptr frame = dequeueInput();
        if(!frame.isValid()) return;

        if(frame->imageList().isEmpty()) {
                deliverOutput(frame);
                return;
        }

        Image::Ptr img = frame->imageList()[0];
        int width = (int)img->width();
        int height = (int)img->height();
        const PixelFormat *pf = img->pixelFormat();

        // Determine JPEG input color space and component count
        J_COLOR_SPACE colorSpace = JCS_RGB;
        int numComponents = 3;
        if(pf != nullptr && pf->id() == PixelFormat::RGBA8) {
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
                emitError("JPEG compression failed");
                return;
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

        jpeg_start_compress(&cinfo, TRUE);

        // Write scanlines
        size_t stride = img->lineStride();
        const uint8_t *pixels = static_cast<const uint8_t *>(img->data());
        while(cinfo.next_scanline < cinfo.image_height) {
                const uint8_t *row = pixels + cinfo.next_scanline * stride;
                JSAMPROW rowPtr = const_cast<JSAMPROW>(row);
                jpeg_write_scanlines(&cinfo, &rowPtr, 1);
        }

        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);

        // Track stats
        size_t uncompressedSize = stride * height;
        _framesEncoded++;
        _totalCompressedBytes += outSize;
        _totalUncompressedBytes += uncompressedSize;

        // Wrap compressed data in a JPEG Image
        int jpegFmt = jpegPixelFormatFor(pf ? pf->id() : PixelFormat::RGB8);
        Image jpegImg = Image::fromCompressedData(outBuffer, outSize, width, height,
                                                  jpegFmt, img->metadata());
        free(outBuffer);

        // Build output frame
        Frame::Ptr outFrame = Frame::Ptr::create();
        outFrame.modify()->imageList().pushToBack(Image::Ptr::create(jpegImg));
        outFrame.modify()->metadata() = frame->metadata();
        deliverOutput(outFrame);
        return;
}

Map<String, Variant> JpegEncoderNode::extendedStats() const {
        Map<String, Variant> ret;
        ret.insert("framesEncoded", Variant((uint64_t)_framesEncoded));
        if(_framesEncoded > 0) {
                ret.insert("avgCompressedSize", Variant((uint64_t)(_totalCompressedBytes / _framesEncoded)));
                if(_totalCompressedBytes > 0) {
                        double ratio = (double)_totalUncompressedBytes / (double)_totalCompressedBytes;
                        ret.insert("compressionRatio", Variant(ratio));
                }
        }
        return ret;
}

PROMEKI_NAMESPACE_END
