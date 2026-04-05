/**
 * @file      pixelformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/pixelformat.h>

using namespace promeki;

// ============================================================================
// Default / Invalid construction
// ============================================================================

TEST_CASE("PixelFormat: default constructs to Invalid") {
        PixelFormat pf;
        CHECK_FALSE(pf.isValid());
        CHECK(pf.id() == PixelFormat::Invalid);
}

TEST_CASE("PixelFormat: explicit Invalid construction") {
        PixelFormat pf(PixelFormat::Invalid);
        CHECK_FALSE(pf.isValid());
}

// ============================================================================
// I_4x8 (e.g. RGBA8 layout)
// ============================================================================

TEST_CASE("PixelFormat: I_4x8 is valid") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.isValid());
        CHECK(pf.id() == PixelFormat::I_4x8);
}

TEST_CASE("PixelFormat: I_4x8 name is non-empty") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK_FALSE(pf.name().isEmpty());
}

TEST_CASE("PixelFormat: I_4x8 compCount is 4") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.compCount() == 4);
}

TEST_CASE("PixelFormat: I_4x8 planeCount is 1") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.planeCount() == 1);
}

TEST_CASE("PixelFormat: I_4x8 bytesPerBlock is 4") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.bytesPerBlock() == 4);
}

TEST_CASE("PixelFormat: I_4x8 sampling is 444") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.sampling() == PixelFormat::Sampling444);
}

TEST_CASE("PixelFormat: I_4x8 component bits are 8") {
        PixelFormat pf(PixelFormat::I_4x8);
        for(size_t i = 0; i < pf.compCount(); i++) {
                CHECK(pf.compDesc(i).bits == 8);
                CHECK(pf.compDesc(i).plane == 0);
        }
}

TEST_CASE("PixelFormat: I_4x8 component byteOffsets are sequential") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.compDesc(0).byteOffset == 0);
        CHECK(pf.compDesc(1).byteOffset == 1);
        CHECK(pf.compDesc(2).byteOffset == 2);
        CHECK(pf.compDesc(3).byteOffset == 3);
}

TEST_CASE("PixelFormat: I_3x8 component byteOffsets are sequential") {
        PixelFormat pf(PixelFormat::I_3x8);
        CHECK(pf.compDesc(0).byteOffset == 0);
        CHECK(pf.compDesc(1).byteOffset == 1);
        CHECK(pf.compDesc(2).byteOffset == 2);
}

TEST_CASE("PixelFormat: I_4x8 lineStride") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.lineStride(0, 1920) == 1920 * 4);
}

TEST_CASE("PixelFormat: I_4x8 planeSize") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080 * 4);
}

// ============================================================================
// I_3x8 / 3x10
// ============================================================================

TEST_CASE("PixelFormat: I_3x8 is valid") {
        PixelFormat pf(PixelFormat::I_3x8);
        CHECK(pf.isValid());
}

TEST_CASE("PixelFormat: I_3x8 lineStride") {
        PixelFormat pf(PixelFormat::I_3x8);
        CHECK(pf.lineStride(0, 1920) == 1920 * 3);
}

TEST_CASE("PixelFormat: I_3x10_DPX is valid") {
        PixelFormat pf(PixelFormat::I_3x10_DPX);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
}

// ============================================================================
// Interleaved 4:2:2 (YUYV / UYVY)
// ============================================================================

TEST_CASE("PixelFormat: I_422_3x8 is valid 422") {
        PixelFormat pf(PixelFormat::I_422_3x8);
        CHECK(pf.isValid());
        CHECK(pf.sampling() == PixelFormat::Sampling422);
}

TEST_CASE("PixelFormat: UYVY 8-bit is valid") {
        PixelFormat pf(PixelFormat::I_422_UYVY_3x8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.sampling() == PixelFormat::Sampling422);
        CHECK(pf.planeCount() == 1);
}

TEST_CASE("PixelFormat: UYVY 8-bit component offsets") {
        PixelFormat pf(PixelFormat::I_422_UYVY_3x8);
        CHECK(pf.compDesc(0).byteOffset == 1);  // Y
        CHECK(pf.compDesc(1).byteOffset == 0);  // Cb
        CHECK(pf.compDesc(2).byteOffset == 2);  // Cr
}

TEST_CASE("PixelFormat: UYVY 8-bit lineStride 1920") {
        PixelFormat pf(PixelFormat::I_422_UYVY_3x8);
        CHECK(pf.lineStride(0, 1920) == 3840);
}

TEST_CASE("PixelFormat: UYVY 10-bit LE is valid") {
        PixelFormat pf(PixelFormat::I_422_UYVY_3x10_LE);
        CHECK(pf.isValid());
        CHECK(pf.bytesPerBlock() == 8);
}

TEST_CASE("PixelFormat: UYVY 12-bit component bits are 12") {
        PixelFormat pf(PixelFormat::I_422_UYVY_3x12_LE);
        for(size_t i = 0; i < pf.compCount(); i++) {
                CHECK(pf.compDesc(i).bits == 12);
        }
}

// ============================================================================
// v210
// ============================================================================

TEST_CASE("PixelFormat: v210 block size") {
        PixelFormat pf(PixelFormat::I_422_v210);
        CHECK(pf.pixelsPerBlock() == 6);
        CHECK(pf.bytesPerBlock() == 16);
}

TEST_CASE("PixelFormat: v210 lineStride 1920") {
        PixelFormat pf(PixelFormat::I_422_v210);
        CHECK(pf.lineStride(0, 1920) == 5120);
}

TEST_CASE("PixelFormat: v210 lineStride 1280 (not divisible by 6)") {
        PixelFormat pf(PixelFormat::I_422_v210);
        size_t stride = pf.lineStride(0, 1280);
        CHECK(stride == 3456);
        CHECK(stride % 128 == 0);
}

TEST_CASE("PixelFormat: v210 lineStride 4096 (DCI 4K)") {
        PixelFormat pf(PixelFormat::I_422_v210);
        size_t stride = pf.lineStride(0, 4096);
        CHECK(stride % 128 == 0);
        CHECK(stride >= (4096 * 16 + 5) / 6);
}

TEST_CASE("PixelFormat: v210 lineStride 720 (SD)") {
        PixelFormat pf(PixelFormat::I_422_v210);
        CHECK(pf.lineStride(0, 720) == 1920);
}

TEST_CASE("PixelFormat: v210 lineStride 6 (minimum)") {
        PixelFormat pf(PixelFormat::I_422_v210);
        CHECK(pf.lineStride(0, 6) == 128);
}

TEST_CASE("PixelFormat: v210 planeSize 1920x1080") {
        PixelFormat pf(PixelFormat::I_422_v210);
        CHECK(pf.planeSize(0, 1920, 1080) == 5120 * 1080);
}

// ============================================================================
// Stride with padding and alignment
// ============================================================================

TEST_CASE("PixelFormat: I_3x8 lineStride with linePad") {
        PixelFormat pf(PixelFormat::I_3x8);
        CHECK(pf.lineStride(0, 100, 4, 1) == 100 * 3 + 4);
}

TEST_CASE("PixelFormat: I_4x8 lineStride with alignment") {
        PixelFormat pf(PixelFormat::I_4x8);
        size_t stride = pf.lineStride(0, 1920, 0, 16);
        CHECK(stride % 16 == 0);
        CHECK(stride >= 1920 * 4);
}

// ============================================================================
// Invalid plane index
// ============================================================================

TEST_CASE("PixelFormat: lineStride for invalid plane returns 0") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.lineStride(1, 1920) == 0);
}

// ============================================================================
// Equality and lookup
// ============================================================================

TEST_CASE("PixelFormat: equality") {
        PixelFormat a(PixelFormat::I_4x8);
        PixelFormat b(PixelFormat::I_4x8);
        PixelFormat c(PixelFormat::I_3x8);
        CHECK(a == b);
        CHECK(a != c);
}

TEST_CASE("PixelFormat: lookup by name") {
        PixelFormat pf(PixelFormat::I_4x8);
        PixelFormat found = PixelFormat::lookup(pf.name());
        CHECK(found.isValid());
        CHECK(found == pf);
}

TEST_CASE("PixelFormat: lookup unknown name returns invalid") {
        PixelFormat found = PixelFormat::lookup("bogus_nonexistent_format");
        CHECK_FALSE(found.isValid());
}

// ============================================================================
// Chroma siting
// ============================================================================

TEST_CASE("PixelFormat: 4:2:2 formats have Left/Top chroma siting") {
        PixelFormat::ID ids422[] = {
                PixelFormat::I_422_3x8,
                PixelFormat::I_422_3x10,
                PixelFormat::I_422_UYVY_3x8,
                PixelFormat::I_422_UYVY_3x10_LE,
                PixelFormat::I_422_v210,
        };
        for(auto id : ids422) {
                PixelFormat pf(id);
                CHECK(pf.chromaSitingH() == PixelFormat::ChromaHLeft);
                CHECK(pf.chromaSitingV() == PixelFormat::ChromaVTop);
        }
}

TEST_CASE("PixelFormat: 4:4:4 formats have Undefined chroma siting") {
        PixelFormat pf(PixelFormat::I_4x8);
        CHECK(pf.chromaSitingH() == PixelFormat::ChromaHUndefined);
        CHECK(pf.chromaSitingV() == PixelFormat::ChromaVUndefined);
}

TEST_CASE("PixelFormat: 4:2:0 formats have Left/Center chroma siting") {
        PixelFormat::ID ids420[] = {
                PixelFormat::P_420_3x8,
                PixelFormat::P_420_3x10_LE,
                PixelFormat::SP_420_8,
                PixelFormat::SP_420_10_LE,
        };
        for(auto id : ids420) {
                PixelFormat pf(id);
                CHECK(pf.chromaSitingH() == PixelFormat::ChromaHLeft);
                CHECK(pf.chromaSitingV() == PixelFormat::ChromaVCenter);
        }
}

// ============================================================================
// Planar 4:2:2 formats
// ============================================================================

TEST_CASE("PixelFormat: P_422_3x8 properties") {
        PixelFormat pf(PixelFormat::P_422_3x8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 3);
        CHECK(pf.sampling() == PixelFormat::Sampling422);
}

TEST_CASE("PixelFormat: P_422_3x8 stride 1920x1080") {
        PixelFormat pf(PixelFormat::P_422_3x8);
        CHECK(pf.lineStride(0, 1920) == 1920);
        CHECK(pf.lineStride(1, 1920) == 960);
        CHECK(pf.lineStride(2, 1920) == 960);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080);
        CHECK(pf.planeSize(1, 1920, 1080) == 960 * 1080);
        CHECK(pf.planeSize(2, 1920, 1080) == 960 * 1080);
}

TEST_CASE("PixelFormat: P_422_3x10_LE stride 1920") {
        PixelFormat pf(PixelFormat::P_422_3x10_LE);
        CHECK(pf.isValid());
        CHECK(pf.lineStride(0, 1920) == 3840);
        CHECK(pf.lineStride(1, 1920) == 1920);
}

// ============================================================================
// Planar 4:2:0 formats
// ============================================================================

TEST_CASE("PixelFormat: P_420_3x8 properties") {
        PixelFormat pf(PixelFormat::P_420_3x8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 3);
        CHECK(pf.sampling() == PixelFormat::Sampling420);
}

TEST_CASE("PixelFormat: P_420_3x8 stride 1920x1080") {
        PixelFormat pf(PixelFormat::P_420_3x8);
        CHECK(pf.lineStride(0, 1920) == 1920);
        CHECK(pf.lineStride(1, 1920) == 960);
        CHECK(pf.lineStride(2, 1920) == 960);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080);
        CHECK(pf.planeSize(1, 1920, 1080) == 960 * 540);
        CHECK(pf.planeSize(2, 1920, 1080) == 960 * 540);
}

TEST_CASE("PixelFormat: P_420_3x8 total is 1.5x luma") {
        PixelFormat pf(PixelFormat::P_420_3x8);
        size_t total = pf.planeSize(0, 1920, 1080) +
                       pf.planeSize(1, 1920, 1080) +
                       pf.planeSize(2, 1920, 1080);
        CHECK(total == 1920 * 1080 * 3 / 2);
}

TEST_CASE("PixelFormat: P_420_3x10_LE stride 1920") {
        PixelFormat pf(PixelFormat::P_420_3x10_LE);
        CHECK(pf.lineStride(0, 1920) == 3840);
        CHECK(pf.lineStride(1, 1920) == 1920);
        CHECK(pf.planeSize(1, 1920, 1080) == 1920 * 540);
}

// ============================================================================
// Semi-planar 4:2:0 (NV12) formats
// ============================================================================

TEST_CASE("PixelFormat: SP_420_8 properties") {
        PixelFormat pf(PixelFormat::SP_420_8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.planeCount() == 2);
        CHECK(pf.sampling() == PixelFormat::Sampling420);
}

TEST_CASE("PixelFormat: SP_420_8 stride 1920x1080") {
        PixelFormat pf(PixelFormat::SP_420_8);
        CHECK(pf.lineStride(0, 1920) == 1920);       // Y: width * 1
        CHECK(pf.lineStride(1, 1920) == 1920);       // CbCr: (width/2) * 2 = width
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080);
        CHECK(pf.planeSize(1, 1920, 1080) == 1920 * 540);
}

TEST_CASE("PixelFormat: SP_420_8 total equals I420 total") {
        PixelFormat nv12(PixelFormat::SP_420_8);
        PixelFormat i420(PixelFormat::P_420_3x8);
        size_t nv12Total = nv12.planeSize(0, 1920, 1080) + nv12.planeSize(1, 1920, 1080);
        size_t i420Total = i420.planeSize(0, 1920, 1080) + i420.planeSize(1, 1920, 1080) + i420.planeSize(2, 1920, 1080);
        CHECK(nv12Total == i420Total);
}

TEST_CASE("PixelFormat: SP_420_10_LE stride 1920") {
        PixelFormat pf(PixelFormat::SP_420_10_LE);
        CHECK(pf.lineStride(0, 1920) == 3840);       // Y: 1920 * 2
        CHECK(pf.lineStride(1, 1920) == 3840);       // CbCr: (1920/2) * 4 = 3840
}

// ============================================================================
// registeredIDs
// ============================================================================

TEST_CASE("PixelFormat: registeredIDs includes all formats") {
        auto ids = PixelFormat::registeredIDs();
        CHECK(ids.size() >= 78);
        CHECK(ids.contains(PixelFormat::I_4x8));
        CHECK(ids.contains(PixelFormat::I_422_UYVY_3x8));
        CHECK(ids.contains(PixelFormat::I_422_v210));
        CHECK(ids.contains(PixelFormat::P_422_3x8));
        CHECK(ids.contains(PixelFormat::P_420_3x8));
        CHECK(ids.contains(PixelFormat::SP_420_8));
        CHECK(ids.contains(PixelFormat::P_420_3x12_BE));
        CHECK(ids.contains(PixelFormat::SP_420_12_BE));
}

// ============================================================================
// Automatic iteration over ALL registered formats
// ============================================================================

TEST_CASE("PixelFormat: all registered formats have valid properties") {
        auto ids = PixelFormat::registeredIDs();
        for(auto id : ids) {
                PixelFormat pf(id);
                CHECK(pf.isValid());
                CHECK_FALSE(pf.name().isEmpty());
                CHECK(pf.compCount() > 0);
                CHECK(pf.planeCount() > 0);
                CHECK(PixelFormat::lookup(pf.name()) == pf);
        }
}

// ============================================================================
// Monochrome formats
// ============================================================================

TEST_CASE("PixelFormat: Monochrome I_1x8") {
        SUBCASE("I_1x8 basic properties") {
                PixelFormat pf(PixelFormat::I_1x8);
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

TEST_CASE("PixelFormat: Float half I_4xF16_LE") {
        SUBCASE("I_4xF16_LE basic properties") {
                PixelFormat pf(PixelFormat::I_4xF16_LE);
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

TEST_CASE("PixelFormat: Float single I_3xF32_LE") {
        SUBCASE("I_3xF32_LE basic properties") {
                PixelFormat pf(PixelFormat::I_3xF32_LE);
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

TEST_CASE("PixelFormat: 10:10:10:2 I_10_10_10_2_LE") {
        SUBCASE("I_10_10_10_2_LE basic properties") {
                PixelFormat pf(PixelFormat::I_10_10_10_2_LE);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 4);
                CHECK(pf.bytesPerBlock() == 4);
                CHECK(pf.lineStride(0, 1920) == 7680);
        }
}

// ============================================================================
// NV21 (semi-planar 4:2:0 CrCb order)
// ============================================================================

TEST_CASE("PixelFormat: NV21 SP_420_NV21_8") {
        SUBCASE("SP_420_NV21_8 basic properties") {
                PixelFormat pf(PixelFormat::SP_420_NV21_8);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.planeCount() == 2);
                CHECK(pf.sampling() == PixelFormat::Sampling420);
        }
}

// ============================================================================
// NV16 (semi-planar 4:2:2)
// ============================================================================

TEST_CASE("PixelFormat: NV16 SP_422_8") {
        SUBCASE("SP_422_8 basic properties") {
                PixelFormat pf(PixelFormat::SP_422_8);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.planeCount() == 2);
                CHECK(pf.sampling() == PixelFormat::Sampling422);
        }
}

// ============================================================================
// 4:1:1 planar
// ============================================================================

TEST_CASE("PixelFormat: 4:1:1 P_411_3x8") {
        SUBCASE("P_411_3x8 basic properties") {
                PixelFormat pf(PixelFormat::P_411_3x8);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.planeCount() == 3);
                CHECK(pf.sampling() == PixelFormat::Sampling411);
        }
}

// ============================================================================
// 16-bit UYVY
// ============================================================================

TEST_CASE("PixelFormat: 16-bit UYVY I_422_UYVY_3x16_LE") {
        SUBCASE("I_422_UYVY_3x16_LE basic properties") {
                PixelFormat pf(PixelFormat::I_422_UYVY_3x16_LE);
                CHECK(pf.isValid());
                CHECK(pf.compCount() == 3);
                CHECK(pf.sampling() == PixelFormat::Sampling422);
        }
}
