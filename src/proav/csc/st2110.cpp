/**
 * @file      st2110.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Scalar CSC fast-path kernels for SMPTE ST 2110-20 wire-format
 * pgroups.  These kernels register as @ref CSCRegistry fast paths so
 * the existing @ref _videoWirePixelFormat seam at
 * @c rtpmediaio.cpp:243-247 routes producer essence through them on
 * its way to @ref RtpPayloadRawVideo, bypassing the float-SoA
 * intermediate.  Highway-accelerated replacements land in E20c-4.
 */

#include <promeki/config.h>
#if PROMEKI_ENABLE_CSC

#include <promeki/csccontext.h>
#include <promeki/cscregistry.h>
#include <promeki/pixelformat.h>
#include <cstdint>

PROMEKI_NAMESPACE_BEGIN

namespace {

// -----------------------------------------------------------------------------
// Sample readers — extract Cb/Y0/Cr/Y1 from one source pgroup (2 pixels) and
// return the four 10/12-bit samples as native uint16_t values.
//
// All ST 2110 wire packings are MSB-first BE per §6.1.1, so the writer-side
// helpers below mask each sample to its bit width and bit-pack the output
// stream directly — no intermediate sample buffer needed.
// -----------------------------------------------------------------------------

struct Uyvy422Pair {
                uint16_t cb;
                uint16_t y0;
                uint16_t cr;
                uint16_t y1;
};

inline Uyvy422Pair readUyvy_10BE(const uint8_t *p) {
        // I_422_UYVY_3x10_BE: 8 octets / 2 pixels — Cb(2 BE) Y0(2 BE) Cr(2 BE) Y1(2 BE).
        // Each 16-bit BE word carries the 10-bit sample in the low 10 bits.
        Uyvy422Pair s;
        s.cb = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8 | p[1]) & 0x3FFu);
        s.y0 = static_cast<uint16_t>((static_cast<uint16_t>(p[2]) << 8 | p[3]) & 0x3FFu);
        s.cr = static_cast<uint16_t>((static_cast<uint16_t>(p[4]) << 8 | p[5]) & 0x3FFu);
        s.y1 = static_cast<uint16_t>((static_cast<uint16_t>(p[6]) << 8 | p[7]) & 0x3FFu);
        return s;
}

inline Uyvy422Pair readUyvy_10LE(const uint8_t *p) {
        // I_422_UYVY_3x10_LE: same geometry, LE 16-bit words.
        Uyvy422Pair s;
        s.cb = static_cast<uint16_t>((static_cast<uint16_t>(p[1]) << 8 | p[0]) & 0x3FFu);
        s.y0 = static_cast<uint16_t>((static_cast<uint16_t>(p[3]) << 8 | p[2]) & 0x3FFu);
        s.cr = static_cast<uint16_t>((static_cast<uint16_t>(p[5]) << 8 | p[4]) & 0x3FFu);
        s.y1 = static_cast<uint16_t>((static_cast<uint16_t>(p[7]) << 8 | p[6]) & 0x3FFu);
        return s;
}

inline Uyvy422Pair readUyvy_12BE(const uint8_t *p) {
        Uyvy422Pair s;
        s.cb = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8 | p[1]) & 0xFFFu);
        s.y0 = static_cast<uint16_t>((static_cast<uint16_t>(p[2]) << 8 | p[3]) & 0xFFFu);
        s.cr = static_cast<uint16_t>((static_cast<uint16_t>(p[4]) << 8 | p[5]) & 0xFFFu);
        s.y1 = static_cast<uint16_t>((static_cast<uint16_t>(p[6]) << 8 | p[7]) & 0xFFFu);
        return s;
}

inline Uyvy422Pair readUyvy_12LE(const uint8_t *p) {
        Uyvy422Pair s;
        s.cb = static_cast<uint16_t>((static_cast<uint16_t>(p[1]) << 8 | p[0]) & 0xFFFu);
        s.y0 = static_cast<uint16_t>((static_cast<uint16_t>(p[3]) << 8 | p[2]) & 0xFFFu);
        s.cr = static_cast<uint16_t>((static_cast<uint16_t>(p[5]) << 8 | p[4]) & 0xFFFu);
        s.y1 = static_cast<uint16_t>((static_cast<uint16_t>(p[7]) << 8 | p[6]) & 0xFFFu);
        return s;
}

// -----------------------------------------------------------------------------
// Sample writers — emit one ST 2110-20 §6.2 pgroup to the wire stream.
// -----------------------------------------------------------------------------

// 4:2:2 / 10-bit: 5 octets per 2 pixels, 4 samples × 10 bits = 40 bits BE.
inline void writeSt2110_422_10(uint8_t *q, const Uyvy422Pair &s) {
        const uint32_t cb = s.cb & 0x3FFu;
        const uint32_t y0 = s.y0 & 0x3FFu;
        const uint32_t cr = s.cr & 0x3FFu;
        const uint32_t y1 = s.y1 & 0x3FFu;
        q[0] = static_cast<uint8_t>((cb >> 2) & 0xFFu);
        q[1] = static_cast<uint8_t>(((cb & 0x3u) << 6) | ((y0 >> 4) & 0x3Fu));
        q[2] = static_cast<uint8_t>(((y0 & 0xFu) << 4) | ((cr >> 6) & 0xFu));
        q[3] = static_cast<uint8_t>(((cr & 0x3Fu) << 2) | ((y1 >> 8) & 0x3u));
        q[4] = static_cast<uint8_t>(y1 & 0xFFu);
}

// 4:2:2 / 12-bit: 6 octets per 2 pixels, 4 samples × 12 bits = 48 bits BE.
inline void writeSt2110_422_12(uint8_t *q, const Uyvy422Pair &s) {
        const uint32_t cb = s.cb & 0xFFFu;
        const uint32_t y0 = s.y0 & 0xFFFu;
        const uint32_t cr = s.cr & 0xFFFu;
        const uint32_t y1 = s.y1 & 0xFFFu;
        q[0] = static_cast<uint8_t>((cb >> 4) & 0xFFu);
        q[1] = static_cast<uint8_t>(((cb & 0xFu) << 4) | ((y0 >> 8) & 0xFu));
        q[2] = static_cast<uint8_t>(y0 & 0xFFu);
        q[3] = static_cast<uint8_t>((cr >> 4) & 0xFFu);
        q[4] = static_cast<uint8_t>(((cr & 0xFu) << 4) | ((y1 >> 8) & 0xFu));
        q[5] = static_cast<uint8_t>(y1 & 0xFFu);
}

// -----------------------------------------------------------------------------
// Reverse-direction unpackers — read one ST 2110-20 pgroup from the wire
// stream into the four 10/12-bit samples (Cb Y0 Cr Y1).
// -----------------------------------------------------------------------------

