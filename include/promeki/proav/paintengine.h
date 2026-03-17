/**
 * @file      proav/paintengine.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once
#include <promeki/core/namespace.h>
#include <promeki/proav/pixelformat.h>
#include <promeki/core/sharedptr.h>
#include <promeki/core/list.h>
#include <promeki/core/size2d.h>
#include <promeki/core/point.h>
#include <promeki/core/line.h>
#include <promeki/core/rect.h>

PROMEKI_NAMESPACE_BEGIN

class Image;

/**
 * @brief 2D drawing engine for rendering primitives onto images.
 * @ingroup proav_paint
 *
 * PaintEngine provides a pixel-format-aware interface for drawing points,
 * lines, filling surfaces, and blitting images.  It delegates to a
 * polymorphic Impl that is specific to the underlying pixel format.
 */
class PaintEngine {
	public:
                /** @brief Shared pointer type for PaintEngine. */
                using Ptr = SharedPtr<PaintEngine>;

                /** @brief Opaque pixel value optimized for the underlying format. */
                using Pixel = List<uint8_t>;

                /** @brief List of 2D points used for batch drawing operations. */
                using PointList = List<Point2Di32>;

                /** @brief List of per-point alpha values used for compositing. */
                using AlphaList = List<float>;

                /**
                 * @brief Abstract implementation backend for PaintEngine.
                 *
                 * Subclass this to provide pixel-format-specific drawing
                 * operations.  The PaintEngine facade delegates every call
                 * to the active Impl instance.
                 */
                class Impl {
                        PROMEKI_SHARED(Impl)
                        public:
                                /** @brief Virtual destructor. */
                                virtual ~Impl();

                                /**
                                 * @brief Returns the pixel format used by this implementation.
                                 * @return Pointer to the PixelFormat, or nullptr if unset.
                                 */
                                const PixelFormat *pixelFormat() const { return _pixelFormat; }

                                /**
                                 * @brief Blits a rectangular region from a source image onto the surface.
                                 * @param destTopLeft Top-left corner on the destination surface.
                                 * @param src         Source image to copy from.
                                 * @param srcTopLeft  Top-left corner of the source region.
                                 * @param srcSize     Size of the source region to copy.
                                 * @return true on success, false on failure.
                                 */
                                virtual bool blit(const Point2Di32 &destTopLeft, const Image &src,
                                                const Point2Di32 &srcTopLeft, const Size2Du32 &srcSize) const;

                                /**
                                 * @brief Creates a Pixel value from component values.
                                 *
                                 * The returned Pixel is in a format specific to the underlying
                                 * data layout and may contain pre-packed component values for
                                 * fast drawing.  Do not modify the returned Pixel unless you
                                 * know the underlying packing format.
                                 *
                                 * @param comps     Array of component values.
                                 * @param compCount Number of components in the array.
                                 * @return A Pixel suitable for use with this engine's draw calls.
                                 */
                                virtual Pixel createPixel(const uint16_t *comps, size_t compCount) const;

                                /**
                                 * @brief Draws a set of points onto the surface.
                                 * @param pixel      The pixel value to draw.
                                 * @param points     Array of points to draw.
                                 * @param pointCount Number of points in the array.
                                 * @return The number of points actually drawn.
                                 */
                                virtual size_t drawPoints(const Pixel &pixel, const Point2Di32 *points, size_t pointCount) const;

                                /**
                                 * @brief Composites a set of points onto the surface with per-point alpha.
                                 *
                                 * The default implementation simply calls drawPoints() without
                                 * compositing.  Reimplement in your surface-specific subclass
                                 * to support true alpha compositing.
                                 *
                                 * @param pixel      The pixel value to composite.
                                 * @param points     Array of points to composite.
                                 * @param alphas     Array of alpha values, one per point.
                                 * @param pointCount Number of points in the arrays.
                                 * @return The number of points actually composited.
                                 */
                                virtual size_t compositePoints(const Pixel &pixel, const Point2Di32 *points,
                                                const float *alphas, size_t pointCount) const;

