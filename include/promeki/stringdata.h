/**
 * @file      stringdata.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <string>
#include <cstdint>
#include <mutex>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/char.h>
#include <promeki/list.h>
#include <promeki/fnv1a.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Pure virtual interface for String storage backends.
 * @ingroup strings
 *
 * StringData defines the contract that both Latin1 and Unicode storage
 * implementations must satisfy. It manually provides RefCount and
 * _promeki_clone() since PROMEKI_SHARED cannot be used on abstract classes.
 */
class StringData {
        public:
                /** @brief Manual reference count (PROMEKI_SHARED cannot be used on abstract classes). */
                RefCount _promeki_refct;

                /** @brief Creates a deep copy of this storage backend. */
                virtual StringData *_promeki_clone() const = 0;

                /** @brief Virtual destructor. */
                virtual ~StringData();

                /** @name Character access */
                ///@{

                /** @brief Returns the number of characters in the string. */
                virtual size_t  length() const = 0;

                /** @brief Returns the character at the given index. */
                virtual Char    charAt(size_t idx) const = 0;

                /**
                 * @brief Sets the character at the given index.
                 * @param idx Zero-based character index.
                 * @param ch  The replacement character.
                 */
                virtual void    setCharAt(size_t idx, Char ch) = 0;

                /** @brief Returns true if the storage uses Latin1 encoding (one byte per character). */
                virtual bool    isLatin1() const = 0;

                /** @brief Returns true if the storage wraps an immutable literal. */
                virtual bool    isLiteral() const { return false; }

                ///@}

                /** @name Substring / search */
                ///@{

                /**
                 * @brief Finds the first occurrence of a character starting from @p from.
                 * @param ch   The character to search for.
                 * @param from Starting character index.
                 * @return Character index of the match, or npos if not found.
                 */
                virtual size_t  find(Char ch, size_t from = 0) const = 0;

                /**
                 * @brief Finds the first occurrence of a substring starting from @p from.
                 * @param s    The substring data to search for.
                 * @param from Starting character index.
                 * @return Character index of the match, or npos if not found.
                 */
                virtual size_t  find(const StringData &s, size_t from = 0) const = 0;

                /**
                 * @brief Finds the last occurrence of a character at or before @p from.
                 * @param ch   The character to search for.
                 * @param from Maximum character index to consider (npos = end).
                 * @return Character index of the match, or npos if not found.
                 */
                virtual size_t  rfind(Char ch, size_t from = npos) const = 0;

                /**
                 * @brief Finds the last occurrence of a substring at or before @p from.
                 * @param s    The substring data to search for.
                 * @param from Maximum character index to consider (npos = end).
                 * @return Character index of the match, or npos if not found.
                 */
                virtual size_t  rfind(const StringData &s, size_t from = npos) const = 0;

                /**
                 * @brief Creates a new StringData containing a substring.
                 * @param pos Starting character index.
                 * @param len Number of characters.
                 * @return A newly allocated StringData; caller takes ownership.
                 */
                virtual StringData *createSubstr(size_t pos, size_t len) const = 0;

                /** @brief Reverses the characters in place. */
                virtual void    reverseInPlace() = 0;

                /**
                 * @brief Counts non-overlapping occurrences of a substring.
                 * @param substr The substring to count.
                 * @return Number of occurrences.
                 */
                virtual size_t  count(const StringData &substr) const = 0;

                ///@}

                /** @name Mutation */
                ///@{

                /**
                 * @brief Appends another string's data to this storage.
                 * @param other The data to append.
                 */
                virtual void    append(const StringData &other) = 0;

                /**
                 * @brief Appends a single character.
                 * @param ch The character to append.
                 */
                virtual void    append(Char ch) = 0;

                /**
                 * @brief Inserts a string at the given position.
                 * @param pos Character index at which to insert.
                 * @param s   The data to insert.
                 */
                virtual void    insert(size_t pos, const StringData &s) = 0;

                /**
                 * @brief Erases characters from the string.
                 * @param pos   Starting character index.
                 * @param count Number of characters to erase.
                 */
                virtual void    erase(size_t pos, size_t count) = 0;

                /** @brief Removes all characters from the string. */
                virtual void    clear() = 0;

                /**
                 * @brief Resizes the string to @p len characters.
                 * @param len  New character count.
                 * @param fill Character used to pad if growing.
                 */
                virtual void    resize(size_t len, Char fill = Char()) = 0;

