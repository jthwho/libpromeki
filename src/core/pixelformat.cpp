/**
 * @file      pixelformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/pixelformat.h>
#include <promeki/atomic.h>
#include <promeki/map.h>
#include <promeki/util.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(PixelFormat)

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered types
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{PixelFormat::UserDefined};

PixelFormat::ID PixelFormat::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// Stride / plane-size helpers shared by several formats
// ---------------------------------------------------------------------------

static size_t interleavedLineStride(const PixelFormat::Data *d, size_t /*planeIdx*/,
                                    size_t width, size_t linePad, size_t lineAlign) {
        size_t lineBytes = (width * d->bytesPerBlock) / d->pixelsPerBlock + linePad;
        return PROMEKI_ALIGN_UP(lineBytes, lineAlign);
}

static size_t interleavedPlaneSize(const PixelFormat::Data *d, size_t planeIdx,
                                   size_t width, size_t height,
                                   size_t linePad, size_t lineAlign) {
        return interleavedLineStride(d, planeIdx, width, linePad, lineAlign) * height;
}

// ---------------------------------------------------------------------------
// Factory functions for well-known pixel formats
// ---------------------------------------------------------------------------

static PixelFormat::Data makeInvalid() {
        PixelFormat::Data d;
        d.id   = PixelFormat::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid pixel format";
        return d;
}

static PixelFormat::Data makeInterleaved4x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_4x8;
        d.name           = "Interleaved_4x8";
        d.desc           = "4 components, 8 bits each, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock  = 4;
        d.compCount      = 4;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 0, 8, 1 };
        d.comps[2]       = { 0, 8, 2 };
        d.comps[3]       = { 0, 8, 3 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleaved3x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_3x8;
        d.name           = "Interleaved_3x8";
        d.desc           = "3 components, 8 bits each, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock  = 3;
        d.compCount      = 3;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 0, 8, 1 };
        d.comps[2]       = { 0, 8, 2 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleaved3x10() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_3x10;
        d.name           = "Interleaved_3x10";
        d.desc           = "3 components, 10 bits each, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock  = 4;  // 30 bits packed into 4 bytes
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 0 };
        d.comps[1]       = { 0, 10, 1 };
        d.comps[2]       = { 0, 10, 2 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleaved422_3x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_3x8;
        d.name           = "Interleaved_422_3x8";
        d.desc           = "3 components, 8 bits, 4:2:2 subsampled, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock  = 4;
        d.compCount      = 3;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 0, 8, 1 };
        d.comps[2]       = { 0, 8, 3 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleaved422_3x10() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_3x10;
        d.name           = "Interleaved_422_3x10";
        d.desc           = "3 components, 10 bits, 4:2:2 subsampled, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock  = 8;
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 0 };
        d.comps[1]       = { 0, 10, 2 };
        d.comps[2]       = { 0, 10, 6 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleavedUYVY3x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_UYVY_3x8;
        d.name           = "Interleaved_422_UYVY_3x8";
        d.desc           = "3 components, 8 bits, 4:2:2 UYVY, 1 interleaved plane";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock  = 4;  // Cb Y0 Cr Y1
        d.compCount      = 3;
        d.comps[0]       = { 0, 8, 1 };  // Y  at byte 1
        d.comps[1]       = { 0, 8, 0 };  // Cb at byte 0
        d.comps[2]       = { 0, 8, 2 };  // Cr at byte 2
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleavedUYVY3x10LE() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_UYVY_3x10_LE;
        d.name           = "Interleaved_422_UYVY_3x10_LE";
        d.desc           = "3 components, 10 bits in 16-bit LE words, 4:2:2 UYVY";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock  = 8;  // Cb(2) Y0(2) Cr(2) Y1(2)
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 2 };  // Y  at byte 2
        d.comps[1]       = { 0, 10, 0 };  // Cb at byte 0
        d.comps[2]       = { 0, 10, 4 };  // Cr at byte 4
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleavedUYVY3x10BE() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_UYVY_3x10_BE;
        d.name           = "Interleaved_422_UYVY_3x10_BE";
        d.desc           = "3 components, 10 bits in 16-bit BE words, 4:2:2 UYVY";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock  = 8;
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 2 };
        d.comps[1]       = { 0, 10, 0 };
        d.comps[2]       = { 0, 10, 4 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleavedUYVY3x12LE() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_UYVY_3x12_LE;
        d.name           = "Interleaved_422_UYVY_3x12_LE";
        d.desc           = "3 components, 12 bits in 16-bit LE words, 4:2:2 UYVY";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock  = 8;
        d.compCount      = 3;
        d.comps[0]       = { 0, 12, 2 };
        d.comps[1]       = { 0, 12, 0 };
        d.comps[2]       = { 0, 12, 4 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

static PixelFormat::Data makeInterleavedUYVY3x12BE() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_UYVY_3x12_BE;
        d.name           = "Interleaved_422_UYVY_3x12_BE";
        d.desc           = "3 components, 12 bits in 16-bit BE words, 4:2:2 UYVY";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.pixelsPerBlock = 2;
        d.bytesPerBlock  = 8;
        d.compCount      = 3;
        d.comps[0]       = { 0, 12, 2 };
        d.comps[1]       = { 0, 12, 0 };
        d.comps[2]       = { 0, 12, 4 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = interleavedLineStride;
        d.planeSizeFunc  = interleavedPlaneSize;
        return d;
}

// ---------------------------------------------------------------------------
// v210 stride / plane-size helpers
// ---------------------------------------------------------------------------

static size_t v210LineStride(const PixelFormat::Data * /*d*/, size_t /*planeIdx*/,
                             size_t width, size_t linePad, size_t lineAlign) {
        // v210: 6 pixels per 16 bytes, lines padded to 128-byte boundary
        size_t lineBytes = ((width + 5) / 6) * 16 + linePad;
        lineAlign = std::max(lineAlign, size_t(128));
        return PROMEKI_ALIGN_UP(lineBytes, lineAlign);
}

static size_t v210PlaneSize(const PixelFormat::Data *d, size_t planeIdx,
                            size_t width, size_t height,
                            size_t linePad, size_t lineAlign) {
        return v210LineStride(d, planeIdx, width, linePad, lineAlign) * height;
}

static PixelFormat::Data makeInterleavedV210() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Interleaved_422_v210;
        d.name           = "Interleaved_422_v210";
        d.desc           = "3 components, 10 bits, 4:2:2 v210 packed (3x10 in 32-bit words)";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.pixelsPerBlock = 6;
        d.bytesPerBlock  = 16;  // 4 x 32-bit words for 6 pixels
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 0 };
        d.comps[1]       = { 0, 10, 0 };
        d.comps[2]       = { 0, 10, 0 };
        d.planeCount     = 1;
        d.planes[0]      = { "Interleaved" };
        d.lineStrideFunc = v210LineStride;
        d.planeSizeFunc  = v210PlaneSize;
        return d;
}

