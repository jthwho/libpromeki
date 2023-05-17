/*****************************************************************************
 * string.cpp
 * April 26, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/error.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

static const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

template <typename T>
static String num(T val, 
              int base = 10, 
              int padding = 0, 
              char padchar = ' ',
              bool addPrefix = false) {
        if(base < 2 || base > 36) {
                // FIXME: Report an error the base is out of bounds
                return String();
        }

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
                        case 10: /* base 10 has no prefix */; break;
                        // There doesn't seem to be any common prefix notation for arbitary number
                        // bases, so inventing one here.
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
        if(padding > remaining) {
                // FIXME: Report a warning that we've run out of space for the requested padding.
                padding = remaining;
        }
        for(int i = 0; i < padding; i++) buf[index++] = padchar;
        buf[index] = 0; // And the null terminator like the candle on the C string cake.
        
        // In the case the padding was negative, it comes after the number so we leave it
        // where it is in the string reverse.
        int lastPos = index - 1;
        if(isPaddingNegative) lastPos -= padding;

        // Now reverse the whole damn thing because it's easier to build a number in reverse.
        for (int i = 0, j = lastPos; i < j; i++, j--) {
                char temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
        }
        ret.resize(index);
        //promekiInfo("str: %d, base %d, pad %d, pc '%c', %s, index %d, ret '%s'",
        //        (int)val, (int)base, (int)padding, (int)padchar, addPrefix ? "prefix" : "noprefix", 
        //        (int)index, ret.c_str());
        return String(ret);
}

String String::sprintf(const char *fmt, ...) {
        std::string ret;
        ret.resize(256); // Pick a good first guess at the size.
        // Attempt to format the string into the reserved buffer
        va_list args;
        va_start(args, fmt);
        int length = std::vsnprintf(ret.data(), ret.size() + 1, fmt, args);
        va_end(args);
        if(length < 0) return String();
        // Check if the string was longer than our initial guess.  If not,
        // do it again with the actual length.
        if(static_cast<size_t>(length) > ret.size()) {
                ret.resize(length);
                va_start(args, fmt);
                std::vsnprintf(ret.data(), ret.size() + 1, fmt, args);
                va_end(args);
        } else {
                // The reserved size was sufficient; update the string size
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

String &String::arg(const String &str) {
        // Find the lowest numbered unreplaced placeholder.
        int minValue = std::numeric_limits<int>::max();
        size_t minPos = std::string::npos;
        std::string placeholderToReplace;
        for(size_t i = 0; i < d.size(); ++i) {
            if(d[i] == '%' && i + 1 < d.size() && std::isdigit(d[i + 1])) {
                size_t j = i + 1;
                while (j < d.size() && std::isdigit(d[j])) ++j;
                std::string placeholder = d.substr(i, j - i);
                int value = std::stoi(placeholder.substr(1));
                if(value < minValue) {
                    minValue = value;
                    minPos = i;
                    placeholderToReplace = placeholder;
                }
            }
        }

        // Replace the found placeholder with the argument value.
        if(minPos != std::string::npos) d.replace(minPos, placeholderToReplace.length(), str.d);
        return *this;
}

bool String::toBool(Error *e) const {
        Error err;
        bool ret = false;
        if(d == "1") {
                ret = true;
        } else if(d == "0") {
                ret = false;
        } else {
                String s = toLower();
                if(s == "true") {
                        ret = true;
                } else if(s == "false") {
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
        int ret;
        try {
                ret = std::stoi(d);
        } catch(const std::invalid_argument &) {
                err = Error::Invalid;
        } catch (const std::out_of_range &) {
                err = Error::OutOfRange;
        }
        if(e != nullptr) *e = err;
        return ret;
}

unsigned int String::toUInt(Error *e) const {
        Error err;
        unsigned long ret;
        try {
                ret = std::stoul(d);
        } catch(const std::invalid_argument &) {
                err = Error::Invalid;
        } catch (const std::out_of_range &) {
                err = Error::OutOfRange;
        }
        if(ret > static_cast<unsigned long>(std::numeric_limits<unsigned int>::max())) err = Error::OutOfRange;
        if(e != nullptr) *e = err;
        return ret;
}

double String::toDouble(Error *e) const {
        Error err;
        double ret;
        try {
                ret = std::stod(d);
        } catch(const std::invalid_argument &) {
                err = Error::Invalid;
        } catch (const std::out_of_range &) {
                err = Error::OutOfRange;
        }
        if(e != nullptr) *e = err;
        return ret;
}

int64_t String::parseNumberWords(bool *success) const {
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

        std::string copy = d;
        for(char &c : copy) if(!std::isalpha(c)) c = ' ';
        std::istringstream iss(copy);
        std::string token;
        int64_t value = 0;
        int64_t current = 0;
        bool found = false;

        while (iss >> token) {
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
                        //promekiInfo("%s %lld %lld", token.c_str(), (long long)current, (long long)value);
                } else if (token == "and") {
                        continue;
                } else {
                        break;
                }
        }
        value += current;
        if(success != nullptr) *success = found;
        return value;
}

StringList String::split(const std::string& delimiter) const {
        StringList result;
        size_t pos = 0;
        std::string str = d;
        while((pos = str.find(delimiter)) != std::string::npos) {
                String token = str.substr(0, pos);
                if (!token.isEmpty()) {
                        result += token;
                }
                str.erase(0, pos + delimiter.length());
        }
        if(!str.empty()) result += str;
        return result;
}

PROMEKI_NAMESPACE_END

