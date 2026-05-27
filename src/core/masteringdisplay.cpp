/**
 * @file      masteringdisplay.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/masteringdisplay.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

const MasteringDisplay MasteringDisplay::HDR10(CIEPoint(0.708, 0.292), CIEPoint(0.170, 0.797), CIEPoint(0.131, 0.046),
                                               CIEPoint(0.3127, 0.3290), 0.0050, 1000.0);

String MasteringDisplay::toString() const {
        return String::sprintf("R(%.4f,%.4f) G(%.4f,%.4f) B(%.4f,%.4f) WP(%.4f,%.4f) "
                               "Lmin=%.4f Lmax=%.1f cd/m²",
                               _red.x(), _red.y(), _green.x(), _green.y(), _blue.x(), _blue.y(), _whitePoint.x(),
                               _whitePoint.y(), _minLum, _maxLum);
}

// ============================================================================
// DataStream wire format (v1: ten tagged doubles).
// ============================================================================

Error MasteringDisplay::writeToStream(DataStream &s) const {
        s << _red.x() << _red.y() << _green.x() << _green.y() << _blue.x() << _blue.y()
          << _whitePoint.x() << _whitePoint.y() << _minLum << _maxLum;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<MasteringDisplay> MasteringDisplay::readFromStream<1>(DataStream &s) {
        double rx = 0, ry = 0, gx = 0, gy = 0, bx = 0, by = 0, wx = 0, wy = 0, minL = 0, maxL = 0;
        s >> rx >> ry >> gx >> gy >> bx >> by >> wx >> wy >> minL >> maxL;
        if (s.status() != DataStream::Ok) return makeError<MasteringDisplay>(s.toError());
        return makeResult(MasteringDisplay(CIEPoint(rx, ry), CIEPoint(gx, gy), CIEPoint(bx, by),
                                           CIEPoint(wx, wy), minL, maxL));
}

PROMEKI_NAMESPACE_END
