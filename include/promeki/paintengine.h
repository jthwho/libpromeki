/**
 * @file      paintengine.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once
#include <promeki/namespace.h>
#include <promeki/pixelformat.h>
#include <promeki/sharedptr.h>
#include <promeki/array.h>
#include <promeki/list.h>
#include <promeki/size2d.h>
#include <promeki/point.h>
#include <promeki/line.h>
#include <promeki/rect.h>
#include <promeki/color.h>

PROMEKI_NAMESPACE_BEGIN

class Image;
class UncompressedVideoPayload;

/**
 * @brief 2D drawing engine for rendering primitives onto images.
 * @ingroup paint
 *
 * PaintEngine provides a pixel-description-aware interface for drawing points,
 * lines, filling surfaces, and blitting images.  It delegates to a
 * polymorphic Impl that is specific to the underlying pixel description.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance — including any combination of
 * drawing operations on the same target image — must be externally
 * synchronized.
 */
class PaintEngine {
        public:
                /** @brief Shared pointer type for PaintEngine. */
                using Ptr = SharedPtr<PaintEngine>;

                /** @brief Maximum bytes per pixel supported (covers RGBA F32). */
                static constexpr size_t MaxPixelBytes = 16;

                /**
                 * @brief Opaque pixel value with inline fixed-size storage.
                 *
                 * No heap allocation.  Stores up to MaxPixelBytes bytes of
                 * format-specific pixel data.  The @p _size field tracks how
                 * many bytes are meaningful; isEmpty() returns true when no
                 * pixel data has been written (e.g. invalid createPixel call).
                 */
                struct Pixel {
                                uint8_t _data[MaxPixelBytes] = {};
                                uint8_t _size = 0;

                                /** @brief Returns true if the pixel has no data. */
                                bool isEmpty() const { return _size == 0; }

                                /** @brief Returns the number of meaningful bytes. */
                                size_t size() const { return _size; }

                                /** @brief Returns a pointer to the pixel data. */
                                const uint8_t *data() const { return _data; }

                                /** @brief Returns a mutable pointer to the pixel data. */
                                uint8_t *data() { return _data; }

                                /** @brief Element access. */
                                uint8_t &operator[](size_t i) { return _data[i]; }
                                uint8_t  operator[](size_t i) const { return _data[i]; }

                                /** @brief Sets the active byte count. */
                                void resize(size_t n) { _size = static_cast<uint8_t>(n); }
                };

                /** @brief List of 2D points used for batch drawing operations. */
                using PointList = ::promeki::List<Point2Di32>;

                /** @brief List of per-point alpha values used for compositing. */
                using AlphaList = ::promeki::List<float>;

                /**
                 * @brief Abstract implementation backend for PaintEngine.
                 *
                 * Subclass this to provide pixel-description-specific drawing
                 * operations.  The PaintEngine facade delegates every call
                 * to the active Impl instance.
                 */
                class Impl {
                                PROMEKI_SHARED(Impl)
                        public:
                                /** @brief Virtual destructor. */
                                virtual ~Impl();

                                /**
                                 * @brief Blits a rectangular region from a source payload onto the surface.
                                 * @param destTopLeft Top-left corner on the destination surface.
                                 * @param src         Source payload to copy from.
                                 * @param srcTopLeft  Top-left corner of the source region.
                                 * @param srcSize     Size of the source region to copy.
                                 * @return true on success, false on failure.
                                 */
                                virtual bool blit(const Point2Di32 &destTopLeft, const UncompressedVideoPayload &src,
                                                  const Point2Di32 &srcTopLeft, const Size2Du32 &srcSize) const;

