/**
 * @file      xyzcolor.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/xyzcolor.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// DataStream wire format (v1: three tagged doubles).
// ============================================================================

Error XYZColor::writeToStream(DataStream &s) const {
        s << d[0] << d[1] << d[2];
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<XYZColor> XYZColor::readFromStream<1>(DataStream &s) {
        double x = 0, y = 0, z = 0;
        s >> x >> y >> z;
        if (s.status() != DataStream::Ok) return makeError<XYZColor>(s.toError());
        return makeResult(XYZColor(x, y, z));
}

PROMEKI_NAMESPACE_END
