/**
 * @file      paintengine_multiplane.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <cmath>
#include <cstring>
#include <promeki/pixelformat.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/paintengine.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/buffer.h>

PROMEKI_NAMESPACE_BEGIN

template <typename CompType, int BitsPerComp>
class PaintEngine_MultiPlane : public PaintEngine::Impl {
        PROMEKI_SHARED_DERIVED(PaintEngine::Impl, PaintEngine_MultiPlane)
        public:
                static constexpr int MaxCompValue = (1 << BitsPerComp) - 1;
                static constexpr int BytesPerComp = sizeof(CompType);

                struct PlaneInfo {
                        uint8_t *data;
                        size_t  stride;
                        size_t  hSub;
                        size_t  vSub;
                        size_t  sampleStride;
                };

                struct CompInfo {
                        int     planeIdx;
                        size_t  byteOffset;
                        float   offset;
                        float   scale;
                };

                // Hold the per-plane Buffer::Ptrs so the payload's
                // backing memory survives for the lifetime of the
                // engine, independent of the payload shared pointer.
                Buffer::Ptr     _planeBufs[PixelMemLayout::MaxPlanes];
                Size2Du32       _size;
                PixelFormat       _pixDesc;
                size_t          _compCount = 0;
                size_t          _planeCount = 0;
                PlaneInfo       _planes[PixelMemLayout::MaxPlanes] = {};
                CompInfo        _comps[PixelMemLayout::MaxComps] = {};

                PaintEngine_MultiPlane(const UncompressedVideoPayload &payload)
                        : _size(payload.desc().size()),
                          _pixDesc(payload.desc().pixelFormat()) {
                        const PixelMemLayout &pf = _pixDesc.memLayout();
                        _compCount = pf.compCount();
                        _planeCount = pf.planeCount();
                        for(size_t i = 0; i < _planeCount; i++) {
                                const auto &pd = pf.data()->planes[i];
                                auto view = payload.plane(i);
                                _planeBufs[i]     = view.buffer();
                                _planes[i].data   = const_cast<uint8_t *>(view.data());
                                _planes[i].stride = _pixDesc.lineStride(i, payload.desc());
                                _planes[i].hSub   = pd.hSubsampling > 0 ? pd.hSubsampling : 1;
                                _planes[i].vSub   = pd.vSubsampling > 0 ? pd.vSubsampling : 1;
                                _planes[i].sampleStride = pd.bytesPerSample > 0
                                        ? pd.bytesPerSample : BytesPerComp;
                        }
                        for(size_t i = 0; i < _compCount; i++) {
                                const auto &cd = pf.data()->comps[i];
                                const auto &cs = _pixDesc.compSemantic(i);
                                _comps[i].planeIdx   = cd.plane;
                                _comps[i].byteOffset = cd.byteOffset;
                                _comps[i].offset     = cs.rangeMin;
                                _comps[i].scale      = (cs.rangeMax > cs.rangeMin)
                                        ? (cs.rangeMax - cs.rangeMin) / 65535.0f
                                        : MaxCompValue / 65535.0f;
                        }
                }

                const PixelFormat &pixelFormat() const override { return _pixDesc; }

                CompType mapComp(uint16_t v, size_t comp) const {
                        return static_cast<CompType>(
                                _comps[comp].offset + v * _comps[comp].scale + 0.5f);
                }

                CompType *compAddr(int x, int y, size_t comp) const {
                        const CompInfo &c = _comps[comp];
                        const PlaneInfo &p = _planes[c.planeIdx];
                        int cx = x / static_cast<int>(p.hSub);
                        int cy = y / static_cast<int>(p.vSub);
                        return reinterpret_cast<CompType *>(
                                p.data + cy * p.stride + cx * p.sampleStride + c.byteOffset);
                }

                PaintEngine::Pixel createPixel(const uint16_t *c, size_t ct) const override {
                        PaintEngine::Pixel ret;
                        CompType *p = reinterpret_cast<CompType *>(ret.data());
                        if(ct == 1) {
                                CompType v = mapComp(c[0], 0);
                                for(size_t i = 0; i < _compCount; i++) p[i] = v;
                        } else if(ct >= _compCount) {
                                for(size_t i = 0; i < _compCount; i++) p[i] = mapComp(c[i], i);
                        } else if(ct >= 3 && _compCount >= 3) {
                                for(size_t i = 0; i < 3; i++) p[i] = mapComp(c[i], i);
                                for(size_t i = 3; i < _compCount; i++) p[i] = MaxCompValue;
                        } else {
                                return PaintEngine::Pixel();
                        }
                        ret.resize(_compCount * BytesPerComp);
                        return ret;
                }

                void writePixelAt(int x, int y, const PaintEngine::Pixel &pixel) const {
                        const CompType *src = reinterpret_cast<const CompType *>(pixel.data());
                        for(size_t i = 0; i < _compCount; i++) {
                                *compAddr(x, y, i) = src[i];
                        }
                }

                PaintEngine::Pixel readPixelAt(const UncompressedVideoPayload &src, int x, int y) const {
                        PaintEngine::Pixel ret;
                        CompType *p = reinterpret_cast<CompType *>(ret.data());
                        const PixelMemLayout &pf = _pixDesc.memLayout();
                        for(size_t i = 0; i < _compCount; i++) {
                                const auto &cd = pf.data()->comps[i];
                                const auto &pd = pf.data()->planes[cd.plane];
                                size_t hSub = pd.hSubsampling > 0 ? pd.hSubsampling : 1;
                                size_t vSub = pd.vSubsampling > 0 ? pd.vSubsampling : 1;
                                size_t ss = pd.bytesPerSample > 0 ? pd.bytesPerSample : BytesPerComp;
                                int cx = x / static_cast<int>(hSub);
                                int cy = y / static_cast<int>(vSub);
                                const uint8_t *base = static_cast<const uint8_t *>(
                                        src.plane(cd.plane).data());
                                size_t stride = _pixDesc.lineStride(cd.plane, src.desc());
                                p[i] = *reinterpret_cast<const CompType *>(
                                        base + cy * stride + cx * ss + cd.byteOffset);
                        }
                        ret.resize(_compCount * BytesPerComp);
                        return ret;
                }

                bool blit(const Point2Di32 &dpt, const UncompressedVideoPayload &src,
                          const Point2Di32 &spt, const Size2Du32 &ssz) const override {
                        if(src.desc().pixelFormat() != _pixDesc) return false;
                        const unsigned int sWpx = src.desc().size().width();
                        const unsigned int sHpx = src.desc().size().height();
                        int sx = spt.x(), sy = spt.y();
                        int dx = dpt.x(), dy = dpt.y();
                        int sw = ssz.isValid() ? ssz.width()  : sWpx - sx;
                        int sh = ssz.isValid() ? ssz.height() : sHpx - sy;
                        int dw = static_cast<int>(_size.width());
                        int dh = static_cast<int>(_size.height());
                        if(sx < 0) { sw += sx; dx -= sx; sx = 0; }
                        if(sy < 0) { sh += sy; dy -= sy; sy = 0; }
                        if(dx < 0) { sw += dx; sx -= dx; dx = 0; }
                        if(dy < 0) { sh += dy; sy -= dy; dy = 0; }
                        if(dx + sw > dw) sw = dw - dx;
                        if(dy + sh > dh) sh = dh - dy;
                        if(sw <= 0 || sh <= 0) return true;
                        for(int y = 0; y < sh; y++)
                                for(int x = 0; x < sw; x++)
                                        writePixelAt(dx + x, dy + y,
                                                     readPixelAt(src, sx + x, sy + y));
                        return true;
                }

                size_t drawPoints(const PaintEngine::Pixel &pixel,
                                  const Point2Di32 *points, size_t count) const override {
                        size_t ret = 0;
                        for(size_t i = 0; i < count; i++) {
                                const Point2Di32 &p = points[i];
                                if(!_size.pointIsInside(p)) continue;
                                writePixelAt(p.x(), p.y(), pixel);
                                ret++;
                        }
                        return ret;
                }

                size_t compositePoints(const PaintEngine::Pixel &pixel,
                                       const Point2Di32 *points,
                                       const float *alphas, size_t count) const override {
                        size_t ret = 0;
                        const CompType *pdata = reinterpret_cast<const CompType *>(pixel.data());
                        for(size_t i = 0; i < count; i++) {
                                const Point2Di32 &pt = points[i];
                                if(!_size.pointIsInside(pt)) continue;
                                float alpha = alphas[i];
                                float invAlpha = 1.0f - alpha;
                                for(size_t c = 0; c < _compCount; c++) {
                                        CompType *dst = compAddr(pt.x(), pt.y(), c);
                                        *dst = static_cast<CompType>(pdata[c] * alpha + *dst * invAlpha);
                                }
                                ret++;
                        }
                        return ret;
                }

                bool fill(const PaintEngine::Pixel &pixel) const override {
                        int w = _size.width();
                        int h = _size.height();
                        if(w <= 0 || h <= 0) return true;
                        fillRectImpl(pixel, 0, 0, w, h);
                        return true;
                }

                size_t fillRect(const PaintEngine::Pixel &pixel,
                                const Rect<int32_t> &rect) const override {
                        int rx = rect.x();
                        int ry = rect.y();
                        int rw = rect.width();
                        int rh = rect.height();
                        if(rw <= 0 || rh <= 0) return 0;
                        int w = static_cast<int>(_size.width());
                        int h = static_cast<int>(_size.height());
                        if(rx < 0) { rw += rx; rx = 0; }
                        if(ry < 0) { rh += ry; ry = 0; }
                        if(rx + rw > w) rw = w - rx;
                        if(ry + rh > h) rh = h - ry;
                        if(rw <= 0 || rh <= 0) return 0;
                        fillRectImpl(pixel, rx, ry, rw, rh);
                        return rw * rh;
                }

                size_t drawRect(const PaintEngine::Pixel &pixel,
                                const Rect<int32_t> &rect) const override {
                        int rx = rect.x();
                        int ry = rect.y();
                        int rw = rect.width();
                        int rh = rect.height();
                        if(rw <= 0 || rh <= 0) return 0;
                        size_t count = 0;
                        int w = static_cast<int>(_size.width());
                        int h = static_cast<int>(_size.height());
                        auto hline = [&](int y, int x0, int x1) {
                                if(y < 0 || y >= h) return;
                                x0 = std::max(x0, 0);
                                x1 = std::min(x1, w);
                                for(int x = x0; x < x1; x++) { writePixelAt(x, y, pixel); count++; }
                        };
                        hline(ry, rx, rx + rw);
                        if(rh > 1) hline(ry + rh - 1, rx, rx + rw);
                        for(int y = ry + 1; y < ry + rh - 1; y++) {
                                if(y < 0 || y >= h) continue;
                                if(rx >= 0 && rx < w) { writePixelAt(rx, y, pixel); count++; }
                                int ex = rx + rw - 1;
                                if(ex >= 0 && ex < w && rw > 1) { writePixelAt(ex, y, pixel); count++; }
                        }
                        return count;
                }

                size_t drawLines(const PaintEngine::Pixel &pixel,
                                 const Line2Di32 *lines, size_t count) const override {
                        int w = static_cast<int>(_size.width());
                        int h = static_cast<int>(_size.height());
                        size_t ret = 0;
                        for(size_t li = 0; li < count; li++) {
                                int x0 = lines[li].start().x();
                                int y0 = lines[li].start().y();
                                int x1 = lines[li].end().x();
                                int y1 = lines[li].end().y();
                                int dx = std::abs(x1 - x0);
                                int dy = -std::abs(y1 - y0);
                                int sx = (x0 < x1) ? 1 : -1;
                                int sy = (y0 < y1) ? 1 : -1;
                                int err = dx + dy;
                                while(true) {
                                        if(x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
                                                writePixelAt(x0, y0, pixel);
                                                ret++;
                                        }
                                        if(x0 == x1 && y0 == y1) break;
                                        int e2 = 2 * err;
                                        if(e2 >= dy) { err += dy; x0 += sx; }
                                        if(e2 <= dx) { err += dx; y0 += sy; }
                                }
                        }
                        return ret;
                }

                size_t fillCircle(const PaintEngine::Pixel &pixel,
                                  const Point2Di32 &center, int radius) const override {
                        int cx = center.x();
                        int cy = center.y();
                        int x = radius, y = 0, err = 1 - radius;
                        size_t count = 0;
                        while(x >= y) {
                                count += fillSpan(pixel, cy + y, cx - x, cx + x + 1);
                                count += fillSpan(pixel, cy - y, cx - x, cx + x + 1);
                                if(x != y) {
                                        count += fillSpan(pixel, cy + x, cx - y, cx + y + 1);
                                        count += fillSpan(pixel, cy - x, cx - y, cx + y + 1);
                                }
                                y++;
                                if(err < 0) { err += 2 * y + 1; } else { x--; err += 2 * (y - x) + 1; }
                        }
                        return count;
                }

                size_t fillEllipse(const PaintEngine::Pixel &pixel,
                                   const Point2Di32 &center, const Size2Du32 &sz) const override {
                        int cx = center.x();
                        int cy = center.y();
                        int rx = sz.width(), ry = sz.height();
                        if(rx == 0 || ry == 0) return 0;
                        int64_t rx2 = static_cast<int64_t>(rx) * rx;
                        int64_t ry2 = static_cast<int64_t>(ry) * ry;
                        size_t count = 0;
                        for(int y = -ry; y <= ry; y++) {
                                int64_t xRange2 = rx2 * (ry2 - static_cast<int64_t>(y) * y);
                                if(xRange2 < 0) continue;
                                int xMax = static_cast<int>(std::sqrt(static_cast<double>(xRange2) / ry2));
                                count += fillSpan(pixel, cy + y, cx - xMax, cx + xMax + 1);
                        }
                        return count;
                }

        private:
                size_t fillSpan(const PaintEngine::Pixel &pixel, int y, int x0, int x1) const {
                        int w = static_cast<int>(_size.width());
                        int h = static_cast<int>(_size.height());
                        if(y < 0 || y >= h) return 0;
                        if(x0 < 0) x0 = 0;
                        if(x1 > w) x1 = w;
                        if(x1 <= x0) return 0;
                        for(int x = x0; x < x1; x++) writePixelAt(x, y, pixel);
                        return x1 - x0;
                }

                void fillRectImpl(const PaintEngine::Pixel &pixel,
                                  int rx, int ry, int rw, int rh) const {
                        const CompType *src = reinterpret_cast<const CompType *>(pixel.data());
                        for(size_t c = 0; c < _compCount; c++) {
                                const CompInfo &ci = _comps[c];
                                const PlaneInfo &pi = _planes[ci.planeIdx];
                                int hSub = static_cast<int>(pi.hSub);
                                int vSub = static_cast<int>(pi.vSub);
                                int cx0 = rx / hSub;
                                int cy0 = ry / vSub;
                                int cx1 = (rx + rw - 1) / hSub + 1;
                                int cy1 = (ry + rh - 1) / vSub + 1;
                                CompType val = src[c];
                                for(int cy = cy0; cy < cy1; cy++) {
                                        uint8_t *row = pi.data + cy * pi.stride;
                                        for(int cx = cx0; cx < cx1; cx++) {
                                                *reinterpret_cast<CompType *>(
                                                        row + cx * pi.sampleStride + ci.byteOffset) = val;
                                        }
                                }
                        }
                }
};

// --- Explicit instantiations ---
template class PaintEngine_MultiPlane<uint8_t,  8>;
template class PaintEngine_MultiPlane<uint16_t, 10>;
template class PaintEngine_MultiPlane<uint16_t, 12>;
template class PaintEngine_MultiPlane<uint16_t, 16>;

// --- Factory functions ---

PaintEngine createPaintEngine_MultiPlane8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_MultiPlane<uint8_t, 8>(payload);
}
PaintEngine createPaintEngine_MultiPlane10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_MultiPlane<uint16_t, 10>(payload);
}
PaintEngine createPaintEngine_MultiPlane12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_MultiPlane<uint16_t, 12>(payload);
}
PaintEngine createPaintEngine_MultiPlane16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_MultiPlane<uint16_t, 16>(payload);
}

PROMEKI_NAMESPACE_END