                ///@}

                /** @name Byte-level output */
                ///@{

                /** @brief Returns the byte count of the encoded string data. */
                virtual size_t          byteCount() const = 0;

                /**
                 * @brief Returns the byte at the given index.
                 * @param idx Zero-based byte index.
                 * @return The byte value.
                 */
                virtual uint8_t         byteAt(size_t idx) const = 0;

                /** @brief Returns a null-terminated C string pointer. */
                virtual const char     *cstr() const = 0;

                /** @brief Returns a const reference to the internal std::string representation. */
                virtual const std::string &str() const = 0;

                ///@}

                /** @brief Returns true if the string has zero length. */
                bool isEmpty() const { return length() == 0; }

                /** @brief Returns a 64-bit hash of the string data. */
                virtual uint64_t hash() const = 0;

                /** @brief Sentinel value indicating "not found". */
                static constexpr size_t npos = static_cast<size_t>(-1);
};

/**
 * @brief Latin1 string storage. One byte = one character.
 *
 * All character positions and byte positions are identical.
 * This is the fast path for ASCII / Latin1 strings.
 */
class StringLatin1Data : public StringData {
        PROMEKI_SHARED_DERIVED(StringData, StringLatin1Data)
        public:
                /** @brief Default constructor. Creates empty Latin1 storage. */
                StringLatin1Data() = default;

                /**
                 * @brief Constructs from a std::string (copy).
                 * @param s The source string.
                 */
                StringLatin1Data(const std::string &s) : _s(s) {}

                /**
                 * @brief Constructs from a std::string (move).
                 * @param s The source string to move from.
                 */
                StringLatin1Data(std::string &&s) : _s(std::move(s)) {}

                /**
                 * @brief Constructs from a C string.
                 * @param s The null-terminated string (null treated as empty).
                 */
                StringLatin1Data(const char *s) : _s(s ? s : "") {}

                /**
                 * @brief Constructs from a buffer with explicit length.
                 * @param s   Pointer to character data.
                 * @param len Number of bytes.
                 */
                StringLatin1Data(const char *s, size_t len) : _s(s, len) {}

                /**
                 * @brief Constructs a string of repeated characters.
                 * @param ct Number of characters.
                 * @param c  Character to repeat.
                 */
                StringLatin1Data(size_t ct, char c) : _s(ct, c) {}

                size_t  length() const override { return _s.size(); }
                Char    charAt(size_t idx) const override;
                void    setCharAt(size_t idx, Char ch) override;
                bool    isLatin1() const override { return true; }

                size_t  find(Char ch, size_t from = 0) const override;
                size_t  find(const StringData &s, size_t from = 0) const override;
                size_t  rfind(Char ch, size_t from = npos) const override;
                size_t  rfind(const StringData &s, size_t from = npos) const override;
                StringData *createSubstr(size_t pos, size_t len) const override;
                void    reverseInPlace() override;
                size_t  count(const StringData &substr) const override;

                void    append(const StringData &other) override;
                void    append(Char ch) override;
                void    insert(size_t pos, const StringData &s) override;
                void    erase(size_t pos, size_t count) override;
                void    clear() override { _s.clear(); }
                void    resize(size_t len, Char fill = Char()) override;

                size_t          byteCount() const override { return _s.size(); }
                uint8_t         byteAt(size_t idx) const override { return static_cast<uint8_t>(_s[idx]); }
                const char     *cstr() const override { return _s.c_str(); }
                const std::string &str() const override { return _s; }

                uint64_t hash() const override {
                        // Hash each Latin1 byte as a 4-byte little-endian
                        // codepoint so the result matches StringUnicodeData
                        // for the same logical characters.
                        return fnv1aLatin1AsCodepoints(_s.data(), _s.size());
                }

                /** @brief Direct access to the underlying std::string. */
                std::string &rawStr() { return _s; }

        private:
                std::string _s;
};

/**
 * @brief Unicode string storage. Decoded codepoints in a List<Char>.
 *
 * All character positions are O(1) since codepoints are stored
 * as fixed-width char32_t values. A UTF-8 encoded std::string
 * is lazily cached for byte-level output.
 */
