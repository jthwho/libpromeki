/**
 * @file      fnv1a.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Computes the FNV-1a hash of a null-terminated string at compile time.
 * @ingroup util
 *
 * Uses the 64-bit FNV-1a algorithm to produce a hash from a C string.
 * The function is constexpr, so it can be evaluated at compile time when
 * given a constant expression argument.
 *
 * @param s     Pointer to the null-terminated input string.
 * @param seed  Initial hash value (defaults to the FNV offset basis).
 * @return The 64-bit FNV-1a hash.
 */
constexpr uint64_t fnv1a(const char *s, uint64_t seed = 0xcbf29ce484222325ULL) {
        return *s ? fnv1a(s + 1, (seed ^ static_cast<uint64_t>(*s)) * 0x100000001b3ULL) : seed;
}

/**
 * @brief Computes the FNV-1a hash of a block of data.
 *
 * Iterates over a byte range and applies the FNV-1a mixing step to
 * each byte.  The function is constexpr for use in constant expressions.
 *
 * @param data  Pointer to the input data.
 * @param len   Length of the input data in bytes.
 * @param seed  Initial hash value (defaults to the FNV offset basis).
 * @return The 64-bit FNV-1a hash.
 */
constexpr uint64_t fnv1aData(const void *data, size_t len, uint64_t seed = 0xcbf29ce484222325ULL) {
        const auto *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < len; ++i) {
                seed = (seed ^ static_cast<uint64_t>(p[i])) * 0x100000001b3ULL;
        }
        return seed;
}

/**
 * @brief Mixes a single Unicode codepoint into a running FNV-1a hash.
 *
 * Each codepoint contributes exactly four bytes to the hash, written in
 * little-endian order regardless of host endianness.  This makes the hash
 * stable across architectures and lets Latin1 and Unicode storage backends
 * produce identical hashes for the same logical string content.
 *
 * @param seed The current FNV-1a accumulator.
 * @param cp   The codepoint to mix in.
 * @return The updated accumulator.
 */
constexpr uint64_t fnv1aMixCodepoint(uint64_t seed, char32_t cp) {
        constexpr uint64_t prime = 0x100000001b3ULL;
        seed = (seed ^ static_cast<uint64_t>((cp >> 0) & 0xffu)) * prime;
        seed = (seed ^ static_cast<uint64_t>((cp >> 8) & 0xffu)) * prime;
        seed = (seed ^ static_cast<uint64_t>((cp >> 16) & 0xffu)) * prime;
        seed = (seed ^ static_cast<uint64_t>((cp >> 24) & 0xffu)) * prime;
        return seed;
}

/**
 * @brief Computes the FNV-1a hash of a Unicode codepoint sequence.
 *
 * Each codepoint is mixed in as four little-endian bytes via
 * @ref fnv1aMixCodepoint, so the result is endian-independent and matches
 * @ref fnv1aLatin1AsCodepoints when given the same logical content.
 *
 * @param data  Pointer to the codepoint array.
 * @param count Number of codepoints.
 * @param seed  Initial hash value (defaults to the FNV offset basis).
 * @return The 64-bit FNV-1a hash.
 */
constexpr uint64_t fnv1aCodepoints(const char32_t *data, size_t count, uint64_t seed = 0xcbf29ce484222325ULL) {
        for (size_t i = 0; i < count; ++i) {
                seed = fnv1aMixCodepoint(seed, data[i]);
        }
        return seed;
}

/**
 * @brief Computes the FNV-1a hash of a Latin1 byte stream as if each byte
 *        were a 4-byte little-endian Unicode codepoint.
 *
 * This produces the same hash as @ref fnv1aCodepoints over a codepoint
 * array containing the same logical characters, allowing Latin1 and Unicode
 * representations of the same string to hash to identical values.
 *
 * @param data Pointer to the Latin1 byte data.
 * @param len  Number of bytes.
 * @param seed Initial hash value (defaults to the FNV offset basis).
 * @return The 64-bit FNV-1a hash.
 */
constexpr uint64_t fnv1aLatin1AsCodepoints(const void *data, size_t len, uint64_t seed = 0xcbf29ce484222325ULL) {
        const auto *p = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < len; ++i) {
                seed = fnv1aMixCodepoint(seed, static_cast<char32_t>(p[i]));
        }
        return seed;
}

PROMEKI_NAMESPACE_END
