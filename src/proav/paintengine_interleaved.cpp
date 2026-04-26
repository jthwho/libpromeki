/**
 * @file      paintengine_interleaved.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Templated PaintEngine implementation for interleaved pixel formats
 * (RGB/RGBA at 8, 10, 12, and 16 bits per component stored in uint8_t
 * or uint16_t words).  Component ordering (RGBA, BGRA, ARGB, etc.) is
 * a compile-time template parameter for zero-overhead mapping.
 *
 * Key optimizations:
 *   - Component map is a compile-time constant (no runtime array lookup)
 *   - fillRect() uses scanline memcpy instead of per-pixel PointList
 *   - fillCircle()/fillEllipse() use scanline spans via fillRect
 *   - Pixel type uses inline storage (no heap allocation)
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

/**
 * @brief Compile-time component order map.
 *
 * Maps semantic index (R=0, G=1, B=2, A=3) to byte position in the
 * pixel.  Used as a non-type template parameter (C++20 structural type).
 */
struct CompMap {
                int           v[4] = {0, 1, 2, 3};
                constexpr int operator[](int i) const { return v[i]; }
};

// Well-known component orderings
static constexpr CompMap RGBA = {{0, 1, 2, 3}};
static constexpr CompMap BGRA = {{2, 1, 0, 3}};
static constexpr CompMap ARGB = {{1, 2, 3, 0}};
static constexpr CompMap ABGR = {{3, 2, 1, 0}};
static constexpr CompMap RGB = {{0, 1, 2, 0}};
static constexpr CompMap BGR = {{2, 1, 0, 0}};
static constexpr CompMap MONO = {{0, 0, 0, 0}};

/**
 * @brief PaintEngine implementation for interleaved pixel formats.
 *
 * All component-order decisions are resolved at compile time via the
 * Map template parameter.  The compiler constant-folds Map[i] accesses
 * into immediate operands.
 *
 * @tparam CompType    Component storage type (uint8_t or uint16_t).
 * @tparam CompCount   Number of components per pixel (3 or 4).
 * @tparam BitsPerComp Bits per component (8, 10, 12, or 16).
 * @tparam Map         Compile-time component order map.
 */
template <typename CompType, int CompCount, int BitsPerComp, CompMap Map>
class PaintEngine_Interleaved : public PaintEngine::Impl {
                PROMEKI_SHARED_DERIVED(PaintEngine::Impl, PaintEngine_Interleaved)
        public:
                static constexpr int  BytesPerPixel = sizeof(CompType) * CompCount;
                static constexpr int  MaxCompValue = (1 << BitsPerComp) - 1;
                static constexpr int  Shift = 16 - BitsPerComp;
                static constexpr bool HasAlpha = (CompCount == 4);

                // Compile-time component positions
                static constexpr int R = Map[0];
                static constexpr int G = Map[1];
                static constexpr int B = Map[2];
                static constexpr int A = Map[3];

                // Hold a Buffer::Ptr to the single interleaved plane
                // so the payload's backing memory survives the life of
                // the engine, even if the original payload is dropped.
                Buffer::Ptr _plane0;
                Size2Du32   _size;
                uint8_t    *_buf;
                size_t      _stride;
                PixelFormat _pixDesc;
                bool        _rangeMap = false;
                float       _compOffset[CompCount] = {};
                float       _compScale[CompCount] = {};

