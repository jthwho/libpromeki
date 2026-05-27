/**
 * @file      sha2.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SHA-256 message-digest algorithm (RFC 6234 / FIPS 180-4).  Implementation
 * mirrors the shape of @c sha1.cpp: a small file-local context plus
 * init/update/transform/final helpers, wrapped by the public free function
 * and the streaming @ref Sha256 class.
 */

#include <cstring>
#include <promeki/sha2.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        struct SHA256Context {
                        uint32_t state[8];
                        uint64_t count;
                        uint8_t  buffer[64];
        };

        // First 32 bits of the fractional parts of the cube roots of the
        // first 64 primes — RFC 6234 §5.1.
        constexpr uint32_t kSha256K[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        inline uint32_t rotr(uint32_t x, int n) {
                return (x >> n) | (x << (32 - n));
        }

        inline uint32_t decode32be(const uint8_t *p) {
                return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                       (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        }

        inline void encode32be(uint8_t *out, uint32_t val) {
                out[0] = static_cast<uint8_t>(val >> 24);
                out[1] = static_cast<uint8_t>(val >> 16);
                out[2] = static_cast<uint8_t>(val >> 8);
                out[3] = static_cast<uint8_t>(val);
        }

        void sha256Transform(uint32_t state[8], const uint8_t block[64]) {
                uint32_t w[64];
                for (int i = 0; i < 16; i++) w[i] = decode32be(block + i * 4);
                for (int i = 16; i < 64; i++) {
                        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }

                uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
                uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

                for (int i = 0; i < 64; i++) {
                        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                        uint32_t ch = (e & f) ^ (~e & g);
                        uint32_t t1 = h + S1 + ch + kSha256K[i] + w[i];
                        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                        uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
                        uint32_t t2 = S0 + mj;
                        h = g;
                        g = f;
                        f = e;
                        e = d + t1;
                        d = c;
                        c = b;
                        b = a;
                        a = t1 + t2;
                }

                state[0] += a;
                state[1] += b;
                state[2] += c;
                state[3] += d;
                state[4] += e;
                state[5] += f;
                state[6] += g;
                state[7] += h;
        }

        void sha256Init(SHA256Context &ctx) {
                ctx.state[0] = 0x6a09e667;
                ctx.state[1] = 0xbb67ae85;
                ctx.state[2] = 0x3c6ef372;
                ctx.state[3] = 0xa54ff53a;
                ctx.state[4] = 0x510e527f;
                ctx.state[5] = 0x9b05688c;
                ctx.state[6] = 0x1f83d9ab;
                ctx.state[7] = 0x5be0cd19;
                ctx.count = 0;
        }

        void sha256Update(SHA256Context &ctx, const uint8_t *data, size_t len) {
                size_t index = static_cast<size_t>(ctx.count % 64);
                ctx.count += len;
                size_t i = 0;
                if (index) {
                        size_t part = 64 - index;
                        if (len >= part) {
                                std::memcpy(ctx.buffer + index, data, part);
                                sha256Transform(ctx.state, ctx.buffer);
                                i = part;
                        } else {
                                std::memcpy(ctx.buffer + index, data, len);
                                return;
                        }
                }
                for (; i + 64 <= len; i += 64) {
                        sha256Transform(ctx.state, data + i);
                }
                if (i < len) {
                        std::memcpy(ctx.buffer, data + i, len - i);
                }
        }

        SHA256Digest sha256Final(SHA256Context &ctx) {
                uint8_t  bits[8];
                uint64_t bitCount = ctx.count * 8;
                for (int i = 0; i < 8; i++) bits[i] = static_cast<uint8_t>(bitCount >> ((7 - i) * 8));

                size_t  index = static_cast<size_t>(ctx.count % 64);
                size_t  padLen = (index < 56) ? (56 - index) : (120 - index);
                uint8_t padding[128] = {};
                padding[0] = 0x80;
                sha256Update(ctx, padding, padLen);
                sha256Update(ctx, bits, 8);

                SHA256Digest digest;
                for (int i = 0; i < 8; i++) encode32be(digest.data() + i * 4, ctx.state[i]);
                return digest;
        }

} // anonymous namespace

SHA256Digest sha256(const void *data, size_t len) {
        SHA256Context ctx;
        sha256Init(ctx);
        sha256Update(ctx, static_cast<const uint8_t *>(data), len);
        return sha256Final(ctx);
}

struct Sha256::Impl {
                SHA256Context ctx;
};

Sha256::Sha256() : _d(UniquePtr<Impl>::create()) {
        sha256Init(_d->ctx);
}

Sha256::~Sha256() = default;

void Sha256::update(const void *data, size_t len) {
        sha256Update(_d->ctx, static_cast<const uint8_t *>(data), len);
}

SHA256Digest Sha256::finalize() {
        return sha256Final(_d->ctx);
}

PROMEKI_NAMESPACE_END
