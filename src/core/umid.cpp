/**
 * @file      umid.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <ctime>
#include <promeki/umid.h>
#include <promeki/random.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

// SMPTE 330M Universal Label for a UMID.  The 12-byte prefix is a
// fixed SMPTE-registered key; the final byte of the designator
// (0x20) signals the Material Number generation method — 0x20
// denotes a random 16-byte material number, which is what
// generate() produces.
static const uint8_t kUniversalLabel[UMID::UniversalLabelSize] = {
        0x06, 0x0A, 0x2B, 0x34,
        0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x0F, 0x20
};

// Length byte values per SMPTE 330M — count of bytes following the
// length byte itself, i.e. (Instance + Material + SourcePack).
static constexpr uint8_t kLengthBasic    = 0x13;   // 19 bytes of payload after length.
static constexpr uint8_t kLengthExtended = 0x33;   // 51 bytes of payload after length.

// Four-byte libpromeki signature placed in the Extended UMID
// Organization field so a UMID alone is sufficient to identify
// files written by libpromeki.
static constexpr uint8_t kOrganizationTag[4] = { 'M', 'E', 'K', 'I' };

// Offsets into the UMID byte layout.
static constexpr size_t kOffLength       = 12;
static constexpr size_t kOffInstance     = 13;
static constexpr size_t kOffMaterial     = 16;
static constexpr size_t kOffTimeDate     = 32;
static constexpr size_t kOffSpatial      = 40;
static constexpr size_t kOffCountry      = 52;
static constexpr size_t kOffOrganization = 56;
static constexpr size_t kOffUser         = 60;

static void fillSourcePackTimeDate(uint8_t *out) {
        // 8-byte Time/Date field.  SMPTE 330M does not nail down an
        // exact encoding for every use case; this layout is the
        // practical de-facto form used by several MXF and BWF
        // implementations and is both compact and human-decodable.
        //
        //  Offset  Size  Meaning
        //       0     2  Year (big-endian)
        //       2     1  Month (1..12)
        //       3     1  Day (1..31)
        //       4     1  Hour (0..23, UTC)
        //       5     1  Minute (0..59)
        //       6     1  Second (0..59)
        //       7     1  Frame (reserved, 0)
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        gmtime_r(&t, &tm);
        int year = tm.tm_year + 1900;
        out[0] = static_cast<uint8_t>((year >> 8) & 0xFF);
        out[1] = static_cast<uint8_t>(year & 0xFF);
        out[2] = static_cast<uint8_t>(tm.tm_mon + 1);
        out[3] = static_cast<uint8_t>(tm.tm_mday);
        out[4] = static_cast<uint8_t>(tm.tm_hour);
        out[5] = static_cast<uint8_t>(tm.tm_min);
        out[6] = static_cast<uint8_t>(tm.tm_sec);
        out[7] = 0;
}

UMID UMID::generate(Length len) {
        if(len != Basic && len != Extended) {
                promekiErr("UMID::generate: unsupported length %d", static_cast<int>(len));
                return UMID();
        }

        UMID ret;
        ret._length = len;

        // Universal Label
        std::memcpy(ret.d.data(), kUniversalLabel, UniversalLabelSize);

        // Length byte
        ret.d[kOffLength] = (len == Extended) ? kLengthExtended : kLengthBasic;

        // Instance Number — zero for the root instance.
        ret.d[kOffInstance + 0] = 0;
        ret.d[kOffInstance + 1] = 0;
        ret.d[kOffInstance + 2] = 0;

        // Material Number — 16 random bytes.
        Error err = Random::trueRandom(ret.d.data() + kOffMaterial, 16);
        if(err.isError()) {
                promekiErr("UMID::generate: random material number failed: %s",
                                err.name().cstr());
                return UMID();
        }

        if(len == Extended) {
                // Source Pack: Time/Date + Spatial + Country + Organization + User.
                fillSourcePackTimeDate(ret.d.data() + kOffTimeDate);
                // Spatial Coordinates (12 bytes) — leave zeroed.
                std::memset(ret.d.data() + kOffSpatial, 0, 12);
                // Country (4 bytes) — leave zeroed.
                std::memset(ret.d.data() + kOffCountry, 0, 4);
                // Organization (4 bytes) — "MEKI" libpromeki signature.
                std::memcpy(ret.d.data() + kOffOrganization, kOrganizationTag, 4);
                // User (4 bytes) — leave zeroed.
                std::memset(ret.d.data() + kOffUser, 0, 4);
        }

        return ret;
}

UMID UMID::fromBytes(const uint8_t *bytes, size_t byteLen) {
        if(bytes == nullptr) return UMID();
        Length len;
        if(byteLen == BasicSize) {
                len = Basic;
        } else if(byteLen == ExtendedSize) {
                len = Extended;
        } else {
                return UMID();
        }
        UMID ret;
        ret._length = len;
        std::memcpy(ret.d.data(), bytes, byteLen);
        // The Array is zero-initialized in the default constructor, so
        // the trailing 32 bytes for a Basic UMID are already zero.
        return ret;
}

static int hexVal(char c) {
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
}

UMID UMID::fromString(const String &str, Error *err) {
        // Strip whitespace and dashes from the input; accept either
        // pure hex or a dash-separated grouping for readability.
        const char *src = str.cstr();
        uint8_t bytes[ExtendedSize];
        size_t byteCount = 0;
        int nibble = -1;

        while(*src != '\0') {
                char c = *src++;
                if(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-') continue;
                int v = hexVal(c);
                if(v < 0) {
                        if(err != nullptr) *err = Error::Invalid;
                        return UMID();
                }
                if(nibble < 0) {
                        nibble = v;
                } else {
                        if(byteCount >= ExtendedSize) {
                                if(err != nullptr) *err = Error::Invalid;
                                return UMID();
                        }
                        bytes[byteCount++] = static_cast<uint8_t>((nibble << 4) | v);
                        nibble = -1;
                }
        }
        if(nibble >= 0) {
                // Odd number of hex digits.
                if(err != nullptr) *err = Error::Invalid;
                return UMID();
        }

        Length len;
        if(byteCount == BasicSize) {
                len = Basic;
        } else if(byteCount == ExtendedSize) {
                len = Extended;
        } else {
                if(err != nullptr) *err = Error::Invalid;
                return UMID();
        }

        UMID ret;
        ret._length = len;
        std::memcpy(ret.d.data(), bytes, byteCount);
        if(err != nullptr) *err = Error::Ok;
        return ret;
}

String UMID::toString() const {
        if(_length == Invalid) return String();
        static const char digits[] = "0123456789abcdef";
        const size_t n = static_cast<size_t>(_length);
        char buf[ExtendedSize * 2 + 1];
        char *out = buf;
        for(size_t i = 0; i < n; ++i) {
                uint8_t b = d[i];
                *out++ = digits[b >> 4];
                *out++ = digits[b & 0x0F];
        }
        *out = '\0';
        return String(buf, n * 2);
}

PROMEKI_NAMESPACE_END
