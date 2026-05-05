/**
 * @file      paintengine_422.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <promeki/pixelformat.h>
#include <promeki/paintengine.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/buffer.h>

PROMEKI_NAMESPACE_BEGIN

template <typename CompType, int BitsPerComp, bool IsUYVY> class PaintEngine_422 : public PaintEngine::Impl {
                PROMEKI_SHARED_DERIVED(PaintEngine_422)
        public:
                static constexpr int BytesPerComp = sizeof(CompType);
                static constexpr int BytesPerMacro = 4 * BytesPerComp;
                static constexpr int MaxCompValue = (1 << BitsPerComp) - 1;

                static constexpr int Y0_off = IsUYVY ? BytesPerComp : 0;
                static constexpr int Y1_off = IsUYVY ? 3 * BytesPerComp : 2 * BytesPerComp;
                static constexpr int Cb_off = IsUYVY ? 0 : BytesPerComp;
                static constexpr int Cr_off = IsUYVY ? 2 * BytesPerComp : 3 * BytesPerComp;

                Buffer _plane0;
                Size2Du32   _size;
                uint8_t    *_buf;
                size_t      _stride;
                PixelFormat _pixDesc;
                float       _compOffset[3] = {};
                float       _compScale[3] = {};

                PaintEngine_422(const UncompressedVideoPayload &payload)
                    : _plane0(payload.plane(0).buffer()), _size(payload.desc().size()),
                      _buf(const_cast<uint8_t *>(payload.plane(0).data())),
                      _stride(payload.desc().pixelFormat().lineStride(0, payload.desc())),
                      _pixDesc(payload.desc().pixelFormat()) {
                        for (int i = 0; i < 3; i++) {
                                const auto &cs = _pixDesc.compSemantic(i);
                                _compOffset[i] = cs.rangeMin;
                                _compScale[i] = (cs.rangeMax > cs.rangeMin) ? (cs.rangeMax - cs.rangeMin) / 65535.0f
                                                                            : MaxCompValue / 65535.0f;
                        }
                }

                const PixelFormat &pixelFormat() const override { return _pixDesc; }

                CompType mapComp(uint16_t v, int comp) const {
                        return static_cast<CompType>(_compOffset[comp] + v * _compScale[comp] + 0.5f);
                }

                PaintEngine::Pixel createPixel(const uint16_t *c, size_t ct) const override {
                        PaintEngine::Pixel ret;
                        CompType          *p = reinterpret_cast<CompType *>(ret.data());
                        if (ct == 1) {
                                p[0] = mapComp(c[0], 0);
                                CompType neutral = mapComp(32768, 1);
                                p[1] = neutral;
                                p[2] = neutral;
                        } else if (ct >= 3) {
                                p[0] = mapComp(c[0], 0);
                                p[1] = mapComp(c[1], 1);
                                p[2] = mapComp(c[2], 2);
                        } else {
                                return PaintEngine::Pixel();
                        }
                        ret.resize(3 * BytesPerComp);
                        return ret;
                }

                void writePixelAt(int x, int y, const PaintEngine::Pixel &pixel) const {
                        const CompType *src = reinterpret_cast<const CompType *>(pixel.data());
                        uint8_t        *macro = _buf + y * _stride + (x / 2) * BytesPerMacro;
                        *reinterpret_cast<CompType *>(macro + ((x & 1) ? Y1_off : Y0_off)) = src[0];
                        *reinterpret_cast<CompType *>(macro + Cb_off) = src[1];
                        *reinterpret_cast<CompType *>(macro + Cr_off) = src[2];
                }

                PaintEngine::Pixel readPixelFrom(const uint8_t *buf, size_t stride, int x, int y) const {
                        PaintEngine::Pixel ret;
                        CompType          *p = reinterpret_cast<CompType *>(ret.data());
                        const uint8_t     *macro = buf + y * stride + (x / 2) * BytesPerMacro;
                        p[0] = *reinterpret_cast<const CompType *>(macro + ((x & 1) ? Y1_off : Y0_off));
                        p[1] = *reinterpret_cast<const CompType *>(macro + Cb_off);
                        p[2] = *reinterpret_cast<const CompType *>(macro + Cr_off);
                        ret.resize(3 * BytesPerComp);
                        return ret;
                }

                bool blit(const Point2Di32 &dpt, const UncompressedVideoPayload &src, const Point2Di32 &spt,
                          const Size2Du32 &ssz) const override {
                        if (src.desc().pixelFormat() != _pixDesc) return false;
                        if (src.planeCount() == 0) return false;
                        const uint8_t     *sbuf = static_cast<const uint8_t *>(src.plane(0).data());
                        size_t             sStride = src.desc().pixelFormat().lineStride(0, src.desc());
                        const unsigned int sWpx = src.desc().size().width();
                        const unsigned int sHpx = src.desc().size().height();
                        int                sx = spt.x(), sy = spt.y();
                        int                dx = dpt.x(), dy = dpt.y();
                        int                sw = ssz.isValid() ? ssz.width() : sWpx - sx;
                        int                sh = ssz.isValid() ? ssz.height() : sHpx - sy;
                        int                dw = static_cast<int>(_size.width());
                        int                dh = static_cast<int>(_size.height());
                        if (sx < 0) {
                                sw += sx;
                                dx -= sx;
                                sx = 0;
                        }
                        if (sy < 0) {
                                sh += sy;
                                dy -= sy;
                                sy = 0;
                        }
                        if (dx < 0) {
                                sw += dx;
                                sx -= dx;
                                dx = 0;
                        }
                        if (dy < 0) {
                                sh += dy;
                                sy -= dy;
                                dy = 0;
                        }
                        if (dx + sw > dw) sw = dw - dx;
                        if (dy + sh > dh) sh = dh - dy;
                        if (sw <= 0 || sh <= 0) return true;
                        for (int y = 0; y < sh; y++)
                                for (int x = 0; x < sw; x++)
                                        writePixelAt(dx + x, dy + y, readPixelFrom(sbuf, sStride, sx + x, sy + y));
                        return true;
                }

                size_t drawPoints(const PaintEngine::Pixel &pixel, const Point2Di32 *points,
                                  size_t count) const override {
                        size_t ret = 0;
                        for (size_t i = 0; i < count; i++) {
                                const Point2Di32 &p = points[i];
                                if (!_size.pointIsInside(p)) continue;
                                writePixelAt(p.x(), p.y(), pixel);
                                ret++;
                        }
                        return ret;
                }

                size_t compositePoints(const PaintEngine::Pixel &pixel, const Point2Di32 *points, const float *alphas,
                                       size_t count) const override {
                        size_t          ret = 0;
                        const CompType *pdata = reinterpret_cast<const CompType *>(pixel.data());
                        for (size_t i = 0; i < count; i++) {
                                const Point2Di32 &p = points[i];
                                if (!_size.pointIsInside(p)) continue;
                                float     alpha = alphas[i];
                                float     invAlpha = 1.0f - alpha;
                                uint8_t  *macro = _buf + p.y() * _stride + (p.x() / 2) * BytesPerMacro;
                                CompType *yPtr = reinterpret_cast<CompType *>(macro + ((p.x() & 1) ? Y1_off : Y0_off));
                                CompType *cbPtr = reinterpret_cast<CompType *>(macro + Cb_off);
                                CompType *crPtr = reinterpret_cast<CompType *>(macro + Cr_off);
                                *yPtr = static_cast<CompType>(pdata[0] * alpha + *yPtr * invAlpha);
                                *cbPtr = static_cast<CompType>(pdata[1] * alpha + *cbPtr * invAlpha);
                                *crPtr = static_cast<CompType>(pdata[2] * alpha + *crPtr * invAlpha);
                                ret++;
                        }
                        return ret;
                }

                bool fill(const PaintEngine::Pixel &pixel) const override {
                        int w = _size.width();
                        int h = _size.height();
                        if (w <= 0 || h <= 0) return true;
                        fillRectImpl(pixel, 0, 0, w, h);
                        return true;
                }

                size_t fillRect(const PaintEngine::Pixel &pixel, const Rect<int32_t> &rect) const override {
                        int rx = rect.x();
                        int ry = rect.y();
                        int rw = rect.width();
                        int rh = rect.height();
                        if (rw <= 0 || rh <= 0) return 0;
                        int w = static_cast<int>(_size.width());
                        int h = static_cast<int>(_size.height());
                        if (rx < 0) {
                                rw += rx;
                                rx = 0;
                        }
                        if (ry < 0) {
                                rh += ry;
                                ry = 0;
                        }
                        if (rx + rw > w) rw = w - rx;
                        if (ry + rh > h) rh = h - ry;
                        if (rw <= 0 || rh <= 0) return 0;
                        fillRectImpl(pixel, rx, ry, rw, rh);
                        return rw * rh;
                }

                size_t drawRect(const PaintEngine::Pixel &pixel, const Rect<int32_t> &rect) const override {
                        int rx = rect.x();
                        int ry = rect.y();
                        int rw = rect.width();
                        int rh = rect.height();
                        if (rw <= 0 || rh <= 0) return 0;
                        size_t count = 0;
                        int    w = static_cast<int>(_size.width());
                        int    h = static_cast<int>(_size.height());
                        auto   hline = [&](int y, int x0, int x1) {
                                if (y < 0 || y >= h) return;
                                x0 = std::max(x0, 0);
                                x1 = std::min(x1, w);
                                for (int x = x0; x < x1; x++) {
                                        writePixelAt(x, y, pixel);
                                        count++;
                                }
                        };
                        hline(ry, rx, rx + rw);
                        if (rh > 1) hline(ry + rh - 1, rx, rx + rw);
                        for (int y = ry + 1; y < ry + rh - 1; y++) {
                                if (y < 0 || y >= h) continue;
                                if (rx >= 0 && rx < w) {
                                        writePixelAt(rx, y, pixel);
                                        count++;
                                }
                                int ex = rx + rw - 1;
                                if (ex >= 0 && ex < w && rw > 1) {
                                        writePixelAt(ex, y, pixel);
                                        count++;
                                }
                        }
                        return count;
                }

                size_t drawLines(const PaintEngine::Pixel &pixel, const Line2Di32 *lines, size_t count) const override {
                        int    w = static_cast<int>(_size.width());
                        int    h = static_cast<int>(_size.height());
                        size_t ret = 0;
                        for (size_t li = 0; li < count; li++) {
                                int x0 = lines[li].start().x();
                                int y0 = lines[li].start().y();
                                int x1 = lines[li].end().x();
                                int y1 = lines[li].end().y();
                                int dx = std::abs(x1 - x0);
                                int dy = -std::abs(y1 - y0);
                                int sx = (x0 < x1) ? 1 : -1;
                                int sy = (y0 < y1) ? 1 : -1;
                                int err = dx + dy;
                                while (true) {
                                        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
                                                writePixelAt(x0, y0, pixel);
                                                ret++;
                                        }
                                        if (x0 == x1 && y0 == y1) break;
                                        int e2 = 2 * err;
                                        if (e2 >= dy) {
                                                err += dy;
                                                x0 += sx;
                                        }
                                        if (e2 <= dx) {
                                                err += dx;
                                                y0 += sy;
                                        }
                                }
                        }
                        return ret;
                }

                size_t fillCircle(const PaintEngine::Pixel &pixel, const Point2Di32 &center,
                                  int radius) const override {
                        int    cx = center.x();
                        int    cy = center.y();
                        int    x = radius, y = 0, err = 1 - radius;
                        size_t count = 0;
                        while (x >= y) {
                                count += fillSpan(pixel, cy + y, cx - x, cx + x + 1);
                                count += fillSpan(pixel, cy - y, cx - x, cx + x + 1);
                                if (x != y) {
                                        count += fillSpan(pixel, cy + x, cx - y, cx + y + 1);
                                        count += fillSpan(pixel, cy - x, cx - y, cx + y + 1);
                                }
                                y++;
                                if (err < 0) {
                                        err += 2 * y + 1;
                                } else {
                                        x--;
                                        err += 2 * (y - x) + 1;
                                }
                        }
                        return count;
                }

                size_t fillEllipse(const PaintEngine::Pixel &pixel, const Point2Di32 &center,
                                   const Size2Du32 &sz) const override {
                        int cx = center.x();
                        int cy = center.y();
                        int rx = sz.width(), ry = sz.height();
                        if (rx == 0 || ry == 0) return 0;
                        int64_t rx2 = static_cast<int64_t>(rx) * rx;
                        int64_t ry2 = static_cast<int64_t>(ry) * ry;
                        size_t  count = 0;
                        for (int y = -ry; y <= ry; y++) {
                                int64_t xRange2 = rx2 * (ry2 - static_cast<int64_t>(y) * y);
                                if (xRange2 < 0) continue;
                                int xMax = static_cast<int>(std::sqrt(static_cast<double>(xRange2) / ry2));
                                count += fillSpan(pixel, cy + y, cx - xMax, cx + xMax + 1);
                        }
                        return count;
                }

        private:
                size_t fillSpan(const PaintEngine::Pixel &pixel, int y, int x0, int x1) const {
                        int w = static_cast<int>(_size.width());
                        int h = static_cast<int>(_size.height());
                        if (y < 0 || y >= h) return 0;
                        if (x0 < 0) x0 = 0;
                        if (x1 > w) x1 = w;
                        if (x1 <= x0) return 0;
                        for (int x = x0; x < x1; x++) writePixelAt(x, y, pixel);
                        return x1 - x0;
                }

                void fillRectImpl(const PaintEngine::Pixel &pixel, int rx, int ry, int rw, int rh) const {
                        const CompType *src = reinterpret_cast<const CompType *>(pixel.data());
                        CompType        yVal = src[0];
                        CompType        cbVal = src[1];
                        CompType        crVal = src[2];

                        for (int y = ry; y < ry + rh; y++) {
                                uint8_t *line = _buf + y * _stride;
                                for (int x = rx; x < rx + rw; x++) {
                                        uint8_t *macro = line + (x / 2) * BytesPerMacro;
                                        *reinterpret_cast<CompType *>(macro + ((x & 1) ? Y1_off : Y0_off)) = yVal;
                                        *reinterpret_cast<CompType *>(macro + Cb_off) = cbVal;
                                        *reinterpret_cast<CompType *>(macro + Cr_off) = crVal;
                                }
                        }
                }
};

// --- Explicit instantiations ---

// YUYV
template class PaintEngine_422<uint8_t, 8, false>;
template class PaintEngine_422<uint16_t, 10, false>;

// UYVY
template class PaintEngine_422<uint8_t, 8, true>;
template class PaintEngine_422<uint16_t, 10, true>;
template class PaintEngine_422<uint16_t, 12, true>;
template class PaintEngine_422<uint16_t, 16, true>;

// --- Factory functions ---

PaintEngine createPaintEngine_YUYV8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_422<uint8_t, 8, false>(payload);
}
PaintEngine createPaintEngine_YUYV10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_422<uint16_t, 10, false>(payload);
}
PaintEngine createPaintEngine_UYVY8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_422<uint8_t, 8, true>(payload);
}
PaintEngine createPaintEngine_UYVY10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_422<uint16_t, 10, true>(payload);
}
PaintEngine createPaintEngine_UYVY12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_422<uint16_t, 12, true>(payload);
}
PaintEngine createPaintEngine_UYVY16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_422<uint16_t, 16, true>(payload);
}

PROMEKI_NAMESPACE_END
