/**
 * @file      sha1.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * SHA-1 message-digest algorithm (RFC 3174).
 */

#include <cstring>
#include <promeki/sha1.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

struct SHA1Context {
        uint32_t state[5];
        uint64_t count;
        uint8_t  buffer[64];
};

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

inline uint32_t decode32be(const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 24)
             | (static_cast<uint32_t>(p[1]) << 16)
             | (static_cast<uint32_t>(p[2]) << 8)
             |  static_cast<uint32_t>(p[3]);
}

inline void encode32be(uint8_t *out, uint32_t val) {
        out[0] = static_cast<uint8_t>(val >> 24);
        out[1] = static_cast<uint8_t>(val >> 16);
        out[2] = static_cast<uint8_t>(val >> 8);
        out[3] = static_cast<uint8_t>(val);
}

void sha1Transform(uint32_t state[5], const uint8_t block[64]) {
        uint32_t w[80];
        for(int i = 0; i < 16; i++) w[i] = decode32be(block + i * 4);
        for(int i = 16; i < 80; i++) w[i] = rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

        for(int i = 0; i < 80; i++) {
                uint32_t f, k;
                if(i < 20) {
                        f = (b & c) | (~b & d);
                        k = 0x5A827999;
                } else if(i < 40) {
                        f = b ^ c ^ d;
                        k = 0x6ED9EBA1;
                } else if(i < 60) {
                        f = (b & c) | (b & d) | (c & d);
                        k = 0x8F1BBCDC;
                } else {
                        f = b ^ c ^ d;
                        k = 0xCA62C1D6;
                }
                uint32_t temp = rotl(a, 5) + f + e + k + w[i];
                e = d;
                d = c;
                c = rotl(b, 30);
                b = a;
                a = temp;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
}

void sha1Init(SHA1Context &ctx) {
        ctx.state[0] = 0x67452301;
        ctx.state[1] = 0xEFCDAB89;
        ctx.state[2] = 0x98BADCFE;
        ctx.state[3] = 0x10325476;
        ctx.state[4] = 0xC3D2E1F0;
        ctx.count = 0;
}

void sha1Update(SHA1Context &ctx, const uint8_t *data, size_t len) {
        size_t index = static_cast<size_t>(ctx.count % 64);
        ctx.count += len;
        size_t i = 0;
        if(index) {
                size_t part = 64 - index;
                if(len >= part) {
                        std::memcpy(ctx.buffer + index, data, part);
                        sha1Transform(ctx.state, ctx.buffer);
                        i = part;
                } else {
                        std::memcpy(ctx.buffer + index, data, len);
                        return;
                }
        }
        for(; i + 64 <= len; i += 64) {
                sha1Transform(ctx.state, data + i);
        }
        if(i < len) {
                std::memcpy(ctx.buffer, data + i, len - i);
        }
}

SHA1Digest sha1Final(SHA1Context &ctx) {
        uint8_t bits[8];
        uint64_t bitCount = ctx.count * 8;
        for(int i = 0; i < 8; i++) bits[i] = static_cast<uint8_t>(bitCount >> ((7 - i) * 8));

        size_t index = static_cast<size_t>(ctx.count % 64);
        size_t padLen = (index < 56) ? (56 - index) : (120 - index);
        uint8_t padding[128] = {};
        padding[0] = 0x80;
        sha1Update(ctx, padding, padLen);
        sha1Update(ctx, bits, 8);

        SHA1Digest digest;
        for(int i = 0; i < 5; i++) encode32be(digest.data() + i * 4, ctx.state[i]);
        return digest;
}

} // anonymous namespace

SHA1Digest sha1(const void *data, size_t len) {
        SHA1Context ctx;
        sha1Init(ctx);
        sha1Update(ctx, static_cast<const uint8_t *>(data), len);
        return sha1Final(ctx);
}

PROMEKI_NAMESPACE_END
