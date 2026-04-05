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
// Default / Invalid
// ============================================================================

TEST_CASE("PixelDesc: default constructs to Invalid") {
        PixelDesc pd;
        CHECK_FALSE(pd.isValid());
        CHECK(pd.id() == PixelDesc::Invalid);
}

// ============================================================================
// RGBA8
// ============================================================================

TEST_CASE("PixelDesc: RGBA8 is valid") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.isValid());
        CHECK(pd.pixelFormat().id() == PixelFormat::Interleaved_4x8);
        CHECK(pd.hasAlpha());
        CHECK_FALSE(pd.isCompressed());
        CHECK(pd.compCount() == 4);
        CHECK(pd.planeCount() == 1);
}

// ============================================================================
// RGB8
// ============================================================================

TEST_CASE("PixelDesc: RGB8 is valid") {
        PixelDesc pd(PixelDesc::RGB8_sRGB_Full);
        CHECK(pd.isValid());
        CHECK_FALSE(pd.hasAlpha());
        CHECK(pd.compCount() == 3);
}

// ============================================================================
// YUV8_422 (YUYV)
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422 is valid") {
        PixelDesc pd(PixelDesc::YUV8_422_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK_FALSE(pd.hasAlpha());
}

TEST_CASE("PixelDesc: YUV8_422 YUYV has YUY2 and YUYV FourCCs") {
        PixelDesc pd(PixelDesc::YUV8_422_Rec709_Limited);
        CHECK(pd.fourccList().size() == 2);
        CHECK(pd.fourccList()[0] == FourCC("YUY2"));
        CHECK(pd.fourccList()[1] == FourCC("YUYV"));
}

// ============================================================================
// JPEG compressed formats
// ============================================================================

TEST_CASE("PixelDesc: JPEG_RGBA8 is compressed") {
        PixelDesc pd(PixelDesc::JPEG_RGBA8_sRGB_Full);
        CHECK(pd.isCompressed());
        CHECK(pd.hasAlpha());
}

TEST_CASE("PixelDesc: JPEG_RGB8 codecName is jpeg") {
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB_Full);
        CHECK(pd.codecName() == "jpeg");
}

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
}

TEST_CASE("PixelDesc: JPEG_YUV8_422 encodeSources and decodeTargets") {
        PixelDesc pd(PixelDesc::JPEG_YUV8_422_Rec709_Limited);
        CHECK(pd.encodeSources().size() == 5);
        CHECK(pd.encodeSources().contains(PixelDesc::RGB8_sRGB_Full));
        CHECK(pd.encodeSources().contains(PixelDesc::RGBA8_sRGB_Full));
        CHECK(pd.encodeSources().contains(PixelDesc::YUV8_422_Rec709_Limited));
        CHECK(pd.encodeSources().contains(PixelDesc::YUV8_422_UYVY_Rec709_Limited));
        CHECK(pd.encodeSources().contains(PixelDesc::YUV8_422_Planar_Rec709_Limited));
        CHECK(pd.decodeTargets().size() == 5);
        CHECK(pd.decodeTargets().contains(PixelDesc::YUV8_422_Rec709_Limited));
        CHECK(pd.decodeTargets().contains(PixelDesc::YUV8_422_UYVY_Rec709_Limited));
        CHECK(pd.decodeTargets().contains(PixelDesc::YUV8_422_Planar_Rec709_Limited));
        CHECK(pd.decodeTargets().contains(PixelDesc::RGB8_sRGB_Full));
        CHECK(pd.decodeTargets().contains(PixelDesc::RGBA8_sRGB_Full));
}

// ============================================================================
// lineStride and planeSize delegation
// ============================================================================

TEST_CASE("PixelDesc: RGBA8 lineStride via ImageDesc") {
        ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB_Full);
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        CHECK(pd.lineStride(0, desc) == 1920 * 4);
}

TEST_CASE("PixelDesc: JPEG planeSize reads CompressedSize from metadata") {
        ImageDesc desc(640, 480, PixelDesc::JPEG_RGB8_sRGB_Full);
        desc.metadata().set(Metadata::CompressedSize, 12345);
        PixelDesc pd(PixelDesc::JPEG_RGB8_sRGB_Full);
        CHECK(pd.planeSize(0, desc) == 12345);
}

// ============================================================================
// Lookup and equality
// ============================================================================

TEST_CASE("PixelDesc: lookup by name") {
        PixelDesc pd(PixelDesc::RGBA8_sRGB_Full);
        PixelDesc found = PixelDesc::lookup(pd.name());
        CHECK(found == pd);
}

TEST_CASE("PixelDesc: equality") {
        PixelDesc a(PixelDesc::RGBA8_sRGB_Full);
        PixelDesc b(PixelDesc::RGBA8_sRGB_Full);
        PixelDesc c(PixelDesc::RGB8_sRGB_Full);
        CHECK(a == b);
        CHECK(a != c);
}

// ============================================================================
// UYVY 8-bit
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422_UYVY is valid") {
        PixelDesc pd(PixelDesc::YUV8_422_UYVY_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.pixelFormat().id() == PixelFormat::Interleaved_422_UYVY_3x8);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
}