                                /**
                                 * @brief Creates a Pixel value from component values.
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
                                virtual size_t drawPoints(const Pixel &pixel, const Point2Di32 *points,
                                                          size_t pointCount) const;

                                /**
                                 * @brief Composites a set of points onto the surface with per-point alpha.
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
                                 * @param pixel The pixel value to draw with.
                                 * @param lines Array of line segments to draw.
                                 * @param count Number of line segments in the array.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t drawLines(const Pixel &pixel, const Line2Di32 *lines,
                                                         size_t count) const;

                                /**
                                 * @brief Draws a rectangle outline.
                                 * @param pixel The pixel value to draw with.
                                 * @param rect  The rectangle to draw.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t drawRect(const Pixel &pixel, const Rect<int32_t> &rect) const;

                                /**
                                 * @brief Fills a rectangle with a solid color.
                                 * @param pixel The pixel value to fill with.
                                 * @param rect  The rectangle to fill.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t fillRect(const Pixel &pixel, const Rect<int32_t> &rect) const;

                                /**
                                 * @brief Draws a circle outline.
                                 * @param pixel  The pixel value to draw with.
                                 * @param center Center of the circle.
                                 * @param radius Radius in pixels.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t drawCircle(const Pixel &pixel, const Point2Di32 &center,
                                                          int radius) const;

                                /**
                                 * @brief Fills a circle with a solid color.
                                 * @param pixel  The pixel value to fill with.
                                 * @param center Center of the circle.
                                 * @param radius Radius in pixels.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t fillCircle(const Pixel &pixel, const Point2Di32 &center,
                                                          int radius) const;

                                /**
                                 * @brief Draws an ellipse outline.
                                 * @param pixel  The pixel value to draw with.
                                 * @param center Center of the ellipse.
                                 * @param size   Half-widths (rx, ry) of the ellipse.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t drawEllipse(const Pixel &pixel, const Point2Di32 &center,
                                                           const Size2Du32 &size) const;

                                /**
                                 * @brief Fills an ellipse with a solid color.
                                 * @param pixel  The pixel value to fill with.
                                 * @param center Center of the ellipse.
                                 * @param size   Half-widths (rx, ry) of the ellipse.
                                 * @return The number of points drawn.
                                 */
                                virtual size_t fillEllipse(const Pixel &pixel, const Point2Di32 &center,
                                                           const Size2Du32 &size) const;

                                /**
                                 * @brief Returns the pixel description associated with this implementation.
                                 * @return The PixelFormat, or an invalid PixelFormat if unset.
                                 */
                                virtual const PixelFormat &pixelFormat() const;

                        protected:
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
                 * @brief Returns the pixel description of the underlying implementation.
                 * @return A const reference to the PixelFormat.
                 */
                const PixelFormat &pixelFormat() const { return d->pixelFormat(); }

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
                Pixel createPixel(uint16_t c1) const { return d->createPixel(&c1, 1); }

                /**
                 * @brief Creates a Pixel from two component values.
                 * @param c1 First component value.
                 * @param c2 Second component value.
                 * @return A Pixel suitable for drawing on this engine.
                 */
                Pixel createPixel(uint16_t c1, uint16_t c2) const {
                        uint16_t data[] = {c1, c2};
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
                        uint16_t data[] = {c1, c2, c3};
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
                        uint16_t data[] = {c1, c2, c3, c4};
                        return d->createPixel(data, 4);
                }

                /**
                 * @brief Creates a Pixel from a Color value.
                 *
                 * Converts the Color to the PaintEngine's color model (from
                 * PixelFormat), then maps each component to a 16-bit value and
                 * delegates to the format-specific createPixel().
                 *
                 * @param color The Color to convert.
                 * @return A Pixel suitable for drawing on this engine.
                 */
                Pixel createPixel(const Color &color) const {
                        const PixelFormat &pd = d->pixelFormat();
                        const ColorModel  &targetModel = pd.isValid() ? pd.colorModel() : ColorModel(ColorModel::sRGB);
                        Color              c = (color.model() == targetModel) ? color : color.convert(targetModel);
                        size_t             count = pd.isValid() ? pd.compCount() : 4;
                        uint16_t           data[PixelMemLayout::MaxComps] = {};
                        for (size_t i = 0; i < count && i < 3; i++) {
                                float v = c.comp(i) * 65535.0f;
                                if (v < 0.0f) v = 0.0f;
                                if (v > 65535.0f) v = 65535.0f;
                                data[i] = static_cast<uint16_t>(v);
                        }
                        if (count >= 4) {
                                float a = c.alpha() * 65535.0f;
                                if (a < 0.0f) a = 0.0f;
                                if (a > 65535.0f) a = 65535.0f;
                                data[3] = static_cast<uint16_t>(a);
                        }
                        return d->createPixel(data, count);
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
                size_t compositePoints(const Pixel &pixel, const Point2Di32 *points, const float *alphas,
                                       size_t pointCount) const {
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
                bool fill(const Pixel &pixel) { return d->fill(pixel); }

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
                 * @brief Blits a rectangular region from a source payload onto the surface.
                 * @param destTopLeft Top-left corner on the destination surface.
                 * @param src         Source payload to copy from.
                 * @param srcTopLeft  Top-left corner of the source region (default: origin).
                 * @param srcSize     Size of the source region (default: entire source).
                 * @return true on success, false on failure.
                 */
                bool blit(const Point2Di32 &destTopLeft, const UncompressedVideoPayload &src,
                          const Point2Di32 &srcTopLeft = Point2Di32(0, 0),
                          const Size2Du32  &srcSize = Size2Du32()) const {
                        return d->blit(destTopLeft, src, srcTopLeft, srcSize);
                }

        private:
                SharedPtr<Impl, false> d;
};

PROMEKI_NAMESPACE_END