                                /**
                                 * @brief Fills the entire surface with a single pixel value.
                                 * @param pixel The pixel value to fill with.
                                 * @return true on success, false on failure.
                                 */
                                virtual bool fill(const Pixel &pixel) const;

                                /**
                                 * @brief Draws a set of line segments onto the surface.
                                 *
                                 * The default implementation rasterizes each line into a point
                                 * list and calls drawPoints().  Reimplement for a natively
                                 * faster line-drawing path.
                                 *
                                 * @param pixel The pixel value to draw with.
                                 * @param lines Array of line segments to draw.
                                 * @param count Number of line segments in the array.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t drawLines(const Pixel &pixel, const Line2Di32 *lines, size_t count) const;

                                /**
                                 * @brief Draws a rectangle outline.
                                 *
                                 * Default implementation rasterizes the outline into
                                 * a point list and calls drawPoints().
                                 *
                                 * @param pixel The pixel value to draw with.
                                 * @param rect  The rectangle to draw.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t drawRect(const Pixel &pixel, const Rect<int32_t> &rect) const;

                                /**
                                 * @brief Fills a rectangle with a solid color.
                                 *
                                 * Default implementation rasterizes all interior points.
                                 *
                                 * @param pixel The pixel value to fill with.
                                 * @param rect  The rectangle to fill.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t fillRect(const Pixel &pixel, const Rect<int32_t> &rect) const;

                                /**
                                 * @brief Draws a circle outline.
                                 *
                                 * Default implementation uses the midpoint circle algorithm.
                                 *
                                 * @param pixel  The pixel value to draw with.
                                 * @param center Center of the circle.
                                 * @param radius Radius in pixels.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t drawCircle(const Pixel &pixel, const Point2Di32 &center, int radius) const;

                                /**
                                 * @brief Fills a circle with a solid color.
                                 *
                                 * Default implementation uses the midpoint circle algorithm
                                 * with horizontal scanline fill.
                                 *
                                 * @param pixel  The pixel value to fill with.
                                 * @param center Center of the circle.
                                 * @param radius Radius in pixels.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t fillCircle(const Pixel &pixel, const Point2Di32 &center, int radius) const;

                                /**
                                 * @brief Draws an ellipse outline.
                                 *
                                 * Default implementation uses the midpoint ellipse algorithm.
                                 *
                                 * @param pixel  The pixel value to draw with.
                                 * @param center Center of the ellipse.
                                 * @param size   Half-widths (rx, ry) of the ellipse.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t drawEllipse(const Pixel &pixel, const Point2Di32 &center, const Size2Du32 &size) const;

                                /**
                                 * @brief Fills an ellipse with a solid color.
                                 *
                                 * Default implementation uses the midpoint ellipse algorithm
                                 * with horizontal scanline fill.
                                 *
                                 * @param pixel  The pixel value to fill with.
                                 * @param center Center of the ellipse.
                                 * @param size   Half-widths (rx, ry) of the ellipse.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t fillEllipse(const Pixel &pixel, const Point2Di32 &center, const Size2Du32 &size) const;

                        protected:
                                /** @brief Pixel format for this implementation. */
                                const PixelFormat       *_pixelFormat = nullptr;
                };

                /**
                 * @brief Plots a line using Bresenham's algorithm.
                 * @param x1 X coordinate of the start point.
                 * @param y1 Y coordinate of the start point.
                 * @param x2 X coordinate of the end point.
                 * @param y2 Y coordinate of the end point.
                 * @return A list of points along the rasterized line.
                 */
                static PointList plotLine(int x1, int y1, int x2, int y2);

                /** @brief Constructs a PaintEngine with a default (no-op) implementation. */
                PaintEngine() : d(SharedPtr<Impl, false>::create()) {};

                /**
                 * @brief Constructs a PaintEngine that takes ownership of the given Impl.
                 * @param impl Pointer to a heap-allocated Impl subclass.
                 */
                PaintEngine(Impl *impl) : d(SharedPtr<Impl, false>::takeOwnership(impl)) {}

                /**
                 * @brief Returns the pixel format of the underlying implementation.
                 * @return Pointer to the PixelFormat.
                 */
                const PixelFormat *pixelFormat() const {
                        return d->pixelFormat();
                }

