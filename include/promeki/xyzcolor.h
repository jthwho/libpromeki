/*****************************************************************************
 * xyzcolor.h
 * May 04, 2023
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

#include <sstream>
#include <promeki/namespace.h>
#include <promeki/array.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class XYZColor {
        public:
                using DataType = Array<double, 3>;

                XYZColor() : d{-1.0, -1.0, -1.0} {}
                XYZColor(const DataType &val) : d(val) {}
                XYZColor(double x, double y, double z) : d(x, y, z) {}

                bool isValid() const {
                        for(size_t i = 0; i < d.size(); ++i) if(d[i] < 0.0 || d[i] > 1.0) return false;
                        return true;
                }

                const DataType &data() const { return d; }
                DataType &data() { return d; }
                double x() { return d[0]; }
                double y() { return d[1]; }
                double z() { return d[2]; }
                void set(double x, double y, double z) { d = { x, y, z }; }
                void setX(double val) { d[0] = val; }
                void setY(double val) { d[1] = val; }
                void setZ(double val) { d[2] = val; }

                XYZColor lerp(const XYZColor &val, double t) const { return d.lerp(val.d, t); }
                operator String() { return toString(); }

                String toString() const {
                        std::stringstream ss;
                        ss << "XYZ(" << d[0] << ", " << d[1] << ", " << d[2] << ")";
                        return ss.str();
                }
 
        private:
                DataType d;
};

PROMEKI_NAMESPACE_END

