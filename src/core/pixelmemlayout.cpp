/**
 * @file      pixelmemlayout.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pixelmemlayout.h>
#include <promeki/atomic.h>
#include <promeki/map.h>
#include <promeki/util.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(PixelMemLayout)

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered types
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{PixelMemLayout::UserDefined};

PixelMemLayout::ID PixelMemLayout::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// Stride / plane-size helpers shared by several formats
// ---------------------------------------------------------------------------

static size_t interleavedLineStride(const PixelMemLayout::Data *d, size_t /*planeIdx*/, size_t width, size_t linePad,
                                    size_t lineAlign) {
        size_t lineBytes = (width * d->bytesPerBlock) / d->pixelsPerBlock + linePad;
        return PROMEKI_ALIGN_UP(lineBytes, lineAlign);
}

static size_t interleavedPlaneSize(const PixelMemLayout::Data *d, size_t planeIdx, size_t width, size_t height,
                                   size_t linePad, size_t lineAlign) {
        return interleavedLineStride(d, planeIdx, width, linePad, lineAlign) * height;
}

// ---------------------------------------------------------------------------
// Factory functions for well-known pixel formats
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeInvalid() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid pixel format";
        return d;
}

static PixelMemLayout::Data makeInterleaved4x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_4x8;
        d.name = "4x8";
        d.desc = "4 components, 8 bits each, 1 interleaved plane";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 4;
        d.compCount = 4;
        d.comps[0] = {0, 8, 0};
        d.comps[1] = {0, 8, 1};
        d.comps[2] = {0, 8, 2};
        d.comps[3] = {0, 8, 3};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved3x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_3x8;
        d.name = "3x8";
        d.desc = "3 components, 8 bits each, 1 interleaved plane";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 3;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0};
        d.comps[1] = {0, 8, 1};
        d.comps[2] = {0, 8, 2};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved3x10() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_3x10_DPX;
        d.name = "3x10_DPX";
        d.desc = "3 components, 10 bits each, 1 interleaved plane";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 4; // 30 bits packed into 4 bytes
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {0, 10, 1};
        d.comps[2] = {0, 10, 2};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved3x10_DPX_B() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_3x10_DPX_B;
        d.name = "3x10_DPX_B";
        d.desc = "3 components, 10 bits each, DPX Method B packed";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 4;
        d.compCount = 3;
        d.comps[0] = {0, 10, 3};
        d.comps[1] = {0, 10, 3};
        d.comps[2] = {0, 10, 3};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved422_3x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_422_3x8;
        d.name = "422_3x8";
        d.desc = "3 components, 8 bits, 4:2:2 subsampled, 1 interleaved plane";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock = 4;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0};
        d.comps[1] = {0, 8, 1};
        d.comps[2] = {0, 8, 3};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved422_3x10() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_422_3x10;
        d.name = "422_3x10";
        d.desc = "3 components, 10 bits, 4:2:2 subsampled, 1 interleaved plane";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock = 8;
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {0, 10, 2};
        d.comps[2] = {0, 10, 6};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleavedUYVY3x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_422_UYVY_3x8;
        d.name = "422_UYVY_3x8";
        d.desc = "3 components, 8 bits, 4:2:2 UYVY, 1 interleaved plane";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock = 4; // Cb Y0 Cr Y1
        d.compCount = 3;
        d.comps[0] = {0, 8, 1}; // Y  at byte 1
        d.comps[1] = {0, 8, 0}; // Cb at byte 0
        d.comps[2] = {0, 8, 2}; // Cr at byte 2
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleavedUYVY3x10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_422_UYVY_3x10_LE;
        d.name = "422_UYVY_3x10_LE";
        d.desc = "3 components, 10 bits in 16-bit LE words, 4:2:2 UYVY";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock = 8; // Cb(2) Y0(2) Cr(2) Y1(2)
        d.compCount = 3;
        d.comps[0] = {0, 10, 2}; // Y  at byte 2
        d.comps[1] = {0, 10, 0}; // Cb at byte 0
        d.comps[2] = {0, 10, 4}; // Cr at byte 4
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleavedUYVY3x10BE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_422_UYVY_3x10_BE;
        d.name = "422_UYVY_3x10_BE";
        d.desc = "3 components, 10 bits in 16-bit BE words, 4:2:2 UYVY";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock = 8;
        d.compCount = 3;
        d.comps[0] = {0, 10, 2};
        d.comps[1] = {0, 10, 0};
        d.comps[2] = {0, 10, 4};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleavedUYVY3x12LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_422_UYVY_3x12_LE;
        d.name = "422_UYVY_3x12_LE";
        d.desc = "3 components, 12 bits in 16-bit LE words, 4:2:2 UYVY";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock = 8;
        d.compCount = 3;
        d.comps[0] = {0, 12, 2};
        d.comps[1] = {0, 12, 0};
        d.comps[2] = {0, 12, 4};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleavedUYVY3x12BE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_422_UYVY_3x12_BE;
        d.name = "422_UYVY_3x12_BE";
        d.desc = "3 components, 12 bits in 16-bit BE words, 4:2:2 UYVY";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock = 8;
        d.compCount = 3;
        d.comps[0] = {0, 12, 2};
        d.comps[1] = {0, 12, 0};
        d.comps[2] = {0, 12, 4};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

