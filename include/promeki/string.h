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

/**
 * @brief A versatile string class inspired by Qt's QString.
 *
 * Internally wraps std::string but provides a less verbose interface with
 * additional convenience methods. Implicit conversion operators allow passing
 * a String directly to functions expecting std::string or const char*.
 * For std::string member functions not directly exposed, use stds() to obtain
 * a reference to the underlying std::string.
 */
class String {
        PROMEKI_SHARED_FINAL(String)
        public:
                /** @brief Shared pointer type for String. */
                using Ptr = SharedPtr<String>;

                /** @brief Mutable forward iterator. */
                using Iterator = std::string::iterator;

                /** @brief Const forward iterator. */
                using ConstIterator = std::string::const_iterator;

                /** @brief Mutable reverse iterator. */
                using RevIterator = std::string::reverse_iterator;

                /** @brief Const reverse iterator. */
                using ConstRevIterator = std::string::const_reverse_iterator;

                /** @brief Sentinel value indicating "not found", equivalent to std::string::npos. */
                static constexpr size_t npos = std::string::npos;

                /** @brief Characters considered whitespace by trim(). */
                static constexpr const char *WhitespaceChars = " \t\n\r\f\v";

                /**
                 * @brief Converts an integer to its string representation.
                 * @param value     The value to convert.
                 * @param base      Numeric base (e.g. 10 for decimal, 16 for hex).
                 * @param padding   Minimum width of the output, padded if needed.
                 * @param padchar   Character used for padding.
                 * @param addPrefix If true, adds a base prefix (e.g. "0x" for hex).
                 * @return The string representation of the value.
                 */
                static String number(int8_t value,
                                int base = 10,
                                int padding = 0,
                                char padchar = ' ',
                                bool addPrefix = false);

                /// @copydoc number(int8_t, int, int, char, bool)
                static String number(uint8_t value,
                                int base = 10,
                                int padding = 0,
                                char padchar = ' ',
                                bool addPrefix = false);

                /// @copydoc number(int8_t, int, int, char, bool)
                static String number(int16_t value,
                                int base = 10,
                                int padding = 0,
                                char padchar = ' ',
                                bool addPrefix = false);

                /// @copydoc number(int8_t, int, int, char, bool)
                static String number(uint16_t value,
                                int base = 10,
                                int padding = 0,
                                char padchar = ' ',
                                bool addPrefix = false);

                /// @copydoc number(int8_t, int, int, char, bool)
                static String number(int32_t value,
                                int base = 10,
                                int padding = 0,
                                char padchar = ' ',
                                bool addPrefix = false);

                /// @copydoc number(int8_t, int, int, char, bool)
                static String number(uint32_t value,
                                int base = 10,
                                int padding = 0,
                                char padchar = ' ',
                                bool addPrefix = false);

                /// @copydoc number(int8_t, int, int, char, bool)
                static String number(int64_t value,
                                int base = 10,
                                int padding = 0,
                                char padchar = ' ',
                                bool addPrefix = false);

                /// @copydoc number(int8_t, int, int, char, bool)
                static String number(uint64_t value,
                                int base = 10,
                                int padding = 0,
                                char padchar = ' ',
                                bool addPrefix = false);

                /**
                 * @brief Converts a boolean to "true" or "false".
                 * @param value The boolean value.
                 * @return "true" or "false".
                 */
                static String number(bool value) {
                        return value ? "true" : "false";
                }

                /**
                 * @brief Converts a float to its string representation.
                 * @param value     The value to convert.
                 * @param precision Number of significant digits.
                 * @return The string representation.
                 */
                static String number(float value, int precision = 9);

                /**
                 * @brief Converts a double to its string representation.
                 * @param value     The value to convert.
                 * @param precision Number of significant digits.
                 * @return The string representation.
                 */
                static String number(double value, int precision = 9);

                /**
                 * @brief Converts a value to a padded decimal string.
                 * @tparam T       The type of value to convert.
                 * @param val      The value to convert.
                 * @param padding  Minimum field width.
                 * @param padchar  Character used for padding.
                 * @return The formatted decimal string.
                 */
                template <typename T>
                static String dec(const T &val, int padding = 0, char padchar = ' ') {
                        std::ostringstream oss;
                        oss << std::setw(padding) << std::setfill(padchar) << val;
                        return oss.str();
                }

