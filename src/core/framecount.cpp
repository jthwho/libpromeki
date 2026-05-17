/**
 * @file      framecount.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/framecount.h>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

String FrameCount::toString() const {
        if (isUnknown()) return String();
        if (isInfinite()) return String("inf");
        return String::number(_value) + String("f");
}

Result<FrameCount> FrameCount::fromString(const String &str) {
        String t = str.trim();
        if (t.isEmpty()) return makeResult(FrameCount::unknown());
        String lc = t.toLower();
        if (lc == "unknown" || lc == "unk" || lc == "?") return makeResult(FrameCount::unknown());
        if (lc == "inf" || lc == "infinity" || lc == "infinite" || lc == "\xe2\x88\x9e")
                return makeResult(FrameCount::infinity());
        // Drop a trailing 'f' / 'F' so that "50f", "50F", or bare "50"
        // all parse.  Anything else after digits is a parse error.
        String body = t;
        if (!body.isEmpty()) {
                char last = body.cstr()[body.byteCount() - 1];
                if (last == 'f' || last == 'F') {
                        body = String(body.cstr(), body.byteCount() - 1).trim();
                        if (body.isEmpty()) return makeError<FrameCount>(Error::ParseFailed);
                }
        }
        Error   parseErr;
        int64_t v = body.to<int64_t>(&parseErr);
        if (parseErr.isError()) return makeError<FrameCount>(Error::ParseFailed);
        if (v < 0) return makeError<FrameCount>(Error::OutOfRange);
        return makeResult(FrameCount(v));
}

FrameCount &FrameCount::operator+=(int64_t n) {
        if (isUnknown()) return *this;
        if (isInfinite()) return *this;
        int64_t nv = _value + n;
        _value = nv < 0 ? UnknownValue : nv;
        return *this;
}

FrameCount &FrameCount::operator-=(int64_t n) {
        if (isUnknown()) return *this;
        if (isInfinite()) return *this;
        int64_t nv = _value - n;
        _value = nv < 0 ? UnknownValue : nv;
        return *this;
}

FrameCount &FrameCount::operator+=(const FrameCount &other) {
        if (isUnknown() || other.isUnknown()) {
                _value = UnknownValue;
                return *this;
        }
        if (isInfinite() || other.isInfinite()) {
                _value = InfinityValue;
                return *this;
        }
        int64_t nv = _value + other._value;
        _value = nv < 0 ? UnknownValue : nv;
        return *this;
}

FrameCount &FrameCount::operator-=(const FrameCount &other) {
        if (isUnknown() || other.isUnknown()) {
                _value = UnknownValue;
                return *this;
        }
        if (isInfinite() && other.isInfinite()) {
                _value = UnknownValue;
                return *this;
        }
        if (isInfinite()) {
                _value = InfinityValue;
                return *this;
        } // ∞ - finite = ∞
        if (other.isInfinite()) {
                _value = UnknownValue;
                return *this;
        } // finite - ∞ = Unknown
        int64_t nv = _value - other._value;
        _value = nv < 0 ? UnknownValue : nv;
        return *this;
}

bool FrameCount::operator<(const FrameCount &other) const {
        if (isUnknown() || other.isUnknown()) return false;
        if (isInfinite()) return false;      // ∞ is not < anything
        if (other.isInfinite()) return true; // finite < ∞
        return _value < other._value;
}

// ============================================================================
// DataStream wire format (v1: tagged int64 raw storage value).
// ============================================================================

Error FrameCount::writeToStream(DataStream &s) const {
        s << static_cast<int64_t>(_value);
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<FrameCount> FrameCount::readFromStream<1>(DataStream &s) {
        int64_t v = 0;
        s >> v;
        if (s.status() != DataStream::Ok) return makeError<FrameCount>(s.toError());
        FrameCount c;
        if (v == UnknownValue) c = FrameCount::unknown();
        else if (v == InfinityValue) c = FrameCount::infinity();
        else if (v >= 0) c = FrameCount(v);
        else return makeError<FrameCount>(Error::CorruptData);
        return makeResult(c);
}

PROMEKI_NAMESPACE_END
