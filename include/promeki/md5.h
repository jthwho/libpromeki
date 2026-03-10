/**
 * @file      md5.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/bytearray.h>

PROMEKI_NAMESPACE_BEGIN

/** @brief Result type for an MD5 hash (16 bytes / 128 bits). */
using MD5Digest = ByteArray<16>;

/**
 * @brief Computes the MD5 hash of a block of data.
 * @param data Pointer to the input data.
 * @param len  Length of the input data in bytes.
 * @return The 16-byte MD5 digest.
 */
MD5Digest md5(const void *data, size_t len);

PROMEKI_NAMESPACE_END
