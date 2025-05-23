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
#include <promeki/namespace.h>
#include <promeki/util.h>
#include <Poco/Dynamic/Var.h>

PROMEKI_NAMESPACE_BEGIN

class Error;
class StringList;

// A more versitle string object, inspired by the Qt QString object.  Internally it uses
// std::string, but removes some of its verbosity. For the most part, you should be able
// to use this string just as you would std::string.  It implements cast overloading for
// std::string, so you should be able to pass it directly to a function that uses a
// std::string.  If you want to use any of the std::string functions that aren't exported
// directly in the String interface, you can use the stds() function as it'll give you 
// a reference or const reference to the underlying std::string object.
class String {
        public:
                using Iterator = std::string::iterator;
                using ConstIterator = std::string::const_iterator;
                using RevIterator = std::string::reverse_iterator;
                using ConstRevIterator = std::string::const_reverse_iterator;

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

                static String number(bool value) {
                        return value ? "true" : "false";
                }

                static String number(float value, int precision = 9);
                static String number(double value, int precision = 9);

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
                String(const char *str) : d(str == nullptr ? std::string() : str) { }
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

                Iterator begin() noexcept {
                        return d.begin();
                }

                ConstIterator begin() const noexcept {
                        return d.begin();
                }

                ConstIterator cbegin() const noexcept {
                        return d.cbegin();
                }

                RevIterator rbegin() noexcept {
                        return d.rbegin();
                }

                ConstRevIterator rbegin() const noexcept {
                        return d.rbegin();
                }
 
                Iterator end() noexcept {
                        return d.end();
                }

                ConstIterator end() const noexcept {
                        return d.end();
                }

                ConstIterator cend() const noexcept {
                        return d.end();
                }

                RevIterator rend() noexcept {
                        return d.rend();
                }

                ConstRevIterator rend() const noexcept {
                        return d.rend();
                }

                const char *cstr() const {
                        return d.c_str();
                }

                size_t size() const {
                        return d.size();
                }

                void clear() {
                        d.clear();
                        return;
                }

                void resize(size_t val) {
                        d.resize(val);
                        return;
                }

                size_t length() const {
                        return d.length();
                }

                bool isEmpty() const {
                        return d.empty();
                }

                size_t find(char val) const {
                        return d.find(val);
                }

                String substr(size_t pos = 0, size_t len = npos) const {
                        return d.substr(pos, len);
                }

                String mid(size_t pos, size_t count = npos) const {
                        return d.substr(pos, count);
                }

                String left(size_t count) const {
                        return d.substr(0, count);
                }

                String right(std::size_t count) const {
                        return d.substr(d.length() - count, count);
                }

                operator Poco::Dynamic::Var() const { return d; }

                operator std::string&() {
                        return d;
                }

                operator const std::string &() const {
                        return d;
                }

                operator const char *() const {
                        return d.c_str();
                }

                String &operator=(const std::string &str) {
                        d = str;
                        return *this;
                }

                String &operator=(const std::string &&str) {
                        d = str;
                        return *this;
                }

                String &operator=(const char *str) {
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

                friend bool operator<(const String &lhs, const String &rhs) {
                        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
                }

                friend std::ostream &operator<<(std::ostream &os, const String &val) {
                        os << val.d;
                        return os;
                }

                friend std::istream &operator>>(std::istream &in, String &val) {
                        in >> val.d;
                        return in;
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

                StringList split(const std::string& delimiter) const;

                bool startsWith(const String &prefix) const {
                        return d.compare(0, prefix.size(), prefix.d) == 0;
                }

                bool startsWith(char c) const {
                        return !isEmpty() && d[0] == c;
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
                        return !isEmpty() && std::all_of(d.begin(), d.end(), ::isdigit);
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

                template <typename OutputType> OutputType to(bool *ok = nullptr) const {
                        OutputType ret;
                        std::istringstream iss(d);
                        iss >> ret;
                        if(iss.fail() || !iss.eof()) {
                                if(ok != nullptr) *ok = false;
                                return OutputType{};
                        }
                        if(ok != nullptr) *ok = true;
                        return ret;
                }

                bool toBool(Error *err = nullptr) const;
                int toInt(Error *err = nullptr) const;
                unsigned int toUInt(Error *err = nullptr) const;
                double toDouble(Error *err = nullptr) const;

                int64_t parseNumberWords(bool *success = nullptr) const;

        private:
                // Composition of std::string because apparently inheriting std::string
                // to add functionality is a bad idea due to the lack of virtual destructor
                // and object slicing.
                std::string d;
};

PROMEKI_NAMESPACE_END

