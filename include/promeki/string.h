/**
 * @file      string.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/util.h>
#include <promeki/error.h>
#include <promeki/sharedptr.h>
#include <promeki/char.h>
#include <promeki/stringdata.h>

PROMEKI_NAMESPACE_BEGIN

class StringList;

/**
 * @brief Encoding-aware string class with copy-on-write semantics.
 * @ingroup strings
 *
 * String is a lightweight handle wrapping a SharedPtr<StringData>.
 * Copy is a refcount increment; mutation triggers COW. Latin1 is
 * the fast path (byte == character). Unicode strings store decoded
 * codepoints for O(1) indexed access.
 *
 * @par Formatting
 * New code should prefer @ref String::format (a thin wrapper over
 * C++20 @c std::format) over the older @ref String::sprintf and
 * @ref String::arg helpers.  @c format gives compile-time format string
 * checking, type-safe argument handling, and an extensible mechanism
 * for adding format support to user-defined types via the standard
 * @c std::formatter customization point.  String and Char already have
 * formatter specializations, so they compose with @c std::format
 * directly:
 * @code
 *   String name = "Alice";
 *   String s = String::format("hello, {}!", name);
 *   String s2 = String::format("{:>8}", 42);
 * @endcode
 * @c sprintf and @c arg are not deprecated yet, but expect them to be
 * phased out as call sites are migrated.
 */
class String {
        public:
                /** @brief List of Strings. */
                using List = promeki::List<String>;

                /**
                 * @brief Random-access iterator over characters (Char values).
                 *
                 * Wraps a StringData pointer and an index. Both Latin1 and
                 * Unicode backends provide O(1) charAt(), so this is efficient.
                 */
                class CharIterator {
                        public:
                                using iterator_category = std::random_access_iterator_tag;
                                using value_type        = Char;
                                using difference_type   = std::ptrdiff_t;
                                using pointer           = void;
                                using reference         = Char;

                                CharIterator() : _data(nullptr), _idx(0) {}
                                CharIterator(const StringData *data, size_t idx) : _data(data), _idx(idx) {}

                                Char operator*() const { return _data->charAt(_idx); }
                                Char operator[](difference_type n) const { return _data->charAt(_idx + n); }

                                CharIterator &operator++() { ++_idx; return *this; }
                                CharIterator operator++(int) { auto tmp = *this; ++_idx; return tmp; }
                                CharIterator &operator--() { --_idx; return *this; }
                                CharIterator operator--(int) { auto tmp = *this; --_idx; return tmp; }
                                CharIterator &operator+=(difference_type n) { _idx += n; return *this; }
                                CharIterator &operator-=(difference_type n) { _idx -= n; return *this; }
                                CharIterator operator+(difference_type n) const { return {_data, _idx + n}; }
                                CharIterator operator-(difference_type n) const { return {_data, _idx - n}; }
                                difference_type operator-(const CharIterator &o) const {
                                        return static_cast<difference_type>(_idx) - static_cast<difference_type>(o._idx);
                                }
                                friend CharIterator operator+(difference_type n, const CharIterator &it) {
                                        return it + n;
                                }

                                bool operator==(const CharIterator &o) const { return _idx == o._idx; }
                                bool operator!=(const CharIterator &o) const { return _idx != o._idx; }
                                bool operator<(const CharIterator &o) const { return _idx < o._idx; }
                                bool operator<=(const CharIterator &o) const { return _idx <= o._idx; }
                                bool operator>(const CharIterator &o) const { return _idx > o._idx; }
                                bool operator>=(const CharIterator &o) const { return _idx >= o._idx; }

                        private:
                                const StringData *_data;
                                size_t _idx;
                };

                /** @brief Const character iterator. */
                using ConstIterator = CharIterator;

                /** @brief Sentinel value indicating "not found". */
                static constexpr size_t npos = StringData::npos;

                /** @brief Characters considered whitespace by trim(). */
                static constexpr const char *WhitespaceChars = " \t\n\r\f\v";

                /** @brief String encoding. */
                enum Encoding {
                        Latin1,  ///< One byte per character, ISO-8859-1.
                        Unicode  ///< Decoded codepoints, O(1) indexed access.
                };

                // ============================================================
                // Static factory methods (implemented in string.cpp)
                // ============================================================

                /**
                 * @brief Converts a numeric value to its String representation.
                 * @param value     The value to convert.
                 * @param base      Numeric base (2-36, default 10).
                 * @param padding   Minimum width; padded with @p padchar.
                 * @param padchar   Padding character (default: space).
                 * @param addPrefix If true, adds a base prefix (e.g. "0x" for hex).
                 * @return The formatted number string.
                 */
                static String number(int8_t value, int base = 10, int padding = 0,
                                char padchar = ' ', bool addPrefix = false);
                /// @copydoc number(int8_t,int,int,char,bool)
                static String number(uint8_t value, int base = 10, int padding = 0,
                                char padchar = ' ', bool addPrefix = false);
                /// @copydoc number(int8_t,int,int,char,bool)
                static String number(int16_t value, int base = 10, int padding = 0,
                                char padchar = ' ', bool addPrefix = false);
                /// @copydoc number(int8_t,int,int,char,bool)
                static String number(uint16_t value, int base = 10, int padding = 0,
                                char padchar = ' ', bool addPrefix = false);
                /// @copydoc number(int8_t,int,int,char,bool)
                static String number(int32_t value, int base = 10, int padding = 0,
                                char padchar = ' ', bool addPrefix = false);
                /// @copydoc number(int8_t,int,int,char,bool)
                static String number(uint32_t value, int base = 10, int padding = 0,
                                char padchar = ' ', bool addPrefix = false);
                /// @copydoc number(int8_t,int,int,char,bool)
                static String number(int64_t value, int base = 10, int padding = 0,
                                char padchar = ' ', bool addPrefix = false);
                /// @copydoc number(int8_t,int,int,char,bool)
                static String number(uint64_t value, int base = 10, int padding = 0,
                                char padchar = ' ', bool addPrefix = false);

