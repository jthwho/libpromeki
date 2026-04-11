/**
 * @file      string.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <type_traits>
#include <promeki/string.h>
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

template <typename T>
std::string floatToString(T value, int precision) {
        char buf[64];
        int len = std::snprintf(buf, sizeof(buf), "%.*f", precision, static_cast<double>(value));
        if(len < 0) return "0";
        return std::string(buf, static_cast<size_t>(len));
}

template <typename T>
static String num(T val,
              int base = 10,
              int padding = 0,
              char padchar = ' ',
              bool addPrefix = false) {
        if(base < 2 || base > 36) return String();

        std::string ret;
        ret.resize(128);

        char *buf = ret.data();
        bool isNegative = false;
        bool isPaddingNegative = false;

        // Compute the absolute value in the corresponding unsigned type.
        // Naively doing "val = -val" is UB for INT_MIN of the signed type
        // (e.g. int8_t(-128)), so we negate in unsigned space instead.
        using UT = std::make_unsigned_t<T>;
        UT uval;
        if constexpr (std::is_signed_v<T>) {
                if(val < 0) {
                        isNegative = true;
                        uval = static_cast<UT>(0) - static_cast<UT>(val);
                } else {
                        uval = static_cast<UT>(val);
                }
        } else {
                uval = val;
        }

        if(padding < 0) {
                isPaddingNegative = true;
                padding = -padding;
        }

        int index = 0;
        if(uval == 0) {
                buf[index++] = '0';
        } else {
                UT ubase = static_cast<UT>(base);
                while(uval > 0) {
                        UT r = uval % ubase;
                        buf[index++] = digits[r];
                        uval /= ubase;
                }
        }
        // Emit the minus sign for negatives.  Digits are being built in
        // reverse order, so appending here places the '-' at the start of
        // the final string after the reversal step below.
        if(isNegative) {
                buf[index++] = '-';
        }
        int digitEnd = index;

        if(addPrefix) {
                switch(base) {
                        case 2:  buf[index++] = 'b'; buf[index++] = '0'; break;
                        case 8:  buf[index++] = 'o'; buf[index++] = '0'; break;
                        case 16: buf[index++] = 'x'; buf[index++] = '0'; break;
                        case 10: break;
                        default:
                                buf[index++] = ':';
                                buf[index++] = digits[base % 10];
                                if(base / 10) buf[index++] = digits[base / 10];
                                buf[index++] = 'b';
                                break;
                }
        }
        int remaining = ret.size() - index - 1;
        padding -= index;
        if(padding < 0) {
                padding = 0;
                isPaddingNegative = false;
        }
        if(padding > remaining) padding = remaining;

        if(addPrefix && padchar == '0') {
                // Insert zero-padding between digits and prefix (so it appears
                // between prefix and digits after reversal, e.g. 0x0042)
                int prefixLen = index - digitEnd;
                // Shift prefix chars right by padding amount
                for(int i = index - 1; i >= digitEnd; i--) {
                        buf[i + padding] = buf[i];
                }
                for(int i = 0; i < padding; i++) buf[digitEnd + i] = padchar;
                index += padding;
        } else {
                for(int i = 0; i < padding; i++) buf[index++] = padchar;
        }
        buf[index] = 0;

        int lastPos = index - 1;
        if(isPaddingNegative) lastPos -= padding;

        for(int i = 0, j = lastPos; i < j; i++, j--) {
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
        if(length < 0) return String();
        if(static_cast<size_t>(length) > ret.size()) {
                ret.resize(length);
                va_start(args, fmt);
                std::vsnprintf(ret.data(), ret.size() + 1, fmt, args);
                va_end(args);
        } else {
                ret.resize(length);
        }
        return ret;
}

namespace {

String formatByteCount(uint64_t bytes, int maxDecimals, bool binary) {
        // Unit tables: binary uses powers of 1024 with IEC suffixes,
        // metric uses powers of 1000 with SI suffixes.  Only bytes
        // are shown without scaling.  The metric "K" is uppercased
        // for visual consistency with the binary "KiB" — strict SI
        // would write "kB" but libpromeki prefers the uppercase form
        // throughout its reporting.
        static const char *metricUnits[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };
        static const char *binaryUnits[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
        constexpr size_t unitCount = sizeof(metricUnits) / sizeof(metricUnits[0]);

        if(maxDecimals < 0) maxDecimals = 0;
        if(maxDecimals > 9) maxDecimals = 9;

        const double base = binary ? 1024.0 : 1000.0;
        const char *const *units = binary ? binaryUnits : metricUnits;

        // Bytes: no scaling, no decimal point.
        if(bytes < static_cast<uint64_t>(base)) {
                return String::sprintf("%llu %s",
                        (unsigned long long)bytes, units[0]);
        }

        double value = static_cast<double>(bytes);
        size_t unit = 0;
        while(value >= base && unit + 1 < unitCount) {
                value /= base;
                ++unit;
        }

        // Render with the requested number of decimals, then trim
        // trailing zeros (and the decimal point itself if the
        // fractional part is entirely zero).
        String ret = String::sprintf("%.*f %s", maxDecimals, value, units[unit]);
        if(maxDecimals > 0) {
                // Locate the decimal point and the space separator.
                // sprintf always writes them in that order: there is
                // exactly one '.' and one ' '.
                const size_t len = ret.size();
                size_t spacePos = len;
                for(size_t i = 0; i < len; ++i) {
                        if(ret.charAt(i) == ' ') { spacePos = i; break; }
                }
                size_t dotPos = spacePos;
                for(size_t i = 0; i < spacePos; ++i) {
                        if(ret.charAt(i) == '.') { dotPos = i; break; }
                }
                if(dotPos < spacePos) {
                        size_t lastKeep = spacePos;     // exclusive
                        while(lastKeep > dotPos + 1 && ret.charAt(lastKeep - 1) == '0') {
                                --lastKeep;
                        }
                        if(lastKeep == dotPos + 1) --lastKeep;  // drop the dot too
                        if(lastKeep < spacePos) {
                                ret.erase(lastKeep, spacePos - lastKeep);
                        }
                }
        }
        return ret;
}

} // namespace

String String::fromByteCount(uint64_t bytes, int maxDecimals) {
        return formatByteCount(bytes, maxDecimals, /*binary=*/false);
}