class StringUnicodeData : public StringData {
        PROMEKI_SHARED_DERIVED(StringData, StringUnicodeData)
        public:
                /** @brief Default constructor. Creates empty Unicode storage. */
                StringUnicodeData() : _strDirty(true) {}

                /**
                 * @brief Constructs from a list of codepoints (copy).
                 * @param chars The decoded codepoints.
                 */
                StringUnicodeData(const List<Char> &chars) : _chars(chars), _strDirty(true) {}

                /**
                 * @brief Constructs from a list of codepoints (move).
                 * @param chars The decoded codepoints to move from.
                 */
                StringUnicodeData(List<Char> &&chars) : _chars(std::move(chars)), _strDirty(true) {}

                /**
                 * @brief Creates a StringUnicodeData by decoding UTF-8 bytes.
                 * @param data Pointer to UTF-8 encoded bytes.
                 * @param len  Number of bytes.
                 * @return A new StringUnicodeData, caller takes ownership.
                 */
                static StringUnicodeData *fromUtf8(const char *data, size_t len);

                /**
                 * @brief Creates a StringUnicodeData by promoting Latin1 bytes.
                 * @param s The Latin1 string (each byte becomes a Char).
                 * @return A new StringUnicodeData, caller takes ownership.
                 */
                static StringUnicodeData *fromLatin1(const std::string &s);

                size_t  length() const override { return _chars.size(); }
                Char    charAt(size_t idx) const override { return _chars[idx]; }
                void    setCharAt(size_t idx, Char ch) override;
                bool    isLatin1() const override { return false; }

                size_t  find(Char ch, size_t from = 0) const override;
                size_t  find(const StringData &s, size_t from = 0) const override;
                size_t  rfind(Char ch, size_t from = npos) const override;
                size_t  rfind(const StringData &s, size_t from = npos) const override;
                StringData *createSubstr(size_t pos, size_t len) const override;
                void    reverseInPlace() override;
                size_t  count(const StringData &substr) const override;

                void    append(const StringData &other) override;
                void    append(Char ch) override;
                void    insert(size_t pos, const StringData &s) override;
                void    erase(size_t pos, size_t count) override;
                void    clear() override;
                void    resize(size_t len, Char fill = Char()) override;

                size_t          byteCount() const override;
                uint8_t         byteAt(size_t idx) const override;
                const char     *cstr() const override;
                const std::string &str() const override;

                uint64_t hash() const override;

        private:
                void ensureEncoded() const;

                List<Char>              _chars;
                mutable std::string     _strCache;
                mutable bool            _strDirty;
};

/**
 * @brief Immutable string storage wrapping a string literal pointer.
 *
 * No copy of the string data is made. The refcount is immortal so
 * the object is never deleted — it lives in static storage at each
 * call site via PROMEKI_STRING. COW always clones to a
 * StringLatin1Data before any mutation.
 */
class StringLiteralData : public StringData {
        public:
                /**
                 * @brief Constructs from a string literal pointer.
                 * @param s               Pointer to the literal data (must remain valid for the lifetime of this object).
                 * @param len             Length of the literal in bytes.
                 * @param precomputedHash Optional pre-computed FNV-1a hash (0 = compute on construction).
                 */
                StringLiteralData(const char *s, size_t len, uint64_t precomputedHash = 0)
                        : _s(s), _len(len), _hash(precomputedHash) {
                        // Hash each Latin1 byte as a 4-byte little-endian
                        // codepoint so the result matches StringUnicodeData
                        // for the same logical characters.
                        if(_hash == 0 && _len > 0)
                                _hash = fnv1aLatin1AsCodepoints(_s, _len);
                        _promeki_refct.setImmortal();
                }

                StringData *_promeki_clone() const override {
                        return new StringLatin1Data(_s, _len);
                }

                // Character access
                size_t  length() const override { return _len; }
                Char    charAt(size_t idx) const override {
                        return Char(static_cast<char>(_s[idx]));
                }
                void    setCharAt(size_t, Char) override { assert(false); }
                bool    isLatin1() const override { return true; }
                bool    isLiteral() const override { return true; }

                // Search
                size_t  find(Char ch, size_t from = 0) const override;
                size_t  find(const StringData &s, size_t from = 0) const override;
                size_t  rfind(Char ch, size_t from = npos) const override;
                size_t  rfind(const StringData &s, size_t from = npos) const override;
                StringData *createSubstr(size_t pos, size_t len) const override;
                void    reverseInPlace() override { assert(false); }
                size_t  count(const StringData &substr) const override;

