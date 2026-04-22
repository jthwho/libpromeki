/**
 * @file      mediaduration.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <climits>
#include <promeki/mediaduration.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Compute the inclusive-end frame index `start + length - 1` without
// invoking signed-overflow UB when the two operands are close to
// INT64_MAX.  Returns the computed index on success, or INT64_MIN as
// a sentinel when the addition would overflow.  Callers treat the
// sentinel as "no representable end" and collapse to Unknown.
int64_t safeInclusiveEnd(int64_t start, int64_t length) {
        int64_t sum = 0;
        if(__builtin_add_overflow(start, length, &sum)) return INT64_MIN;
        return sum - 1;
}

} // namespace

FrameNumber MediaDuration::end() const {
        if(!isValid()) return FrameNumber::unknown();
        if(isInfinite()) return FrameNumber::unknown();
        if(_length.isEmpty()) return FrameNumber::unknown();
        const int64_t e = safeInclusiveEnd(_start.value(), _length.value());
        return e < 0 ? FrameNumber::unknown() : FrameNumber(e);
}

void MediaDuration::setEnd(const FrameNumber &e) {
        if(!_start.isValid() || !e.isValid() || e.value() < _start.value()) {
                _length = FrameCount::unknown();
                return;
        }
        _length = FrameCount(e.value() - _start.value() + 1);
}

bool MediaDuration::contains(const FrameNumber &frame) const {
        if(!frame.isValid())  return false;
        if(!_start.isValid()) return false;
        if(_length.isUnknown() || _length.isEmpty()) return false;
        if(frame.value() < _start.value()) return false;
        if(_length.isInfinite()) return true;
        const int64_t last = safeInclusiveEnd(_start.value(), _length.value());
        if(last < 0) return false;
        return frame.value() <= last;
}

bool MediaDuration::contains(const MediaDuration &other) const {
        if(!isValid() || !other.isValid()) return false;
        if(other.isEmpty()) return true;       // empty is contained in any valid duration
        if(isEmpty())       return false;
        if(other._start.value() < _start.value()) return false;
        if(isInfinite())       return true;    // we extend forever from our start
        if(other.isInfinite()) return false;   // can't enclose infinity in finite
        const int64_t thisLast  = safeInclusiveEnd(_start.value(), _length.value());
        const int64_t otherLast = safeInclusiveEnd(other._start.value(), other._length.value());
        if(thisLast < 0 || otherLast < 0) return false;
        return otherLast <= thisLast;
}

bool MediaDuration::overlaps(const MediaDuration &other) const {
        return intersect(other).isValid();
}

MediaDuration MediaDuration::intersect(const MediaDuration &other) const {
        if(!isValid() || !other.isValid()) return MediaDuration();
        if(isEmpty() || other.isEmpty())   return MediaDuration();

        // Lift each side to (start, endInclusive) where an Infinite
        // length yields INT64_MAX as the inclusive end so the same
        // min/max math handles every combination.
        const int64_t aStart = _start.value();
        const int64_t bStart = other._start.value();
        int64_t aEnd = INT64_MAX;
        int64_t bEnd = INT64_MAX;
        if(!isInfinite()) {
                aEnd = safeInclusiveEnd(aStart, _length.value());
                if(aEnd < 0) aEnd = INT64_MAX;  // overflow → treat as open-ended
        }
        if(!other.isInfinite()) {
                bEnd = safeInclusiveEnd(bStart, other._length.value());
                if(bEnd < 0) bEnd = INT64_MAX;  // overflow → treat as open-ended
        }

        const int64_t lo = aStart > bStart ? aStart : bStart;
        const int64_t hi = aEnd   < bEnd   ? aEnd   : bEnd;
        if(lo > hi) return MediaDuration();

        if(hi == INT64_MAX) {
                // Both ranges extend to infinity past lo.
                return MediaDuration(FrameNumber(lo), FrameCount::infinity());
        }
        return MediaDuration(FrameNumber(lo), FrameCount(hi - lo + 1));
}

bool MediaDuration::canAppend(const MediaDuration &other) const {
        // This must have a defined end frame.
        if(!_start.isValid())                    return false;
        if(!_length.isFinite() || _length.isEmpty()) return false;
        // Other must have a defined start and a valid length (finite-positive or infinite).
        if(!other._start.isValid())              return false;
        if(other._length.isUnknown() || other._length.isEmpty()) return false;
        // Adjacency: other starts at the frame right after this duration ends.
        return other._start.value() == _start.value() + _length.value();
}

Error MediaDuration::append(const MediaDuration &other) {
        if(!canAppend(other)) return Error::NotAdjacent;
        _length += other._length;
        return Error::Ok;
}

bool MediaDuration::canPrepend(const MediaDuration &other) const {
        // Other must have a defined end frame.
        if(!other._start.isValid())                            return false;
        if(!other._length.isFinite() || other._length.isEmpty()) return false;
        // This must have a defined start and a valid length (finite-positive or infinite).
        if(!_start.isValid())                                  return false;
        if(_length.isUnknown() || _length.isEmpty())           return false;
        // Adjacency: this starts at the frame right after other ends.
        return _start.value() == other._start.value() + other._length.value();
}

Error MediaDuration::prepend(const MediaDuration &other) {
        if(!canPrepend(other)) return Error::NotAdjacent;
        _start = other._start;
        _length += other._length;
        return Error::Ok;
}

Result<MediaDuration::FrameRange> MediaDuration::toFrameRange() const {
        if(isUnknown())   return makeError<FrameRange>(Error::DurationUnknown);
        if(isInfinite())  return makeError<FrameRange>(Error::FrameRangeInfinite);
        if(_length.isEmpty()) return makeError<FrameRange>(Error::Invalid);
        const int64_t last = safeInclusiveEnd(_start.value(), _length.value());
        if(last < 0) return makeError<FrameRange>(Error::OutOfRange);
        FrameRange r;
        r.start = _start;
        r.end   = FrameNumber(last);
        return makeResult(r);
}

String MediaDuration::toString() const {
        // Canonical: "<start>+<length>".  Each component renders its
        // own canonical form (empty for Unknown, "inf" for Infinity,
        // <N>f for a valid count, plain integer for a valid start).
        return _start.toString() + String("+") + _length.toString();
}

MediaDuration MediaDuration::fromString(const String &str, Error *err) {
        String t = str.trim();
        if(t.isEmpty()) {
                if(err != nullptr) *err = Error::Ok;
                return MediaDuration();
        }

        // Find the operator that splits the two components.  Prefer
        // '+' (start+length form); fall back to '-' (inclusive range
        // form).  Operator cannot be at position 0 so negative-looking
        // inputs do not confuse the scan.
        const char *s = t.cstr();
        const size_t n = t.byteCount();
        size_t opIdx = SIZE_MAX;
        char   opCh  = 0;
        for(size_t i = 1; i < n; ++i) {
                if(s[i] == '+') { opIdx = i; opCh = '+'; break; }
        }
        if(opIdx == SIZE_MAX) {
                for(size_t i = 1; i < n; ++i) {
                        if(s[i] == '-') { opIdx = i; opCh = '-'; break; }
                }
        }
        if(opIdx == SIZE_MAX) {
                if(err != nullptr) *err = Error::ParseFailed;
                return MediaDuration();
        }

        String left(s, opIdx);
        String right(s + opIdx + 1, n - opIdx - 1);

        Error leftErr;
        FrameNumber start = FrameNumber::fromString(left, &leftErr);
        if(leftErr.isError()) {
                if(err != nullptr) *err = Error::ParseFailed;
                return MediaDuration();
        }

        if(opCh == '+') {
                Error rightErr;
                FrameCount length = FrameCount::fromString(right, &rightErr);
                if(rightErr.isError()) {
                        if(err != nullptr) *err = Error::ParseFailed;
                        return MediaDuration();
                }
                if(err != nullptr) *err = Error::Ok;
                return MediaDuration(start, length);
        }

        // Range form "<start>-<end>".
        Error rightErr;
        FrameNumber end = FrameNumber::fromString(right, &rightErr);
        if(rightErr.isError()) {
                if(err != nullptr) *err = Error::ParseFailed;
                return MediaDuration();
        }
        if(err != nullptr) *err = Error::Ok;
        return fromFrameRange(FrameRange(start, end));
}

PROMEKI_NAMESPACE_END