// ---------------------------------------------------------------------------
// v210 stride / plane-size helpers
// ---------------------------------------------------------------------------

static size_t v210LineStride(const PixelMemLayout::Data * /*d*/, size_t /*planeIdx*/, size_t width, size_t linePad,
                             size_t lineAlign) {
        // v210: 6 pixels per 16 bytes, lines padded to 128-byte boundary
        size_t lineBytes = ((width + 5) / 6) * 16 + linePad;
        lineAlign = std::max(lineAlign, size_t(128));
        return PROMEKI_ALIGN_UP(lineBytes, lineAlign);
}

static size_t v210PlaneSize(const PixelMemLayout::Data *d, size_t planeIdx, size_t width, size_t height, size_t linePad,
                            size_t lineAlign) {
        return v210LineStride(d, planeIdx, width, linePad, lineAlign) * height;
}

static PixelMemLayout::Data makeInterleavedV210() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_422_v210;
        d.name = "422_v210";
        d.desc = "3 components, 10 bits, 4:2:2 v210 packed (3x10 in 32-bit words)";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.pixelsPerBlock = 6;
        d.bytesPerBlock = 16; // 4 x 32-bit words for 6 pixels
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {0, 10, 0};
        d.comps[2] = {0, 10, 0};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = v210LineStride;
        d.planeSizeFunc = v210PlaneSize;
        return d;
}

// ---------------------------------------------------------------------------
// Planar stride / plane-size helpers
// ---------------------------------------------------------------------------

static size_t planarLineStride(const PixelMemLayout::Data *d, size_t planeIdx, size_t width, size_t linePad,
                               size_t lineAlign) {
        const auto &p = d->planes[planeIdx];
        size_t      lineBytes = (width / p.hSubsampling) * p.bytesPerSample + linePad;
        return PROMEKI_ALIGN_UP(lineBytes, lineAlign);
}

static size_t planarPlaneSize(const PixelMemLayout::Data *d, size_t planeIdx, size_t width, size_t height,
                              size_t linePad, size_t lineAlign) {
        const auto &p = d->planes[planeIdx];
        // Ceiling division on the row count: a sub-sampled chroma
        // plane must have enough rows to receive every output the CSC
        // pipeline (and other per-luma-row writers) will produce, even
        // when the luma height is not an exact multiple of the chroma
        // vSubsampling.  For example, a 1-row 4:2:0 image needs *one*
        // chroma row, not zero — the CSC iterates `for(y = 0; y < 1)`
        // and writes chroma row `0/2 = 0`.  Floor division would
        // allocate zero chroma rows and the write would corrupt
        // adjacent heap allocations.  This same fix also covers odd
        // luma heights for arbitrary 4:2:0 / 4:2:2 / 4:1:1 inputs.
        const size_t vSub = p.vSubsampling > 0 ? p.vSubsampling : 1;
        const size_t chromaRows = (height + vSub - 1) / vSub;
        return planarLineStride(d, planeIdx, width, linePad, lineAlign) * chromaRows;
}

// ---------------------------------------------------------------------------
// Planar 4:2:2 factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makePlanar422_3x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::P_422_3x8;
        d.name = "Planar_422_3x8";
        d.desc = "3 planes, 8-bit, 4:2:2";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0};
        d.comps[1] = {1, 8, 0};
        d.comps[2] = {2, 8, 0};
        d.planeCount = 3;
        d.planes[0] = {"Y", 1, 1, 1};
        d.planes[1] = {"Cb", 2, 1, 1};
        d.planes[2] = {"Cr", 2, 1, 1};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makePlanar422_3x10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::P_422_3x10_LE;
        d.name = "Planar_422_3x10_LE";
        d.desc = "3 planes, 10-bit in 16-bit LE words, 4:2:2";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {1, 10, 0};
        d.comps[2] = {2, 10, 0};
        d.planeCount = 3;
        d.planes[0] = {"Y", 1, 1, 2};
        d.planes[1] = {"Cb", 2, 1, 2};
        d.planes[2] = {"Cr", 2, 1, 2};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makePlanar422_3x10BE() {
        PixelMemLayout::Data d = makePlanar422_3x10LE();
        d.id = PixelMemLayout::P_422_3x10_BE;
        d.name = "Planar_422_3x10_BE";
        d.desc = "3 planes, 10-bit in 16-bit BE words, 4:2:2";
        return d;
}

