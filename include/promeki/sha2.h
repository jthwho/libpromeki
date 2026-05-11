/**
 * @file      sha2.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/bytearray.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/** @brief Result type for a SHA-256 hash (32 bytes / 256 bits). */
using SHA256Digest = ByteArray<32>;

/**
 * @brief Computes the SHA-256 hash of a contiguous block of data.
 * @ingroup crypto
 *
 * One-shot helper that mirrors the @ref sha1 / @ref md5 free functions.
 * For inputs that arrive in fragments (e.g. the RTMP complex handshake
 * walks the C1/S1 byte map in two regions around the digest field),
 * use the streaming @ref Sha256 class instead.
 *
 * @param data Pointer to the input data.
 * @param len  Length of the input data in bytes.
 * @return The 32-byte SHA-256 digest.
 */
SHA256Digest sha256(const void *data, size_t len);

/**
 * @brief Streaming SHA-256 hasher (RFC 6234).
 * @ingroup crypto
 *
 * The streaming form is what the RTMP complex handshake needs — it
 * concatenates two non-contiguous regions of the 1536-byte C1/S1
 * payload into the digest input, and @c update() lets the caller
 * feed both regions without first materializing the concatenation.
 *
 * Thread Safety: not thread-safe.  Use one instance per thread.
 *
 * Typical usage:
 * @code
 * Sha256 h;
 * h.update(part1, len1);
 * h.update(part2, len2);
 * SHA256Digest digest = h.finalize();
 * @endcode
 */
class Sha256 {
        public:
                Sha256();
                ~Sha256();

                Sha256(const Sha256 &) = delete;
                Sha256 &operator=(const Sha256 &) = delete;
                Sha256(Sha256 &&) noexcept = default;
                Sha256 &operator=(Sha256 &&) noexcept = default;

                /**
                 * @brief Append @p len bytes from @p data to the hash input.
                 * Must not be called after @ref finalize.
                 */
                void update(const void *data, size_t len);

                /**
                 * @brief Compute and return the final digest.
                 *
                 * After calling @c finalize the hasher is in a finalized
                 * state; further @ref update calls have undefined effect.
                 * To hash a new message, construct a new @c Sha256.
                 */
                SHA256Digest finalize();

        private:
                struct Impl;
                UniquePtr<Impl> _d;
};

PROMEKI_NAMESPACE_END
