/**
 * @file      image.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/image.h>
#include <promeki/pixeldesc.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("Image_Default") {
    Image img;
    CHECK(!img.isValid());
}

// ============================================================================
// Construction with desc
// ============================================================================

TEST_CASE("Image_Construct") {
    Image img(1920, 1080, PixelDesc::RGBA8_sRGB);
    CHECK(img.isValid());
    CHECK(img.width() == 1920);
    CHECK(img.height() == 1080);
    CHECK(img.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
}

TEST_CASE("Image_ConstructFromDesc") {
    ImageDesc desc(1920, 1080, PixelDesc::RGB8_sRGB);
    Image img(desc);
    CHECK(img.isValid());
    CHECK(img.width() == 1920);
    CHECK(img.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

// ============================================================================
// Fill
// ============================================================================

TEST_CASE("Image_Fill") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    CHECK(img.fill(0xAB).isOk());

    const uint8_t *ptr = static_cast<const uint8_t *>(img.data());
    CHECK(ptr[0] == 0xAB);
    CHECK(ptr[1] == 0xAB);
}

// ============================================================================
// Plane access
// ============================================================================

TEST_CASE("Image_Plane") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    const Buffer::Ptr &p = img.plane(0);
    CHECK(p->isValid());
    CHECK(p->data() != nullptr);
}

// ============================================================================
// Shared copy (COW)
// ============================================================================

TEST_CASE("Image_Copy") {
    Image img1(64, 64, PixelDesc::RGBA8_sRGB);
    Image img2 = img1;
    CHECK(img1.width() == img2.width());
    CHECK(img1.height() == img2.height());
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("Image_Metadata") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    CHECK(img.metadata().isEmpty());

    img.metadata().set(Metadata::Title, String("Test Image"));
    CHECK(!img.metadata().isEmpty());
    CHECK(img.metadata().get(Metadata::Title).get<String>() == "Test Image");
}

// ============================================================================
// Line stride
// ============================================================================

TEST_CASE("Image_LineStride") {
    Image img(1920, 1080, PixelDesc::RGBA8_sRGB);
    CHECK(img.lineStride() == 1920 * 4);
}

// ============================================================================
// Construction from Size2Du32
// ============================================================================

TEST_CASE("Image_ConstructFromSize2D") {
    Size2Du32 sz(640, 480);
    Image img(sz, PixelDesc::RGBA8_sRGB);
    CHECK(img.isValid());
    CHECK(img.width() == 640);
    CHECK(img.height() == 480);
    CHECK(img.size().width() == sz.width());
    CHECK(img.size().height() == sz.height());
}

// ============================================================================
// Desc accessor
// ============================================================================

TEST_CASE("Image_DescAccessor") {
    Image img(1280, 720, PixelDesc::RGB8_sRGB);
    REQUIRE(img.isValid());
    const ImageDesc &desc = img.desc();
    CHECK(desc.isValid());
    CHECK(desc.width() == 1280);
    CHECK(desc.height() == 720);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

// ============================================================================
// PixelFormat accessor
// ============================================================================

TEST_CASE("Image_PixelDesc") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    const PixelDesc &pd = img.pixelDesc();
    CHECK(pd.id() == PixelDesc::RGBA8_sRGB);
    CHECK(pd.isValid());
}

// ============================================================================
// Planes list
// ============================================================================

TEST_CASE("Image_Planes") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    const Buffer::PtrList &pl = img.planes();
    CHECK(pl.size() >= 1);
    CHECK(pl[0]->isValid());
}

// ============================================================================
// Data pointer
// ============================================================================

TEST_CASE("Image_DataPointer") {
    Image img(32, 32, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    void *ptr = img.data();
    CHECK(ptr != nullptr);
    CHECK(ptr == img.plane(0)->data());
}

// ============================================================================
// Default image is not valid, fill returns false
// ============================================================================

TEST_CASE("Image_FillInvalid") {
    Image img;
    CHECK(!img.isValid());
    CHECK(img.fill(0).isError());
}

// ============================================================================
// RGB8 pixel format and line stride
// ============================================================================

TEST_CASE("Image_RGB8_LineStride") {
    Image img(100, 100, PixelDesc::RGB8_sRGB);
    REQUIRE(img.isValid());
    CHECK(img.lineStride() == 100 * 3);
    CHECK(img.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

// ============================================================================
// PaintEngine creation
// ============================================================================

TEST_CASE("Image_CreatePaintEngine") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    PaintEngine pe = img.createPaintEngine();
    CHECK(pe.pixelDesc().isValid());
}

// ============================================================================
// isExclusive
// ============================================================================

TEST_CASE("Image_IsExclusive_Fresh") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    CHECK(img.isExclusive());
}

TEST_CASE("Image_IsExclusive_SharedPlane") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());

    // Copy the plane pointer to create a second reference
    Buffer::Ptr shared = img.plane(0);
    CHECK(shared.referenceCount() > 1);
    CHECK_FALSE(img.isExclusive());
}

TEST_CASE("Image_IsExclusive_DefaultImage") {
    Image img;
    // No planes at all — trivially exclusive
    CHECK(img.isExclusive());
}

// ============================================================================
// ensureExclusive
// ============================================================================

TEST_CASE("Image_EnsureExclusive_NoOp") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    CHECK(img.isExclusive());

    // Should be a no-op since we already own the planes exclusively
    void *dataBefore = img.data();
    img.ensureExclusive();
    CHECK(img.isExclusive());
    CHECK(img.data() == dataBefore);
}

TEST_CASE("Image_EnsureExclusive_DetachesShared") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    img.fill(0x42);

    // Create a shared reference to the plane buffer
    Buffer::Ptr shared = img.plane(0);
    REQUIRE(shared.referenceCount() > 1);
    REQUIRE_FALSE(img.isExclusive());

    // ensureExclusive should COW-detach, making img exclusive again
    img.ensureExclusive();
    CHECK(img.isExclusive());

    // The shared copy should still be valid with original data
    CHECK(shared->isValid());
    const uint8_t *sharedData = static_cast<const uint8_t *>(shared->data());
    CHECK(sharedData[0] == 0x42);
}

TEST_CASE("Image_EnsureExclusive_PreservesData") {
    Image img(32, 32, PixelDesc::RGB8_sRGB);
    REQUIRE(img.isValid());
    img.fill(0xAA);

    // Force sharing
    Buffer::Ptr shared = img.plane(0);
    REQUIRE_FALSE(img.isExclusive());

    img.ensureExclusive();
    CHECK(img.isExclusive());

    // Data should be preserved after detach
    const uint8_t *data = static_cast<const uint8_t *>(img.data());
    CHECK(data[0] == 0xAA);
    CHECK(data[1] == 0xAA);
    CHECK(data[2] == 0xAA);
}

// ============================================================================
// isExclusive / ensureExclusive with Image::Ptr
// ============================================================================

TEST_CASE("Image_IsExclusive_ViaPtr") {
    Image::Ptr imgPtr = Image::Ptr::create(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(imgPtr->isValid());
    CHECK(imgPtr->isExclusive());

    // Sharing the Image::Ptr itself doesn't affect plane exclusivity
    Image::Ptr copy = imgPtr;
    CHECK(copy->isExclusive());
    // But the Image refcount is now 2
    CHECK(imgPtr.referenceCount() == 2);
}

// ============================================================================
// isCompressed
// ============================================================================

TEST_CASE("Image_IsCompressed_Uncompressed") {
    Image img(64, 64, PixelDesc::RGBA8_sRGB);
    REQUIRE(img.isValid());
    CHECK_FALSE(img.isCompressed());
}

TEST_CASE("Image_IsCompressed_Default") {
    Image img;
    CHECK_FALSE(img.isCompressed());
}

// ============================================================================
// compressedSize
// ============================================================================

TEST_CASE("Image_CompressedSize_Uncompressed") {
    Image img(64, 64, PixelDesc::RGB8_sRGB);
    REQUIRE(img.isValid());
    CHECK(img.compressedSize() == 0);
}

TEST_CASE("Image_CompressedSize_Default") {
    Image img;
    CHECK(img.compressedSize() == 0);
}

// ============================================================================
// fromCompressedData
// ============================================================================

TEST_CASE("Image_FromCompressedData_Valid") {
    // Fake compressed payload
    const uint8_t fakeData[] = { 0xFF, 0xD8, 0x00, 0x01, 0x02, 0xFF, 0xD9 };
    size_t fakeSize = sizeof(fakeData);

    Image img = Image::fromCompressedData(fakeData, fakeSize,
                                          1920, 1080,
                                          PixelDesc::JPEG_RGB8_sRGB);
    REQUIRE(img.isValid());
    CHECK(img.width() == 1920);
    CHECK(img.height() == 1080);
    CHECK(img.pixelDesc().id() == PixelDesc::JPEG_RGB8_sRGB);
    CHECK(img.isCompressed());
    CHECK(img.compressedSize() == fakeSize);

    // Verify the data was copied correctly
    const uint8_t *out = static_cast<const uint8_t *>(img.data());
    for(size_t i = 0; i < fakeSize; i++) {
        CHECK(out[i] == fakeData[i]);
    }
}

TEST_CASE("Image_FromCompressedData_MetadataPreserved") {
    const uint8_t fakeData[] = { 0xAA, 0xBB };
    Metadata md;
    md.set(Metadata::Title, String("test jpeg"));

    Image img = Image::fromCompressedData(fakeData, sizeof(fakeData),
                                          640, 480,
                                          PixelDesc::JPEG_RGBA8_sRGB,
                                          md);
    REQUIRE(img.isValid());
    CHECK(img.metadata().contains(Metadata::Title));
    CHECK(img.metadata().get(Metadata::Title).get<String>() == "test jpeg");
    // CompressedSize metadata should also be set internally
    CHECK(img.metadata().contains(Metadata::CompressedSize));
}

TEST_CASE("Image_FromCompressedData_RGBA8") {
    const uint8_t fakeData[] = { 0x01, 0x02, 0x03 };
    Image img = Image::fromCompressedData(fakeData, sizeof(fakeData),
                                          320, 240,
                                          PixelDesc::JPEG_RGBA8_sRGB);
    REQUIRE(img.isValid());
    CHECK(img.pixelDesc().id() == PixelDesc::JPEG_RGBA8_sRGB);
    CHECK(img.isCompressed());
    CHECK(img.compressedSize() == sizeof(fakeData));
}