static PixelMemLayout::Data makePlanar422_3x12LE() {
        PixelMemLayout::Data d = makePlanar422_3x10LE();
        d.id = PixelMemLayout::P_422_3x12_LE;
        d.name = "Planar_422_3x12_LE";
        d.desc = "3 planes, 12-bit in 16-bit LE words, 4:2:2";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelMemLayout::Data makePlanar422_3x12BE() {
        PixelMemLayout::Data d = makePlanar422_3x12LE();
        d.id = PixelMemLayout::P_422_3x12_BE;
        d.name = "Planar_422_3x12_BE";
        d.desc = "3 planes, 12-bit in 16-bit BE words, 4:2:2";
        return d;
}

// ---------------------------------------------------------------------------
// Planar 4:2:0 factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makePlanar420_3x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::P_420_3x8;
        d.name = "Planar_420_3x8";
        d.desc = "3 planes, 8-bit, 4:2:0";
        d.sampling = PixelMemLayout::Sampling420;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVCenter;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0};
        d.comps[1] = {1, 8, 0};
        d.comps[2] = {2, 8, 0};
        d.planeCount = 3;
        d.planes[0] = {"Y", 1, 1, 1};
        d.planes[1] = {"Cb", 2, 2, 1};
        d.planes[2] = {"Cr", 2, 2, 1};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makePlanar420_3x10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::P_420_3x10_LE;
        d.name = "Planar_420_3x10_LE";
        d.desc = "3 planes, 10-bit in 16-bit LE words, 4:2:0";
        d.sampling = PixelMemLayout::Sampling420;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVCenter;
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {1, 10, 0};
        d.comps[2] = {2, 10, 0};
        d.planeCount = 3;
        d.planes[0] = {"Y", 1, 1, 2};
        d.planes[1] = {"Cb", 2, 2, 2};
        d.planes[2] = {"Cr", 2, 2, 2};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makePlanar420_3x10BE() {
        PixelMemLayout::Data d = makePlanar420_3x10LE();
        d.id = PixelMemLayout::P_420_3x10_BE;
        d.name = "Planar_420_3x10_BE";
        d.desc = "3 planes, 10-bit in 16-bit BE words, 4:2:0";
        return d;
}

