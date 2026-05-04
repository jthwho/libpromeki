/**
 * @file      paintengine_packed.cpp
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

// =========================================================================
// DPX 3x10 packed (3 x 10-bit components in a 32-bit word)
//
// Method A (standard): comp0 at bits 31-22, comp1 at 21-12, comp2 at 11-2
// Method B:            comp0 at bits 9-0,   comp1 at 19-10, comp2 at 29-20
// =========================================================================

template <bool MethodB> class PaintEngine_DPX : public PaintEngine::Impl {
                PROMEKI_SHARED_DERIVED(PaintEngine_DPX)
        public:
                static constexpr int MaxCompValue = 1023;

                Buffer::Ptr _plane0;
                Size2Du32   _size;
                uint8_t    *_buf;
                size_t      _stride;
                PixelFormat _pixDesc;
                float       _compOffset[3] = {};
                float       _compScale[3] = {};

                PaintEngine_DPX(const UncompressedVideoPayload &payload)
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

                uint16_t mapComp(uint16_t v, int comp) const {
                        return static_cast<uint16_t>(_compOffset[comp] + v * _compScale[comp] + 0.5f);
                }

                static uint32_t pack(uint16_t c0, uint16_t c1, uint16_t c2) {
                        if constexpr (MethodB)
                                return (c0 & 0x3FF) | ((c1 & 0x3FF) << 10) | ((c2 & 0x3FF) << 20);
                        else
                                return ((c0 & 0x3FF) << 22) | ((c1 & 0x3FF) << 12) | ((c2 & 0x3FF) << 2);
                }

                static void unpack(uint32_t w, uint16_t &c0, uint16_t &c1, uint16_t &c2) {
                        if constexpr (MethodB) {
                                c0 = w & 0x3FF;
                                c1 = (w >> 10) & 0x3FF;
                                c2 = (w >> 20) & 0x3FF;
                        } else {
                                c0 = (w >> 22) & 0x3FF;
                                c1 = (w >> 12) & 0x3FF;
                                c2 = (w >> 2) & 0x3FF;
                        }
                }

                void storeBE(int x, int y, uint32_t w) const {
                        uint8_t *p = _buf + y * _stride + x * 4;
                        p[0] = static_cast<uint8_t>(w >> 24);
                        p[1] = static_cast<uint8_t>(w >> 16);
                        p[2] = static_cast<uint8_t>(w >> 8);
                        p[3] = static_cast<uint8_t>(w);
                }

                uint32_t loadBE(int x, int y) const {
                        const uint8_t *p = _buf + y * _stride + x * 4;
                        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
                }

                PaintEngine::Pixel createPixel(const uint16_t *c, size_t ct) const override {
                        PaintEngine::Pixel ret;
                        uint16_t           c0, c1, c2;
                        if (ct == 1) {
                                c0 = c1 = c2 = mapComp(c[0], 0);
                        } else if (ct >= 3) {
                                c0 = mapComp(c[0], 0);
                                c1 = mapComp(c[1], 1);
                                c2 = mapComp(c[2], 2);
                        } else {
                                return PaintEngine::Pixel();
                        }
                        uint32_t w = pack(c0, c1, c2);
                        std::memcpy(ret.data(), &w, 4);
                        ret.resize(4);
                        return ret;
                }

                void writePixelAt(int x, int y, const PaintEngine::Pixel &pixel) const {
                        uint32_t w;
                        std::memcpy(&w, pixel.data(), 4);
                        storeBE(x, y, w);
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
                        size_t lineBytes = sw * 4;
                        for (int y = 0; y < sh; y++) {
                                const uint8_t *sLine = sbuf + (sy + y) * sStride + sx * 4;
                                uint8_t       *dLine = _buf + (dy + y) * _stride + dx * 4;
                                std::memcpy(dLine, sLine, lineBytes);
                        }
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
                        size_t   ret = 0;
                        uint32_t srcWord;
                        std::memcpy(&srcWord, pixel.data(), 4);
                        uint16_t sr, sg, sb;
                        unpack(srcWord, sr, sg, sb);

                        for (size_t i = 0; i < count; i++) {
                                const Point2Di32 &p = points[i];
                                if (!_size.pointIsInside(p)) continue;
                                float    alpha = alphas[i];
                                float    invAlpha = 1.0f - alpha;
                                uint32_t dstWord = loadBE(p.x(), p.y());
                                uint16_t dr, dg, db;
                                unpack(dstWord, dr, dg, db);
                                dr = static_cast<uint16_t>(sr * alpha + dr * invAlpha);
                                dg = static_cast<uint16_t>(sg * alpha + dg * invAlpha);
                                db = static_cast<uint16_t>(sb * alpha + db * invAlpha);
                                storeBE(p.x(), p.y(), pack(dr, dg, db));
                                ret++;
                        }
                        return ret;
                }

                bool fill(const PaintEngine::Pixel &pixel) const override {
                        int w = _size.width();
                        int h = _size.height();
                        if (w <= 0 || h <= 0) return true;
                        uint32_t val;
                        std::memcpy(&val, pixel.data(), 4);
                        for (int y = 0; y < h; y++)
                                for (int x = 0; x < w; x++) storeBE(x, y, val);
                        return true;
                }

                size_t fillRect(const PaintEngine::Pixel &pixel, const Rect<int32_t> &rect) const override {
                        int rx = rect.x(), ry = rect.y();
                        int rw = rect.width(), rh = rect.height();
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
                        uint32_t val;
                        std::memcpy(&val, pixel.data(), 4);
                        for (int y = ry; y < ry + rh; y++)
                                for (int x = rx; x < rx + rw; x++) storeBE(x, y, val);
                        return rw * rh;
                }

                size_t drawRect(const PaintEngine::Pixel &pixel, const Rect<int32_t> &rect) const override {
                        int rx = rect.x(), ry = rect.y();
                        int rw = rect.width(), rh = rect.height();
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
                                int x0 = lines[li].start().x(), y0 = lines[li].start().y();
                                int x1 = lines[li].end().x(), y1 = lines[li].end().y();
                                int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
                                int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
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
                        int    cx = center.x(), cy = center.y();
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
                                if (err < 0)
                                        err += 2 * y + 1;
                                else {
                                        x--;
                                        err += 2 * (y - x) + 1;
                                }
                        }
                        return count;
                }

                size_t fillEllipse(const PaintEngine::Pixel &pixel, const Point2Di32 &center,
                                   const Size2Du32 &sz) const override {
                        int cx = center.x(), cy = center.y();
                        int rx = sz.width(), ry = sz.height();
                        if (rx == 0 || ry == 0) return 0;
                        int64_t rx2 = static_cast<int64_t>(rx) * rx;
                        int64_t ry2 = static_cast<int64_t>(ry) * ry;
                        size_t  count = 0;
                        for (int y = -ry; y <= ry; y++) {
                                int64_t xR2 = rx2 * (ry2 - static_cast<int64_t>(y) * y);
                                if (xR2 < 0) continue;
                                int xM = static_cast<int>(std::sqrt(static_cast<double>(xR2) / ry2));
                                count += fillSpan(pixel, cy + y, cx - xM, cx + xM + 1);
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
                        uint32_t val;
                        std::memcpy(&val, pixel.data(), 4);
                        for (int x = x0; x < x1; x++) storeBE(x, y, val);
                        return x1 - x0;
                }
};

template class PaintEngine_DPX<false>;
template class PaintEngine_DPX<true>;

PaintEngine createPaintEngine_DPX_A(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_DPX<false>(payload);
}
PaintEngine createPaintEngine_DPX_B(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_DPX<true>(payload);
}

// =========================================================================
// v210 packed 4:2:2  (6 pixels per 16 bytes — 4 x 32-bit LE words)
//
// Each 32-bit word holds 3 x 10-bit values at bits 9:0, 19:10, 29:20.
//
// Word 0: Cb0(9:0)  Y0(19:10) Cr0(29:20)
// Word 1: Y1(9:0)   Cb1(19:10) Y2(29:20)
// Word 2: Cr1(9:0)  Y3(19:10) Cb2(29:20)
// Word 3: Y4(9:0)   Cr2(19:10) Y5(29:20)
// =========================================================================

class PaintEngine_v210 : public PaintEngine::Impl {
                PROMEKI_SHARED_DERIVED(PaintEngine_v210)
        public:
                static constexpr int MaxCompValue = 1023;

                struct V210Addr {
                                int wordIdx;
                                int shift;
                };

                static constexpr V210Addr YAddr[6] = {{0, 10}, {1, 0}, {1, 20}, {2, 10}, {3, 0}, {3, 20}};
                static constexpr V210Addr CbAddr[3] = {{0, 0}, {1, 10}, {2, 20}};
                static constexpr V210Addr CrAddr[3] = {{0, 20}, {2, 0}, {3, 10}};

                Buffer::Ptr _plane0;
                Size2Du32   _size;
                uint8_t    *_buf;
                size_t      _stride;
                PixelFormat _pixDesc;
                float       _compOffset[3] = {};
                float       _compScale[3] = {};

                PaintEngine_v210(const UncompressedVideoPayload &payload)
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

                uint16_t mapComp(uint16_t v, int comp) const {
                        return static_cast<uint16_t>(_compOffset[comp] + v * _compScale[comp] + 0.5f);
                }

                uint32_t *groupBase(int x, int y) const {
                        return reinterpret_cast<uint32_t *>(_buf + y * _stride + (x / 6) * 16);
                }

                static uint16_t readField(const uint32_t *group, const V210Addr &a) {
                        return (group[a.wordIdx] >> a.shift) & 0x3FF;
                }

                static void writeField(uint32_t *group, const V210Addr &a, uint16_t val) {
                        uint32_t mask = ~(uint32_t(0x3FF) << a.shift);
                        group[a.wordIdx] = (group[a.wordIdx] & mask) | ((val & 0x3FF) << a.shift);
                }

                PaintEngine::Pixel createPixel(const uint16_t *c, size_t ct) const override {
                        PaintEngine::Pixel ret;
                        uint16_t          *p = reinterpret_cast<uint16_t *>(ret.data());
                        if (ct == 1) {
                                p[0] = mapComp(c[0], 0);
                                uint16_t neutral = mapComp(32768, 1);
                                p[1] = neutral;
                                p[2] = neutral;
                        } else if (ct >= 3) {
                                p[0] = mapComp(c[0], 0);
                                p[1] = mapComp(c[1], 1);
                                p[2] = mapComp(c[2], 2);
                        } else {
                                return PaintEngine::Pixel();
                        }
                        ret.resize(6);
                        return ret;
                }

                void writePixelAt(int x, int y, const PaintEngine::Pixel &pixel) const {
                        const uint16_t *src = reinterpret_cast<const uint16_t *>(pixel.data());
                        int             pos = x % 6;
                        int             pair = pos / 2;
                        uint32_t       *group = groupBase(x, y);
                        writeField(group, YAddr[pos], src[0]);
                        writeField(group, CbAddr[pair], src[1]);
                        writeField(group, CrAddr[pair], src[2]);
                }

                PaintEngine::Pixel readPixelFrom(const uint8_t *buf, size_t stride, int x, int y) const {
                        PaintEngine::Pixel ret;
                        uint16_t          *p = reinterpret_cast<uint16_t *>(ret.data());
                        const uint32_t    *group = reinterpret_cast<const uint32_t *>(buf + y * stride + (x / 6) * 16);
                        int                pos = x % 6;
                        int                pair = pos / 2;
                        p[0] = readField(group, YAddr[pos]);
                        p[1] = readField(group, CbAddr[pair]);
                        p[2] = readField(group, CrAddr[pair]);
                        ret.resize(6);
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
                        const uint16_t *pdata = reinterpret_cast<const uint16_t *>(pixel.data());
                        for (size_t i = 0; i < count; i++) {
                                const Point2Di32 &p = points[i];
                                if (!_size.pointIsInside(p)) continue;
                                float     alpha = alphas[i];
                                float     invAlpha = 1.0f - alpha;
                                int       pos = p.x() % 6;
                                int       pair = pos / 2;
                                uint32_t *group = groupBase(p.x(), p.y());

                                uint16_t dY = readField(group, YAddr[pos]);
                                uint16_t dCb = readField(group, CbAddr[pair]);
                                uint16_t dCr = readField(group, CrAddr[pair]);
                                dY = static_cast<uint16_t>(pdata[0] * alpha + dY * invAlpha);
                                dCb = static_cast<uint16_t>(pdata[1] * alpha + dCb * invAlpha);
                                dCr = static_cast<uint16_t>(pdata[2] * alpha + dCr * invAlpha);
                                writeField(group, YAddr[pos], dY);
                                writeField(group, CbAddr[pair], dCb);
                                writeField(group, CrAddr[pair], dCr);
                                ret++;
                        }
                        return ret;
                }

                bool fill(const PaintEngine::Pixel &pixel) const override {
                        int w = _size.width();
                        int h = _size.height();
                        if (w <= 0 || h <= 0) return true;
                        for (int y = 0; y < h; y++)
                                for (int x = 0; x < w; x++) writePixelAt(x, y, pixel);
                        return true;
                }

                size_t fillRect(const PaintEngine::Pixel &pixel, const Rect<int32_t> &rect) const override {
                        int rx = rect.x(), ry = rect.y();
                        int rw = rect.width(), rh = rect.height();
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
                        for (int y = ry; y < ry + rh; y++)
                                for (int x = rx; x < rx + rw; x++) writePixelAt(x, y, pixel);
                        return rw * rh;
                }

                size_t drawRect(const PaintEngine::Pixel &pixel, const Rect<int32_t> &rect) const override {
                        int rx = rect.x(), ry = rect.y();
                        int rw = rect.width(), rh = rect.height();
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
                                int x0 = lines[li].start().x(), y0 = lines[li].start().y();
                                int x1 = lines[li].end().x(), y1 = lines[li].end().y();
                                int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
                                int sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
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
                        int    cx = center.x(), cy = center.y();
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
                                if (err < 0)
                                        err += 2 * y + 1;
                                else {
                                        x--;
                                        err += 2 * (y - x) + 1;
                                }
                        }
                        return count;
                }

                size_t fillEllipse(const PaintEngine::Pixel &pixel, const Point2Di32 &center,
                                   const Size2Du32 &sz) const override {
                        int cx = center.x(), cy = center.y();
                        int rx = sz.width(), ry = sz.height();
                        if (rx == 0 || ry == 0) return 0;
                        int64_t rx2 = static_cast<int64_t>(rx) * rx;
                        int64_t ry2 = static_cast<int64_t>(ry) * ry;
                        size_t  count = 0;
                        for (int y = -ry; y <= ry; y++) {
                                int64_t xR2 = rx2 * (ry2 - static_cast<int64_t>(y) * y);
                                if (xR2 < 0) continue;
                                int xM = static_cast<int>(std::sqrt(static_cast<double>(xR2) / ry2));
                                count += fillSpan(pixel, cy + y, cx - xM, cx + xM + 1);
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
};

PaintEngine createPaintEngine_v210(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_v210(payload);
}

PROMEKI_NAMESPACE_END
