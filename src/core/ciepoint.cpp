/**
 * @file      ciepoint.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/ciepoint.h>
#include <promeki/ciewavelengthtable.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

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

PROMEKI_NAMESPACE_END

