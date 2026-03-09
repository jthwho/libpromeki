/**
 * @file      point.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/unittest.h>
#include <promeki/point.h>
#include <promeki/util.h>

using namespace promeki;

PROMEKI_TEST_BEGIN(Point)
        Point2D p1(1, 2);
        Point2D p2(5, 4);

        PROMEKI_TEST(p1.toString() == "1, 2");
        PROMEKI_TEST(p1 * p2 == Point2D(5, 8));
        PROMEKI_TEST(p1.x() == 1);
        PROMEKI_TEST(p1.y() == 2);
PROMEKI_TEST_END()


