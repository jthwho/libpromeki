/**
 * @file      imagedesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/imagedesc.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("ImageDesc_Default") {
    ImageDesc desc;
    CHECK(!desc.isValid());
    CHECK(desc.pixelDesc().id() == PixelDesc::Invalid);
}

// ============================================================================
// Construction with size and pixel format
// ============================================================================

TEST_CASE("ImageDesc_Construct") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    CHECK(desc.isValid());
    CHECK(desc.width() == 1920);
    CHECK(desc.height() == 1080);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
}

TEST_CASE("ImageDesc_ConstructSize2D") {
    Size2Du32 sz(3840, 2160);
    ImageDesc desc(sz, PixelDesc::RGB8_sRGB);
    CHECK(desc.isValid());
    CHECK(desc.width() == 3840);
    CHECK(desc.height() == 2160);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("ImageDesc_SetSize") {
    ImageDesc desc;
    desc.setSize(Size2Du32(640, 480));
    CHECK(desc.width() == 640);
    CHECK(desc.height() == 480);

    desc.setSize(1280, 720);
    CHECK(desc.width() == 1280);
    CHECK(desc.height() == 720);
}

TEST_CASE("ImageDesc_SetPixelFormat") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGBA8_sRGB);

    desc.setPixelDesc(PixelDesc::RGB8_sRGB);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

TEST_CASE("ImageDesc_SetLinePad") {
    ImageDesc desc;
    CHECK(desc.linePad() == 0);

    desc.setLinePad(64);
    CHECK(desc.linePad() == 64);
}

TEST_CASE("ImageDesc_SetLineAlign") {
    ImageDesc desc;
    CHECK(desc.lineAlign() == 1);

    desc.setLineAlign(16);
    CHECK(desc.lineAlign() == 16);
}

TEST_CASE("ImageDesc_SetInterlaced") {
    ImageDesc desc;
    CHECK(desc.interlaced() == false);

    desc.setInterlaced(true);
    CHECK(desc.interlaced() == true);
}

// ============================================================================
// Copy semantics (plain value, no internal COW)
// ============================================================================

TEST_CASE("ImageDesc_CopyIsIndependent") {
    ImageDesc d1(1920, 1080, PixelDesc::RGBA8_sRGB);
    ImageDesc d2 = d1;

    d2.setSize(3840, 2160);
    CHECK(d1.width() == 1920);
    CHECK(d2.width() == 3840);
}

TEST_CASE("ImageDesc_CopyPixelFormatIndependent") {
    ImageDesc d1(1920, 1080, PixelDesc::RGBA8_sRGB);
    ImageDesc d2 = d1;

    d2.setPixelDesc(PixelDesc::RGB8_sRGB);
    CHECK(d1.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
    CHECK(d2.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("ImageDesc_Metadata") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    const Metadata &cm = desc.metadata();
    CHECK(cm.isEmpty());

    desc.metadata().set(Metadata::Title, String("Test Image"));
    CHECK(!desc.metadata().isEmpty());
    CHECK(desc.metadata().get(Metadata::Title).get<String>() == "Test Image");
}

// ============================================================================
// toString
// ============================================================================

TEST_CASE("ImageDesc_ToString") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    String s = desc.toString();
    CHECK(s.size() > 0);
}

// ============================================================================
// PlaneCount
// ============================================================================

TEST_CASE("ImageDesc_PlaneCount") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    CHECK(desc.planeCount() > 0);
}
