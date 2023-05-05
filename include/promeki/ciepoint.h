/*****************************************************************************
 * ciepoint.h
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

#pragma once

#include <cmath>
#include <array>
#include <promeki/point.h>
#include <promeki/xyzcolor.h>

namespace promeki {

class CIEPoint {
        public:
                using DataType = Array<double, 2>;
                static constexpr double MinWavelength = 360;
                static constexpr double MaxWavelength = 700;

                //static const CIEPoint D50;
                //static const CIEPoint D55;

                static bool isValidWavelength(double val) {
                        return val >= MinWavelength && val <= MaxWavelength;
                }
                static XYZColor wavelengthToXYZ(double wavelength);
                static CIEPoint wavelengthToCIEPoint(double wavelength);

                static CIEPoint colorTempToWhitePoint(double cct) {
                        // from http://www.brucelindbloom.com/index.html?Eqn_T_to_xy.html
                        double x, y;
                        if(cct < 4000) {
                                return CIEPoint(); // Invalid.
                        } else if(cct <= 7000.0) {
                                x = (-4.6070e9  / std::pow(cct, 3)) +
                                    ( 2.9678e6  / std::pow(cct, 2)) +
                                    ( 0.09911e3 / cct) + 0.244063;
                        } else if(cct <= 25000) {
                                x = (-2.0064e9  / std::pow(cct, 3)) +
                                    ( 1.9108e6  / std::pow(cct, 2)) +
                                    ( 0.24748e3 / cct) + 0.237040;
                        } else {
                                return CIEPoint(); // Invalid
                        }
                        y = (-3.0 * std::pow(x, 2)) + (2.870 * x) - 0.275;
                        return CIEPoint(x, y);
                }

                CIEPoint(double x = -1.0, double y = -1.0) : d(x, y) { }
                CIEPoint(const DataType &other) : d(other) { }

                bool isValid() const {
                        return d.isBetween(DataType(0.0, 0.0), DataType(0.8, 0.9));
                }

                CIEPoint lerp(const CIEPoint &other, double t) const {
                        return d.lerp(other.d, t);
                }
#if 0 
                XYZ toXYZ(double Y = 1.0) const {
                        double X = (x() * Y) / y();
                        double Z = ((1.0 - x() - y()) * Y) / y();
                        return {X, Y, Z};
                }

                bool isInGamut() const {
                        auto [X, Y, Z] = toXYZ();
                        double u = (4.0 * X) / (-2.0 * X + 12.0 * Y + 3.0 * Z);
                        double v = (9.0 * Y) / (-2.0 * X + 12.0 * Y + 3.0 * Z);
                        return u >= 0 && u <= 0.6 && v >= 0 && v <= 0.6 && u + v <= 0.6;
                }

                double colorTemp() const {
                        // McCamy's approximation
                        double n = (x() - 0.3320) / (0.1858 - y());
                        return 437.0 * std::pow(n, 3.0) + 
                               3601.0 * std::pow(n, 2.0) + 
                               6861.0 * n + 5517;
                }
#endif
        private:
                DataType d;

};


} // namespace promeki
