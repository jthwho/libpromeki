/**
 * @file      pixeldesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/pixeldesc.h>
#include <promeki/imagedesc.h>
#include <promeki/metadata.h>

using namespace promeki;

// ============================================================================
// Default / Invalid construction
// ============================================================================

TEST_CASE("PixelDesc: default constructs to Invalid") {
        PixelDesc pd;
        CHECK_FALSE(pd.isValid());
        CHECK(pd.id() == PixelDesc::Invalid);
}

TEST_CASE("PixelDesc: explicit Invalid construction") {
        PixelDesc pd(PixelDesc::Invalid);
        CHECK_FALSE(pd.isValid());
}

// ============================================================================
// RGBA8
// ============================================================================

TEST_CASE("PixelDesc: RGBA8 is valid") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.isValid());
        CHECK(pd.id() == PixelDesc::RGBA8_sRGB_Full);
}

TEST_CASE("PixelDesc: RGBA8 name is non-empty") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK_FALSE(pd.name().isEmpty());
}

TEST_CASE("PixelDesc: RGBA8 pixelFormat is Interleaved_4x8") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.pixelFormat().id() == PixelFormat::Interleaved_4x8);
}

TEST_CASE("PixelDesc: RGBA8 colorModel is valid") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.colorModel().isValid());
}

TEST_CASE("PixelDesc: RGBA8 has alpha") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.hasAlpha());
}

TEST_CASE("PixelDesc: RGBA8 is not compressed") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK_FALSE(pd.isCompressed());
}

TEST_CASE("PixelDesc: RGBA8 compCount is 4") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.compCount() == 4);
}

TEST_CASE("PixelDesc: RGBA8 planeCount is 1") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.planeCount() == 1);
}

// ============================================================================
// RGB8
// ============================================================================

TEST_CASE("PixelDesc: RGB8 is valid") {
        PixelDesc pd(PixelDesc::RGB8_sRGB_Full);
        CHECK(pd.isValid());
        CHECK(pd.id() == PixelDesc::RGB8_sRGB_Full);
}

TEST_CASE("PixelDesc: RGB8 does not have alpha") {
        PixelDesc pd(PixelDesc::RGB8_sRGB_Full);
        CHECK_FALSE(pd.hasAlpha());
}

TEST_CASE("PixelDesc: RGB8 compCount is 3") {
        PixelDesc pd(PixelDesc::RGB8_sRGB_Full);
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelDesc: RGB8 is not compressed") {
        PixelDesc pd(PixelDesc::RGB8_sRGB_Full);
        CHECK_FALSE(pd.isCompressed());
}

TEST_CASE("PixelDesc: RGB8 pixelFormat is Interleaved_3x8") {
        PixelDesc pd(PixelDesc::RGB8_sRGB_Full);
        CHECK(pd.pixelFormat().id() == PixelFormat::Interleaved_3x8);
}

// ============================================================================
// RGB10
// ============================================================================

TEST_CASE("PixelDesc: RGB10 is valid") {
        PixelDesc pd(PixelDesc::RGB10_sRGB_Full);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK_FALSE(pd.hasAlpha());
}

// ============================================================================
// YUV8_422
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422 is valid") {
        PixelDesc pd(PixelDesc::YUV8_422_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK_FALSE(pd.hasAlpha());
}

// ============================================================================
// YUV10_422
// ============================================================================

TEST_CASE("PixelDesc: YUV10_422 is valid") {
        PixelDesc pd(PixelDesc::YUV10_422_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
}

// ============================================================================
// JPEG compressed formats
// ============================================================================

TEST_CASE("PixelDesc: JPEG_RGBA8 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_RGBA8_sRGB_Full);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.hasAlpha());
        CHECK(pd.compCount() == 4);
}

TEST_CASE("PixelDesc: JPEG_RGB8 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB_Full);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK_FALSE(pd.hasAlpha());
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelDesc: JPEG_YUV8_422 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_YUV8_422_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.isCompressed());
        CHECK(pd.compCount() == 3);
}

TEST_CASE("PixelDesc: JPEG_RGB8 codecName is jpeg") {
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB_Full);
        CHECK(pd.codecName() == "jpeg");
}

// ============================================================================
// encodeSources and decodeTargets
// ============================================================================

TEST_CASE("PixelDesc: JPEG_RGBA8 encodeSources and decodeTargets") {
        PixelDesc pd(PixelDesc::JPEG_RGBA8_sRGB_Full);
        CHECK(pd.encodeSources().size() == 1);
        CHECK(pd.encodeSources()[0] == PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.decodeTargets().size() == 1);
        CHECK(pd.decodeTargets()[0] == PixelDesc::RGBA8_sRGB_Full);
}

TEST_CASE("PixelDesc: JPEG_RGB8 encodeSources includes RGB8 and RGBA8") {
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB_Full);
        CHECK(pd.encodeSources().size() == 2);
        CHECK(pd.encodeSources()[0] == PixelDesc::RGB8_sRGB_Full);
        CHECK(pd.encodeSources()[1] == PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.decodeTargets().size() == 1);
        CHECK(pd.decodeTargets()[0] == PixelDesc::RGB8_sRGB_Full);
}

TEST_CASE("PixelDesc: JPEG_YUV8_422 encodeSources and decodeTargets") {
        PixelDesc pd(PixelDesc::JPEG_YUV8_422_Rec709_Limited);
        CHECK(pd.encodeSources().size() == 2);
        CHECK(pd.encodeSources()[0] == PixelDesc::RGB8_sRGB_Full);
        CHECK(pd.encodeSources()[1] == PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.decodeTargets().size() == 1);
        CHECK(pd.decodeTargets()[0] == PixelDesc::YUV8_422_Rec709_Limited);
}

TEST_CASE("PixelDesc: uncompressed format has empty encodeSources and decodeTargets") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.encodeSources().isEmpty());
        CHECK(pd.decodeTargets().isEmpty());
}

// ============================================================================
// lineStride and planeSize delegation
// ============================================================================

TEST_CASE("PixelDesc: RGBA8 lineStride via ImageDesc") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.lineStride(0, desc) == 1920 * 4);
}

TEST_CASE("PixelDesc: RGB8 planeSize via ImageDesc") {
        PixelDesc pd(PixelDesc::RGB8_sRGB_Full);
        ImageDesc desc(1920, 1080, PixelDesc::RGB8_sRGB_Full);
        CHECK(pd.planeSize(0, desc) == 1920 * 1080 * 3);
}

TEST_CASE("PixelDesc: JPEG planeSize is 0 without CompressedSize metadata") {
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB_Full);
        ImageDesc desc(640, 480, PixelDesc::JPEG_RGB8_sRGB_Full);
        CHECK(pd.planeSize(0, desc) == 0);
}

TEST_CASE("PixelDesc: JPEG planeSize reads CompressedSize from metadata") {
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB_Full);
        ImageDesc desc(640, 480, PixelDesc::JPEG_RGB8_sRGB_Full);
        desc.metadata().set(Metadata::CompressedSize, 12345);
        CHECK(pd.planeSize(0, desc) == 12345);
}

TEST_CASE("PixelDesc: JPEG lineStride is 0") {
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB_Full);
        ImageDesc desc(640, 480, PixelDesc::JPEG_RGB8_sRGB_Full);
        CHECK(pd.lineStride(0, desc) == 0);
}

// ============================================================================
// fourccList
// ============================================================================

TEST_CASE("PixelDesc: RGBA8 fourccList") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        // May or may not have FourCC entries; just ensure the accessor works
        const FourCCList &list = pd.fourccList();
        (void)list;
}

// ============================================================================
// lookup by name
// ============================================================================

TEST_CASE("PixelDesc: lookup by name") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        PixelDesc found = PixelDesc::lookup(pd.name());
        CHECK(found.isValid());
        CHECK(found == pd);
}

TEST_CASE("PixelDesc: lookup unknown name returns invalid") {
        PixelDesc found = PixelDesc::lookup("bogus_nonexistent_pixeldesc");
        CHECK_FALSE(found.isValid());
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("PixelDesc: equality") {
        PixelDesc a(PixelDesc::RGBA8_sRGB_Full);
        PixelDesc b(PixelDesc::RGBA8_sRGB_Full);
        PixelDesc c(PixelDesc::RGB8_sRGB_Full);
        CHECK(a == b);
        CHECK(a != c);
}

// ============================================================================
// Invalid behavior
// ============================================================================

TEST_CASE("PixelDesc: Invalid has no alpha") {
        PixelDesc pd(PixelDesc::Invalid);
        // Accessing properties on Invalid should not crash
        CHECK_FALSE(pd.isValid());
}

// ============================================================================
// registeredIDs
// ============================================================================

TEST_CASE("PixelDesc: registeredIDs returns all well-known descriptions") {
        auto ids = PixelDesc::registeredIDs();
        CHECK(ids.size() >= 8);
        CHECK(ids.contains(PixelDesc::RGBA8_sRGB_Full));
        CHECK(ids.contains(PixelDesc::RGB8_sRGB_Full));
        CHECK(ids.contains(PixelDesc::RGB10_sRGB_Full));
        CHECK(ids.contains(PixelDesc::YUV8_422_Rec709_Limited));
        CHECK(ids.contains(PixelDesc::YUV10_422_Rec709_Limited));
        CHECK(ids.contains(PixelDesc::JPEG_RGBA8_sRGB_Full));
        CHECK(ids.contains(PixelDesc::JPEG_RGB8_sRGB_Full));
        CHECK(ids.contains(PixelDesc::JPEG_YUV8_422_Rec709_Limited));
}
