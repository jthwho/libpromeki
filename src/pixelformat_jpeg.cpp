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

// This is a JPEG PixelFormat base object.  It gets used by other JPEG specialization objects
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
                        _planeList = { { "JPEG YUV 422" } };
                }


                ~PixelFormat_JPEG_YUV8_422() {}
};
PROMEKI_REGISTER_PIXELFORMAT(PixelFormat_JPEG_YUV8_422);


PROMEKI_NAMESPACE_END

