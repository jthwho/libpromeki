/**
 * @file      char.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/char.h>

PROMEKI_NAMESPACE_BEGIN

bool Char::isAlpha() const {
        if(_cp < 0x80) {
                return (_cp >= 'A' && _cp <= 'Z') || (_cp >= 'a' && _cp <= 'z');
        }
        // Latin1 supplement letters (0xC0–0xFF excluding 0xD7 and 0xF7)
        if(_cp >= 0xC0 && _cp <= 0xFF && _cp != 0xD7 && _cp != 0xF7) return true;
        // Basic multilingual plane: rough heuristic for common letter ranges
        if(_cp >= 0x0100 && _cp <= 0x024F) return true;  // Latin Extended
        if(_cp >= 0x0370 && _cp <= 0x03FF) return true;  // Greek
        if(_cp >= 0x0400 && _cp <= 0x04FF) return true;  // Cyrillic
        if(_cp >= 0x3040 && _cp <= 0x30FF) return true;  // Hiragana + Katakana
        if(_cp >= 0x4E00 && _cp <= 0x9FFF) return true;  // CJK Unified Ideographs
        if(_cp >= 0xAC00 && _cp <= 0xD7A3) return true;  // Hangul Syllables
        return false;
}

bool Char::isDigit() const {
        return _cp >= '0' && _cp <= '9';
}

bool Char::isAlphaNumeric() const {
        return isAlpha() || isDigit();
}

bool Char::isSpace() const {
        return _cp == ' ' || _cp == '\t' || _cp == '\n' ||
               _cp == '\r' || _cp == '\f' || _cp == '\v' ||
               _cp == 0xA0 || _cp == 0x2000 || _cp == 0x3000;
}

bool Char::isUpper() const {
        if(_cp >= 'A' && _cp <= 'Z') return true;
        if(_cp >= 0xC0 && _cp <= 0xDE && _cp != 0xD7) return true;
        return false;
}

bool Char::isLower() const {
        if(_cp >= 'a' && _cp <= 'z') return true;
        if(_cp >= 0xDF && _cp <= 0xFF && _cp != 0xF7) return true;
        return false;
}

bool Char::isPrintable() const {
        if(_cp < 0x20) return false;
        if(_cp == 0x7F) return false;
        if(_cp >= 0x80 && _cp < 0xA0) return false;
        return true;
}

Char Char::toUpper() const {
        if(_cp >= 'a' && _cp <= 'z') return Char(static_cast<char32_t>(_cp - 32));
        if(_cp >= 0xE0 && _cp <= 0xFE && _cp != 0xF7) return Char(static_cast<char32_t>(_cp - 32));
        return *this;
}

Char Char::toLower() const {
        if(_cp >= 'A' && _cp <= 'Z') return Char(static_cast<char32_t>(_cp + 32));
        if(_cp >= 0xC0 && _cp <= 0xDE && _cp != 0xD7) return Char(static_cast<char32_t>(_cp + 32));
        return *this;
}

size_t Char::utf8ByteCount() const {
        if(_cp < 0x80) return 1;
        if(_cp < 0x800) return 2;
        if(_cp < 0x10000) return 3;
        return 4;
}

size_t Char::toUtf8(char *buf) const {
        if(_cp < 0x80) {
                buf[0] = static_cast<char>(_cp);
                return 1;
        }
        if(_cp < 0x800) {
                buf[0] = static_cast<char>(0xC0 | (_cp >> 6));
                buf[1] = static_cast<char>(0x80 | (_cp & 0x3F));
                return 2;
        }
        if(_cp < 0x10000) {
                buf[0] = static_cast<char>(0xE0 | (_cp >> 12));
                buf[1] = static_cast<char>(0x80 | ((_cp >> 6) & 0x3F));
                buf[2] = static_cast<char>(0x80 | (_cp & 0x3F));
                return 3;
        }
        buf[0] = static_cast<char>(0xF0 | (_cp >> 18));
        buf[1] = static_cast<char>(0x80 | ((_cp >> 12) & 0x3F));
        buf[2] = static_cast<char>(0x80 | ((_cp >> 6) & 0x3F));
        buf[3] = static_cast<char>(0x80 | (_cp & 0x3F));
        return 4;
}

Char Char::fromUtf8(const char *buf, size_t *bytesRead) {
        unsigned char b0 = static_cast<unsigned char>(buf[0]);
        if(b0 < 0x80) {
                if(bytesRead) *bytesRead = 1;
                return Char(static_cast<char32_t>(b0));
        }
        if((b0 & 0xE0) == 0xC0) {
                char32_t cp = (static_cast<char32_t>(b0 & 0x1F) << 6) |
                              (static_cast<char32_t>(buf[1] & 0x3F));
                if(bytesRead) *bytesRead = 2;
                return Char(cp);
        }
        if((b0 & 0xF0) == 0xE0) {
                char32_t cp = (static_cast<char32_t>(b0 & 0x0F) << 12) |
                              (static_cast<char32_t>(buf[1] & 0x3F) << 6) |
                              (static_cast<char32_t>(buf[2] & 0x3F));
                if(bytesRead) *bytesRead = 3;
                return Char(cp);
        }
        if((b0 & 0xF8) == 0xF0) {
                char32_t cp = (static_cast<char32_t>(b0 & 0x07) << 18) |
                              (static_cast<char32_t>(buf[1] & 0x3F) << 12) |
                              (static_cast<char32_t>(buf[2] & 0x3F) << 6) |
                              (static_cast<char32_t>(buf[3] & 0x3F));
                if(bytesRead) *bytesRead = 4;
                return Char(cp);
        }
        // Invalid UTF-8 byte — return replacement character
        if(bytesRead) *bytesRead = 1;
        return Char(static_cast<char32_t>(0xFFFD));
}

PROMEKI_NAMESPACE_END