inline Uyvy422Pair readSt2110_422_10(const uint8_t *p) {
        Uyvy422Pair s;
        s.cb = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 2) | (p[1] >> 6));
        s.y0 = static_cast<uint16_t>(((static_cast<uint16_t>(p[1]) & 0x3Fu) << 4) | (p[2] >> 4));
        s.cr = static_cast<uint16_t>(((static_cast<uint16_t>(p[2]) & 0xFu) << 6) | (p[3] >> 2));
        s.y1 = static_cast<uint16_t>(((static_cast<uint16_t>(p[3]) & 0x3u) << 8) | p[4]);
        return s;
}

inline Uyvy422Pair readSt2110_422_12(const uint8_t *p) {
        Uyvy422Pair s;
        s.cb = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 4) | (p[1] >> 4));
        s.y0 = static_cast<uint16_t>(((static_cast<uint16_t>(p[1]) & 0xFu) << 8) | p[2]);
        s.cr = static_cast<uint16_t>((static_cast<uint16_t>(p[3]) << 4) | (p[4] >> 4));
        s.y1 = static_cast<uint16_t>(((static_cast<uint16_t>(p[4]) & 0xFu) << 8) | p[5]);
        return s;
}

inline void writeUyvy_10BE(uint8_t *q, const Uyvy422Pair &s) {
        q[0] = static_cast<uint8_t>((s.cb >> 8) & 0x3u);
        q[1] = static_cast<uint8_t>(s.cb & 0xFFu);
        q[2] = static_cast<uint8_t>((s.y0 >> 8) & 0x3u);
        q[3] = static_cast<uint8_t>(s.y0 & 0xFFu);
        q[4] = static_cast<uint8_t>((s.cr >> 8) & 0x3u);
        q[5] = static_cast<uint8_t>(s.cr & 0xFFu);
        q[6] = static_cast<uint8_t>((s.y1 >> 8) & 0x3u);
        q[7] = static_cast<uint8_t>(s.y1 & 0xFFu);
}

inline void writeUyvy_10LE(uint8_t *q, const Uyvy422Pair &s) {
        q[0] = static_cast<uint8_t>(s.cb & 0xFFu);
        q[1] = static_cast<uint8_t>((s.cb >> 8) & 0x3u);
        q[2] = static_cast<uint8_t>(s.y0 & 0xFFu);
        q[3] = static_cast<uint8_t>((s.y0 >> 8) & 0x3u);
        q[4] = static_cast<uint8_t>(s.cr & 0xFFu);
        q[5] = static_cast<uint8_t>((s.cr >> 8) & 0x3u);
        q[6] = static_cast<uint8_t>(s.y1 & 0xFFu);
        q[7] = static_cast<uint8_t>((s.y1 >> 8) & 0x3u);
}

inline void writeUyvy_12BE(uint8_t *q, const Uyvy422Pair &s) {
        q[0] = static_cast<uint8_t>((s.cb >> 8) & 0xFu);
        q[1] = static_cast<uint8_t>(s.cb & 0xFFu);
        q[2] = static_cast<uint8_t>((s.y0 >> 8) & 0xFu);
        q[3] = static_cast<uint8_t>(s.y0 & 0xFFu);
        q[4] = static_cast<uint8_t>((s.cr >> 8) & 0xFu);
        q[5] = static_cast<uint8_t>(s.cr & 0xFFu);
        q[6] = static_cast<uint8_t>((s.y1 >> 8) & 0xFu);
        q[7] = static_cast<uint8_t>(s.y1 & 0xFFu);
}

inline void writeUyvy_12LE(uint8_t *q, const Uyvy422Pair &s) {
        q[0] = static_cast<uint8_t>(s.cb & 0xFFu);
        q[1] = static_cast<uint8_t>((s.cb >> 8) & 0xFu);
        q[2] = static_cast<uint8_t>(s.y0 & 0xFFu);
        q[3] = static_cast<uint8_t>((s.y0 >> 8) & 0xFu);
        q[4] = static_cast<uint8_t>(s.cr & 0xFFu);
        q[5] = static_cast<uint8_t>((s.cr >> 8) & 0xFu);
        q[6] = static_cast<uint8_t>(s.y1 & 0xFFu);
        q[7] = static_cast<uint8_t>((s.y1 >> 8) & 0xFu);
}

// -----------------------------------------------------------------------------
// LineFuncPtr kernels — writer side (source memory layout → ST 2110 wire).
// -----------------------------------------------------------------------------

void uyvy10BEtoWire2110_422_10(const void *const *srcPlanes, const size_t * /*srcStrides*/,
                               void *const *dstPlanes, const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pairs = width / 2;
        for (size_t i = 0; i < pairs; i++) writeSt2110_422_10(dst + i * 5, readUyvy_10BE(src + i * 8));
}

void uyvy10LEtoWire2110_422_10(const void *const *srcPlanes, const size_t * /*srcStrides*/,
                               void *const *dstPlanes, const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pairs = width / 2;
        for (size_t i = 0; i < pairs; i++) writeSt2110_422_10(dst + i * 5, readUyvy_10LE(src + i * 8));
}

void uyvy12BEtoWire2110_422_12(const void *const *srcPlanes, const size_t * /*srcStrides*/,
                               void *const *dstPlanes, const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pairs = width / 2;
        for (size_t i = 0; i < pairs; i++) writeSt2110_422_12(dst + i * 6, readUyvy_12BE(src + i * 8));
}

void uyvy12LEtoWire2110_422_12(const void *const *srcPlanes, const size_t * /*srcStrides*/,
                               void *const *dstPlanes, const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pairs = width / 2;
        for (size_t i = 0; i < pairs; i++) writeSt2110_422_12(dst + i * 6, readUyvy_12LE(src + i * 8));
}

// -----------------------------------------------------------------------------
// LineFuncPtr kernels — reader side (ST 2110 wire → source memory layout).
// -----------------------------------------------------------------------------

void wire2110_422_10toUyvy10BE(const void *const *srcPlanes, const size_t * /*srcStrides*/,
                               void *const *dstPlanes, const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pairs = width / 2;
        for (size_t i = 0; i < pairs; i++) writeUyvy_10BE(dst + i * 8, readSt2110_422_10(src + i * 5));
}

void wire2110_422_10toUyvy10LE(const void *const *srcPlanes, const size_t * /*srcStrides*/,
                               void *const *dstPlanes, const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pairs = width / 2;
        for (size_t i = 0; i < pairs; i++) writeUyvy_10LE(dst + i * 8, readSt2110_422_10(src + i * 5));
}

void wire2110_422_12toUyvy12BE(const void *const *srcPlanes, const size_t * /*srcStrides*/,
                               void *const *dstPlanes, const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pairs = width / 2;
        for (size_t i = 0; i < pairs; i++) writeUyvy_12BE(dst + i * 8, readSt2110_422_12(src + i * 6));
}

