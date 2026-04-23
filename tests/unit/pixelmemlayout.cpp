/**
 * @file      pixelmemlayout.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/pixelmemlayout.h>

using namespace promeki;

// ============================================================================
// Default / Invalid construction
// ============================================================================

TEST_CASE("PixelMemLayout: default constructs to Invalid") {
        PixelMemLayout pf;
        CHECK_FALSE(pf.isValid());
        CHECK(pf.id() == PixelMemLayout::Invalid);
}

TEST_CASE("PixelMemLayout: explicit Invalid construction") {
        PixelMemLayout pf(PixelMemLayout::Invalid);
        CHECK_FALSE(pf.isValid());
}

// ============================================================================
// I_4x8 (e.g. RGBA8 layout)
// ============================================================================

TEST_CASE("PixelMemLayout: I_4x8 is valid") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.isValid());
        CHECK(pf.id() == PixelMemLayout::I_4x8);
}

TEST_CASE("PixelMemLayout: I_4x8 name is non-empty") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK_FALSE(pf.name().isEmpty());
}

TEST_CASE("PixelMemLayout: I_4x8 compCount is 4") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.compCount() == 4);
}

TEST_CASE("PixelMemLayout: I_4x8 planeCount is 1") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.planeCount() == 1);
}

TEST_CASE("PixelMemLayout: I_4x8 bytesPerBlock is 4") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.bytesPerBlock() == 4);
}

TEST_CASE("PixelMemLayout: I_4x8 sampling is 444") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.sampling() == PixelMemLayout::Sampling444);
}

TEST_CASE("PixelMemLayout: I_4x8 component bits are 8") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        for(size_t i = 0; i < pf.compCount(); i++) {
                CHECK(pf.compDesc(i).bits == 8);
                CHECK(pf.compDesc(i).plane == 0);
        }
}

TEST_CASE("PixelMemLayout: I_4x8 component byteOffsets are sequential") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.compDesc(0).byteOffset == 0);
        CHECK(pf.compDesc(1).byteOffset == 1);
        CHECK(pf.compDesc(2).byteOffset == 2);
        CHECK(pf.compDesc(3).byteOffset == 3);
}

TEST_CASE("PixelMemLayout: I_3x8 component byteOffsets are sequential") {
        PixelMemLayout pf(PixelMemLayout::I_3x8);
        CHECK(pf.compDesc(0).byteOffset == 0);
        CHECK(pf.compDesc(1).byteOffset == 1);
        CHECK(pf.compDesc(2).byteOffset == 2);
}

TEST_CASE("PixelMemLayout: I_4x8 lineStride") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.lineStride(0, 1920) == 1920 * 4);
}

TEST_CASE("PixelMemLayout: I_4x8 planeSize") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080 * 4);
}

// ============================================================================
// I_3x8 / 3x10
// ============================================================================

TEST_CASE("PixelMemLayout: I_3x8 is valid") {
        PixelMemLayout pf(PixelMemLayout::I_3x8);
        CHECK(pf.isValid());
}

TEST_CASE("PixelMemLayout: I_3x8 lineStride") {
        PixelMemLayout pf(PixelMemLayout::I_3x8);
        CHECK(pf.lineStride(0, 1920) == 1920 * 3);
}

TEST_CASE("PixelMemLayout: I_3x10_DPX is valid") {
        PixelMemLayout pf(PixelMemLayout::I_3x10_DPX);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
}

// ============================================================================
// Interleaved 4:2:2 (YUYV / UYVY)
// ============================================================================

TEST_CASE("PixelMemLayout: I_422_3x8 is valid 422") {
        PixelMemLayout pf(PixelMemLayout::I_422_3x8);
        CHECK(pf.isValid());
        CHECK(pf.sampling() == PixelMemLayout::Sampling422);
}

TEST_CASE("PixelMemLayout: UYVY 8-bit is valid") {
        PixelMemLayout pf(PixelMemLayout::I_422_UYVY_3x8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.sampling() == PixelMemLayout::Sampling422);
        CHECK(pf.planeCount() == 1);
}

TEST_CASE("PixelMemLayout: UYVY 8-bit component offsets") {
        PixelMemLayout pf(PixelMemLayout::I_422_UYVY_3x8);
        CHECK(pf.compDesc(0).byteOffset == 1);  // Y
        CHECK(pf.compDesc(1).byteOffset == 0);  // Cb
        CHECK(pf.compDesc(2).byteOffset == 2);  // Cr
}

TEST_CASE("PixelMemLayout: UYVY 8-bit lineStride 1920") {
        PixelMemLayout pf(PixelMemLayout::I_422_UYVY_3x8);
        CHECK(pf.lineStride(0, 1920) == 3840);
}

TEST_CASE("PixelMemLayout: UYVY 10-bit LE is valid") {
        PixelMemLayout pf(PixelMemLayout::I_422_UYVY_3x10_LE);
        CHECK(pf.isValid());
        CHECK(pf.bytesPerBlock() == 8);
}

TEST_CASE("PixelMemLayout: UYVY 12-bit component bits are 12") {
        PixelMemLayout pf(PixelMemLayout::I_422_UYVY_3x12_LE);
        for(size_t i = 0; i < pf.compCount(); i++) {
                CHECK(pf.compDesc(i).bits == 12);
        }
}

// ============================================================================
// v210
// ============================================================================

TEST_CASE("PixelMemLayout: v210 block size") {
        PixelMemLayout pf(PixelMemLayout::I_422_v210);
        CHECK(pf.pixelsPerBlock() == 6);
        CHECK(pf.bytesPerBlock() == 16);
}

TEST_CASE("PixelMemLayout: v210 lineStride 1920") {
        PixelMemLayout pf(PixelMemLayout::I_422_v210);
        CHECK(pf.lineStride(0, 1920) == 5120);
}

TEST_CASE("PixelMemLayout: v210 lineStride 1280 (not divisible by 6)") {
        PixelMemLayout pf(PixelMemLayout::I_422_v210);
        size_t stride = pf.lineStride(0, 1280);
        CHECK(stride == 3456);
        CHECK(stride % 128 == 0);
}

TEST_CASE("PixelMemLayout: v210 lineStride 4096 (DCI 4K)") {
        PixelMemLayout pf(PixelMemLayout::I_422_v210);
        size_t stride = pf.lineStride(0, 4096);
        CHECK(stride % 128 == 0);
        CHECK(stride >= (4096 * 16 + 5) / 6);
}

TEST_CASE("PixelMemLayout: v210 lineStride 720 (SD)") {
        PixelMemLayout pf(PixelMemLayout::I_422_v210);
        CHECK(pf.lineStride(0, 720) == 1920);
}

TEST_CASE("PixelMemLayout: v210 lineStride 6 (minimum)") {
        PixelMemLayout pf(PixelMemLayout::I_422_v210);
        CHECK(pf.lineStride(0, 6) == 128);
}

TEST_CASE("PixelMemLayout: v210 planeSize 1920x1080") {
        PixelMemLayout pf(PixelMemLayout::I_422_v210);
        CHECK(pf.planeSize(0, 1920, 1080) == 5120 * 1080);
}

// ============================================================================
// Stride with padding and alignment
// ============================================================================

TEST_CASE("PixelMemLayout: I_3x8 lineStride with linePad") {
        PixelMemLayout pf(PixelMemLayout::I_3x8);
        CHECK(pf.lineStride(0, 100, 4, 1) == 100 * 3 + 4);
}

TEST_CASE("PixelMemLayout: I_4x8 lineStride with alignment") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        size_t stride = pf.lineStride(0, 1920, 0, 16);
        CHECK(stride % 16 == 0);
        CHECK(stride >= 1920 * 4);
}

// ============================================================================
// Invalid plane index
// ============================================================================

TEST_CASE("PixelMemLayout: lineStride for invalid plane returns 0") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.lineStride(1, 1920) == 0);
}

// ============================================================================
// Equality and lookup
// ============================================================================

TEST_CASE("PixelMemLayout: equality") {
        PixelMemLayout a(PixelMemLayout::I_4x8);
        PixelMemLayout b(PixelMemLayout::I_4x8);
        PixelMemLayout c(PixelMemLayout::I_3x8);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("PixelMemLayout: lookup by name") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        PixelMemLayout found = PixelMemLayout::lookup(pf.name());
        CHECK(found.isValid());
        CHECK(found == pf);
}

TEST_CASE("PixelMemLayout: lookup unknown name returns invalid") {
        PixelMemLayout found = PixelMemLayout::lookup("bogus_nonexistent_format");
        CHECK_FALSE(found.isValid());
}

// ============================================================================
// Chroma siting
// ============================================================================

TEST_CASE("PixelMemLayout: 4:2:2 formats have Left/Top chroma siting") {
        PixelMemLayout::ID ids422[] = {
                PixelMemLayout::I_422_3x8,
                PixelMemLayout::I_422_3x10,
                PixelMemLayout::I_422_UYVY_3x8,
                PixelMemLayout::I_422_UYVY_3x10_LE,
                PixelMemLayout::I_422_v210,
        };
        for(auto id : ids422) {
                PixelMemLayout pf(id);
                CHECK(pf.chromaSitingH() == PixelMemLayout::ChromaHLeft);
                CHECK(pf.chromaSitingV() == PixelMemLayout::ChromaVTop);
        }
}

TEST_CASE("PixelMemLayout: 4:4:4 formats have Undefined chroma siting") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.chromaSitingH() == PixelMemLayout::ChromaHUndefined);
        CHECK(pf.chromaSitingV() == PixelMemLayout::ChromaVUndefined);
}

TEST_CASE("PixelMemLayout: 4:2:0 formats have Left/Center chroma siting") {
        PixelMemLayout::ID ids420[] = {
                PixelMemLayout::P_420_3x8,
                PixelMemLayout::P_420_3x10_LE,
                PixelMemLayout::SP_420_8,
                PixelMemLayout::SP_420_10_LE,
        };
        for(auto id : ids420) {
                PixelMemLayout pf(id);
                CHECK(pf.chromaSitingH() == PixelMemLayout::ChromaHLeft);
                CHECK(pf.chromaSitingV() == PixelMemLayout::ChromaVCenter);
        }
}

// ============================================================================
// Planar 4:2:2 formats
// ============================================================================

TEST_CASE("PixelMemLayout: P_422_3x8 properties") {
        PixelMemLayout pf(PixelMemLayout::P_422_3x8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 3);
        CHECK(pf.sampling() == PixelMemLayout::Sampling422);
}

TEST_CASE("PixelMemLayout: P_422_3x8 stride 1920x1080") {
        PixelMemLayout pf(PixelMemLayout::P_422_3x8);
        CHECK(pf.lineStride(0, 1920) == 1920);
        CHECK(pf.lineStride(1, 1920) == 960);
        CHECK(pf.lineStride(2, 1920) == 960);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080);
        CHECK(pf.planeSize(1, 1920, 1080) == 960 * 1080);
        CHECK(pf.planeSize(2, 1920, 1080) == 960 * 1080);
}

TEST_CASE("PixelMemLayout: P_422_3x10_LE stride 1920") {
        PixelMemLayout pf(PixelMemLayout::P_422_3x10_LE);
        CHECK(pf.isValid());
        CHECK(pf.lineStride(0, 1920) == 3840);
        CHECK(pf.lineStride(1, 1920) == 1920);
}

// ============================================================================
// Planar 4:2:0 formats
// ============================================================================

TEST_CASE("PixelMemLayout: P_420_3x8 properties") {
        PixelMemLayout pf(PixelMemLayout::P_420_3x8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 3);
        CHECK(pf.sampling() == PixelMemLayout::Sampling420);
}

TEST_CASE("PixelMemLayout: P_420_3x8 stride 1920x1080") {
        PixelMemLayout pf(PixelMemLayout::P_420_3x8);
        CHECK(pf.lineStride(0, 1920) == 1920);
        CHECK(pf.lineStride(1, 1920) == 960);
        CHECK(pf.lineStride(2, 1920) == 960);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080);
        CHECK(pf.planeSize(1, 1920, 1080) == 960 * 540);
        CHECK(pf.planeSize(2, 1920, 1080) == 960 * 540);
}

TEST_CASE("PixelMemLayout: P_420_3x8 total is 1.5x luma") {
        PixelMemLayout pf(PixelMemLayout::P_420_3x8);
        size_t total = pf.planeSize(0, 1920, 1080) +
                       pf.planeSize(1, 1920, 1080) +
                       pf.planeSize(2, 1920, 1080);
        CHECK(total == 1920 * 1080 * 3 / 2);
}

TEST_CASE("PixelMemLayout: P_420_3x10_LE stride 1920") {
        PixelMemLayout pf(PixelMemLayout::P_420_3x10_LE);
        CHECK(pf.lineStride(0, 1920) == 3840);
        CHECK(pf.lineStride(1, 1920) == 1920);
        CHECK(pf.planeSize(1, 1920, 1080) == 1920 * 540);
}

// ============================================================================
// Semi-planar 4:2:0 (NV12) formats
// ============================================================================

TEST_CASE("PixelMemLayout: SP_420_8 properties") {
        PixelMemLayout pf(PixelMemLayout::SP_420_8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 2);
        CHECK(pf.sampling() == PixelMemLayout::Sampling420);
}

TEST_CASE("PixelMemLayout: SP_420_8 stride 1920x1080") {
        PixelMemLayout pf(PixelMemLayout::SP_420_8);
        CHECK(pf.lineStride(0, 1920) == 1920);       // Y: width * 1
        CHECK(pf.lineStride(1, 1920) == 1920);       // CbCr: (width/2) * 2 = width
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080);
        CHECK(pf.planeSize(1, 1920, 1080) == 1920 * 540);
}

TEST_CASE("PixelMemLayout: SP_420_8 total equals I420 total") {
        PixelMemLayout nv12(PixelMemLayout::SP_420_8);
        PixelMemLayout i420(PixelMemLayout::P_420_3x8);
        size_t nv12Total = nv12.planeSize(0, 1920, 1080) + nv12.planeSize(1, 1920, 1080);
        size_t i420Total = i420.planeSize(0, 1920, 1080) + i420.planeSize(1, 1920, 1080) + i420.planeSize(2, 1920, 1080);
        CHECK(nv12Total == i420Total);
}

TEST_CASE("PixelMemLayout: P_420_3x8 chroma plane non-zero for height=1") {
        // Regression: planarPlaneSize used floor division so a 4:2:0 image
        // with height=1 produced 0 chroma rows, letting the CSC pipeline
        // write past the end of the empty chroma buffer.  Ceiling division
        // must yield 1 chroma row for both the Cb and Cr planes.
        PixelMemLayout pf(PixelMemLayout::P_420_3x8);
        CHECK(pf.planeSize(0, 1920, 1) == 1920);  // Y: 1 row
        CHECK(pf.planeSize(1, 1920, 1) == 960);   // Cb: ceil(1/2)=1 row, 960 bytes
        CHECK(pf.planeSize(2, 1920, 1) == 960);   // Cr: same
}

TEST_CASE("PixelMemLayout: SP_420_8 chroma plane non-zero for height=1") {
        // Same regression but for the semi-planar NV12 variant.
        PixelMemLayout pf(PixelMemLayout::SP_420_8);
        CHECK(pf.planeSize(0, 1920, 1) == 1920);   // Y: 1 row
        CHECK(pf.planeSize(1, 1920, 1) == 1920);   // CbCr: ceil(1/2)=1 row, 1920 bytes
}

TEST_CASE("PixelMemLayout: P_420_3x8 odd-luma-height ceiling") {
        // Odd luma heights must give ceil(height/2) chroma rows, not floor.
        PixelMemLayout pf(PixelMemLayout::P_420_3x8);
        CHECK(pf.planeSize(1, 1920, 3) == 960 * 2);   // ceil(3/2)=2 chroma rows
        CHECK(pf.planeSize(1, 1920, 5) == 960 * 3);   // ceil(5/2)=3 chroma rows
}

TEST_CASE("PixelMemLayout: SP_420_10_LE stride 1920") {
        PixelMemLayout pf(PixelMemLayout::SP_420_10_LE);
        CHECK(pf.lineStride(0, 1920) == 3840);       // Y: 1920 * 2
        CHECK(pf.lineStride(1, 1920) == 3840);       // CbCr: (1920/2) * 4 = 3840
}

// ============================================================================
// registeredIDs
// ============================================================================

TEST_CASE("PixelMemLayout: registeredIDs includes all formats") {
        auto ids = PixelMemLayout::registeredIDs();
        CHECK(ids.size() >= 78);
        CHECK(ids.contains(PixelMemLayout::I_4x8));
        CHECK(ids.contains(PixelMemLayout::I_422_UYVY_3x8));
        CHECK(ids.contains(PixelMemLayout::I_422_v210));
        CHECK(ids.contains(PixelMemLayout::P_422_3x8));
        CHECK(ids.contains(PixelMemLayout::P_420_3x8));
        CHECK(ids.contains(PixelMemLayout::SP_420_8));
        CHECK(ids.contains(PixelMemLayout::P_420_3x12_BE));
        CHECK(ids.contains(PixelMemLayout::SP_420_12_BE));
}

// ============================================================================
// Automatic iteration over ALL registered formats
// ============================================================================

TEST_CASE("PixelMemLayout: all registered formats have valid properties") {
        auto ids = PixelMemLayout::registeredIDs();
        for(auto id : ids) {
                PixelMemLayout pf(id);
                CHECK(pf.isValid());
                CHECK_FALSE(pf.name().isEmpty());
                CHECK(pf.compCount() > 0);
                CHECK(pf.planeCount() > 0);
                CHECK(PixelMemLayout::lookup(pf.name()) == pf);
        }
}

// ============================================================================
// Monochrome formats
// ============================================================================

TEST_CASE("PixelMemLayout: Monochrome I_1x8") {
        SUBCASE("I_1x8 basic properties") {
                PixelMemLayout pf(PixelMemLayout::I_1x8);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 1);
                CHECK(pf.planeCount() == 1);
                CHECK(pf.bytesPerBlock() == 1);
                CHECK(pf.lineStride(0, 1920) == 1920);
        }
}

// ============================================================================
// Float half-precision formats
// ============================================================================

TEST_CASE("PixelMemLayout: Float half I_4xF16_LE") {
        SUBCASE("I_4xF16_LE basic properties") {
                PixelMemLayout pf(PixelMemLayout::I_4xF16_LE);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 4);
                CHECK(pf.planeCount() == 1);
                CHECK(pf.bytesPerBlock() == 8);
                CHECK(pf.lineStride(0, 1920) == 15360);
        }
}

// ============================================================================
// Float single-precision formats
// ============================================================================

TEST_CASE("PixelMemLayout: Float single I_3xF32_LE") {
        SUBCASE("I_3xF32_LE basic properties") {
                PixelMemLayout pf(PixelMemLayout::I_3xF32_LE);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.planeCount() == 1);
                CHECK(pf.bytesPerBlock() == 12);
                CHECK(pf.lineStride(0, 1920) == 23040);
        }
}

// ============================================================================
// 10:10:10:2 packed formats
// ============================================================================

TEST_CASE("PixelMemLayout: 10:10:10:2 I_10_10_10_2_LE") {
        SUBCASE("I_10_10_10_2_LE basic properties") {
                PixelMemLayout pf(PixelMemLayout::I_10_10_10_2_LE);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 4);
                CHECK(pf.bytesPerBlock() == 4);
                CHECK(pf.lineStride(0, 1920) == 7680);
        }
}

// ============================================================================
// NV21 (semi-planar 4:2:0 CrCb order)
// ============================================================================

TEST_CASE("PixelMemLayout: NV21 SP_420_NV21_8") {
        SUBCASE("SP_420_NV21_8 basic properties") {
                PixelMemLayout pf(PixelMemLayout::SP_420_NV21_8);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.planeCount() == 2);
                CHECK(pf.sampling() == PixelMemLayout::Sampling420);
        }
}

// ============================================================================
// NV16 (semi-planar 4:2:2)
// ============================================================================

TEST_CASE("PixelMemLayout: NV16 SP_422_8") {
        SUBCASE("SP_422_8 basic properties") {
                PixelMemLayout pf(PixelMemLayout::SP_422_8);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.planeCount() == 2);
                CHECK(pf.sampling() == PixelMemLayout::Sampling422);
        }
}

// ============================================================================
// 4:1:1 planar
// ============================================================================

TEST_CASE("PixelMemLayout: 4:1:1 P_411_3x8") {
        SUBCASE("P_411_3x8 basic properties") {
                PixelMemLayout pf(PixelMemLayout::P_411_3x8);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.planeCount() == 3);
                CHECK(pf.sampling() == PixelMemLayout::Sampling411);
        }
}

// ============================================================================
// 16-bit UYVY
// ============================================================================

TEST_CASE("PixelMemLayout: 16-bit UYVY I_422_UYVY_3x16_LE") {
        SUBCASE("I_422_UYVY_3x16_LE basic properties") {
                PixelMemLayout pf(PixelMemLayout::I_422_UYVY_3x16_LE);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.sampling() == PixelMemLayout::Sampling422);
        }
}

// ============================================================================
// Planar 4:4:4 (RGB / YUV 4:4:4)
// ============================================================================

TEST_CASE("PixelMemLayout: Planar 4:4:4 P_444_3x8") {
        PixelMemLayout pf(PixelMemLayout::P_444_3x8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 3);
        CHECK(pf.sampling() == PixelMemLayout::Sampling444);
        // Planar 4:4:4 declares co-sited (Center) chroma siting per the
        // well-known factory (makePlanar444_3x8).  Interleaved 4:4:4 uses
        // ChromaHUndefined / ChromaVUndefined — see the I_4x8 test.
        CHECK(pf.chromaSitingH() == PixelMemLayout::ChromaHCenter);
        CHECK(pf.chromaSitingV() == PixelMemLayout::ChromaVCenter);
}

TEST_CASE("PixelMemLayout: Planar 4:4:4 P_444_3x8 stride 1920x1080") {
        // All three planes are the same size (no subsampling).
        PixelMemLayout pf(PixelMemLayout::P_444_3x8);
        CHECK(pf.lineStride(0, 1920) == 1920);
        CHECK(pf.lineStride(1, 1920) == 1920);
        CHECK(pf.lineStride(2, 1920) == 1920);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080);
        CHECK(pf.planeSize(1, 1920, 1080) == 1920 * 1080);
        CHECK(pf.planeSize(2, 1920, 1080) == 1920 * 1080);
}

TEST_CASE("PixelMemLayout: Planar 4:4:4 P_444_3x10_LE stride 1920") {
        PixelMemLayout pf(PixelMemLayout::P_444_3x10_LE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 3);
        CHECK(pf.sampling() == PixelMemLayout::Sampling444);
        // 10-bit stored in 16-bit LE words — 2 bytes per sample.
        CHECK(pf.lineStride(0, 1920) == 3840);
        CHECK(pf.lineStride(1, 1920) == 3840);
        CHECK(pf.lineStride(2, 1920) == 3840);
}

// ============================================================================
// I_3x10_DPX_B (DPX Method B packed)
// ============================================================================

TEST_CASE("PixelMemLayout: I_3x10_DPX_B basic properties") {
        PixelMemLayout pf(PixelMemLayout::I_3x10_DPX_B);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 1);
        CHECK(pf.bytesPerBlock() == 4);
        CHECK(pf.pixelsPerBlock() == 1);
        CHECK(pf.sampling() == PixelMemLayout::Sampling444);
        // 10 bits per component.
        for(size_t i = 0; i < pf.compCount(); i++) {
                CHECK(pf.compDesc(i).bits == 10);
                CHECK(pf.compDesc(i).plane == 0);
        }
        // 4 bytes per pixel — stride 1920 yields 7680.
        CHECK(pf.lineStride(0, 1920) == 7680);
}

// ============================================================================
// 10:10:10:2 packed BE variant
// ============================================================================

TEST_CASE("PixelMemLayout: 10:10:10:2 I_10_10_10_2_BE") {
        PixelMemLayout pf(PixelMemLayout::I_10_10_10_2_BE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 4);
        CHECK(pf.bytesPerBlock() == 4);
        CHECK(pf.lineStride(0, 1920) == 7680);
}

// ============================================================================
// Float half/single precision mono variants
// ============================================================================

TEST_CASE("PixelMemLayout: Float half mono I_1xF16_LE") {
        PixelMemLayout pf(PixelMemLayout::I_1xF16_LE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 1);
        CHECK(pf.bytesPerBlock() == 2);
        CHECK(pf.lineStride(0, 1920) == 3840);
}

TEST_CASE("PixelMemLayout: Float half 3-comp I_3xF16_LE") {
        PixelMemLayout pf(PixelMemLayout::I_3xF16_LE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.bytesPerBlock() == 6);
        CHECK(pf.lineStride(0, 1920) == 11520);
}

TEST_CASE("PixelMemLayout: Float half 4-comp BE I_4xF16_BE") {
        PixelMemLayout pf(PixelMemLayout::I_4xF16_BE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 4);
        CHECK(pf.bytesPerBlock() == 8);
}

TEST_CASE("PixelMemLayout: Float single mono I_1xF32_LE") {
        PixelMemLayout pf(PixelMemLayout::I_1xF32_LE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 1);
        CHECK(pf.bytesPerBlock() == 4);
        CHECK(pf.lineStride(0, 1920) == 7680);
}

TEST_CASE("PixelMemLayout: Float single 4-comp I_4xF32_LE") {
        PixelMemLayout pf(PixelMemLayout::I_4xF32_LE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 4);
        CHECK(pf.bytesPerBlock() == 16);
        CHECK(pf.lineStride(0, 1920) == 30720);
}

TEST_CASE("PixelMemLayout: Float single BE I_3xF32_BE") {
        PixelMemLayout pf(PixelMemLayout::I_3xF32_BE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.bytesPerBlock() == 12);
}

// ============================================================================
// Monochrome 10/12/16-bit variants
// ============================================================================

TEST_CASE("PixelMemLayout: Monochrome I_1x10_LE stride") {
        PixelMemLayout pf(PixelMemLayout::I_1x10_LE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 1);
        CHECK(pf.bytesPerBlock() == 2);
        CHECK(pf.lineStride(0, 1920) == 3840);
}

TEST_CASE("PixelMemLayout: Monochrome I_1x16_BE stride") {
        PixelMemLayout pf(PixelMemLayout::I_1x16_BE);
        CHECK(pf.isValid());
        CHECK(pf.bytesPerBlock() == 2);
}

// ============================================================================
// Big-endian RGB interleaved variants
// ============================================================================

TEST_CASE("PixelMemLayout: I_4x10_BE basic properties") {
        PixelMemLayout pf(PixelMemLayout::I_4x10_BE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 4);
        CHECK(pf.bytesPerBlock() == 8);
}

TEST_CASE("PixelMemLayout: I_4x16_LE basic properties") {
        PixelMemLayout pf(PixelMemLayout::I_4x16_LE);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 4);
        CHECK(pf.bytesPerBlock() == 8);
        CHECK(pf.lineStride(0, 1920) == 15360);
}

// ============================================================================
// data() accessor and direct Data introspection
// ============================================================================

TEST_CASE("PixelMemLayout: data() returns non-null for valid formats") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.data() != nullptr);
        CHECK(pf.data()->id == PixelMemLayout::I_4x8);
        CHECK(pf.data()->name == "4x8");
}

TEST_CASE("PixelMemLayout: data() for Invalid returns the Invalid record") {
        // lookupData for Invalid still returns the Invalid entry (not nullptr).
        PixelMemLayout pf;
        CHECK(pf.data() != nullptr);
        CHECK(pf.data()->id == PixelMemLayout::Invalid);
}

// ============================================================================
// isValidPlane()
// ============================================================================

TEST_CASE("PixelMemLayout: isValidPlane for interleaved (single plane)") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.isValidPlane(0));
        CHECK_FALSE(pf.isValidPlane(1));
        CHECK_FALSE(pf.isValidPlane(42));
}

TEST_CASE("PixelMemLayout: isValidPlane for planar (three planes)") {
        PixelMemLayout pf(PixelMemLayout::P_422_3x8);
        CHECK(pf.isValidPlane(0));
        CHECK(pf.isValidPlane(1));
        CHECK(pf.isValidPlane(2));
        CHECK_FALSE(pf.isValidPlane(3));
}

TEST_CASE("PixelMemLayout: isValidPlane for semi-planar (two planes)") {
        PixelMemLayout pf(PixelMemLayout::SP_420_8);
        CHECK(pf.isValidPlane(0));
        CHECK(pf.isValidPlane(1));
        CHECK_FALSE(pf.isValidPlane(2));
}

// ============================================================================
// planeSize with invalid plane returns 0
// ============================================================================

TEST_CASE("PixelMemLayout: planeSize for invalid plane returns 0") {
        PixelMemLayout pf(PixelMemLayout::I_4x8);
        CHECK(pf.planeSize(1, 1920, 1080) == 0);  // only plane 0 exists
        CHECK(pf.planeSize(99, 1920, 1080) == 0);
}

// ============================================================================
// Invalid PixelMemLayout via unknown ID (falls back to Invalid record)
// ============================================================================

TEST_CASE("PixelMemLayout: unknown ID falls back to Invalid record") {
        // Any ID between the last well-known and UserDefined without a
        // registered record falls back to the Invalid entry.
        PixelMemLayout pf(static_cast<PixelMemLayout::ID>(900));
        CHECK_FALSE(pf.isValid());
        CHECK(pf.id() == PixelMemLayout::Invalid);
}

// ============================================================================
// registerType / registerData (user-defined types)
// ============================================================================

TEST_CASE("PixelMemLayout: registerType returns unique increasing IDs >= UserDefined") {
        auto a = PixelMemLayout::registerType();
        auto b = PixelMemLayout::registerType();
        auto c = PixelMemLayout::registerType();
        CHECK(static_cast<int>(a) >= static_cast<int>(PixelMemLayout::UserDefined));
        CHECK(static_cast<int>(b) == static_cast<int>(a) + 1);
        CHECK(static_cast<int>(c) == static_cast<int>(b) + 1);
        CHECK(a != b);
        CHECK(b != c);
}

TEST_CASE("PixelMemLayout: registerData allows user-defined layouts to be looked up") {
        // Register a simple user-defined layout: 2 components, 8 bits each,
        // interleaved, 2 bytes per pixel, like a minimal "LA8" (luma+alpha).
        PixelMemLayout::ID id = PixelMemLayout::registerType();
        PixelMemLayout::Data d;
        d.id             = id;
        d.name           = "TestUserLA8";
        d.desc           = "User-registered LA8 layout for unit testing";
        d.sampling       = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock  = 2;
        d.compCount      = 2;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 0, 8, 1 };
        d.planeCount     = 1;
        d.planes[0]      = { "" };
        PixelMemLayout::registerData(std::move(d));

        PixelMemLayout pf(id);
        CHECK(pf.isValid());
        CHECK(pf.id() == id);
        CHECK(pf.name() == "TestUserLA8");
        CHECK(pf.compCount() == 2);
        CHECK(pf.bytesPerBlock() == 2);

        // Lookup by name should resolve the new registration.
        PixelMemLayout found = PixelMemLayout::lookup("TestUserLA8");
        CHECK(found == pf);

        // Should appear in registeredIDs().
        auto ids = PixelMemLayout::registeredIDs();
        CHECK(ids.contains(id));
}

TEST_CASE("PixelMemLayout: registerData with null function pointers → lineStride/planeSize return 0") {
        // Exercise the null-function-pointer guards in lineStride / planeSize.
        PixelMemLayout::ID id = PixelMemLayout::registerType();
        PixelMemLayout::Data d;
        d.id             = id;
        d.name           = "TestUserNullFuncs";
        d.desc           = "User layout with no stride/size functions";
        d.sampling       = PixelMemLayout::Sampling444;
        d.pixelsPerBlock = 1;
        d.bytesPerBlock  = 4;
        d.compCount      = 4;
        d.comps[0]       = { 0, 8, 0 };
        d.comps[1]       = { 0, 8, 1 };
        d.comps[2]       = { 0, 8, 2 };
        d.comps[3]       = { 0, 8, 3 };
        d.planeCount     = 1;
        d.planes[0]      = { "" };
        // lineStrideFunc and planeSizeFunc deliberately left null.
        PixelMemLayout::registerData(std::move(d));

        PixelMemLayout pf(id);
        REQUIRE(pf.isValid());
        CHECK(pf.lineStride(0, 1920) == 0);
        CHECK(pf.planeSize(0, 1920, 1080) == 0);
}

// ============================================================================
// Chroma siting for 4:1:1
// ============================================================================

TEST_CASE("PixelMemLayout: 4:1:1 planar has Left/Top chroma siting") {
        PixelMemLayout pf(PixelMemLayout::P_411_3x8);
        CHECK(pf.chromaSitingH() == PixelMemLayout::ChromaHLeft);
        CHECK(pf.chromaSitingV() == PixelMemLayout::ChromaVTop);
}

// ============================================================================
// Planar 4:2:2 16-bit variants
// ============================================================================

TEST_CASE("PixelMemLayout: P_422_3x16_LE basic properties and stride") {
        PixelMemLayout pf(PixelMemLayout::P_422_3x16_LE);
        CHECK(pf.isValid());
        CHECK(pf.planeCount() == 3);
        CHECK(pf.sampling() == PixelMemLayout::Sampling422);
        // 16-bit luma: stride == width * 2.  Chroma planes half-width.
        CHECK(pf.lineStride(0, 1920) == 3840);
        CHECK(pf.lineStride(1, 1920) == 1920);
        CHECK(pf.lineStride(2, 1920) == 1920);
}

TEST_CASE("PixelMemLayout: P_420_3x16_LE plane sizes") {
        PixelMemLayout pf(PixelMemLayout::P_420_3x16_LE);
        CHECK(pf.isValid());
        CHECK(pf.sampling() == PixelMemLayout::Sampling420);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 2 * 1080);
        CHECK(pf.planeSize(1, 1920, 1080) == 1920 /* 960 * 2 */ * 540);
        CHECK(pf.planeSize(2, 1920, 1080) == 1920 * 540);
}