String String::fromByteCount(uint64_t bytes, int maxDecimals, const ByteCountStyle &style) {
        // TypedEnum pins the type statically, so a plain value()
        // compare against Binary's integer is sufficient — no
        // name-lookup dance required.
        return formatByteCount(bytes, maxDecimals,
                               style.value() == ByteCountStyle::Binary.value());
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
        const std::string &s = d->str();
        int minValue = std::numeric_limits<int>::max();
        size_t minPos = std::string::npos;
        std::string placeholderToReplace;
        for(size_t i = 0; i < s.size(); ++i) {
            if(s[i] == '%' && i + 1 < s.size() && std::isdigit(s[i + 1])) {
                size_t j = i + 1;
                while(j < s.size() && std::isdigit(s[j])) ++j;
                std::string placeholder = s.substr(i, j - i);
                int value = std::stoi(placeholder.substr(1));
                if(value < minValue) {
                    minValue = value;
                    minPos = i;
                    placeholderToReplace = placeholder;
                }
            }
        }
        if(minPos != std::string::npos) {
                std::string result = s;
                result.replace(minPos, placeholderToReplace.length(), str.d->str());
                *this = std::move(result);
        }
        return *this;
}

// ============================================================================
// Conversion
// ============================================================================

bool String::toBool(Error *e) const {
        Error err;
        bool ret = false;
        const std::string &s = d->str();
        if(s == "1") {
                ret = true;
        } else if(s == "0") {
                ret = false;
        } else {
                String lower = toLower();
                if(lower == "true") {
                        ret = true;
                } else if(lower == "false") {
                        ret = false;
                } else {
                        err = Error::Invalid;
                }
        }
        if(e != nullptr) *e = err;
        return ret;
}

