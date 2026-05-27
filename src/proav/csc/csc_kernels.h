/**
 * @file      csc_kernels.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Internal header declaring CSC kernel dispatch functions.  These are
 * the entry points called by the pipeline stages.  Each function
 * dispatches to the best available SIMD implementation at runtime
 * (via Highway dynamic dispatch).
 *
 * When @p useSimd is false, kernels bypass SIMD fast paths and use
 * scalar-only code.  This is controlled by the "Path" config key on
 * the CSCPipeline.
 */

#pragma once

#include <cstddef>

namespace promeki {

        struct CSCPipeline;

        namespace csc {

                // --- Unpack kernels ---

                void unpackInterleaved(const void *src, float *const *buffers, size_t width, int compCount,
                                       int bitsPerComp, int bytesPerBlock, int pixelsPerBlock,
                                       const int *compByteOffset, const int *compBits, bool hasAlpha,
                                       int alphaCompIndex, bool useSimd = true);

                void unpackPlanar(const void *const *planes, const size_t *strides, float *const *buffers, size_t width,
                                  int compCount, int bitsPerComp, const int *compPlane, const int *planeHSub,
                                  const int *planeBytesPerSample, bool hasAlpha, int alphaCompIndex,
                                  bool useSimd = true);

                void unpackSemiPlanar(const void *const *planes, const size_t *strides, float *const *buffers,
                                      size_t width, int bitsPerComp, const int *planeHSub,
                                      const int *planeBytesPerSample, bool hasAlpha, int alphaCompIndex,
                                      const int *compByteOffset = nullptr, bool useSimd = true);

                // --- Pack kernels ---

                void packInterleaved(const float *const *buffers, void *dst, size_t width, int compCount,
                                     int bitsPerComp, int bytesPerBlock, int pixelsPerBlock, const int *compByteOffset,
                                     const int *compBits, bool hasAlpha, int alphaCompIndex, bool useSimd = true);

                void packPlanar(const float *const *buffers, void *const *planes, const size_t *strides, size_t width,
                                int compCount, int bitsPerComp, const int *compPlane, const int *planeHSub,
                                const int *planeBytesPerSample, bool hasAlpha, int alphaCompIndex, bool useSimd = true);

                void packSemiPlanar(const float *const *buffers, void *const *planes, const size_t *strides,
                                    size_t width, int bitsPerComp, const int *planeHSub, const int *planeBytesPerSample,
                                    bool hasAlpha, int alphaCompIndex, const int *compByteOffset = nullptr,
                                    bool useSimd = true);

                // --- Transfer function kernels ---

                void applyLUT(float *buffer, size_t width, const float *lut, size_t lutSize, bool useSimd = true);

                void applyTransferFunc(float *buffer, size_t width, double (*func)(double));

                // HDR direct-compute transfer kernels.  Required for
                // float source / destination buffers where the @ref
                // applyLUT path's [0,1] indexing would clamp
                // scene-referred linear inputs above 1.0 and destroy
                // HDR highlight data.  Each operates in-place on a
                // single SoA float buffer of length @p width.
                void applyPqOETF(float *buffer, size_t width, bool useSimd = true);
                void applyPqEOTF(float *buffer, size_t width, bool useSimd = true);
                void applyHlgOETF(float *buffer, size_t width, bool useSimd = true);
                void applyHlgEOTF(float *buffer, size_t width, bool useSimd = true);

                // BT.2390-9 Annex B.2.5 EETF tone-mapping in PQ space.
                // Compresses input PQ values in [0, @p srcMaxPq] into
                // [0, @p dstMaxPq] via a Hermite-spline shoulder.
                // Operates per-channel in place; @p srcMaxPq and
                // @p dstMaxPq are both PQ-encoded peak values
                // (typically derived from the source / target nominal
                // peak luminance via the PQ OETF).  When @p dstMaxPq
                // >= @p srcMaxPq the call is identity — no compression
                // is performed.
                void applyBt2390EETF(float *buffer, size_t width, float srcMaxPq, float dstMaxPq, bool useSimd = true);

                // --- Matrix multiply kernel ---

                void matrixMultiply3x3(float *buf0, float *buf1, float *buf2, size_t width, const float matrix[3][3],
                                       const float preOffset[3], const float postOffset[3], bool useSimd = true);

                // --- Range mapping kernels ---

                void rangeMap(float *const *buffers, size_t width, int compCount, const float *scale, const float *bias,
                              bool useSimd = true);

                // --- Chroma kernels ---

                void chromaUpsampleH(const float *src, float *dst, size_t srcWidth, int ratio);

                void chromaDownsampleH(const float *src, float *dst, size_t srcWidth, int ratio);

                // --- Alpha kernels ---

                void alphaFill(float *buffer, size_t width, float value, bool useSimd = true);

        } // namespace csc
} // namespace promeki