// ---------------------------------------------------------------------------
// Planar stride / plane-size helpers
// ---------------------------------------------------------------------------

static size_t planarLineStride(const PixelFormat::Data *d, size_t planeIdx,
                               size_t width, size_t linePad, size_t lineAlign) {
        const auto &p = d->planes[planeIdx];
        size_t lineBytes = (width / p.hSubsampling) * p.bytesPerSample + linePad;
        return PROMEKI_ALIGN_UP(lineBytes, lineAlign);
}

static size_t planarPlaneSize(const PixelFormat::Data *d, size_t planeIdx,
                              size_t width, size_t height,
                              size_t linePad, size_t lineAlign) {
        const auto &p = d->planes[planeIdx];
        return planarLineStride(d, planeIdx, width, linePad, lineAlign) * (height / p.vSubsampling);
}

// ---------------------------------------------------------------------------
// Planar 4:2:2 factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makePlanar422_3x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Planar_422_3x8;
        d.name           = "Planar_422_3x8";
        d.desc           = "3 planes, 8-bit, 4:2:2";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.compCount      = 3;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 1, 8, 0 };
        d.comps[2]       = { 2, 8, 0 };
        d.planeCount     = 3;
        d.planes[0]      = { "Y",  1, 1, 1 };
        d.planes[1]      = { "Cb", 2, 1, 1 };
        d.planes[2]      = { "Cr", 2, 1, 1 };
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc  = planarPlaneSize;
        return d;
}

