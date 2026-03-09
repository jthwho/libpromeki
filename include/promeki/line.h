/**
 * @file      line.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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