void wire2110_422_12toUyvy12LE(const void *const *srcPlanes, const size_t * /*srcStrides*/,
                               void *const *dstPlanes, const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pairs = width / 2;
        for (size_t i = 0; i < pairs; i++) writeUyvy_12LE(dst + i * 8, readSt2110_422_12(src + i * 6));
}

// -----------------------------------------------------------------------------
// DPX Method B 10-bit (1 pixel / 4 octets) ↔ ST 2110 4:4:4 10-bit wire
// (4 pixels / 15 octets).
//
// DPX-B is a 32-bit BE word per pixel: [pad(2) | comp2(10) | comp1(10) | comp0(10)]
// with comp0 at bits 9-0, comp1 at 19-10, comp2 at 29-20 (matches
// PackInterleavedImpl's Method B detection at pack-inl.h:140-162).
// -----------------------------------------------------------------------------

struct Rgb444Triplet {
                uint16_t c0;
                uint16_t c1;
                uint16_t c2;
};

inline Rgb444Triplet readDpxB_10(const uint8_t *p) {
        // 32-bit BE word: pad[31:30] | c2[29:20] | c1[19:10] | c0[9:0]
        const uint32_t w =
                (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
        Rgb444Triplet t;
        t.c0 = static_cast<uint16_t>(w & 0x3FFu);
        t.c1 = static_cast<uint16_t>((w >> 10) & 0x3FFu);
        t.c2 = static_cast<uint16_t>((w >> 20) & 0x3FFu);
        return t;
}

inline void writeDpxB_10(uint8_t *q, const Rgb444Triplet &t) {
        const uint32_t w = ((static_cast<uint32_t>(t.c2) & 0x3FFu) << 20) |
                           ((static_cast<uint32_t>(t.c1) & 0x3FFu) << 10) |
                           (static_cast<uint32_t>(t.c0) & 0x3FFu);
        q[0] = static_cast<uint8_t>((w >> 24) & 0xFFu);
        q[1] = static_cast<uint8_t>((w >> 16) & 0xFFu);
        q[2] = static_cast<uint8_t>((w >> 8) & 0xFFu);
        q[3] = static_cast<uint8_t>(w & 0xFFu);
}

// 4:4:4 10-bit wire pgroup: 4 pixels × 3 samples × 10 bits = 120 bits = 15
// octets.  Samples are emitted MSB-first per §6.1.1, ordered c0 c1 c2 per
// pixel, pixels 0..3 sequential.
void dpxB10toWire2110_3x10(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                            const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroups = width / 4;
        // Bit-stream accumulator MSB-first BE.
        for (size_t i = 0; i < pgroups; i++) {
                uint64_t accum = 0;
                int      accumBits = 0;
                uint8_t *out = dst + i * 15;
                size_t   outIdx = 0;
                for (size_t px = 0; px < 4; px++) {
                        Rgb444Triplet t = readDpxB_10(src + (i * 4 + px) * 4);
                        for (int s = 0; s < 3; s++) {
                                uint32_t sample = (s == 0) ? t.c0 : (s == 1 ? t.c1 : t.c2);
                                sample &= 0x3FFu;
                                accum = (accum << 10) | sample;
                                accumBits += 10;
                                while (accumBits >= 8) {
                                        accumBits -= 8;
                                        out[outIdx++] = static_cast<uint8_t>((accum >> accumBits) & 0xFFu);
                                }
                        }
                }
        }
}

void wire2110_3x10toDpxB10(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                            const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroups = width / 4;
        for (size_t i = 0; i < pgroups; i++) {
                const uint8_t *in = src + i * 15;
                uint64_t       accum = 0;
                int            accumBits = 0;
                size_t         inIdx = 0;
                uint16_t       samples[12]; // 4 pixels × 3 samples
                for (int s = 0; s < 12; s++) {
                        while (accumBits < 10) {
                                accum = (accum << 8) | in[inIdx++];
                                accumBits += 8;
                        }
                        accumBits -= 10;
                        samples[s] = static_cast<uint16_t>((accum >> accumBits) & 0x3FFu);
                }
                for (size_t px = 0; px < 4; px++) {
                        Rgb444Triplet t;
                        t.c0 = samples[px * 3 + 0];
                        t.c1 = samples[px * 3 + 1];
                        t.c2 = samples[px * 3 + 2];
                        writeDpxB_10(dst + (i * 4 + px) * 4, t);
                }
        }
}

// -----------------------------------------------------------------------------
// Mono (Key) 10-bit BE ↔ ST 2110 Key 10-bit wire (4 pixels / 5 octets).
// Mono12 BE ↔ wire (2 pixels / 3 octets).
//
// Source layout: 1x10/1x12 BE — each sample is a 16-bit BE word with the
// 10/12-bit value in the low N bits.
// -----------------------------------------------------------------------------

void mono10BEtoWire2110_1x10(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                              const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroups = width / 4;
        for (size_t i = 0; i < pgroups; i++) {
                uint32_t s0 = (static_cast<uint16_t>(src[(i * 4 + 0) * 2]) << 8 |
                              src[(i * 4 + 0) * 2 + 1]) & 0x3FFu;
                uint32_t s1 = (static_cast<uint16_t>(src[(i * 4 + 1) * 2]) << 8 |
                              src[(i * 4 + 1) * 2 + 1]) & 0x3FFu;
                uint32_t s2 = (static_cast<uint16_t>(src[(i * 4 + 2) * 2]) << 8 |
                              src[(i * 4 + 2) * 2 + 1]) & 0x3FFu;
                uint32_t s3 = (static_cast<uint16_t>(src[(i * 4 + 3) * 2]) << 8 |
                              src[(i * 4 + 3) * 2 + 1]) & 0x3FFu;
                uint8_t *q = dst + i * 5;
                q[0] = static_cast<uint8_t>((s0 >> 2) & 0xFFu);
                q[1] = static_cast<uint8_t>(((s0 & 0x3u) << 6) | ((s1 >> 4) & 0x3Fu));
                q[2] = static_cast<uint8_t>(((s1 & 0xFu) << 4) | ((s2 >> 6) & 0xFu));
                q[3] = static_cast<uint8_t>(((s2 & 0x3Fu) << 2) | ((s3 >> 8) & 0x3u));
                q[4] = static_cast<uint8_t>(s3 & 0xFFu);
        }
}

void wire2110_1x10toMono10BE(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                              const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroups = width / 4;
        for (size_t i = 0; i < pgroups; i++) {
                const uint8_t *p = src + i * 5;
                uint16_t       s0 = static_cast<uint16_t>((p[0] << 2) | (p[1] >> 6));
                uint16_t       s1 = static_cast<uint16_t>(((p[1] & 0x3Fu) << 4) | (p[2] >> 4));
                uint16_t       s2 = static_cast<uint16_t>(((p[2] & 0xFu) << 6) | (p[3] >> 2));
                uint16_t       s3 = static_cast<uint16_t>(((p[3] & 0x3u) << 8) | p[4]);
                uint8_t       *q = dst;
                q[(i * 4 + 0) * 2 + 0] = static_cast<uint8_t>((s0 >> 8) & 0x3u);
                q[(i * 4 + 0) * 2 + 1] = static_cast<uint8_t>(s0 & 0xFFu);
                q[(i * 4 + 1) * 2 + 0] = static_cast<uint8_t>((s1 >> 8) & 0x3u);
                q[(i * 4 + 1) * 2 + 1] = static_cast<uint8_t>(s1 & 0xFFu);
                q[(i * 4 + 2) * 2 + 0] = static_cast<uint8_t>((s2 >> 8) & 0x3u);
                q[(i * 4 + 2) * 2 + 1] = static_cast<uint8_t>(s2 & 0xFFu);
                q[(i * 4 + 3) * 2 + 0] = static_cast<uint8_t>((s3 >> 8) & 0x3u);
                q[(i * 4 + 3) * 2 + 1] = static_cast<uint8_t>(s3 & 0xFFu);
        }
}

// Mono 12-bit BE ↔ wire (2 pixels / 3 octets per pgroup).
void mono12BEtoWire2110_1x12(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                              const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroups = width / 2;
        for (size_t i = 0; i < pgroups; i++) {
                uint32_t s0 = (static_cast<uint16_t>(src[(i * 2 + 0) * 2]) << 8 |
                              src[(i * 2 + 0) * 2 + 1]) & 0xFFFu;
                uint32_t s1 = (static_cast<uint16_t>(src[(i * 2 + 1) * 2]) << 8 |
                              src[(i * 2 + 1) * 2 + 1]) & 0xFFFu;
                uint8_t *q = dst + i * 3;
                q[0] = static_cast<uint8_t>((s0 >> 4) & 0xFFu);
                q[1] = static_cast<uint8_t>(((s0 & 0xFu) << 4) | ((s1 >> 8) & 0xFu));
                q[2] = static_cast<uint8_t>(s1 & 0xFFu);
        }
}

void wire2110_1x12toMono12BE(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                              const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroups = width / 2;
        for (size_t i = 0; i < pgroups; i++) {
                const uint8_t *p = src + i * 3;
                uint16_t       s0 = static_cast<uint16_t>((p[0] << 4) | (p[1] >> 4));
                uint16_t       s1 = static_cast<uint16_t>(((p[1] & 0xFu) << 8) | p[2]);
                uint8_t       *q = dst;
                q[(i * 2 + 0) * 2 + 0] = static_cast<uint8_t>((s0 >> 8) & 0xFu);
                q[(i * 2 + 0) * 2 + 1] = static_cast<uint8_t>(s0 & 0xFFu);
                q[(i * 2 + 1) * 2 + 0] = static_cast<uint8_t>((s1 >> 8) & 0xFu);
                q[(i * 2 + 1) * 2 + 1] = static_cast<uint8_t>(s1 & 0xFFu);
        }
}

// -----------------------------------------------------------------------------
// v210 (Apple QuickTime TN2162: 6 pixels / 16 octets LE, 3 × 10-bit per
// 32-bit word + 2 bits padding) → ST 2110 4:2:2 10-bit wire (2 pixels / 5
// octets BE).  Writer-only — reverse is a c-4 Highway target.
//
// v210 word layout (LE):
//   word 0: Cb0 (bits 0-9)  Y0 (10-19)  Cr0 (20-29)  pad (30-31)
//   word 1: Y1  (bits 0-9)  Cb2 (10-19) Y2  (20-29)  pad
//   word 2: Cr2 (bits 0-9)  Y3  (10-19) Cb4 (20-29)  pad
//   word 3: Y4  (bits 0-9)  Cr4 (10-19) Y5  (20-29)  pad
// 6 pixels → 3 wire pgroups (Cb0 Y0 Cr0 Y1 | Cb2 Y2 Cr2 Y3 | Cb4 Y4 Cr4 Y5).
// -----------------------------------------------------------------------------

void v210toWire2110_422_10(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                            const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   blocks = width / 6;
        for (size_t i = 0; i < blocks; i++) {
                const uint8_t *in = src + i * 16;
                auto           readWord = [&](size_t off) -> uint32_t {
                        return static_cast<uint32_t>(in[off]) | (static_cast<uint32_t>(in[off + 1]) << 8) |
                               (static_cast<uint32_t>(in[off + 2]) << 16) | (static_cast<uint32_t>(in[off + 3]) << 24);
                };
                const uint32_t w0 = readWord(0);
                const uint32_t w1 = readWord(4);
                const uint32_t w2 = readWord(8);
                const uint32_t w3 = readWord(12);
                Uyvy422Pair    p0 = {static_cast<uint16_t>(w0 & 0x3FFu),
                                  static_cast<uint16_t>((w0 >> 10) & 0x3FFu),
                                  static_cast<uint16_t>((w0 >> 20) & 0x3FFu),
                                  static_cast<uint16_t>(w1 & 0x3FFu)};
                Uyvy422Pair    p1 = {static_cast<uint16_t>((w1 >> 10) & 0x3FFu),
                                  static_cast<uint16_t>((w1 >> 20) & 0x3FFu),
                                  static_cast<uint16_t>(w2 & 0x3FFu),
                                  static_cast<uint16_t>((w2 >> 10) & 0x3FFu)};
                Uyvy422Pair    p2 = {static_cast<uint16_t>((w2 >> 20) & 0x3FFu),
                                  static_cast<uint16_t>(w3 & 0x3FFu),
                                  static_cast<uint16_t>((w3 >> 10) & 0x3FFu),
                                  static_cast<uint16_t>((w3 >> 20) & 0x3FFu)};
                writeSt2110_422_10(dst + i * 15 + 0, p0);
                writeSt2110_422_10(dst + i * 15 + 5, p1);
                writeSt2110_422_10(dst + i * 15 + 10, p2);
        }
}

// -----------------------------------------------------------------------------
// 4:2:0 wire-format kernels (ST 2110-20 §6.2.5).
//
// Driven by the @c CSCPipeline::execute "destination plane 0 vSub=2"
// row-pair iterator extension — each kernel call consumes 2 source
// image rows (Y row N and Y row N+1, accessed via @c srcStrides[0])
// plus one chroma row, and produces one wire byte row containing
// pgroup-interleaved data.
// -----------------------------------------------------------------------------

// NV12 (SP_420_8) → ST 2110 4:2:0 8-bit wire (I_420_BE_2110_8).
//
// Source planes:
//   plane 0 (Y):    width × full-height, 1 byte per sample
//   plane 1 (CbCr): width × half-height, interleaved Cb,Cr bytes
// Destination plane 0: pgroups of 6 octets / 4 pixels (2×2 block) in
// order Y_top0 Y_top1 Y_bot0 Y_bot1 Cb Cr.
void nv12toWire2110_420_8(const void *const *srcPlanes, const size_t *srcStrides, void *const *dstPlanes,
                          const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *yTop = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *yBot = yTop + srcStrides[0];
        const uint8_t *chroma = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroupsPerRow = width / 2; // 2 pixels per pgroup horizontally.
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                uint8_t *q = dst + i * 6;
                q[0] = yTop[i * 2 + 0];
                q[1] = yTop[i * 2 + 1];
                q[2] = yBot[i * 2 + 0];
                q[3] = yBot[i * 2 + 1];
                q[4] = chroma[i * 2 + 0]; // Cb
                q[5] = chroma[i * 2 + 1]; // Cr
        }
}

