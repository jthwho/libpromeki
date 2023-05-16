/*****************************************************************************
 * paintengine.h
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

#pragma once
#include <promeki/namespace.h>
#include <promeki/shareddata.h>
#include <promeki/list.h>
#include <promeki/point.h>
#include <promeki/line.h>

PROMEKI_NAMESPACE_BEGIN

class PaintEngine {
	public:
                using Pixel = List<uint8_t>;

                class Impl : public SharedData {
                        public:
                                virtual ~Impl();

                                virtual Pixel createPixel(const uint16_t *comps, size_t compCount) const;         
                                virtual size_t drawPoints(const Pixel &pixel, const Point2D *points, size_t pointCount) const;
                                virtual bool fill(const Pixel &pixel) const;
                                virtual size_t drawLines(const Pixel &pixel, const Line2D *lines, size_t count) const;
                                //virtual void drawRect(const QRect &rect);
                                //virtual void drawFilledRect(const QRect &rect);
                                //virtual void drawCircle(const QPoint &pt, int radius);
                                //virtual void drawFilledCircle(const QPoint &pt, int radius);
                                //virtual void drawEllipse(const QPoint &center, const QSize &size);
                                //virtual void drawFilledEllipse(const QPoint &center, const QSize &size);
                };

                static List<Point2D> plotLine(int x1, int y1, int x2, int y2);

                PaintEngine() : d(new Impl) {};
                PaintEngine(Impl *impl) : d(impl) {}

                Pixel createPixel(const uint16_t *comps, size_t compCount) const {
                        return d->createPixel(comps, compCount);
                }

                Pixel createPixel(uint16_t c1) const {
                        return d->createPixel(&c1, 1);
                }

                Pixel createPixel(uint16_t c1, uint16_t c2) const {
                        uint16_t data[] = { c1, c2 };
                        return d->createPixel(data, 2);
                }

                Pixel createPixel(uint16_t c1, uint16_t c2, uint16_t c3) const {
                        uint16_t data[] = { c1, c2, c3 };
                        return d->createPixel(data, 3);
                }

                Pixel createPixel(uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4) const {
                        uint16_t data[] = { c1, c2, c3, c4 };
                        return d->createPixel(data, 4);
                }

                size_t drawPoints(const Pixel &pixel, const Point2D *points, size_t pointCount) const {
                        return d->drawPoints(pixel, points, pointCount);
                }

                size_t drawLines(const Pixel &pixel, const Line2D *lines, size_t lineCount) const {
                        return d->drawLines(pixel, lines, lineCount);
                }

                size_t drawLine(const Pixel &pixel, const Line2D &line) const {
                        return d->drawLines(pixel, &line, 1);
                }

                size_t drawLine(const Pixel &pixel, int x1, int y1, int x2, int y2) const {
                        Line2D line(Point2D(x1, y1), Point2D(x2, y2));
                        return d->drawLines(pixel, &line, 1);
                }

                bool fill(const Pixel &pixel) {
                        return d->fill(pixel);
                }

        private:
                ExplicitSharedDataPtr<Impl> d;
};

PROMEKI_NAMESPACE_END

