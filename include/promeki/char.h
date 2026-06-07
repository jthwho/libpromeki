/**
 * @file      char.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Unicode-aware character class wrapping a single codepoint.
 * @ingroup strings
 *
 * Stores a char32_t codepoint and provides character classification,
 * case conversion, and UTF-8 encoding/decoding.
 *
 * @par Thread Safety
 * Trivially thread-safe.  Char is a value-type wrapper around a
 * single @c char32_t; distinct instances may be used concurrently
 * and a single instance carries no mutable shared state.
 */
class Char {
        public:
                /** @brief Constructs a null character (U+0000). */
                Char() : _cp(0) {}

                /** @brief Constructs from a Latin1 byte (0x00–0xFF). */
                Char(char c) : _cp(static_cast<unsigned char>(c)) {}

                /** @brief Constructs from a Unicode codepoint. */
                explicit Char(char32_t cp) : _cp(cp) {}

                /** @brief Returns the Unicode codepoint. */
                char32_t codepoint() const { return _cp; }

                /** @brief Returns true if this is the null character. */
                bool isNull() const { return _cp == 0; }

                /** @brief Returns true if the codepoint is in the ASCII range (< 0x80). */
                bool isAscii() const { return _cp < 0x80; }

                /** @brief Returns true if the character is alphabetic. */
                bool isAlpha() const;

                /** @brief Returns true if the character is a decimal digit. */
                bool isDigit() const;

                /** @brief Returns true if the character is alphanumeric. */
                bool isAlphaNumeric() const;

                /** @brief Returns true if the character is whitespace. */
                bool isSpace() const;

                /** @brief Returns true if the character is uppercase. */
                bool isUpper() const;

                /** @brief Returns true if the character is lowercase. */
                bool isLower() const;

                /** @brief Returns true if the character is printable. */
                bool isPrintable() const;

                /** @brief Returns true if the character is a hexadecimal digit (0-9, a-f, A-F). */
                bool isHexDigit() const {
                        return (_cp >= '0' && _cp <= '9') || (_cp >= 'a' && _cp <= 'f') || (_cp >= 'A' && _cp <= 'F');
                }

                /**
                 * @brief Returns the numeric value of this hex digit.
                 * @return 0-15 for valid hex digits, or -1 if not a hex digit.
                 */
                int hexDigitValue() const {
                        if (_cp >= '0' && _cp <= '9') return _cp - '0';
                        if (_cp >= 'a' && _cp <= 'f') return 10 + _cp - 'a';
                        if (_cp >= 'A' && _cp <= 'F') return 10 + _cp - 'A';
                        return -1;
                }

                /** @brief Returns the uppercase version of this character. */
                Char toUpper() const;

                /** @brief Returns the lowercase version of this character. */
                Char toLower() const;

                /** @brief Returns the number of bytes needed to encode this codepoint as UTF-8 (1–4). */
                size_t utf8ByteCount() const;

                /**
                 * @brief Encodes this codepoint as UTF-8 into the given buffer.
                 * @param buf Buffer of at least 4 bytes.
                 * @return Number of bytes written.
                 */
                size_t toUtf8(char *buf) const;

                /**
                 * @brief Decodes a single UTF-8 codepoint from the given buffer.
                 *
                 * @warning @p buf must be NUL-terminated.  The decode stops at
                 * the terminator, so a truncated trailing multibyte sequence
                 * cannot read past it; prefer the bounded overload for
                 * arbitrary (non-NUL-terminated) byte ranges.
                 *
                 * @param buf       Pointer to NUL-terminated UTF-8 bytes.
                 * @param bytesRead If non-null, set to the number of bytes consumed.
                 * @return The decoded character.
                 */
                static Char fromUtf8(const char *buf, size_t *bytesRead = nullptr);

                /**
                 * @brief Bounds-checked UTF-8 decode of a single codepoint.
                 *
                 * Never reads more than @p avail bytes from @p buf.  A
                 * multibyte sequence that would run past @p avail (truncated
                 * input, or binary data that merely looks like a lead byte) is
                 * treated as invalid: the result is U+FFFD and @p bytesRead is
                 * 1 so the caller resynchronizes one byte at a time.  Use this
                 * for arbitrary byte ranges that are not NUL-terminated.
                 *
                 * @param buf       Pointer to UTF-8 bytes.
                 * @param avail     Number of valid bytes readable from @p buf.
                 * @param bytesRead If non-null, set to the number of bytes consumed.
                 * @return The decoded character, or U+FFFD on truncated/invalid input.
                 */
                static Char fromUtf8(const char *buf, size_t avail, size_t *bytesRead = nullptr);

                bool operator==(Char o) const { return _cp == o._cp; }
                bool operator==(char c) const { return _cp == static_cast<unsigned char>(c); }
                bool operator!=(Char o) const { return _cp != o._cp; }
                bool operator<(Char o) const { return _cp < o._cp; }
                bool operator<=(Char o) const { return _cp <= o._cp; }
                bool operator>(Char o) const { return _cp > o._cp; }
                bool operator>=(Char o) const { return _cp >= o._cp; }

        private:
                char32_t _cp;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