TEST_CASE("PixelDesc: YUV8_422_UYVY has UYVY FourCC") {
        PixelDesc pd(PixelDesc::YUV8_422_UYVY_Rec709_Limited);
        CHECK(pd.fourccList()[0] == FourCC("UYVY"));
}

TEST_CASE("PixelDesc: YUV8_422_UYVY limited range") {
        PixelDesc pd(PixelDesc::YUV8_422_UYVY_Rec709_Limited);
        CHECK(pd.compSemantic(0).rangeMin == 16);
        CHECK(pd.compSemantic(0).rangeMax == 235);
}

// ============================================================================
// 10/12-bit UYVY and v210
// ============================================================================

TEST_CASE("PixelDesc: YUV10_422_UYVY_LE is valid") {
        PixelDesc pd(PixelDesc::YUV10_422_UYVY_LE_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.compSemantic(0).rangeMax == 940);
}

TEST_CASE("PixelDesc: YUV12_422_UYVY_LE is valid") {
        PixelDesc pd(PixelDesc::YUV12_422_UYVY_LE_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.compSemantic(0).rangeMax == 3760);
}

TEST_CASE("PixelDesc: YUV10_422_v210 has v210 FourCC") {
        PixelDesc pd(PixelDesc::YUV10_422_v210_Rec709_Limited);
        CHECK(pd.fourccList()[0] == FourCC("v210"));
}

// ============================================================================
// Planar 4:2:2 PixelDescs
// ============================================================================

TEST_CASE("PixelDesc: YUV8_422_Planar is valid") {
        PixelDesc pd(PixelDesc::YUV8_422_Planar_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 3);
        CHECK(pd.pixelFormat().id() == PixelFormat::Planar_422_3x8);
        CHECK(pd.colorModel().id() == ColorModel::YCbCr_Rec709);
}

TEST_CASE("PixelDesc: YUV8_422_Planar has I422 FourCC") {
        PixelDesc pd(PixelDesc::YUV8_422_Planar_Rec709_Limited);
        CHECK(pd.fourccList()[0] == FourCC("I422"));
}

// ============================================================================
// Planar 4:2:0 PixelDescs
// ============================================================================

TEST_CASE("PixelDesc: YUV8_420_Planar is valid") {
        PixelDesc pd(PixelDesc::YUV8_420_Planar_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 3);
        CHECK(pd.pixelFormat().id() == PixelFormat::Planar_420_3x8);
}

TEST_CASE("PixelDesc: YUV8_420_Planar has I420 FourCC") {
        PixelDesc pd(PixelDesc::YUV8_420_Planar_Rec709_Limited);
        CHECK(pd.fourccList()[0] == FourCC("I420"));
}

TEST_CASE("PixelDesc: YUV10_420_Planar_LE is valid") {
        PixelDesc pd(PixelDesc::YUV10_420_Planar_LE_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.pixelFormat().id() == PixelFormat::Planar_420_3x10_LE);
}

TEST_CASE("PixelDesc: YUV12_420_Planar_BE is valid") {
        PixelDesc pd(PixelDesc::YUV12_420_Planar_BE_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.pixelFormat().id() == PixelFormat::Planar_420_3x12_BE);
}

// ============================================================================
// Semi-planar 4:2:0 PixelDescs
// ============================================================================

TEST_CASE("PixelDesc: YUV8_420_SemiPlanar is valid") {
        PixelDesc pd(PixelDesc::YUV8_420_SemiPlanar_Rec709_Limited);
        CHECK(pd.isValid());
        CHECK(pd.compCount() == 3);
        CHECK(pd.planeCount() == 2);
        CHECK(pd.pixelFormat().id() == PixelFormat::SemiPlanar_420_8);
}

TEST_CASE("PixelDesc: YUV8_420_SemiPlanar has NV12 FourCC") {
        PixelDesc pd(PixelDesc::YUV8_420_SemiPlanar_Rec709_Limited);
        CHECK(pd.fourccList()[0] == FourCC("NV12"));
}

// ============================================================================
// registeredIDs
// ============================================================================

TEST_CASE("PixelDesc: registeredIDs includes all formats") {
        auto ids = PixelDesc::registeredIDs();
        CHECK(ids.size() >= 29);
        CHECK(ids.contains(PixelDesc::RGBA8_sRGB_Full));
        CHECK(ids.contains(PixelDesc::YUV8_422_UYVY_Rec709_Limited));
        CHECK(ids.contains(PixelDesc::YUV10_422_v210_Rec709_Limited));
        CHECK(ids.contains(PixelDesc::YUV8_422_Planar_Rec709_Limited));
        CHECK(ids.contains(PixelDesc::YUV8_420_Planar_Rec709_Limited));
        CHECK(ids.contains(PixelDesc::YUV8_420_SemiPlanar_Rec709_Limited));
        CHECK(ids.contains(PixelDesc::YUV12_420_SemiPlanar_BE_Rec709_Limited));
}
