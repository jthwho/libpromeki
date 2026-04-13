/**
 * @file      mediatimestamp.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <promeki/mediatimestamp.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

MediaTimeStamp::MediaTimeStamp(const TimeStamp &ts, const ClockDomain &domain,
                               const Duration &offset) :
        _timeStamp(ts),
        _domain(domain),
        _offset(offset)
{
}

String MediaTimeStamp::toString() const {
        if(!isValid()) return String();
        int64_t offsetNs = _offset.nanoseconds();
        return String::sprintf("%s %s %+lld",
                _domain.toString().cstr(),
                _timeStamp.toString().cstr(),
                static_cast<long long>(offsetNs));
}

Result<MediaTimeStamp> MediaTimeStamp::fromString(const String &str) {
        StringList tokens = str.split(" ");
        if(tokens.size() != 3) {
                return makeError<MediaTimeStamp>(Error::Invalid);
        }

        // Token 0: clock domain name (must be already registered)
        ClockDomain domain = ClockDomain::lookup(tokens[0]);
        if(!domain.isValid()) {
                return makeError<MediaTimeStamp>(Error::Invalid);
        }

        // Token 1: timestamp in seconds (matches TimeStamp::toString())
        Error tsErr;
        double sec = tokens[1].toDouble(&tsErr);
        if(tsErr.isError()) {
                return makeError<MediaTimeStamp>(Error::Invalid);
        }
        TimeStamp ts(TimeStamp::Value(TimeStamp::secondsToDuration(sec)));

        // Token 2: signed offset in nanoseconds (e.g. "+0", "-5000000")
        char *end = nullptr;
        int64_t offsetNs = std::strtoll(tokens[2].cstr(), &end, 10);
        if(end == tokens[2].cstr()) {
                return makeError<MediaTimeStamp>(Error::Invalid);
        }
        Duration offset = Duration::fromNanoseconds(offsetNs);

        return makeResult(MediaTimeStamp(ts, domain, offset));
}

bool MediaTimeStamp::operator==(const MediaTimeStamp &other) const {
        return _domain == other._domain &&
               _timeStamp == other._timeStamp &&
               _offset == other._offset;
}

bool MediaTimeStamp::operator!=(const MediaTimeStamp &other) const {
        return !(*this == other);
}

PROMEKI_NAMESPACE_END
