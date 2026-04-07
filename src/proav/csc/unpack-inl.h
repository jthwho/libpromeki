/**
 * @file      unpack-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Highway SIMD implementation of pixel unpack kernels.
 * Re-included per target via foreach_target.h.
 */

#if defined(PROMEKI_CSC_UNPACK_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_UNPACK_INL_H_
#undef PROMEKI_CSC_UNPACK_INL_H_
#else
#define PROMEKI_CSC_UNPACK_INL_H_
#endif

#include "hwy/highway.h"
#include <cstdint>
#include <cstring>

HWY_BEFORE_NAMESPACE();
namespace promeki {
namespace csc {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// Unpack interleaved RGBA (4 components, 8 bits, 1 pixel per block)
static void unpackI4x8(const uint8_t *src, float *const *buffers, size_t width) {
        const hn::ScalableTag<float> df;
        const hn::ScalableTag<uint8_t> du8;
        const size_t Nf = hn::Lanes(df);
        // Number of u8 lanes is 4x the float lanes (u8 is 4x narrower)
        const size_t N8 = hn::Lanes(du8);
        // We process Nf pixels at a time (each pixel = 4 bytes)
        // But LoadInterleaved4 loads N8 elements per channel,
        // and we need to promote u8->float in chunks of Nf.
        // Use a quarter-width u8 tag that matches float lane count.
        const hn::Rebind<uint8_t, decltype(df)> du8q;    // Nf u8 lanes
        const hn::Rebind<uint16_t, decltype(df)> du16q;  // Nf/2 u16 lanes -- wrong
        // Actually: promote chain is u8[Nf] -> u16[Nf] (via PromoteTo with wider tag)
        // Highway PromoteTo: Rebind<u16, df> has Nf lanes of u16
        const hn::Rebind<int32_t, decltype(df)> di32;

        size_t x = 0;
        for(; x + Nf <= width; x += Nf) {
                // Load Nf pixels worth of RGBA data (4*Nf bytes)
                // Use quarter-width u8 tag to load Nf u8 values per component
                hn::Vec<decltype(du8q)> r8, g8, b8, a8;
                hn::LoadInterleaved4(du8q, src + x * 4, r8, g8, b8, a8);

                // Promote u8 -> i32 -> float
                auto ri = hn::PromoteTo(di32, r8);
                auto gi = hn::PromoteTo(di32, g8);
                auto bi = hn::PromoteTo(di32, b8);
                auto ai = hn::PromoteTo(di32, a8);

                hn::StoreU(hn::ConvertTo(df, ri), df, buffers[0] + x);
                hn::StoreU(hn::ConvertTo(df, gi), df, buffers[1] + x);
                hn::StoreU(hn::ConvertTo(df, bi), df, buffers[2] + x);
                hn::StoreU(hn::ConvertTo(df, ai), df, buffers[3] + x);
        }
        // Scalar tail
        for(; x < width; x++) {
                buffers[0][x] = static_cast<float>(src[x * 4 + 0]);
                buffers[1][x] = static_cast<float>(src[x * 4 + 1]);
                buffers[2][x] = static_cast<float>(src[x * 4 + 2]);
                buffers[3][x] = static_cast<float>(src[x * 4 + 3]);
        }
        return;
}

// Unpack interleaved RGB (3 components, 8 bits, 1 pixel per block)
static void unpackI3x8(const uint8_t *src, float *const *buffers, size_t width) {
        const hn::ScalableTag<float> df;
        const hn::Rebind<uint8_t, decltype(df)> du8q;
        const hn::Rebind<int32_t, decltype(df)> di32;
        const size_t Nf = hn::Lanes(df);

        size_t x = 0;
        for(; x + Nf <= width; x += Nf) {
                hn::Vec<decltype(du8q)> r8, g8, b8;
                hn::LoadInterleaved3(du8q, src + x * 3, r8, g8, b8);

                hn::StoreU(hn::ConvertTo(df, hn::PromoteTo(di32, r8)), df, buffers[0] + x);
                hn::StoreU(hn::ConvertTo(df, hn::PromoteTo(di32, g8)), df, buffers[1] + x);
                hn::StoreU(hn::ConvertTo(df, hn::PromoteTo(di32, b8)), df, buffers[2] + x);
        }
        for(; x < width; x++) {
                buffers[0][x] = static_cast<float>(src[x * 3 + 0]);
                buffers[1][x] = static_cast<float>(src[x * 3 + 1]);
                buffers[2][x] = static_cast<float>(src[x * 3 + 2]);
        }
        return;
}

// General interleaved unpack dispatch -- uses SIMD for common 8-bit formats,
// falls back to scalar for everything else.
void UnpackInterleavedImpl(const void *src, float *const *buffers,
                           size_t width, int compCount, int bitsPerComp,
                           int bytesPerBlock, int pixelsPerBlock,
                           const int *compByteOffset, const int *compBits,
                           bool hasAlpha, int alphaCompIndex, bool useSimd) {
        const uint8_t *p = static_cast<const uint8_t *>(src);

        // Fast path: 4-component 8-bit interleaved (RGBA8, BGRA8, ARGB8, etc.)
        // Note: the caller (kernelUnpackInterleaved) already applies semBufMap
        // to route each component to the correct semantic buffer, so no
        // additional alpha remapping is needed here.
        if(useSimd && pixelsPerBlock == 1 && compCount == 4 && bitsPerComp == 8 &&
           bytesPerBlock == 4 &&
           compByteOffset[0] == 0 && compByteOffset[1] == 1 &&
           compByteOffset[2] == 2 && compByteOffset[3] == 3) {
                unpackI4x8(p, buffers, width);
                return;
        }

        // Fast path: 3-component 8-bit interleaved (RGB8, BGR8)
        if(useSimd && pixelsPerBlock == 1 && compCount == 3 && bitsPerComp == 8 &&
           bytesPerBlock == 3 &&
           compByteOffset[0] == 0 && compByteOffset[1] == 1 &&
           compByteOffset[2] == 2) {
                unpackI3x8(p, buffers, width);
                return;
        }

        // DPX 10-bit packed: 3 x 10-bit in a 32-bit word (big-endian byte order).
        // Layout: R[9:0] at bits 31-22, G[9:0] at bits 21-12, B[9:0] at bits 11-2, pad at 1-0.
        if(pixelsPerBlock == 1 && compCount == 3 && bitsPerComp == 10 && bytesPerBlock == 4) {
                for(size_t x = 0; x < width; x++) {
                        const uint8_t *px = p + x * 4;
                        // Read as big-endian 32-bit word
                        uint32_t word = (static_cast<uint32_t>(px[0]) << 24)
                                      | (static_cast<uint32_t>(px[1]) << 16)
                                      | (static_cast<uint32_t>(px[2]) <<  8)
                                      |  static_cast<uint32_t>(px[3]);
                        buffers[0][x] = static_cast<float>((word >> 22) & 0x3FF);
                        buffers[1][x] = static_cast<float>((word >> 12) & 0x3FF);
                        buffers[2][x] = static_cast<float>((word >>  2) & 0x3FF);
                }
                return;
        }

        // Scalar fallback for all other formats
        if(pixelsPerBlock == 1) {
                for(size_t x = 0; x < width; x++) {
                        const uint8_t *pixel = p + x * bytesPerBlock;
                        for(int c = 0; c < compCount; c++) {
                                float val;
                                if(compBits[c] <= 8) {
                                        val = static_cast<float>(pixel[compByteOffset[c]]);
                                } else if(compBits[c] <= 16) {
                                        uint16_t v;
                                        std::memcpy(&v, pixel + compByteOffset[c], sizeof(v));
                                        val = static_cast<float>(v);
                                } else {
                                        val = 0.0f;
                                }
                                buffers[c][x] = val;
                        }
                }
        } else {
                // Multi-pixel block (YUYV / UYVY, 8- or 10-bit) — scalar.
                //
                // Each block carries @c pixelsPerBlock luma samples
                // and one chroma pair, laid out so that Y1 is a fixed
                // distance (one "luma slot") past Y0.  For a 4:2:2
                // block with two luma samples and one Cb+Cr pair,
                // that slot is exactly @c bytesPerBlock /
                // @c pixelsPerBlock bytes — 2 bytes for 8-bit YUYV
                // and 4 bytes for 10-bit YUYV (each luma sample is
                // stored as a 16-bit LE word at 10 bits).  The old
                // hardcoded @c px*2 only worked for 8-bit.
                //
                // Similarly, each component read must use the right
                // sample width: 1 byte for 8-bit, 2 bytes for 10/16.
                // The previous code always read a single byte, so for
                // 10-bit 4:2:2 only the low 8 bits of each component
                // survived — producing near-zero chroma and a flat,
                // very dark luma channel (the "green with vague bars"
                // failure mode).
                auto readSample = [](const uint8_t *base, int byteOffset,
                                     int bits) -> float {
                        if(bits <= 8) {
                                return static_cast<float>(base[byteOffset]);
                        }
                        uint16_t v;
                        std::memcpy(&v, base + byteOffset, sizeof(v));
                        return static_cast<float>(v);
                };
                const int lumaSlot = bytesPerBlock / pixelsPerBlock;
                size_t blockCount = (width + pixelsPerBlock - 1) / pixelsPerBlock;
                for(size_t b = 0; b < blockCount; b++) {
                        const uint8_t *block = p + b * bytesPerBlock;
                        for(int px = 0; px < pixelsPerBlock; px++) {
                                size_t x = b * pixelsPerBlock + px;
                                if(x >= width) break;
                                int lumaOffset = compByteOffset[0] + px * lumaSlot;
                                buffers[0][x] = readSample(block, lumaOffset, compBits[0]);
                                buffers[1][x] = readSample(block, compByteOffset[1], compBits[1]);
                                buffers[2][x] = readSample(block, compByteOffset[2], compBits[2]);
                        }
                }
        }
        return;
}

// SIMD unpack for a single full-resolution 8-bit plane -> float buffer
static void unpackPlane8(const uint8_t *plane, float *buf, size_t width) {
        const hn::ScalableTag<float> df;
        const hn::Rebind<uint8_t, decltype(df)> du8q;
        const hn::Rebind<int32_t, decltype(df)> di32;
        const size_t Nf = hn::Lanes(df);

        size_t x = 0;
        for(; x + Nf <= width; x += Nf) {
                auto v8 = hn::LoadU(du8q, plane + x);
                auto vi = hn::PromoteTo(di32, v8);
                hn::StoreU(hn::ConvertTo(df, vi), df, buf + x);
        }
        for(; x < width; x++) {
                buf[x] = static_cast<float>(plane[x]);
        }
        return;
}

void UnpackPlanarImpl(const void *const *planes, const size_t *strides,
                      float *const *buffers, size_t width,
                      int compCount, int bitsPerComp,
                      const int *compPlane, const int *planeHSub,
                      const int *planeBytesPerSample,
                      bool hasAlpha, int alphaCompIndex, bool useSimd) {
        for(int c = 0; c < compCount; c++) {
                int plane = compPlane[c];
                int hSub = planeHSub[plane];
                int bps = planeBytesPerSample[plane];
                const uint8_t *p = static_cast<const uint8_t *>(planes[plane]);
                int bufIdx = (c == alphaCompIndex) ? 3 :
                             ((c < alphaCompIndex || alphaCompIndex < 0) ? c : c - 1);

                // SIMD fast path: full-resolution 8-bit plane
                if(useSimd && hSub == 1 && bps == 1) {
                        unpackPlane8(p, buffers[bufIdx], width);
                        continue;
                }

                // Scalar fallback for subsampled or >8-bit planes
                size_t planeWidth = (width + hSub - 1) / hSub;
                for(size_t x = 0; x < width; x++) {
                        size_t sx = x / hSub;
                        if(sx >= planeWidth) sx = planeWidth - 1;
                        if(bps == 1) {
                                buffers[bufIdx][x] = static_cast<float>(p[sx]);
                        } else if(bps == 2) {
                                uint16_t val;
                                std::memcpy(&val, p + sx * 2, sizeof(val));
                                buffers[bufIdx][x] = static_cast<float>(val);
                        }
                }
        }
        return;
}

void UnpackSemiPlanarImpl(const void *const *planes, const size_t *strides,
                          float *const *buffers, size_t width,
                          int bitsPerComp, const int *planeHSub,
                          const int *planeBytesPerSample,
                          bool hasAlpha, int alphaCompIndex,
                          const int *compByteOffset, bool useSimd) {
        const uint8_t *lumaPlane = static_cast<const uint8_t *>(planes[0]);
        const uint8_t *chromaPlane = static_cast<const uint8_t *>(planes[1]);
        int chromaHSub = planeHSub[1];
        int sampleBytes = (bitsPerComp <= 8) ? 1 : 2;
        size_t chromaWidth = (width + chromaHSub - 1) / chromaHSub;

        // Determine Cb/Cr byte offsets within the interleaved chroma plane
        // (handles NV12 CbCr vs NV21 CrCb ordering)
        int cbOffset = compByteOffset[1];  // comp 1 = Cb
        int crOffset = compByteOffset[2];  // comp 2 = Cr

        // SIMD fast path: 8-bit luma
        if(useSimd && sampleBytes == 1) {
                unpackPlane8(lumaPlane, buffers[0], width);

                // 8-bit semi-planar chroma: LoadInterleaved2 for CbCr pairs
                const hn::ScalableTag<float> df;
                const hn::Rebind<uint8_t, decltype(df)> du8q;
                const hn::Rebind<int32_t, decltype(df)> di32;
                const size_t Nf = hn::Lanes(df);

                // Process chroma at subsampled rate, then replicate
                size_t cx = 0;
                for(; cx + Nf <= chromaWidth; cx += Nf) {
                        hn::Vec<decltype(du8q)> a8, b8;
                        hn::LoadInterleaved2(du8q, chromaPlane + cx * 2, a8, b8);
                        auto af = hn::ConvertTo(df, hn::PromoteTo(di32, a8));
                        auto bf = hn::ConvertTo(df, hn::PromoteTo(di32, b8));

                        // a8 is byte offset 0, b8 is byte offset 1
                        // Map to Cb/Cr based on compByteOffset
                        auto cbf = (cbOffset == 0) ? af : bf;
                        auto crf = (crOffset == 0) ? af : bf;

                        // Replicate to full width (write each value chromaHSub times)
                        for(int rep = 0; rep < chromaHSub; rep++) {
                                size_t fx = cx * chromaHSub + rep;
                                if(fx + Nf <= width) {
                                        // Can't easily scatter individual replicated values with SIMD,
                                        // so store to chroma position and replicate below
                                }
                        }

                        // For now, just store at chroma resolution; replication
                        // handled by scalar loop below
                        hn::StoreU(cbf, df, buffers[1] + cx);
                        hn::StoreU(crf, df, buffers[2] + cx);
                }
                // Scalar tail for remaining chroma samples
                for(; cx < chromaWidth; cx++) {
                        buffers[1][cx] = static_cast<float>(chromaPlane[cx * 2 + cbOffset]);
                        buffers[2][cx] = static_cast<float>(chromaPlane[cx * 2 + crOffset]);
                }

                // Replicate chroma from chromaWidth to full width (backwards to avoid overwriting)
                if(chromaHSub > 1) {
                        for(size_t x = width; x > 0; ) {
                                --x;
                                size_t sx = x / chromaHSub;
                                if(sx >= chromaWidth) sx = chromaWidth - 1;
                                buffers[1][x] = buffers[1][sx];
                                buffers[2][x] = buffers[2][sx];
                        }
                }
                return;
        }

        // Scalar fallback
        for(size_t x = 0; x < width; x++) {
                if(sampleBytes == 1) {
                        buffers[0][x] = static_cast<float>(lumaPlane[x]);
                } else {
                        uint16_t val;
                        std::memcpy(&val, lumaPlane + x * 2, sizeof(val));
                        buffers[0][x] = static_cast<float>(val);
                }

                size_t cx = x / chromaHSub;
                if(cx >= chromaWidth) cx = chromaWidth - 1;
                if(sampleBytes == 1) {
                        buffers[1][x] = static_cast<float>(chromaPlane[cx * 2 + cbOffset]);
                        buffers[2][x] = static_cast<float>(chromaPlane[cx * 2 + crOffset]);
                } else {
                        uint16_t cb, cr;
                        std::memcpy(&cb, chromaPlane + cx * 4 + cbOffset, sizeof(cb));
                        std::memcpy(&cr, chromaPlane + cx * 4 + crOffset, sizeof(cr));
                        buffers[1][x] = static_cast<float>(cb);
                        buffers[2][x] = static_cast<float>(cr);
                }
        }
        return;
}

}  // namespace HWY_NAMESPACE
}  // namespace csc
}  // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