static PixelFormat::Data makePlanar422_3x10LE() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Planar_422_3x10_LE;
        d.name           = "Planar_422_3x10_LE";
        d.desc           = "3 planes, 10-bit in 16-bit LE words, 4:2:2";
        d.sampling       = PixelFormat::Sampling422;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVTop;
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 0 };
        d.comps[1]       = { 1, 10, 0 };
        d.comps[2]       = { 2, 10, 0 };
        d.planeCount     = 3;
        d.planes[0]      = { "Y",  1, 1, 2 };
        d.planes[1]      = { "Cb", 2, 1, 2 };
        d.planes[2]      = { "Cr", 2, 1, 2 };
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc  = planarPlaneSize;
        return d;
}

static PixelFormat::Data makePlanar422_3x10BE() {
        PixelFormat::Data d = makePlanar422_3x10LE();
        d.id   = PixelFormat::Planar_422_3x10_BE;
        d.name = "Planar_422_3x10_BE";
        d.desc = "3 planes, 10-bit in 16-bit BE words, 4:2:2";
        return d;
}

static PixelFormat::Data makePlanar422_3x12LE() {
        PixelFormat::Data d = makePlanar422_3x10LE();
        d.id   = PixelFormat::Planar_422_3x12_LE;
        d.name = "Planar_422_3x12_LE";
        d.desc = "3 planes, 12-bit in 16-bit LE words, 4:2:2";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelFormat::Data makePlanar422_3x12BE() {
        PixelFormat::Data d = makePlanar422_3x12LE();
        d.id   = PixelFormat::Planar_422_3x12_BE;
        d.name = "Planar_422_3x12_BE";
        d.desc = "3 planes, 12-bit in 16-bit BE words, 4:2:2";
        return d;
}

// ---------------------------------------------------------------------------
// Planar 4:2:0 factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makePlanar420_3x8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Planar_420_3x8;
        d.name           = "Planar_420_3x8";
        d.desc           = "3 planes, 8-bit, 4:2:0";
        d.sampling       = PixelFormat::Sampling420;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVCenter;
        d.compCount      = 3;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 1, 8, 0 };
        d.comps[2]       = { 2, 8, 0 };
        d.planeCount     = 3;
        d.planes[0]      = { "Y",  1, 1, 1 };
        d.planes[1]      = { "Cb", 2, 2, 1 };
        d.planes[2]      = { "Cr", 2, 2, 1 };
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc  = planarPlaneSize;
        return d;
}

static PixelFormat::Data makePlanar420_3x10LE() {
        PixelFormat::Data d;
        d.id             = PixelFormat::Planar_420_3x10_LE;
        d.name           = "Planar_420_3x10_LE";
        d.desc           = "3 planes, 10-bit in 16-bit LE words, 4:2:0";
        d.sampling       = PixelFormat::Sampling420;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVCenter;
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 0 };
        d.comps[1]       = { 1, 10, 0 };
        d.comps[2]       = { 2, 10, 0 };
        d.planeCount     = 3;
        d.planes[0]      = { "Y",  1, 1, 2 };
        d.planes[1]      = { "Cb", 2, 2, 2 };
        d.planes[2]      = { "Cr", 2, 2, 2 };
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc  = planarPlaneSize;
        return d;
}

static PixelFormat::Data makePlanar420_3x10BE() {
        PixelFormat::Data d = makePlanar420_3x10LE();
        d.id   = PixelFormat::Planar_420_3x10_BE;
        d.name = "Planar_420_3x10_BE";
        d.desc = "3 planes, 10-bit in 16-bit BE words, 4:2:0";
        return d;
}

