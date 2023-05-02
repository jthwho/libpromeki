/*****************************************************************************
 * size2d.h
 * April 27, 2023
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

#include <promeki/string.h>
namespace promeki {

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

                const T & width() const {
                        return _width;
                }

                void setHeight(const T & val) {
                        _height = val;
                        return;
                }

                const T & height() const {
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

}   // namespace promeki

