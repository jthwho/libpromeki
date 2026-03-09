/**
 * @file      image2.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/unittest.h>
#include <promeki/image.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

PROMEKI_TEST_BEGIN(Image_Default)
    Image img;
    PROMEKI_TEST(!img.isValid());
    PROMEKI_TEST(img.referenceCount() == 1);
PROMEKI_TEST_END()

// ============================================================================
// Construction with desc
// ============================================================================

PROMEKI_TEST_BEGIN(Image_Construct)
    Image img(1920, 1080, PixelFormat::RGBA8);
    PROMEKI_TEST(img.isValid());
    PROMEKI_TEST(img.width() == 1920);
    PROMEKI_TEST(img.height() == 1080);
    PROMEKI_TEST(img.pixelFormatID() == PixelFormat::RGBA8);
    PROMEKI_TEST(img.referenceCount() == 1);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(Image_ConstructFromDesc)
    ImageDesc desc(1920, 1080, PixelFormat::RGB8);
    Image img(desc);
    PROMEKI_TEST(img.isValid());
    PROMEKI_TEST(img.width() == 1920);
    PROMEKI_TEST(img.pixelFormatID() == PixelFormat::RGB8);
PROMEKI_TEST_END()

// ============================================================================
// Fill
// ============================================================================

PROMEKI_TEST_BEGIN(Image_Fill)
    Image img(64, 64, PixelFormat::RGBA8);
    PROMEKI_TEST(img.isValid());
    PROMEKI_TEST(img.fill(0xAB));

    const uint8_t *ptr = static_cast<const uint8_t *>(img.data());
    PROMEKI_TEST(ptr[0] == 0xAB);
    PROMEKI_TEST(ptr[1] == 0xAB);
PROMEKI_TEST_END()

// ============================================================================
// Plane access
// ============================================================================

PROMEKI_TEST_BEGIN(Image_Plane)
    Image img(64, 64, PixelFormat::RGBA8);
    PROMEKI_TEST(img.isValid());
    Buffer p = img.plane(0);
    PROMEKI_TEST(p.isValid());
    PROMEKI_TEST(p.data() != nullptr);
PROMEKI_TEST_END()

// ============================================================================
// Shared copy (COW)
// ============================================================================

PROMEKI_TEST_BEGIN(Image_SharedCopy)
    Image img1(64, 64, PixelFormat::RGBA8);
    Image img2 = img1;
    PROMEKI_TEST(img1.referenceCount() == 2);
    PROMEKI_TEST(img2.referenceCount() == 2);
    PROMEKI_TEST(img1.width() == img2.width());
    PROMEKI_TEST(img1.height() == img2.height());
PROMEKI_TEST_END()

// ============================================================================
// Metadata
// ============================================================================

PROMEKI_TEST_BEGIN(Image_Metadata)
    Image img(64, 64, PixelFormat::RGBA8);
    PROMEKI_TEST(img.metadata().isEmpty());

    img.metadata().set(Metadata::Title, String("Test Image"));
    PROMEKI_TEST(!img.metadata().isEmpty());
    PROMEKI_TEST(img.metadata().get(Metadata::Title).get<String>() == "Test Image");
PROMEKI_TEST_END()

// ============================================================================
// Line stride
// ============================================================================

PROMEKI_TEST_BEGIN(Image_LineStride)
    Image img(1920, 1080, PixelFormat::RGBA8);
    PROMEKI_TEST(img.lineStride() == 1920 * 4);
PROMEKI_TEST_END()