                /**
                 * @brief Returns "true" or "false" for a boolean value.
                 * @param value The boolean to convert.
                 * @return "true" or "false".
                 */
                static String number(bool value) {
                        return value ? "true" : "false";
                }

                /**
                 * @brief Converts a float to its string representation.
                 * @param value     The float to convert.
                 * @param precision Number of decimal digits (default 9).
                 * @return The formatted number string.
                 */
                static String number(float value, int precision = 9);
                /// @copydoc number(float,int)
                static String number(double value, int precision = 9);

                /**
                 * @brief Formats a value as a decimal string with optional padding.
                 * @tparam T       Integer or arithmetic type.
                 * @param val      The value to format.
                 * @param padding  Minimum field width.
                 * @param padchar  Padding character (default: space).
                 * @return The formatted string.
                 */
                template <typename T>
                static String dec(const T &val, int padding = 0, char padchar = ' ') {
                        return number(static_cast<int64_t>(val), 10, padding, padchar);
                }

                /**
                 * @brief Formats a value as a hexadecimal string.
                 * @tparam T        Integer type.
                 * @param val       The value to format.
                 * @param padding   Minimum digit width (zero-padded).
                 * @param addPrefix If true, prepends "0x".
                 * @return The formatted hex string.
                 */
                template <typename T>
                static String hex(const T &val, int padding = 0, bool addPrefix = true) {
                        int totalWidth = (addPrefix && padding > 0) ? padding + 2 : padding;
                        return number(static_cast<uint64_t>(val), 16, totalWidth, '0', addPrefix);
                }

                /**
                 * @brief Formats a value as a binary string.
                 * @tparam T        Value type (must support bitwise operations).
                 * @param val       The value to format.
                 * @param digits    Number of binary digits to emit (default 32).
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
                 * @brief Creates a formatted string using printf-style syntax.
                 * @param fmt The printf format string.
                 * @return The formatted string.
                 *
                 * @note New code should prefer @ref format, which gives
                 *       compile-time format string checking and type-safe
                 *       argument handling via C++20 @c std::format.
                 */
                PROMEKI_PRINTF_FUNC(1, 2) static String sprintf(const char *fmt, ...);

                /**
                 * @brief Creates a formatted string using C++20 @c std::format syntax.
                 *
                 * Thin wrapper around @c std::format that returns a promeki
                 * String.  The format string is checked at compile time
                 * (malformed specs fail to compile), and arguments are
                 * type-safe via the standard @c std::formatter customization
                 * point.  @ref String and @ref Char already have formatter
                 * specializations, so they may be used as arguments directly.
                 *
                 * @code
                 *   String name = "Alice";
                 *   String s  = String::format("hello, {}!", name);
                 *   String s2 = String::format("{:>8}", 42);          // right-justified, width 8
                 *   String s3 = String::format("{:.3f}", 3.14159);    // "3.142"
                 * @endcode
                 *
                 * The result is wrapped via @ref fromUtf8, so a pure-ASCII
                 * format result lands in cheap Latin1 storage and a result
                 * with multi-byte UTF-8 sequences lands in Unicode storage.
                 *
                 * @tparam Args Argument types deduced from the call.
                 * @param fmt   Compile-time @c std::format format string.
                 * @param args  Arguments to substitute into @p fmt.
                 * @return A new String containing the formatted text.
                 *
                 * @sa vformat for runtime format strings.
                 */
                template <typename... Args>
                static String format(std::format_string<Args...> fmt, Args &&... args) {
                        std::string s = std::format(fmt, std::forward<Args>(args)...);
                        return fromUtf8(s.data(), s.size());
                }

                /**
                 * @brief Runtime-format-string variant of @ref format.
                 *
                 * Use when the format string is not known at compile time
                 * (e.g. loaded from a translation table or user input).
                 * Compile-time format string checking is not available in
                 * this case — a malformed format string throws
                 * @c std::format_error at runtime.
                 *
                 * @code
                 *   std::string fmt = loadFormat();
                 *   String s = String::vformat(fmt, std::make_format_args(value));
                 * @endcode
                 *
                 * @param fmt  Runtime format string.
                 * @param args Pre-erased argument pack from @c std::make_format_args.
                 * @return A new String containing the formatted text.
                 */
                static String vformat(std::string_view fmt, std::format_args args) {
                        std::string s = std::vformat(fmt, args);
                        return fromUtf8(s.data(), s.size());
                }

                /**
                 * @brief Creates a String by decoding UTF-8 data.
                 * @param data Pointer to UTF-8 bytes.
                 * @param len  Number of bytes.
                 * @return The most compact valid representation: a Latin1-encoded
                 *         String when @p data is pure ASCII (every byte < 0x80,
                 *         which is also valid UTF-8 with one codepoint per byte),
                 *         otherwise a Unicode-encoded String with codepoints
                 *         decoded from the UTF-8 sequence.
                 */
                static String fromUtf8(const char *data, size_t len) {
                        // Pure ASCII fast path: every byte is its own codepoint and
                        // fits trivially in the cheaper Latin1 storage.
                        for(size_t i = 0; i < len; ++i) {
                                if(static_cast<unsigned char>(data[i]) > 0x7F)
                                        return String(StringUnicodeData::fromUtf8(data, len));
                        }
                        return String(data, len);
                }

                /**
                 * @brief Wraps a static literal StringData without copying.
                 *
                 * Used by PROMEKI_STRING. The literal data has an immortal
                 * refcount so it is never freed. Works with both
                 * StringLiteralData (Latin1) and StringUnicodeLiteralData.
                 */
                static String fromLiteralData(StringData *data) {
                        return String(data);
                }

                // ============================================================
                // Constructors
                // ============================================================

                /** @brief Default constructor. Creates an empty Latin1 string. */
                String()
                        : d(SharedPtr<StringData>::takeOwnership(new StringLatin1Data())) {}

                /** @brief Constructs an empty string (null pointer overload). */
                String(std::nullptr_t) : String() {}

