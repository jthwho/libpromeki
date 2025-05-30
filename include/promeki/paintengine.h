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
#include <promeki/pixelformat.h>
#include <promeki/shareddata.h>
#include <promeki/list.h>
#include <promeki/size2d.h>
#include <promeki/point.h>
#include <promeki/line.h>

PROMEKI_NAMESPACE_BEGIN

class Image;

class PaintEngine {
	public:
                using Pixel = List<uint8_t>;
                using PointList = List<Point2D>;
                using AlphaList = List<float>;

                class Impl : public SharedData {
                        public:
                                virtual ~Impl();

                                // Returns the pixel format used by this PaintEngine
                                const PixelFormat *pixelFormat() const { return _pixelFormat; }

                                // Blits from the src buffer to the surface.
                                virtual bool blit(const Point2D &destTopLeft, const Image &src, 
                                                const Point2D &srcTopLeft, const Size2D &srcSize) const;

                                // Creates a pixel for this underlying data format of the paint engine.  This format is meant
                                // to be specific to the underlying data format and may contain extra versions of the pixel
                                // componenets in the various packings to make drawing the pixel a fast operation.  Don't 
                                // modify the returned Pixel object unless you know the underlying packing format of the
                                // Pixel itself.
                                virtual Pixel createPixel(const uint16_t *comps, size_t compCount) const;

                                // Draws a set of points to the underlying surface.
                                virtual size_t drawPoints(const Pixel &pixel, const Point2D *points, size_t pointCount) const;

                                // Composites a set of points to the underlying surface.  The default version of this will just
                                // call drawPoints and not do any compositing.  It's up to the given surface implementation to
                                // reimplement if compositing should be a supported operation.
                                virtual size_t compositePoints(const Pixel &pixel, const Point2D *points, 
                                                const float *alphas, size_t pointCount) const;

                                // Fills the entire surface with a pixel.
                                virtual bool fill(const Pixel &pixel) const;

                                // Draws lines.  The default behavior is to render the lines into a point list and call
                                // drawPoints.  You can reimplement this for your surface if this can be made a natively
                                // faster operation.
                                virtual size_t drawLines(const Pixel &pixel, const Line2D *lines, size_t count) const;

                                //virtual void drawRect(const QRect &rect);
                                //virtual void drawFilledRect(const QRect &rect);
                                //virtual void drawCircle(const QPoint &pt, int radius);
                                //virtual void drawFilledCircle(const QPoint &pt, int radius);
                                //virtual void drawEllipse(const QPoint &center, const QSize &size);
                                //virtual void drawFilledEllipse(const QPoint &center, const QSize &size);
                                
                        protected:
                                const PixelFormat       *_pixelFormat = nullptr;
                };

                static PointList plotLine(int x1, int y1, int x2, int y2);

                PaintEngine() : d(new Impl) {};
                PaintEngine(Impl *impl) : d(impl) {}

                const PixelFormat *pixelFormat() const {
                        return d->pixelFormat();
                }

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

                size_t drawPoints(const Pixel &pixel, const PointList &points) const {
                        return d->drawPoints(pixel, points.data(), points.size());
                }

                size_t compositePoints(const Pixel &pixel, const Point2D *points, const float *alphas, size_t pointCount) const {
                        return d->compositePoints(pixel, points, alphas, pointCount);
                }

                size_t compositePoints(const Pixel &pixel, const PointList &points, const AlphaList &alphas) const {
                        return d->compositePoints(pixel, points.data(), alphas.data(), points.size());
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

                bool blit(const Point2D &destTopLeft, const Image &src, 
                          const Point2D &srcTopLeft = Point2D(0, 0), const Size2D &srcSize = Size2D()) const {
                        return d->blit(destTopLeft, src, srcTopLeft, srcSize);
                }


        private:
                ExplicitSharedDataPtr<Impl> d;
};

PROMEKI_NAMESPACE_END

