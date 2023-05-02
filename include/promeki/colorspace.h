/*****************************************************************************
 * colorspace.h
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

#include <array>
#include <promeki/ciepoint.h>

namespace promeki {

class ColorSpace {
        public:
                enum ID {
                        Invalid = 0,
                        Rec709,
                        LinearRec709,
                        Rec601_PAL,
                        LinearRec601_PAL,
                        Rec601_NTSC,
                        LinearRec601_NTSC
                };

                typedef double (*TransformFunc)(double);
                typedef std::array<CIEPoint, 4> Params;

                struct Data {
                        ID              id;
                        String          name;
                        String          desc;
                        ID              invID;
                        Params          params;      // red, green, blue, white point
                        TransformFunc   transferFunc = nullptr;
                        TransformFunc   invTransferFunc = nullptr;
                };

                //static const ColorSpace &lookupColorSpace(ID type);
                ColorSpace(ID id = Invalid) : d(lookup(id)) { }

                const ID id() const { return d->id; }
                ColorSpace inverseColorSpace() { return d->invID; }
                const String name() const { return d->name; }
                const String desc() const { return d->desc; }
                const CIEPoint &red() const { return d->params[0]; }
                const CIEPoint &green() const { return d->params[1]; }
                const CIEPoint &blue() const { return d->params[2]; }
                const CIEPoint &whitePoint() const { return d->params[3]; }
                double transferFunc(double input) const { return d->transferFunc(input); }
                double invtTransferFunc(double input) const { return d->invTransferFunc(input); }

        private:
                const Data *d = nullptr;
                static const Data *lookup(ID val);
};

} // namespace promeki
