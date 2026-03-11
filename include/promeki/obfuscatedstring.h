/**
 * @file      obfuscatedstring.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/fnv1a.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Compile-time string obfuscation.
 *
 * Strings wrapped with the PROMEKI_OBFUSCATE macro are encoded at compile
 * time so the plaintext never appears in the binary.  Each call site gets
 * a unique key derived from the file name, line number, and build timestamp.
 *
 * The encoding applies four operations per byte (XOR, modular add,
 * bit-rotate, XOR) with 64-bit cipher-block-chaining so each output byte
 * depends on the full history of all previous bytes.  The per-byte key
 * stream is derived from a splitmix64 finalizer, and the 64-bit CBC
 * state is advanced with a multiply-xorshift step after each byte.
 *
 * Usage:
 * @code
 *   String value = PROMEKI_OBFUSCATE("my_secret");
 * @endcode
 *
 * @tparam N    Size of the string literal (including the null terminator).
 * @tparam Seed Unique per-site seed derived from file, line, and build time.
 */
template <size_t N, uint64_t Seed>
class ObfuscatedString {
        public:
                /**
                 * @brief Encodes a string literal at compile time.
                 *
                 * The constructor is consteval, guaranteeing that encoding
                 * happens entirely at compile time and the plaintext literal
                 * is never emitted into the binary.
                 *
                 * @param str The string literal to obfuscate.
                 */
                consteval ObfuscatedString(const char (&str)[N]) : _data{} {
                        uint64_t state = initState();
                        for (size_t i = 0; i < N - 1; ++i) {
                                uint64_t key = keyAt(i);
                                unsigned char k0 = static_cast<unsigned char>(key);
                                unsigned char k1 = static_cast<unsigned char>(key >> 8);
                                unsigned char c = static_cast<unsigned char>(str[i]);

                                // Round 1: XOR with position-derived key byte.
                                c ^= k0;
                                // Round 2: modular add of CBC state byte.
                                c = static_cast<unsigned char>(c + static_cast<unsigned char>(state));
                                // Round 3: rotate left by 1-7 bits (derived from second key byte).
                                unsigned shift = (k1 & 0x07) | 1;
                                c = static_cast<unsigned char>((c << shift) | (c >> (8 - shift)));
                                // Round 4: XOR with second key byte.
                                c ^= k1;

                                _data[i] = static_cast<char>(c);
                                state = advanceState(state, c, key);
                        }
                }

                /**
                 * @brief Decodes the obfuscated data back to the original string.
                 * @return The decoded plaintext as a String.
                 */
                String decode() const {
                        String out(N - 1, '\0');
                        uint64_t state = initState();
                        for (size_t i = 0; i < N - 1; ++i) {
                                uint64_t key = keyAt(i);
                                unsigned char k0 = static_cast<unsigned char>(key);
                                unsigned char k1 = static_cast<unsigned char>(key >> 8);
                                unsigned char c = static_cast<unsigned char>(_data[i]);

                                // Undo round 4: XOR with second key byte.
                                c ^= k1;
                                // Undo round 3: rotate right.
                                unsigned shift = (k1 & 0x07) | 1;
                                c = static_cast<unsigned char>((c >> shift) | (c << (8 - shift)));
                                // Undo round 2: subtract CBC state byte.
                                c = static_cast<unsigned char>(c - static_cast<unsigned char>(state));
                                // Undo round 1: XOR.
                                c ^= k0;

                                out[i] = static_cast<char>(c);
                                state = advanceState(state, static_cast<unsigned char>(_data[i]), key);
                        }
                        return out;
                }

                /**
                 * @brief Implicit conversion to String via decode().
                 * @return The decoded plaintext as a String.
                 */
                operator String() const { return decode(); }

        private:
                // Initial 64-bit CBC state derived from the full seed.
                static constexpr uint64_t initState() {
                        uint64_t h = Seed * 0xbf58476d1ce4e5b9ULL;
                        h ^= h >> 31;
                        h *= 0x94d049bb133111ebULL;
                        h ^= h >> 31;
                        return h;
                }

                // Per-byte 64-bit key derived from Seed and position (splitmix64 finalizer).
                static constexpr uint64_t keyAt(size_t i) {
                        uint64_t h = (Seed ^ (static_cast<uint64_t>(i) * 0x9e3779b97f4a7c15ULL)) *
                                     0xbf58476d1ce4e5b9ULL;
                        h ^= h >> 31;
                        h *= 0x94d049bb133111ebULL;
                        h ^= h >> 31;
                        return h;
                }

                // Advance the 64-bit CBC state using ciphertext and key.
                static constexpr uint64_t advanceState(uint64_t state, unsigned char cipher, uint64_t key) {
                        state ^= static_cast<uint64_t>(cipher) | (key << 8);
                        state *= 0x517cc1b727220a95ULL;
                        state ^= state >> 29;
                        return state;
                }

                char _data[N - 1];
};

PROMEKI_NAMESPACE_END

/**
 * @def PROMEKI_OBFUSCATE_SEED
 * @brief Combines __FILE__, __LINE__, __DATE__, and __TIME__ into a unique per-site seed.
 */
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define PROMEKI_OBFUSCATE_SEED                                                                     \
    (::promeki::fnv1a(__FILE__) ^ (static_cast<uint64_t>(__LINE__) * 0x9e3779b97f4a7c15ULL) ^      \
     ::promeki::fnv1a(__DATE__ __TIME__))

/**
 * @def PROMEKI_OBFUSCATE(str)
 * @brief Obfuscates a string literal at compile time.
 *
 * The string is encoded at compile time and decoded at runtime on each access.
 * Returns a promeki::String containing the original plaintext.
 *
 * @param str The string literal to obfuscate.
 */
// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define PROMEKI_OBFUSCATE(str)                                                                     \
    ([]() {                                                                                        \
        static constexpr auto _obf =                                                               \
            ::promeki::ObfuscatedString<sizeof(str), PROMEKI_OBFUSCATE_SEED>(str);                 \
        return _obf.decode();                                                                      \
    }())
