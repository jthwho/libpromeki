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

namespace promeki {

template <typename T>
class Size2D {
        public:
                Size2D(const T &width = 0, const T &height = 0) :
                        _width(width), _height(height) { }
                ~Size2D() { }

                bool isValid() const {
                        return _width > 0 && _height > 0;
                }

                void set(const T &w, const T &h) {
                        _width = w;
                        _height = h;
                        return;
                }

                void setWidth(const T &val) {
                        _width = val;
                        return;
                }

                const T &width() const {
                        return _width;
                }

                void setHeight(const T &val) {
                        _height = val;
                        return;
                }

                const T &height() const {
                        return _height;
                }

                T area() const {
                        return _width * _height;
                }

        private:
                T       _width = 0;
                T       _height = 0;
};

using Size2Di = Size2D<int>;
using Size2Df = Size2D<float>;
using Size2Dd = Size2D<double>;

} // namespace promeki

