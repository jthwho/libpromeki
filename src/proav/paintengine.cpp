/**
 * @file      paintengine.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/paintengine.h>
#include <promeki/pixelformat.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

List<Point2Di32> PaintEngine::plotLine(int x1, int y1, int x2, int y2) {
        bool y_longer = false;
        int incval, endval;
        int short_len = y2 - y1;
        int long_len = x2 - x1;

        if(abs(short_len) > abs(long_len)) {
                int swap = short_len;
                short_len = long_len;
                long_len = swap;
                y_longer = true;
        }

        endval = long_len;
        if(long_len < 0) {
                incval = -1;
                long_len = -long_len;
        } else incval = 1;

        List<Point2Di32> pts;
        pts.reserve(abs(endval));
        double decinc = (long_len == 0) ? (double)short_len : (double)short_len / (double)long_len;
        double j = 0.0;
        if(y_longer) {
                for(int i = 0; i != endval; i += incval) {
                        pts += Point2Di32(x1 + (int)j, y1 + i);
                        j += decinc;
                }
        } else {
                for (int i = 0; i != endval; i += incval) {
                        pts += Point2Di32(x1 + i, y1 + (int)j);
                        j += decinc;
                }
        }
        return pts;
}

PaintEngine::Impl::~Impl() {

}

const PixelFormat &PaintEngine::Impl::pixelFormat() const {
        static PixelFormat invalid;
        return invalid;
}

bool PaintEngine::Impl::blit(const Point2Di32 &destTopLeft, const Image &src, const Point2Di32 &srcTopLeft, const Size2Du32 &srcSize) const {
        return false;
}

PaintEngine::Pixel PaintEngine::Impl::createPixel(const uint16_t *comps, size_t compCount) const {
        return PaintEngine::Pixel();
}

size_t PaintEngine::Impl::drawPoints(const Pixel &pixel, const Point2Di32 *points, size_t pointCount) const {
        promekiWarn("%p Failed to draw %d points", this, (int)pointCount);
        return 0;
}

size_t PaintEngine::Impl::compositePoints(const Pixel &pixel, const Point2Di32 *points, const float *alphas, size_t pointCount) const {
        return drawPoints(pixel, points, pointCount);
}

bool PaintEngine::Impl::fill(const Pixel &pixel) const {
        return false;
}

size_t PaintEngine::Impl::drawLines(const Pixel &pixel, const Line2Di32 *lines, size_t lineCount) const {
        List<Point2Di32> points;
        for(size_t i = 0; i < lineCount; i++) {
                const Line2Di32 &line = lines[i];
                points += plotLine(line.start().x(), line.start().y(), line.end().x(), line.end().y());
        }
        size_t ptsdrawn = drawPoints(pixel, points.data(), points.size());
        promekiInfo("drawLines(%d) = %d points, %d drawn", (int)lineCount, (int)points.size(), (int)ptsdrawn);
        return ptsdrawn > 0 ? lineCount : 0;
}

size_t PaintEngine::Impl::drawRect(const Pixel &pixel, const Rect<int32_t> &rect) const {
        int w = rect.width();
        int h = rect.height();
        if(w <= 0 || h <= 0) return 0;
        PointList points;
        int x = rect.x();
        int y = rect.y();
        // Top and bottom edges
        for(int i = x; i < x + w; i++) {
                points.pushToBack(Point2Di32(i, y));
                points.pushToBack(Point2Di32(i, y + h - 1));
        }
        // Left and right edges (excluding corners already drawn)
        for(int j = y + 1; j < y + h - 1; j++) {
                points.pushToBack(Point2Di32(x, j));
                points.pushToBack(Point2Di32(x + w - 1, j));
        }
        return drawPoints(pixel, points.data(), points.size());
}

size_t PaintEngine::Impl::fillRect(const Pixel &pixel, const Rect<int32_t> &rect) const {
        int w = rect.width();
        int h = rect.height();
        if(w <= 0 || h <= 0) return 0;
        PointList points;
        int x = rect.x();
        int y = rect.y();
        points.reserve(w * h);
        for(int j = y; j < y + h; j++) {
                for(int i = x; i < x + w; i++) {
                        points.pushToBack(Point2Di32(i, j));
                }
        }
        return drawPoints(pixel, points.data(), points.size());
}

size_t PaintEngine::Impl::drawCircle(const Pixel &pixel, const Point2Di32 &center, int radius) const {
        // Midpoint circle algorithm
        PointList points;
        int cx = center.x();
        int cy = center.y();
        int x = radius;
        int y = 0;
        int err = 1 - radius;

        while(x >= y) {
                points.pushToBack(Point2Di32(cx + x, cy + y));
                points.pushToBack(Point2Di32(cx - x, cy + y));
                points.pushToBack(Point2Di32(cx + x, cy - y));
                points.pushToBack(Point2Di32(cx - x, cy - y));
                points.pushToBack(Point2Di32(cx + y, cy + x));
                points.pushToBack(Point2Di32(cx - y, cy + x));
                points.pushToBack(Point2Di32(cx + y, cy - x));
                points.pushToBack(Point2Di32(cx - y, cy - x));
                y++;
                if(err < 0) {
                        err += 2 * y + 1;
                } else {
                        x--;
                        err += 2 * (y - x) + 1;
                }
        }
        return drawPoints(pixel, points.data(), points.size());
}

size_t PaintEngine::Impl::fillCircle(const Pixel &pixel, const Point2Di32 &center, int radius) const {
        // Midpoint circle algorithm with horizontal scanline fill
        PointList points;
        int cx = center.x();
        int cy = center.y();
        int x = radius;
        int y = 0;
        int err = 1 - radius;

        while(x >= y) {
                // Draw horizontal lines for each octant pair
                for(int i = cx - x; i <= cx + x; i++) {
                        points.pushToBack(Point2Di32(i, cy + y));
                        points.pushToBack(Point2Di32(i, cy - y));
                }
                for(int i = cx - y; i <= cx + y; i++) {
                        points.pushToBack(Point2Di32(i, cy + x));
                        points.pushToBack(Point2Di32(i, cy - x));
                }
                y++;
                if(err < 0) {
                        err += 2 * y + 1;
                } else {
                        x--;
                        err += 2 * (y - x) + 1;
                }
        }
        return drawPoints(pixel, points.data(), points.size());
}

size_t PaintEngine::Impl::drawEllipse(const Pixel &pixel, const Point2Di32 &center, const Size2Du32 &size) const {
        // Midpoint ellipse algorithm
        PointList points;
        int cx = center.x();
        int cy = center.y();
        int rx = size.width();
        int ry = size.height();
        if(rx == 0 || ry == 0) return 0;

        int64_t rx2 = (int64_t)rx * rx;
        int64_t ry2 = (int64_t)ry * ry;
        int x = 0;
        int y = ry;
        int64_t px = 0;
        int64_t py = 2 * rx2 * y;

        // Region 1
        int64_t p = ry2 - rx2 * ry + rx2 / 4;
        while(px < py) {
                points.pushToBack(Point2Di32(cx + x, cy + y));
                points.pushToBack(Point2Di32(cx - x, cy + y));
                points.pushToBack(Point2Di32(cx + x, cy - y));
                points.pushToBack(Point2Di32(cx - x, cy - y));
                x++;
                px += 2 * ry2;
                if(p < 0) {
                        p += ry2 + px;
                } else {
                        y--;
                        py -= 2 * rx2;
                        p += ry2 + px - py;
                }
        }

        // Region 2
        p = ry2 * (x * 2 + 1) * (x * 2 + 1) / 4 + rx2 * ((int64_t)(y - 1) * (y - 1) - ry2);
        while(y >= 0) {
                points.pushToBack(Point2Di32(cx + x, cy + y));
                points.pushToBack(Point2Di32(cx - x, cy + y));
                points.pushToBack(Point2Di32(cx + x, cy - y));
                points.pushToBack(Point2Di32(cx - x, cy - y));
                y--;
                py -= 2 * rx2;
                if(p > 0) {
                        p += rx2 - py;
                } else {
                        x++;
                        px += 2 * ry2;
                        p += rx2 - py + px;
                }
        }
        return drawPoints(pixel, points.data(), points.size());
}

size_t PaintEngine::Impl::fillEllipse(const Pixel &pixel, const Point2Di32 &center, const Size2Du32 &size) const {
        // Midpoint ellipse algorithm with scanline fill
        PointList points;
        int cx = center.x();
        int cy = center.y();
        int rx = size.width();
        int ry = size.height();
        if(rx == 0 || ry == 0) return 0;

        int64_t rx2 = (int64_t)rx * rx;
        int64_t ry2 = (int64_t)ry * ry;

        // For each scanline y from -ry to +ry, compute x range
        for(int y = -ry; y <= ry; y++) {
                // x^2/rx^2 + y^2/ry^2 <= 1
                // x^2 <= rx^2 * (1 - y^2/ry^2)
                int64_t xRange2 = rx2 * (ry2 - (int64_t)y * y);
                if(xRange2 < 0) continue;
                int xMax = (int)(std::sqrt((double)xRange2 / ry2));
                for(int x = -xMax; x <= xMax; x++) {
                        points.pushToBack(Point2Di32(cx + x, cy + y));
                }
        }
        return drawPoints(pixel, points.data(), points.size());
}

PROMEKI_NAMESPACE_END

