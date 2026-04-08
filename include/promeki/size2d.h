/**
 * @file      size2d.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdlib>
#include <cerrno>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/point.h>
#include <promeki/error.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Two-dimensional size (width and height).
 * @ingroup math
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

                /**
                 * @brief Parses a size from a "WxH" string.
                 *
                 * Accepts decimal width and height separated by a
                 * lowercase or uppercase 'x' (e.g. @c "1920x1080" or
                 * @c "1280X720").  Whitespace is not tolerated.
                 *
                 * @param str The string to parse.
                 * @return A @c Result containing the parsed size on
                 *         success, or @c Error::Invalid on a malformed
                 *         input.
                 */
                static Result<Size2DTemplate<T>> fromString(const String &str) {
                        const char *s = str.cstr();
                        if(s == nullptr || *s == '\0') {
                                return makeError<Size2DTemplate<T>>(Error::Invalid);
                        }
                        char *end = nullptr;
                        errno = 0;
                        unsigned long long w = std::strtoull(s, &end, 10);
                        if(end == s || errno != 0) {
                                return makeError<Size2DTemplate<T>>(Error::Invalid);
                        }
                        if(*end != 'x' && *end != 'X') {
                                return makeError<Size2DTemplate<T>>(Error::Invalid);
                        }
                        const char *rest = end + 1;
                        if(*rest == '\0') {
                                return makeError<Size2DTemplate<T>>(Error::Invalid);
                        }
                        errno = 0;
                        unsigned long long h = std::strtoull(rest, &end, 10);
                        if(end == rest || errno != 0 || *end != '\0') {
                                return makeError<Size2DTemplate<T>>(Error::Invalid);
                        }
                        return makeResult(Size2DTemplate<T>(static_cast<T>(w),
                                                            static_cast<T>(h)));
                }

                /** @brief Returns true if both sizes have equal width and height. */
                bool operator==(const Size2DTemplate &other) const {
                        return _width == other._width && _height == other._height;
                }

                /** @brief Returns true if the sizes differ. */
                bool operator!=(const Size2DTemplate &other) const {
                        return !(*this == other);
                }

                /** @brief Returns true if the given 2D point lies within the size bounds. */
                template <typename N> bool pointIsInside(const Point<N, 2> &p) const {
                        return p.x() >= 0 && 
                               p.x() < _width &&
                               p.y() >= 0 &&
                               p.y() < _height;
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

/**
 * @brief @c std::formatter partial specialization for @ref promeki::Size2DTemplate.
 *
 * Class templates need a hand-written partial specialization rather than
 * the @ref PROMEKI_FORMAT_VIA_TOSTRING macro.  Inherits from
 * @ref promeki::ToStringFormatter so the standard string format
 * specifiers (width, fill, alignment) work automatically.
 */
template <typename T>
struct std::formatter<promeki::Size2DTemplate<T>>
        : promeki::ToStringFormatter<promeki::Size2DTemplate<T>> {};