                /**
                 * @brief Converts a value to a hexadecimal string.
                 * @tparam T        The type of value to convert.
                 * @param val       The value to convert.
                 * @param padding   Minimum field width, zero-padded.
                 * @param addPrefix If true, prepends "0x".
                 * @return The formatted hexadecimal string.
                 */
                template <typename T>
                static String hex(const T &val, int padding = 0, bool addPrefix = true) {
                        std::ostringstream oss;
                        if(addPrefix) oss << "0x";
                        oss << std::setw(padding) << std::setfill('0') << std::hex << val;
                        return oss.str();
                }

                /**
                 * @brief Converts a value to a binary string.
                 * @tparam T        The type of value to convert.
                 * @param val       The value to convert.
                 * @param digits    Number of binary digits to output.
                 * @param addPrefix If true, prepends "0b".
                 * @return The formatted binary string.
                 */
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

                /**
                 * @brief Creates a formatted string using printf-style format specifiers.
                 * @param fmt The printf-style format string.
                 * @param ... Variable arguments matching the format specifiers.
                 * @return The formatted string.
                 */
                PROMEKI_PRINTF_FUNC(1, 2) static String sprintf(const char *fmt, ...);

                /** @brief Constructs an empty string. */
                String() = default;

                /**
                 * @brief Constructs a string from a C string.
                 * @param str The C string, or nullptr for an empty string.
                 */
                String(const char *str) : _s(str == nullptr ? std::string() : std::string(str)) { }

                /**
                 * @brief Constructs a string from a C string with explicit length.
                 * @param str The C string.
                 * @param len Number of characters to copy.
                 */
                String(const char *str, size_t len) : _s(str, len) { }

                /**
                 * @brief Constructs a string by repeating a character.
                 * @param ct Number of repetitions.
                 * @param c  The character to repeat.
                 */
                String(size_t ct, char c) : _s(ct, c) { }

                /**
                 * @brief Constructs a string from a std::string.
                 * @param str The source string.
                 */
                String(const std::string &str) : _s(str) { }

                /**
                 * @brief Move-constructs a string from a std::string.
                 * @param str The source string (moved from).
                 */
                String(std::string &&str) : _s(std::move(str)) { }

                /**
                 * @brief Returns a mutable reference to the underlying std::string.
                 * @return Reference to the internal std::string.
                 */
                std::string &stds() {
                        return _s;
                }

                /**
                 * @brief Returns a const reference to the underlying std::string.
                 * @return Const reference to the internal std::string.
                 */
                const std::string &stds() const {
                        return _s;
                }

                /** @brief Returns a mutable iterator to the beginning. */
                Iterator begin() noexcept {
                        return _s.begin();
                }

                /** @brief Returns a const iterator to the beginning. */
                ConstIterator begin() const noexcept {
                        return _s.begin();
                }

                /** @brief Returns a const iterator to the beginning. */
                ConstIterator cbegin() const noexcept {
                        return _s.cbegin();
                }

                /** @brief Returns a mutable reverse iterator to the beginning. */
                RevIterator rbegin() noexcept {
                        return _s.rbegin();
                }

                /** @brief Returns a const reverse iterator to the beginning. */
                ConstRevIterator rbegin() const noexcept {
                        return _s.rbegin();
                }

                /** @brief Returns a mutable iterator to the end. */
                Iterator end() noexcept {
                        return _s.end();
                }

                /** @brief Returns a const iterator to the end. */
                ConstIterator end() const noexcept {
                        return _s.end();
                }

                /** @brief Returns a const iterator to the end. */
                ConstIterator cend() const noexcept {
                        return _s.end();
                }

                /** @brief Returns a mutable reverse iterator to the end. */
                RevIterator rend() noexcept {
                        return _s.rend();
                }

                /** @brief Returns a const reverse iterator to the end. */
                ConstRevIterator rend() const noexcept {
                        return _s.rend();
                }

                /**
                 * @brief Returns a pointer to the null-terminated C string.
                 * @return Pointer to the internal character data.
                 */
                const char *cstr() const {
                        return _s.c_str();
                }

                /**
                 * @brief Returns the number of characters in the string.
                 * @return The string size in bytes.
                 */
                size_t size() const {
                        return _s.size();
                }

