/**
 * @file      string.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <promeki/string.h>
#include <promeki/map.h>
#include <promeki/stringlist.h>
#include <promeki/error.h>
#include <promeki/enum.h>
#include <promeki/enums.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Number formatting helpers
// ============================================================================

static const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

template <typename T> std::string floatToString(T value, int precision) {
        char buf[64];
        int  len = std::snprintf(buf, sizeof(buf), "%.*f", precision, static_cast<double>(value));
        if (len < 0) return "0";
        return std::string(buf, static_cast<size_t>(len));
}

template <typename T>
static String num(T val, int base = 10, int padding = 0, char padchar = ' ', bool addPrefix = false) {
        if (base < 2 || base > 36) return String();

        std::string ret;
        ret.resize(128);

        char *buf = ret.data();
        bool  isNegative = false;
        bool  isPaddingNegative = false;

        // Compute the absolute value in the corresponding unsigned type.
        // Naively doing "val = -val" is UB for INT_MIN of the signed type
        // (e.g. int8_t(-128)), so we negate in unsigned space instead.
        using UT = std::make_unsigned_t<T>;
        UT uval;
        if constexpr (std::is_signed_v<T>) {
                if (val < 0) {
                        isNegative = true;
                        uval = static_cast<UT>(0) - static_cast<UT>(val);
                } else {
                        uval = static_cast<UT>(val);
                }
        } else {
                uval = val;
        }

        if (padding < 0) {
                isPaddingNegative = true;
                padding = -padding;
        }

        int index = 0;
        if (uval == 0) {
                buf[index++] = '0';
        } else {
                UT ubase = static_cast<UT>(base);
                while (uval > 0) {
                        UT r = uval % ubase;
                        buf[index++] = digits[r];
                        uval /= ubase;
                }
        }
        // Emit the minus sign for negatives.  Digits are being built in
        // reverse order, so appending here places the '-' at the start of
        // the final string after the reversal step below.
        if (isNegative) {
                buf[index++] = '-';
        }
        int digitEnd = index;

        if (addPrefix) {
                switch (base) {
                        case 2:
                                buf[index++] = 'b';
                                buf[index++] = '0';
                                break;
                        case 8:
                                buf[index++] = 'o';
                                buf[index++] = '0';
                                break;
                        case 16:
                                buf[index++] = 'x';
                                buf[index++] = '0';
                                break;
                        case 10: break;
                        default:
                                buf[index++] = ':';
                                buf[index++] = digits[base % 10];
                                if (base / 10) buf[index++] = digits[base / 10];
                                buf[index++] = 'b';
                                break;
                }
        }
        int remaining = ret.size() - index - 1;
        padding -= index;
        if (padding < 0) {
                padding = 0;
                isPaddingNegative = false;
        }
        if (padding > remaining) padding = remaining;

        if (addPrefix && padchar == '0') {
                // Insert zero-padding between digits and prefix (so it appears
                // between prefix and digits after reversal, e.g. 0x0042)
                // Shift prefix chars right by padding amount
                for (int i = index - 1; i >= digitEnd; i--) {
                        buf[i + padding] = buf[i];
                }
                for (int i = 0; i < padding; i++) buf[digitEnd + i] = padchar;
                index += padding;
        } else {
                for (int i = 0; i < padding; i++) buf[index++] = padchar;
        }
        buf[index] = 0;

        int lastPos = index - 1;
        if (isPaddingNegative) lastPos -= padding;

        for (int i = 0, j = lastPos; i < j; i++, j--) {
                char temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
        }
        ret.resize(index);
        return String(ret);
}

// ============================================================================
// Static factory methods
// ============================================================================

String String::sprintf(const char *fmt, ...) {
        std::string ret;
        ret.resize(256);
        va_list args;
        va_start(args, fmt);
        int length = std::vsnprintf(ret.data(), ret.size() + 1, fmt, args);
        va_end(args);
        if (length < 0) return String();
        if (static_cast<size_t>(length) > ret.size()) {
                ret.resize(length);
                va_start(args, fmt);
                std::vsnprintf(ret.data(), ret.size() + 1, fmt, args);
                va_end(args);
        } else {
                ret.resize(length);
        }
        return ret;
}

String String::number(int8_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(uint8_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(int16_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(uint16_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(int32_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(uint32_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(int64_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(uint64_t val, int base, int padding, char padchar, bool addPrefix) {
        return num(val, base, padding, padchar, addPrefix);
}

String String::number(float val, int precision) {
        return floatToString(val, precision);
}

String String::number(double val, int precision) {
        return floatToString(val, precision);
}

// ============================================================================
// Arg replacement
// ============================================================================

String &String::arg(const String &str) {
        // Find the placeholder `%N` (N a run of decimal digits) with the
        // smallest N.  Iterate by Char so multi-byte UTF-8 runs in the
        // surrounding text are handled correctly: '%' and ASCII digits
        // round-trip through Char::codepoint() as themselves.
        int          minValue = std::numeric_limits<int>::max();
        size_t       minPos = npos;
        size_t       minLen = 0;
        const size_t n = length();
        for (size_t i = 0; i < n; ++i) {
                if (charAt(i).codepoint() != '%') continue;
                size_t j = i + 1;
                if (j >= n || !charAt(j).isDigit()) continue;
                int value = 0;
                while (j < n && charAt(j).isDigit()) {
                        value = value * 10 + static_cast<int>(charAt(j).codepoint() - '0');
                        ++j;
                }
                if (value < minValue) {
                        minValue = value;
                        minPos = i;
                        minLen = j - i;
                }
        }
        if (minPos != npos) {
                String result;
                result.reserve(byteCount() + str.byteCount());
                result += left(minPos);
                result += str;
                result += mid(minPos + minLen);
                *this = std::move(result);
        }
        return *this;
}

// ============================================================================
// Conversion
// ============================================================================

// ----------------------------------------------------------------------------
// Numeric string preprocessing
// ----------------------------------------------------------------------------

String String::stripNumericSeparators(const char *s) {
        String result;
        while (*s) {
                char c = *s++;
                if (c == '\'' || c == '_' || c == ',') continue;
                result += c;
        }
        return result;
}

String String::prepareIntParse(const char *s, int *base) {
        String result;
        *base = 10;
        const char *p = s;

        // Preserve leading whitespace (strtoll skips it anyway).
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
                result += *p++;
        }

        // Preserve optional sign.
        if (*p == '+' || *p == '-') {
                result += *p++;
        }

        // Detect and consume base prefix.
        if (*p == '0' && p[1] != '\0') {
                char next = p[1];
                if (next == 'x' || next == 'X') {
                        *base = 16;
                        p += 2;
                } else if (next == 'b' || next == 'B') {
                        *base = 2;
                        p += 2;
                } else if (next == 'o' || next == 'O') {
                        *base = 8;
                        p += 2;
                }
        }

        // Copy remaining characters, stripping separators.
        while (*p) {
                char c = *p++;
                if (c == '\'' || c == '_' || c == ',') continue;
                result += c;
        }

        return result;
}

// ----------------------------------------------------------------------------
// Convenience numeric conversions
// ----------------------------------------------------------------------------

bool String::toBool(Error *e) const {
        Error              err;
        bool               ret = false;
        const std::string &s = d->str();
        if (s == "1") {
                ret = true;
        } else if (s == "0") {
                ret = false;
        } else {
                String lower = toLower();
                if (lower == "true") {
                        ret = true;
                } else if (lower == "false") {
                        ret = false;
                } else {
                        err = Error::Invalid;
                }
        }
        if (e != nullptr) *e = err;
        return ret;
}

int String::toInt(Error *e) const {
        int         base = 10;
        String      cleaned = prepareIntParse(cstr(), &base);
        char       *end = nullptr;
        const char *s = cleaned.cstr();
        errno = 0;
        long long v = std::strtoll(s, &end, base);
        if (end == s || *end != '\0') {
                if (e != nullptr) *e = Error::Invalid;
                return 0;
        }
        if (errno == ERANGE || v > std::numeric_limits<int>::max() || v < std::numeric_limits<int>::min()) {
                if (e != nullptr) *e = Error::OutOfRange;
                return 0;
        }
        if (e != nullptr) *e = Error::Ok;
        return static_cast<int>(v);
}

unsigned int String::toUInt(Error *e) const {
        int         base = 10;
        String      cleaned = prepareIntParse(cstr(), &base);
        char       *end = nullptr;
        const char *s = cleaned.cstr();
        errno = 0;
        unsigned long long v = std::strtoull(s, &end, base);
        if (end == s || *end != '\0') {
                if (e != nullptr) *e = Error::Invalid;
                return 0;
        }
        if (errno == ERANGE || v > std::numeric_limits<unsigned int>::max()) {
                if (e != nullptr) *e = Error::OutOfRange;
                return 0;
        }
        if (e != nullptr) *e = Error::Ok;
        return static_cast<unsigned int>(v);
}

int64_t String::toInt64(Error *e) const {
        int         base = 10;
        String      cleaned = prepareIntParse(cstr(), &base);
        char       *end = nullptr;
        const char *s = cleaned.cstr();
        errno = 0;
        long long v = std::strtoll(s, &end, base);
        if (end == s || *end != '\0') {
                if (e != nullptr) *e = Error::Invalid;
                return 0;
        }
        // strtoll's own out-of-range signal — int64_t and long long
        // are not guaranteed to be the same width on every platform
        // (LLP64 systems make long long 64-bit and long 32-bit), but
        // every platform we ship to keeps long long >= 64 bits, so the
        // bounds-vs-int64_t check below is the operative range guard.
        if (errno == ERANGE || v > std::numeric_limits<int64_t>::max() ||
            v < std::numeric_limits<int64_t>::min()) {
                if (e != nullptr) *e = Error::OutOfRange;
                return 0;
        }
        if (e != nullptr) *e = Error::Ok;
        return static_cast<int64_t>(v);
}

uint64_t String::toUInt64(Error *e) const {
        int         base = 10;
        String      cleaned = prepareIntParse(cstr(), &base);
        char       *end = nullptr;
        const char *s = cleaned.cstr();
        errno = 0;
        unsigned long long v = std::strtoull(s, &end, base);
        if (end == s || *end != '\0') {
                if (e != nullptr) *e = Error::Invalid;
                return 0;
        }
        if (errno == ERANGE || v > std::numeric_limits<uint64_t>::max()) {
                if (e != nullptr) *e = Error::OutOfRange;
                return 0;
        }
        if (e != nullptr) *e = Error::Ok;
        return static_cast<uint64_t>(v);
}

double String::toDouble(Error *e) const {
        String      cleaned = stripNumericSeparators(cstr());
        char       *end = nullptr;
        const char *s = cleaned.cstr();
        errno = 0;
        double v = std::strtod(s, &end);
        if (end == s || *end != '\0') {
                if (e != nullptr) *e = Error::Invalid;
                return 0.0;
        }
        if (errno == ERANGE) {
                if (e != nullptr) *e = Error::OutOfRange;
                return 0.0;
        }
        if (e != nullptr) *e = Error::Ok;
        return v;
}

int64_t String::parseNumberWords(Error *err) const {
        static const Map<std::string, int64_t> numberWords = {
                {"zero", 0},      {"one", 1},         {"two", 2},           {"three", 3},
                {"four", 4},      {"five", 5},        {"six", 6},           {"seven", 7},
                {"eight", 8},     {"nine", 9},        {"ten", 10},          {"eleven", 11},
                {"twelve", 12},   {"thirteen", 13},   {"fourteen", 14},     {"fifteen", 15},
                {"sixteen", 16},  {"seventeen", 17},  {"eighteen", 18},     {"nineteen", 19},
                {"twenty", 20},   {"thirty", 30},     {"forty", 40},        {"fifty", 50},
                {"sixty", 60},    {"seventy", 70},    {"eighty", 80},       {"ninety", 90},
                {"hundred", 100}, {"thousand", 1000}, {"million", 1000000}, {"billion", 1000000000}};

        std::string copy = d->str();
        for (char &c : copy)
                if (!std::isalpha(c)) c = ' ';
        // Tokenize by whitespace
        StringList tokens = String(copy).split(" ");
        int64_t    value = 0;
        int64_t    current = 0;
        bool       found = false;

        for (size_t ti = 0; ti < tokens.size(); ++ti) {
                std::string token = tokens[ti].str();
                for (char &c : token) c = std::tolower(static_cast<unsigned char>(c));
                auto it = numberWords.find(token);
                if (it != numberWords.end()) {
                        found = true;
                        int64_t wordval = it->second;
                        if (wordval >= 1000) {
                                if (!current) current = 1;
                                value += current * wordval;
                                current = 0;
                        } else if (wordval >= 100) {
                                if (!current) current = 1;
                                current *= wordval;
                        } else {
                                current += wordval;
                        }
                } else if (token == "and") {
                        continue;
                } else {
                        break;
                }
        }
        value += current;
        if (err != nullptr) *err = found ? Error::Ok : Error::Invalid;
        return value;
}

// ============================================================================
// Replace
// ============================================================================

String String::replace(const String &find, const String &replacement) const {
        if (find.isEmpty()) return *this;
        String result;
        size_t start = 0;
        size_t pos;
        while ((pos = this->find(find, start)) != npos) {
                if (pos > start) result += substr(start, pos - start);
                result += replacement;
                start = pos + find.length();
        }
        if (start == 0) return *this;
        if (start < length()) result += substr(start);
        return result;
}

// ============================================================================
// Split
// ============================================================================

StringList String::split(const String &delimiter) const {
        StringList result;
        if (delimiter.isEmpty()) {
                if (!isEmpty()) result += *this;
                return result;
        }
        size_t start = 0;
        size_t pos;
        while ((pos = find(delimiter, start)) != npos) {
                if (pos > start) result += substr(start, pos - start);
                start = pos + delimiter.length();
        }
        if (start < length()) result += substr(start);
        return result;
}

StringList String::split(const char *delimiter) const {
        return split(String(delimiter));
}

StringList String::split(char delimiter) const {
        return split(String(1, delimiter));
}

PROMEKI_NAMESPACE_END