                /**
                 * @brief Constructs from a null-terminated C string.
                 * @param str The C string (null treated as empty).
                 */
                String(const char *str)
                        : d(SharedPtr<StringData>::takeOwnership(new StringLatin1Data(str ? str : ""))) {}

                /**
                 * @brief Constructs from a character buffer with explicit length.
                 * @param str Pointer to character data.
                 * @param len Number of bytes.
                 */
                String(const char *str, size_t len)
                        : d(SharedPtr<StringData>::takeOwnership(new StringLatin1Data(str, len))) {}

                /**
                 * @brief Constructs a string of repeated characters.
                 * @param ct Number of characters.
                 * @param c  Character to repeat.
                 */
                String(size_t ct, char c)
                        : d(SharedPtr<StringData>::takeOwnership(new StringLatin1Data(ct, c))) {}

                /**
                 * @brief Constructs from a std::string (copy).
                 * @param str The source string.
                 */
                String(const std::string &str)
                        : d(SharedPtr<StringData>::takeOwnership(new StringLatin1Data(str))) {}

                /**
                 * @brief Constructs from a std::string (move).
                 * @param str The source string to move from.
                 */
                String(std::string &&str)
                        : d(SharedPtr<StringData>::takeOwnership(new StringLatin1Data(std::move(str)))) {}

                // ============================================================
                // Const access (pure delegation)
                // ============================================================

                /** @brief Returns a const reference to the underlying std::string. */
                const std::string &str() const { return d->str(); }

                /** @brief Returns a null-terminated C string pointer. */
                const char *cstr() const { return d->cstr(); }

                /** @brief Returns the number of characters in the string. */
                size_t size() const { return d->length(); }

                /** @brief Returns the number of characters in the string. */
                size_t length() const { return d->length(); }

                /** @brief Returns the number of bytes in the encoded representation. */
                size_t byteCount() const { return d->byteCount(); }

                /**
                 * @brief Returns the byte at the given index.
                 * @param idx Zero-based byte index.
                 * @return The byte value.
                 */
                uint8_t byteAt(size_t idx) const { return d->byteAt(idx); }

                /**
                 * @brief Returns the character at the given index.
                 * @param idx Zero-based character index.
                 * @return The character.
                 */
                Char charAt(size_t idx) const { return d->charAt(idx); }

                /** @brief Returns true if the string has zero length. */
                bool isEmpty() const { return d->isEmpty(); }

                /** @brief Returns the current shared reference count. */
                int referenceCount() const { return d.referenceCount(); }

                /** @brief Returns true if this string wraps an immutable literal. */
                bool isLiteral() const { return d->isLiteral(); }

                /** @brief Returns the encoding of this string (Latin1 or Unicode). */
                Encoding encoding() const {
                        return d->isLatin1() ? Latin1 : Unicode;
                }

                // ============================================================
                // Character iterators
                // ============================================================

                /** @brief Returns a const character iterator to the first character. */
                ConstIterator begin() const noexcept { return {d.ptr(), 0}; }
                /// @copydoc begin()
                ConstIterator cbegin() const noexcept { return {d.ptr(), 0}; }
                /** @brief Returns a const character iterator past the last character. */
                ConstIterator end() const noexcept { return {d.ptr(), d->length()}; }
                /// @copydoc end()
                ConstIterator cend() const noexcept { return {d.ptr(), d->length()}; }

                // ============================================================
                // Implicit conversions
                // ============================================================

                /** @brief Implicit conversion to const std::string reference. */
                operator const std::string &() const { return d->str(); }

                /** @brief Implicit conversion to const char pointer. */
                operator const char *() const { return d->cstr(); }

                // ============================================================
                // Find / contains
                // ============================================================

                /**
                 * @brief Finds the first occurrence of a character.
                 * @param val  The character to find.
                 * @param from Starting character index (default 0).
                 * @return Character index of the match, or npos if not found.
                 */
                size_t find(char val, size_t from = 0) const { return d->find(Char(val), from); }
                /// @copydoc find(char,size_t) const
                size_t find(Char val, size_t from = 0) const { return d->find(val, from); }
                /**
                 * @brief Finds the first occurrence of a UTF-8 encoded substring.
                 *
                 * The C-string argument is decoded as UTF-8 (matching
                 * @ref operator==(const char *) const and @ref String::fromUtf8),
                 * not as a Latin1 byte sequence.
                 */
                size_t find(const char *val, size_t from = 0) const {
                        if(val == nullptr) return npos;
                        size_t len = 0;
                        while(val[len]) ++len;
                        return d->find(*String::fromUtf8(val, len).d, from);
                }
                /// @copydoc find(char,size_t) const
                size_t find(const String &val, size_t from = 0) const { return d->find(*val.d, from); }

                /**
                 * @brief Finds the last occurrence of a character.
                 * @param val  The character to find.
                 * @param from Maximum character index to consider (npos = end).
                 * @return Character index of the match, or npos if not found.
                 */
                size_t rfind(char val, size_t from = npos) const { return d->rfind(Char(val), from); }
                /// @copydoc rfind(char,size_t) const
                size_t rfind(Char val, size_t from = npos) const { return d->rfind(val, from); }
                /**
                 * @brief Finds the last occurrence of a UTF-8 encoded substring.
                 *
                 * The C-string argument is decoded as UTF-8.
                 */
                size_t rfind(const char *val, size_t from = npos) const {
                        if(val == nullptr) return npos;
                        size_t len = 0;
                        while(val[len]) ++len;
                        return d->rfind(*String::fromUtf8(val, len).d, from);
                }
                /// @copydoc rfind(char,size_t) const
                size_t rfind(const String &val, size_t from = npos) const { return d->rfind(*val.d, from); }

                /**
                 * @brief Returns true if the string contains the given value.
                 * @param val The value to search for.
                 * @return True if found.
                 */
                bool contains(char val) const { return d->find(Char(val)) != npos; }
                /// @copydoc contains(char) const
                bool contains(Char val) const { return d->find(val) != npos; }
                /// @copydoc contains(char) const
                bool contains(const String &val) const { return d->find(*val.d) != npos; }
                /**
                 * @brief Returns true if the string contains a UTF-8 encoded substring.
                 *
                 * The C-string argument is decoded as UTF-8.
                 */
                bool contains(const char *val) const { return find(val) != npos; }