                /** @brief Clears the string contents, making it empty. */
                void clear() {
                        _s.clear();
                        return;
                }

                /**
                 * @brief Resizes the string to the given length.
                 * @param val The new size. If larger, new characters are zero-initialized.
                 */
                void resize(size_t val) {
                        _s.resize(val);
                        return;
                }

                /**
                 * @brief Returns the length of the string.
                 * @return The string length in bytes (same as size()).
                 */
                size_t length() const {
                        return _s.length();
                }

                /**
                 * @brief Checks whether the string is empty.
                 * @return True if the string has no characters.
                 */
                bool isEmpty() const {
                        return _s.empty();
                }

                /**
                 * @brief Finds the first occurrence of a character.
                 * @param val The character to search for.
                 * @return The position of the character, or npos if not found.
                 */
                size_t find(char val) const {
                        return _s.find(val);
                }

                /**
                 * @brief Finds the first occurrence of a substring.
                 * @param val The substring to search for.
                 * @return The position of the substring, or npos if not found.
                 */
                size_t find(const String &val) const {
                        return _s.find(val._s);
                }

                /**
                 * @brief Checks if the string contains the given character.
                 * @param val The character to search for.
                 * @return True if the character is found.
                 */
                bool contains(char val) const {
                        return _s.find(val) != std::string::npos;
                }

                /**
                 * @brief Checks if the string contains the given substring.
                 * @param val The substring to search for.
                 * @return True if the substring is found.
                 */
                bool contains(const String &val) const {
                        return _s.find(val._s) != std::string::npos;
                }

                /**
                 * @brief Checks if the string contains the given C string.
                 * @param val The C string to search for.
                 * @return True if the substring is found.
                 */
                bool contains(const char *val) const {
                        return _s.find(val) != std::string::npos;
                }

                /**
                 * @brief Returns a substring.
                 * @param pos Starting position.
                 * @param len Maximum number of characters (default: to end of string).
                 * @return The substring.
                 */
                String substr(size_t pos = 0, size_t len = npos) const {
                        return _s.substr(pos, len);
                }

                /**
                 * @brief Returns a substring starting at a given position.
                 * @param pos   Starting position.
                 * @param count Maximum number of characters (default: to end of string).
                 * @return The substring.
                 */
                String mid(size_t pos, size_t count = npos) const {
                        return _s.substr(pos, count);
                }

                /**
                 * @brief Returns the leftmost characters of the string.
                 * @param count Number of characters to return.
                 * @return The leftmost @p count characters.
                 */
                String left(size_t count) const {
                        return _s.substr(0, count);
                }

                /**
                 * @brief Returns the rightmost characters of the string.
                 * @param count Number of characters to return.
                 * @return The rightmost @p count characters.
                 */
                String right(std::size_t count) const {
                        return _s.substr(_s.length() - count, count);
                }

                /** @brief Implicit conversion to a mutable std::string reference. */
                operator std::string&() {
                        return _s;
                }

                /** @brief Implicit conversion to a const std::string reference. */
                operator const std::string &() const {
                        return _s;
                }

                /** @brief Implicit conversion to a C string pointer. */
                operator const char *() const {
                        return _s.c_str();
                }

                /** @brief Assignment from a std::string. */
                String &operator=(const std::string &str) {
                        _s = str;
                        return *this;
                }

                /** @brief Move assignment from a std::string. */
                String &operator=(std::string &&str) {
                        _s = std::move(str);
                        return *this;
                }

                /** @brief Assignment from a C string. */
                String &operator=(const char *str) {
                        _s = str;
                        return *this;
                }

                /** @brief Concatenation with another String. */
                String operator+(const String &val) const {
                        return String(_s + val._s);
                }

                /** @brief Concatenation with a std::string. */
                String operator+(const std::string &val) const {
                        return String(_s + val);
                }

                /** @brief Concatenation with a C string. */
                String operator+(const char *val) const {
                        return String(_s + val);
                }

                /** @brief Concatenation with a single character. */
                String operator+(char val) const {
                        return String(_s + val);
                }

                /** @brief Appends another String. */
                String &operator+=(const String &val) {
                        _s += val._s;
                        return *this;
                }

                /** @brief Appends a std::string. */
                String &operator+=(const std::string &val) {
                        _s += val;
                        return *this;
                }