TEST_CASE("PixelMemLayout: SP_420_16_LE basic properties") {
        PixelMemLayout pf(PixelMemLayout::SP_420_16_LE);
        CHECK(pf.isValid());
        CHECK(pf.planeCount() == 2);
        CHECK(pf.sampling() == PixelMemLayout::Sampling420);
        // Y plane: 16-bit, stride = width*2.
        CHECK(pf.lineStride(0, 1920) == 3840);
        // CbCr interleaved plane: 2 samples per chroma block, 16 bits each =
        // half-width * 4 bytes = 2*width bytes.
        CHECK(pf.lineStride(1, 1920) == 3840);
}

// ============================================================================
// lineStride with padding + alignment interacts correctly
// ============================================================================

TEST_CASE("PixelMemLayout: planar lineStride with linePad") {
        PixelMemLayout pf(PixelMemLayout::P_420_3x8);
        // Padding adds directly to the per-plane line length.
        CHECK(pf.lineStride(0, 1920, 16, 1) == 1920 + 16);
        CHECK(pf.lineStride(1, 1920, 16, 1) == 960 + 16);
}

TEST_CASE("PixelMemLayout: v210 lineStride respects larger alignment") {
        // v210 minimum alignment is 128 bytes; a larger lineAlign should
        // take precedence.
        PixelMemLayout pf(PixelMemLayout::I_422_v210);
        size_t stride = pf.lineStride(0, 1920, 0, 256);
        CHECK(stride % 256 == 0);
}

// ============================================================================
// Equality: different IDs compare unequal; same ID compares equal
// ============================================================================

TEST_CASE("PixelMemLayout: operator!= across ids") {
        PixelMemLayout a(PixelMemLayout::I_4x8);
        PixelMemLayout b(PixelMemLayout::I_3x8);
        CHECK(a != b);
        CHECK_FALSE(a == b);
}

TEST_CASE("PixelMemLayout: Invalid != valid") {
        PixelMemLayout inv;
        PixelMemLayout v(PixelMemLayout::I_4x8);
        CHECK(inv != v);
}