                /**
                 * @brief Counts non-overlapping occurrences of a substring.
                 * @param substr The substring to count.
                 * @return Number of occurrences.
                 */
                size_t count(const String &substr) const { return d->count(*substr.d); }

                // ============================================================
                // Substring
                // ============================================================

                /**
                 * @brief Returns a substring.
                 * @param pos Starting character index (default 0).
                 * @param len Maximum number of characters (npos = rest of string).
                 * @return The substring.
                 */
                String substr(size_t pos = 0, size_t len = npos) const {
                        if(pos >= d->length()) return String();
                        if(len == npos) len = d->length() - pos;
                        return String(d->createSubstr(pos, len));
                }

                /**
                 * @brief Returns a substring starting at @p pos (alias for substr).
                 * @param pos   Starting character index.
                 * @param count Maximum number of characters.
                 * @return The substring.
                 */
                String mid(size_t pos, size_t count = npos) const { return substr(pos, count); }

                /**
                 * @brief Returns the first @p count characters.
                 * @param count Number of characters to return.
                 * @return The left substring.
                 */
                String left(size_t count) const { return substr(0, count); }

                /**
                 * @brief Returns the last @p count characters.
                 * @param count Number of characters to return.
                 * @return The right substring.
                 */
                String right(std::size_t count) const {
                        if(count >= length()) return *this;
                        return substr(length() - count, count);
                }

                // ============================================================
                // Mutation (COW)
                // ============================================================

                /** @brief Removes all characters from the string. */
                void clear() { d.modify()->clear(); }

                /**
                 * @brief Resizes the string to the given character count.
                 * @param val New character count.
                 */
                void resize(size_t val) { d.modify()->resize(val); }

                /**
                 * @brief Erases characters from the string.
                 * @param pos   Starting character index.
                 * @param count Number of characters to erase (default 1).
                 */
                void erase(size_t pos, size_t count = 1) { d.modify()->erase(pos, count); }

                /**
                 * @brief Sets the character at the given index (promotes to Unicode if needed).
                 * @param idx Zero-based character index.
                 * @param ch  The replacement character.
                 */
                void setCharAt(size_t idx, Char ch) {
                        if(d->isLatin1() && ch.codepoint() > 0xFF) {
                                auto *ud = StringUnicodeData::fromLatin1(d->str());
                                d = SharedPtr<StringData>::takeOwnership(ud);
                        }
                        d.modify()->setCharAt(idx, ch);
                }

                /**
                 * @brief Inserts a string at the given position (promotes to Unicode if needed).
                 * @param pos Zero-based character index.
                 * @param s   The string to insert.
                 */
                void insert(size_t pos, const String &s) {
                        if(d->isLatin1() && !s.d->isLatin1()) {
                                auto *ud = StringUnicodeData::fromLatin1(d->str());
                                d = SharedPtr<StringData>::takeOwnership(ud);
                        }
                        d.modify()->insert(pos, *s.d);
                }

                // ============================================================
                // Assignment
                // ============================================================

                /** @brief Assigns from a std::string (copy). */
                String &operator=(const std::string &str) {
                        d = SharedPtr<StringData>::takeOwnership(new StringLatin1Data(str));
                        return *this;
                }

                /** @brief Assigns from a std::string (move). */
                String &operator=(std::string &&str) {
                        d = SharedPtr<StringData>::takeOwnership(new StringLatin1Data(std::move(str)));
                        return *this;
                }

                /** @brief Assigns from a C string. */
                String &operator=(const char *str) {
                        d = SharedPtr<StringData>::takeOwnership(new StringLatin1Data(str ? str : ""));
                        return *this;
                }

                // ============================================================
                // Concatenation
                // ============================================================

                /**
                 * @brief Appends a String to this string (promotes to Unicode if needed).
                 * @param val The string to append.
                 * @return Reference to this string.
                 */
                String &operator+=(const String &val) {
                        if(val.isEmpty()) return *this;
                        if(d->isLatin1() && !val.d->isLatin1()) {
                                auto *ud = StringUnicodeData::fromLatin1(d->str());
                                d = SharedPtr<StringData>::takeOwnership(ud);
                        }
                        d.modify()->append(*val.d);
                        return *this;
                }

                /// @copydoc operator+=(const String &)
                String &operator+=(const std::string &val) { return *this += String(val); }
                /// @copydoc operator+=(const String &)
                String &operator+=(const char *val) { return *this += String(val); }
                /// @copydoc operator+=(const String &)
                String &operator+=(char val) { return *this += String(1, val); }

                /**
                 * @brief Returns the concatenation of this string and @p val.
                 * @param val The string to concatenate.
                 * @return A new String containing both parts.
                 */
                String operator+(const String &val) const {
                        String result = *this;
                        result += val;
                        return result;
                }

                /// @copydoc operator+(const String &) const
                String operator+(const std::string &val) const { return *this + String(val); }
                /// @copydoc operator+(const String &) const
                String operator+(const char *val) const { return *this + String(val); }
                /// @copydoc operator+(const String &) const
                String operator+(char val) const { return *this + String(1, val); }

                // ============================================================
                // Comparison
                // ============================================================

                /**
                 * @brief Equality comparison with another String.
                 * @param val The string to compare with.
                 * @return True if both strings have identical characters.
                 */
                bool operator==(const String &val) const {
                        if(d->length() != val.d->length()) return false;
                        if(d->isLatin1() && val.d->isLatin1()) return d->str() == val.d->str();
                        for(size_t i = 0; i < d->length(); ++i) {
                                if(d->charAt(i) != val.d->charAt(i)) return false;
                        }
                        return true;
                }

