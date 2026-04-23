/**
 * @file      cscpipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/cscpipeline.h>
#include <promeki/image.h>
#include <promeki/colormodel.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/enums.h>
#include <cstring>
#include <cmath>
#include <tuple>
#include "csc_kernels.h"

PROMEKI_NAMESPACE_BEGIN

// Resolves the @ref MediaConfig::CscPath knob from a @ref MediaConfig
// down to a single boolean: true = SIMD-optimized path, false = scalar.
// Accepts the value as either a @ref CscPath @ref Enum, its string name
// (``"Optimized"`` / ``"Scalar"``), or its integer ordinal — every form
// the @ref Variant::asEnum dispatcher knows about.  Unknown values are
// treated as @ref CscPath::Optimized so a malformed config never disables
// SIMD silently for an unrelated downstream stage.
static bool resolveUseSimd(const MediaConfig &config) {
        if(!config.contains(MediaConfig::CscPath)) return true;
        Variant v = config.get(MediaConfig::CscPath);
        Enum e = v.asEnum(CscPath::Type);
        if(!e.hasListedValue()) return true;
        return e.value() != CscPath::Scalar.value();
}

// --- Stage copy/move ---

CSCPipeline::Stage::Stage(const Stage &other) {
        std::memcpy(this, &other, offsetof(Stage, lut));
        lutSize = other.lutSize;
        if(other.lut && other.lutSize > 0) {
                lut = new float[other.lutSize];
                std::memcpy(lut, other.lut, other.lutSize * sizeof(float));
        } else {
                lut = nullptr;
        }
        // Copy remaining fields after lut
        compCount = other.compCount;
        alphaValue = other.alphaValue;
        chromaHRatio = other.chromaHRatio;
        chromaVRatio = other.chromaVRatio;
        pixelCompCount = other.pixelCompCount;
        bitsPerComp = other.bitsPerComp;
        bytesPerBlock = other.bytesPerBlock;
        pixelsPerBlock = other.pixelsPerBlock;
        hasAlpha = other.hasAlpha;
        alphaCompIndex = other.alphaCompIndex;
        planeCount = other.planeCount;
        std::memcpy(planeBytesPerSample, other.planeBytesPerSample, sizeof(planeBytesPerSample));
        std::memcpy(planeHSub, other.planeHSub, sizeof(planeHSub));
        std::memcpy(planeVSub, other.planeVSub, sizeof(planeVSub));
        std::memcpy(compPlane, other.compPlane, sizeof(compPlane));
        std::memcpy(compByteOffset, other.compByteOffset, sizeof(compByteOffset));
        std::memcpy(compBits, other.compBits, sizeof(compBits));
}

CSCPipeline::Stage &CSCPipeline::Stage::operator=(const Stage &other) {
        if(this != &other) {
                delete[] lut;
                new (this) Stage(other);
        }
        return *this;
}

CSCPipeline::Stage::Stage(Stage &&other) noexcept {
        std::memcpy(this, &other, sizeof(Stage));
        other.lut = nullptr;
        other.lutSize = 0;
}

CSCPipeline::Stage &CSCPipeline::Stage::operator=(Stage &&other) noexcept {
        if(this != &other) {
                delete[] lut;
                std::memcpy(this, &other, sizeof(Stage));
                other.lut = nullptr;
                other.lutSize = 0;
        }
        return *this;
}

// --- Pipeline construction ---

CSCPipeline::CSCPipeline(const PixelFormat &src, const PixelFormat &dst,
                         const MediaConfig &config)
        : _srcDesc(src), _dstDesc(dst), _config(config) {
        _useSimd = resolveUseSimd(_config);
        compile();
}

// --- Global pipeline cache ---
//
// A compiled CSCPipeline is a pure function of (srcDesc, dstDesc,
// useSimd): no image-specific state is ever stored, and execute() is
// documented as thread-safe to call concurrently from multiple threads.
// That makes it safe to share compiled pipelines process-wide via a
// mutex-protected lookup table.  The alternative — constructing a
// fresh CSCPipeline on every Image::convert() call — pays compile()'s
// stage-building cost every frame even when the conversion target
// never changes.

namespace {

using CacheKey = std::tuple<const PixelFormat::Data *,
                            const PixelFormat::Data *,
                            bool>;

struct CacheEntry {
        CSCPipeline::Ptr pipeline;
};

// Wrap the cache state in function-local statics so the destructors
// run via atexit (before valgrind's leak check) instead of via
// _dl_fini (after).  Otherwise the cached pipelines and the map
// nodes that hold them show up as "still reachable" on every run
// even though everything is freed at process exit.
static Mutex &cacheMutex() {
        static Mutex m;
        return m;
}

static Map<CacheKey, CacheEntry> &cache() {
        static Map<CacheKey, CacheEntry> c;
        return c;
}

} // anonymous

CSCPipeline::Ptr CSCPipeline::cached(const PixelFormat &src, const PixelFormat &dst,
                                     const MediaConfig &config) {
        // Mirror the useSimd derivation from the instance constructor
        // so cache keys line up with actual pipeline behavior.
        const bool useSimd = resolveUseSimd(config);
        CacheKey key{src.data(), dst.data(), useSimd};

        {
                Mutex::Locker lock(cacheMutex());
                auto &c = cache();
                auto it = c.find(key);
                if(it != c.end()) {
                        return it->second.pipeline;
                }
        }

        // Compile outside the lock so concurrent callers requesting
        // different format pairs don't serialize on each other.  The
        // second-lookup-and-insert below handles the race where two
        // callers ask for the same pair at the same time — the loser
        // drops its freshly-compiled pipeline and returns the winner's.
        Ptr fresh = Ptr::create(src, dst, config);
        if(!fresh.isValid()) return Ptr();

        Mutex::Locker lock(cacheMutex());
        auto &c = cache();
        auto it = c.find(key);
        if(it != c.end()) {
                return it->second.pipeline;
        }
        c.insert(key, CacheEntry{fresh});
        return fresh;
}

// --- Stage kernel wrappers ---
// These are the function pointers stored in Stage::func.  They adapt
// between the uniform Stage callback signature and the specific kernel APIs.

static void kernelUnpackInterleaved(const CSCPipeline::Stage *stage,
                                    const void *const *srcPlanes,
                                    const size_t *srcStrides,
                                    void *const *dstPlanes,
                                    const size_t *dstStrides,
                                    size_t width, size_t y,
                                    CSCContext &ctx) {
        // Use semBufMap to route components to correct SoA buffers
        // (handles BGRA, ARGB, etc. where component order differs from R,G,B,A)
        const int *m = stage->semBufMap;
        float *buffers[4] = { ctx.buffer(m[0]), ctx.buffer(m[1]), ctx.buffer(m[2]), ctx.buffer(m[3]) };
        csc::unpackInterleaved(srcPlanes[0], buffers, width,
                               stage->pixelCompCount, stage->bitsPerComp,
                               stage->bytesPerBlock, stage->pixelsPerBlock,
                               stage->compByteOffset, stage->compBits,
                               stage->hasAlpha, stage->alphaCompIndex,
                               stage->useSimd);
}

static void kernelUnpackPlanar(const CSCPipeline::Stage *stage,
                               const void *const *srcPlanes,
                               const size_t *srcStrides,
                               void *const *dstPlanes,
                               const size_t *dstStrides,
                               size_t width, size_t y,
                               CSCContext &ctx) {
        float *buffers[4] = { ctx.buffer(0), ctx.buffer(1), ctx.buffer(2), ctx.buffer(3) };
        csc::unpackPlanar(srcPlanes, srcStrides, buffers, width,
                          stage->pixelCompCount, stage->bitsPerComp,
                          stage->compPlane, stage->planeHSub,
                          stage->planeBytesPerSample,
                          stage->hasAlpha, stage->alphaCompIndex,
                          stage->useSimd);
}

static void kernelUnpackSemiPlanar(const CSCPipeline::Stage *stage,
                                   const void *const *srcPlanes,
                                   const size_t *srcStrides,
                                   void *const *dstPlanes,
                                   const size_t *dstStrides,
                                   size_t width, size_t y,
                                   CSCContext &ctx) {
        float *buffers[4] = { ctx.buffer(0), ctx.buffer(1), ctx.buffer(2), ctx.buffer(3) };
        csc::unpackSemiPlanar(srcPlanes, srcStrides, buffers, width,
                              stage->bitsPerComp, stage->planeHSub,
                              stage->planeBytesPerSample,
                              stage->hasAlpha, stage->alphaCompIndex,
                              stage->compByteOffset, stage->useSimd);
}

static void kernelPackInterleaved(const CSCPipeline::Stage *stage,
                                  const void *const *srcPlanes,
                                  const size_t *srcStrides,
                                  void *const *dstPlanes,
                                  const size_t *dstStrides,
                                  size_t width, size_t y,
                                  CSCContext &ctx) {
        const int *m = stage->semBufMap;
        const float *buffers[4] = { ctx.buffer(m[0]), ctx.buffer(m[1]), ctx.buffer(m[2]), ctx.buffer(m[3]) };
        csc::packInterleaved(buffers, dstPlanes[0], width,
                             stage->pixelCompCount, stage->bitsPerComp,
                             stage->bytesPerBlock, stage->pixelsPerBlock,
                             stage->compByteOffset, stage->compBits,
                             stage->hasAlpha, stage->alphaCompIndex,
                             stage->useSimd);
}

static void kernelPackPlanar(const CSCPipeline::Stage *stage,
                             const void *const *srcPlanes,
                             const size_t *srcStrides,
                             void *const *dstPlanes,
                             const size_t *dstStrides,
                             size_t width, size_t y,
                             CSCContext &ctx) {
        const float *buffers[4] = { ctx.buffer(0), ctx.buffer(1), ctx.buffer(2), ctx.buffer(3) };
        csc::packPlanar(buffers, dstPlanes, dstStrides, width,
                        stage->pixelCompCount, stage->bitsPerComp,
                        stage->compPlane, stage->planeHSub,
                        stage->planeBytesPerSample,
                        stage->hasAlpha, stage->alphaCompIndex,
                        stage->useSimd);
}

static void kernelPackSemiPlanar(const CSCPipeline::Stage *stage,
                                 const void *const *srcPlanes,
                                 const size_t *srcStrides,
                                 void *const *dstPlanes,
                                 const size_t *dstStrides,
                                 size_t width, size_t y,
                                 CSCContext &ctx) {
        const float *buffers[4] = { ctx.buffer(0), ctx.buffer(1), ctx.buffer(2), ctx.buffer(3) };
        csc::packSemiPlanar(buffers, dstPlanes, dstStrides, width,
                            stage->bitsPerComp, stage->planeHSub,
                            stage->planeBytesPerSample,
                            stage->hasAlpha, stage->alphaCompIndex,
                            stage->compByteOffset, stage->useSimd);
}

static void kernelRangeMap(const CSCPipeline::Stage *stage,
                           const void *const *srcPlanes,
                           const size_t *srcStrides,
                           void *const *dstPlanes,
                           const size_t *dstStrides,
                           size_t width, size_t y,
                           CSCContext &ctx) {
        float *buffers[4] = { ctx.buffer(0), ctx.buffer(1), ctx.buffer(2), ctx.buffer(3) };
        csc::rangeMap(buffers, width, stage->compCount, stage->rangeScale, stage->rangeBias,
                      stage->useSimd);
}

static void kernelTransferLUT(const CSCPipeline::Stage *stage,
                              const void *const *srcPlanes,
                              const size_t *srcStrides,
                              void *const *dstPlanes,
                              const size_t *dstStrides,
                              size_t width, size_t y,
                              CSCContext &ctx) {
        for(int c = 0; c < stage->compCount; c++) {
                csc::applyLUT(ctx.buffer(c), width, stage->lut, stage->lutSize, stage->useSimd);
        }
}

static void kernelMatrix(const CSCPipeline::Stage *stage,
                         const void *const *srcPlanes,
                         const size_t *srcStrides,
                         void *const *dstPlanes,
                         const size_t *dstStrides,
                         size_t width, size_t y,
                         CSCContext &ctx) {
        csc::matrixMultiply3x3(ctx.buffer(0), ctx.buffer(1), ctx.buffer(2),
                               width, stage->matrix,
                               stage->matrixPreOffset, stage->matrixOffset,
                               stage->useSimd);
}

static void kernelAlphaFill(const CSCPipeline::Stage *stage,
                            const void *const *srcPlanes,
                            const size_t *srcStrides,
                            void *const *dstPlanes,
                            const size_t *dstStrides,
                            size_t width, size_t y,
                            CSCContext &ctx) {
        csc::alphaFill(ctx.buffer(3), width, stage->alphaValue, stage->useSimd);
}

// Broadcast the luma buffer (buffer 0) into buffers 1 and 2 so that
// downstream stages which operate on all three color components see
// valid grayscale data in each channel.  Used when converting a mono
// source to a multi-component destination.
static void kernelMonoExpand(const CSCPipeline::Stage *stage,
                             const void *const *srcPlanes,
                             const size_t *srcStrides,
                             void *const *dstPlanes,
                             const size_t *dstStrides,
                             size_t width, size_t y,
                             CSCContext &ctx) {
        (void)stage; (void)srcPlanes; (void)srcStrides;
        (void)dstPlanes; (void)dstStrides; (void)y;
        const float *src = ctx.buffer(0);
        float *g = ctx.buffer(1);
        float *b = ctx.buffer(2);
        std::memcpy(g, src, width * sizeof(float));
        std::memcpy(b, src, width * sizeof(float));
}

// --- Pipeline compiler ---

// Compute semantic buffer mapping: maps component index to SoA buffer
// index based on the component's semantic meaning.
// R/Y -> buf 0, G/Cb -> buf 1, B/Cr -> buf 2, A -> buf 3.
static void computeSemBufMap(const PixelFormat &pd, int *semBufMap) {
        const PixelFormat::Data *pdd = pd.data();
        int cc = static_cast<int>(pd.memLayout().compCount());
        for(int c = 0; c < cc; c++) {
                const String &abbrev = pdd->compSemantics[c].abbrev;
                if(abbrev == "R" || abbrev == "Y")       semBufMap[c] = 0;
                else if(abbrev == "G" || abbrev == "Cb")  semBufMap[c] = 1;
                else if(abbrev == "B" || abbrev == "Cr")  semBufMap[c] = 2;
                else if(abbrev == "A")                    semBufMap[c] = 3;
                else                                      semBufMap[c] = c; // fallback
        }
        return;
}

void CSCPipeline::buildUnpackStage(const PixelFormat &pd, Stage &stage) {
        stage.type = StageUnpack;
        const PixelMemLayout &pf = pd.memLayout();
        const PixelMemLayout::Data *pfd = pf.data();

        stage.pixelCompCount = static_cast<int>(pfd->compCount);
        stage.bitsPerComp = static_cast<int>(pfd->comps[0].bits);
        stage.bytesPerBlock = static_cast<int>(pfd->bytesPerBlock);
        stage.pixelsPerBlock = static_cast<int>(pfd->pixelsPerBlock);
        stage.hasAlpha = pd.hasAlpha();
        stage.alphaCompIndex = pd.alphaCompIndex();
        stage.planeCount = static_cast<int>(pfd->planeCount);

        for(size_t i = 0; i < pfd->compCount; i++) {
                stage.compPlane[i] = pfd->comps[i].plane;
                stage.compByteOffset[i] = static_cast<int>(pfd->comps[i].byteOffset);
                stage.compBits[i] = static_cast<int>(pfd->comps[i].bits);
        }

        computeSemBufMap(pd, stage.semBufMap);

        for(size_t i = 0; i < pfd->planeCount; i++) {
                stage.planeHSub[i] = static_cast<int>(pfd->planes[i].hSubsampling);
                stage.planeVSub[i] = static_cast<int>(pfd->planes[i].vSubsampling);
                stage.planeBytesPerSample[i] = static_cast<int>(pfd->planes[i].bytesPerSample);
        }

        if(pfd->planeCount == 1) {
                stage.func = kernelUnpackInterleaved;
        } else if(pfd->planeCount >= 3) {
                stage.func = kernelUnpackPlanar;
        } else if(pfd->planeCount == 2) {
                stage.func = kernelUnpackSemiPlanar;
        }
        return;
}

void CSCPipeline::buildPackStage(const PixelFormat &pd, Stage &stage) {
        stage.type = StagePack;
        const PixelMemLayout &pf = pd.memLayout();
        const PixelMemLayout::Data *pfd = pf.data();

        stage.pixelCompCount = static_cast<int>(pfd->compCount);
        stage.bitsPerComp = static_cast<int>(pfd->comps[0].bits);
        stage.bytesPerBlock = static_cast<int>(pfd->bytesPerBlock);
        stage.pixelsPerBlock = static_cast<int>(pfd->pixelsPerBlock);
        stage.hasAlpha = pd.hasAlpha();
        stage.alphaCompIndex = pd.alphaCompIndex();
        stage.planeCount = static_cast<int>(pfd->planeCount);

        for(size_t i = 0; i < pfd->compCount; i++) {
                stage.compPlane[i] = pfd->comps[i].plane;
                stage.compByteOffset[i] = static_cast<int>(pfd->comps[i].byteOffset);
                stage.compBits[i] = static_cast<int>(pfd->comps[i].bits);
        }

        computeSemBufMap(pd, stage.semBufMap);

        for(size_t i = 0; i < pfd->planeCount; i++) {
                stage.planeHSub[i] = static_cast<int>(pfd->planes[i].hSubsampling);
                stage.planeVSub[i] = static_cast<int>(pfd->planes[i].vSubsampling);
                stage.planeBytesPerSample[i] = static_cast<int>(pfd->planes[i].bytesPerSample);
        }

        if(pfd->planeCount == 1) {
                stage.func = kernelPackInterleaved;
        } else if(pfd->planeCount >= 3) {
                stage.func = kernelPackPlanar;
        } else if(pfd->planeCount == 2) {
                stage.func = kernelPackSemiPlanar;
        }
        return;
}

void CSCPipeline::buildRangeStage(const PixelFormat &pd, Stage &stage, bool isInput) {
        const PixelFormat::Data *pdd = pd.data();
        int cc = static_cast<int>(pd.memLayout().compCount());

        // Determine color component count (excluding alpha)
        int colorComps = pd.hasAlpha() ? cc - 1 : cc;
        stage.compCount = colorComps;

        if(isInput) {
                // Input: map from [rangeMin, rangeMax] to [0.0, 1.0]
                stage.type = StageRangeIn;
                for(int c = 0; c < colorComps && c < 3; c++) {
                        int semIdx = c;
                        // Skip alpha component index
                        if(pd.hasAlpha() && c >= pd.alphaCompIndex()) semIdx = c + 1;
                        if(semIdx < cc) {
                                float rMin = pdd->compSemantics[semIdx].rangeMin;
                                float rMax = pdd->compSemantics[semIdx].rangeMax;
                                if(rMax > rMin) {
                                        stage.rangeScale[c] = 1.0f / (rMax - rMin);
                                        stage.rangeBias[c] = -rMin / (rMax - rMin);
                                } else {
                                        stage.rangeScale[c] = 1.0f;
                                        stage.rangeBias[c] = 0.0f;
                                }
                        }
                }
        } else {
                // Output: map from [0.0, 1.0] to [rangeMin, rangeMax]
                stage.type = StageRangeOut;
                for(int c = 0; c < colorComps && c < 3; c++) {
                        int semIdx = c;
                        if(pd.hasAlpha() && c >= pd.alphaCompIndex()) semIdx = c + 1;
                        if(semIdx < cc) {
                                float rMin = pdd->compSemantics[semIdx].rangeMin;
                                float rMax = pdd->compSemantics[semIdx].rangeMax;
                                stage.rangeScale[c] = rMax - rMin;
                                stage.rangeBias[c] = rMin;
                        }
                }
        }

        stage.func = kernelRangeMap;
        return;
}

void CSCPipeline::buildTransferStage(const ColorModel &cm, Stage &stage, bool isEOTF, int bits) {
        stage.type = isEOTF ? StageEOTF : StageOETF;
        stage.compCount = 3;

        const ColorModel::Data *cmd = cm.data();

        // Build LUT for integer bit depths up to 12 bits
        if(bits > 0 && bits <= 12) {
                size_t lutEntries = 1u << bits;
                stage.lut = new float[lutEntries];
                stage.lutSize = lutEntries;

                auto transferFunc = isEOTF ? cmd->eotf : cmd->oetf;
                if(transferFunc) {
                        for(size_t i = 0; i < lutEntries; i++) {
                                double normalized = static_cast<double>(i) / (lutEntries - 1);
                                stage.lut[i] = static_cast<float>(transferFunc(normalized));
                        }
                } else {
                        // Identity
                        for(size_t i = 0; i < lutEntries; i++) {
                                stage.lut[i] = static_cast<float>(i) / (lutEntries - 1);
                        }
                }
                stage.func = kernelTransferLUT;
        } else {
                // For >12 bit or float: will need direct compute (Phase 2)
                // For now, use LUT with 4096 entries as approximation
                stage.lut = new float[MaxLUTSize];
                stage.lutSize = MaxLUTSize;
                auto transferFunc = isEOTF ? cmd->eotf : cmd->oetf;
                if(transferFunc) {
                        for(size_t i = 0; i < MaxLUTSize; i++) {
                                double normalized = static_cast<double>(i) / (MaxLUTSize - 1);
                                stage.lut[i] = static_cast<float>(transferFunc(normalized));
                        }
                } else {
                        for(size_t i = 0; i < MaxLUTSize; i++) {
                                stage.lut[i] = static_cast<float>(i) / (MaxLUTSize - 1);
                        }
                }
                stage.func = kernelTransferLUT;
        }
        return;
}

// Helper: check if two color models share the same CIE primaries
static bool samePrimaries(const ColorModel &a, const ColorModel &b) {
        const auto &pa = a.primaries();
        const auto &pb = b.primaries();
        for(int i = 0; i < 4; i++) {
                if(std::abs(pa[i].x() - pb[i].x()) > 1e-6 ||
                   std::abs(pa[i].y() - pb[i].y()) > 1e-6) {
                        return false;
                }
        }
        return true;
}

// Helper: get the "base RGB" model for a color model (strips YCbCr/HSV/HSL to parent)
static ColorModel baseRGBModel(const ColorModel &cm) {
        const ColorModel::Data *d = cm.data();
        if(d->type == ColorModel::TypeYCbCr || d->type == ColorModel::TypeHSV ||
           d->type == ColorModel::TypeHSL) {
                return cm.parentModel();
        }
        return cm;
}

// Helper: get the linear variant of a model
static ColorModel linearVariant(const ColorModel &cm) {
        if(cm.isLinear()) return cm;
        ColorModel lin = cm.linearCounterpart();
        return lin.isValid() ? lin : cm;
}

// Build a YCbCr -> encoded RGB matrix stage (toParentMatrix + toParentOffset)
static void buildYCbCrToRGBStage(const ColorModel::Data *cmd, CSCPipeline::Stage &stage) {
        stage.type = CSCPipeline::StageMatrix;
        stage.func = kernelMatrix;
        std::memcpy(stage.matrixPreOffset, cmd->toParentOffset, sizeof(stage.matrixPreOffset));
        std::memset(stage.matrixOffset, 0, sizeof(stage.matrixOffset));
        for(int i = 0; i < 3; i++)
                for(int j = 0; j < 3; j++)
                        stage.matrix[i][j] = cmd->toParentMatrix.get(i, j);
}

// Build an encoded RGB -> YCbCr matrix stage (fromParentMatrix + fromParentOffset)
static void buildRGBToYCbCrStage(const ColorModel::Data *cmd, CSCPipeline::Stage &stage) {
        stage.type = CSCPipeline::StageMatrix;
        stage.func = kernelMatrix;
        std::memset(stage.matrixPreOffset, 0, sizeof(stage.matrixPreOffset));
        std::memcpy(stage.matrixOffset, cmd->fromParentOffset, sizeof(stage.matrixOffset));
        for(int i = 0; i < 3; i++)
                for(int j = 0; j < 3; j++)
                        stage.matrix[i][j] = cmd->fromParentMatrix.get(i, j);
}

void CSCPipeline::buildMatrixStage(const ColorModel &src, const ColorModel &dst, Stage &stage) {
        stage.type = StageMatrix;
        stage.func = kernelMatrix;

        // Zero out offsets
        std::memset(stage.matrixPreOffset, 0, sizeof(stage.matrixPreOffset));
        std::memset(stage.matrixOffset, 0, sizeof(stage.matrixOffset));

        // Pure gamut conversion in linear space: dstInvNPM * srcNPM
        ColorModel srcRGB = baseRGBModel(src);
        ColorModel dstRGB = baseRGBModel(dst);

        ColorModel srcLin = linearVariant(srcRGB);
        ColorModel dstLin = linearVariant(dstRGB);
        Matrix3x3 gamutMatrix = dstLin.data()->xyzToRgb * srcLin.data()->rgbToXyz;

        for(int i = 0; i < 3; i++)
                for(int j = 0; j < 3; j++)
                        stage.matrix[i][j] = gamutMatrix.get(i, j);
        return;
}

static bool isMatrixIdentity(const float m[3][3], const float pre[3], const float post[3]) {
        const float eps = 1e-6f;
        for(int i = 0; i < 3; i++) {
                if(std::abs(pre[i]) > eps || std::abs(post[i]) > eps) return false;
                for(int j = 0; j < 3; j++) {
                        float expected = (i == j) ? 1.0f : 0.0f;
                        if(std::abs(m[i][j] - expected) > eps) return false;
                }
        }
        return true;
}

void CSCPipeline::compile() {
        _valid = false;
        _identity = false;
        _fastPathFunc = nullptr;
        _stages.clear();

        if(!_srcDesc.isValid() || !_dstDesc.isValid()) return;
        if(_srcDesc.isCompressed() || _dstDesc.isCompressed()) return;

        // Identity check
        if(_srcDesc == _dstDesc) {
                _identity = true;
                _valid = true;
                return;
        }

        // Check fast-path registry (skip in scalar mode)
        if(_useSimd) {
                _fastPathFunc = CSCRegistry::lookupFastPath(_srcDesc, _dstDesc);
                if(_fastPathFunc) {
                        _valid = true;
                        return;
                }
        }

        const ColorModel &srcCM = _srcDesc.colorModel();
        const ColorModel &dstCM = _dstDesc.colorModel();
        const ColorModel::Data *srcCMD = srcCM.data();
        const ColorModel::Data *dstCMD = dstCM.data();

        int srcBits = static_cast<int>(_srcDesc.memLayout().data()->comps[0].bits);
        int dstBits = static_cast<int>(_dstDesc.memLayout().data()->comps[0].bits);

        bool srcIsYCbCr = (srcCMD->type == ColorModel::TypeYCbCr);
        bool dstIsYCbCr = (dstCMD->type == ColorModel::TypeYCbCr);

        // Resolve transfer functions from parent RGB for YCbCr models
        const ColorModel::Data *srcTransferCM = srcCMD;
        const ColorModel::Data *dstTransferCM = dstCMD;
        if(srcIsYCbCr) {
                srcTransferCM = srcCM.parentModel().data();
        }
        if(dstIsYCbCr) {
                dstTransferCM = dstCM.parentModel().data();
        }

        bool srcLinear = srcTransferCM->linear;
        bool dstLinear = dstTransferCM->linear;
        bool sameTransfer = (srcTransferCM->oetf == dstTransferCM->oetf &&
                             srcTransferCM->eotf == dstTransferCM->eotf);

        // Check if gamut conversion is needed (different RGB primaries)
        ColorModel srcRGB = baseRGBModel(srcCM);
        ColorModel dstRGB = baseRGBModel(dstCM);
        bool needGamut = !samePrimaries(srcRGB, dstRGB);

        // Transfer functions needed if gamut changes or transfer functions differ
        bool needEOTF = !srcLinear && (needGamut || !sameTransfer);
        bool needOETF = !dstLinear && (needGamut || !sameTransfer);

        // --- Stage 1: Unpack source ---
        {
                Stage s;
                buildUnpackStage(_srcDesc, s);
                _stages.pushToBack(std::move(s));
        }

        // --- Stage 2: Alpha fill (if target needs alpha but source doesn't have it) ---
        if(_dstDesc.hasAlpha() && !_srcDesc.hasAlpha()) {
                Stage s;
                s.type = StageAlphaFill;
                s.func = kernelAlphaFill;
                // Fill with max value for target bit depth (opaque alpha)
                s.alphaValue = static_cast<float>((1 << dstBits) - 1);
                _stages.pushToBack(std::move(s));
        }

        // --- Stage 3: Range input (limited -> normalized 0-1) ---
        {
                Stage s;
                buildRangeStage(_srcDesc, s, true);
                _stages.pushToBack(std::move(s));
        }

        // --- Stage 3b: Mono expansion ---
        //
        // When the source is a single-color-component (mono / grayscale)
        // format and the destination needs more color components, the
        // unpack stage leaves buffers 1 and 2 untouched — so without
        // this stage, later stages (gamut, OETF, range-out, pack) would
        // read zero/garbage for G and B and produce an all-red image.
        // Broadcast the normalized luma from buffer[0] into buffers 1
        // and 2 so every downstream stage sees three identical grays.
        {
                int srcCC = static_cast<int>(_srcDesc.memLayout().compCount());
                int srcColorComps = _srcDesc.hasAlpha() ? srcCC - 1 : srcCC;
                int dstCC = static_cast<int>(_dstDesc.memLayout().compCount());
                int dstColorComps = _dstDesc.hasAlpha() ? dstCC - 1 : dstCC;
                if(srcColorComps == 1 && dstColorComps >= 3) {
                        Stage s;
                        s.type = StageMonoExpand;
                        s.func = kernelMonoExpand;
                        _stages.pushToBack(std::move(s));
                }
        }

        // --- Stage 4: YCbCr -> encoded RGB (if source is YCbCr) ---
        // The YCbCr matrix operates on encoded (gamma-corrected) values
        if(srcIsYCbCr) {
                Stage s;
                buildYCbCrToRGBStage(srcCMD, s);
                _stages.pushToBack(std::move(s));
        }

        // --- Stage 5: EOTF (remove source transfer -> linear RGB) ---
        if(needEOTF && srcTransferCM->eotf) {
                Stage s;
                buildTransferStage(ColorModel(srcTransferCM->id), s, true, srcBits);
                _stages.pushToBack(std::move(s));
        }

        // --- Stage 6: Gamut matrix (linear space conversion) ---
        if(needGamut) {
                Stage s;
                buildMatrixStage(srcCM, dstCM, s);
                if(!isMatrixIdentity(s.matrix, s.matrixPreOffset, s.matrixOffset)) {
                        _stages.pushToBack(std::move(s));
                }
        }

        // --- Stage 7: OETF (linear -> encoded RGB) ---
        if(needOETF && dstTransferCM->oetf) {
                Stage s;
                buildTransferStage(ColorModel(dstTransferCM->id), s, false, dstBits);
                _stages.pushToBack(std::move(s));
        }

        // --- Stage 8: encoded RGB -> YCbCr (if target is YCbCr) ---
        if(dstIsYCbCr) {
                Stage s;
                buildRGBToYCbCrStage(dstCMD, s);
                _stages.pushToBack(std::move(s));
        }

        // --- Stage 9: Range output (normalized 0-1 -> target range) ---
        {
                Stage s;
                buildRangeStage(_dstDesc, s, false);
                _stages.pushToBack(std::move(s));
        }

        // --- Stage 10: Pack target ---
        {
                Stage s;
                buildPackStage(_dstDesc, s);
                _stages.pushToBack(std::move(s));
        }

        // Apply useSimd flag to all stages
        for(auto &s : _stages) s.useSimd = _useSimd;

        _valid = true;
        return;
}

// --- Execution ---

void CSCPipeline::processLine(const void *const *srcPlanes,
                              const size_t *srcStrides,
                              void *const *dstPlanes,
                              const size_t *dstStrides,
                              size_t width, size_t y,
                              CSCContext &ctx) const {
        for(const auto &stage : _stages) {
                stage.func(&stage, srcPlanes, srcStrides, dstPlanes, dstStrides,
                           width, y, ctx);
        }
        return;
}

Error CSCPipeline::execute(const Image &src, Image &dst) const {
        if(!_valid) return Error::Invalid;
        if(!src.isValid() || !dst.isValid()) return Error::Invalid;

        size_t width = src.width();
        size_t height = src.height();

        if(dst.width() != width || dst.height() != height) return Error::Invalid;

        // Identity: copy planes directly
        if(_identity) {
                int planeCount = static_cast<int>(src.pixelFormat().planeCount());
                for(int p = 0; p < planeCount; p++) {
                        size_t planeSize = src.plane(p)->size();
                        std::memcpy(dst.data(p), src.data(p), planeSize);
                }
                return Error::Ok;
        }

        // Fast path
        if(_fastPathFunc) {
                int srcPlaneCount = static_cast<int>(_srcDesc.planeCount());
                int dstPlaneCount = static_cast<int>(_dstDesc.planeCount());

                CSCContext ctx(width);
                if(!ctx.isValid()) return Error::NoMem;

                for(size_t y = 0; y < height; y++) {
                        const void *srcLinePtrs[4] = {};
                        size_t srcStrides[4] = {};
                        void *dstLinePtrs[4] = {};
                        size_t dstStrides[4] = {};

                        for(int p = 0; p < srcPlaneCount; p++) {
                                size_t stride = src.lineStride(p);
                                const PixelMemLayout::PlaneDesc &pd = _srcDesc.memLayout().planeDesc(p);
                                size_t planeY = y / pd.vSubsampling;
                                srcLinePtrs[p] = static_cast<const uint8_t *>(src.data(p)) + planeY * stride;
                                srcStrides[p] = stride;
                        }
                        for(int p = 0; p < dstPlaneCount; p++) {
                                size_t stride = dst.lineStride(p);
                                const PixelMemLayout::PlaneDesc &pd = _dstDesc.memLayout().planeDesc(p);
                                size_t planeY = y / pd.vSubsampling;
                                dstLinePtrs[p] = static_cast<uint8_t *>(dst.data(p)) + planeY * stride;
                                dstStrides[p] = stride;
                        }

                        _fastPathFunc(srcLinePtrs, srcStrides, dstLinePtrs, dstStrides,
                                      width, ctx);
                }
                return Error::Ok;
        }

        // General pipeline
        int srcPlaneCount = static_cast<int>(_srcDesc.planeCount());
        int dstPlaneCount = static_cast<int>(_dstDesc.planeCount());

        CSCContext ctx(width);
        if(!ctx.isValid()) return Error::NoMem;

        for(size_t y = 0; y < height; y++) {
                const void *srcLinePtrs[4] = {};
                size_t srcStrides[4] = {};
                void *dstLinePtrs[4] = {};
                size_t dstStrides[4] = {};

                for(int p = 0; p < srcPlaneCount; p++) {
                        size_t stride = src.lineStride(p);
                        const PixelMemLayout::PlaneDesc &pd = _srcDesc.memLayout().planeDesc(p);
                        size_t planeY = y / pd.vSubsampling;
                        srcLinePtrs[p] = static_cast<const uint8_t *>(src.data(p)) + planeY * stride;
                        srcStrides[p] = stride;
                }
                for(int p = 0; p < dstPlaneCount; p++) {
                        size_t stride = dst.lineStride(p);
                        const PixelMemLayout::PlaneDesc &pd = _dstDesc.memLayout().planeDesc(p);
                        size_t planeY = y / pd.vSubsampling;
                        dstLinePtrs[p] = static_cast<uint8_t *>(dst.data(p)) + planeY * stride;
                        dstStrides[p] = stride;
                }

                processLine(srcLinePtrs, srcStrides, dstLinePtrs, dstStrides,
                            width, y, ctx);
        }

        return Error::Ok;
}

PROMEKI_NAMESPACE_END
