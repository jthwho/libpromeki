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

#include <promeki/point2d.h>

namespace promeki {

class CIEPoint : public Point2Dd {
        public:
                static constexpr double MinWavelength = 360;
                static constexpr double MaxWavelength = 700;

                using XYZ = std::tuple<double, double, double>;

                static bool isValidWavelength(double val) {
                        return val >= MinWavelength && val <= MaxWavelength;
                }
                static XYZ wavelengthToXYZ(double wavelength);
                static CIEPoint wavelengthToCIEPoint(double wavelength);

                CIEPoint(double x = 0, double y = 0) : Point2Dd(x, y) { }
                CIEPoint(const Point2Dd& other) : Point2Dd(other) { }
                CIEPoint(const std::initializer_list<double>& list) : Point2Dd(list) { }

                bool isValid() const {
                        return isInside(0.0, 0.0, 0.8, 0.9);
                }
                
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

};

} // namespace promeki
