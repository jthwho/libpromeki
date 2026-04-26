/**
 * @file      masteringdisplay.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/masteringdisplay.h>

PROMEKI_NAMESPACE_BEGIN

const MasteringDisplay MasteringDisplay::HDR10(CIEPoint(0.708, 0.292), CIEPoint(0.170, 0.797), CIEPoint(0.131, 0.046),
                                               CIEPoint(0.3127, 0.3290), 0.0050, 1000.0);

String MasteringDisplay::toString() const {
        return String::sprintf("R(%.4f,%.4f) G(%.4f,%.4f) B(%.4f,%.4f) WP(%.4f,%.4f) "
                               "Lmin=%.4f Lmax=%.1f cd/m²",
                               _red.x(), _red.y(), _green.x(), _green.y(), _blue.x(), _blue.y(), _whitePoint.x(),
                               _whitePoint.y(), _minLum, _maxLum);
}

PROMEKI_NAMESPACE_END
