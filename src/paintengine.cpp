/**
 * @file      paintengine.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/paintengine.h>
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

//virtual void drawLines(const Pixel &pixel, const Line2Di32 *lines, size_t count);
//virtual void drawRect(const QRect &rect);
//virtual void drawFilledRect(const QRect &rect);
//virtual void drawCircle(const QPoint &pt, int radius);
//virtual void drawFilledCircle(const QPoint &pt, int radius);
//virtual void drawEllipse(const QPoint &center, const QSize &size);
//virtual void drawFilledEllipse(const QPoint &center, const QSize &size);


PROMEKI_NAMESPACE_END

