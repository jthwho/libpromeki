/**
 * @file      string.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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
#include <promeki/error.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

class StringList;

// A more versitle string object, inspired by the Qt QString object.  Internally it uses
// std::string, but removes some of its verbosity. For the most part, you should be able
// to use this string just as you would std::string.  It implements cast overloading for
// std::string, so you should be able to pass it directly to a function that uses a
// std::string.  If you want to use any of the std::string functions that aren't exported
// directly in the String interface, you can use the stds() function as it'll give you
// a reference or const reference to the underlying std::string object.
//
// String uses SharedPtr for copy-on-write semantics.  Copying a String is O(1) and only
// incurs an atomic refcount increment.  The underlying std::string is only copied when
// a mutating operation is performed on a shared instance.
class String {
        public:
                using Iterator = std::string::iterator;
                using ConstIterator = std::string::const_iterator;
                using RevIterator = std::string::reverse_iterator;
                using ConstRevIterator = std::string::const_reverse_iterator;

                static constexpr size_t npos = std::string::npos;

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

                String() : d(SharedPtr<Data>::create()) { }
                String(const char *str) : d(SharedPtr<Data>::create(str == nullptr ? std::string() : std::string(str))) { }
                String(const char *str, size_t len) : d(SharedPtr<Data>::create(std::string(str, len))) { }
                String(size_t ct, char c) : d(SharedPtr<Data>::create(std::string(ct, c))) { }
                String(const std::string &str) : d(SharedPtr<Data>::create(str)) { }
                String(std::string &&str) : d(SharedPtr<Data>::create(std::move(str))) { }

                std::string &stds() {
                        return d.modify()->s;
                }

                const std::string &stds() const {
                        return d->s;
                }

                Iterator begin() noexcept {
                        return d.modify()->s.begin();
                }

                ConstIterator begin() const noexcept {
                        return d->s.begin();
                }

                ConstIterator cbegin() const noexcept {
                        return d->s.cbegin();
                }

                RevIterator rbegin() noexcept {
                        return d.modify()->s.rbegin();
                }

                ConstRevIterator rbegin() const noexcept {
                        return d->s.rbegin();
                }

                Iterator end() noexcept {
                        return d.modify()->s.end();
                }

                ConstIterator end() const noexcept {
                        return d->s.end();
                }

                ConstIterator cend() const noexcept {
                        return d->s.end();
                }

                RevIterator rend() noexcept {
                        return d.modify()->s.rend();
                }

                ConstRevIterator rend() const noexcept {
                        return d->s.rend();
                }

                const char *cstr() const {
                        return d->s.c_str();
                }

                size_t size() const {
                        return d->s.size();
                }

                void clear() {
                        d.modify()->s.clear();
                        return;
                }

                void resize(size_t val) {
                        d.modify()->s.resize(val);
                        return;
                }

                size_t length() const {
                        return d->s.length();
                }

                bool isEmpty() const {
                        return d->s.empty();
                }

                size_t find(char val) const {
                        return d->s.find(val);
                }

                String substr(size_t pos = 0, size_t len = npos) const {
                        return d->s.substr(pos, len);
                }

                String mid(size_t pos, size_t count = npos) const {
                        return d->s.substr(pos, count);
                }

                String left(size_t count) const {
                        return d->s.substr(0, count);
                }

                String right(std::size_t count) const {
                        return d->s.substr(d->s.length() - count, count);
                }

                operator std::string&() {
                        return d.modify()->s;
                }

                operator const std::string &() const {
                        return d->s;
                }

                operator const char *() const {
                        return d->s.c_str();
                }

                String &operator=(const std::string &str) {
                        d.modify()->s = str;
                        return *this;
                }

                String &operator=(std::string &&str) {
                        d.modify()->s = std::move(str);
                        return *this;
                }

                String &operator=(const char *str) {
                        d.modify()->s = str;
                        return *this;
                }

                String operator+(const String &val) const {
                        return String(d->s + val.d->s);
                }

                String operator+(const std::string &val) const {
                        return String(d->s + val);
                }

                String operator+(const char *val) const {
                        return String(d->s + val);
                }

                String operator+(char val) const {
                        return String(d->s + val);
                }

                String &operator+=(const String &val) {
                        d.modify()->s += val.d->s;
                        return *this;
                }

                String &operator+=(const std::string &val) {
                        d.modify()->s += val;
                        return *this;
                }

                String &operator+=(const char *val) {
                        d.modify()->s += val;
                        return *this;
                }

                String &operator+=(char val) {
                        d.modify()->s += val;
                        return *this;
                }

                char &operator[](int index) {
                        return d.modify()->s[index];
                }

                const char &operator[](int index) const {
                        return d->s[index];
                }

                bool operator==(const String &val) const {
                        return d->s == val.d->s;
                }

                bool operator==(const char *val) const {
                        return d->s == val;
                }

                bool operator==(char val) const {
                        return d->s.size() == 1 && d->s[0] == val;
                }

                bool operator!=(const String &val) const {
                        return d->s != val.d->s;
                }

                bool operator!=(const char *val) const {
                        return d->s != val;
                }

                bool operator!=(char val) const {
                        return d->s.size() != 1 || d->s[0] != val;
                }

                friend bool operator<(const String &lhs, const String &rhs) {
                        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
                }

                friend std::ostream &operator<<(std::ostream &os, const String &val) {
                        os << val.d->s;
                        return os;
                }

                friend std::istream &operator>>(std::istream &in, String &val) {
                        in >> val.d.modify()->s;
                        return in;
                }

                String toUpper() const {
                        std::string result(d->s);
                        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
                        return result;
                }

                String toLower() const {
                        std::string result(d->s);
                        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
                        return result;
                }

                String trim() const {
                        std::string result;
                        size_t first = d->s.find_first_not_of(WhitespaceChars);
                        if(first != std::string::npos) {
                                size_t last = d->s.find_last_not_of(WhitespaceChars);
                                result = d->s.substr(first, last - first + 1);
                        }
                        return result;
                }

                StringList split(const std::string& delimiter) const;

                bool startsWith(const String &prefix) const {
                        return d->s.compare(0, prefix.size(), prefix.d->s) == 0;
                }

                bool startsWith(char c) const {
                        return !isEmpty() && d->s[0] == c;
                }

                bool endsWith(const String &suffix) const {
                        if(suffix.size() > size()) return false;
                        return std::equal(suffix.d->s.rbegin(), suffix.d->s.rend(), d->s.rbegin());
                }

                size_t count(const String &substr) const {
                        size_t count = 0;
                        size_t pos = 0;
                        while((pos = d->s.find(substr.d->s, pos)) != std::string::npos) {
                                ++count;
                                pos += substr.length();
                        }
                        return count;
                }

                String reverse() const {
                        std::string result(d->s);
                        std::reverse(result.begin(), result.end());
                        return result;
                }

                bool isNumeric() const {
                        return !isEmpty() && std::all_of(d->s.begin(), d->s.end(), ::isdigit);
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

                template <typename OutputType> OutputType to(Error *err = nullptr) const {
                        OutputType ret;
                        std::istringstream iss(d->s);
                        iss >> ret;
                        if(iss.fail() || !iss.eof()) {
                                if(err != nullptr) *err = Error::Invalid;
                                return OutputType{};
                        }
                        if(err != nullptr) *err = Error::Ok;
                        return ret;
                }

                bool toBool(Error *err = nullptr) const;
                int toInt(Error *err = nullptr) const;
                unsigned int toUInt(Error *err = nullptr) const;
                double toDouble(Error *err = nullptr) const;

                int64_t parseNumberWords(Error *err = nullptr) const;

                int referenceCount() const {
                        return d.referenceCount();
                }

        private:
                class Data {
                        PROMEKI_SHARED_FINAL(Data)
                        public:
                                std::string s;
                                Data() = default;
                                Data(const std::string &str) : s(str) {}
                                Data(std::string &&str) : s(std::move(str)) {}
                                Data(const Data &o) = default;
                };

                SharedPtr<Data> d;
};

PROMEKI_NAMESPACE_END