                /**
                 * @brief Equality comparison with a UTF-8 C string.
                 *
                 * The argument is interpreted as UTF-8 (matching the
                 * convention used by @ref String::fromUtf8 and the @c _ps
                 * literal): each codepoint is decoded from @p val and
                 * compared against the corresponding character of this
                 * string.  Pure-ASCII inputs work the same as a byte-level
                 * compare; multi-byte UTF-8 sequences match the Unicode
                 * codepoints they encode.
                 */
                bool operator==(const char *val) const {
                        if(val == nullptr) return d->length() == 0;
                        const size_t len = d->length();
                        size_t i = 0;
                        while(*val) {
                                if(i >= len) return false;
                                size_t bytesRead = 0;
                                Char vc = Char::fromUtf8(val, &bytesRead);
                                if(d->charAt(i) != vc) return false;
                                val += bytesRead;
                                ++i;
                        }
                        return i == len;
                }
                /// @copydoc operator==(const String &) const
                bool operator==(char val) const { return d->length() == 1 && d->charAt(0) == val; }

                /** @brief Inequality comparison. */
                bool operator!=(const String &val) const { return !(*this == val); }
                /// @copydoc operator!=(const String &) const
                bool operator!=(const char *val) const { return !(*this == val); }
                /// @copydoc operator!=(const String &) const
                bool operator!=(char val) const { return !(*this == val); }

                /**
                 * @brief Less-than comparison (lexicographic on Unicode codepoints).
                 *
                 * The result is consistent with @ref operator==(const String &) const:
                 * two strings are equal iff neither is less than the other.  Same
                 * encoding on both sides takes a fast byte-level path (Latin1 byte
                 * order matches codepoint order, and UTF-8 is designed so that
                 * byte-level lexicographic order equals codepoint order); mixed
                 * encodings fall back to a codepoint-by-codepoint walk.
                 */
                friend bool operator<(const String &lhs, const String &rhs) {
                        if(lhs.d->isLatin1() == rhs.d->isLatin1()) {
                                // Same encoding: byte-level comparison is correct.
                                // - Latin1 vs Latin1: byte order == codepoint order.
                                // - Unicode vs Unicode: UTF-8 byte order == codepoint order.
                                return lhs.d->str() < rhs.d->str();
                        }
                        // Mixed encodings: walk codepoint-by-codepoint so that
                        // logically equal strings (e.g. Latin1 0xE9 and Unicode
                        // U+00E9) compare as equal under operator<.
                        const size_t llen = lhs.d->length();
                        const size_t rlen = rhs.d->length();
                        const size_t n = std::min(llen, rlen);
                        for(size_t i = 0; i < n; ++i) {
                                char32_t a = lhs.d->charAt(i).codepoint();
                                char32_t b = rhs.d->charAt(i).codepoint();
                                if(a != b) return a < b;
                        }
                        return llen < rlen;
                }

                /** @brief Less-than-or-equal comparison. */
                friend bool operator<=(const String &lhs, const String &rhs) {
                        return !(rhs < lhs);
                }

                /** @brief Greater-than comparison. */
                friend bool operator>(const String &lhs, const String &rhs) {
                        return rhs < lhs;
                }

                /** @brief Greater-than-or-equal comparison. */
                friend bool operator>=(const String &lhs, const String &rhs) {
                        return !(lhs < rhs);
                }

                // ============================================================
                // Case / whitespace
                // ============================================================

                /**
                 * @brief Returns an uppercase copy of this string.
                 *
                 * Case folding goes through @ref Char::toUpper for both the
                 * Latin1 and Unicode storage paths so the result is
                 * locale-independent and consistent across encodings.
                 */
                String toUpper() const {
                        if(d->isLatin1()) {
                                const std::string &src = d->str();
                                std::string s;
                                s.resize(src.size());
                                for(size_t i = 0; i < src.size(); ++i) {
                                        char32_t cp = Char(src[i]).toUpper().codepoint();
                                        // Latin1 toUpper stays within the
                                        // 0x00–0xFF range, so this is lossless.
                                        s[i] = static_cast<char>(cp);
                                }
                                return String(std::move(s));
                        }
                        promeki::List<Char> chars;
                        chars.reserve(d->length());
                        for(size_t i = 0; i < d->length(); ++i)
                                chars.pushToBack(d->charAt(i).toUpper());
                        return String(new StringUnicodeData(std::move(chars)));
                }

                /**
                 * @brief Returns a lowercase copy of this string.
                 *
                 * Case folding goes through @ref Char::toLower for both the
                 * Latin1 and Unicode storage paths so the result is
                 * locale-independent and consistent across encodings.
                 */
                String toLower() const {
                        if(d->isLatin1()) {
                                const std::string &src = d->str();
                                std::string s;
                                s.resize(src.size());
                                for(size_t i = 0; i < src.size(); ++i) {
                                        char32_t cp = Char(src[i]).toLower().codepoint();
                                        s[i] = static_cast<char>(cp);
                                }
                                return String(std::move(s));
                        }
                        promeki::List<Char> chars;
                        chars.reserve(d->length());
                        for(size_t i = 0; i < d->length(); ++i)
                                chars.pushToBack(d->charAt(i).toLower());
                        return String(new StringUnicodeData(std::move(chars)));
                }

                /** @brief Returns a copy with leading and trailing whitespace removed. */
                String trim() const {
                        size_t len = length();
                        if(len == 0) return String();
                        size_t first = 0;
                        while(first < len && d->charAt(first).isSpace()) ++first;
                        if(first == len) return String();
                        size_t last = len - 1;
                        while(last > first && d->charAt(last).isSpace()) --last;
                        return substr(first, last - first + 1);
                }

                // ============================================================
                // Starts / ends / reverse / numeric
                // ============================================================

                /**
                 * @brief Returns true if the string starts with the given prefix.
                 * @param prefix The prefix to test.
                 * @return True if this string begins with @p prefix.
                 */
                bool startsWith(const String &prefix) const {
                        if(prefix.length() > length()) return false;
                        for(size_t i = 0; i < prefix.length(); ++i) {
                                if(d->charAt(i) != prefix.d->charAt(i)) return false;
                        }
                        return true;
                }

