/**
 * @file      masteringdisplay.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/ciepoint.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Mastering display color volume metadata (SMPTE ST 2086).
 * @ingroup color
 *
 * Describes the color volume of the mastering display used to
 * grade the content.  This metadata rides alongside HDR video
 * so downstream displays and tone-mappers know the creative
 * intent — it does not describe the content itself (that is
 * @ref ContentLightLevel).
 *
 * Fields map directly to the SMPTE ST 2086 / CTA-861.3
 * mastering display color volume descriptor:
 *
 * | Field          | Unit            | Typical HDR10 value           |
 * |----------------|-----------------|-------------------------------|
 * | red/green/blue | CIE xy          | BT.2020 primaries             |
 * | whitePoint     | CIE xy          | D65 (0.3127, 0.3290)          |
 * | minLuminance   | cd/m² (nits)    | 0.0050                        |
 * | maxLuminance   | cd/m² (nits)    | 1000.0                        |
 *
 * A default-constructed MasteringDisplay is invalid
 * (@ref isValid returns false).
 *
 * @par Example
 * @code
 * MasteringDisplay md;
 * md.setRed(CIEPoint(0.708, 0.292));
 * md.setGreen(CIEPoint(0.170, 0.797));
 * md.setBlue(CIEPoint(0.131, 0.046));
 * md.setWhitePoint(CIEPoint(0.3127, 0.3290));
 * md.setMinLuminance(0.005);
 * md.setMaxLuminance(1000.0);
 * @endcode
 */
class MasteringDisplay {
        public:
                static const MasteringDisplay HDR10;

                MasteringDisplay() = default;

                MasteringDisplay(const CIEPoint &red, const CIEPoint &green,
                                 const CIEPoint &blue, const CIEPoint &wp,
                                 double minLum, double maxLum)
                        : _red(red), _green(green), _blue(blue),
                          _whitePoint(wp), _minLum(minLum), _maxLum(maxLum) {}

                bool isValid() const {
                        return _red.isValid() && _green.isValid() &&
                               _blue.isValid() && _whitePoint.isValid() &&
                               _maxLum > 0.0;
                }

                const CIEPoint &red() const        { return _red; }
                const CIEPoint &green() const      { return _green; }
                const CIEPoint &blue() const       { return _blue; }
                const CIEPoint &whitePoint() const { return _whitePoint; }
                double minLuminance() const         { return _minLum; }
                double maxLuminance() const         { return _maxLum; }

                void setRed(const CIEPoint &v)        { _red = v; }
                void setGreen(const CIEPoint &v)      { _green = v; }
                void setBlue(const CIEPoint &v)       { _blue = v; }
                void setWhitePoint(const CIEPoint &v) { _whitePoint = v; }
                void setMinLuminance(double v)         { _minLum = v; }
                void setMaxLuminance(double v)         { _maxLum = v; }

                bool operator==(const MasteringDisplay &o) const {
                        return _red.data() == o._red.data() &&
                               _green.data() == o._green.data() &&
                               _blue.data() == o._blue.data() &&
                               _whitePoint.data() == o._whitePoint.data() &&
                               _minLum == o._minLum && _maxLum == o._maxLum;
                }
                bool operator!=(const MasteringDisplay &o) const { return !(*this == o); }

                String toString() const;

        private:
                CIEPoint _red;
                CIEPoint _green;
                CIEPoint _blue;
                CIEPoint _whitePoint;
                double   _minLum = 0.0;
                double   _maxLum = 0.0;
};

PROMEKI_NAMESPACE_END
