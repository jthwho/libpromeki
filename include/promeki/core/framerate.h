/**
 * @file      core/framerate.h
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/rational.h>

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

/**
 * @brief Represents a video frame rate as a rational number.
 * @ingroup core_time
 *
 * FrameRate wraps a Rational value and provides an enumeration of
 * well-known industry-standard frame rates (24, 25, 29.97, 30, etc.).
 * A frame rate can be constructed from a WellKnownRate enum or from
 * an arbitrary rational value.
 */
class FrameRate {
        public:
                /** @brief Underlying rational type used to store the frame rate. */
                using RationalType = Rational<unsigned int>;

#define X(type, string, num, den) type,
                /** @brief Enumeration of well-known industry-standard frame rates. */
                enum WellKnownRate {
                        FPS_NotWellKnown = 0, ///< Not a well-known rate.
                        PROMEKI_WELL_KNOWN_FRAME_RATES
                };
#undef X

                /** @brief Default constructor. Creates an invalid (zero) frame rate. */
                FrameRate() = default;

                /**
                 * @brief Constructs a FrameRate from a well-known rate enum.
                 * @param rate The well-known rate to use.
                 */
                FrameRate(WellKnownRate rate);

                /**
                 * @brief Constructs a FrameRate from an arbitrary rational value.
                 * @param r The rational frame rate (numerator / denominator).
                 */
                FrameRate(const RationalType &r);

                /**
                 * @brief Returns true if this frame rate is valid (numerator > 0).
                 * @return true if valid.
                 */
                bool isValid() const { return _fps.numerator() > 0; }

                /** @brief Returns the numerator of the frame rate rational. */
                unsigned int numerator() const { return _fps.numerator(); }

                /** @brief Returns the denominator of the frame rate rational. */
                unsigned int denominator() const { return _fps.denominator(); }

                /**
                 * @brief Returns the frame rate as a double-precision floating point value.
                 * @return The frame rate in frames per second.
                 */
                double toDouble() const { return _fps.toDouble(); }

                /**
                 * @brief Returns a string representation of the frame rate.
                 * @return The frame rate as a String (e.g. "30000/1001").
                 */
                String toString() const { return _fps.toString(); }

                /**
                 * @brief Returns true if this is a well-known industry frame rate.
                 * @return true if the rate matches a WellKnownRate entry.
                 */
                bool isWellKnownRate() const { return _rate != FPS_NotWellKnown; }

                /**
                 * @brief Returns the WellKnownRate enum value for this frame rate.
                 * @return The matching WellKnownRate, or FPS_NotWellKnown.
                 */
                WellKnownRate wellKnownRate() const { return _rate; }

        private:
                RationalType    _fps;
                WellKnownRate   _rate = FPS_NotWellKnown;
};

PROMEKI_NAMESPACE_END

