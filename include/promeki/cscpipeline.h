/**
 * @file      cscpipeline.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/pixelformat.h>
#include <promeki/mediaconfig.h>
#include <promeki/csccontext.h>
#include <promeki/cscregistry.h>

PROMEKI_NAMESPACE_BEGIN

class Image;

/**
 * @brief Pre-compiled color space conversion pipeline.
 * @ingroup proav
 *
 * CSCPipeline analyzes a source and target PixelFormat at construction
 * time and compiles an optimized chain of processing stages.  After
 * construction the pipeline is immutable and thread-safe to execute
 * concurrently from multiple threads (each caller must provide its own
 * CSCContext for scratch storage).
 *
 * The general conversion pipeline for one scanline is:
 *
 * 1. **Unpack** source memory layout into float SoA buffers
 * 2. **Chroma upsample** (if source is subsampled)
 * 3. **Range denormalize** limited-range integers to 0.0-1.0
 * 4. **Source EOTF** remove source transfer function
 * 5. **Matrix multiply** source color model to target color model
 * 6. **Target OETF** apply target transfer function
 * 7. **Range normalize** 0.0-1.0 to target integer range
 * 8. **Chroma downsample** (if target is subsampled)
 * 9. **Pack** float SoA buffers into target memory layout
 *
 * The compiler aggressively eliminates stages when possible (same color
 * model, same transfer function, same primaries, etc.) and pre-computes
 * combined matrices and LUT tables.
 *
 * @par Execution tiers
 * The pipeline selects the best available execution path:
 * 1. **Fast path** — integer-domain kernel for common broadcast pairs
 *    (3,000–10,000+ Mpix/s at 1080p).
 * 2. **SIMD generic** — float-domain multi-stage pipeline with Highway
 *    SIMD (200–1,300 Mpix/s).
 * 3. **Scalar generic** — same pipeline, SIMD disabled.  Selected by
 *    setting @ref MediaConfig::CscPath to @c CscPath::Scalar in the config.
 *
 * @par Accuracy
 * The scalar pipeline matches Color::convert() within ±2 LSB for 8-bit.
 * Fast paths use integer BT.709/601/2020 arithmetic and differ from the
 * scalar pipeline by up to ±35 LSB near black (sRGB vs Rec.709 transfer
 * function divergence).  For mid-range values, fast-path error is ≤2 LSB.
 * See @ref csc_accuracy "CSC Accuracy Characteristics" for details.
 *
 * @par Example
 * @code
 * CSCPipeline pipeline(PixelFormat::RGBA8_sRGB, PixelFormat::YUV8_422_Rec709);
 * if(pipeline.isValid()) {
 *     Image dst(srcImage.size(), PixelFormat::YUV8_422_Rec709);
 *     pipeline.execute(srcImage, dst);
 * }
 *
 * // Force scalar path for reference comparison:
 * MediaConfig cfg;
 * cfg.set(MediaConfig::CscPath, CscPath::Scalar);
 * CSCPipeline ref(src, dst, cfg);
 * @endcode
 *
 * @see CSCContext, CSCRegistry, @ref csc "CSC Framework"
 */
class CSCPipeline {
        PROMEKI_SHARED_FINAL(CSCPipeline)
        public:
                /** @brief Shared pointer type for CSCPipeline. */
                using Ptr = SharedPtr<CSCPipeline>;

                /** @brief Maximum LUT size for transfer function tables. */
                static constexpr size_t MaxLUTSize = 4096;

                /**
                 * @brief Identifies a single processing stage in the pipeline.
                 */
                enum StageType {
                        StageUnpack,            ///< Unpack source pixels to float SoA.
                        StageChromaUpsample,    ///< Upsample chroma to full resolution.
                        StageRangeIn,           ///< Map input range to 0.0-1.0.
                        StageMonoExpand,        ///< Broadcast buffer[0] to buffers[1] and [2] (mono -> RGB).
                        StageEOTF,              ///< Remove source transfer function.
                        StageMatrix,            ///< 3x3 color matrix multiply.
                        StageOETF,              ///< Apply target transfer function.
                        StageRangeOut,          ///< Map 0.0-1.0 to output range.
                        StageChromaDownsample,  ///< Downsample chroma for target.
                        StagePack,              ///< Pack float SoA to target pixels.
                        StageAlphaFill          ///< Fill alpha channel with constant.
                };

