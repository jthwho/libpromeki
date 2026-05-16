/**
 * @file      hmac.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/sha1.h>
#include <promeki/sha2.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief HMAC-SHA-256 of @p data under @p key (RFC 2104).
 * @ingroup crypto
 *
 * One-shot helper.  For the RTMP complex handshake, which feeds the
 * 1504 bytes outside the digest field as two regions, prefer the
 * streaming @ref HmacSha256 class — it avoids the scratch concat.
 */
SHA256Digest hmacSha256(const void *key,
                        size_t      keyLen,
                        const void *data,
                        size_t      dataLen);

/**
 * @brief HMAC-SHA-1 of @p data under @p key (RFC 2104).
 * @ingroup crypto
 *
 * Provided alongside HMAC-SHA-256 because the implementation is
 * trivially the same shape and the library already ships @ref sha1.
 * Not used by RTMP itself; included for general-purpose use.
 */
SHA1Digest   hmacSha1(const void *key,
                      size_t      keyLen,
                      const void *data,
                      size_t      dataLen);

/**
 * @brief Streaming HMAC-SHA-256 hasher.
 * @ingroup crypto
 *
 * The streaming form is what the RTMP complex handshake uses — the
 * 1504-byte digest input is split into two non-contiguous regions
 * around the 32-byte digest field, and @ref update lets callers
 * feed both regions without materializing the concatenation.
 *
 * Thread Safety: not thread-safe.  Use one instance per thread.
 */
class HmacSha256 {
        public:
                HmacSha256(const void *key, size_t keyLen);
                ~HmacSha256();

                HmacSha256(const HmacSha256 &) = delete;
                HmacSha256 &operator=(const HmacSha256 &) = delete;
                HmacSha256(HmacSha256 &&) noexcept = default;
                HmacSha256 &operator=(HmacSha256 &&) noexcept = default;

                /** @brief Append @p len bytes from @p data to the inner-hash input. */
                void update(const void *data, size_t len);

                /** @brief Compute and return the final HMAC tag. */
                SHA256Digest finalize();

        private:
                struct Impl;
                UniquePtr<Impl> _d;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
