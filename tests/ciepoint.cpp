/*****************************************************************************
 * ciepoint.cpp
 * May 01, 2023
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
#include <promeki/ciepoint.h>

using namespace promeki;


PROMEKI_TEST_BEGIN(CIEPoint)
        for(double wl = CIEPoint::MinWavelength; wl < (CIEPoint::MinWavelength + 10.0); wl += 0.1) {
                auto [x, y, z] = CIEPoint::wavelengthToXYZ(wl);
                promekiInfo("%lf %lf %lf %lf", wl, x, y, z);
        }

PROMEKI_TEST_END()


