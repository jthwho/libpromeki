/**
 * @file      framenumber.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/framenumber.h>

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

PROMEKI_NAMESPACE_END