                /**
                 * @brief Creates a Pixel from an array of component values.
                 * @param comps     Array of component values.
                 * @param compCount Number of components in the array.
                 * @return A Pixel suitable for drawing on this engine.
                 */
                Pixel createPixel(const uint16_t *comps, size_t compCount) const {
                        return d->createPixel(comps, compCount);
                }

                /**
                 * @brief Creates a Pixel from a single component value.
                 * @param c1 The component value.
                 * @return A Pixel suitable for drawing on this engine.
                 */
                Pixel createPixel(uint16_t c1) const {
                        return d->createPixel(&c1, 1);
                }

                /**
                 * @brief Creates a Pixel from two component values.
                 * @param c1 First component value.
                 * @param c2 Second component value.
                 * @return A Pixel suitable for drawing on this engine.
                 */
                Pixel createPixel(uint16_t c1, uint16_t c2) const {
                        uint16_t data[] = { c1, c2 };
                        return d->createPixel(data, 2);
                }

                /**
                 * @brief Creates a Pixel from three component values.
                 * @param c1 First component value.
                 * @param c2 Second component value.
                 * @param c3 Third component value.
                 * @return A Pixel suitable for drawing on this engine.
                 */
                Pixel createPixel(uint16_t c1, uint16_t c2, uint16_t c3) const {
                        uint16_t data[] = { c1, c2, c3 };
                        return d->createPixel(data, 3);
                }

                /**
                 * @brief Creates a Pixel from four component values.
                 * @param c1 First component value.
                 * @param c2 Second component value.
                 * @param c3 Third component value.
                 * @param c4 Fourth component value.
                 * @return A Pixel suitable for drawing on this engine.
                 */
                Pixel createPixel(uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4) const {
                        uint16_t data[] = { c1, c2, c3, c4 };
                        return d->createPixel(data, 4);
                }

                /**
                 * @brief Draws points onto the surface.
                 * @param pixel      The pixel value to draw.
                 * @param points     Array of points to draw.
                 * @param pointCount Number of points in the array.
                 * @return The number of points actually drawn.
                 */
                size_t drawPoints(const Pixel &pixel, const Point2Di32 *points, size_t pointCount) const {
                        return d->drawPoints(pixel, points, pointCount);
                }

                /**
                 * @brief Draws points from a PointList onto the surface.
                 * @param pixel  The pixel value to draw.
                 * @param points List of points to draw.
                 * @return The number of points actually drawn.
                 */
                size_t drawPoints(const Pixel &pixel, const PointList &points) const {
                        return d->drawPoints(pixel, points.data(), points.size());
                }

                /**
                 * @brief Composites points onto the surface with per-point alpha.
                 * @param pixel      The pixel value to composite.
                 * @param points     Array of points.
                 * @param alphas     Array of alpha values, one per point.
                 * @param pointCount Number of points in the arrays.
                 * @return The number of points actually composited.
                 */
                size_t compositePoints(const Pixel &pixel, const Point2Di32 *points, const float *alphas, size_t pointCount) const {
                        return d->compositePoints(pixel, points, alphas, pointCount);
                }

                /**
                 * @brief Composites points from lists onto the surface with per-point alpha.
                 * @param pixel  The pixel value to composite.
                 * @param points List of points.
                 * @param alphas List of alpha values, one per point.
                 * @return The number of points actually composited.
                 */
                size_t compositePoints(const Pixel &pixel, const PointList &points, const AlphaList &alphas) const {
                        return d->compositePoints(pixel, points.data(), alphas.data(), points.size());
                }

                /**
                 * @brief Draws multiple line segments onto the surface.
                 * @param pixel     The pixel value to draw with.
                 * @param lines     Array of line segments.
                 * @param lineCount Number of line segments in the array.
                 * @return The number of points drawn.
                 */
                size_t drawLines(const Pixel &pixel, const Line2Di32 *lines, size_t lineCount) const {
                        return d->drawLines(pixel, lines, lineCount);
                }

