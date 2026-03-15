/**
 * @file      core/sha1.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/core/namespace.h>
#include <promeki/core/bytearray.h>

PROMEKI_NAMESPACE_BEGIN

/** @brief Result type for a SHA-1 hash (20 bytes / 160 bits). */
using SHA1Digest = ByteArray<20>;

/**
 * @brief Computes the SHA-1 hash of a block of data.
 * @param data Pointer to the input data.
 * @param len  Length of the input data in bytes.
 * @return The 20-byte SHA-1 digest.
 */
SHA1Digest sha1(const void *data, size_t len);

PROMEKI_NAMESPACE_END
