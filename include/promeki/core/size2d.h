/**
 * @file      core/size2d.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/point.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Two-dimensional size (width and height).
 * @ingroup core_math
 *
 * A simple value type representing a 2D extent. Supports validity checks,
 * area computation, point containment tests, and string serialization in
 *
 * @par Example
 * @code
 * Size2Du32 hd(1920, 1080);
 * uint32_t w = hd.width();   // 1920
 * uint32_t h = hd.height();  // 1080
 * String s = hd.toString();  // "1920x1080"
 * @endcode
 * "WxH" format.
 *
 * @tparam T The component type (e.g. size_t, float, double).
 */
template<typename T> class Size2DTemplate {
        public:
                /** @brief Constructs a 2D size with the given width and height, defaulting to 0x0. */
                Size2DTemplate(const T & width = 0, const T & height = 0) : _width(width), _height(height) {}

                /** @brief Destructor. */
                ~Size2DTemplate() {}

                /** @brief Returns true if both width and height are greater than zero. */
                bool isValid() const {
                        return (_width > 0) && (_height > 0);
                }

                /** @brief Sets both width and height. */
                void set(const T & w, const T & h) {
                        _width  = w;
                        _height = h;
                        return;
                }

                /** @brief Sets the width. */
                void setWidth(const T & val) {
                        _width = val;
                        return;
                }

                /** @brief Returns the width. */
                const T &width() const {
                        return _width;
                }

                /** @brief Sets the height. */
                void setHeight(const T & val) {
                        _height = val;
                        return;
                }

                /** @brief Returns the height. */
                const T &height() const {
                        return _height;
                }

                /** @brief Returns the area (width * height). */
                T area() const {
                        return _width * _height;
                }

                /** @brief Returns the size as a string in "WxH" format. */
                String toString() const {
                        return std::to_string(_width) + "x" + std::to_string(_height);
                }

                /** @brief Converts to a String using toString(). */
                operator String() const {
                        return toString();
                }

                /** @brief Returns true if the given 2D point lies within the size bounds. */
                template <typename N> bool pointIsInside(const Point<N, 2> &p) const {
                        return p.x() >= 0 && 
                               p.x() < _width &&
                               p.y() >= 0 &&
                               p.y() < _height;
                }

                /** @brief Stream output operator. */
                friend std::ostream & operator<<(std::ostream & os, const Size2DTemplate<T> & size) {
                        os << size.toString();
                        return os;
                }

                /** @brief Stream input operator, parses "WxH" format. */
                friend std::istream & operator>>(std::istream & input, Size2DTemplate<T> &s) {
                        char x;
                        T    w, h;
                        input >> std::ws >> w >> x >> h;
                        if(input.fail() || x != 'x') {
                                input.setstate(std::ios::failbit);
                        } else {
                                s._width  = w;
                                s._height = h;
                        }
                        return input;
                }

        private:
                T _width  = 0;
                T _height = 0;
};

/** @brief 2D size with int32_t components. */
using Size2Di32 = Size2DTemplate<int32_t>;
/** @brief 2D size with uint32_t components. */
using Size2Du32 = Size2DTemplate<uint32_t>;
/** @brief 2D size with float components. */
using Size2Df = Size2DTemplate<float>;
/** @brief 2D size with double components. */
using Size2Dd = Size2DTemplate<double>;

PROMEKI_NAMESPACE_END

