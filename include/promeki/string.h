/*****************************************************************************
 * string.h
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

#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <promeki/util.h>

namespace promeki {

class Error;

class String {
        public:
                static const size_t npos = std::string::npos;

                static constexpr const char *WhitespaceChars = " \t\n\r\f\v";
 
                static String number(int8_t value, 
                                int base = 10, 
                                int padding = 0, 
                                char padchar = ' ',
                                bool addPrefix = false);

                static String number(uint8_t value, 
                                int base = 10, 
                                int padding = 0, 
                                char padchar = ' ',
                                bool addPrefix = false);

                static String number(int16_t value, 
                                int base = 10, 
                                int padding = 0, 
                                char padchar = ' ',
                                bool addPrefix = false);

                static String number(uint16_t value, 
                                int base = 10, 
                                int padding = 0, 
                                char padchar = ' ',
                                bool addPrefix = false);

                static String number(int32_t value, 
                                int base = 10, 
                                int padding = 0, 
                                char padchar = ' ',
                                bool addPrefix = false);

                static String number(uint32_t value, 
                                int base = 10, 
                                int padding = 0, 
                                char padchar = ' ',
                                bool addPrefix = false);

                static String number(int64_t value, 
                                int base = 10, 
                                int padding = 0, 
                                char padchar = ' ',
                                bool addPrefix = false);

                static String number(uint64_t value, 
                                int base = 10, 
                                int padding = 0, 
                                char padchar = ' ',
                                bool addPrefix = false);


                template <typename T>
                static String dec(const T &val, int padding = 0, char padchar = ' ') {
                        std::ostringstream oss;
                        oss << std::setw(padding) << std::setfill(padchar) << val;
                        return oss.str();
                }

                template <typename T>
                static String hex(const T &val, int padding = 0, bool addPrefix = true) {
                        std::ostringstream oss;
                        if(addPrefix) oss << "0x";
                        oss << std::setw(padding) << std::setfill('0') << std::hex << val;
                        return oss.str();
                }

                template <typename T>
                static String bin(const T &val, int digits = 32, bool addPrefix = true) {
                        std::string ret;
                        if(addPrefix) ret += "0b";
                        T mask = static_cast<T>(1) << (digits - 1);
                        for(int i = 0; i < digits; i++) {
                                ret.push_back(val & mask ? '1' : '0');
                                mask >>= 1;
                        }
                        return ret;
                }

                PROMEKI_PRINTF_FUNC(1, 2) static String sprintf(const char *fmt, ...);

                String() { }
                String(const char *str) : d(str) { }
                String(const char *str, size_t len) : d(str, len) { }
                String(size_t ct, char c) : d(ct, c) { }
                String(const std::string &str) : d(str) { }
                String(const std::string &&str) : d(str) { }

                std::string &stds() {
                        return d;
                }

                const std::string &stds() const {
                        return d;
                }

                const char *cstr() const {
                        return d.c_str();
                }

                size_t size() const {
                        return d.size();
                }

                void resize(size_t val) {
                        d.resize(val);
                        return;
                }

                size_t length() const {
                        return d.length();
                }

                bool empty() const {
                        return d.empty();
                }

                size_t find(char val) const {
                        return d.find(val);
                }

                String substr(size_t pos = 0, size_t len = npos) {
                        return substr(pos, len);
                }

                String &operator=(const std::string &str) {
                        d = str;
                        return *this;
                }

                String &operator=(const std::string &&str) {
                        d = str;
                        return *this;
                }

                String operator+(const String &val) const {
                        return String(d + val.d);
                }

                String operator+(const std::string &val) const {
                        return String(d + val);
                }

                String operator+(const char *val) const {
                        return String(d + val);
                }

                String operator+(char val) const {
                        return String(d + val);
                }

                String &operator+=(const String &val) {
                        d += val.d;
                        return *this;
                }

                String &operator+=(const std::string &val) {
                        d += val;
                        return *this;
                }

                String &operator+=(const char *val) {
                        d += val;
                        return *this;
                }

                String &operator+=(char val) {
                        d += val;
                        return *this;
                }

                char &operator[](int index) {
                        return d[index];
                }

                const char &operator[](int index) const {
                        return d[index];
                }

                bool operator==(const String &val) const {
                        return d == val.d;
                }

                bool operator==(const char *val) const {
                        return d == val;
                }

                bool operator==(char val) const {
                        return d.size() == 1 && d[0] == val;
                }

                bool operator!=(const String &val) const {
                        return d != val.d;
                }

                bool operator!=(const char *val) const {
                        return d != val;
                }

                bool operator!=(char val) const {
                        return d.size() != 1 || d[0] != val;
                }

               friend std::ostream &operator<<(std::ostream& os, const String &val) {
                        os << val.d;
                        return os;
                }

                String toUpper() const {
                        std::string result(d);
                        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
                        return result;
                }

                String toLower() const {
                        std::string result(d);
                        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
                        return result;
                }

                String trim() const {
                        std::string result;
                        size_t first = d.find_first_not_of(WhitespaceChars);
                        if(first != std::string::npos) {
                                size_t last = d.find_last_not_of(WhitespaceChars);
                                result = d.substr(first, last - first + 1);
                        }
                        return result;
                }

                std::vector<String> split(const std::string& delimiter) const {
                        std::vector<String> result;
                        size_t pos = 0;
                        std::string str = d;
                        while((pos = str.find(delimiter)) != std::string::npos) {
                                String token = str.substr(0, pos);
                                if (!token.empty()) {
                                        result.push_back(token);
                                }
                                str.erase(0, pos + delimiter.length());
                        }
                        if(!str.empty()) {
                                result.push_back(str);
                        }
                        return result;
                }

                bool startsWith(const String &prefix) const {
                        return d.compare(0, prefix.size(), prefix.d) == 0;
                }

                bool endsWith(const String &suffix) const {
                        if(suffix.size() > size()) return false;
                        return std::equal(suffix.d.rbegin(), suffix.d.rend(), d.rbegin());
                }

                size_t count(const String &substr) const {
                        size_t count = 0;
                        size_t pos = 0;
                        while((pos = d.find(substr.d, pos)) != std::string::npos) {
                                ++count;
                                pos += substr.length();
                        }
                        return count;
                }

                String reverse() const {
                        std::string result(d);
                        std::reverse(result.begin(), result.end());
                        return result;
                }

                bool isNumeric() const {
                        return !empty() && std::all_of(d.begin(), d.end(), ::isdigit);
                }

                String &arg(const String &str);

                String &arg(int8_t value, 
                            int base = 10, 
                            int padding = 0, 
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                String &arg(uint8_t value, 
                            int base = 10, 
                            int padding = 0, 
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                String &arg(int16_t value, 
                            int base = 10, 
                            int padding = 0, 
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                String &arg(uint16_t value, 
                            int base = 10, 
                            int padding = 0, 
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }


                String &arg(int32_t value, 
                            int base = 10, 
                            int padding = 0, 
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                String &arg(uint32_t value, 
                            int base = 10, 
                            int padding = 0, 
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                String &arg(int64_t value, 
                            int base = 10, 
                            int padding = 0, 
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                String &arg(uint64_t value, 
                            int base = 10, 
                            int padding = 0, 
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                int toInt(Error *err = nullptr) const;
                unsigned int toUInt(Error *err = nullptr) const;

                int64_t parseNumberWords(bool *success = nullptr) const;

        private:
                // Composition of std::string because apparently inheriting std::string
                // to add functionality is a bad idea due to the lack of virtual destructor
                // and object slicing.
                std::string d;
};

}

