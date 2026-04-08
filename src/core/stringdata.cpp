/**
 * @file      stringdata.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <cassert>
#include <promeki/stringdata.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// StringData
// ============================================================================

StringData::~StringData() = default;

// ============================================================================
// StringLatin1Data
// ============================================================================

Char StringLatin1Data::charAt(size_t idx) const {
        return Char(static_cast<char>(_s[idx]));
}

void StringLatin1Data::setCharAt(size_t idx, Char ch) {
        assert(ch.codepoint() <= 0xFF);
        _s[idx] = static_cast<char>(ch.codepoint());
}

size_t StringLatin1Data::find(Char ch, size_t from) const {
        if(ch.codepoint() > 0xFF) return npos;
        size_t r = _s.find(static_cast<char>(ch.codepoint()), from);
        return r == std::string::npos ? npos : r;
}

size_t StringLatin1Data::find(const StringData &s, size_t from) const {
        size_t slen = s.length();
        if(slen == 0) return from <= _s.size() ? from : npos;
        if(slen > _s.size() || from > _s.size() - slen) return npos;
        if(s.isLatin1()) {
                // Fast path: byte-level search using the std::string overload,
                // which respects the explicit length (so embedded NULs in the
                // needle are not silently truncated by C-string semantics).
                size_t r = _s.find(s.str(), from);
                return r == std::string::npos ? npos : r;
        }
        // Cross-encoding: the needle is a Unicode-encoded StringData whose
        // cstr() would be UTF-8 bytes that do not match Latin1 storage byte
        // for byte.  Walk codepoint-by-codepoint instead.
        for(size_t i = from; i + slen <= _s.size(); ++i) {
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        char32_t haystackCp = static_cast<unsigned char>(_s[i + j]);
                        if(haystackCp != s.charAt(j).codepoint()) {
                                match = false;
                                break;
                        }
                }
                if(match) return i;
        }
        return npos;
}

size_t StringLatin1Data::rfind(Char ch, size_t from) const {
        if(ch.codepoint() > 0xFF) return npos;
        size_t r = _s.rfind(static_cast<char>(ch.codepoint()), from == npos ? std::string::npos : from);
        return r == std::string::npos ? npos : r;
}

size_t StringLatin1Data::rfind(const StringData &s, size_t from) const {
        size_t slen = s.length();
        if(slen == 0) return (from == npos || from >= _s.size()) ? _s.size() : from;
        if(slen > _s.size()) return npos;
        if(s.isLatin1()) {
                // Fast path: byte-level rfind using the std::string overload.
                size_t r = _s.rfind(s.str(), from == npos ? std::string::npos : from);
                return r == std::string::npos ? npos : r;
        }
        // Cross-encoding: walk codepoint-by-codepoint from the right.
        size_t maxStart = _s.size() - slen;
        size_t start = (from == npos || from > maxStart) ? maxStart : from;
        for(size_t i = start + 1; i > 0; --i) {
                size_t idx = i - 1;
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        char32_t haystackCp = static_cast<unsigned char>(_s[idx + j]);
                        if(haystackCp != s.charAt(j).codepoint()) {
                                match = false;
                                break;
                        }
                }
                if(match) return idx;
        }
        return npos;
}

StringData *StringLatin1Data::createSubstr(size_t pos, size_t len) const {
        return new StringLatin1Data(_s.substr(pos, len));
}

void StringLatin1Data::reverseInPlace() {
        std::reverse(_s.begin(), _s.end());
}

size_t StringLatin1Data::count(const StringData &substr) const {
        size_t slen = substr.length();
        if(slen == 0 || slen > _s.size()) return 0;
        if(substr.isLatin1()) {
                // Fast path: byte-level scan using the std::string overload.
                size_t ct = 0;
                size_t pos = 0;
                const std::string &sub = substr.str();
                while((pos = _s.find(sub, pos)) != std::string::npos) {
                        ++ct;
                        pos += sub.size();
                }
                return ct;
        }
        // Cross-encoding: codepoint-by-codepoint scan.
        size_t ct = 0;
        size_t i = 0;
        while(i + slen <= _s.size()) {
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        char32_t haystackCp = static_cast<unsigned char>(_s[i + j]);
                        if(haystackCp != substr.charAt(j).codepoint()) {
                                match = false;
                                break;
                        }
                }
                if(match) { ++ct; i += slen; } else { ++i; }
        }
        return ct;
}

void StringLatin1Data::append(const StringData &other) {
        _s += other.str();
}

void StringLatin1Data::append(Char ch) {
        assert(ch.codepoint() <= 0xFF);
        _s += static_cast<char>(ch.codepoint());
}

void StringLatin1Data::insert(size_t pos, const StringData &s) {
        _s.insert(pos, s.cstr(), s.byteCount());
}

void StringLatin1Data::erase(size_t pos, size_t count) {
        _s.erase(pos, count);
}

void StringLatin1Data::resize(size_t len, Char fill) {
        _s.resize(len, static_cast<char>(fill.codepoint()));
}

// ============================================================================
// StringUnicodeData
// ============================================================================

StringUnicodeData *StringUnicodeData::fromUtf8(const char *data, size_t len) {
        auto *ud = new StringUnicodeData();
        size_t pos = 0;
        while(pos < len) {
                size_t bytesRead = 0;
                Char ch = Char::fromUtf8(data + pos, &bytesRead);
                ud->_chars.pushToBack(ch);
                pos += bytesRead;
        }
        // Cache the original UTF-8 bytes
        ud->_strCache.assign(data, len);
        ud->_strDirty = false;
        return ud;
}

StringUnicodeData *StringUnicodeData::fromLatin1(const std::string &s) {
        auto *ud = new StringUnicodeData();
        ud->_chars.reserve(s.size());
        for(unsigned char c : s) {
                ud->_chars.pushToBack(Char(static_cast<char>(c)));
        }
        ud->_strDirty = true;
        return ud;
}

void StringUnicodeData::setCharAt(size_t idx, Char ch) {
        _chars[idx] = ch;
        _strDirty = true;
}

size_t StringUnicodeData::find(Char ch, size_t from) const {
        for(size_t i = from; i < _chars.size(); ++i) {
                if(_chars[i] == ch) return i;
        }
        return npos;
}

size_t StringUnicodeData::find(const StringData &s, size_t from) const {
        size_t slen = s.length();
        if(slen == 0) return from <= _chars.size() ? from : npos;
        if(slen > _chars.size()) return npos;
        for(size_t i = from; i + slen <= _chars.size(); ++i) {
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        if(_chars[i + j] != s.charAt(j)) {
                                match = false;
                                break;
                        }
                }
                if(match) return i;
        }
        return npos;
}

size_t StringUnicodeData::rfind(Char ch, size_t from) const {
        size_t start = (from == npos || from >= _chars.size()) ? _chars.size() : from + 1;
        for(size_t i = start; i > 0; --i) {
                if(_chars[i - 1] == ch) return i - 1;
        }
        return npos;
}

size_t StringUnicodeData::rfind(const StringData &s, size_t from) const {
        size_t slen = s.length();
        if(slen == 0) return (from == npos || from >= _chars.size()) ? _chars.size() : from;
        if(slen > _chars.size()) return npos;
        size_t start = (from == npos || from + slen > _chars.size()) ? _chars.size() - slen : from;
        for(size_t i = start + 1; i > 0; --i) {
                size_t idx = i - 1;
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        if(_chars[idx + j] != s.charAt(j)) { match = false; break; }
                }
                if(match) return idx;
        }
        return npos;
}

StringData *StringUnicodeData::createSubstr(size_t pos, size_t len) const {
        size_t end = pos + len;
        if(end > _chars.size()) end = _chars.size();
        // If every codepoint in the slice fits in Latin1, return the cheaper
        // Latin1 backend instead of cloning into another Unicode codepoint
        // list.  This shrinks substrings of mostly-ASCII Unicode strings
        // (e.g. PROMEKI_STRING("café lait").substr(5)) back down to one byte
        // per character.
        bool allLatin1 = true;
        for(size_t i = pos; i < end; ++i) {
                if(_chars[i].codepoint() > 0xFF) { allLatin1 = false; break; }
        }
        if(allLatin1) {
                std::string s;
                s.reserve(end - pos);
                for(size_t i = pos; i < end; ++i) {
                        s.push_back(static_cast<char>(_chars[i].codepoint()));
                }
                return new StringLatin1Data(std::move(s));
        }
        auto *ud = new StringUnicodeData();
        ud->_chars.reserve(end - pos);
        for(size_t i = pos; i < end; ++i) {
                ud->_chars.pushToBack(_chars[i]);
        }
        return ud;
}

void StringUnicodeData::reverseInPlace() {
        std::reverse(_chars.begin(), _chars.end());
        _strDirty = true;
}

size_t StringUnicodeData::count(const StringData &substr) const {
        size_t slen = substr.length();
        if(slen == 0) return 0;
        size_t ct = 0;
        size_t pos = 0;
        while(pos + slen <= _chars.size()) {
                size_t found = find(substr, pos);
                if(found == npos) break;
                ++ct;
                pos = found + slen;
        }
        return ct;
}

void StringUnicodeData::append(const StringData &other) {
        for(size_t i = 0; i < other.length(); ++i) {
                _chars.pushToBack(other.charAt(i));
        }
        _strDirty = true;
}

void StringUnicodeData::append(Char ch) {
        _chars.pushToBack(ch);
        _strDirty = true;
}

void StringUnicodeData::insert(size_t pos, const StringData &s) {
        for(size_t i = 0; i < s.length(); ++i) {
                _chars.insert(pos + i, s.charAt(i));
        }
        _strDirty = true;
}

void StringUnicodeData::erase(size_t pos, size_t count) {
        size_t end = pos + count;
        if(end > _chars.size()) end = _chars.size();
        if(pos < end) {
                _chars.erase(_chars.cbegin() + pos, _chars.cbegin() + end);
        }
        _strDirty = true;
}

void StringUnicodeData::clear() {
        _chars.clear();
        _strCache.clear();
        _strDirty = false;
}

void StringUnicodeData::resize(size_t len, Char fill) {
        while(_chars.size() < len) _chars.pushToBack(fill);
        while(_chars.size() > len) _chars.remove(_chars.size() - 1);
        _strDirty = true;
}

uint64_t StringUnicodeData::hash() const {
        // Use the endian-independent codepoint mixer so a Unicode-encoded
        // string hashes identically to a Latin1-encoded string with the
        // same logical content.
        uint64_t seed = 0xcbf29ce484222325ULL;
        for(size_t i = 0; i < _chars.size(); ++i) {
                seed = fnv1aMixCodepoint(seed, _chars[i].codepoint());
        }
        return seed;
}

void StringUnicodeData::ensureEncoded() const {
        if(!_strDirty) return;
        _strCache.clear();
        char buf[4];
        for(size_t i = 0; i < _chars.size(); ++i) {
                size_t n = _chars[i].toUtf8(buf);
                _strCache.append(buf, n);
        }
        _strDirty = false;
}

size_t StringUnicodeData::byteCount() const {
        ensureEncoded();
        return _strCache.size();
}

uint8_t StringUnicodeData::byteAt(size_t idx) const {
        ensureEncoded();
        return static_cast<uint8_t>(_strCache[idx]);
}

const char *StringUnicodeData::cstr() const {
        ensureEncoded();
        return _strCache.c_str();
}

const std::string &StringUnicodeData::str() const {
        ensureEncoded();
        return _strCache;
}

// ============================================================================
// StringLiteralData
// ============================================================================

size_t StringLiteralData::find(Char ch, size_t from) const {
        if(ch.codepoint() > 0xFF) return npos;
        char c = static_cast<char>(ch.codepoint());
        for(size_t i = from; i < _len; ++i) {
                if(_s[i] == c) return i;
        }
        return npos;
}

size_t StringLiteralData::find(const StringData &s, size_t from) const {
        size_t slen = s.length();
        if(slen == 0) return from <= _len ? from : npos;
        if(slen > _len || from > _len - slen) return npos;
        if(s.isLatin1()) {
                // Fast byte-level path: literal storage is one byte per char.
                const char *needle = s.cstr();
                for(size_t i = from; i + slen <= _len; ++i) {
                        bool match = true;
                        for(size_t j = 0; j < slen; ++j) {
                                if(_s[i + j] != needle[j]) {
                                        match = false;
                                        break;
                                }
                        }
                        if(match) return i;
                }
                return npos;
        }
        // Cross-encoding: walk codepoint-by-codepoint so a Unicode needle's
        // logical characters are matched against the Latin1 literal bytes.
        for(size_t i = from; i + slen <= _len; ++i) {
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        char32_t haystackCp = static_cast<unsigned char>(_s[i + j]);
                        if(haystackCp != s.charAt(j).codepoint()) {
                                match = false;
                                break;
                        }
                }
                if(match) return i;
        }
        return npos;
}

size_t StringLiteralData::rfind(Char ch, size_t from) const {
        if(ch.codepoint() > 0xFF) return npos;
        char c = static_cast<char>(ch.codepoint());
        size_t start = (from == npos || from >= _len) ? _len : from + 1;
        for(size_t i = start; i > 0; --i) {
                if(_s[i - 1] == c) return i - 1;
        }
        return npos;
}

size_t StringLiteralData::rfind(const StringData &s, size_t from) const {
        size_t slen = s.length();
        if(slen == 0) return (from == npos || from >= _len) ? _len : from;
        if(slen > _len) return npos;
        size_t maxStart = _len - slen;
        size_t start = (from == npos || from > maxStart) ? maxStart : from;
        if(s.isLatin1()) {
                const char *needle = s.cstr();
                for(size_t i = start + 1; i > 0; --i) {
                        size_t idx = i - 1;
                        bool match = true;
                        for(size_t j = 0; j < slen; ++j) {
                                if(_s[idx + j] != needle[j]) { match = false; break; }
                        }
                        if(match) return idx;
                }
                return npos;
        }
        // Cross-encoding: codepoint-by-codepoint scan from the right.
        for(size_t i = start + 1; i > 0; --i) {
                size_t idx = i - 1;
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        char32_t haystackCp = static_cast<unsigned char>(_s[idx + j]);
                        if(haystackCp != s.charAt(j).codepoint()) {
                                match = false;
                                break;
                        }
                }
                if(match) return idx;
        }
        return npos;
}

StringData *StringLiteralData::createSubstr(size_t pos, size_t len) const {
        if(pos + len > _len) len = _len - pos;
        return new StringLatin1Data(_s + pos, len);
}

size_t StringLiteralData::count(const StringData &substr) const {
        size_t slen = substr.length();
        if(slen == 0 || slen > _len) return 0;
        size_t ct = 0;
        if(substr.isLatin1()) {
                const char *needle = substr.cstr();
                for(size_t i = 0; i + slen <= _len; ) {
                        bool match = true;
                        for(size_t j = 0; j < slen; ++j) {
                                if(_s[i + j] != needle[j]) { match = false; break; }
                        }
                        if(match) { ++ct; i += slen; } else { ++i; }
                }
                return ct;
        }
        // Cross-encoding: codepoint-by-codepoint scan.
        for(size_t i = 0; i + slen <= _len; ) {
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        char32_t haystackCp = static_cast<unsigned char>(_s[i + j]);
                        if(haystackCp != substr.charAt(j).codepoint()) {
                                match = false;
                                break;
                        }
                }
                if(match) { ++ct; i += slen; } else { ++i; }
        }
        return ct;
}

const std::string &StringLiteralData::str() const {
        std::call_once(_strOnce, [this]() {
                _strCache.assign(_s, _len);
        });
        return _strCache;
}

// ============================================================================
// StringUnicodeLiteralData
// ============================================================================

StringData *StringUnicodeLiteralData::_promeki_clone() const {
        List<Char> chars;
        chars.reserve(_charCount);
        for(size_t i = 0; i < _charCount; ++i) {
                chars.pushToBack(Char(_codepoints[i]));
        }
        return new StringUnicodeData(std::move(chars));
}

size_t StringUnicodeLiteralData::find(Char ch, size_t from) const {
        for(size_t i = from; i < _charCount; ++i) {
                if(_codepoints[i] == ch.codepoint()) return i;
        }
        return npos;
}

size_t StringUnicodeLiteralData::find(const StringData &s, size_t from) const {
        size_t slen = s.length();
        if(slen == 0) return from <= _charCount ? from : npos;
        if(slen > _charCount) return npos;
        for(size_t i = from; i + slen <= _charCount; ++i) {
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        if(Char(_codepoints[i + j]) != s.charAt(j)) {
                                match = false;
                                break;
                        }
                }
                if(match) return i;
        }
        return npos;
}

size_t StringUnicodeLiteralData::rfind(Char ch, size_t from) const {
        size_t start = (from == npos || from >= _charCount) ? _charCount : from + 1;
        for(size_t i = start; i > 0; --i) {
                if(_codepoints[i - 1] == ch.codepoint()) return i - 1;
        }
        return npos;
}

size_t StringUnicodeLiteralData::rfind(const StringData &s, size_t from) const {
        size_t slen = s.length();
        if(slen == 0) return (from == npos || from >= _charCount) ? _charCount : from;
        if(slen > _charCount) return npos;
        size_t start = (from == npos || from + slen > _charCount) ? _charCount - slen : from;
        for(size_t i = start + 1; i > 0; --i) {
                size_t idx = i - 1;
                bool match = true;
                for(size_t j = 0; j < slen; ++j) {
                        if(Char(_codepoints[idx + j]) != s.charAt(j)) { match = false; break; }
                }
                if(match) return idx;
        }
        return npos;
}

StringData *StringUnicodeLiteralData::createSubstr(size_t pos, size_t len) const {
        if(pos + len > _charCount) len = _charCount - pos;
        // If the slice is all Latin1-fittable, return the cheaper Latin1
        // backend instead of cloning into a Unicode codepoint list.
        bool allLatin1 = true;
        for(size_t i = pos; i < pos + len; ++i) {
                if(_codepoints[i] > 0xFF) { allLatin1 = false; break; }
        }
        if(allLatin1) {
                std::string s;
                s.reserve(len);
                for(size_t i = pos; i < pos + len; ++i) {
                        s.push_back(static_cast<char>(_codepoints[i]));
                }
                return new StringLatin1Data(std::move(s));
        }
        List<Char> chars;
        chars.reserve(len);
        for(size_t i = pos; i < pos + len; ++i) {
                chars.pushToBack(Char(_codepoints[i]));
        }
        return new StringUnicodeData(std::move(chars));
}

size_t StringUnicodeLiteralData::count(const StringData &substr) const {
        size_t slen = substr.length();
        if(slen == 0) return 0;
        size_t ct = 0;
        size_t pos = 0;
        while(pos + slen <= _charCount) {
                size_t found = find(substr, pos);
                if(found == npos) break;
                ++ct;
                pos = found + slen;
        }
        return ct;
}

const std::string &StringUnicodeLiteralData::str() const {
        std::call_once(_strOnce, [this]() {
                _strCache.assign(_bytes, _byteLen);
        });
        return _strCache;
}

PROMEKI_NAMESPACE_END
