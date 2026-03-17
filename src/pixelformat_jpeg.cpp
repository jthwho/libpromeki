/**
 * @file      pixelformat_jpeg.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */
#include <cstring>
#include <promeki/proav/pixelformat.h>
#include <promeki/proav/image.h>

PROMEKI_NAMESPACE_BEGIN

// Base PixelFormat for all JPEG-compressed formats.
//
// Compressed images store the encoded bitstream in a single plane
// buffer.  The allocation size is determined by reading
// Metadata::CompressedSize from the ImageDesc — this is an internal
// detail handled by Image::fromCompressedData().  After construction,
// the buffer's logical size (Buffer::size()) is the authoritative
// compressed byte count, exposed via Image::compressedSize().
//
// lineStride() returns 0 because compressed data is not scanline-
// addressable.  createPaintEngine() returns an invalid PaintEngine
// because drawing on compressed data is not supported — decode first.
class PixelFormat_JPEG : public PixelFormat {
        public:
                PixelFormat_JPEG() {
                    _compressed = true;
                    _fourccList = { "jpeg", "mjpa", "mjpb", "mjpg", "AVRn", "AVDJ", "ADJV", "jpg\x00" };
                };

                virtual ~PixelFormat_JPEG() {}

                size_t __lineStride(size_t planeIndex, const ImageDesc &desc) const override {
                        return 0;
                }

                size_t __planeSize(size_t planeIndex, const ImageDesc &desc) const override {
                        if(!desc.metadata().contains(Metadata::CompressedSize)) return 0;
                        return desc.metadata().get(Metadata::CompressedSize).get<size_t>();
                }

                PaintEngine __createPaintEngine(const Image &img) const override {
                        return PaintEngine();
                }

};

class PixelFormat_JPEG_RGB8 : public PixelFormat_JPEG {
        public:
                PixelFormat_JPEG_RGB8() : PixelFormat_JPEG() {
                        _id = JPEG_RGB8;
                        _name = "JPEG_RGB8";
                        _desc = "JPEG 8bit RGB";
                        _sampling = Sampling444;
                        _pixelsPerBlock = 1;
                        _bytesPerBlock = 3;
                        _hasAlpha = false;
                        _compList = {
                                { 0, CompRed,   8 },
                                { 0, CompGreen, 8 },
                                { 0, CompBlue,  8 }
                        };
                        _planeList = { { "JPEG RGB" } };
                }


                ~PixelFormat_JPEG_RGB8() {}
};
PROMEKI_REGISTER_PIXELFORMAT(PixelFormat_JPEG_RGB8);

class PixelFormat_JPEG_RGBA8 : public PixelFormat_JPEG {
        public:
                PixelFormat_JPEG_RGBA8() : PixelFormat_JPEG() {
                        _id = JPEG_RGBA8;
                        _name = "JPEG_RGBA8";
                        _desc = "JPEG 8bit RGBA";
                        _sampling = Sampling444;
                        _pixelsPerBlock = 1;
                        _bytesPerBlock = 4;
                        _hasAlpha = true;
                        _compList = {
                                { 0, CompRed,   8 },
                                { 0, CompGreen, 8 },
                                { 0, CompBlue,  8 },
                                { 0, CompAlpha, 8 }
                        };
                        _planeList = { { "JPEG RGBA" } };
                }


                ~PixelFormat_JPEG_RGBA8() {}
};
PROMEKI_REGISTER_PIXELFORMAT(PixelFormat_JPEG_RGBA8);

class PixelFormat_JPEG_YUV8_422 : public PixelFormat_JPEG {
        public:
                PixelFormat_JPEG_YUV8_422() : PixelFormat_JPEG() {
                        _id = JPEG_YUV8_422;
                        _name = "JPEG_YUV8_422";
                        _desc = "JPEG 8bit YUV 4:2:2";
                        _sampling = Sampling422;
                        _pixelsPerBlock = 1;
                        _bytesPerBlock = 2;
                        _hasAlpha = false;
                        _compList = {
                                { 0, CompY,  8 },
                                { 0, CompCb, 8 },
                                { 0, CompCr, 8 }
                        };
                        _planeList = { { "JPEG YUV 422" } };
                }


                ~PixelFormat_JPEG_YUV8_422() {}
};
PROMEKI_REGISTER_PIXELFORMAT(PixelFormat_JPEG_YUV8_422);


PROMEKI_NAMESPACE_END

