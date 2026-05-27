/**
 * @file      pixelaspect.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/pixelaspect.h>

PROMEKI_NAMESPACE_BEGIN

String PixelAspect::toString() const {
        return String::number(static_cast<int64_t>(_par.numerator())) + String(":")
               + String::number(static_cast<int64_t>(_par.denominator()));
}

Result<PixelAspect> PixelAspect::fromString(const String &s) {
        const size_t colon = s.find(':');
        if (colon == String::npos) {
                promekiWarn("PixelAspect::fromString('%s') failed: missing ':' separator", s.cstr());
                return makeError<PixelAspect>(Error::ParseFailed);
        }
        Error      werr, herr;
        const int  w = s.left(colon).toInt(&werr);
        const int  h = s.mid(colon + 1).toInt(&herr);
        if (werr.isError() || herr.isError() || w <= 0 || h <= 0) {
                promekiWarn("PixelAspect::fromString('%s') failed: non-positive or non-integer components",
                            s.cstr());
                return makeError<PixelAspect>(Error::ParseFailed);
        }
        return makeResult(PixelAspect(static_cast<unsigned int>(w), static_cast<unsigned int>(h)));
}

// ============================================================================
// DataStream wire format (v1: two tagged uint32 fields, width/height).
// ============================================================================

Error PixelAspect::writeToStream(DataStream &s) const {
        s << static_cast<uint32_t>(_par.numerator());
        s << static_cast<uint32_t>(_par.denominator());
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<PixelAspect> PixelAspect::readFromStream<1>(DataStream &s) {
        uint32_t w = 1, h = 1;
        s >> w >> h;
        if (s.status() != DataStream::Ok) return makeError<PixelAspect>(s.toError());
        if (w == 0 || h == 0) {
                promekiWarn("PixelAspect::readFromStream: corrupt data (w=%u, h=%u)", w, h);
                return makeError<PixelAspect>(Error::CorruptData);
        }
        return makeResult(PixelAspect(w, h));
}

PROMEKI_NAMESPACE_END