                /**
                 * @brief Internal stage descriptor.
                 *
                 * Each stage holds a function pointer to its kernel and the
                 * parameters it needs.  Stages are stored in a flat list and
                 * invoked sequentially for each scanline.
                 */
                struct Stage {
                        StageType type;

                        /** @brief The kernel function for this stage. */
                        void (*func)(const Stage *stage,
                                     const void *const *srcPlanes,
                                     const size_t *srcStrides,
                                     void *const *dstPlanes,
                                     const size_t *dstStrides,
                                     size_t width, size_t y,
                                     CSCContext &ctx) = nullptr;

                        /** @brief 3x3 matrix for StageMatrix. */
                        float matrix[3][3] = {};

                        /** @brief Offset applied before or after matrix multiply. */
                        float matrixOffset[3] = {};

                        /** @brief Pre-offset applied before matrix multiply (for YCbCr source). */
                        float matrixPreOffset[3] = {};

                        /** @brief Per-component scale for range mapping. */
                        float rangeScale[4] = {};

                        /** @brief Per-component bias for range mapping. */
                        float rangeBias[4] = {};

                        /** @brief Transfer function LUT (indexed by integer sample value). */
                        float *lut = nullptr;

                        /** @brief LUT entry count. */
                        size_t lutSize = 0;

                        /** @brief Number of components to process. */
                        int compCount = 3;

                        /** @brief Alpha fill value (for StageAlphaFill). */
                        float alphaValue = 1.0f;

                        /** @brief Horizontal chroma subsampling ratio. */
                        int chromaHRatio = 1;

                        /** @brief Vertical chroma subsampling ratio. */
                        int chromaVRatio = 1;

                        /**
                         * @brief Semantic buffer map for unpack/pack.
                         *
                         * Maps component index to SoA buffer index based on
                         * semantic meaning (R/Y->0, G/Cb->1, B/Cr->2, A->3).
                         * Handles formats with non-standard component ordering
                         * such as BGRA, ARGB, etc.
                         */
                        int semBufMap[8] = {0, 1, 2, 3, 4, 5, 6, 7};

                        /** @brief Source/target component count for unpack/pack. */
                        int pixelCompCount = 0;

                        /** @brief Bits per component for unpack/pack. */
                        int bitsPerComp = 0;

                        /** @brief Bytes per pixel block for unpack/pack. */
                        int bytesPerBlock = 0;

                        /** @brief Pixels per block for unpack/pack. */
                        int pixelsPerBlock = 1;

                        /** @brief Whether source/target has alpha. */
                        bool hasAlpha = false;

                        /** @brief Component index of alpha in source/target. */
                        int alphaCompIndex = -1;

                        /** @brief Whether to use SIMD-optimized code paths. */
                        bool useSimd = true;

                        /** @brief Number of planes in source/target format. */
                        int planeCount = 1;

                        /** @brief Per-plane bytes per sample. */
                        int planeBytesPerSample[4] = {};

                        /** @brief Per-plane horizontal subsampling. */
                        int planeHSub[4] = {};

                        /** @brief Per-plane vertical subsampling. */
                        int planeVSub[4] = {};

                        /** @brief Per-component plane index. */
                        int compPlane[8] = {};

                        /** @brief Per-component byte offset within pixel/block. */
                        int compByteOffset[8] = {};

                        /** @brief Per-component bit depth. */
                        int compBits[8] = {};

                        ~Stage() { delete[] lut; }
                        Stage() = default;
                        Stage(const Stage &other);
                        Stage &operator=(const Stage &other);
                        Stage(Stage &&other) noexcept;
                        Stage &operator=(Stage &&other) noexcept;
                };

                /** @brief Constructs an invalid pipeline. */
                CSCPipeline() = default;

                /**
                 * @brief Compiles a pipeline from source to target pixel descriptions.
                 * @param src    Source pixel description.
                 * @param dst    Target pixel description.
                 * @param config Optional configuration hints
                 *               (e.g. @ref MediaConfig::CscPath).
                 */
                CSCPipeline(const PixelFormat &src, const PixelFormat &dst,
                            const MediaConfig &config = MediaConfig());

