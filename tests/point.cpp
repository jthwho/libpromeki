/**
 * @file      point.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/point.h>
#include <promeki/util.h>

using namespace promeki;

TEST_CASE("Point") {
        Point2D p1(1, 2);
        Point2D p2(5, 4);

        CHECK(p1.toString() == "1, 2");
        CHECK(p1 * p2 == Point2D(5, 8));
        CHECK(p1.x() == 1);
        CHECK(p1.y() == 2);
}
