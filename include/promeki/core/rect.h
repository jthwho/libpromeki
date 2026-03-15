/**
 * @file      core/rect.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <algorithm>
#include <promeki/core/namespace.h>
#include <promeki/core/point.h>
#include <promeki/core/size2d.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief 2D rectangle defined by position and size.
 *
 * A simple value type combining a Point<T,2> origin (top-left corner)
 * with a Size2DTemplate<T> extent.  Provides intersection, union,
 * containment, and coordinate transformation operations.
 *
 * @tparam T The component type (e.g. int, float, double).
 */
template <typename T> class Rect {
        public:
                /** @brief Default constructor. Creates an empty rectangle at origin. */
                Rect() = default;

                /** @brief Constructs a rectangle from position and size. */
                Rect(const Point<T, 2> &pos, const Size2DTemplate<T> &size)
                        : _pos(pos), _size(size) {}

                /** @brief Constructs a rectangle from individual components. */
                Rect(T x, T y, T width, T height)
                        : _pos(x, y), _size(width, height) {}

                /** @brief Returns the X coordinate of the left edge. */
                T x() const { return _pos.x(); }

                /** @brief Returns the Y coordinate of the top edge. */
                T y() const { return _pos.y(); }

                /** @brief Returns the width. */
                T width() const { return _size.width(); }

                /** @brief Returns the height. */
                T height() const { return _size.height(); }

                /** @brief Sets the X coordinate. */
                void setX(T val) { _pos.setX(val); }

                /** @brief Sets the Y coordinate. */
                void setY(T val) { _pos.setY(val); }

                /** @brief Sets the width. */
                void setWidth(T val) { _size.setWidth(val); }

                /** @brief Sets the height. */
                void setHeight(T val) { _size.setHeight(val); }

                /** @brief Returns the top-left position. */
                Point<T, 2> topLeft() const { return _pos; }

                /** @brief Returns the top-right position. */
                Point<T, 2> topRight() const { return Point<T, 2>(x() + width(), y()); }

                /** @brief Returns the bottom-left position. */
                Point<T, 2> bottomLeft() const { return Point<T, 2>(x(), y() + height()); }

                /** @brief Returns the bottom-right position. */
                Point<T, 2> bottomRight() const { return Point<T, 2>(x() + width(), y() + height()); }

                /** @brief Returns the center point. */
                Point<T, 2> center() const { return Point<T, 2>(x() + width() / 2, y() + height() / 2); }

                /** @brief Returns the position (top-left). */
                const Point<T, 2> &pos() const { return _pos; }

                /** @brief Returns the size. */
                const Size2DTemplate<T> &size() const { return _size; }

                /** @brief Sets the position. */
                void setPos(const Point<T, 2> &p) { _pos = p; }

                /** @brief Sets the size. */
                void setSize(const Size2DTemplate<T> &s) { _size = s; }

                /** @brief Returns true if both width and height are greater than zero. */
                bool isValid() const { return _size.isValid(); }

                /** @brief Returns true if width or height is zero or negative. */
                bool isEmpty() const { return !isValid(); }

                /** @brief Returns true if the given point is inside this rectangle. */
                bool contains(const Point<T, 2> &p) const {
                        return p.x() >= x() && p.x() < x() + width() &&
                               p.y() >= y() && p.y() < y() + height();
                }

                /** @brief Returns true if the given rectangle is entirely inside this rectangle. */
                bool contains(const Rect &r) const {
                        return r.x() >= x() && r.x() + r.width() <= x() + width() &&
                               r.y() >= y() && r.y() + r.height() <= y() + height();
                }

                /** @brief Returns true if the given rectangle overlaps this rectangle. */
                bool intersects(const Rect &r) const {
                        return x() < r.x() + r.width() && x() + width() > r.x() &&
                               y() < r.y() + r.height() && y() + height() > r.y();
                }

                /** @brief Returns the intersection of this rectangle with another. */
                Rect intersected(const Rect &r) const {
                        T ix = std::max(x(), r.x());
                        T iy = std::max(y(), r.y());
                        T ix2 = std::min(x() + width(), r.x() + r.width());
                        T iy2 = std::min(y() + height(), r.y() + r.height());
                        if(ix2 <= ix || iy2 <= iy) return Rect();
                        return Rect(ix, iy, ix2 - ix, iy2 - iy);
                }

                /** @brief Returns the smallest rectangle containing both this and another. */
                Rect united(const Rect &r) const {
                        if(isEmpty()) return r;
                        if(r.isEmpty()) return *this;
                        T ux = std::min(x(), r.x());
                        T uy = std::min(y(), r.y());
                        T ux2 = std::max(x() + width(), r.x() + r.width());
                        T uy2 = std::max(y() + height(), r.y() + r.height());
                        return Rect(ux, uy, ux2 - ux, uy2 - uy);
                }

                /** @brief Returns a rectangle adjusted by the given deltas. */
                Rect adjusted(T dx1, T dy1, T dx2, T dy2) const {
                        return Rect(x() + dx1, y() + dy1,
                                    width() + dx2 - dx1, height() + dy2 - dy1);
                }

                /** @brief Returns a rectangle translated by dx, dy. */
                Rect translated(T dx, T dy) const {
                        return Rect(x() + dx, y() + dy, width(), height());
                }

                /** @brief Equality operator. */
                bool operator==(const Rect &other) const {
                        return _pos == other._pos && _size.width() == other._size.width() &&
                               _size.height() == other._size.height();
                }

                /** @brief Inequality operator. */
                bool operator!=(const Rect &other) const {
                        return !(*this == other);
                }

                /** @brief Stream output operator. */
                friend std::ostream &operator<<(std::ostream &os, const Rect &r) {
                        os << r.x() << "," << r.y() << " " << r.width() << "x" << r.height();
                        return os;
                }

        private:
                Point<T, 2>            _pos;
                Size2DTemplate<T>      _size;
};

/** @brief 2D rectangle with int32_t components. */
using Rect2Di32 = Rect<int32_t>;
/** @brief 2D rectangle with float components. */
using Rect2Df = Rect<float>;
/** @brief 2D rectangle with double components. */
using Rect2Dd = Rect<double>;

PROMEKI_NAMESPACE_END
