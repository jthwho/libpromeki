/*****************************************************************************
 * pixelformat_rgb8.cpp
 * May 15, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/
#include <cstring>
#include <promeki/pixelformat.h>
#include <promeki/paintengine.h>
#include <promeki/image.h>

PROMEKI_NAMESPACE_BEGIN

class PaintEngine_RGB8 : public PaintEngine::Impl {
        public:
                Image           image;
                Size2D          size;
                uint8_t         *buf;
                size_t          stride;

                PaintEngine_RGB8(const Image &img) {
                        image = img;
                        size = img.size();
                        buf = static_cast<uint8_t *>(image.plane(0).data());
                        stride = img.lineStride(0);
                        _pixelFormat = img.pixelFormat();
                }

                PaintEngine::Pixel createPixel(const uint16_t *c, size_t ct) const override {
                        PaintEngine::Pixel ret;
                        ret.resize(3);
                        if(ct == 1) {
                                ret[0] = c[0];
                                ret[1] = c[0];
                                ret[2] = c[0];
                        } else if(ct >= 3) {
                                ret[0] = c[0];
                                ret[1] = c[1];
                                ret[2] = c[2];
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

                size_t drawPoints(const PaintEngine::Pixel &pixel, const Point2D *points, size_t count) const override {
                        size_t ret = 0;
                        for(size_t i = 0; i < count; i++) {
                                const Point2D &p = points[i];
                                if(!size.pointIsInside(p)) continue;
                                uint8_t *pix = buf + (stride * p.y()) + (p.x() * 3);
                                std::memcpy(pix, pixel.data(), 3);
                                ret++;
                        }
                        return ret;
                }

                size_t compositePoints(const PaintEngine::Pixel &pixel, const Point2D *points, 
                                const float *alphas, size_t count) const override {
                        size_t ret = 0;
                        const uint8_t *pdata = pixel.data();
                        for(size_t i = 0; i < count; i++) {
                                const Point2D &p = points[i];
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

