/**
 * @file      colorspace.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <array>
#include <promeki/namespace.h>
#include <promeki/ciepoint.h>

PROMEKI_NAMESPACE_BEGIN

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

PROMEKI_NAMESPACE_END