                // Mutation (unreachable — COW always clones first)
                void    append(const StringData &) override { assert(false); }
                void    append(Char) override { assert(false); }
                void    insert(size_t, const StringData &) override { assert(false); }
                void    erase(size_t, size_t) override { assert(false); }
                void    clear() override { assert(false); }
                void    resize(size_t, Char) override { assert(false); }

                // Byte-level
                size_t          byteCount() const override { return _len; }
                uint8_t         byteAt(size_t idx) const override {
                        return static_cast<uint8_t>(_s[idx]);
                }
                const char     *cstr() const override { return _s; }
                const std::string &str() const override;

                uint64_t hash() const override { return _hash; }

        private:
                const char             *_s;
                size_t                  _len;
                uint64_t                _hash;
                mutable std::once_flag  _strOnce;
                mutable std::string     _strCache;
};

/**
 * @brief Immutable Unicode string storage wrapping compile-time decoded data.
 *
 * Wraps pointers to a pre-decoded char32_t codepoint array and the
 * original UTF-8 bytes — both live in CompiledString's constexpr
 * static storage. The refcount is immortal so the object is never
 * deleted. COW always clones to a StringUnicodeData before mutation.
 */
class StringUnicodeLiteralData : public StringData {
        public:
                /**
                 * @brief Constructs from pre-decoded codepoints and original UTF-8 bytes.
                 * @param codepoints      Pointer to decoded char32_t array (must remain valid for the lifetime of this object).
                 * @param charCount       Number of codepoints.
                 * @param bytes           Pointer to original UTF-8 bytes (must remain valid for the lifetime of this object).
                 * @param byteLen         Length of the UTF-8 data in bytes.
                 * @param precomputedHash Optional pre-computed FNV-1a hash (0 = compute on construction).
                 */
                StringUnicodeLiteralData(const char32_t *codepoints, size_t charCount,
                                         const char *bytes, size_t byteLen,
                                         uint64_t precomputedHash = 0)
                        : _codepoints(codepoints), _charCount(charCount),
                          _bytes(bytes), _byteLen(byteLen),
                          _hash(precomputedHash) {
                        // Endian-independent codepoint mixing so the result
                        // matches StringLatin1Data and StringUnicodeData for
                        // the same logical characters.
                        if(_hash == 0 && _charCount > 0)
                                _hash = fnv1aCodepoints(_codepoints, _charCount);
                        _promeki_refct.setImmortal();
                }

                StringData *_promeki_clone() const override;

                // Character access
                size_t  length() const override { return _charCount; }
                Char    charAt(size_t idx) const override {
                        return Char(_codepoints[idx]);
                }
                void    setCharAt(size_t, Char) override { assert(false); }
                bool    isLatin1() const override { return false; }
                bool    isLiteral() const override { return true; }

                // Search
                size_t  find(Char ch, size_t from = 0) const override;
                size_t  find(const StringData &s, size_t from = 0) const override;
                size_t  rfind(Char ch, size_t from = npos) const override;
                size_t  rfind(const StringData &s, size_t from = npos) const override;
                StringData *createSubstr(size_t pos, size_t len) const override;
                void    reverseInPlace() override { assert(false); }
                size_t  count(const StringData &substr) const override;

                // Mutation (unreachable — COW always clones first)
                void    append(const StringData &) override { assert(false); }
                void    append(Char) override { assert(false); }
                void    insert(size_t, const StringData &) override { assert(false); }
                void    erase(size_t, size_t) override { assert(false); }
                void    clear() override { assert(false); }
                void    resize(size_t, Char) override { assert(false); }

                // Byte-level — uses the original UTF-8 bytes directly
                size_t          byteCount() const override { return _byteLen; }
                uint8_t         byteAt(size_t idx) const override {
                        return static_cast<uint8_t>(_bytes[idx]);
                }
                const char     *cstr() const override { return _bytes; }
                const std::string &str() const override;

                uint64_t hash() const override { return _hash; }

        private:
                const char32_t         *_codepoints;
                size_t                  _charCount;
                const char             *_bytes;
                size_t                  _byteLen;
                uint64_t                _hash;
                mutable std::once_flag  _strOnce;
                mutable std::string     _strCache;
};

PROMEKI_NAMESPACE_END