                PaintEngine_Interleaved(const UncompressedVideoPayload &payload)
                    : _plane0(payload.plane(0).buffer()), _size(payload.desc().size()),
                      _buf(const_cast<uint8_t *>(payload.plane(0).data())),
                      _stride(payload.desc().pixelFormat().lineStride(0, payload.desc())),
                      _pixDesc(payload.desc().pixelFormat()) {
                        for (int i = 0; i < CompCount; i++) {
                                const auto &cs = _pixDesc.compSemantic(i);
                                if (cs.rangeMin != 0.0f || (cs.rangeMax != 0.0f && cs.rangeMax != MaxCompValue)) {
                                        _rangeMap = true;
                                }
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

                        if (_rangeMap) {
                                if (ct == 1) {
                                        CompType v = mapComp(c[0], 0);
                                        for (int i = 0; i < CompCount; i++) p[i] = v;
                                        if constexpr (HasAlpha)
                                                p[A] = static_cast<CompType>(_compOffset[A] + 65535.0f * _compScale[A] +
                                                                             0.5f);
                                } else if (ct >= 3) {
                                        p[R] = mapComp(c[0], 0);
                                        if constexpr (CompCount >= 3) {
                                                p[G] = mapComp(c[1], 1);
                                                p[B] = mapComp(c[2], 2);
                                        }
                                        if constexpr (HasAlpha) {
                                                p[A] = (ct >= 4)
                                                               ? mapComp(c[3], 3)
                                                               : static_cast<CompType>(_compOffset[A] +
                                                                                       65535.0f * _compScale[A] + 0.5f);
                                        }
                                } else {
                                        return PaintEngine::Pixel();
                                }
                        } else {
                                if (ct == 1) {
                                        CompType v = static_cast<CompType>(c[0] >> Shift);
                                        for (int i = 0; i < CompCount; i++) p[i] = v;
                                        if constexpr (HasAlpha) p[A] = MaxCompValue;
                                } else if (ct >= 3) {
                                        p[R] = static_cast<CompType>(c[0] >> Shift);
                                        if constexpr (CompCount >= 3) {
                                                p[G] = static_cast<CompType>(c[1] >> Shift);
                                                p[B] = static_cast<CompType>(c[2] >> Shift);
                                        }
                                        if constexpr (HasAlpha) {
                                                p[A] = (ct >= 4) ? static_cast<CompType>(c[3] >> Shift) : MaxCompValue;
                                        }
                                } else {
                                        return PaintEngine::Pixel();
                                }
                        }
                        ret.resize(BytesPerPixel);
                        return ret;
                }

                // Inline pixel write -- lets the compiler emit a single store
                // instruction for small pixel sizes.
                void writePixel(uint8_t *dst, const PaintEngine::Pixel &pixel) const {
                        if constexpr (BytesPerPixel == 4) {
                                uint32_t v;
                                std::memcpy(&v, pixel.data(), 4);
                                std::memcpy(dst, &v, 4);
                        } else if constexpr (BytesPerPixel == 3) {
                                dst[0] = pixel[0];
                                dst[1] = pixel[1];
                                dst[2] = pixel[2];
                        } else {
                                std::memcpy(dst, pixel.data(), BytesPerPixel);
                        }
                        return;
                }

                // Fill a horizontal span without bounds checking.  Caller
                // guarantees y is in [0, height) and x0/x1 are in [0, width).
                // Returns the number of pixels written (x1 - x0).
                size_t fillSpan(const PaintEngine::Pixel &pixel, int y, int x0, int x1) const {
                        if (x1 <= x0) return 0;
                        uint8_t *line = _buf + y * _stride + x0 * BytesPerPixel;
                        int      count = x1 - x0;

                        // Write first pixel
                        writePixel(line, pixel);

                        if (count > 1) {
                                // Duplicate first pixel across the span via doubling memcpy
                                size_t written = BytesPerPixel;
                                size_t total = static_cast<size_t>(count) * BytesPerPixel;
                                while (written < total) {
                                        size_t chunk = std::min(written, total - written);
                                        std::memcpy(line + written, line, chunk);
                                        written += chunk;
                                }
                        }
                        return count;
                }

                bool fill(const PaintEngine::Pixel &pixel) const override {
                        int w = _size.width();
                        int h = _size.height();
                        if (w <= 0 || h <= 0) return true;

                        // Check for uniform-byte fill (all pixel bytes are the same value)
                        // which enables memset for the entire buffer.
                        bool    uniform = true;
                        uint8_t val = pixel[0];
                        for (int i = 1; i < BytesPerPixel; i++) {
                                if (pixel[i] != val) {
                                        uniform = false;
                                        break;
                                }
                        }

                        if (uniform) {
                                // memset each line (can't memset whole buffer due to stride padding)
                                uint8_t *p = _buf;
                                size_t   lineBytes = static_cast<size_t>(w) * BytesPerPixel;
                                for (int y = 0; y < h; y++) {
                                        std::memset(p, val, lineBytes);
                                        p += _stride;
                                }
                        } else {
                                // Write first line via span fill, then memcpy to remaining
                                fillSpan(pixel, 0, 0, w);
                                uint8_t *p = _buf + _stride;
                                size_t   lineBytes = static_cast<size_t>(w) * BytesPerPixel;
                                for (int y = 1; y < h; y++) {
                                        std::memcpy(p, _buf, lineBytes);
                                        p += _stride;
                                }
                        }
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

                        // Write first span, then memcpy to remaining lines
                        fillSpan(pixel, ry, rx, rx + rw);
                        uint8_t *firstLine = _buf + ry * _stride + rx * BytesPerPixel;
                        size_t   lineBytes = rw * BytesPerPixel;
                        uint8_t *dst = firstLine + _stride;
                        for (int j = 1; j < rh; j++) {
                                std::memcpy(dst, firstLine, lineBytes);
                                dst += _stride;
                        }
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

                        // Top edge
                        if (ry >= 0 && ry < h) {
                                int      x0 = std::max(rx, 0);
                                int      x1 = std::min(rx + rw, w);
                                uint8_t *line = _buf + ry * _stride;
                                for (int x = x0; x < x1; x++) {
                                        writePixel(line + x * BytesPerPixel, pixel);
                                        count++;
                                }
                        }
                        // Bottom edge
                        int by = ry + rh - 1;
                        if (by >= 0 && by < h && rh > 1) {
                                int      x0 = std::max(rx, 0);
                                int      x1 = std::min(rx + rw, w);
                                uint8_t *line = _buf + by * _stride;
                                for (int x = x0; x < x1; x++) {
                                        writePixel(line + x * BytesPerPixel, pixel);
                                        count++;
                                }
                        }
                        // Left and right edges
                        for (int y = ry + 1; y < ry + rh - 1; y++) {
                                if (y < 0 || y >= h) continue;
                                if (rx >= 0 && rx < w) {
                                        writePixel(_buf + y * _stride + rx * BytesPerPixel, pixel);
                                        count++;
                                }
                                int ex = rx + rw - 1;
                                if (ex >= 0 && ex < w && rw > 1) {
                                        writePixel(_buf + y * _stride + ex * BytesPerPixel, pixel);
                                        count++;
                                }
                        }
                        return count;
                }

                // Inline Bresenham line drawing -- writes pixels directly
                // without generating an intermediate PointList.
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
                                                writePixel(_buf + y0 * _stride + x0 * BytesPerPixel, pixel);
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

                size_t drawPoints(const PaintEngine::Pixel &pixel, const Point2Di32 *points,
                                  size_t count) const override {
                        size_t ret = 0;
                        for (size_t i = 0; i < count; i++) {
                                const Point2Di32 &p = points[i];
                                if (!_size.pointIsInside(p)) continue;
                                writePixel(_buf + (_stride * p.y()) + (p.x() * BytesPerPixel), pixel);
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
                                CompType *pix = reinterpret_cast<CompType *>(_buf + (_stride * p.y()) +
                                                                             (p.x() * BytesPerPixel));
                                pix[R] = static_cast<CompType>(pdata[R] * alpha + pix[R] * invAlpha);
                                if constexpr (CompCount >= 3) {
                                        pix[G] = static_cast<CompType>(pdata[G] * alpha + pix[G] * invAlpha);
                                        pix[B] = static_cast<CompType>(pdata[B] * alpha + pix[B] * invAlpha);
                                }
                                if constexpr (HasAlpha) pix[A] = pdata[A];
                                ret++;
                        }
                        return ret;
                }

                // Clip and fill a horizontal span.  Used by fillCircle/fillEllipse
                // to avoid the full Rect clipping overhead of fillRect.
                size_t clippedSpan(const PaintEngine::Pixel &pixel, int y, int x0, int x1) const {
                        int w = static_cast<int>(_size.width());
                        int h = static_cast<int>(_size.height());
                        if (y < 0 || y >= h) return 0;
                        if (x0 < 0) x0 = 0;
                        if (x1 > w) x1 = w;
                        if (x1 <= x0) return 0;
                        return fillSpan(pixel, y, x0, x1);
                }

                size_t fillCircle(const PaintEngine::Pixel &pixel, const Point2Di32 &center,
                                  int radius) const override {
                        int    cx = center.x();
                        int    cy = center.y();
                        int    x = radius;
                        int    y = 0;
                        int    err = 1 - radius;
                        size_t count = 0;

                        while (x >= y) {
                                count += clippedSpan(pixel, cy + y, cx - x, cx + x + 1);
                                count += clippedSpan(pixel, cy - y, cx - x, cx + x + 1);
                                if (x != y) {
                                        count += clippedSpan(pixel, cy + x, cx - y, cx + y + 1);
                                        count += clippedSpan(pixel, cy - x, cx - y, cx + y + 1);
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
                        int rx = sz.width();
                        int ry = sz.height();
                        if (rx == 0 || ry == 0) return 0;

                        int64_t rx2 = static_cast<int64_t>(rx) * rx;
                        int64_t ry2 = static_cast<int64_t>(ry) * ry;
                        size_t  count = 0;

                        for (int y = -ry; y <= ry; y++) {
                                int64_t xRange2 = rx2 * (ry2 - static_cast<int64_t>(y) * y);
                                if (xRange2 < 0) continue;
                                int xMax = static_cast<int>(std::sqrt(static_cast<double>(xRange2) / ry2));
                                count += clippedSpan(pixel, cy + y, cx - xMax, cx + xMax + 1);
                        }
                        return count;
                }

                bool blit(const Point2Di32 &dpt, const UncompressedVideoPayload &src, const Point2Di32 &spt,
                          const Size2Du32 &ssz) const override {
                        if (src.desc().pixelFormat() != _pixDesc) return false;
                        if (src.planeCount() == 0) return false;
                        const uint8_t     *inbuf = static_cast<const uint8_t *>(src.plane(0).data());
                        size_t             srcStride = src.desc().pixelFormat().lineStride(0, src.desc());
                        const unsigned int srcWpx = src.desc().size().width();
                        const unsigned int srcHpx = src.desc().size().height();

                        int destX = dpt.x();
                        int destY = dpt.y();
                        int srcX = spt.x();
                        int srcY = spt.y();

                        int srcWidth, srcHeight;
                        if (ssz.isValid()) {
                                srcWidth = ssz.width();
                                srcHeight = ssz.height();
                        } else {
                                srcWidth = srcWpx - srcX;
                                srcHeight = srcHpx - srcY;
                        }

                        if (srcX < 0) {
                                srcWidth += srcX;
                                destX -= srcX;
                                srcX = 0;
                        }
                        if (srcY < 0) {
                                srcHeight += srcY;
                                destY -= srcY;
                                srcY = 0;
                        }
                        if (srcX + srcWidth > (int)srcWpx) srcWidth = srcWpx - srcX;
                        if (srcY + srcHeight > (int)srcHpx) srcHeight = srcHpx - srcY;

                        if (destX < 0) {
                                srcWidth += destX;
                                srcX -= destX;
                                destX = 0;
                        }
                        if (destY < 0) {
                                srcHeight += destY;
                                srcY -= destY;
                                destY = 0;
                        }
                        if (destX + srcWidth > (int)_size.width()) srcWidth = _size.width() - destX;
                        if (destY + srcHeight > (int)_size.height()) srcHeight = _size.height() - destY;

                        if (srcWidth <= 0 || srcHeight <= 0) return true;

                        size_t         lineBytes = srcWidth * BytesPerPixel;
                        const uint8_t *srcLine = inbuf + srcY * srcStride + srcX * BytesPerPixel;
                        uint8_t       *destLine = _buf + destY * _stride + destX * BytesPerPixel;
                        for (int y = 0; y < srcHeight; ++y) {
                                std::memcpy(destLine, srcLine, lineBytes);
                                destLine += _stride;
                                srcLine += srcStride;
                        }
                        return true;
                }
};

// --- Explicit instantiations ---

// 8-bit RGBA/RGB orderings
template class PaintEngine_Interleaved<uint8_t, 4, 8, RGBA>;
template class PaintEngine_Interleaved<uint8_t, 3, 8, RGB>;
template class PaintEngine_Interleaved<uint8_t, 4, 8, BGRA>;
template class PaintEngine_Interleaved<uint8_t, 3, 8, BGR>;
template class PaintEngine_Interleaved<uint8_t, 4, 8, ARGB>;
template class PaintEngine_Interleaved<uint8_t, 4, 8, ABGR>;

// 10-bit LE (all orderings)
template class PaintEngine_Interleaved<uint16_t, 4, 10, RGBA>;
template class PaintEngine_Interleaved<uint16_t, 3, 10, RGB>;
template class PaintEngine_Interleaved<uint16_t, 4, 10, BGRA>;
template class PaintEngine_Interleaved<uint16_t, 3, 10, BGR>;
template class PaintEngine_Interleaved<uint16_t, 4, 10, ARGB>;
template class PaintEngine_Interleaved<uint16_t, 4, 10, ABGR>;

// 12-bit LE (all orderings)
template class PaintEngine_Interleaved<uint16_t, 4, 12, RGBA>;
template class PaintEngine_Interleaved<uint16_t, 3, 12, RGB>;
template class PaintEngine_Interleaved<uint16_t, 4, 12, BGRA>;
template class PaintEngine_Interleaved<uint16_t, 3, 12, BGR>;
template class PaintEngine_Interleaved<uint16_t, 4, 12, ARGB>;
template class PaintEngine_Interleaved<uint16_t, 4, 12, ABGR>;

// 16-bit LE (all orderings)
template class PaintEngine_Interleaved<uint16_t, 4, 16, RGBA>;
template class PaintEngine_Interleaved<uint16_t, 3, 16, RGB>;
template class PaintEngine_Interleaved<uint16_t, 4, 16, BGRA>;
template class PaintEngine_Interleaved<uint16_t, 3, 16, BGR>;
template class PaintEngine_Interleaved<uint16_t, 4, 16, ARGB>;
template class PaintEngine_Interleaved<uint16_t, 4, 16, ABGR>;

// Monochrome
template class PaintEngine_Interleaved<uint8_t, 1, 8, MONO>;
template class PaintEngine_Interleaved<uint16_t, 1, 10, MONO>;
template class PaintEngine_Interleaved<uint16_t, 1, 12, MONO>;
template class PaintEngine_Interleaved<uint16_t, 1, 16, MONO>;

// --- Factory functions ---

// RGBA/RGB 8-bit
PaintEngine createPaintEngine_RGBA8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint8_t, 4, 8, RGBA>(payload);
}
PaintEngine createPaintEngine_RGB8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint8_t, 3, 8, RGB>(payload);
}

// BGRA/BGR 8-bit
PaintEngine createPaintEngine_BGRA8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint8_t, 4, 8, BGRA>(payload);
}
PaintEngine createPaintEngine_BGR8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint8_t, 3, 8, BGR>(payload);
}

