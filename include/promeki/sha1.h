/**
 * @file      sha1.h
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
#include <promeki/bytearray.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/** @brief Result type for a SHA-1 hash (20 bytes / 160 bits). */
using SHA1Digest = ByteArray<20>;

/**
 * @brief Computes the SHA-1 hash of a block of data.
 * @ingroup crypto
 *
 * @param data Pointer to the input data.
 * @param len  Length of the input data in bytes.
 * @return The 20-byte SHA-1 digest.
 */
SHA1Digest sha1(const void *data, size_t len);

/**
 * @brief Streaming SHA-1 hasher (RFC 3174).
 * @ingroup crypto
 *
 * Mirrors @ref Sha256 — the streaming form lets callers feed input
 * in fragments without first materializing a contiguous buffer.
 *
 * Thread Safety: not thread-safe.  Use one instance per thread.
 */
class Sha1 {
        public:
                Sha1();
                ~Sha1();

                Sha1(const Sha1 &) = delete;
                Sha1 &operator=(const Sha1 &) = delete;
                Sha1(Sha1 &&) noexcept = default;
                Sha1 &operator=(Sha1 &&) noexcept = default;

                /** @brief Append @p len bytes from @p data to the hash input. */
                void update(const void *data, size_t len);

                /** @brief Compute and return the final digest. */
                SHA1Digest finalize();

        private:
                struct Impl;
                UniquePtr<Impl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