                /**
                 * @brief Returns true if the string starts with the given character.
                 * @param c The character to test.
                 */
                bool startsWith(char c) const {
                        return !isEmpty() && d->charAt(0) == c;
                }

                /**
                 * @brief Returns true if the string ends with the given suffix.
                 * @param suffix The suffix to test.
                 * @return True if this string ends with @p suffix.
                 */
                bool endsWith(const String &suffix) const {
                        if(suffix.length() > length()) return false;
                        size_t offset = length() - suffix.length();
                        for(size_t i = 0; i < suffix.length(); ++i) {
                                if(d->charAt(offset + i) != suffix.d->charAt(i)) return false;
                        }
                        return true;
                }

                /** @brief Returns a copy with characters in reverse order. */
                String reverse() const {
                        String result = *this;
                        result.d.modify()->reverseInPlace();
                        return result;
                }

                /** @brief Returns true if every character is a decimal digit. */
                bool isNumeric() const {
                        if(isEmpty()) return false;
                        for(size_t i = 0; i < d->length(); ++i) {
                                if(!d->charAt(i).isDigit()) return false;
                        }
                        return true;
                }

                /**
                 * @brief Returns a copy with all occurrences of @p find replaced
                 *        by @p replacement.
                 */
                String replace(const String &find, const String &replacement) const;

                /**
                 * @brief Case-insensitive comparison.
                 * @return Negative if *this < other, 0 if equal, positive if *this > other.
                 */
                int compareIgnoreCase(const String &other) const {
                        size_t len = std::min(length(), other.length());
                        for(size_t i = 0; i < len; ++i) {
                                char32_t a = d->charAt(i).toLower().codepoint();
                                char32_t b = other.d->charAt(i).toLower().codepoint();
                                if(a != b) return a < b ? -1 : 1;
                        }
                        if(length() < other.length()) return -1;
                        if(length() > other.length()) return 1;
                        return 0;
                }

                /**
                 * @brief Returns a 64-bit FNV-1a hash of this string's native data.
                 *
                 * Each StringData backend hashes its native storage directly,
                 * avoiding unnecessary encoding conversions.
                 */
                uint64_t hash() const {
                        return d->hash();
                }

                // ============================================================
                // Encoding conversion
                // ============================================================

                /**
                 * @brief Returns a Latin1 version of this string.
                 *
                 * If already Latin1, returns a shallow copy (refcount increment).
                 * Otherwise converts, clamping codepoints above 0xFF to '?'.
                 */
                String toLatin1() const {
                        if(d->isLatin1()) return *this;
                        std::string s;
                        s.reserve(d->length());
                        for(size_t i = 0; i < d->length(); ++i) {
                                char32_t cp = d->charAt(i).codepoint();
                                s += static_cast<char>(cp <= 0xFF ? cp : '?');
                        }
                        return String(std::move(s));
                }

                /**
                 * @brief Returns a Unicode version of this string.
                 *
                 * If already Unicode, returns a shallow copy (refcount increment).
                 * Otherwise promotes Latin1 data to decoded codepoints.
                 */
                String toUnicode() const {
                        if(!d->isLatin1()) return *this;
                        return String(StringUnicodeData::fromLatin1(d->str()));
                }

                // ============================================================
                // Arg replacement (implemented in string.cpp)
                // ============================================================

                /**
                 * @brief Replaces the lowest-numbered %N placeholder with the given string.
                 * @param str The replacement text.
                 * @return Reference to this string.
                 */
                String &arg(const String &str);

