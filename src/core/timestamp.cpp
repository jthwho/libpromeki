/**
 * @file      timestamp.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/timestamp.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// DataStream wire format (v1: int64 nanoseconds since steady_clock epoch).
// Invalid timestamps round-trip via the @ref TimeStamp::Invalid sentinel
// stored as-is.
// ============================================================================

Error TimeStamp::writeToStream(DataStream &s) const {
        s << _ns;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<TimeStamp> TimeStamp::readFromStream<1>(DataStream &s) {
        int64_t ns = 0;
        s >> ns;
        if (s.status() != DataStream::Ok) return makeError<TimeStamp>(s.toError());
        return makeResult(TimeStamp(ns));
}

PROMEKI_NAMESPACE_END