void wire2110_420_8toNv12(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                          const size_t *dstStrides, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *yTop = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t       *yBot = yTop + dstStrides[0];
        uint8_t       *chroma = static_cast<uint8_t *>(dstPlanes[1]);
        const size_t   pgroupsPerRow = width / 2;
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                const uint8_t *p = src + i * 6;
                yTop[i * 2 + 0] = p[0];
                yTop[i * 2 + 1] = p[1];
                yBot[i * 2 + 0] = p[2];
                yBot[i * 2 + 1] = p[3];
                chroma[i * 2 + 0] = p[4]; // Cb
                chroma[i * 2 + 1] = p[5]; // Cr
        }
}

// P010 BE (SP_420_10_BE) → ST 2110 4:2:0 10-bit wire (I_420_BE_2110_10).
//
// 8 pixels (4×2 block) per pgroup → 12 samples (8 Y + 2 Cb + 2 Cr)
// in 15 octets BE.  Source uses 16-bit BE words with 10-bit sample
// in the low 10 bits (per @ref SP_420_10_BE).  Sample order on the
// wire is Y_top0..3, Y_bot0..3, Cb0 Cb1 Cr0 Cr1 — wait, §6.2.5
// orders them as one row across before chroma; we follow the §6.2.5
// Figure 4 reference: Y_top0, Y_top1, Y_top2, Y_top3, Y_bot0,
// Y_bot1, Y_bot2, Y_bot3, Cb0, Cr0, Cb1, Cr1 (chroma sample order
// matches the §6.2.5 chroma-position layout where one Cb/Cr pair
// services each 2×2 pixel sub-block).
inline uint16_t readBE10(const uint8_t *p) { return static_cast<uint16_t>(((static_cast<uint16_t>(p[0]) << 8) | p[1]) & 0x3FFu); }
inline uint16_t readLE10(const uint8_t *p) { return static_cast<uint16_t>(((static_cast<uint16_t>(p[1]) << 8) | p[0]) & 0x3FFu); }
inline uint16_t readBE12(const uint8_t *p) { return static_cast<uint16_t>(((static_cast<uint16_t>(p[0]) << 8) | p[1]) & 0xFFFu); }
inline uint16_t readLE12(const uint8_t *p) { return static_cast<uint16_t>(((static_cast<uint16_t>(p[1]) << 8) | p[0]) & 0xFFFu); }
inline void     writeBE10(uint8_t *p, uint16_t v) {
        p[0] = static_cast<uint8_t>((v >> 8) & 0x3u);
        p[1] = static_cast<uint8_t>(v & 0xFFu);
}
inline void writeLE10(uint8_t *p, uint16_t v) {
        p[0] = static_cast<uint8_t>(v & 0xFFu);
        p[1] = static_cast<uint8_t>((v >> 8) & 0x3u);
}
inline void writeBE12(uint8_t *p, uint16_t v) {
        p[0] = static_cast<uint8_t>((v >> 8) & 0xFu);
        p[1] = static_cast<uint8_t>(v & 0xFFu);
}
inline void writeLE12(uint8_t *p, uint16_t v) {
        p[0] = static_cast<uint8_t>(v & 0xFFu);
        p[1] = static_cast<uint8_t>((v >> 8) & 0xFu);
}