                /**
                 * @brief Returns a shared, compiled pipeline from a global cache.
                 *
                 * Since a compiled @c CSCPipeline is a pure function of
                 * @c (srcDesc, dstDesc, useSimd) — it stores no
                 * image-specific state and @c execute() is documented
                 * as thread-safe to call concurrently — the library
                 * keeps a process-wide cache of compiled pipelines.
                 * The first call for a given key compiles a pipeline;
                 * subsequent calls return the same shared instance.
                 *
                 * Callers that repeatedly convert between the same
                 * formats should prefer this over constructing a fresh
                 * @c CSCPipeline each time, since @c compile() builds
                 * all the stages from scratch (allocating LUTs,
                 * computing matrices, etc.) for the general pipeline.
                 *
                 * @par Thread safety
                 * The cache itself is mutex-protected.  The returned
                 * pipeline is safe to execute from any thread; each
                 * call to @c execute() allocates its own scratch
                 * @c CSCContext.
                 *
                 * @par Example
                 * @code
                 * auto p = CSCPipeline::cached(srcDesc, dstDesc);
                 * if(p && p->isValid()) {
                 *     p->execute(srcImage, dstImage);
                 * }
                 * @endcode
                 *
                 * @param src    Source pixel description.
                 * @param dst    Target pixel description.
                 * @param config Optional configuration hints.  Only
                 *               @ref MediaConfig::CscPath currently
                 *               affects the cache key; other keys are
                 *               ignored for the purposes of lookup.
                 * @return A shared pipeline, or a null @c Ptr on
                 *         allocation failure.  The pipeline may still
                 *         be invalid (e.g. unsupported format pair) —
                 *         check @c isValid() before executing.
                 */
                static Ptr cached(const PixelFormat &src, const PixelFormat &dst,
                                  const MediaConfig &config = MediaConfig());

                /**
                 * @brief Returns true if the pipeline was compiled successfully.
                 * @return true if the pipeline is ready to execute.
                 */
                bool isValid() const { return _valid; }

                /**
                 * @brief Returns the source pixel description.
                 * @return A const reference to the source PixelFormat.
                 */
                const PixelFormat &srcDesc() const { return _srcDesc; }

                /**
                 * @brief Returns the target pixel description.
                 * @return A const reference to the target PixelFormat.
                 */
                const PixelFormat &dstDesc() const { return _dstDesc; }

                /**
                 * @brief Returns true if this is an identity conversion (src == dst).
                 * @return true if no conversion is needed.
                 */
                bool isIdentity() const { return _identity; }

                /**
                 * @brief Returns true if this uses a registered fast-path kernel.
                 * @return true if a fast path was found.
                 */
                bool isFastPath() const { return _fastPathFunc != nullptr; }

                /**
                 * @brief Returns the number of stages in the pipeline.
                 * @return The stage count.
                 */
                int stageCount() const { return _stages.size(); }

                /**
                 * @brief Converts an entire image.
                 *
                 * The source and destination images must have matching dimensions.
                 * The source must use this pipeline's source PixelFormat and the
                 * destination must use the target PixelFormat.
                 *
                 * @param src Source image.
                 * @param dst Destination image (must be pre-allocated).
                 * @return Error::Ok on success.
                 */
                Error execute(const Image &src, Image &dst) const;

                /**
                 * @brief Processes a single scanline.
                 *
                 * Low-level interface for integration with custom processing.
                 *
                 * @param srcPlanes  Array of source plane pointers for this line.
                 * @param srcStrides Array of source line strides in bytes.
                 * @param dstPlanes  Array of target plane pointers for this line.
                 * @param dstStrides Array of target line strides in bytes.
                 * @param width      Pixel width.
                 * @param y          Current line number.
                 * @param ctx        Scratch context.
                 */
                void processLine(const void *const *srcPlanes,
                                 const size_t *srcStrides,
                                 void *const *dstPlanes,
                                 const size_t *dstStrides,
                                 size_t width, size_t y,
                                 CSCContext &ctx) const;

        private:
                PixelFormat               _srcDesc;
                PixelFormat               _dstDesc;
                MediaConfig             _config;
                bool                    _valid = false;
                bool                    _identity = false;
                bool                    _useSimd = true;
                CSCRegistry::LineFuncPtr _fastPathFunc = nullptr;
                List<Stage>             _stages;

                void compile();
                void buildUnpackStage(const PixelFormat &pd, Stage &stage);
                void buildPackStage(const PixelFormat &pd, Stage &stage);
                void buildRangeStage(const PixelFormat &pd, Stage &stage, bool isInput);
                void buildTransferStage(const ColorModel &cm, Stage &stage, bool isEOTF, int bits);
                void buildMatrixStage(const ColorModel &src, const ColorModel &dst, Stage &stage);
};

PROMEKI_NAMESPACE_END
