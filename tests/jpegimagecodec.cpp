/**
 * @file      tests/jpegimagecodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/jpegimagecodec.h>
#include <promeki/codec.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>

using namespace promeki;

static Image createTestImage(int width, int height, PixelDesc::ID pixfmt = PixelDesc::RGB8_sRGB) {
        ImageDesc idesc(width, height, pixfmt);
        Image img(idesc);
        int comps = (pixfmt == PixelDesc::RGBA8_sRGB) ? 4 : 3;
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

static Image createTestYCbCrImage(int width, int height, PixelDesc::ID pd) {
        Image img(width, height, pd);
        uint8_t *data = static_cast<uint8_t *>(img.data());
        size_t stride = img.lineStride();
        bool isUYVY = (pd == PixelDesc::YUV8_422_UYVY_Rec709);
        for(int y = 0; y < height; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < width / 2; x++) {
                        uint8_t luma0 = (uint8_t)(16 + (x * 2) * 219 / width);
                        uint8_t luma1 = (uint8_t)(16 + (x * 2 + 1) * 219 / width);
                        uint8_t cb = 128, cr = 128;
                        if(isUYVY) {
                                row[x*4+0]=cb; row[x*4+1]=luma0; row[x*4+2]=cr; row[x*4+3]=luma1;
                        } else {
                                row[x*4+0]=luma0; row[x*4+1]=cb; row[x*4+2]=luma1; row[x*4+3]=cr;
                        }
                }
        }
        return img;
}

static Image createPlanarImage(int width, int height, PixelDesc::ID pd) {
        Image img(width, height, pd);
        if(!img.isValid()) return img;
        uint8_t *y = static_cast<uint8_t *>(img.data(0));
        size_t ySize = img.pixelDesc().pixelFormat().planeSize(0, width, height);
        for(size_t i = 0; i < ySize; i++) y[i] = (uint8_t)(16 + i * 219 / ySize);
        for(size_t p = 1; p < img.pixelDesc().planeCount(); p++) {
                size_t pSize = img.pixelDesc().pixelFormat().planeSize(p, width, height);
                std::memset(img.data(p), 128, pSize);
        }
        return img;
}

// ============================================================================
// Defaults
// ============================================================================

TEST_CASE("JpegImageCodec_Defaults") {
        JpegImageCodec codec;
        CHECK(codec.canEncode());
        CHECK(codec.canDecode());
        CHECK(codec.quality() == 85);
}

// ============================================================================
// Quality clamping
// ============================================================================

TEST_CASE("JpegImageCodec_QualityClamping") {
        JpegImageCodec codec;
        codec.setQuality(0);  CHECK(codec.quality() == 1);
        codec.setQuality(200); CHECK(codec.quality() == 100);
}

// ============================================================================
// Encode RGB8 / RGBA8
// ============================================================================

TEST_CASE("JpegImageCodec_EncodeRGB8") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestImage(320, 240));
        CHECK(encoded.isValid());
        CHECK(encoded.isCompressed());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_RGB8_sRGB);
        const uint8_t *d = static_cast<const uint8_t *>(encoded.data());
        CHECK(d[0] == 0xFF); CHECK(d[1] == 0xD8);
}

TEST_CASE("JpegImageCodec_EncodeRGBA8") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestImage(160, 120, PixelDesc::RGBA8_sRGB));
        CHECK(encoded.isValid());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_RGBA8_sRGB);
}

// ============================================================================
// Metadata preservation
// ============================================================================

TEST_CASE("JpegImageCodec_MetadataPreserved") {
        JpegImageCodec codec;
        Image img = createTestImage(64, 64);
        img.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 30, 15, 10));
        Image encoded = codec.encode(img);
        REQUIRE(encoded.isValid());
        Timecode tc = encoded.metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.hour() == 1); CHECK(tc.frame() == 10);
}

// ============================================================================
// Quality affects size
// ============================================================================

TEST_CASE("JpegImageCodec_QualityAffectsSize") {
        Image img = createTestImage(320, 240);
        JpegImageCodec lo; lo.setQuality(10);
        JpegImageCodec hi; hi.setQuality(95);
        CHECK(hi.encode(img).compressedSize() > lo.encode(img).compressedSize());
}

// ============================================================================
// Subsampling modes
// ============================================================================

TEST_CASE("JpegImageCodec_SubsamplingModes") {
        Image img = createTestImage(320, 240);
        JpegImageCodec c444; c444.setSubsampling(JpegImageCodec::Subsampling444);
        JpegImageCodec c420; c420.setSubsampling(JpegImageCodec::Subsampling420);
        CHECK(c444.encode(img).compressedSize() > c420.encode(img).compressedSize());
}

// ============================================================================
// Invalid input
// ============================================================================

TEST_CASE("JpegImageCodec_InvalidInput") {
        JpegImageCodec codec;
        CHECK_FALSE(codec.encode(Image()).isValid());
        CHECK_FALSE(codec.decode(Image()).isValid());
}

// ============================================================================
// RGB round-trip
// ============================================================================

TEST_CASE("JpegImageCodec_RoundTripRGB8") {
        JpegImageCodec codec; codec.setQuality(100);
        Image encoded = codec.encode(createTestImage(320, 240));
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::RGB8_sRGB);
        CHECK(decoded.isValid());
        CHECK(decoded.width() == 320);
        CHECK(decoded.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

TEST_CASE("JpegImageCodec_DecodeToRGBA8") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestImage(160, 120));
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::RGBA8_sRGB);
        CHECK(decoded.isValid());
        CHECK(decoded.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("JpegImageCodec_Registry") {
        CHECK(ImageCodec::registeredCodecs().contains("jpeg"));
        ImageCodec *codec = ImageCodec::createCodec("jpeg");
        REQUIRE(codec != nullptr);
        CHECK(codec->canEncode()); CHECK(codec->canDecode());
        delete codec;
}

// ============================================================================
// Generic ImageCodec::configure dispatch
// ============================================================================
//
// JpegImageCodec inherits the configure() virtual from ImageCodec and
// pulls JpegQuality / JpegSubsampling out of any MediaConfig handed to
// it.  This locks the dispatch in so future codecs can't accidentally
// regress the contract that Image::convert relies on.

TEST_CASE("JpegImageCodec_ConfigureFromMediaConfig") {
        Image img = createTestImage(320, 240);

        SUBCASE("JpegQuality flows through configure()") {
                // Pull the codec out of the registry exactly the way
                // Image::convert does, hand it a MediaConfig with two
                // different quality values, and verify the encoded
                // output sizes track the setting.
                ImageCodec *lo = ImageCodec::createCodec("jpeg");
                ImageCodec *hi = ImageCodec::createCodec("jpeg");
                REQUIRE(lo != nullptr);
                REQUIRE(hi != nullptr);

                MediaConfig loCfg;
                loCfg.set(MediaConfig::JpegQuality, 10);
                lo->configure(loCfg);

                MediaConfig hiCfg;
                hiCfg.set(MediaConfig::JpegQuality, 95);
                hi->configure(hiCfg);

                Image encLo = lo->encode(img);
                Image encHi = hi->encode(img);
                REQUIRE(encLo.isValid());
                REQUIRE(encHi.isValid());
                CHECK(encHi.compressedSize() > encLo.compressedSize());

                delete lo;
                delete hi;
        }

        SUBCASE("JpegSubsampling Enum flows through configure()") {
                // YUV444 keeps full chroma resolution and produces a
                // larger file than YUV420 for a chroma-rich gradient.
                ImageCodec *c444 = ImageCodec::createCodec("jpeg");
                ImageCodec *c420 = ImageCodec::createCodec("jpeg");
                REQUIRE(c444 != nullptr);
                REQUIRE(c420 != nullptr);

                MediaConfig cfg444;
                cfg444.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV444);
                c444->configure(cfg444);

                MediaConfig cfg420;
                cfg420.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV420);
                c420->configure(cfg420);

                Image enc444 = c444->encode(img);
                Image enc420 = c420->encode(img);
                REQUIRE(enc444.isValid());
                REQUIRE(enc420.isValid());
                CHECK(enc444.compressedSize() > enc420.compressedSize());

                delete c444;
                delete c420;
        }

        SUBCASE("Empty MediaConfig leaves codec defaults intact") {
                // configure() with no recognised keys must not change
                // anything.  We compare against a freshly-constructed
                // codec to make sure the round trip lands at the same
                // 85/4:2:2 baseline.
                JpegImageCodec configured;
                configured.configure(MediaConfig());

                JpegImageCodec baseline;
                CHECK(configured.quality() == baseline.quality());
                CHECK(configured.subsampling() == baseline.subsampling());
        }

        SUBCASE("Unknown subsampling string is ignored") {
                // Variant::asEnum returns hasListedValue()==false for
                // garbage strings; configure() must leave the previous
                // setting alone instead of clobbering it.
                JpegImageCodec codec;
                codec.setSubsampling(JpegImageCodec::Subsampling444);

                MediaConfig cfg;
                cfg.set(MediaConfig::JpegSubsampling, String("YUV777"));
                codec.configure(cfg);

                CHECK(codec.subsampling() == JpegImageCodec::Subsampling444);
        }
}

// ============================================================================
// 4:2:2 structure check
// ============================================================================

static size_t scanMarker(const uint8_t *data, size_t size, uint8_t marker, size_t startPos = 0) {
        for(size_t i = startPos; i + 1 < size; i++)
                if(data[i] == 0xFF && data[i+1] == marker) return i;
        return size;
}

TEST_CASE("JpegImageCodec_422Structure") {
        JpegImageCodec codec; codec.setSubsampling(JpegImageCodec::Subsampling422);
        Image encoded = codec.encode(createTestImage(640, 480));
        REQUIRE(encoded.isValid());
        const uint8_t *d = static_cast<const uint8_t *>(encoded.data());
        size_t sof0 = scanMarker(d, encoded.compressedSize(), 0xC0, 2);
        REQUIRE(sof0 < encoded.compressedSize());
        CHECK(d[sof0+11] == 0x21); // Y: H=2 V=1
        CHECK(d[sof0+14] == 0x11); // Cb
        CHECK(d[sof0+17] == 0x11); // Cr
}

// ============================================================================
// Interleaved UYVY / YUYV encode & round-trip
// ============================================================================

TEST_CASE("JpegImageCodec_EncodeUYVY") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestYCbCrImage(320, 240, PixelDesc::YUV8_422_UYVY_Rec709));
        CHECK(encoded.isValid());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_YUV8_422_Rec709);
}

TEST_CASE("JpegImageCodec_EncodeYUYV") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestYCbCrImage(320, 240, PixelDesc::YUV8_422_Rec709));
        CHECK(encoded.isValid());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_YUV8_422_Rec709);
}

TEST_CASE("JpegImageCodec_RoundTripUYVY") {
        JpegImageCodec codec; codec.setQuality(100);
        Image src = createTestYCbCrImage(320, 240, PixelDesc::YUV8_422_UYVY_Rec709);
        Image encoded = codec.encode(src);
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::YUV8_422_UYVY_Rec709);
        CHECK(decoded.isValid());
        CHECK(decoded.pixelDesc().id() == PixelDesc::YUV8_422_UYVY_Rec709);
        const uint8_t *s = static_cast<const uint8_t *>(src.data());
        const uint8_t *d = static_cast<const uint8_t *>(decoded.data());
        CHECK(std::abs((int)s[1] - (int)d[1]) < 4);
}

TEST_CASE("JpegImageCodec_RoundTripYUYV") {
        JpegImageCodec codec; codec.setQuality(100);
        Image encoded = codec.encode(createTestYCbCrImage(320, 240, PixelDesc::YUV8_422_Rec709));
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::YUV8_422_Rec709);
        CHECK(decoded.isValid());
}

// ============================================================================
// Cross-format interleaved
// ============================================================================

TEST_CASE("JpegImageCodec_EncodeUYVYDecodeRGB") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestYCbCrImage(320, 240, PixelDesc::YUV8_422_UYVY_Rec709));
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::RGB8_sRGB);
        CHECK(decoded.isValid());
        CHECK(decoded.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

TEST_CASE("JpegImageCodec_EncodeRGBDecodeUYVY") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestImage(320, 240));
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::YUV8_422_UYVY_Rec709);
        CHECK(decoded.isValid());
}

// ============================================================================
// Non-8-aligned height
// ============================================================================

TEST_CASE("JpegImageCodec_EncodeUYVYNonAlignedHeight") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestYCbCrImage(320, 244, PixelDesc::YUV8_422_UYVY_Rec709));
        CHECK(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::YUV8_422_UYVY_Rec709);
        CHECK(decoded.isValid());
        CHECK(decoded.height() == 244);
}

// ============================================================================
// Default decode format
// ============================================================================

TEST_CASE("JpegImageCodec_DecodeDefaultFormat") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createTestImage(160, 120));
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded);
        CHECK(decoded.isValid());
}

// ============================================================================
// Planar 4:2:2 encode & round-trip
// ============================================================================

TEST_CASE("JpegImageCodec_EncodePlanar422") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createPlanarImage(320, 240, PixelDesc::YUV8_422_Planar_Rec709));
        CHECK(encoded.isValid());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_YUV8_422_Rec709);
}

TEST_CASE("JpegImageCodec_RoundTripPlanar422") {
        JpegImageCodec codec; codec.setQuality(100);
        Image src = createPlanarImage(320, 240, PixelDesc::YUV8_422_Planar_Rec709);
        Image encoded = codec.encode(src);
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::YUV8_422_Planar_Rec709);
        CHECK(decoded.isValid());
        CHECK(decoded.pixelDesc().id() == PixelDesc::YUV8_422_Planar_Rec709);
        CHECK(decoded.width() == 320);
        CHECK(decoded.height() == 240);
}

// ============================================================================
// Planar 4:2:0 encode & round-trip
// ============================================================================

TEST_CASE("JpegImageCodec_EncodePlanar420") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createPlanarImage(320, 240, PixelDesc::YUV8_420_Planar_Rec709));
        CHECK(encoded.isValid());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_YUV8_420_Rec709);
}

TEST_CASE("JpegImageCodec_RoundTripPlanar420") {
        JpegImageCodec codec; codec.setQuality(100);
        Image src = createPlanarImage(320, 240, PixelDesc::YUV8_420_Planar_Rec709);
        Image encoded = codec.encode(src);
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::YUV8_420_Planar_Rec709);
        CHECK(decoded.isValid());
        CHECK(decoded.pixelDesc().id() == PixelDesc::YUV8_420_Planar_Rec709);
}

// ============================================================================
// NV12 encode & round-trip
// ============================================================================

TEST_CASE("JpegImageCodec_EncodeNV12") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createPlanarImage(320, 240, PixelDesc::YUV8_420_SemiPlanar_Rec709));
        CHECK(encoded.isValid());
        CHECK(encoded.pixelDesc().id() == PixelDesc::JPEG_YUV8_420_Rec709);
}

TEST_CASE("JpegImageCodec_RoundTripNV12") {
        JpegImageCodec codec; codec.setQuality(100);
        Image src = createPlanarImage(320, 240, PixelDesc::YUV8_420_SemiPlanar_Rec709);
        Image encoded = codec.encode(src);
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::YUV8_420_SemiPlanar_Rec709);
        CHECK(decoded.isValid());
        CHECK(decoded.pixelDesc().id() == PixelDesc::YUV8_420_SemiPlanar_Rec709);
}

// ============================================================================
// Cross-format: planar 4:2:0 <-> RGB
// ============================================================================

TEST_CASE("JpegImageCodec_EncodePlanar420DecodeRGB") {
        JpegImageCodec codec;
        Image encoded = codec.encode(createPlanarImage(320, 240, PixelDesc::YUV8_420_Planar_Rec709));
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::RGB8_sRGB);
        CHECK(decoded.isValid());
        CHECK(decoded.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

TEST_CASE("JpegImageCodec_EncodeRGBDecodePlanar420") {
        JpegImageCodec codec;
        codec.setSubsampling(JpegImageCodec::Subsampling420);
        Image encoded = codec.encode(createTestImage(320, 240));
        REQUIRE(encoded.isValid());
        Image decoded = codec.decode(encoded, PixelDesc::YUV8_420_Planar_Rec709);
        CHECK(decoded.isValid());
        CHECK(decoded.pixelDesc().id() == PixelDesc::YUV8_420_Planar_Rec709);
}