// 12-bit 4:2:0 wire pgroup: 6 samples × 12 bits = 72 bits = 9 octets MSB-first BE.
inline void packSt2110_420_12(uint8_t *q, const uint16_t s[6]) {
        // 3 sample pairs, each 12+12=24 bits → 3 octets.
        for (int i = 0; i < 3; i++) {
                const uint32_t a = s[i * 2 + 0] & 0xFFFu;
                const uint32_t b = s[i * 2 + 1] & 0xFFFu;
                q[i * 3 + 0] = static_cast<uint8_t>((a >> 4) & 0xFFu);
                q[i * 3 + 1] = static_cast<uint8_t>(((a & 0xFu) << 4) | ((b >> 8) & 0xFu));
                q[i * 3 + 2] = static_cast<uint8_t>(b & 0xFFu);
        }
}

inline void unpackSt2110_420_12(const uint8_t *q, uint16_t s[6]) {
        for (int i = 0; i < 3; i++) {
                s[i * 2 + 0] = static_cast<uint16_t>((static_cast<uint16_t>(q[i * 3 + 0]) << 4) | (q[i * 3 + 1] >> 4));
                s[i * 2 + 1] = static_cast<uint16_t>(((static_cast<uint16_t>(q[i * 3 + 1]) & 0xFu) << 8) | q[i * 3 + 2]);
        }
}

void p010BEtoWire2110_420_10(const void *const *srcPlanes, const size_t *srcStrides, void *const *dstPlanes,
                              const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *yTop = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *yBot = yTop + srcStrides[0];
        const uint8_t *chroma = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroupsPerRow = width / 4; // 4 pixels per pgroup horizontally.
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                // 12 samples for this pgroup.
                uint16_t s[12];
                // Y top row: 4 samples (BE 16-bit each).
                s[0] = readBE10(yTop + (i * 4 + 0) * 2);
                s[1] = readBE10(yTop + (i * 4 + 1) * 2);
                s[2] = readBE10(yTop + (i * 4 + 2) * 2);
                s[3] = readBE10(yTop + (i * 4 + 3) * 2);
                // Y bottom row: 4 samples.
                s[4] = readBE10(yBot + (i * 4 + 0) * 2);
                s[5] = readBE10(yBot + (i * 4 + 1) * 2);
                s[6] = readBE10(yBot + (i * 4 + 2) * 2);
                s[7] = readBE10(yBot + (i * 4 + 3) * 2);
                // Chroma: 2 Cb + 2 Cr per pgroup (one chroma pair per
                // 2×2 sub-block).  Source stores CbCr interleaved at
                // chroma resolution (W/2 positions × 2 samples = W
                // samples per chroma row).  Pgroup i spans chroma
                // positions 2i and 2i+1.
                s[8]  = readBE10(chroma + (i * 4 + 0) * 2); // Cb0
                s[9]  = readBE10(chroma + (i * 4 + 1) * 2); // Cr0
                s[10] = readBE10(chroma + (i * 4 + 2) * 2); // Cb1
                s[11] = readBE10(chroma + (i * 4 + 3) * 2); // Cr1
                // Bit-pack 12 samples × 10 bits MSB-first → 15 octets.
                uint8_t *q = dst + i * 15;
                uint64_t accum = 0;
                int      accumBits = 0;
                size_t   outIdx = 0;
                for (int j = 0; j < 12; j++) {
                        accum = (accum << 10) | (s[j] & 0x3FFu);
                        accumBits += 10;
                        while (accumBits >= 8) {
                                accumBits -= 8;
                                q[outIdx++] = static_cast<uint8_t>((accum >> accumBits) & 0xFFu);
                        }
                }
        }
}