// ARGB/ABGR 8-bit
PaintEngine createPaintEngine_ARGB8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint8_t, 4, 8, ARGB>(payload);
}
PaintEngine createPaintEngine_ABGR8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint8_t, 4, 8, ABGR>(payload);
}

// RGBA/RGB 10/12/16-bit LE
PaintEngine createPaintEngine_RGBA10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 10, RGBA>(payload);
}
PaintEngine createPaintEngine_RGB10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 10, RGB>(payload);
}
PaintEngine createPaintEngine_RGBA12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 12, RGBA>(payload);
}
PaintEngine createPaintEngine_RGB12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 12, RGB>(payload);
}
PaintEngine createPaintEngine_RGBA16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 16, RGBA>(payload);
}
PaintEngine createPaintEngine_RGB16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 16, RGB>(payload);
}

// BGRA/BGR 10/12/16-bit LE
PaintEngine createPaintEngine_BGRA10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 10, BGRA>(payload);
}
PaintEngine createPaintEngine_BGR10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 10, BGR>(payload);
}
PaintEngine createPaintEngine_BGRA12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 12, BGRA>(payload);
}
PaintEngine createPaintEngine_BGR12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 12, BGR>(payload);
}
PaintEngine createPaintEngine_BGRA16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 16, BGRA>(payload);
}
PaintEngine createPaintEngine_BGR16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 16, BGR>(payload);
}

