/**
 * @file      pack-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Highway SIMD implementation of pixel pack kernels.
 * Re-included per target via foreach_target.h.
 */

#if defined(PROMEKI_CSC_PACK_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_PACK_INL_H_
#undef PROMEKI_CSC_PACK_INL_H_
#else
#define PROMEKI_CSC_PACK_INL_H_
#endif

#include "hwy/highway.h"
#include <cstdint>
#include <cstring>

HWY_BEFORE_NAMESPACE();
namespace promeki {
namespace csc {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// Pack float SoA to interleaved RGBA8 (4 components, 8 bits)
static void packI4x8(const float *const *buffers, uint8_t *dst, size_t width) {
        const hn::ScalableTag<float> df;
        const hn::Rebind<int32_t, decltype(df)> di32;
        const hn::Rebind<uint8_t, decltype(df)> du8q;
        const size_t Nf = hn::Lanes(df);
        const auto vhalf = hn::Set(df, 0.5f);
        const auto vzero = hn::Zero(df);
        const auto v255 = hn::Set(df, 255.0f);

        size_t x = 0;
        for(; x + Nf <= width; x += Nf) {
                // Load, clamp to [0, 255], round, convert to u8
                auto rf = hn::Min(hn::Max(hn::Add(hn::LoadU(df, buffers[0] + x), vhalf), vzero), v255);
                auto gf = hn::Min(hn::Max(hn::Add(hn::LoadU(df, buffers[1] + x), vhalf), vzero), v255);
                auto bf = hn::Min(hn::Max(hn::Add(hn::LoadU(df, buffers[2] + x), vhalf), vzero), v255);
                auto af = hn::Min(hn::Max(hn::Add(hn::LoadU(df, buffers[3] + x), vhalf), vzero), v255);

                auto r8 = hn::DemoteTo(du8q, hn::ConvertTo(di32, rf));
                auto g8 = hn::DemoteTo(du8q, hn::ConvertTo(di32, gf));
                auto b8 = hn::DemoteTo(du8q, hn::ConvertTo(di32, bf));
                auto a8 = hn::DemoteTo(du8q, hn::ConvertTo(di32, af));

                hn::StoreInterleaved4(r8, g8, b8, a8, du8q, dst + x * 4);
        }
        for(; x < width; x++) {
                auto clamp = [](float v) -> uint8_t {
                        int i = static_cast<int>(v + 0.5f);
                        if(i < 0) i = 0;
                        if(i > 255) i = 255;
                        return static_cast<uint8_t>(i);
                };
                dst[x * 4 + 0] = clamp(buffers[0][x]);
                dst[x * 4 + 1] = clamp(buffers[1][x]);
                dst[x * 4 + 2] = clamp(buffers[2][x]);
                dst[x * 4 + 3] = clamp(buffers[3][x]);
        }
        return;
}

// Pack float SoA to interleaved RGB8 (3 components, 8 bits)
static void packI3x8(const float *const *buffers, uint8_t *dst, size_t width) {
        const hn::ScalableTag<float> df;
        const hn::Rebind<int32_t, decltype(df)> di32;
        const hn::Rebind<uint8_t, decltype(df)> du8q;
        const size_t Nf = hn::Lanes(df);
        const auto vhalf = hn::Set(df, 0.5f);
        const auto vzero = hn::Zero(df);
        const auto v255 = hn::Set(df, 255.0f);

        size_t x = 0;
        for(; x + Nf <= width; x += Nf) {
                auto rf = hn::Min(hn::Max(hn::Add(hn::LoadU(df, buffers[0] + x), vhalf), vzero), v255);
                auto gf = hn::Min(hn::Max(hn::Add(hn::LoadU(df, buffers[1] + x), vhalf), vzero), v255);
                auto bf = hn::Min(hn::Max(hn::Add(hn::LoadU(df, buffers[2] + x), vhalf), vzero), v255);

                auto r8 = hn::DemoteTo(du8q, hn::ConvertTo(di32, rf));
                auto g8 = hn::DemoteTo(du8q, hn::ConvertTo(di32, gf));
                auto b8 = hn::DemoteTo(du8q, hn::ConvertTo(di32, bf));

                hn::StoreInterleaved3(r8, g8, b8, du8q, dst + x * 3);
        }
        for(; x < width; x++) {
                auto clamp = [](float v) -> uint8_t {
                        int i = static_cast<int>(v + 0.5f);
                        if(i < 0) i = 0;
                        if(i > 255) i = 255;
                        return static_cast<uint8_t>(i);
                };
                dst[x * 3 + 0] = clamp(buffers[0][x]);
                dst[x * 3 + 1] = clamp(buffers[1][x]);
                dst[x * 3 + 2] = clamp(buffers[2][x]);
        }
        return;
}

// General interleaved pack dispatch
void PackInterleavedImpl(const float *const *buffers, void *dst,
                         size_t width, int compCount, int bitsPerComp,
                         int bytesPerBlock, int pixelsPerBlock,
                         const int *compByteOffset, const int *compBits,
                         bool hasAlpha, int alphaCompIndex, bool useSimd) {
        uint8_t *p = static_cast<uint8_t *>(dst);

        // Fast path: 4-component 8-bit
        // Note: the caller (kernelPackInterleaved) already applies semBufMap
        // to route each semantic buffer to the correct component position,
        // so no additional alpha remapping is needed here.
        if(useSimd && pixelsPerBlock == 1 && compCount == 4 && bitsPerComp == 8 &&
           bytesPerBlock == 4 &&
           compByteOffset[0] == 0 && compByteOffset[1] == 1 &&
           compByteOffset[2] == 2 && compByteOffset[3] == 3) {
                packI4x8(buffers, p, width);
                return;
        }

        // Fast path: 3-component 8-bit
        if(useSimd && pixelsPerBlock == 1 && compCount == 3 && bitsPerComp == 8 &&
           bytesPerBlock == 3 &&
           compByteOffset[0] == 0 && compByteOffset[1] == 1 &&
           compByteOffset[2] == 2) {
                packI3x8(buffers, p, width);
                return;
        }

        // DPX 10-bit packed: 3 x 10-bit in a 32-bit word (big-endian byte order).
        if(pixelsPerBlock == 1 && compCount == 3 && bitsPerComp == 10 && bytesPerBlock == 4) {
                for(size_t x = 0; x < width; x++) {
                        auto clamp10 = [](float v) -> uint32_t {
                                int i = static_cast<int>(v + 0.5f);
                                if(i < 0) i = 0;
                                if(i > 1023) i = 1023;
                                return static_cast<uint32_t>(i);
                        };
                        uint32_t r = clamp10(buffers[0][x]);
                        uint32_t g = clamp10(buffers[1][x]);
                        uint32_t b = clamp10(buffers[2][x]);
                        uint32_t word = (r << 22) | (g << 12) | (b << 2);
                        // Store as big-endian
                        p[x * 4 + 0] = static_cast<uint8_t>(word >> 24);
                        p[x * 4 + 1] = static_cast<uint8_t>(word >> 16);
                        p[x * 4 + 2] = static_cast<uint8_t>(word >>  8);
                        p[x * 4 + 3] = static_cast<uint8_t>(word);
                }
                return;
        }

        // Scalar fallback
        auto packSample = [](uint8_t *base, int byteOffset, int bits, float val) {
                int maxVal = (1 << bits) - 1;
                int ival = static_cast<int>(val + 0.5f);
                if(ival < 0) ival = 0;
                if(ival > maxVal) ival = maxVal;
                if(bits <= 8) {
                        base[byteOffset] = static_cast<uint8_t>(ival);
                } else if(bits <= 16) {
                        uint16_t v = static_cast<uint16_t>(ival);
                        std::memcpy(base + byteOffset, &v, sizeof(v));
                }
        };

        if(pixelsPerBlock == 1) {
                for(size_t x = 0; x < width; x++) {
                        uint8_t *pixel = p + x * bytesPerBlock;
                        for(int c = 0; c < compCount; c++) {
                                packSample(pixel, compByteOffset[c], compBits[c], buffers[c][x]);
                        }
                }
        } else {
                size_t blockCount = (width + pixelsPerBlock - 1) / pixelsPerBlock;
                for(size_t b = 0; b < blockCount; b++) {
                        uint8_t *block = p + b * bytesPerBlock;
                        for(int px = 0; px < pixelsPerBlock; px++) {
                                size_t x = b * pixelsPerBlock + px;
                                if(x >= width) break;
                                int lumaOffset = compByteOffset[0] + px * 2;
                                packSample(block, lumaOffset, compBits[0], buffers[0][x]);
                        }
                        float cb = 0.0f, cr = 0.0f;
                        int count = 0;
                        for(int px = 0; px < pixelsPerBlock; px++) {
                                size_t x = b * pixelsPerBlock + px;
                                if(x >= width) break;
                                cb += buffers[1][x];
                                cr += buffers[2][x];
                                count++;
                        }
                        if(count > 0) { cb /= count; cr /= count; }
                        packSample(block, compByteOffset[1], compBits[1], cb);
                        packSample(block, compByteOffset[2], compBits[2], cr);
                }
        }
        return;
}

// SIMD pack for a single full-resolution 8-bit plane from float buffer
static void packPlane8(const float *buf, uint8_t *plane, size_t width) {
        const hn::ScalableTag<float> df;
        const hn::Rebind<int32_t, decltype(df)> di32;
        const hn::Rebind<uint8_t, decltype(df)> du8q;
        const size_t Nf = hn::Lanes(df);
        const auto vhalf = hn::Set(df, 0.5f);
        const auto vzero = hn::Zero(df);
        const auto v255 = hn::Set(df, 255.0f);

        size_t x = 0;
        for(; x + Nf <= width; x += Nf) {
                auto vf = hn::Min(hn::Max(hn::Add(hn::LoadU(df, buf + x), vhalf), vzero), v255);
                auto v8 = hn::DemoteTo(du8q, hn::ConvertTo(di32, vf));
                hn::StoreU(v8, du8q, plane + x);
        }
        for(; x < width; x++) {
                int val = static_cast<int>(buf[x] + 0.5f);
                if(val < 0) val = 0;
                if(val > 255) val = 255;
                plane[x] = static_cast<uint8_t>(val);
        }
        return;
}

void PackPlanarImpl(const float *const *buffers, void *const *planes,
                    const size_t *strides, size_t width,
                    int compCount, int bitsPerComp,
                    const int *compPlane, const int *planeHSub,
                    const int *planeBytesPerSample,
                    bool hasAlpha, int alphaCompIndex, bool useSimd) {
        for(int c = 0; c < compCount; c++) {
                int plane = compPlane[c];
                int hSub = planeHSub[plane];
                int bps = planeBytesPerSample[plane];
                uint8_t *p = static_cast<uint8_t *>(planes[plane]);
                int bufIdx = (c == alphaCompIndex) ? 3 :
                             ((c < alphaCompIndex || alphaCompIndex < 0) ? c : c - 1);

                size_t planeWidth = (width + hSub - 1) / hSub;

                // SIMD fast path: full-resolution 8-bit plane
                if(useSimd && hSub == 1 && bps == 1) {
                        packPlane8(buffers[bufIdx], p, width);
                        continue;
                }

                // Scalar fallback
                if(hSub == 1) {
                        for(size_t x = 0; x < planeWidth; x++) {
                                if(bps == 1) {
                                        int val = static_cast<int>(buffers[bufIdx][x] + 0.5f);
                                        if(val < 0) val = 0;
                                        if(val > 255) val = 255;
                                        p[x] = static_cast<uint8_t>(val);
                                } else if(bps == 2) {
                                        int val = static_cast<int>(buffers[bufIdx][x] + 0.5f);
                                        if(val < 0) val = 0;
                                        if(val > 65535) val = 65535;
                                        uint16_t v = static_cast<uint16_t>(val);
                                        std::memcpy(p + x * 2, &v, sizeof(v));
                                }
                        }
                } else {
                        for(size_t sx = 0; sx < planeWidth; sx++) {
                                float sum = 0.0f;
                                int count = 0;
                                for(int i = 0; i < hSub; i++) {
                                        size_t x = sx * hSub + i;
                                        if(x < width) {
                                                sum += buffers[bufIdx][x];
                                                count++;
                                        }
                                }
                                float avg = (count > 0) ? sum / count : 0.0f;
                                if(bps == 1) {
                                        int val = static_cast<int>(avg + 0.5f);
                                        if(val < 0) val = 0;
                                        if(val > 255) val = 255;
                                        p[sx] = static_cast<uint8_t>(val);
                                } else if(bps == 2) {
                                        int val = static_cast<int>(avg + 0.5f);
                                        if(val < 0) val = 0;
                                        if(val > 65535) val = 65535;
                                        uint16_t v = static_cast<uint16_t>(val);
                                        std::memcpy(p + sx * 2, &v, sizeof(v));
                                }
                        }
                }
        }
        return;
}

void PackSemiPlanarImpl(const float *const *buffers, void *const *planes,
                        const size_t *strides, size_t width,
                        int bitsPerComp, const int *planeHSub,
                        const int *planeBytesPerSample,
                        bool hasAlpha, int alphaCompIndex,
                        const int *compByteOffset, bool useSimd) {
        uint8_t *lumaPlane = static_cast<uint8_t *>(planes[0]);
        uint8_t *chromaPlane = static_cast<uint8_t *>(planes[1]);
        int chromaHSub = planeHSub[1];
        int sampleBytes = (bitsPerComp <= 8) ? 1 : 2;

        int cbOffset = compByteOffset[1];
        int crOffset = compByteOffset[2];

        // SIMD fast path: 8-bit luma
        if(useSimd && sampleBytes == 1) {
                packPlane8(buffers[0], lumaPlane, width);
        } else {
                // Scalar luma
                for(size_t x = 0; x < width; x++) {
                        int val = static_cast<int>(buffers[0][x] + 0.5f);
                        if(val < 0) val = 0;
                        if(sampleBytes == 1) {
                                if(val > 255) val = 255;
                                lumaPlane[x] = static_cast<uint8_t>(val);
                        } else {
                                if(val > 65535) val = 65535;
                                uint16_t v = static_cast<uint16_t>(val);
                                std::memcpy(lumaPlane + x * 2, &v, sizeof(v));
                        }
                }
        }

        // Pack chroma (interleaved CbCr or CrCb) with averaging
        size_t chromaWidth = (width + chromaHSub - 1) / chromaHSub;
        for(size_t cx = 0; cx < chromaWidth; cx++) {
                float cb = 0.0f, cr = 0.0f;
                int count = 0;
                for(int i = 0; i < chromaHSub; i++) {
                        size_t x = cx * chromaHSub + i;
                        if(x < width) {
                                cb += buffers[1][x];
                                cr += buffers[2][x];
                                count++;
                        }
                }
                if(count > 0) { cb /= count; cr /= count; }

                if(sampleBytes == 1) {
                        int icb = static_cast<int>(cb + 0.5f);
                        int icr = static_cast<int>(cr + 0.5f);
                        if(icb < 0) icb = 0; if(icb > 255) icb = 255;
                        if(icr < 0) icr = 0; if(icr > 255) icr = 255;
                        chromaPlane[cx * 2 + cbOffset] = static_cast<uint8_t>(icb);
                        chromaPlane[cx * 2 + crOffset] = static_cast<uint8_t>(icr);
                } else {
                        int icb = static_cast<int>(cb + 0.5f);
                        int icr = static_cast<int>(cr + 0.5f);
                        if(icb < 0) icb = 0; if(icb > 65535) icb = 65535;
                        if(icr < 0) icr = 0; if(icr > 65535) icr = 65535;
                        uint16_t vcb = static_cast<uint16_t>(icb);
                        uint16_t vcr = static_cast<uint16_t>(icr);
                        std::memcpy(chromaPlane + cx * 4 + cbOffset, &vcb, sizeof(vcb));
                        std::memcpy(chromaPlane + cx * 4 + crOffset, &vcr, sizeof(vcr));
                }
        }
        return;
}

}  // namespace HWY_NAMESPACE
}  // namespace csc
}  // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