void wire2110_420_10toP010BE(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                              const size_t *dstStrides, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *yTop = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t       *yBot = yTop + dstStrides[0];
        uint8_t       *chroma = static_cast<uint8_t *>(dstPlanes[1]);
        const size_t   pgroupsPerRow = width / 4;
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                const uint8_t *p = src + i * 15;
                uint64_t       accum = 0;
                int            accumBits = 0;
                size_t         inIdx = 0;
                uint16_t       s[12];
                for (int j = 0; j < 12; j++) {
                        while (accumBits < 10) {
                                accum = (accum << 8) | p[inIdx++];
                                accumBits += 8;
                        }
                        accumBits -= 10;
                        s[j] = static_cast<uint16_t>((accum >> accumBits) & 0x3FFu);
                }
                writeBE10(yTop + (i * 4 + 0) * 2, s[0]);
                writeBE10(yTop + (i * 4 + 1) * 2, s[1]);
                writeBE10(yTop + (i * 4 + 2) * 2, s[2]);
                writeBE10(yTop + (i * 4 + 3) * 2, s[3]);
                writeBE10(yBot + (i * 4 + 0) * 2, s[4]);
                writeBE10(yBot + (i * 4 + 1) * 2, s[5]);
                writeBE10(yBot + (i * 4 + 2) * 2, s[6]);
                writeBE10(yBot + (i * 4 + 3) * 2, s[7]);
                writeBE10(chroma + (i * 4 + 0) * 2, s[8]);
                writeBE10(chroma + (i * 4 + 1) * 2, s[9]);
                writeBE10(chroma + (i * 4 + 2) * 2, s[10]);
                writeBE10(chroma + (i * 4 + 3) * 2, s[11]);
        }
}

// P010 LE (SP_420_10_LE) ↔ ST 2110 4:2:0 10-bit wire.  Same packing as the
// BE variant — only the 16-bit word byte order on the source/destination
// changes.
void p010LEtoWire2110_420_10(const void *const *srcPlanes, const size_t *srcStrides, void *const *dstPlanes,
                              const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *yTop = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *yBot = yTop + srcStrides[0];
        const uint8_t *chroma = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroupsPerRow = width / 4;
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                uint16_t s[12];
                s[0] = readLE10(yTop + (i * 4 + 0) * 2);
                s[1] = readLE10(yTop + (i * 4 + 1) * 2);
                s[2] = readLE10(yTop + (i * 4 + 2) * 2);
                s[3] = readLE10(yTop + (i * 4 + 3) * 2);
                s[4] = readLE10(yBot + (i * 4 + 0) * 2);
                s[5] = readLE10(yBot + (i * 4 + 1) * 2);
                s[6] = readLE10(yBot + (i * 4 + 2) * 2);
                s[7] = readLE10(yBot + (i * 4 + 3) * 2);
                s[8]  = readLE10(chroma + (i * 4 + 0) * 2);
                s[9]  = readLE10(chroma + (i * 4 + 1) * 2);
                s[10] = readLE10(chroma + (i * 4 + 2) * 2);
                s[11] = readLE10(chroma + (i * 4 + 3) * 2);
                uint8_t *q = dst + i * 15;
                uint64_t accum = 0;
                int      accumBits = 0;
                size_t   outIdx = 0;
                for (int j = 0; j < 12; j++) {
                        accum = (accum << 10) | (s[j] & 0x3FFu);
                        accumBits += 10;
                        while (accumBits >= 8) {
                                accumBits -= 8;
                                q[outIdx++] = static_cast<uint8_t>((accum >> accumBits) & 0xFFu);
                        }
                }
        }
}

void wire2110_420_10toP010LE(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                              const size_t *dstStrides, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *yTop = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t       *yBot = yTop + dstStrides[0];
        uint8_t       *chroma = static_cast<uint8_t *>(dstPlanes[1]);
        const size_t   pgroupsPerRow = width / 4;
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                const uint8_t *p = src + i * 15;
                uint64_t       accum = 0;
                int            accumBits = 0;
                size_t         inIdx = 0;
                uint16_t       s[12];
                for (int j = 0; j < 12; j++) {
                        while (accumBits < 10) {
                                accum = (accum << 8) | p[inIdx++];
                                accumBits += 8;
                        }
                        accumBits -= 10;
                        s[j] = static_cast<uint16_t>((accum >> accumBits) & 0x3FFu);
                }
                writeLE10(yTop + (i * 4 + 0) * 2, s[0]);
                writeLE10(yTop + (i * 4 + 1) * 2, s[1]);
                writeLE10(yTop + (i * 4 + 2) * 2, s[2]);
                writeLE10(yTop + (i * 4 + 3) * 2, s[3]);
                writeLE10(yBot + (i * 4 + 0) * 2, s[4]);
                writeLE10(yBot + (i * 4 + 1) * 2, s[5]);
                writeLE10(yBot + (i * 4 + 2) * 2, s[6]);
                writeLE10(yBot + (i * 4 + 3) * 2, s[7]);
                writeLE10(chroma + (i * 4 + 0) * 2, s[8]);
                writeLE10(chroma + (i * 4 + 1) * 2, s[9]);
                writeLE10(chroma + (i * 4 + 2) * 2, s[10]);
                writeLE10(chroma + (i * 4 + 3) * 2, s[11]);
        }
}

