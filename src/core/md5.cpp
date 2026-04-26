/**
 * @file      md5.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * MD5 message-digest algorithm (RFC 1321).
 */

#include <cstring>
#include <promeki/md5.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        struct MD5Context {
                        uint32_t state[4];
                        uint64_t count;
                        uint8_t  buffer[64];
        };

        inline uint32_t F(uint32_t x, uint32_t y, uint32_t z) {
                return (x & y) | (~x & z);
        }
        inline uint32_t G(uint32_t x, uint32_t y, uint32_t z) {
                return (x & z) | (y & ~z);
        }
        inline uint32_t H(uint32_t x, uint32_t y, uint32_t z) {
                return x ^ y ^ z;
        }
        inline uint32_t I(uint32_t x, uint32_t y, uint32_t z) {
                return y ^ (x | ~z);
        }

        inline uint32_t rotl(uint32_t x, int n) {
                return (x << n) | (x >> (32 - n));
        }

        inline void step(uint32_t (*f)(uint32_t, uint32_t, uint32_t), uint32_t &a, uint32_t b, uint32_t c, uint32_t d,
                         uint32_t x, uint32_t t, int s) {
                a += f(b, c, d) + x + t;
                a = rotl(a, s) + b;
        }

        inline uint32_t decode32(const uint8_t *p) {
                return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        inline void encode32(uint8_t *out, uint32_t val) {
                out[0] = static_cast<uint8_t>(val);
                out[1] = static_cast<uint8_t>(val >> 8);
                out[2] = static_cast<uint8_t>(val >> 16);
                out[3] = static_cast<uint8_t>(val >> 24);
        }

        void md5Transform(uint32_t state[4], const uint8_t block[64]) {
                uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
                uint32_t x[16];
                for (int i = 0; i < 16; i++) x[i] = decode32(block + i * 4);

                // Round 1
                step(F, a, b, c, d, x[0], 0xd76aa478, 7);
                step(F, d, a, b, c, x[1], 0xe8c7b756, 12);
                step(F, c, d, a, b, x[2], 0x242070db, 17);
                step(F, b, c, d, a, x[3], 0xc1bdceee, 22);
                step(F, a, b, c, d, x[4], 0xf57c0faf, 7);
                step(F, d, a, b, c, x[5], 0x4787c62a, 12);
                step(F, c, d, a, b, x[6], 0xa8304613, 17);
                step(F, b, c, d, a, x[7], 0xfd469501, 22);
                step(F, a, b, c, d, x[8], 0x698098d8, 7);
                step(F, d, a, b, c, x[9], 0x8b44f7af, 12);
                step(F, c, d, a, b, x[10], 0xffff5bb1, 17);
                step(F, b, c, d, a, x[11], 0x895cd7be, 22);
                step(F, a, b, c, d, x[12], 0x6b901122, 7);
                step(F, d, a, b, c, x[13], 0xfd987193, 12);
                step(F, c, d, a, b, x[14], 0xa679438e, 17);
                step(F, b, c, d, a, x[15], 0x49b40821, 22);

                // Round 2
                step(G, a, b, c, d, x[1], 0xf61e2562, 5);
                step(G, d, a, b, c, x[6], 0xc040b340, 9);
                step(G, c, d, a, b, x[11], 0x265e5a51, 14);
                step(G, b, c, d, a, x[0], 0xe9b6c7aa, 20);
                step(G, a, b, c, d, x[5], 0xd62f105d, 5);
                step(G, d, a, b, c, x[10], 0x02441453, 9);
                step(G, c, d, a, b, x[15], 0xd8a1e681, 14);
                step(G, b, c, d, a, x[4], 0xe7d3fbc8, 20);
                step(G, a, b, c, d, x[9], 0x21e1cde6, 5);
                step(G, d, a, b, c, x[14], 0xc33707d6, 9);
                step(G, c, d, a, b, x[3], 0xf4d50d87, 14);
                step(G, b, c, d, a, x[8], 0x455a14ed, 20);
                step(G, a, b, c, d, x[13], 0xa9e3e905, 5);
                step(G, d, a, b, c, x[2], 0xfcefa3f8, 9);
                step(G, c, d, a, b, x[7], 0x676f02d9, 14);
                step(G, b, c, d, a, x[12], 0x8d2a4c8a, 20);

                // Round 3
                step(H, a, b, c, d, x[5], 0xfffa3942, 4);
                step(H, d, a, b, c, x[8], 0x8771f681, 11);
                step(H, c, d, a, b, x[11], 0x6d9d6122, 16);
                step(H, b, c, d, a, x[14], 0xfde5380c, 23);
                step(H, a, b, c, d, x[1], 0xa4beea44, 4);
                step(H, d, a, b, c, x[4], 0x4bdecfa9, 11);
                step(H, c, d, a, b, x[7], 0xf6bb4b60, 16);
                step(H, b, c, d, a, x[10], 0xbebfbc70, 23);
                step(H, a, b, c, d, x[13], 0x289b7ec6, 4);
                step(H, d, a, b, c, x[0], 0xeaa127fa, 11);
                step(H, c, d, a, b, x[3], 0xd4ef3085, 16);
                step(H, b, c, d, a, x[6], 0x04881d05, 23);
                step(H, a, b, c, d, x[9], 0xd9d4d039, 4);
                step(H, d, a, b, c, x[12], 0xe6db99e5, 11);
                step(H, c, d, a, b, x[15], 0x1fa27cf8, 16);
                step(H, b, c, d, a, x[2], 0xc4ac5665, 23);

                // Round 4
                step(I, a, b, c, d, x[0], 0xf4292244, 6);
                step(I, d, a, b, c, x[7], 0x432aff97, 10);
                step(I, c, d, a, b, x[14], 0xab9423a7, 15);
                step(I, b, c, d, a, x[5], 0xfc93a039, 21);
                step(I, a, b, c, d, x[12], 0x655b59c3, 6);
                step(I, d, a, b, c, x[3], 0x8f0ccc92, 10);
                step(I, c, d, a, b, x[10], 0xffeff47d, 15);
                step(I, b, c, d, a, x[1], 0x85845dd1, 21);
                step(I, a, b, c, d, x[8], 0x6fa87e4f, 6);
                step(I, d, a, b, c, x[15], 0xfe2ce6e0, 10);
                step(I, c, d, a, b, x[6], 0xa3014314, 15);
                step(I, b, c, d, a, x[13], 0x4e0811a1, 21);
                step(I, a, b, c, d, x[4], 0xf7537e82, 6);
                step(I, d, a, b, c, x[11], 0xbd3af235, 10);
                step(I, c, d, a, b, x[2], 0x2ad7d2bb, 15);
                step(I, b, c, d, a, x[9], 0xeb86d391, 21);

                state[0] += a;
                state[1] += b;
                state[2] += c;
                state[3] += d;
        }

        void md5Init(MD5Context &ctx) {
                ctx.state[0] = 0x67452301;
                ctx.state[1] = 0xefcdab89;
                ctx.state[2] = 0x98badcfe;
                ctx.state[3] = 0x10325476;
                ctx.count = 0;
        }

        void md5Update(MD5Context &ctx, const uint8_t *data, size_t len) {
                size_t index = static_cast<size_t>(ctx.count % 64);
                ctx.count += len;
                size_t i = 0;
                if (index) {
                        size_t part = 64 - index;
                        if (len >= part) {
                                std::memcpy(ctx.buffer + index, data, part);
                                md5Transform(ctx.state, ctx.buffer);
                                i = part;
                        } else {
                                std::memcpy(ctx.buffer + index, data, len);
                                return;
                        }
                }
                for (; i + 64 <= len; i += 64) {
                        md5Transform(ctx.state, data + i);
                }
                if (i < len) {
                        std::memcpy(ctx.buffer, data + i, len - i);
                }
        }

        MD5Digest md5Final(MD5Context &ctx) {
                uint8_t  bits[8];
                uint64_t bitCount = ctx.count * 8;
                for (int i = 0; i < 8; i++) bits[i] = static_cast<uint8_t>(bitCount >> (i * 8));

                size_t  index = static_cast<size_t>(ctx.count % 64);
                size_t  padLen = (index < 56) ? (56 - index) : (120 - index);
                uint8_t padding[128] = {};
                padding[0] = 0x80;
                md5Update(ctx, padding, padLen);
                md5Update(ctx, bits, 8);

                MD5Digest digest;
                for (int i = 0; i < 4; i++) encode32(digest.data() + i * 4, ctx.state[i]);
                return digest;
        }

} // anonymous namespace

MD5Digest md5(const void *data, size_t len) {
        MD5Context ctx;
        md5Init(ctx);
        md5Update(ctx, static_cast<const uint8_t *>(data), len);
        return md5Final(ctx);
}

PROMEKI_NAMESPACE_END
