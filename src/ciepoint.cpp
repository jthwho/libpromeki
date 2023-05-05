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

#include <cmath>
#include <promeki/ciepoint.h>
#include <promeki/ciewavelengthtable.h>
#include <promeki/util.h>


namespace promeki {

XYZColor CIEPoint::wavelengthToXYZ(double val) {
        if(!CIEPoint::isValidWavelength(val)) return XYZColor();
        double tmp;
        double t = std::modf(val, &tmp);
        int index = static_cast<int>(tmp);
        index -= (int)CIEPoint::MinWavelength; // Our table starts at this point.
        const CIEWavelength &left = cieWavelengthTable[index];
        if(t == 0.0) return left.xyz;
        const CIEWavelength &right = cieWavelengthTable[index + 1]; 
        return left.xyz.lerp(right.xyz, t);
}

CIEPoint CIEPoint::wavelengthToCIEPoint(double val) {
        if(!CIEPoint::isValidWavelength(val)) return CIEPoint();
        double tmp;
        double t = std::modf(val, &tmp);
        int index = static_cast<int>(tmp);
        index -= (int)CIEPoint::MinWavelength; // Our table starts at this point.
        const CIEWavelength &left = cieWavelengthTable[index];
        if(t == 0.0) return left.xy;
        const CIEWavelength &right = cieWavelengthTable[index + 1]; 
        return left.xy.lerp(right.xy, t);
}

} // namespace promeki

