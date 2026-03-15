/**
 * @file      colorspace.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cmath>
#include <promeki/proav/colorspace.h>
#include <promeki/core/structdatabase.h>
#include <promeki/core/util.h>

#define DEFINE_SPACE(value) \
        .id = ColorSpace::value, \
        .name = PROMEKI_STRINGIFY(value)

PROMEKI_NAMESPACE_BEGIN

static double transferLinear(double val) {
        return val;
}

static double invTransferLinear(double val) {
        return val;
}

static const ColorSpace::Params paramsRec709 = { 
        CIEPoint(0.64, 0.33),
        CIEPoint(0.30, 0.60),
        CIEPoint(0.15, 0.06),
        CIEPoint(0.3127, 0.3290)
};

static double transferRec709(double val) {
        return val < 0.018 ? 4.5 * val : 1.099 * pow(val, 0.45) - 0.099;
}

static double invTransferRec709(double val) {
        return val < 0.081 ? val / 4.5 : pow((val + 0.099) / 1.099, 1.0 / 0.45);
}

static const ColorSpace::Params paramsRec601_PAL = { 
        CIEPoint(0.64, 0.33),
        CIEPoint(0.29, 0.60),
        CIEPoint(0.15, 0.06),
        CIEPoint(0.3127, 0.3290)
};

static double transferRec601_PAL(double val) {
        return val < 0.018 ? 4.5 * val : 1.099 * pow(val, 0.45) - 0.099;
}

static double invTransferRec601_PAL(double val) {
        return val < 0.081 ? val / 4.5 : pow((val + 0.099) / 1.099, 1.0 / 0.45);
}


static const ColorSpace::Params paramsRec601_NTSC = { 
        CIEPoint(0.63, 0.34),
        CIEPoint(0.31, 0.595),
        CIEPoint(0.155, 0.07),
        CIEPoint(0.3127, 0.3290)
};

static double transferRec601_NTSC(double val) {
        return val < 0.018 ? 4.5 * val : 1.099 * pow(val, 0.45) - 0.099;
}

static double invTransferRec601_NTSC(double val) {
        return val < 0.081 ? val / 4.5 : pow((val + 0.099) / 1.099, 1.0 / 0.45);
}

static double transferSRGB(double val) {
        return val <= 0.0031308 ? 2.92 * val : 1.055 * std::pow(val, 1.0 / 2.4) - 0.055;
}

static double invTransferSRGB(double val) {
        return val <= 0.04045 ? val / 12.92 : std::pow((val + 0.055) / 1.055, 2.4);
}

static StructDatabase<ColorSpace::ID, ColorSpace::Data> db = {
        {
                DEFINE_SPACE(Invalid),
                .desc = "Invalid",
                .transferFunc = transferLinear,
                .invTransferFunc = invTransferLinear
        },
        {
                DEFINE_SPACE(Rec709),
                .desc = "ITU-R BT.709",
                .invID = ColorSpace::LinearRec709,
                .params = paramsRec709,
                .transferFunc = transferRec709,
                .invTransferFunc = invTransferRec709
        },
        {
                DEFINE_SPACE(LinearRec709),
                .desc = "Linear ITU-R BT.709",
                .invID = ColorSpace::Rec709,
                .params = paramsRec709,
                .transferFunc = invTransferRec709,
                .invTransferFunc = transferRec709
        },
        {
                DEFINE_SPACE(Rec601_PAL),
                .desc = "ITU-R BT.601 PAL",
                .invID = ColorSpace::LinearRec601_PAL,
                .params = paramsRec601_PAL,
                .transferFunc = transferRec601_PAL,
                .invTransferFunc = invTransferRec601_PAL,
        },
        {
                DEFINE_SPACE(LinearRec601_PAL),
                .desc = "Linear ITU-R BT.601 PAL",
                .invID = ColorSpace::Rec601_PAL,
                .params = paramsRec601_PAL,
                .transferFunc = invTransferRec601_PAL,
                .invTransferFunc = transferRec601_PAL
        },
        {
                DEFINE_SPACE(Rec601_NTSC),
                .desc = "ITU-R BT.601 NTSC",
                .invID = ColorSpace::LinearRec601_NTSC,
                .params = paramsRec601_NTSC,
                .transferFunc = transferRec601_NTSC,
                .invTransferFunc = invTransferRec601_NTSC
        },
        {
                DEFINE_SPACE(LinearRec601_NTSC),
                .desc = "Linear ITU-R BT.601 NTSC",
                .invID = ColorSpace::Rec601_NTSC,
                .params = paramsRec601_NTSC,
                .transferFunc = invTransferRec601_NTSC,
                .invTransferFunc = transferRec601_NTSC
        }
};

const ColorSpace::Data *ColorSpace::lookup(ID id) {
        return &db.get(id);
}

PROMEKI_NAMESPACE_END