// 12-bit 4:2:0 ↔ wire.  Layout `I_420_BE_2110_12`: pixelsPerBlock=2,
// bytesPerBlock=9, vSubsampling=2 — one pgroup spans a 2×2 pixel block
// (4 Y + 1 Cb + 1 Cr = 6 samples × 12 bits = 72 bits = 9 octets).  Sample
// order on the wire per §6.2.5: Y_top0, Y_top1, Y_bot0, Y_bot1, Cb, Cr.
//
// Source planes (P012 BE/LE — `SP_420_12_BE/LE`):
//   plane 0 (Y):    width × full-height, 16-bit BE/LE words, value in low 12 bits.
//   plane 1 (CbCr): width × half-height, interleaved Cb,Cr 16-bit words.
void sp420_12BEtoWire2110_420_12(const void *const *srcPlanes, const size_t *srcStrides, void *const *dstPlanes,
                                  const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *yTop = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *yBot = yTop + srcStrides[0];
        const uint8_t *chroma = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroupsPerRow = width / 2;
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                uint16_t s[6];
                s[0] = readBE12(yTop + (i * 2 + 0) * 2);
                s[1] = readBE12(yTop + (i * 2 + 1) * 2);
                s[2] = readBE12(yBot + (i * 2 + 0) * 2);
                s[3] = readBE12(yBot + (i * 2 + 1) * 2);
                // One chroma position per pgroup: Cb / Cr at byte offset (i*2)*2 / (i*2+1)*2.
                s[4] = readBE12(chroma + (i * 2 + 0) * 2);
                s[5] = readBE12(chroma + (i * 2 + 1) * 2);
                packSt2110_420_12(dst + i * 9, s);
        }
}

void wire2110_420_12toSp420_12BE(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                                  const size_t *dstStrides, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *yTop = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t       *yBot = yTop + dstStrides[0];
        uint8_t       *chroma = static_cast<uint8_t *>(dstPlanes[1]);
        const size_t   pgroupsPerRow = width / 2;
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                uint16_t s[6];
                unpackSt2110_420_12(src + i * 9, s);
                writeBE12(yTop + (i * 2 + 0) * 2, s[0]);
                writeBE12(yTop + (i * 2 + 1) * 2, s[1]);
                writeBE12(yBot + (i * 2 + 0) * 2, s[2]);
                writeBE12(yBot + (i * 2 + 1) * 2, s[3]);
                writeBE12(chroma + (i * 2 + 0) * 2, s[4]);
                writeBE12(chroma + (i * 2 + 1) * 2, s[5]);
        }
}

void sp420_12LEtoWire2110_420_12(const void *const *srcPlanes, const size_t *srcStrides, void *const *dstPlanes,
                                  const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *yTop = static_cast<const uint8_t *>(srcPlanes[0]);
        const uint8_t *yBot = yTop + srcStrides[0];
        const uint8_t *chroma = static_cast<const uint8_t *>(srcPlanes[1]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   pgroupsPerRow = width / 2;
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                uint16_t s[6];
                s[0] = readLE12(yTop + (i * 2 + 0) * 2);
                s[1] = readLE12(yTop + (i * 2 + 1) * 2);
                s[2] = readLE12(yBot + (i * 2 + 0) * 2);
                s[3] = readLE12(yBot + (i * 2 + 1) * 2);
                s[4] = readLE12(chroma + (i * 2 + 0) * 2);
                s[5] = readLE12(chroma + (i * 2 + 1) * 2);
                packSt2110_420_12(dst + i * 9, s);
        }
}

void wire2110_420_12toSp420_12LE(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                                  const size_t *dstStrides, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *yTop = static_cast<uint8_t *>(dstPlanes[0]);
        uint8_t       *yBot = yTop + dstStrides[0];
        uint8_t       *chroma = static_cast<uint8_t *>(dstPlanes[1]);
        const size_t   pgroupsPerRow = width / 2;
        for (size_t i = 0; i < pgroupsPerRow; i++) {
                uint16_t s[6];
                unpackSt2110_420_12(src + i * 9, s);
                writeLE12(yTop + (i * 2 + 0) * 2, s[0]);
                writeLE12(yTop + (i * 2 + 1) * 2, s[1]);
                writeLE12(yBot + (i * 2 + 0) * 2, s[2]);
                writeLE12(yBot + (i * 2 + 1) * 2, s[3]);
                writeLE12(chroma + (i * 2 + 0) * 2, s[4]);
                writeLE12(chroma + (i * 2 + 1) * 2, s[5]);
        }
}

// -----------------------------------------------------------------------------
// ST 2110 4:2:2 10-bit wire → v210 (Apple TN2162).  Reverse of
// @ref v210toWire2110_422_10 — read 3 wire pgroups (15 octets, 6 pixels),
// write 4 LE 32-bit v210 words (16 octets).  Padding within the v210 line
// stride (128-byte aligned per @ref v210LineStride) is the caller's
// responsibility; this kernel writes exactly the v210 active words.
// -----------------------------------------------------------------------------

void wire2110_422_10toV210(const void *const *srcPlanes, const size_t * /*srcStrides*/, void *const *dstPlanes,
                            const size_t * /*dstStrides*/, size_t width, CSCContext &) {
        const uint8_t *src = static_cast<const uint8_t *>(srcPlanes[0]);
        uint8_t       *dst = static_cast<uint8_t *>(dstPlanes[0]);
        const size_t   blocks = width / 6;
        auto writeWord = [&](uint8_t *p, uint32_t w) {
                p[0] = static_cast<uint8_t>(w & 0xFFu);
                p[1] = static_cast<uint8_t>((w >> 8) & 0xFFu);
                p[2] = static_cast<uint8_t>((w >> 16) & 0xFFu);
                p[3] = static_cast<uint8_t>((w >> 24) & 0xFFu);
        };
        for (size_t i = 0; i < blocks; i++) {
                const Uyvy422Pair p0 = readSt2110_422_10(src + i * 15 + 0);
                const Uyvy422Pair p1 = readSt2110_422_10(src + i * 15 + 5);
                const Uyvy422Pair p2 = readSt2110_422_10(src + i * 15 + 10);
                // v210 word layout (LE, low 30 bits + 2 bits pad):
                //   word 0: Cb0 (bits 0-9)  Y0 (10-19)  Cr0 (20-29)
                //   word 1: Y1  (bits 0-9)  Cb2 (10-19) Y2  (20-29)
                //   word 2: Cr2 (bits 0-9)  Y3  (10-19) Cb4 (20-29)
                //   word 3: Y4  (bits 0-9)  Cr4 (10-19) Y5  (20-29)
                const uint32_t w0 = (static_cast<uint32_t>(p0.cb) & 0x3FFu) |
                                    ((static_cast<uint32_t>(p0.y0) & 0x3FFu) << 10) |
                                    ((static_cast<uint32_t>(p0.cr) & 0x3FFu) << 20);
                const uint32_t w1 = (static_cast<uint32_t>(p0.y1) & 0x3FFu) |
                                    ((static_cast<uint32_t>(p1.cb) & 0x3FFu) << 10) |
                                    ((static_cast<uint32_t>(p1.y0) & 0x3FFu) << 20);
                const uint32_t w2 = (static_cast<uint32_t>(p1.cr) & 0x3FFu) |
                                    ((static_cast<uint32_t>(p1.y1) & 0x3FFu) << 10) |
                                    ((static_cast<uint32_t>(p2.cb) & 0x3FFu) << 20);
                const uint32_t w3 = (static_cast<uint32_t>(p2.y0) & 0x3FFu) |
                                    ((static_cast<uint32_t>(p2.cr) & 0x3FFu) << 10) |
                                    ((static_cast<uint32_t>(p2.y1) & 0x3FFu) << 20);
                writeWord(dst + i * 16 + 0, w0);
                writeWord(dst + i * 16 + 4, w1);
                writeWord(dst + i * 16 + 8, w2);
                writeWord(dst + i * 16 + 12, w3);
        }
}

