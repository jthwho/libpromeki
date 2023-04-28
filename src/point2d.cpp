/*****************************************************************************
 * point2d.cpp
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

#include <promeki/point2d.h>
#include <promeki/unittest.h>

namespace promeki {

#if 0
template<typename T> 
bool test_point2d(const UnitTest &unit) {
	// Tests the constructor.
	Point2D<T> p1(1, 1);
	PROMEKI_TEST(p1.x() == 1 && p1.y() == 1);

	// Tests the set methods.
	p1.set(2, 2);
	PROMEKI_TEST(p1.x() == 2 && p1.y() == 2);

	p1.setX(3);
	PROMEKI_TEST(p1.x() == 3);
	p1.setY(3);
	PROMEKI_TEST(p1.y() == 3);

	// Tests the operators.
	Point2D<T> p2(4, 4);
	Point2D<T> p3 = p1 + p2;
	PROMEKI_TEST(p3.x() == 7 && p3.y() == 7);

        p1.set(1, 1);
	Point2D<T> p4 = p1 - p2;
	PROMEKI_TEST(p4.x() == -3 && p4.y() == -3);

        p1.set(3, 4);
	Point2D<T> p5 = p1 * 5;
	PROMEKI_TEST(p5.x() == 15 && p5.y() == 20);

	Point2D<T> p6 = p1 / 5;
	PROMEKI_TEST(p6.x() == static_cast<T>(3.0 /  5.0) && p6.y() == static_cast<T>(4.0 / 5.0));

	PROMEKI_TEST(p1 == p1);
	PROMEKI_TEST(p1 != p2);

	// Tests other methods.
	PROMEKI_TEST(p1.dot(p2) == 28);
	PROMEKI_TEST(p1.cross(p2) == -4);
	PROMEKI_TEST(p1.magnitude() == std::sqrt(25));
	PROMEKI_TEST(p1.normalize() == Point2D<T>(0.6, 0.8));

        return true;
}

PROMEKI_TEST_BEGIN(Point2D)
        PROMEKI_MSG("Testing float");
        if(!test_point2d<float>(unit)) return false;
        PROMEKI_MSG("Testing double");
        if(!test_point2d<double>(unit)) return false;
PROMEKI_TEST_END()
#endif

} // namespace promeki