// ARGB 10/12/16-bit LE
PaintEngine createPaintEngine_ARGB10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 10, ARGB>(payload);
}
PaintEngine createPaintEngine_ARGB12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 12, ARGB>(payload);
}
PaintEngine createPaintEngine_ARGB16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 16, ARGB>(payload);
}

// ABGR 10/12/16-bit LE
PaintEngine createPaintEngine_ABGR10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 10, ABGR>(payload);
}
PaintEngine createPaintEngine_ABGR12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 12, ABGR>(payload);
}
PaintEngine createPaintEngine_ABGR16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 4, 16, ABGR>(payload);
}

// YCbCr 4:4:4 LE (same memory layout as RGB, range mapping via CompSemantic)
PaintEngine createPaintEngine_YUV8_444(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint8_t, 3, 8, RGB>(payload);
}
PaintEngine createPaintEngine_YUV10_444_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 10, RGB>(payload);
}
PaintEngine createPaintEngine_YUV12_444_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 12, RGB>(payload);
}
PaintEngine createPaintEngine_YUV16_444_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 3, 16, RGB>(payload);
}

// Monochrome LE
PaintEngine createPaintEngine_Mono8(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint8_t, 1, 8, MONO>(payload);
}
PaintEngine createPaintEngine_Mono10_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 1, 10, MONO>(payload);
}
PaintEngine createPaintEngine_Mono12_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 1, 12, MONO>(payload);
}
PaintEngine createPaintEngine_Mono16_LE(const PixelFormat::Data *, const UncompressedVideoPayload &payload) {
        return new PaintEngine_Interleaved<uint16_t, 1, 16, MONO>(payload);
}

PROMEKI_NAMESPACE_END
