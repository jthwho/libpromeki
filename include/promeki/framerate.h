/**
 * @file      framerate.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/rational.h>

PROMEKI_NAMESPACE_BEGIN

#define PROMEKI_WELL_KNOWN_FRAME_RATES \
    X(FPS_Invalid,  "INV",      0,          1) \
    X(FPS_60,       "60",       60,         1) \
    X(FPS_5994,     "59.94",    60000,      1001) \
    X(FPS_50,       "50",       50,         1) \
    X(FPS_30,       "30",       30,         1) \
    X(FPS_2997,     "29.97",    30000,      1001) \
    X(FPS_25,       "25",       25,         1) \
    X(FPS_24,       "24",       24,         1) \
    X(FPS_2398,     "23.98",    24000,      1001)

class FrameRate {
    public:
        using RationalType = Rational<unsigned int>;

#define X(type, string, num, den) type,
        enum WellKnownRate {
            FPS_NotWellKnown = 0,
            PROMEKI_WELL_KNOWN_FRAME_RATES
        };
#undef X

        FrameRate() = default;
        FrameRate(WellKnownRate rate);
        FrameRate(const RationalType &r);

        bool isValid() const { return _fps.numerator() > 0; }
        unsigned int numerator() const { return _fps.numerator(); }
        unsigned int denominator() const { return _fps.denominator(); }
        double toDouble() const { return _fps.toDouble(); }
        String toString() const { return _fps.toString(); }

        bool isWellKnownRate() const { return _rate != FPS_NotWellKnown; }
        WellKnownRate wellKnownRate() const { return _rate; }

    private:
        RationalType    _fps;
        WellKnownRate   _rate = FPS_NotWellKnown;
};

PROMEKI_NAMESPACE_END

