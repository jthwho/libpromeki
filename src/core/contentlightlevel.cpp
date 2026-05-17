/**
 * @file      contentlightlevel.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <promeki/contentlightlevel.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

String ContentLightLevel::toString() const {
        return String::sprintf("MaxCLL=%u MaxFALL=%u cd/m²", _maxCLL, _maxFALL);
}

// ============================================================================
// DataStream wire format (v1: two tagged uint32s).
// ============================================================================

Error ContentLightLevel::writeToStream(DataStream &s) const {
        s << _maxCLL << _maxFALL;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<ContentLightLevel> ContentLightLevel::readFromStream<1>(DataStream &s) {
        uint32_t cll = 0, fall = 0;
        s >> cll >> fall;
        if (s.status() != DataStream::Ok) return makeError<ContentLightLevel>(s.toError());
        return makeResult(ContentLightLevel(cll, fall));
}

PROMEKI_NAMESPACE_END