static PixelMemLayout::Data makePlanar420_3x12LE() {
        PixelMemLayout::Data d = makePlanar420_3x10LE();
        d.id = PixelMemLayout::P_420_3x12_LE;
        d.name = "Planar_420_3x12_LE";
        d.desc = "3 planes, 12-bit in 16-bit LE words, 4:2:0";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelMemLayout::Data makePlanar420_3x12BE() {
        PixelMemLayout::Data d = makePlanar420_3x12LE();
        d.id = PixelMemLayout::P_420_3x12_BE;
        d.name = "Planar_420_3x12_BE";
        d.desc = "3 planes, 12-bit in 16-bit BE words, 4:2:0";
        return d;
}

// ---------------------------------------------------------------------------
// Semi-planar 4:2:0 (NV12) factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeSemiPlanar420_8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::SP_420_8;
        d.name = "SemiPlanar_420_8";
        d.desc = "2 planes, 8-bit, 4:2:0 NV12 (Y + interleaved CbCr)";
        d.sampling = PixelMemLayout::Sampling420;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVCenter;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0}; // Y in plane 0
        d.comps[1] = {1, 8, 0}; // Cb in plane 1, byte 0
        d.comps[2] = {1, 8, 1}; // Cr in plane 1, byte 1
        d.planeCount = 2;
        d.planes[0] = {"Y", 1, 1, 1};
        d.planes[1] = {"CbCr", 2, 2, 2}; // half-width chroma positions, 2 bytes each (Cb+Cr), half height
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420_10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::SP_420_10_LE;
        d.name = "SemiPlanar_420_10_LE";
        d.desc = "2 planes, 10-bit in 16-bit LE words, 4:2:0 NV12";
        d.sampling = PixelMemLayout::Sampling420;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVCenter;
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {1, 10, 0};
        d.comps[2] = {1, 10, 2};
        d.planeCount = 2;
        d.planes[0] = {"Y", 1, 1, 2};
        d.planes[1] = {"CbCr", 2, 2, 4}; // half-width chroma positions, 4 bytes each (Cb+Cr in 16-bit words)
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420_10BE() {
        PixelMemLayout::Data d = makeSemiPlanar420_10LE();
        d.id = PixelMemLayout::SP_420_10_BE;
        d.name = "SemiPlanar_420_10_BE";
        d.desc = "2 planes, 10-bit in 16-bit BE words, 4:2:0 NV12";
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420_12LE() {
        PixelMemLayout::Data d = makeSemiPlanar420_10LE();
        d.id = PixelMemLayout::SP_420_12_LE;
        d.name = "SemiPlanar_420_12_LE";
        d.desc = "2 planes, 12-bit in 16-bit LE words, 4:2:0 NV12";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420_12BE() {
        PixelMemLayout::Data d = makeSemiPlanar420_12LE();
        d.id = PixelMemLayout::SP_420_12_BE;
        d.name = "SemiPlanar_420_12_BE";
        d.desc = "2 planes, 12-bit in 16-bit BE words, 4:2:0 NV12";
        return d;
}

// ---------------------------------------------------------------------------
// Interleaved 4:4:4 (10/12/16-bit in 16-bit words) factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeInterleaved4x10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_4x10_LE;
        d.name = "4x10_LE";
        d.desc = "4 components, 10 bits in 16-bit LE words";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 8;
        d.compCount = 4;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {0, 10, 2};
        d.comps[2] = {0, 10, 4};
        d.comps[3] = {0, 10, 6};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved4x10BE() {
        PixelMemLayout::Data d = makeInterleaved4x10LE();
        d.id = PixelMemLayout::I_4x10_BE;
        d.name = "4x10_BE";
        d.desc = "4 components, 10 bits in 16-bit BE words";
        return d;
}

static PixelMemLayout::Data makeInterleaved3x10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_3x10_LE;
        d.name = "3x10_LE";
        d.desc = "3 components, 10 bits in 16-bit LE words";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 6;
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {0, 10, 2};
        d.comps[2] = {0, 10, 4};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved3x10BE() {
        PixelMemLayout::Data d = makeInterleaved3x10LE();
        d.id = PixelMemLayout::I_3x10_BE;
        d.name = "3x10_BE";
        d.desc = "3 components, 10 bits in 16-bit BE words";
        return d;
}

static PixelMemLayout::Data makeInterleaved4x12LE() {
        PixelMemLayout::Data d = makeInterleaved4x10LE();
        d.id = PixelMemLayout::I_4x12_LE;
        d.name = "4x12_LE";
        d.desc = "4 components, 12 bits in 16-bit LE words";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        d.comps[3].bits = 12;
        return d;
}

static PixelMemLayout::Data makeInterleaved4x12BE() {
        PixelMemLayout::Data d = makeInterleaved4x12LE();
        d.id = PixelMemLayout::I_4x12_BE;
        d.name = "4x12_BE";
        d.desc = "4 components, 12 bits in 16-bit BE words";
        return d;
}

static PixelMemLayout::Data makeInterleaved3x12LE() {
        PixelMemLayout::Data d = makeInterleaved3x10LE();
        d.id = PixelMemLayout::I_3x12_LE;
        d.name = "3x12_LE";
        d.desc = "3 components, 12 bits in 16-bit LE words";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelMemLayout::Data makeInterleaved3x12BE() {
        PixelMemLayout::Data d = makeInterleaved3x12LE();
        d.id = PixelMemLayout::I_3x12_BE;
        d.name = "3x12_BE";
        d.desc = "3 components, 12 bits in 16-bit BE words";
        return d;
}

static PixelMemLayout::Data makeInterleaved4x16LE() {
        PixelMemLayout::Data d = makeInterleaved4x10LE();
        d.id = PixelMemLayout::I_4x16_LE;
        d.name = "4x16_LE";
        d.desc = "4 components, 16-bit LE";
        d.comps[0].bits = 16;
        d.comps[1].bits = 16;
        d.comps[2].bits = 16;
        d.comps[3].bits = 16;
        return d;
}

static PixelMemLayout::Data makeInterleaved4x16BE() {
        PixelMemLayout::Data d = makeInterleaved4x16LE();
        d.id = PixelMemLayout::I_4x16_BE;
        d.name = "4x16_BE";
        d.desc = "4 components, 16-bit BE";
        return d;
}

static PixelMemLayout::Data makeInterleaved3x16LE() {
        PixelMemLayout::Data d = makeInterleaved3x10LE();
        d.id = PixelMemLayout::I_3x16_LE;
        d.name = "3x16_LE";
        d.desc = "3 components, 16-bit LE";
        d.comps[0].bits = 16;
        d.comps[1].bits = 16;
        d.comps[2].bits = 16;
        return d;
}

static PixelMemLayout::Data makeInterleaved3x16BE() {
        PixelMemLayout::Data d = makeInterleaved3x16LE();
        d.id = PixelMemLayout::I_3x16_BE;
        d.name = "3x16_BE";
        d.desc = "3 components, 16-bit BE";
        return d;
}

// ---------------------------------------------------------------------------
// Monochrome (single component) factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeInterleaved1x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_1x8;
        d.name = "1x8";
        d.desc = "1 component, 8 bits (1 byte/pixel)";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 1;
        d.compCount = 1;
        d.comps[0] = {0, 8, 0};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved1x10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_1x10_LE;
        d.name = "1x10_LE";
        d.desc = "1 component, 10 bits in 16-bit LE word (2 bytes/pixel)";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 2;
        d.compCount = 1;
        d.comps[0] = {0, 10, 0};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved1x10BE() {
        PixelMemLayout::Data d = makeInterleaved1x10LE();
        d.id = PixelMemLayout::I_1x10_BE;
        d.name = "1x10_BE";
        d.desc = "1 component, 10 bits in 16-bit BE word (2 bytes/pixel)";
        return d;
}

static PixelMemLayout::Data makeInterleaved1x12LE() {
        PixelMemLayout::Data d = makeInterleaved1x10LE();
        d.id = PixelMemLayout::I_1x12_LE;
        d.name = "1x12_LE";
        d.desc = "1 component, 12 bits in 16-bit LE word (2 bytes/pixel)";
        d.comps[0].bits = 12;
        return d;
}

static PixelMemLayout::Data makeInterleaved1x12BE() {
        PixelMemLayout::Data d = makeInterleaved1x12LE();
        d.id = PixelMemLayout::I_1x12_BE;
        d.name = "1x12_BE";
        d.desc = "1 component, 12 bits in 16-bit BE word (2 bytes/pixel)";
        return d;
}

static PixelMemLayout::Data makeInterleaved1x16LE() {
        PixelMemLayout::Data d = makeInterleaved1x10LE();
        d.id = PixelMemLayout::I_1x16_LE;
        d.name = "1x16_LE";
        d.desc = "1 component, 16 bits LE (2 bytes/pixel)";
        d.comps[0].bits = 16;
        return d;
}

static PixelMemLayout::Data makeInterleaved1x16BE() {
        PixelMemLayout::Data d = makeInterleaved1x16LE();
        d.id = PixelMemLayout::I_1x16_BE;
        d.name = "1x16_BE";
        d.desc = "1 component, 16 bits BE (2 bytes/pixel)";
        return d;
}

// ---------------------------------------------------------------------------
// Float half-precision (16-bit IEEE 754) factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeInterleaved4xF16LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_4xF16_LE;
        d.name = "4xF16_LE";
        d.desc = "4 components, half-float LE (8 bytes/pixel)";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 8;
        d.compCount = 4;
        d.comps[0] = {0, 16, 0};
        d.comps[1] = {0, 16, 2};
        d.comps[2] = {0, 16, 4};
        d.comps[3] = {0, 16, 6};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved4xF16BE() {
        PixelMemLayout::Data d = makeInterleaved4xF16LE();
        d.id = PixelMemLayout::I_4xF16_BE;
        d.name = "4xF16_BE";
        d.desc = "4 components, half-float BE (8 bytes/pixel)";
        return d;
}

static PixelMemLayout::Data makeInterleaved3xF16LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_3xF16_LE;
        d.name = "3xF16_LE";
        d.desc = "3 components, half-float LE (6 bytes/pixel)";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 6;
        d.compCount = 3;
        d.comps[0] = {0, 16, 0};
        d.comps[1] = {0, 16, 2};
        d.comps[2] = {0, 16, 4};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved3xF16BE() {
        PixelMemLayout::Data d = makeInterleaved3xF16LE();
        d.id = PixelMemLayout::I_3xF16_BE;
        d.name = "3xF16_BE";
        d.desc = "3 components, half-float BE (6 bytes/pixel)";
        return d;
}

static PixelMemLayout::Data makeInterleaved1xF16LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_1xF16_LE;
        d.name = "1xF16_LE";
        d.desc = "1 component, half-float LE (2 bytes/pixel)";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 2;
        d.compCount = 1;
        d.comps[0] = {0, 16, 0};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved1xF16BE() {
        PixelMemLayout::Data d = makeInterleaved1xF16LE();
        d.id = PixelMemLayout::I_1xF16_BE;
        d.name = "1xF16_BE";
        d.desc = "1 component, half-float BE (2 bytes/pixel)";
        return d;
}

// ---------------------------------------------------------------------------
// Float single-precision (32-bit IEEE 754) factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeInterleaved4xF32LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_4xF32_LE;
        d.name = "4xF32_LE";
        d.desc = "4 components, float LE (16 bytes/pixel)";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 16;
        d.compCount = 4;
        d.comps[0] = {0, 32, 0};
        d.comps[1] = {0, 32, 4};
        d.comps[2] = {0, 32, 8};
        d.comps[3] = {0, 32, 12};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved4xF32BE() {
        PixelMemLayout::Data d = makeInterleaved4xF32LE();
        d.id = PixelMemLayout::I_4xF32_BE;
        d.name = "4xF32_BE";
        d.desc = "4 components, float BE (16 bytes/pixel)";
        return d;
}

static PixelMemLayout::Data makeInterleaved3xF32LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_3xF32_LE;
        d.name = "3xF32_LE";
        d.desc = "3 components, float LE (12 bytes/pixel)";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 12;
        d.compCount = 3;
        d.comps[0] = {0, 32, 0};
        d.comps[1] = {0, 32, 4};
        d.comps[2] = {0, 32, 8};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved3xF32BE() {
        PixelMemLayout::Data d = makeInterleaved3xF32LE();
        d.id = PixelMemLayout::I_3xF32_BE;
        d.name = "3xF32_BE";
        d.desc = "3 components, float BE (12 bytes/pixel)";
        return d;
}

static PixelMemLayout::Data makeInterleaved1xF32LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_1xF32_LE;
        d.name = "1xF32_LE";
        d.desc = "1 component, float LE (4 bytes/pixel)";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 4;
        d.compCount = 1;
        d.comps[0] = {0, 32, 0};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved1xF32BE() {
        PixelMemLayout::Data d = makeInterleaved1xF32LE();
        d.id = PixelMemLayout::I_1xF32_BE;
        d.name = "1xF32_BE";
        d.desc = "1 component, float BE (4 bytes/pixel)";
        return d;
}

// ---------------------------------------------------------------------------
// 10:10:10:2 packed factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeInterleaved10_10_10_2LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::I_10_10_10_2_LE;
        d.name = "10_10_10_2_LE";
        d.desc = "4 components (10+10+10+2 bits) in 32-bit LE word";
        d.sampling = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock = 4;
        d.compCount = 4;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {0, 10, 0};
        d.comps[2] = {0, 10, 0};
        d.comps[3] = {0, 2, 0};
        d.planeCount = 1;
        d.planes[0] = {""};
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc = interleavedPlaneSize;
        return d;
}

static PixelMemLayout::Data makeInterleaved10_10_10_2BE() {
        PixelMemLayout::Data d = makeInterleaved10_10_10_2LE();
        d.id = PixelMemLayout::I_10_10_10_2_BE;
        d.name = "10_10_10_2_BE";
        d.desc = "4 components (10+10+10+2 bits) in 32-bit BE word";
        return d;
}

// ---------------------------------------------------------------------------
// Semi-planar 4:2:0 NV21 (CrCb order) factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeSemiPlanar420NV21_8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::SP_420_NV21_8;
        d.name = "SemiPlanar_420_NV21_8";
        d.desc = "2 planes, 8-bit, 4:2:0 NV21 (Y + interleaved CrCb)";
        d.sampling = PixelMemLayout::Sampling420;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVCenter;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0}; // Y in plane 0
        d.comps[1] = {1, 8, 1}; // Cb in plane 1, byte 1
        d.comps[2] = {1, 8, 0}; // Cr in plane 1, byte 0
        d.planeCount = 2;
        d.planes[0] = {"Y", 1, 1, 1};
        d.planes[1] = {"CrCb", 2, 2, 2};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420NV21_10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::SP_420_NV21_10_LE;
        d.name = "SemiPlanar_420_NV21_10_LE";
        d.desc = "2 planes, 10-bit in 16-bit LE words, 4:2:0 NV21";
        d.sampling = PixelMemLayout::Sampling420;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVCenter;
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {1, 10, 2}; // Cb in plane 1, byte 2
        d.comps[2] = {1, 10, 0}; // Cr in plane 1, byte 0
        d.planeCount = 2;
        d.planes[0] = {"Y", 1, 1, 2};
        d.planes[1] = {"CrCb", 2, 2, 4};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420NV21_10BE() {
        PixelMemLayout::Data d = makeSemiPlanar420NV21_10LE();
        d.id = PixelMemLayout::SP_420_NV21_10_BE;
        d.name = "SemiPlanar_420_NV21_10_BE";
        d.desc = "2 planes, 10-bit in 16-bit BE words, 4:2:0 NV21";
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420NV21_12LE() {
        PixelMemLayout::Data d = makeSemiPlanar420NV21_10LE();
        d.id = PixelMemLayout::SP_420_NV21_12_LE;
        d.name = "SemiPlanar_420_NV21_12_LE";
        d.desc = "2 planes, 12-bit in 16-bit LE words, 4:2:0 NV21";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420NV21_12BE() {
        PixelMemLayout::Data d = makeSemiPlanar420NV21_12LE();
        d.id = PixelMemLayout::SP_420_NV21_12_BE;
        d.name = "SemiPlanar_420_NV21_12_BE";
        d.desc = "2 planes, 12-bit in 16-bit BE words, 4:2:0 NV21";
        return d;
}

// ---------------------------------------------------------------------------
// Semi-planar 4:2:2 (NV16) factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makeSemiPlanar422_8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::SP_422_8;
        d.name = "SemiPlanar_422_8";
        d.desc = "2 planes, 8-bit, 4:2:2 NV16 (Y + interleaved CbCr)";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0}; // Y in plane 0
        d.comps[1] = {1, 8, 0}; // Cb in plane 1, byte 0
        d.comps[2] = {1, 8, 1}; // Cr in plane 1, byte 1
        d.planeCount = 2;
        d.planes[0] = {"Y", 1, 1, 1};
        d.planes[1] = {"CbCr", 2, 1, 2};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar422_10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::SP_422_10_LE;
        d.name = "SemiPlanar_422_10_LE";
        d.desc = "2 planes, 10-bit in 16-bit LE words, 4:2:2 NV16";
        d.sampling = PixelMemLayout::Sampling422;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {1, 10, 0};
        d.comps[2] = {1, 10, 2};
        d.planeCount = 2;
        d.planes[0] = {"Y", 1, 1, 2};
        d.planes[1] = {"CbCr", 2, 1, 4};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar422_10BE() {
        PixelMemLayout::Data d = makeSemiPlanar422_10LE();
        d.id = PixelMemLayout::SP_422_10_BE;
        d.name = "SemiPlanar_422_10_BE";
        d.desc = "2 planes, 10-bit in 16-bit BE words, 4:2:2 NV16";
        return d;
}

static PixelMemLayout::Data makeSemiPlanar422_12LE() {
        PixelMemLayout::Data d = makeSemiPlanar422_10LE();
        d.id = PixelMemLayout::SP_422_12_LE;
        d.name = "SemiPlanar_422_12_LE";
        d.desc = "2 planes, 12-bit in 16-bit LE words, 4:2:2 NV16";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar422_12BE() {
        PixelMemLayout::Data d = makeSemiPlanar422_12LE();
        d.id = PixelMemLayout::SP_422_12_BE;
        d.name = "SemiPlanar_422_12_BE";
        d.desc = "2 planes, 12-bit in 16-bit BE words, 4:2:2 NV16";
        return d;
}

// ---------------------------------------------------------------------------
// Planar 4:1:1 factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makePlanar411_3x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::P_411_3x8;
        d.name = "Planar_411_3x8";
        d.desc = "3 planes, 8-bit, 4:1:1";
        d.sampling = PixelMemLayout::Sampling411;
        d.chromaSitingH = PixelMemLayout::ChromaHLeft;
        d.chromaSitingV = PixelMemLayout::ChromaVTop;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0};
        d.comps[1] = {1, 8, 0};
        d.comps[2] = {2, 8, 0};
        d.planeCount = 3;
        d.planes[0] = {"Y", 1, 1, 1};
        d.planes[1] = {"Cb", 4, 1, 1};
        d.planes[2] = {"Cr", 4, 1, 1};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

// ---------------------------------------------------------------------------
// Planar 4:4:4 factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makePlanar444_3x8() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::P_444_3x8;
        d.name = "Planar_444_3x8";
        d.desc = "3 planes, 8-bit, 4:4:4";
        d.sampling = PixelMemLayout::Sampling444;
        d.chromaSitingH = PixelMemLayout::ChromaHCenter;
        d.chromaSitingV = PixelMemLayout::ChromaVCenter;
        d.compCount = 3;
        d.comps[0] = {0, 8, 0};
        d.comps[1] = {1, 8, 0};
        d.comps[2] = {2, 8, 0};
        d.planeCount = 3;
        d.planes[0] = {"C0", 1, 1, 1};
        d.planes[1] = {"C1", 1, 1, 1};
        d.planes[2] = {"C2", 1, 1, 1};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

static PixelMemLayout::Data makePlanar444_3x10LE() {
        PixelMemLayout::Data d;
        d.id = PixelMemLayout::P_444_3x10_LE;
        d.name = "Planar_444_3x10_LE";
        d.desc = "3 planes, 10-bit in 16-bit LE words, 4:4:4";
        d.sampling = PixelMemLayout::Sampling444;
        d.chromaSitingH = PixelMemLayout::ChromaHCenter;
        d.chromaSitingV = PixelMemLayout::ChromaVCenter;
        d.compCount = 3;
        d.comps[0] = {0, 10, 0};
        d.comps[1] = {1, 10, 0};
        d.comps[2] = {2, 10, 0};
        d.planeCount = 3;
        d.planes[0] = {"C0", 1, 1, 2};
        d.planes[1] = {"C1", 1, 1, 2};
        d.planes[2] = {"C2", 1, 1, 2};
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc = planarPlaneSize;
        return d;
}

// ---------------------------------------------------------------------------
// 16-bit YCbCr additions factory functions
// ---------------------------------------------------------------------------

static PixelMemLayout::Data makePlanar422_3x16LE() {
        PixelMemLayout::Data d = makePlanar422_3x10LE();
        d.id = PixelMemLayout::P_422_3x16_LE;
        d.name = "Planar_422_3x16_LE";
        d.desc = "3 planes, 16-bit LE, 4:2:2";
        d.comps[0].bits = 16;
        d.comps[1].bits = 16;
        d.comps[2].bits = 16;
        return d;
}

static PixelMemLayout::Data makePlanar422_3x16BE() {
        PixelMemLayout::Data d = makePlanar422_3x16LE();
        d.id = PixelMemLayout::P_422_3x16_BE;
        d.name = "Planar_422_3x16_BE";
        d.desc = "3 planes, 16-bit BE, 4:2:2";
        return d;
}

static PixelMemLayout::Data makePlanar420_3x16LE() {
        PixelMemLayout::Data d = makePlanar420_3x10LE();
        d.id = PixelMemLayout::P_420_3x16_LE;
        d.name = "Planar_420_3x16_LE";
        d.desc = "3 planes, 16-bit LE, 4:2:0";
        d.comps[0].bits = 16;
        d.comps[1].bits = 16;
        d.comps[2].bits = 16;
        return d;
}

static PixelMemLayout::Data makePlanar420_3x16BE() {
        PixelMemLayout::Data d = makePlanar420_3x16LE();
        d.id = PixelMemLayout::P_420_3x16_BE;
        d.name = "Planar_420_3x16_BE";
        d.desc = "3 planes, 16-bit BE, 4:2:0";
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420_16LE() {
        PixelMemLayout::Data d = makeSemiPlanar420_10LE();
        d.id = PixelMemLayout::SP_420_16_LE;
        d.name = "SemiPlanar_420_16_LE";
        d.desc = "2 planes, 16-bit LE, 4:2:0 NV12";
        d.comps[0].bits = 16;
        d.comps[1].bits = 16;
        d.comps[2].bits = 16;
        return d;
}

static PixelMemLayout::Data makeSemiPlanar420_16BE() {
        PixelMemLayout::Data d = makeSemiPlanar420_16LE();
        d.id = PixelMemLayout::SP_420_16_BE;
        d.name = "SemiPlanar_420_16_BE";
        d.desc = "2 planes, 16-bit BE, 4:2:0 NV12";
        return d;
}

static PixelMemLayout::Data makeInterleavedUYVY3x16LE() {
        PixelMemLayout::Data d = makeInterleavedUYVY3x12LE();
        d.id = PixelMemLayout::I_422_UYVY_3x16_LE;
        d.name = "422_UYVY_3x16_LE";
        d.desc = "3 components, 16-bit LE, 4:2:2 UYVY";
        d.comps[0].bits = 16;
        d.comps[1].bits = 16;
        d.comps[2].bits = 16;
        return d;
}

static PixelMemLayout::Data makeInterleavedUYVY3x16BE() {
        PixelMemLayout::Data d = makeInterleavedUYVY3x16LE();
        d.id = PixelMemLayout::I_422_UYVY_3x16_BE;
        d.name = "422_UYVY_3x16_BE";
        d.desc = "3 components, 16-bit BE, 4:2:2 UYVY";
        return d;
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry
// ---------------------------------------------------------------------------

struct PixelMemLayoutRegistry {
                Map<PixelMemLayout::ID, PixelMemLayout::Data> entries;
                Map<String, PixelMemLayout::ID>               nameMap;

                PixelMemLayoutRegistry() {
                        add(makeInvalid());
                        add(makeInterleaved4x8());
                        add(makeInterleaved3x8());
                        add(makeInterleaved3x10());
                        add(makeInterleaved3x10_DPX_B());
                        add(makeInterleaved422_3x8());
                        add(makeInterleaved422_3x10());
                        add(makeInterleavedUYVY3x8());
                        add(makeInterleavedUYVY3x10LE());
                        add(makeInterleavedUYVY3x10BE());
                        add(makeInterleavedUYVY3x12LE());
                        add(makeInterleavedUYVY3x12BE());
                        add(makeInterleavedV210());
                        add(makePlanar422_3x8());
                        add(makePlanar422_3x10LE());
                        add(makePlanar422_3x10BE());
                        add(makePlanar422_3x12LE());
                        add(makePlanar422_3x12BE());
                        add(makePlanar420_3x8());
                        add(makePlanar420_3x10LE());
                        add(makePlanar420_3x10BE());
                        add(makePlanar420_3x12LE());
                        add(makePlanar420_3x12BE());
                        add(makeSemiPlanar420_8());
                        add(makeSemiPlanar420_10LE());
                        add(makeSemiPlanar420_10BE());
                        add(makeSemiPlanar420_12LE());
                        add(makeSemiPlanar420_12BE());
                        add(makeInterleaved4x10LE());
                        add(makeInterleaved4x10BE());
                        add(makeInterleaved3x10LE());
                        add(makeInterleaved3x10BE());
                        add(makeInterleaved4x12LE());
                        add(makeInterleaved4x12BE());
                        add(makeInterleaved3x12LE());
                        add(makeInterleaved3x12BE());
                        add(makeInterleaved4x16LE());
                        add(makeInterleaved4x16BE());
                        add(makeInterleaved3x16LE());
                        add(makeInterleaved3x16BE());
                        add(makeInterleaved1x8());
                        add(makeInterleaved1x10LE());
                        add(makeInterleaved1x10BE());
                        add(makeInterleaved1x12LE());
                        add(makeInterleaved1x12BE());
                        add(makeInterleaved1x16LE());
                        add(makeInterleaved1x16BE());
                        add(makeInterleaved4xF16LE());
                        add(makeInterleaved4xF16BE());
                        add(makeInterleaved3xF16LE());
                        add(makeInterleaved3xF16BE());
                        add(makeInterleaved1xF16LE());
                        add(makeInterleaved1xF16BE());
                        add(makeInterleaved4xF32LE());
                        add(makeInterleaved4xF32BE());
                        add(makeInterleaved3xF32LE());
                        add(makeInterleaved3xF32BE());
                        add(makeInterleaved1xF32LE());
                        add(makeInterleaved1xF32BE());
                        add(makeInterleaved10_10_10_2LE());
                        add(makeInterleaved10_10_10_2BE());
                        add(makeSemiPlanar420NV21_8());
                        add(makeSemiPlanar420NV21_10LE());
                        add(makeSemiPlanar420NV21_10BE());
                        add(makeSemiPlanar420NV21_12LE());
                        add(makeSemiPlanar420NV21_12BE());
                        add(makeSemiPlanar422_8());
                        add(makeSemiPlanar422_10LE());
                        add(makeSemiPlanar422_10BE());
                        add(makeSemiPlanar422_12LE());
                        add(makeSemiPlanar422_12BE());
                        add(makePlanar411_3x8());
                        add(makePlanar422_3x16LE());
                        add(makePlanar422_3x16BE());
                        add(makePlanar420_3x16LE());
                        add(makePlanar420_3x16BE());
                        add(makeSemiPlanar420_16LE());
                        add(makeSemiPlanar420_16BE());
                        add(makeInterleavedUYVY3x16LE());
                        add(makeInterleavedUYVY3x16BE());
                        add(makePlanar444_3x8());
                        add(makePlanar444_3x10LE());
                }

                void add(PixelMemLayout::Data d) {
                        PixelMemLayout::ID id = d.id;
                        // Register every name including the "Invalid" sentinel
                        // so a Variant String round-trip is lossless — see
                        // PixelMemLayout::registerData for the rationale.
                        nameMap[d.name] = id;
                        entries[id] = std::move(d);
                }
};

static PixelMemLayoutRegistry &registry() {
        static PixelMemLayoutRegistry reg;
        return reg;
}

const PixelMemLayout::Data *PixelMemLayout::lookupData(ID id) {
        auto &reg = registry();
        auto  it = reg.entries.find(id);
        if (it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void PixelMemLayout::registerData(Data &&data) {
        auto &reg = registry();
        // The "Invalid" sentinel name is registered too so a Variant
        // String round-trip (PixelMemLayout() → "Invalid" →
        // PixelMemLayout()) is lossless.  See PixelFormat::registerData
        // for the rationale; the same self-consistency rule applies
        // here.
        reg.nameMap[data.name] = data.id;
        reg.entries[data.id] = std::move(data);
}

PixelMemLayout PixelMemLayout::lookup(const String &name) {
        return lookup(name, nullptr);
}

PixelMemLayout PixelMemLayout::lookup(const String &name, Error *err) {
        auto &reg = registry();
        auto  it = reg.nameMap.find(name);
        if (it == reg.nameMap.end()) {
                if (err != nullptr) *err = Error::IdNotFound;
                return PixelMemLayout(Invalid);
        }
        if (err != nullptr) *err = Error::Ok;
        return PixelMemLayout(it->second);
}

PixelMemLayout::IDList PixelMemLayout::registeredIDs() {
        auto  &reg = registry();
        IDList ret;
        for (const auto &[id, data] : reg.entries) {
                if (id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