// -----------------------------------------------------------------------------
// Static registration at init time.
// -----------------------------------------------------------------------------

struct St2110Registrar {
                St2110Registrar() {
                        auto reg = CSCRegistry::registerFastPath;
                        // 4:2:2 10-bit — writer side (UYVY 10 → ST 2110 wire).
                        reg(PixelFormat::YUV10_422_UYVY_BE_Rec709, PixelFormat::YUV10_422_2110_Rec709,
                            uyvy10BEtoWire2110_422_10);
                        reg(PixelFormat::YUV10_422_UYVY_LE_Rec709, PixelFormat::YUV10_422_2110_Rec709,
                            uyvy10LEtoWire2110_422_10);
                        // 4:2:2 12-bit — writer side (UYVY 12 → ST 2110 wire).
                        reg(PixelFormat::YUV12_422_UYVY_BE_Rec709, PixelFormat::YUV12_422_2110_Rec709,
                            uyvy12BEtoWire2110_422_12);
                        reg(PixelFormat::YUV12_422_UYVY_LE_Rec709, PixelFormat::YUV12_422_2110_Rec709,
                            uyvy12LEtoWire2110_422_12);
                        // 4:2:2 10-bit — reader side (ST 2110 wire → UYVY 10).
                        reg(PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_UYVY_BE_Rec709,
                            wire2110_422_10toUyvy10BE);
                        reg(PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_UYVY_LE_Rec709,
                            wire2110_422_10toUyvy10LE);
                        // 4:2:2 12-bit — reader side (ST 2110 wire → UYVY 12).
                        reg(PixelFormat::YUV12_422_2110_Rec709, PixelFormat::YUV12_422_UYVY_BE_Rec709,
                            wire2110_422_12toUyvy12BE);
                        reg(PixelFormat::YUV12_422_2110_Rec709, PixelFormat::YUV12_422_UYVY_LE_Rec709,
                            wire2110_422_12toUyvy12LE);

                        // 4:4:4 10-bit — DPX Method B (1 pixel / 4 octets) ↔ ST 2110 wire (4 pixels / 15 octets).
                        // The byte-level conversion is colour-space-agnostic, so the same kernel covers both
                        // YUV (Rec.709 ColorModel) and the RGB DPX-B layout when registered as a RGB → wire
                        // pair (none exists yet; lands when RGB DPX-B PixelFormat is registered).
                        reg(PixelFormat::YUV10_DPX_B_Rec709, PixelFormat::YUV10_2110_Rec709,
                            dpxB10toWire2110_3x10);
                        reg(PixelFormat::YUV10_2110_Rec709, PixelFormat::YUV10_DPX_B_Rec709,
                            wire2110_3x10toDpxB10);

                        // Key 10/12-bit (BE source only) ↔ wire.
                        reg(PixelFormat::Mono10_BE_sRGB, PixelFormat::Mono10_BE_2110_sRGB,
                            mono10BEtoWire2110_1x10);
                        reg(PixelFormat::Mono10_BE_2110_sRGB, PixelFormat::Mono10_BE_sRGB,
                            wire2110_1x10toMono10BE);
                        reg(PixelFormat::Mono12_BE_sRGB, PixelFormat::Mono12_BE_2110_sRGB,
                            mono12BEtoWire2110_1x12);
                        reg(PixelFormat::Mono12_BE_2110_sRGB, PixelFormat::Mono12_BE_sRGB,
                            wire2110_1x12toMono12BE);

                        // v210 ↔ wire (4:2:2 10-bit broadcast lingua franca).
                        reg(PixelFormat::YUV10_422_v210_Rec709, PixelFormat::YUV10_422_2110_Rec709,
                            v210toWire2110_422_10);
                        reg(PixelFormat::YUV10_422_2110_Rec709, PixelFormat::YUV10_422_v210_Rec709,
                            wire2110_422_10toV210);

                        // 4:2:0 8-bit ↔ wire (NV12 ↔ ST 2110 4:2:0 8-bit pgroup-interleaved).
                        // Driven by the @c CSCPipeline destination-vSub=2
                        // row-pair iterator extension (cscpipeline.cpp).
                        reg(PixelFormat::YUV8_420_SemiPlanar_Rec709, PixelFormat::YUV8_420_2110_Rec709,
                            nv12toWire2110_420_8);
                        reg(PixelFormat::YUV8_420_2110_Rec709, PixelFormat::YUV8_420_SemiPlanar_Rec709,
                            wire2110_420_8toNv12);
                        // 4:2:0 10-bit ↔ wire (P010 BE/LE ↔ ST 2110 4:2:0 10-bit).
                        reg(PixelFormat::YUV10_420_SemiPlanar_BE_Rec709, PixelFormat::YUV10_420_2110_Rec709,
                            p010BEtoWire2110_420_10);
                        reg(PixelFormat::YUV10_420_2110_Rec709, PixelFormat::YUV10_420_SemiPlanar_BE_Rec709,
                            wire2110_420_10toP010BE);
                        reg(PixelFormat::YUV10_420_SemiPlanar_LE_Rec709, PixelFormat::YUV10_420_2110_Rec709,
                            p010LEtoWire2110_420_10);
                        reg(PixelFormat::YUV10_420_2110_Rec709, PixelFormat::YUV10_420_SemiPlanar_LE_Rec709,
                            wire2110_420_10toP010LE);
                        // 4:2:0 12-bit ↔ wire (P012 BE/LE ↔ ST 2110 4:2:0 12-bit).
                        reg(PixelFormat::YUV12_420_SemiPlanar_BE_Rec709, PixelFormat::YUV12_420_2110_Rec709,
                            sp420_12BEtoWire2110_420_12);
                        reg(PixelFormat::YUV12_420_2110_Rec709, PixelFormat::YUV12_420_SemiPlanar_BE_Rec709,
                            wire2110_420_12toSp420_12BE);
                        reg(PixelFormat::YUV12_420_SemiPlanar_LE_Rec709, PixelFormat::YUV12_420_2110_Rec709,
                            sp420_12LEtoWire2110_420_12);
                        reg(PixelFormat::YUV12_420_2110_Rec709, PixelFormat::YUV12_420_SemiPlanar_LE_Rec709,
                            wire2110_420_12toSp420_12LE);
                }
};

[[maybe_unused]] static St2110Registrar gSt2110Registrar;

} // namespace

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CSC
