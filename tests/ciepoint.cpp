/**
 * @file      ciepoint.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/unittest.h>
#include <promeki/ciepoint.h>

using namespace promeki;


PROMEKI_TEST_BEGIN(CIEPoint)
        //for(double wl = CIEPoint::MinWavelength; wl < (CIEPoint::MinWavelength + 10.0); wl += 0.1) {
        //        auto [x, y, z] = CIEPoint::wavelengthToXYZ(wl);
        //        promekiInfo("%lf %lf %lf %lf", wl, x, y, z);
        //}
        
        CIEPoint Kelvin5500(CIEPoint::D55);
        promekiInfo("D55 temp: %lf", Kelvin5500.colorTemp());
        CIEPoint xy55 = CIEPoint::colorTempToWhitePoint(5500);
        promekiInfo("D55 XY: %lf, %lf", xy55.x(), xy55.y());

PROMEKI_TEST_END()


