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
// Interleaved_4x8 (e.g. RGBA8 layout)
// ============================================================================

TEST_CASE("PixelFormat: Interleaved_4x8 is valid") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.isValid());
        CHECK(pf.id() == PixelFormat::Interleaved_4x8);
}

TEST_CASE("PixelFormat: Interleaved_4x8 name is non-empty") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK_FALSE(pf.name().isEmpty());
}

TEST_CASE("PixelFormat: Interleaved_4x8 compCount is 4") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.compCount() == 4);
}

TEST_CASE("PixelFormat: Interleaved_4x8 planeCount is 1") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.planeCount() == 1);
}

TEST_CASE("PixelFormat: Interleaved_4x8 bytesPerBlock is 4") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.bytesPerBlock() == 4);
}

TEST_CASE("PixelFormat: Interleaved_4x8 sampling is 444") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.sampling() == PixelFormat::Sampling444);
}

TEST_CASE("PixelFormat: Interleaved_4x8 component bits are 8") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        for(size_t i = 0; i < pf.compCount(); i++) {
                CHECK(pf.compDesc(i).bits == 8);
                CHECK(pf.compDesc(i).plane == 0);
        }
}

TEST_CASE("PixelFormat: Interleaved_4x8 component byteOffsets are sequential") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.compDesc(0).byteOffset == 0);
        CHECK(pf.compDesc(1).byteOffset == 1);
        CHECK(pf.compDesc(2).byteOffset == 2);
        CHECK(pf.compDesc(3).byteOffset == 3);
}

TEST_CASE("PixelFormat: Interleaved_3x8 component byteOffsets are sequential") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        CHECK(pf.compDesc(0).byteOffset == 0);
        CHECK(pf.compDesc(1).byteOffset == 1);
        CHECK(pf.compDesc(2).byteOffset == 2);
}

TEST_CASE("PixelFormat: Interleaved_4x8 lineStride") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.lineStride(0, 1920) == 1920 * 4);
}

TEST_CASE("PixelFormat: Interleaved_4x8 planeSize") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080 * 4);
}

// ============================================================================
// Interleaved_3x8 (e.g. RGB8 layout)
// ============================================================================

TEST_CASE("PixelFormat: Interleaved_3x8 is valid") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        CHECK(pf.isValid());
        CHECK(pf.id() == PixelFormat::Interleaved_3x8);
}

TEST_CASE("PixelFormat: Interleaved_3x8 compCount is 3") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        CHECK(pf.compCount() == 3);
}

TEST_CASE("PixelFormat: Interleaved_3x8 planeCount is 1") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        CHECK(pf.planeCount() == 1);
}

TEST_CASE("PixelFormat: Interleaved_3x8 bytesPerBlock is 3") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        CHECK(pf.bytesPerBlock() == 3);
}

TEST_CASE("PixelFormat: Interleaved_3x8 sampling is 444") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        CHECK(pf.sampling() == PixelFormat::Sampling444);
}

TEST_CASE("PixelFormat: Interleaved_3x8 lineStride") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        CHECK(pf.lineStride(0, 1920) == 1920 * 3);
}

TEST_CASE("PixelFormat: Interleaved_3x8 planeSize") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        CHECK(pf.planeSize(0, 1920, 1080) == 1920 * 1080 * 3);
}

// ============================================================================
// Interleaved_3x10 (e.g. RGB10 layout)
// ============================================================================

TEST_CASE("PixelFormat: Interleaved_3x10 is valid") {
        PixelFormat pf(PixelFormat::Interleaved_3x10);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
}

TEST_CASE("PixelFormat: Interleaved_3x10 component bits are 10") {
        PixelFormat pf(PixelFormat::Interleaved_3x10);
        for(size_t i = 0; i < pf.compCount(); i++) {
                CHECK(pf.compDesc(i).bits == 10);
        }
}

TEST_CASE("PixelFormat: Interleaved_3x10 bytesPerBlock") {
        PixelFormat pf(PixelFormat::Interleaved_3x10);
        CHECK(pf.bytesPerBlock() > 0);
}

// ============================================================================
// Interleaved_422_3x8 (e.g. YUV8_422 layout)
// ============================================================================

TEST_CASE("PixelFormat: Interleaved_422_3x8 is valid") {
        PixelFormat pf(PixelFormat::Interleaved_422_3x8);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
}

TEST_CASE("PixelFormat: Interleaved_422_3x8 sampling is 422") {
        PixelFormat pf(PixelFormat::Interleaved_422_3x8);
        CHECK(pf.sampling() == PixelFormat::Sampling422);
}

TEST_CASE("PixelFormat: Interleaved_422_3x8 planeCount is 1") {
        PixelFormat pf(PixelFormat::Interleaved_422_3x8);
        CHECK(pf.planeCount() == 1);
}

// ============================================================================
// Interleaved_422_3x10 (e.g. YUV10_422 layout)
// ============================================================================

TEST_CASE("PixelFormat: Interleaved_422_3x10 is valid") {
        PixelFormat pf(PixelFormat::Interleaved_422_3x10);
        CHECK(pf.isValid());
        CHECK(pf.compCount() == 3);
        CHECK(pf.sampling() == PixelFormat::Sampling422);
}

// ============================================================================
// lineStride with padding and alignment
// ============================================================================

TEST_CASE("PixelFormat: Interleaved_3x8 lineStride with linePad") {
        PixelFormat pf(PixelFormat::Interleaved_3x8);
        size_t stride = pf.lineStride(0, 100, 4, 1);
        CHECK(stride == 100 * 3 + 4);
}

TEST_CASE("PixelFormat: Interleaved_4x8 lineStride with alignment") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        // 1920 * 4 = 7680, already aligned to any power-of-2 up to 128
        size_t stride = pf.lineStride(0, 1920, 0, 16);
        CHECK(stride % 16 == 0);
        CHECK(stride >= 1920 * 4);
}

// ============================================================================
// Invalid plane index
// ============================================================================

TEST_CASE("PixelFormat: lineStride for invalid plane returns 0") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.lineStride(1, 1920) == 0);
}

TEST_CASE("PixelFormat: planeSize for invalid plane returns 0") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        CHECK(pf.planeSize(1, 1920, 1080) == 0);
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("PixelFormat: equality") {
        PixelFormat a(PixelFormat::Interleaved_4x8);
        PixelFormat b(PixelFormat::Interleaved_4x8);
        PixelFormat c(PixelFormat::Interleaved_3x8);
        CHECK(a == b);
        CHECK(a != c);
}

// ============================================================================
// lookup by name
// ============================================================================

TEST_CASE("PixelFormat: lookup by name") {
        PixelFormat pf(PixelFormat::Interleaved_4x8);
        PixelFormat found = PixelFormat::lookup(pf.name());
        CHECK(found.isValid());
        CHECK(found == pf);
}

TEST_CASE("PixelFormat: lookup unknown name returns invalid") {
        PixelFormat found = PixelFormat::lookup("bogus_nonexistent_format");
        CHECK_FALSE(found.isValid());
}

TEST_CASE("PixelFormat: registeredIDs returns all well-known formats") {
        auto ids = PixelFormat::registeredIDs();
        CHECK(ids.size() >= 5);
        CHECK(ids.contains(PixelFormat::Interleaved_4x8));
        CHECK(ids.contains(PixelFormat::Interleaved_3x8));
        CHECK(ids.contains(PixelFormat::Interleaved_3x10));
        CHECK(ids.contains(PixelFormat::Interleaved_422_3x8));
        CHECK(ids.contains(PixelFormat::Interleaved_422_3x10));
}
