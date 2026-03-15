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
#include <promeki/core/string.h>
#include <promeki/core/stringlist.h>
#include <promeki/core/error.h>
#include <promeki/core/logger.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Number formatting helpers
// ============================================================================

static const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

template <typename T>
std::string floatToString(T value, int precision, std::ios_base::fmtflags encodingStyle = std::ios_base::fixed) {
        std::stringstream ss;
        ss.setf(encodingStyle);
        ss << std::setprecision(precision) << value;
        return ss.str();
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

        if(val < 0) {
                isNegative = true;
                val = -val;
        }
        if(padding < 0) {
                isPaddingNegative = true;
                padding = -padding;
        }

        int index = 0;
        if(val == 0) {
                buf[index++] = '0';
        } else {
                T remainder;
                while(val > 0) {
                        T r = val % base;
                        buf[index++] = digits[r];
                        val /= base;
                }
        }
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
        for(int i = 0; i < padding; i++) buf[index++] = padchar;
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
        std::istringstream iss(copy);
        std::string token;
        int64_t value = 0;
        int64_t current = 0;
        bool found = false;

        while(iss >> token) {
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
