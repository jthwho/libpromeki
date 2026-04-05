/**
 * @file      encodeddesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/encodeddesc.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("EncodedDesc_Default") {
    EncodedDesc desc;
    CHECK(!desc.isValid());
    CHECK(desc.quality() == -1);
}

// ============================================================================
// Construction with codec
// ============================================================================

TEST_CASE("EncodedDesc_ConstructWithCodec") {
    EncodedDesc desc(FourCC("JPEG"));
    CHECK(desc.isValid());
    CHECK(desc.codec() == FourCC("JPEG"));
    CHECK(desc.quality() == -1);
}

TEST_CASE("EncodedDesc_ConstructWithCodecAndImageDesc") {
    ImageDesc imgDesc(1920, 1080, PixelDesc::RGB8_sRGB);
    EncodedDesc desc(FourCC("JPEG"), imgDesc);
    CHECK(desc.isValid());
    CHECK(desc.codec() == FourCC("JPEG"));
    CHECK(desc.sourceImageDesc().width() == 1920);
    CHECK(desc.sourceImageDesc().height() == 1080);
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("EncodedDesc_SetCodec") {
    EncodedDesc desc;
    desc.setCodec(FourCC("H264"));
    CHECK(desc.isValid());
    CHECK(desc.codec() == FourCC("H264"));
}

TEST_CASE("EncodedDesc_SetQuality") {
    EncodedDesc desc(FourCC("JPEG"));
    desc.setQuality(85);
    CHECK(desc.quality() == 85);
}

TEST_CASE("EncodedDesc_SetSourceImageDesc") {
    EncodedDesc desc(FourCC("JPEG"));
    ImageDesc imgDesc(1280, 720, PixelDesc::RGB8_sRGB);
    desc.setSourceImageDesc(imgDesc);
    CHECK(desc.sourceImageDesc().width() == 1280);
    CHECK(desc.sourceImageDesc().height() == 720);
}

// ============================================================================
// Equality
// ============================================================================

TEST_CASE("EncodedDesc_Equality") {
    EncodedDesc d1(FourCC("JPEG"));
    EncodedDesc d2(FourCC("JPEG"));
    CHECK(d1 == d2);
}

TEST_CASE("EncodedDesc_InequalityCodec") {
    EncodedDesc d1(FourCC("JPEG"));
    EncodedDesc d2(FourCC("H264"));
    CHECK(d1 != d2);
}

TEST_CASE("EncodedDesc_InequalityQuality") {
    EncodedDesc d1(FourCC("JPEG"));
    EncodedDesc d2(FourCC("JPEG"));
    d1.setQuality(85);
    d2.setQuality(50);
    CHECK(d1 != d2);
}

TEST_CASE("EncodedDesc_FormatEqualsIgnoresMetadata") {
    EncodedDesc d1(FourCC("JPEG"));
    EncodedDesc d2(FourCC("JPEG"));
    d1.metadata().set(Metadata::Artist, String("Test"));
    CHECK(d1.formatEquals(d2));
    CHECK_FALSE(d1 == d2);
}

// ============================================================================
// Copy semantics
// ============================================================================

TEST_CASE("EncodedDesc_CopyIsIndependent") {
    EncodedDesc d1(FourCC("JPEG"));
    d1.setQuality(85);
    EncodedDesc d2 = d1;

    d2.setQuality(50);
    CHECK(d1.quality() == 85);
    CHECK(d2.quality() == 50);
}

// ============================================================================
// toString
// ============================================================================

TEST_CASE("EncodedDesc_ToString") {
    EncodedDesc desc(FourCC("JPEG"));
    String s = desc.toString();
    CHECK(s.size() > 0);
}

TEST_CASE("EncodedDesc_ToStringWithQuality") {
    EncodedDesc desc(FourCC("JPEG"));
    desc.setQuality(85);
    String s = desc.toString();
    CHECK(s.size() > 0);
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("EncodedDesc_Metadata") {
    EncodedDesc desc(FourCC("JPEG"));
    CHECK(desc.metadata().isEmpty());

    desc.metadata().set(Metadata::Artist, String("Test Artist"));
    CHECK(!desc.metadata().isEmpty());
    CHECK(desc.metadata().get(Metadata::Artist).get<String>() == "Test Artist");
}

// ============================================================================
// Quality boundary values
// ============================================================================

TEST_CASE("EncodedDesc_QualityBoundaries") {
    EncodedDesc desc(FourCC("JPEG"));
    desc.setQuality(0);
    CHECK(desc.quality() == 0);
    desc.setQuality(1);
    CHECK(desc.quality() == 1);
    desc.setQuality(100);
    CHECK(desc.quality() == 100);
    desc.setQuality(-1);
    CHECK(desc.quality() == -1);
}

// ============================================================================
// Copy with metadata
// ============================================================================

TEST_CASE("EncodedDesc_CopyWithMetadata") {
    EncodedDesc d1(FourCC("JPEG"));
    d1.setQuality(85);
    d1.metadata().set(Metadata::Artist, String("Test"));
    EncodedDesc d2 = d1;

    CHECK(d2.codec() == FourCC("JPEG"));
    CHECK(d2.quality() == 85);
    CHECK(d2.metadata().get(Metadata::Artist).get<String>() == "Test");

    // Verify independence
    d2.metadata().set(Metadata::Artist, String("Other"));
    CHECK(d1.metadata().get(Metadata::Artist).get<String>() == "Test");
    CHECK(d2.metadata().get(Metadata::Artist).get<String>() == "Other");
}

// ============================================================================
// Invalid source image desc
// ============================================================================

TEST_CASE("EncodedDesc_DefaultSourceImageDesc") {
    EncodedDesc desc(FourCC("JPEG"));
    CHECK(!desc.sourceImageDesc().isValid());
}

// ============================================================================
// Equality with matching quality
// ============================================================================

TEST_CASE("EncodedDesc_EqualityWithQuality") {
    EncodedDesc d1(FourCC("JPEG"));
    EncodedDesc d2(FourCC("JPEG"));
    d1.setQuality(85);
    d2.setQuality(85);
    CHECK(d1 == d2);
}

// ============================================================================
// Sequential setter changes
// ============================================================================

TEST_CASE("EncodedDesc_SequentialChanges") {
    EncodedDesc desc(FourCC("JPEG"));
    desc.setQuality(50);
    desc.setCodec(FourCC("H264"));
    desc.setQuality(90);
    CHECK(desc.codec() == FourCC("H264"));
    CHECK(desc.quality() == 90);
}