                /**
                 * @brief Draws a single line segment onto the surface.
                 * @param pixel The pixel value to draw with.
                 * @param line  The line segment to draw.
                 * @return The number of points drawn.
                 */
                size_t drawLine(const Pixel &pixel, const Line2Di32 &line) const {
                        return d->drawLines(pixel, &line, 1);
                }

                /**
                 * @brief Draws a single line segment specified by endpoint coordinates.
                 * @param pixel The pixel value to draw with.
                 * @param x1    X coordinate of the start point.
                 * @param y1    Y coordinate of the start point.
                 * @param x2    X coordinate of the end point.
                 * @param y2    Y coordinate of the end point.
                 * @return The number of points drawn.
                 */
                size_t drawLine(const Pixel &pixel, int x1, int y1, int x2, int y2) const {
                        Line2Di32 line(Point2Di32(x1, y1), Point2Di32(x2, y2));
                        return d->drawLines(pixel, &line, 1);
                }

                /**
                 * @brief Fills the entire surface with a single pixel value.
                 * @param pixel The pixel value to fill with.
                 * @return true on success, false on failure.
                 */
                bool fill(const Pixel &pixel) {
                        return d->fill(pixel);
                }

                /**
                 * @brief Draws a rectangle outline.
                 * @param pixel The pixel value to draw with.
                 * @param rect  The rectangle to draw.
                 * @return The number of points drawn.
                 */
                size_t drawRect(const Pixel &pixel, const Rect<int32_t> &rect) const {
                        return d->drawRect(pixel, rect);
                }

                /**
                 * @brief Fills a rectangle with a solid color.
                 * @param pixel The pixel value to fill with.
                 * @param rect  The rectangle to fill.
                 * @return The number of points drawn.
                 */
                size_t fillRect(const Pixel &pixel, const Rect<int32_t> &rect) const {
                        return d->fillRect(pixel, rect);
                }

                /**
                 * @brief Draws a circle outline.
                 * @param pixel  The pixel value to draw with.
                 * @param center Center of the circle.
                 * @param radius Radius in pixels.
                 * @return The number of points drawn.
                 */
                size_t drawCircle(const Pixel &pixel, const Point2Di32 &center, int radius) const {
                        return d->drawCircle(pixel, center, radius);
                }

                /**
                 * @brief Fills a circle with a solid color.
                 * @param pixel  The pixel value to fill with.
                 * @param center Center of the circle.
                 * @param radius Radius in pixels.
                 * @return The number of points drawn.
                 */
                size_t fillCircle(const Pixel &pixel, const Point2Di32 &center, int radius) const {
                        return d->fillCircle(pixel, center, radius);
                }

                /**
                 * @brief Draws an ellipse outline.
                 * @param pixel  The pixel value to draw with.
                 * @param center Center of the ellipse.
                 * @param size   Half-widths (rx, ry) of the ellipse.
                 * @return The number of points drawn.
                 */
                size_t drawEllipse(const Pixel &pixel, const Point2Di32 &center, const Size2Du32 &size) const {
                        return d->drawEllipse(pixel, center, size);
                }

                /**
                 * @brief Fills an ellipse with a solid color.
                 * @param pixel  The pixel value to fill with.
                 * @param center Center of the ellipse.
                 * @param size   Half-widths (rx, ry) of the ellipse.
                 * @return The number of points drawn.
                 */
                size_t fillEllipse(const Pixel &pixel, const Point2Di32 &center, const Size2Du32 &size) const {
                        return d->fillEllipse(pixel, center, size);
                }

                /**
                 * @brief Blits a rectangular region from a source image onto the surface.
                 * @param destTopLeft Top-left corner on the destination surface.
                 * @param src         Source image to copy from.
                 * @param srcTopLeft  Top-left corner of the source region (default: origin).
                 * @param srcSize     Size of the source region (default: entire source).
                 * @return true on success, false on failure.
                 */
                bool blit(const Point2Di32 &destTopLeft, const Image &src,
                          const Point2Di32 &srcTopLeft = Point2Di32(0, 0), const Size2Du32 &srcSize = Size2Du32()) const {
                        return d->blit(destTopLeft, src, srcTopLeft, srcSize);
                }


        private:
                SharedPtr<Impl, false> d;
};

PROMEKI_NAMESPACE_END

