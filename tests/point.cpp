/*****************************************************************************
 * point.cpp
 * May 02, 2023
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