int String::toInt(Error *e) const {
        Error err;
        int ret = 0;
        try {
                ret = std::stoi(d->str());
        } catch(const std::invalid_argument &) {
                err = Error::Invalid;
        } catch(const std::out_of_range &) {
                err = Error::OutOfRange;
        }
        if(e != nullptr) *e = err;
        return ret;
}

unsigned int String::toUInt(Error *e) const {
        Error err;
        unsigned long ret = 0;
        try {
                ret = std::stoul(d->str());
        } catch(const std::invalid_argument &) {
                err = Error::Invalid;
        } catch(const std::out_of_range &) {
                err = Error::OutOfRange;
        }
        if(ret > static_cast<unsigned long>(std::numeric_limits<unsigned int>::max())) err = Error::OutOfRange;
        if(e != nullptr) *e = err;
        return ret;
}

double String::toDouble(Error *e) const {
        Error err;
        double ret = 0.0;
        try {
                ret = std::stod(d->str());
        } catch(const std::invalid_argument &) {
                err = Error::Invalid;
        } catch(const std::out_of_range &) {
                err = Error::OutOfRange;
        }
        if(e != nullptr) *e = err;
        return ret;
}

int64_t String::parseNumberWords(Error *err) const {
        static const std::map<std::string, int64_t> numberWords = {
                {"zero", 0}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4},
                {"five", 5}, {"six", 6}, {"seven", 7}, {"eight", 8}, {"nine", 9},
                {"ten", 10}, {"eleven", 11}, {"twelve", 12}, {"thirteen", 13},
                {"fourteen", 14}, {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17},
                {"eighteen", 18}, {"nineteen", 19}, {"twenty", 20}, {"thirty", 30},
                {"forty", 40}, {"fifty", 50}, {"sixty", 60}, {"seventy", 70},
                {"eighty", 80}, {"ninety", 90}, {"hundred", 100}, {"thousand", 1000},
                {"million", 1000000}, {"billion", 1000000000}
        };

        std::string copy = d->str();
        for(char &c : copy) if(!std::isalpha(c)) c = ' ';
        // Tokenize by whitespace
        StringList tokens = String(copy).split(" ");
        int64_t value = 0;
        int64_t current = 0;
        bool found = false;

        for(size_t ti = 0; ti < tokens.size(); ++ti) {
                std::string token = tokens[ti].str();
                for(char &c : token) c = std::tolower(static_cast<unsigned char>(c));
                auto it = numberWords.find(token);
                if(it != numberWords.end()) {
                        found = true;
                        int64_t wordval = it->second;
                        if(wordval >= 1000) {
                                if(!current) current = 1;
                                value += current * wordval;
                                current = 0;
                        } else if(wordval >= 100) {
                                if(!current) current = 1;
                                current *= wordval;
                        } else {
                                current += wordval;
                        }
                } else if(token == "and") {
                        continue;
                } else {
                        break;
                }
        }
        value += current;
        if(err != nullptr) *err = found ? Error::Ok : Error::Invalid;
        return value;
}

// ============================================================================
// Replace
// ============================================================================

String String::replace(const String &find, const String &replacement) const {
        if(find.isEmpty()) return *this;
        String result;
        size_t start = 0;
        size_t pos;
        while((pos = this->find(find, start)) != npos) {
                if(pos > start) result += substr(start, pos - start);
                result += replacement;
                start = pos + find.length();
        }
        if(start == 0) return *this;
        if(start < length()) result += substr(start);
        return result;
}

// ============================================================================
// Split
// ============================================================================

StringList String::split(const std::string& delimiter) const {
        StringList result;
        const std::string &s = d->str();
        size_t start = 0;
        size_t pos;
        while((pos = s.find(delimiter, start)) != std::string::npos) {
                std::string token = s.substr(start, pos - start);
                if(!token.empty()) result += String(token);
                start = pos + delimiter.length();
        }
        std::string last = s.substr(start);
        if(!last.empty()) result += String(last);
        return result;
}

PROMEKI_NAMESPACE_END
