/**
 * @file      core/fnv1a.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/core/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Computes the FNV-1a hash of a null-terminated string at compile time.
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

PROMEKI_NAMESPACE_END
