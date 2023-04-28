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

namespace promeki {

class String : public std::string {
        public:
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
                        String ret;
                        if(addPrefix) ret += "0b";
                        T mask = static_cast<T>(1) << (digits - 1);
                        for(int i = 0; i < digits; i++) {
                                ret.push_back(val & mask ? '1' : '0');
                                mask >>= 1;
                        }
                        return ret;
                }

                String() {

                }

                String(const char *str) : std::string(str) {

                }

                String(const std::string &str) : std::string(str) {

                }

                String(const std::string &&str) : std::string(std::move(str)) {

                }

                String(const String &str) : std::string(str) {

                }

                String(const String &&str) : std::string(std::move(str)) {

                }
                
                String &operator=(const std::string &str) {
                        *this = str;
                        return *this;
                }

                String &operator=(const std::string &&str) {
                        if(&str == static_cast<std::string *>(this)) return *this;
                        std::string::operator=(std::move(str));
                        return *this;
                }

                String &operator=(const String &str) {
                        *this = str;
                        return *this;
                }

                String &operator=(const String &&str) {
                        if(&str == this) return *this;
                        std::string::operator=(std::move(str));
                        return *this;
                }

                String toUpper() const {
                        String result(*this);
                        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
                        return result;
                }

                String toLower() const {
                        String result(*this);
                        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
                        return result;
                }

                String trim() const {
                        String result;
                        size_t first = find_first_not_of(WhitespaceChars);
                        if(first != std::string::npos) {
                                size_t last = find_last_not_of(WhitespaceChars);
                                result = substr(first, last - first + 1);
                        }
                        return result;
                }

                std::vector<String> split(const std::string& delimiter) const {
                        std::vector<String> result;
                        size_t pos = 0;
                        std::string str = *this;
                        while ((pos = str.find(delimiter)) != std::string::npos) {
                                String token = str.substr(0, pos);
                                if (!token.empty()) {
                                        result.push_back(token);
                                }
                                str.erase(0, pos + delimiter.length());
                        }
                        if (!str.empty()) {
                                result.push_back(str);
                        }
                        return result;
                }

                bool startsWith(const std::string& prefix) const {
                        return compare(0, prefix.size(), prefix) == 0;
                }

                bool endsWith(const std::string& suffix) const {
                        if (suffix.size() > size()) {
                                return false;
                        }
                        return std::equal(suffix.rbegin(), suffix.rend(), rbegin());
                }

                size_t count(const std::string& substr) const {
                        size_t count = 0;
                        size_t pos = 0;
                        while ((pos = find(substr, pos)) != std::string::npos) {
                                ++count;
                                pos += substr.length();
                        }
                        return count;
                }

                String reverse() const {
                        String result(*this);
                        std::reverse(result.begin(), result.end());
                        return result;
                }

                bool isNumeric() const {
                        return !empty() && std::all_of(begin(), end(), ::isdigit);
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


};

}

