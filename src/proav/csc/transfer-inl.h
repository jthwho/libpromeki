/**
 * @file      transfer-inl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Highway SIMD implementation of LUT-based transfer functions.
 * Re-included per target via foreach_target.h.
 */

#if defined(PROMEKI_CSC_TRANSFER_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef PROMEKI_CSC_TRANSFER_INL_H_
#undef PROMEKI_CSC_TRANSFER_INL_H_
#else
#define PROMEKI_CSC_TRANSFER_INL_H_
#endif

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace promeki {
namespace csc {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

void ApplyLUTImpl(float *buffer, size_t width, const float *lut, size_t lutSize, bool useSimd) {
        const hn::ScalableTag<float> df;
        const hn::RebindToSigned<decltype(df)> di;
        const size_t N = hn::Lanes(df);

        const auto vscale = hn::Set(df, static_cast<float>(lutSize - 1));
        const auto vhalf = hn::Set(df, 0.5f);
        const auto vzero = hn::Zero(df);
        const auto vmax = hn::Set(di, static_cast<int>(lutSize - 1));
        const auto vizero = hn::Zero(di);

        size_t x = 0;
#if HWY_TARGET != HWY_SCALAR && HWY_TARGET != HWY_EMU128
        // Use GatherIndex on targets that support it efficiently
        if(useSimd) for(; x + N <= width; x += N) {
                auto val = hn::LoadU(df, buffer + x);
                val = hn::MulAdd(val, vscale, vhalf);
                val = hn::Max(val, vzero);
                val = hn::Min(val, hn::ConvertTo(df, vmax));
                auto idx = hn::ConvertTo(di, val);
                idx = hn::Max(idx, vizero);
                idx = hn::Min(idx, vmax);
                auto result = hn::GatherIndex(df, lut, idx);
                hn::StoreU(result, df, buffer + x);
        }
#endif
        // Scalar tail (and full fallback for scalar/emu targets)
        float scale = static_cast<float>(lutSize - 1);
        for(; x < width; x++) {
                float val = buffer[x] * scale + 0.5f;
                if(val < 0.0f) val = 0.0f;
                if(val > scale) val = scale;
                int idx = static_cast<int>(val);
                if(idx < 0) idx = 0;
                if(static_cast<size_t>(idx) >= lutSize) idx = static_cast<int>(lutSize - 1);
                buffer[x] = lut[idx];
        }
        return;
}

}  // namespace HWY_NAMESPACE
}  // namespace csc
}  // namespace promeki
HWY_AFTER_NAMESPACE();

#endif