                /// @brief Replaces the lowest `{N}` token with the formatted numeric value.
                String &arg(int8_t value, int base = 10, int padding = 0,
                            char padchar = ' ', bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @brief Replaces the lowest `{N}` token with the formatted numeric value.
                String &arg(uint8_t value, int base = 10, int padding = 0,
                            char padchar = ' ', bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @brief Replaces the lowest `{N}` token with the formatted numeric value.
                String &arg(int16_t value, int base = 10, int padding = 0,
                            char padchar = ' ', bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @brief Replaces the lowest `{N}` token with the formatted numeric value.
                String &arg(uint16_t value, int base = 10, int padding = 0,
                            char padchar = ' ', bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @brief Replaces the lowest `{N}` token with the formatted numeric value.
                String &arg(int32_t value, int base = 10, int padding = 0,
                            char padchar = ' ', bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @brief Replaces the lowest `{N}` token with the formatted numeric value.
                String &arg(uint32_t value, int base = 10, int padding = 0,
                            char padchar = ' ', bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @brief Replaces the lowest `{N}` token with the formatted numeric value.
                String &arg(int64_t value, int base = 10, int padding = 0,
                            char padchar = ' ', bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                /// @brief Replaces the lowest `{N}` token with the formatted numeric value.
                String &arg(uint64_t value, int base = 10, int padding = 0,
                            char padchar = ' ', bool addPrefix = false) {
                        return arg(number(value, base, padding, padchar, addPrefix));
                }

                // ============================================================
                // Conversion (implemented in string.cpp)
                // ============================================================

                /**
                 * @brief Converts the string to a value of type @p OutputType.
                 * @tparam OutputType The target type (int, long, double, float, etc).
                 * @param err Optional error output.
                 * @return The converted value, or a default-constructed value on failure.
                 */
                template <typename OutputType> OutputType to(Error *err = nullptr) const {
                        const char *s = cstr();
                        char *end = nullptr;
                        OutputType ret{};
                        if constexpr (std::is_integral_v<OutputType> && std::is_signed_v<OutputType>) {
                                long long v = std::strtoll(s, &end, 10);
                                ret = static_cast<OutputType>(v);
                        } else if constexpr (std::is_integral_v<OutputType> && std::is_unsigned_v<OutputType>) {
                                unsigned long long v = std::strtoull(s, &end, 10);
                                ret = static_cast<OutputType>(v);
                        } else if constexpr (std::is_floating_point_v<OutputType>) {
                                double v = std::strtod(s, &end);
                                ret = static_cast<OutputType>(v);
                        } else {
                                if(err != nullptr) *err = Error::Invalid;
                                return ret;
                        }
                        if(end == s || *end != '\0') {
                                if(err != nullptr) *err = Error::Invalid;
                                return OutputType{};
                        }
                        if(err != nullptr) *err = Error::Ok;
                        return ret;
                }

                /**
                 * @brief Converts the string to a boolean.
                 * @param err Optional error output.
                 * @return The boolean value (recognizes "true"/"false" and "1"/"0").
                 */
                bool toBool(Error *err = nullptr) const;

                /**
                 * @brief Converts the string to an integer.
                 * @param err Optional error output.
                 * @return The integer value, or 0 on failure.
                 */
                int toInt(Error *err = nullptr) const;

                /**
                 * @brief Converts the string to an unsigned integer.
                 * @param err Optional error output.
                 * @return The unsigned integer value, or 0 on failure.
                 */
                unsigned int toUInt(Error *err = nullptr) const;

                /**
                 * @brief Converts the string to a double.
                 * @param err Optional error output.
                 * @return The double value, or 0.0 on failure.
                 */
                double toDouble(Error *err = nullptr) const;

                /**
                 * @brief Parses English number words into an integer value.
                 * @param err Optional error output.
                 * @return The parsed value (e.g. "twenty three" returns 23).
                 */
                int64_t parseNumberWords(Error *err = nullptr) const;

                /**
                 * @brief Splits the string by a delimiter.
                 * @param delimiter The delimiter string to split on.
                 * @return A StringList containing the split parts.
                 */
                StringList split(const std::string& delimiter) const;

        private:
                SharedPtr<StringData> d;

                explicit String(StringData *data)
                        : d(SharedPtr<StringData>::takeOwnership(data)) {}
};

/** @brief Concatenation with a C string on the left-hand side. */
inline String operator+(const char *lhs, const String &rhs) {
        return String(lhs) + rhs;
}

PROMEKI_NAMESPACE_END

/// @cond INTERNAL
template<>
struct std::hash<promeki::String> {
        size_t operator()(const promeki::String &s) const noexcept {
                return static_cast<size_t>(s.hash());
        }
};
/// @endcond

/**
 * @brief @c std::formatter specialization for @ref promeki::String.
 *
 * Inherits from @c std::formatter<std::string_view> so that the full set
 * of standard string format specifiers (width, fill, alignment, precision)
 * is available for free.  The String's byte representation is exposed as
 * a @c string_view via @c cstr() / @c byteCount() — for Unicode-encoded
 * Strings this is the cached UTF-8 byte sequence, which is the right
 * thing to feed into @c std::format.
 */
template<>
struct std::formatter<promeki::String> : std::formatter<std::string_view> {
        using Base = std::formatter<std::string_view>;
        template <typename FormatContext>
        auto format(const promeki::String &s, FormatContext &ctx) const {
                return Base::format(std::string_view(s.cstr(), s.byteCount()), ctx);
        }
};

/**
 * @brief @c std::formatter specialization for @ref promeki::Char.
 *
 * Renders the character as its UTF-8 byte sequence (1–4 bytes).  All
 * standard string format specifiers (width, fill, alignment) are
 * supported via inheritance from @c std::formatter<std::string_view>.
 * To format the integer codepoint instead, pass @c c.codepoint() as
 * an unsigned integer.
 */
template<>
struct std::formatter<promeki::Char> : std::formatter<std::string_view> {
        using Base = std::formatter<std::string_view>;
        template <typename FormatContext>
        auto format(const promeki::Char &c, FormatContext &ctx) const {
                char buf[4];
                size_t n = c.toUtf8(buf);
                return Base::format(std::string_view(buf, n), ctx);
        }
};

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief @c std::formatter helper that renders any type with a no-arg
 *        @c toString() returning a @ref String.
 *
 * Inherit a @c std::formatter specialization from this template to give
 * a library type out-of-the-box support as a @ref String::format
 * argument.  All standard string format specifiers (width, fill,
 * alignment, precision) are inherited from
 * @c std::formatter<std::string_view>, so users can write
 * @code
 *   String::format("[{:>16}]", uuid);          // right-justified, width 16
 *   String::format("ratio = {}", aspectRatio);
 * @endcode
 *
 * Use the @ref PROMEKI_FORMAT_VIA_TOSTRING convenience macro to
 * register a type in one line:
 * @code
 *   PROMEKI_FORMAT_VIA_TOSTRING(promeki::UUID);
 * @endcode
 *
 * Types with parameterized @c toString() (e.g. @ref Color or @ref Json)
 * or with non-@c String return types (e.g. @ref Timecode, which returns
 * @c Result<String>) need a hand-written
 * @c std::formatter specialization that parses the format spec
 * directly — see the @ref Timecode formatter in @c timecode.h for an
 * example.
 *
 * @tparam T The promeki type to render via its @c toString() method.
 */
template <typename T>
struct ToStringFormatter : std::formatter<std::string_view> {
        using Base = std::formatter<std::string_view>;
        template <typename FormatContext>
        auto format(const T &v, FormatContext &ctx) const {
                String s = v.toString();
                return Base::format(std::string_view(s.cstr(), s.byteCount()), ctx);
        }
};

PROMEKI_NAMESPACE_END

/**
 * @def PROMEKI_FORMAT_VIA_TOSTRING(...)
 * @brief Registers a @c std::formatter specialization that delegates to
 *        the type's no-arg @c toString() method.
 *
 * Place this macro at namespace scope, after the type definition and
 * with @c <promeki/string.h> already included.  Standard string format
 * specifiers (width, fill, alignment) work automatically via inheritance
 * from @ref promeki::ToStringFormatter.
 *
 * @code
 *   PROMEKI_FORMAT_VIA_TOSTRING(promeki::UUID);
 *   PROMEKI_FORMAT_VIA_TOSTRING(promeki::Rational);
 * @endcode
 *
 * Variadic so qualified template names containing commas can be passed.
 */
#define PROMEKI_FORMAT_VIA_TOSTRING(...)                                        \
        template <>                                                             \
        struct std::formatter<__VA_ARGS__>                                      \
            : ::promeki::ToStringFormatter<__VA_ARGS__> {}

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Compile-time string literal with encoding detection and UTF-8 decode.
 *
 * The consteval constructor copies the literal bytes and, if any byte is
 * above 0x7F, decodes the entire string as UTF-8 into a char32_t array.
 * All work happens at compile time — no runtime encoding scan or UTF-8
 * parsing.  Used by the PROMEKI_STRING macro.
 *
 * @tparam N Size of the string literal including the null terminator.
 */
template<size_t N>
class CompiledString {
        public:
                consteval CompiledString(const char (&str)[N])
                        : _bytes{}, _codepoints{}, _charCount(0), _isAscii(true) {
                        for(size_t i = 0; i < N; ++i) _bytes[i] = str[i];
                        size_t pos = 0;
                        while(pos < N - 1) {
                                unsigned char b = static_cast<unsigned char>(str[pos]);
                                if(b > 0x7F) _isAscii = false;
                                char32_t cp;
                                size_t seqLen;
                                if(b < 0x80)      { cp = b; seqLen = 1; }
                                else if(b < 0xE0) { cp = b & 0x1F; seqLen = 2; }
                                else if(b < 0xF0) { cp = b & 0x0F; seqLen = 3; }
                                else              { cp = b & 0x07; seqLen = 4; }
                                for(size_t j = 1; j < seqLen && pos + j < N - 1; ++j)
                                        cp = (cp << 6) | (static_cast<unsigned char>(str[pos + j]) & 0x3F);
                                _codepoints[_charCount++] = cp;
                                pos += seqLen;
                        }
                }

                constexpr bool isAscii() const { return _isAscii; }
                constexpr size_t charCount() const { return _charCount; }
                constexpr size_t byteCount() const { return N - 1; }
                constexpr const char *bytes() const { return _bytes; }
                constexpr const char32_t *codepoints() const { return _codepoints; }

                /**
                 * @brief Compile-time encoding-agnostic FNV-1a hash.
                 *
                 * Mixes each codepoint into the hash as four little-endian
                 * bytes so the result is independent of host endianness and
                 * matches the runtime hash for both Latin1 and Unicode
                 * String storage with the same logical content.
                 */
                constexpr uint64_t hash() const {
                        if(_isAscii) return fnv1aLatin1AsCodepoints(_bytes, N - 1);
                        return fnv1aCodepoints(_codepoints, _charCount);
                }

        private:
                char     _bytes[N];
                char32_t _codepoints[N];  // worst case: N-1 codepoints
                size_t   _charCount;
                bool     _isAscii;
};

/**
 * @brief Right-sized codepoint array extracted from a CompiledString.
 *
 * CompiledString<N> allocates N codepoints (worst case), but a UTF-8
 * string with N bytes has at most N-1 codepoints and usually far fewer.
 * This class copies only the actual codepoints into a correctly-sized
 * constexpr array, so the oversized CompiledString can be optimized away.
 *
 * @tparam Count The exact number of decoded codepoints.
 */
template<size_t Count>
class CompiledCodepoints {
        public:
                template<size_t N>
                consteval CompiledCodepoints(const CompiledString<N> &cs) : _data{} {
                        for(size_t i = 0; i < Count; ++i) _data[i] = cs.codepoints()[i];
                }
                constexpr const char32_t *data() const { return _data; }
                constexpr size_t size() const { return Count; }
        private:
                char32_t _data[Count];
};

/**
 * @brief User-defined literal for convenient String construction.
 *
 * Treats the literal as UTF-8.  Pure-ASCII inputs land in cheap Latin1
 * storage and inputs containing multi-byte sequences are decoded into
 * Unicode storage — the choice is made by @ref String::fromUtf8.
 *
 * @code
 *   using namespace promeki::literals;
 *   String s = "Hello"_ps;          // Latin1 storage (ASCII fast path)
 *   String u = "café"_ps;           // Unicode storage (UTF-8 decoded)
 * @endcode
 */
namespace literals {
        inline String operator""_ps(const char *str, size_t len) {
                return String::fromUtf8(str, len);
        }
} // namespace literals

PROMEKI_NAMESPACE_END

// NOLINTNEXTLINE(bugprone-macro-parentheses)

/**
 * @def PROMEKI_STRING(str)
 * @brief Compile-time optimized String from any string literal.
 *
 * Encoding detection and UTF-8 decoding happen entirely at compile
 * time via consteval.  At runtime both paths are zero-copy:
 * - ASCII/Latin1: StringLiteralData wrapping the literal bytes
 * - UTF-8: StringUnicodeLiteralData wrapping pre-decoded codepoints
 *   and the original UTF-8 bytes
 *
 * No heap allocation, no memcpy, no runtime decode for either path.
 *
 * @code
 *   String a = PROMEKI_STRING("Hello");     // Latin1 zero-copy
 *   String b = PROMEKI_STRING("café");      // compile-time UTF-8 decode
 * @endcode
 */
#define PROMEKI_STRING(str)                                                     \
    ([]() -> ::promeki::String {                                                \
        constexpr auto _cs =                                                    \
            ::promeki::CompiledString<sizeof(str)>(str);                        \
        if constexpr (_cs.isAscii()) {                                          \
            static ::promeki::StringLiteralData _lit(                           \
                str, _cs.byteCount(), _cs.hash());                              \
            return ::promeki::String::fromLiteralData(&_lit);                   \
        } else {                                                                \
            static constexpr auto _cp =                                         \
                ::promeki::CompiledCodepoints<_cs.charCount()>(_cs);            \
            static ::promeki::StringUnicodeLiteralData _lit(                    \
                _cp.data(), _cp.size(),                                          \
                str, _cs.byteCount(), _cs.hash());                              \
            return ::promeki::String::fromLiteralData(&_lit);                   \
        }                                                                       \
    }())
