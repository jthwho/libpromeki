/**
 * @file      tests/jpegimagecodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/jpegimagecodec.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>

using namespace promeki;

static Image createTestImage(int width, int height, PixelDesc::ID pixfmt = PixelDesc::RGB8_sRGB_Full) {
        ImageDesc idesc(width, height, pixfmt);
        Image img(idesc);
        int comps = (pixfmt == PixelDesc::RGBA8_sRGB_Full) ? 4 : 3;
        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        for(int y = 0; y < height; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < width; x++) {
                        row[x * comps + 0] = (uint8_t)(x * 255 / width);
                        row[x * comps + 1] = (uint8_t)(y * 255 / height);
                        row[x * comps + 2] = 128;
                        if(comps == 4) row[x * comps + 3] = 255;
                }
        }
        return img;
}

// ============================================================================
// Construction and properties
// ============================================================================

TEST_CASE("JpegImageCodec_Defaults") {
        JpegImageCodec codec;
        CHECK(codec.name() == "jpeg");
        CHECK(codec.description().size() > 0);
        CHECK(codec.canEncode());
        CHECK_FALSE(codec.canDecode());
        CHECK(codec.quality() == 85);
        CHECK(codec.subsampling() == JpegImageCodec::Subsampling422);
}

// ============================================================================
// Quality clamping
// ============================================================================

TEST_CASE("JpegImageCodec_QualityClamping") {
        JpegImageCodec codec;
        codec.setQuality(0);
        CHECK(codec.quality() == 1);
        codec.setQuality(200);
        CHECK(codec.quality() == 100);
        codec.setQuality(50);
        CHECK(codec.quality() == 50);
}

// ============================================================================
// Encode RGB8
// ============================================================================

TEST_CASE("JpegImageCodec_EncodeRGB8") {
        JpegImageCodec codec;
        Image img = createTestImage(320, 240);

        Image encoded = codec.encode(img);
        CHECK(encoded.isValid());
        CHECK(encoded.isCompressed());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_RGB8_sRGB_Full);
        CHECK(encoded.width() == 320);
        CHECK(encoded.height() == 240);

        // Verify JPEG magic bytes
        size_t compSize = encoded.compressedSize();
        CHECK(compSize > 0);
        const uint8_t *data = static_cast<const uint8_t *>(encoded.data());
        CHECK(data[0] == 0xFF);
        CHECK(data[1] == 0xD8);
        CHECK(data[compSize - 2] == 0xFF);
        CHECK(data[compSize - 1] == 0xD9);
}

// ============================================================================
// Encode RGBA8
// ============================================================================

TEST_CASE("JpegImageCodec_EncodeRGBA8") {
        JpegImageCodec codec;
        Image img = createTestImage(160, 120, PixelDesc::RGBA8_sRGB_Full);

        Image encoded = codec.encode(img);
        CHECK(encoded.isValid());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_RGBA8_sRGB_Full);
}

// ============================================================================
// Metadata preservation
// ============================================================================

TEST_CASE("JpegImageCodec_MetadataPreserved") {
        JpegImageCodec codec;
        Image img = createTestImage(64, 64);
        Timecode tc(Timecode::NDF24, 1, 30, 15, 10);
        img.metadata().set(Metadata::Timecode, tc);

        Image encoded = codec.encode(img);
        REQUIRE(encoded.isValid());
        REQUIRE(encoded.metadata().contains(Metadata::Timecode));
        Timecode outTc = encoded.metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(outTc.hour() == 1);
        CHECK(outTc.min() == 30);
        CHECK(outTc.sec() == 15);
        CHECK(outTc.frame() == 10);
}

// ============================================================================
// Quality affects output size
// ============================================================================

TEST_CASE("JpegImageCodec_QualityAffectsSize") {
        Image img = createTestImage(320, 240);

        JpegImageCodec lowCodec;
        lowCodec.setQuality(10);
        Image lowQ = lowCodec.encode(img);

        JpegImageCodec highCodec;
        highCodec.setQuality(95);
        Image highQ = highCodec.encode(img);

        CHECK(highQ.compressedSize() > lowQ.compressedSize());
}

// ============================================================================
// Subsampling modes
// ============================================================================

TEST_CASE("JpegImageCodec_SubsamplingModes") {
        Image img = createTestImage(320, 240);

        JpegImageCodec codec444;
        codec444.setSubsampling(JpegImageCodec::Subsampling444);
        Image enc444 = codec444.encode(img);
        CHECK(enc444.isValid());

        JpegImageCodec codec422;
        codec422.setSubsampling(JpegImageCodec::Subsampling422);
        Image enc422 = codec422.encode(img);
        CHECK(enc422.isValid());

        JpegImageCodec codec420;
        codec420.setSubsampling(JpegImageCodec::Subsampling420);
        Image enc420 = codec420.encode(img);
        CHECK(enc420.isValid());

        // 4:4:4 should produce larger output than 4:2:0
        CHECK(enc444.compressedSize() > enc420.compressedSize());
}

// ============================================================================
// Invalid input
// ============================================================================

TEST_CASE("JpegImageCodec_InvalidInput") {
        JpegImageCodec codec;
        Image empty;
        Image result = codec.encode(empty);
        CHECK_FALSE(result.isValid());
        CHECK(codec.lastError().isError());
}

// ============================================================================
// Decode not implemented
// ============================================================================

TEST_CASE("JpegImageCodec_DecodeNotImplemented") {
        JpegImageCodec codec;
        Image dummy;
        Image result = codec.decode(dummy);
        CHECK_FALSE(result.isValid());
        CHECK(codec.lastError() == Error::NotImplemented);
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("JpegImageCodec_Registry") {
        auto codecs = ImageCodec::registeredCodecs();
        CHECK(codecs.contains("jpeg"));

        ImageCodec *codec = ImageCodec::createCodec("jpeg");
        REQUIRE(codec != nullptr);
        CHECK(codec->name() == "jpeg");
        CHECK(codec->canEncode());
        delete codec;
}

// ============================================================================
// Verify 4:2:2 subsampling in JPEG structure (RFC 2435)
// ============================================================================

static size_t scanMarker(const uint8_t *data, size_t size, uint8_t marker, size_t startPos = 0) {
        for(size_t i = startPos; i + 1 < size; i++) {
                if(data[i] == 0xFF && data[i + 1] == marker) return i;
        }
        return size;
}

TEST_CASE("JpegImageCodec_422Structure") {
        JpegImageCodec codec;
        codec.setSubsampling(JpegImageCodec::Subsampling422);

        Image img = createTestImage(640, 480);
        Image encoded = codec.encode(img);
        REQUIRE(encoded.isValid());

        const uint8_t *data = static_cast<const uint8_t *>(encoded.data());
        size_t size = encoded.compressedSize();

        // Find SOF0 marker
        size_t sof0 = scanMarker(data, size, 0xC0, 2);
        REQUIRE(sof0 < size);

        // Component 0 (Y): sampling should be H=2 V=1 -> 0x21
        uint8_t y_samp = data[sof0 + 11];
        CHECK(y_samp == 0x21);

        // Component 1 (Cb): H=1 V=1 -> 0x11
        uint8_t cb_samp = data[sof0 + 14];
        CHECK(cb_samp == 0x11);

        // Component 2 (Cr): H=1 V=1 -> 0x11
        uint8_t cr_samp = data[sof0 + 17];
        CHECK(cr_samp == 0x11);
}
