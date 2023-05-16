/*****************************************************************************
 * line.h
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
#include <promeki/point.h>

PROMEKI_NAMESPACE_BEGIN

template <typename T, size_t N> class Line {
        public:
                using Pt = Point<T, N>;
                Line() = default;
                Line(const Pt &s, const Pt &e) : _start(s), _end(e) {};
                Line(const Pt &&s, const Pt &&e) : _start(std::move(s)), _end(std::move(e)) {}

                const Pt &start() const {
                        return _start;
                }

                const Pt &end() const {
                        return _end;
                }

        private:
                Pt      _start;
                Pt      _end;
};

using Line2D = Line<int, 2>;
using Line2Df = Line<float, 2>;
using Line2Dd = Line<double, 2>;
using Line3D = Line<int, 3>;
using Line3Df = Line<float, 3>;
using Line3Dd = Line<double, 3>;
using Line4D = Line<int, 4>;
using Line4Df = Line<float, 4>;
using Line4Dd = Line<double, 4>;

PROMEKI_NAMESPACE_END

