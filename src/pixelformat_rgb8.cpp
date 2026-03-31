/**
 * @file      pixelformat_rgb8.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */
#include <cstring>
#include <promeki/proav/pixelformat.h>
#include <promeki/proav/paintengine.h>
#include <promeki/proav/image.h>

PROMEKI_NAMESPACE_BEGIN

class PaintEngine_RGB8 : public PaintEngine::Impl {
        PROMEKI_SHARED_DERIVED(PaintEngine::Impl, PaintEngine_RGB8)
        public:
                Image           image;
                Size2Du32          size;
                uint8_t         *buf;
                size_t          stride;

                PaintEngine_RGB8(const Image &img) {
                        image = img;
                        size = img.size();
                        buf = static_cast<uint8_t *>(image.plane(0)->data());
                        stride = img.lineStride(0);
                        _pixelFormat = img.pixelFormat();
                }

                PaintEngine::Pixel createPixel(const uint16_t *c, size_t ct) const override {
                        PaintEngine::Pixel ret;
                        ret.resize(3);
                        if(ct == 1) {
                                uint8_t v = (uint8_t)(c[0] >> 8);
                                ret[0] = v;
                                ret[1] = v;
                                ret[2] = v;
                        } else if(ct >= 3) {
                                ret[0] = (uint8_t)(c[0] >> 8);
                                ret[1] = (uint8_t)(c[1] >> 8);
                                ret[2] = (uint8_t)(c[2] >> 8);
                        } else {
                                return PaintEngine::Pixel();
                        }
                        return ret;
                }

                bool fill(const PaintEngine::Pixel &pixel) const override {
                        // First, fill the first line w/ the pixel value
                        uint8_t *p = buf;
                        uint8_t *line0 = buf;
                        for(int i = 0; i < size.width(); i++) {
                                std::memcpy(p, pixel.data(), 3);
                                p += 3;
                        }
                        // Now, fill the rest of the lines from the first.
                        p = buf + stride;
                        for(int i = 1; i < size.height(); i++) {
                                std::memcpy(p, line0, stride);
                                p += stride;
                        }
                        return true;
                }

                size_t drawPoints(const PaintEngine::Pixel &pixel, const Point2Di32 *points, size_t count) const override {
                        size_t ret = 0;
                        for(size_t i = 0; i < count; i++) {
                                const Point2Di32 &p = points[i];
                                if(!size.pointIsInside(p)) continue;
                                uint8_t *pix = buf + (stride * p.y()) + (p.x() * 3);
                                std::memcpy(pix, pixel.data(), 3);
                                ret++;
                        }
                        return ret;
                }

                size_t compositePoints(const PaintEngine::Pixel &pixel, const Point2Di32 *points,
                                const float *alphas, size_t count) const override {
                        size_t ret = 0;
                        const uint8_t *pdata = pixel.data();
                        for(size_t i = 0; i < count; i++) {
                                const Point2Di32 &p = points[i];
                                if(!size.pointIsInside(p)) continue;
                                float alpha = alphas[i];
                                float invAlpha = 1.0 - alpha;
                                uint8_t *pix = buf + (stride * p.y()) + (p.x() * 3);
                                pix[0] = (pdata[0] * alpha) + (pix[0] * invAlpha);
                                pix[1] = (pdata[1] * alpha) + (pix[1] * invAlpha);
                                pix[2] = (pdata[2] * alpha) + (pix[2] * invAlpha);
                                ret++;
                        }
                        return ret;
                }

                bool blit(const Point2Di32 &destTopLeft, const Image &src,
                                const Point2Di32 &srcTopLeft, const Size2Du32 &srcSize) const override {
                        if(src.pixelFormat() != _pixelFormat) return false;

                        const uint8_t *srcBuf = static_cast<const uint8_t *>(src.data());
                        size_t srcStride = src.lineStride();

                        int sx0 = srcTopLeft.x();
                        int sy0 = srcTopLeft.y();
                        if(sx0 < 0 || sy0 < 0 || sx0 >= src.width() || sy0 >= src.height()) return false;

                        int srcW, srcH;
                        if(srcSize.isValid()) {
                                srcW = srcSize.width();
                                srcH = srcSize.height();
                                if(srcW + sx0 > src.width())  srcW = src.width() - sx0;
                                if(srcH + sy0 > src.height()) srcH = src.height() - sy0;
                        } else {
                                srcW = src.width() - sx0;
                                srcH = src.height() - sy0;
                        }

                        int dx = destTopLeft.x();
                        int dy = destTopLeft.y();

                        // Clip against destination
                        if(dx < 0) { sx0 -= dx; srcW += dx; dx = 0; }
                        if(dy < 0) { sy0 -= dy; srcH += dy; dy = 0; }
                        if(dx + srcW > (int)size.width())  srcW = size.width() - dx;
                        if(dy + srcH > (int)size.height()) srcH = size.height() - dy;
                        if(srcW <= 0 || srcH <= 0) return true;

                        int copyBytes = srcW * 3;
                        for(int row = 0; row < srcH; ++row) {
                                const uint8_t *s = srcBuf + (sy0 + row) * srcStride + sx0 * 3;
                                uint8_t *d = buf + (dy + row) * stride + dx * 3;
                                std::memcpy(d, s, copyBytes);
                        }
                        return true;
                }

};

class PixelFormat_RGB8 : public PixelFormat {
        public:
                PixelFormat_RGB8() {
                        _id = RGB8;
                        _name = "RGB8";
                        _desc = "8bit RGB";
                        _sampling = Sampling444;
                        _pixelsPerBlock = 1;
                        _bytesPerBlock = 3;
                        _hasAlpha = false;
                        _fourccList = { "RGB2" };
                        _compList = {
                                { 0, CompRed,   8 },
                                { 0, CompGreen, 8 },
                                { 0, CompBlue,  8 }
                        };
                        _planeList = { { "RGB" } };
                }


                ~PixelFormat_RGB8() {}

                size_t __lineStride(size_t planeIndex, const ImageDesc &desc) const override {
                        size_t lineBytes = desc.width() * 3 + desc.linePad();
                        return PROMEKI_ALIGN_UP(lineBytes, desc.lineAlign());
                }

                size_t __planeSize(size_t planeIndex, const ImageDesc &desc) const override {
                        return __lineStride(planeIndex, desc) * desc.height();
                }

                PaintEngine __createPaintEngine(const Image &img) const override {
                        return new PaintEngine_RGB8(img);
                }

};

PROMEKI_REGISTER_PIXELFORMAT(PixelFormat_RGB8);

PROMEKI_NAMESPACE_END

