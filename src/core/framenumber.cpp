/**
 * @file      framenumber.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/framenumber.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

String FrameNumber::toString() const {
        if (isUnknown()) return String();
        return String::number(_value);
}

Result<FrameNumber> FrameNumber::fromString(const String &str) {
        // Lenient accept: trim surrounding whitespace, accept common
        // sentinel words, otherwise parse as a signed decimal and
        // reject negative values (the only negative state we keep is
        // the canonical Unknown, reached by the empty-string branch).
        String t = str.trim();
        if (t.isEmpty()) return makeResult(FrameNumber::unknown());
        String lc = t.toLower();
        if (lc == "unknown" || lc == "unk" || lc == "?") return makeResult(FrameNumber::unknown());
        Error   parseErr;
        int64_t v = t.to<int64_t>(&parseErr);
        if (parseErr.isError()) return makeError<FrameNumber>(Error::ParseFailed);
        if (v < 0) return makeError<FrameNumber>(Error::OutOfRange);
        return makeResult(FrameNumber(v));
}

// ============================================================================
// DataStream wire format (v1: tagged int64 storage value).
// ============================================================================

Error FrameNumber::writeToStream(DataStream &s) const {
        s << static_cast<int64_t>(_value);
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<FrameNumber> FrameNumber::readFromStream<1>(DataStream &s) {
        int64_t v = 0;
        s >> v;
        if (s.status() != DataStream::Ok) return makeError<FrameNumber>(s.toError());
        return makeResult(FrameNumber(v));
}

PROMEKI_NAMESPACE_END
