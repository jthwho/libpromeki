/**
 * @file      imagedesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information
 */

#include <promeki/unittest.h>
#include <promeki/imagedesc.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

PROMEKI_TEST_BEGIN(ImageDesc_Default)
    ImageDesc desc;
    PROMEKI_TEST(!desc.isValid());
    PROMEKI_TEST(desc.pixelFormatID() == PixelFormat::Invalid);
    PROMEKI_TEST(desc.referenceCount() == 1);
PROMEKI_TEST_END()

// ============================================================================
// Construction with size and pixel format
// ============================================================================

PROMEKI_TEST_BEGIN(ImageDesc_Construct)
    ImageDesc desc(1920, 1080, PixelFormat::RGBA8);
    PROMEKI_TEST(desc.isValid());
    PROMEKI_TEST(desc.width() == 1920);
    PROMEKI_TEST(desc.height() == 1080);
    PROMEKI_TEST(desc.pixelFormatID() == PixelFormat::RGBA8);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(ImageDesc_ConstructSize2D)
    Size2D sz(3840, 2160);
    ImageDesc desc(sz, PixelFormat::RGB8);
    PROMEKI_TEST(desc.isValid());
    PROMEKI_TEST(desc.width() == 3840);
    PROMEKI_TEST(desc.height() == 2160);
    PROMEKI_TEST(desc.pixelFormatID() == PixelFormat::RGB8);
PROMEKI_TEST_END()

// ============================================================================
// Setters
// ============================================================================

PROMEKI_TEST_BEGIN(ImageDesc_SetSize)
    ImageDesc desc;
    desc.setSize(Size2D(640, 480));
    PROMEKI_TEST(desc.width() == 640);
    PROMEKI_TEST(desc.height() == 480);

    desc.setSize(1280, 720);
    PROMEKI_TEST(desc.width() == 1280);
    PROMEKI_TEST(desc.height() == 720);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(ImageDesc_SetPixelFormat)
    ImageDesc desc(1920, 1080, PixelFormat::RGBA8);
    PROMEKI_TEST(desc.pixelFormatID() == PixelFormat::RGBA8);

    desc.setPixelFormat(PixelFormat::RGB8);
    PROMEKI_TEST(desc.pixelFormatID() == PixelFormat::RGB8);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(ImageDesc_SetLinePad)
    ImageDesc desc;
    PROMEKI_TEST(desc.linePad() == 0);

    desc.setLinePad(64);
    PROMEKI_TEST(desc.linePad() == 64);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(ImageDesc_SetLineAlign)
    ImageDesc desc;
    PROMEKI_TEST(desc.lineAlign() == 1);

    desc.setLineAlign(16);
    PROMEKI_TEST(desc.lineAlign() == 16);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(ImageDesc_SetInterlaced)
    ImageDesc desc;
    PROMEKI_TEST(desc.interlaced() == false);

    desc.setInterlaced(true);
    PROMEKI_TEST(desc.interlaced() == true);
PROMEKI_TEST_END()

// ============================================================================
// Copy-on-write
// ============================================================================

PROMEKI_TEST_BEGIN(ImageDesc_CopyOnWrite)
    ImageDesc d1(1920, 1080, PixelFormat::RGBA8);
    ImageDesc d2 = d1;
    PROMEKI_TEST(d1.referenceCount() == 2);
    PROMEKI_TEST(d2.referenceCount() == 2);

    d2.setSize(3840, 2160);
    PROMEKI_TEST(d1.referenceCount() == 1);
    PROMEKI_TEST(d2.referenceCount() == 1);
    PROMEKI_TEST(d1.width() == 1920);
    PROMEKI_TEST(d2.width() == 3840);
PROMEKI_TEST_END()

PROMEKI_TEST_BEGIN(ImageDesc_CopyOnWritePixelFormat)
    ImageDesc d1(1920, 1080, PixelFormat::RGBA8);
    ImageDesc d2 = d1;
    PROMEKI_TEST(d1.referenceCount() == 2);

    d2.setPixelFormat(PixelFormat::RGB8);
    PROMEKI_TEST(d1.referenceCount() == 1);
    PROMEKI_TEST(d1.pixelFormatID() == PixelFormat::RGBA8);
    PROMEKI_TEST(d2.pixelFormatID() == PixelFormat::RGB8);
PROMEKI_TEST_END()

// ============================================================================
// Metadata
// ============================================================================

PROMEKI_TEST_BEGIN(ImageDesc_Metadata)
    ImageDesc desc(1920, 1080, PixelFormat::RGBA8);
    const Metadata &cm = desc.metadata();
    PROMEKI_TEST(cm.isEmpty());

    desc.metadata().set(Metadata::Title, String("Test Image"));
    PROMEKI_TEST(!desc.metadata().isEmpty());
    PROMEKI_TEST(desc.metadata().get(Metadata::Title).get<String>() == "Test Image");
PROMEKI_TEST_END()

// ============================================================================
// toString
// ============================================================================

PROMEKI_TEST_BEGIN(ImageDesc_ToString)
    ImageDesc desc(1920, 1080, PixelFormat::RGBA8);
    String s = desc.toString();
    PROMEKI_TEST(s.size() > 0);
PROMEKI_TEST_END()

// ============================================================================
// PlaneCount
// ============================================================================

PROMEKI_TEST_BEGIN(ImageDesc_PlaneCount)
    ImageDesc desc(1920, 1080, PixelFormat::RGBA8);
    PROMEKI_TEST(desc.planeCount() > 0);
PROMEKI_TEST_END()