                /** @brief Appends a C string. */
                String &operator+=(const char *val) {
                        _s += val;
                        return *this;
                }

                /** @brief Appends a single character. */
                String &operator+=(char val) {
                        _s += val;
                        return *this;
                }

                /**
                 * @brief Accesses a character by index.
                 * @param index The zero-based character position.
                 * @return Mutable reference to the character.
                 */
                char &operator[](int index) {
                        return _s[index];
                }

                /**
                 * @brief Accesses a character by index (const).
                 * @param index The zero-based character position.
                 * @return Const reference to the character.
                 */
                const char &operator[](int index) const {
                        return _s[index];
                }

                /** @brief Equality comparison with another String. */
                bool operator==(const String &val) const {
                        return _s == val._s;
                }

                /** @brief Equality comparison with a C string. */
                bool operator==(const char *val) const {
                        return _s == val;
                }

                /** @brief Equality comparison with a single character. */
                bool operator==(char val) const {
                        return _s.size() == 1 && _s[0] == val;
                }

                /** @brief Inequality comparison with another String. */
                bool operator!=(const String &val) const {
                        return _s != val._s;
                }

                /** @brief Inequality comparison with a C string. */
                bool operator!=(const char *val) const {
                        return _s != val;
                }

                /** @brief Inequality comparison with a single character. */
                bool operator!=(char val) const {
                        return _s.size() != 1 || _s[0] != val;
                }

                /** @brief Lexicographic less-than comparison. */
                friend bool operator<(const String &lhs, const String &rhs) {
                        return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
                }

                /** @brief Stream insertion operator. */
                friend std::ostream &operator<<(std::ostream &os, const String &val) {
                        os << val._s;
                        return os;
                }

                /** @brief Stream extraction operator. */
                friend std::istream &operator>>(std::istream &in, String &val) {
                        in >> val._s;
                        return in;
                }

                /**
                 * @brief Returns an uppercase copy of the string.
                 * @return A new String with all characters converted to uppercase.
                 */
                String toUpper() const {
                        std::string result(_s);
                        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
                        return result;
                }

                /**
                 * @brief Returns a lowercase copy of the string.
                 * @return A new String with all characters converted to lowercase.
                 */
                String toLower() const {
                        std::string result(_s);
                        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
                        return result;
                }

                /**
                 * @brief Returns a copy with leading and trailing whitespace removed.
                 * @return The trimmed string, or an empty string if entirely whitespace.
                 */
                String trim() const {
                        std::string result;
                        size_t first = _s.find_first_not_of(WhitespaceChars);
                        if(first != std::string::npos) {
                                size_t last = _s.find_last_not_of(WhitespaceChars);
                                result = _s.substr(first, last - first + 1);
                        }
                        return result;
                }

                /**
                 * @brief Splits the string by a delimiter.
                 * @param delimiter The delimiter string to split on.
                 * @return A StringList containing the split parts.
                 */
                StringList split(const std::string& delimiter) const;

                /**
                 * @brief Checks if the string starts with the given prefix.
                 * @param prefix The prefix to check.
                 * @return True if the string begins with @p prefix.
                 */
                bool startsWith(const String &prefix) const {
                        return _s.compare(0, prefix.size(), prefix._s) == 0;
                }

                /**
                 * @brief Checks if the string starts with the given character.
                 * @param c The character to check.
                 * @return True if the first character matches @p c.
                 */
                bool startsWith(char c) const {
                        return !isEmpty() && _s[0] == c;
                }

                /**
                 * @brief Checks if the string ends with the given suffix.
                 * @param suffix The suffix to check.
                 * @return True if the string ends with @p suffix.
                 */
                bool endsWith(const String &suffix) const {
                        if(suffix.size() > size()) return false;
                        return std::equal(suffix._s.rbegin(), suffix._s.rend(), _s.rbegin());
                }

                /**
                 * @brief Counts non-overlapping occurrences of a substring.
                 * @param substr The substring to search for.
                 * @return The number of non-overlapping occurrences.
                 */
                size_t count(const String &substr) const {
                        size_t count = 0;
                        size_t pos = 0;
                        while((pos = _s.find(substr._s, pos)) != std::string::npos) {
                                ++count;
                                pos += substr.length();
                        }
                        return count;
                }

