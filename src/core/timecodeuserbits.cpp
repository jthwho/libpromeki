/**
 * @file      timecodeuserbits.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <promeki/datastream.h>
#include <promeki/timecodeuserbits.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Per ST 12-1 §8.4.4 the eight nibbles are listed in transmission
        // order: nibble 0 is the first emitted on the wire (bits 4-7 of
        // the LTC codeword), nibble 7 is the last (bits 60-63).  The
        // ASCII mode (BGF=001) carries two consecutive nibbles per
        // character — the low nibble first.
        constexpr size_t kAsciiCharCount = 4; // four 8-bit characters in 32 bits

        // One-letter mode codes for diagnostics.  Keep the printable
        // forms short so log lines stay aligned.
        const char *modeCode(TimecodeUserbits::Mode m) {
                switch (m) {
                        case TimecodeUserbits::Unspecified:       return "U";
                        case TimecodeUserbits::EightBitChars:     return "C";
                        case TimecodeUserbits::ClockTime:         return "T";
                        case TimecodeUserbits::Reserved:          return "R";
                        case TimecodeUserbits::DateTimeZone:      return "D";
                        case TimecodeUserbits::PageLine:          return "P";
                        case TimecodeUserbits::DateTimeZoneClock: return "DT";
                        case TimecodeUserbits::PageLineClock:     return "PT";
                }
                return "?";
        }

} // namespace

TimecodeUserbits TimecodeUserbits::fromRawBits(uint32_t bits, Mode m) {
        TimecodeUserbits ub;
        for (size_t i = 0; i < NibbleCount; ++i) {
                ub._nibbles[i] = static_cast<uint8_t>((bits >> (i * 4)) & 0x0Fu);
        }
        ub._mode = m;
        return ub;
}

TimecodeUserbits TimecodeUserbits::fromNibbles(const Nibbles &n, Mode m) {
        TimecodeUserbits ub;
        for (size_t i = 0; i < NibbleCount; ++i) {
                ub._nibbles[i] = static_cast<uint8_t>(n[i] & 0x0Fu);
        }
        ub._mode = m;
        return ub;
}

TimecodeUserbits TimecodeUserbits::fromAsciiChars(const String &s) {
        TimecodeUserbits ub;
        ub._mode = EightBitChars;
        const char *cstr = s.cstr();
        const size_t len = s.byteCount();
        for (size_t i = 0; i < kAsciiCharCount; ++i) {
                uint8_t c = (i < len) ? static_cast<uint8_t>(cstr[i]) : uint8_t{0};
                ub._nibbles[i * 2 + 0] = static_cast<uint8_t>(c & 0x0Fu);
                ub._nibbles[i * 2 + 1] = static_cast<uint8_t>((c >> 4) & 0x0Fu);
        }
        return ub;
}

Result<TimecodeUserbits> TimecodeUserbits::fromDateTimeZone(const DateTime &dt) {
        // ST 309 encoding lands incrementally — packing a DateTime into
        // the eight nibbles per the spec needs full year/month/day BCD
        // layout plus the time-zone offset, which is more than the
        // value type wants to know about on its own.  Surface the gap
        // so callers can detect it cleanly.
        (void)dt;
        return makeError<TimecodeUserbits>(Error::NotSupported);
}

uint32_t TimecodeUserbits::toUint32() const {
        uint32_t out = 0;
        for (size_t i = 0; i < NibbleCount; ++i) {
                out |= static_cast<uint32_t>(_nibbles[i] & 0x0Fu) << (i * 4);
        }
        return out;
}

Result<String> TimecodeUserbits::asAsciiChars() const {
        if (_mode != EightBitChars) {
                return makeError<String>(Error::Invalid);
        }
        char buf[kAsciiCharCount + 1] = {0};
        for (size_t i = 0; i < kAsciiCharCount; ++i) {
                buf[i] = static_cast<char>((_nibbles[i * 2 + 0] & 0x0Fu) |
                                           ((_nibbles[i * 2 + 1] & 0x0Fu) << 4));
        }
        return makeResult(String(buf));
}

Result<DateTime> TimecodeUserbits::asDateTimeZone() const {
        if (_mode != DateTimeZone && _mode != DateTimeZoneClock) {
                return makeError<DateTime>(Error::Invalid);
        }
        // ST 309 decoder lands incrementally; signal the gap explicitly.
        return makeError<DateTime>(Error::NotSupported);
}

String TimecodeUserbits::toString() const {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "ub:%s:0x%08X", modeCode(_mode),
                      static_cast<unsigned>(toUint32()));
        return String(buf);
}

JsonObject TimecodeUserbits::toJson() const {
        JsonObject obj;
        obj.set("mode", static_cast<int64_t>(_mode));
        JsonArray nibbles;
        for (size_t i = 0; i < NibbleCount; ++i) {
                nibbles.add(static_cast<int64_t>(_nibbles[i]));
        }
        obj.set("nibbles", nibbles);
        obj.set("raw", static_cast<int64_t>(toUint32()));
        return obj;
}

// DataStream wire format (v1):
//   uint32_t raw       // packed 32-bit user-bit payload
//   uint8_t  mode      // BGF triple

Error TimecodeUserbits::writeToStream(DataStream &s) const {
        s << static_cast<uint32_t>(toUint32());
        s << static_cast<uint8_t>(_mode);
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<TimecodeUserbits> TimecodeUserbits::readFromStream<1>(DataStream &s) {
        uint32_t raw = 0;
        uint8_t  mode = 0;
        s >> raw >> mode;
        if (s.status() != DataStream::Ok) return makeError<TimecodeUserbits>(s.toError());
        return makeResult(TimecodeUserbits::fromRawBits(raw, static_cast<Mode>(mode & 0x07u)));
}

PROMEKI_NAMESPACE_END