static PixelFormat::Data makePlanar420_3x12LE() {
        PixelFormat::Data d = makePlanar420_3x10LE();
        d.id   = PixelFormat::Planar_420_3x12_LE;
        d.name = "Planar_420_3x12_LE";
        d.desc = "3 planes, 12-bit in 16-bit LE words, 4:2:0";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelFormat::Data makePlanar420_3x12BE() {
        PixelFormat::Data d = makePlanar420_3x12LE();
        d.id   = PixelFormat::Planar_420_3x12_BE;
        d.name = "Planar_420_3x12_BE";
        d.desc = "3 planes, 12-bit in 16-bit BE words, 4:2:0";
        return d;
}

// ---------------------------------------------------------------------------
// Semi-planar 4:2:0 (NV12) factory functions
// ---------------------------------------------------------------------------

static PixelFormat::Data makeSemiPlanar420_8() {
        PixelFormat::Data d;
        d.id             = PixelFormat::SemiPlanar_420_8;
        d.name           = "SemiPlanar_420_8";
        d.desc           = "2 planes, 8-bit, 4:2:0 NV12 (Y + interleaved CbCr)";
        d.sampling       = PixelFormat::Sampling420;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVCenter;
        d.compCount      = 3;
        d.comps[0]       = { 0, 8, 0 };  // Y in plane 0
        d.comps[1]       = { 1, 8, 0 };  // Cb in plane 1, byte 0
        d.comps[2]       = { 1, 8, 1 };  // Cr in plane 1, byte 1
        d.planeCount     = 2;
        d.planes[0]      = { "Y",    1, 1, 1 };
        d.planes[1]      = { "CbCr", 2, 2, 2 };  // half-width chroma positions, 2 bytes each (Cb+Cr), half height
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc  = planarPlaneSize;
        return d;
}

static PixelFormat::Data makeSemiPlanar420_10LE() {
        PixelFormat::Data d;
        d.id             = PixelFormat::SemiPlanar_420_10_LE;
        d.name           = "SemiPlanar_420_10_LE";
        d.desc           = "2 planes, 10-bit in 16-bit LE words, 4:2:0 NV12";
        d.sampling       = PixelFormat::Sampling420;
        d.chromaSitingH  = PixelFormat::ChromaHLeft;
        d.chromaSitingV  = PixelFormat::ChromaVCenter;
        d.compCount      = 3;
        d.comps[0]       = { 0, 10, 0 };
        d.comps[1]       = { 1, 10, 0 };
        d.comps[2]       = { 1, 10, 2 };
        d.planeCount     = 2;
        d.planes[0]      = { "Y",    1, 1, 2 };
        d.planes[1]      = { "CbCr", 2, 2, 4 };  // half-width chroma positions, 4 bytes each (Cb+Cr in 16-bit words)
        d.lineStrideFunc = planarLineStride;
        d.planeSizeFunc  = planarPlaneSize;
        return d;
}

static PixelFormat::Data makeSemiPlanar420_10BE() {
        PixelFormat::Data d = makeSemiPlanar420_10LE();
        d.id   = PixelFormat::SemiPlanar_420_10_BE;
        d.name = "SemiPlanar_420_10_BE";
        d.desc = "2 planes, 10-bit in 16-bit BE words, 4:2:0 NV12";
        return d;
}

static PixelFormat::Data makeSemiPlanar420_12LE() {
        PixelFormat::Data d = makeSemiPlanar420_10LE();
        d.id   = PixelFormat::SemiPlanar_420_12_LE;
        d.name = "SemiPlanar_420_12_LE";
        d.desc = "2 planes, 12-bit in 16-bit LE words, 4:2:0 NV12";
        d.comps[0].bits = 12;
        d.comps[1].bits = 12;
        d.comps[2].bits = 12;
        return d;
}

static PixelFormat::Data makeSemiPlanar420_12BE() {
        PixelFormat::Data d = makeSemiPlanar420_12LE();
        d.id   = PixelFormat::SemiPlanar_420_12_BE;
        d.name = "SemiPlanar_420_12_BE";
        d.desc = "2 planes, 12-bit in 16-bit BE words, 4:2:0 NV12";
        return d;
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry
// ---------------------------------------------------------------------------

struct PixelFormatRegistry {
        Map<PixelFormat::ID, PixelFormat::Data> entries;
        Map<String, PixelFormat::ID> nameMap;

        PixelFormatRegistry() {
                add(makeInvalid());
                add(makeInterleaved4x8());
                add(makeInterleaved3x8());
                add(makeInterleaved3x10());
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
        }

        void add(PixelFormat::Data d) {
                PixelFormat::ID id = d.id;
                if(d.id != PixelFormat::Invalid) {
                        nameMap[d.name] = id;
                }
                entries[id] = std::move(d);
        }
};

static PixelFormatRegistry &registry() {
        static PixelFormatRegistry reg;
        return reg;
}

const PixelFormat::Data *PixelFormat::lookupData(ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void PixelFormat::registerData(Data &&data) {
        auto &reg = registry();
        if(data.id != Invalid) {
                reg.nameMap[data.name] = data.id;
        }
        reg.entries[data.id] = std::move(data);
}

PixelFormat PixelFormat::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        return (it != reg.nameMap.end()) ? PixelFormat(it->second) : PixelFormat(Invalid);
}

PixelFormat::IDList PixelFormat::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                if(id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
