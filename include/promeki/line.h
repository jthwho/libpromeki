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

/**
 * @brief N-dimensional line segment defined by a start and end point.
 * @ingroup math
 *
 * A simple value type representing a line segment between two Points of the
 * same dimensionality and component type.
 *
 * @tparam T The component value type (e.g. int, float, double).
 * @tparam N The number of dimensions.
 */
template <typename T, size_t N> class Line {
        public:
                /** @brief The Point type used for the endpoints of this Line. */
                using Pt = Point<T, N>;

                /** @brief Default constructor. Both endpoints are default-constructed (zero). */
                Line() = default;

                /**
                 * @brief Constructs a Line from a start and end point.
                 * @param s The start point.
                 * @param e The end point.
                 */
                Line(const Pt &s, const Pt &e) : _start(s), _end(e) {};

                /**
                 * @brief Move-constructs a Line from a start and end point.
                 * @param s The start point (moved).
                 * @param e The end point (moved).
                 */
                Line(const Pt &&s, const Pt &&e) : _start(std::move(s)), _end(std::move(e)) {}

                /**
                 * @brief Returns a const reference to the start point.
                 * @return The start point of the line segment.
                 */
                const Pt &start() const {
                        return _start;
                }

                /**
                 * @brief Returns a const reference to the end point.
                 * @return The end point of the line segment.
                 */
                const Pt &end() const {
                        return _end;
                }

        private:
                Pt      _start;
                Pt      _end;
};

/** @brief 2D line segment with int32_t components. */
using Line2Di32 = Line<int32_t, 2>;
/** @brief 2D line segment with float components. */
using Line2Df = Line<float, 2>;
/** @brief 2D line segment with double components. */
using Line2Dd = Line<double, 2>;
/** @brief 3D line segment with int32_t components. */
using Line3Di32 = Line<int32_t, 3>;
/** @brief 3D line segment with float components. */
using Line3Df = Line<float, 3>;
/** @brief 3D line segment with double components. */
using Line3Dd = Line<double, 3>;
/** @brief 4D line segment with int32_t components. */
using Line4Di32 = Line<int32_t, 4>;
/** @brief 4D line segment with float components. */
using Line4Df = Line<float, 4>;
/** @brief 4D line segment with double components. */
using Line4Dd = Line<double, 4>;

PROMEKI_NAMESPACE_END