                /**
                 * @brief Returns a reversed copy of the string.
                 * @return A new String with characters in reverse order.
                 */
                String reverse() const {
                        std::string result(_s);
                        std::reverse(result.begin(), result.end());
                        return result;
                }

                /**
                 * @brief Checks if the string contains only digit characters.
                 * @return True if non-empty and all characters satisfy std::isdigit.
                 */
                bool isNumeric() const {
                        return !isEmpty() && std::all_of(_s.begin(), _s.end(), ::isdigit);
                }

                /**
                 * @brief Replaces the lowest-numbered placeholder with a string.
                 *
                 * Placeholders use the format `%1`, `%2`, etc. Each call replaces
                 * the lowest-numbered placeholder found in the string and returns
                 * a reference to this String, allowing chained calls.
                 *
                 * @param str The replacement string.
                 * @return Reference to this String.
                 */
                String &arg(const String &str);

                /**
                 * @brief Replaces the lowest-numbered placeholder with a formatted integer.
                 * @param value     The value to insert.
                 * @param base      Numeric base for conversion.
                 * @param padding   Minimum field width.
                 * @param padchar   Padding character.
                 * @param addPrefix If true, adds a base prefix.
                 * @return Reference to this String.
                 */
                String &arg(int8_t value,
                            int base = 10,
                            int padding = 0,
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @copydoc arg(int8_t, int, int, char, bool)
                String &arg(uint8_t value,
                            int base = 10,
                            int padding = 0,
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @copydoc arg(int8_t, int, int, char, bool)
                String &arg(int16_t value,
                            int base = 10,
                            int padding = 0,
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @copydoc arg(int8_t, int, int, char, bool)
                String &arg(uint16_t value,
                            int base = 10,
                            int padding = 0,
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @copydoc arg(int8_t, int, int, char, bool)
                String &arg(int32_t value,
                            int base = 10,
                            int padding = 0,
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @copydoc arg(int8_t, int, int, char, bool)
                String &arg(uint32_t value,
                            int base = 10,
                            int padding = 0,
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @copydoc arg(int8_t, int, int, char, bool)
                String &arg(int64_t value,
                            int base = 10,
                            int padding = 0,
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @copydoc arg(int8_t, int, int, char, bool)
                String &arg(uint64_t value,
                            int base = 10,
                            int padding = 0,
                            char padchar = ' ',
                            bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /**
                 * @brief Converts the string to a value of the given type.
                 * @tparam OutputType The target type (must support operator>>).
                 * @param err Optional error output. Set to Error::Invalid on failure.
                 * @return The converted value, or a default-constructed value on failure.
                 */
                template <typename OutputType> OutputType to(Error *err = nullptr) const {
                        OutputType ret;
                        std::istringstream iss(_s);
                        iss >> ret;
                        if(iss.fail() || !iss.eof()) {
                                if(err != nullptr) *err = Error::Invalid;
                                return OutputType{};
                        }
                        if(err != nullptr) *err = Error::Ok;
                        return ret;
                }

                /**
                 * @brief Converts the string to a boolean.
                 * @param err Optional error output.
                 * @return The boolean value.
                 */
                bool toBool(Error *err = nullptr) const;

                /**
                 * @brief Converts the string to an integer.
                 * @param err Optional error output.
                 * @return The integer value.
                 */
                int toInt(Error *err = nullptr) const;

                /**
                 * @brief Converts the string to an unsigned integer.
                 * @param err Optional error output.
                 * @return The unsigned integer value.
                 */
                unsigned int toUInt(Error *err = nullptr) const;

                /**
                 * @brief Converts the string to a double.
                 * @param err Optional error output.
                 * @return The double value.
                 */
                double toDouble(Error *err = nullptr) const;

                /**
                 * @brief Parses English number words into an integer value.
                 *
                 * Interprets strings like "twenty three" or "one hundred" as their
                 * numeric equivalents.
                 *
                 * @param err Optional error output.
                 * @return The parsed integer value.
                 */
                int64_t parseNumberWords(Error *err = nullptr) const;

        private:
                std::string _s;     ///< The underlying string storage.
};

/** @brief Concatenation with a C string on the left-hand side. */
inline String operator+(const char *lhs, const String &rhs) {
        return String(lhs) + rhs;
}

PROMEKI_NAMESPACE_END
