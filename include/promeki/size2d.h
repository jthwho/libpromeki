/**
 * @file      size2d.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/point.h>

PROMEKI_NAMESPACE_BEGIN

template<typename T> class Size2DTemplate {
        public:
                Size2DTemplate(const T & width = 0, const T & height = 0) : _width(width), _height(height) {}
                ~Size2DTemplate() {}

                bool isValid() const {
                        return (_width > 0) && (_height > 0);
                }

                void set(const T & w, const T & h) {
                        _width  = w;
                        _height = h;
                        return;
                }

                void setWidth(const T & val) {
                        _width = val;
                        return;
                }

                const T &width() const {
                        return _width;
                }

                void setHeight(const T & val) {
                        _height = val;
                        return;
                }

                const T &height() const {
                        return _height;
                }

                T area() const {
                        return _width * _height;
                }

                String toString() const {
                        return std::to_string(_width) + "x" + std::to_string(_height);
                }

                operator String() const {
                        return toString();
                }

                template <typename N> bool pointIsInside(const Point<N, 2> &p) const {
                        return p.x() >= 0 && 
                               p.x() < _width &&
                               p.y() >= 0 &&
                               p.y() < _height;
                }

                friend std::ostream & operator<<(std::ostream & os, const Size2DTemplate<T> & size) {
                        os << size.toString();
                        return os;
                }

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

using Size2D = Size2DTemplate<size_t>;
using Size2Df = Size2DTemplate<float>;
using Size2Dd = Size2DTemplate<double>;

PROMEKI_NAMESPACE_END

