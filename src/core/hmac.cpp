/**
 * @file      hmac.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * RFC 2104 HMAC bound to libpromeki's hand-rolled SHA-1 / SHA-256.  The
 * construction is identical for both digest functions, so the file-local
 * helpers are templated on a small @c HashTraits adapter that names the
 * block size, digest size, and a streaming-hash class.
 */

#include <cstring>
#include <promeki/hmac.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        struct Sha1Traits {
                        static constexpr size_t BlockSize  = 64;
                        static constexpr size_t DigestSize = 20;
                        using Stream                       = Sha1;
                        using Digest                       = SHA1Digest;
        };

        struct Sha256Traits {
                        static constexpr size_t BlockSize  = 64;
                        static constexpr size_t DigestSize = 32;
                        using Stream                       = Sha256;
                        using Digest                       = SHA256Digest;
        };

        // Per RFC 2104: keys longer than the hash block size are first
        // hashed; keys shorter than the block size are zero-padded.  The
        // resulting block-sized key is XORed with 0x36 (ipad) and 0x5C
        // (opad) to produce the inner / outer prefixes.
        template <typename Traits>
        void hmacKeyToPads(const void *key,
                           size_t      keyLen,
                           uint8_t     ipad[Traits::BlockSize],
                           uint8_t     opad[Traits::BlockSize]) {
                uint8_t k0[Traits::BlockSize] = {};
                if (keyLen > Traits::BlockSize) {
                        typename Traits::Stream  hk;
                        hk.update(key, keyLen);
                        typename Traits::Digest  d = hk.finalize();
                        std::memcpy(k0, d.data(), Traits::DigestSize);
                } else {
                        std::memcpy(k0, key, keyLen);
                }
                for (size_t i = 0; i < Traits::BlockSize; ++i) {
                        ipad[i] = k0[i] ^ 0x36;
                        opad[i] = k0[i] ^ 0x5C;
                }
        }

        template <typename Traits>
        typename Traits::Digest hmacOneShot(const void *key,
                                            size_t      keyLen,
                                            const void *data,
                                            size_t      dataLen) {
                uint8_t ipad[Traits::BlockSize];
                uint8_t opad[Traits::BlockSize];
                hmacKeyToPads<Traits>(key, keyLen, ipad, opad);

                typename Traits::Stream inner;
                inner.update(ipad, Traits::BlockSize);
                inner.update(data, dataLen);
                typename Traits::Digest innerDigest = inner.finalize();

                typename Traits::Stream outer;
                outer.update(opad, Traits::BlockSize);
                outer.update(innerDigest.data(), Traits::DigestSize);
                return outer.finalize();
        }

} // anonymous namespace

SHA256Digest hmacSha256(const void *key, size_t keyLen, const void *data, size_t dataLen) {
        return hmacOneShot<Sha256Traits>(key, keyLen, data, dataLen);
}

SHA1Digest hmacSha1(const void *key, size_t keyLen, const void *data, size_t dataLen) {
        return hmacOneShot<Sha1Traits>(key, keyLen, data, dataLen);
}

struct HmacSha256::Impl {
                Sha256  inner;
                uint8_t opad[Sha256Traits::BlockSize];
};

HmacSha256::HmacSha256(const void *key, size_t keyLen) : _d(UniquePtr<Impl>::create()) {
        uint8_t ipad[Sha256Traits::BlockSize];
        hmacKeyToPads<Sha256Traits>(key, keyLen, ipad, _d->opad);
        _d->inner.update(ipad, Sha256Traits::BlockSize);
}

HmacSha256::~HmacSha256() = default;

void HmacSha256::update(const void *data, size_t len) {
        _d->inner.update(data, len);
}

SHA256Digest HmacSha256::finalize() {
        SHA256Digest innerDigest = _d->inner.finalize();
        Sha256       outer;
        outer.update(_d->opad, Sha256Traits::BlockSize);
        outer.update(innerDigest.data(), innerDigest.size());
        return outer.finalize();
}

PROMEKI_NAMESPACE_END
