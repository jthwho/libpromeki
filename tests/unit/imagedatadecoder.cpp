/**
 * @file      imagedatadecoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <cstring>
#include <promeki/imagedatadecoder.h>
#include <promeki/imagedataencoder.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

namespace {

// Encode a single payload into a fresh black payload and return it.
UncompressedVideoPayload::Ptr encodeOne(uint32_t width, uint32_t height,
                                        PixelFormat::ID id, uint64_t payload,
                                        uint32_t firstLine = 0,
                                        uint32_t lineCount = 16) {
        ImageDesc desc(width, height, id);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        auto img = UncompressedVideoPayload::allocate(desc);
        for(size_t i = 0; i < img->planeCount(); ++i) {
                std::memset(img.modify()->data()[i].data(), 0, img->plane(i).size());
        }
        REQUIRE(enc.encode(*img.modify(),
                           ImageDataEncoder::Item{firstLine, lineCount, payload}).isOk());
        return img;
}

}  // namespace

// ============================================================================
// Round-trip across the formats the encoder explicitly tests
// ============================================================================

TEST_CASE("ImageDataDecoder RGBA8 round-trip") {
        const uint64_t payload = 0x0123456789ABCDEFull;
        auto img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
        CHECK(item.decodedSync == ImageDataDecoder::SyncNibble);
}

TEST_CASE("ImageDataDecoder YUV8_422 round-trip") {
        const uint64_t payload = 0xDEADBEEFCAFEBABEull;
        auto img = encodeOne(1920, 64, PixelFormat::YUV8_422_Rec709, payload);

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

TEST_CASE("ImageDataDecoder YUV8_422 planar round-trip") {
        const uint64_t payload = 0x1122334455667788ull;
        auto img = encodeOne(1920, 64, PixelFormat::YUV8_422_Planar_Rec709, payload);

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

TEST_CASE("ImageDataDecoder v210 round-trip") {
        const uint64_t payload = 0xF00DBABECAFEBEEFull;
        auto img = encodeOne(1920, 64, PixelFormat::YUV10_422_v210_Rec709, payload);

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

TEST_CASE("ImageDataDecoder NV12 (4:2:0 semi-planar) round-trip") {
        const uint64_t payload = 0x0011223344556677ull;
        auto img = encodeOne(1920, 64, PixelFormat::YUV8_420_SemiPlanar_Rec709, payload);

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

// ============================================================================
// Multiple bands per call
// ============================================================================

TEST_CASE("ImageDataDecoder multiple-band decode") {
        const uint64_t pa = 0xAAAAAAAAAAAAAAAAull;
        const uint64_t pb = 0x5555555555555555ull;
        ImageDesc desc(1920, 64, PixelFormat::RGBA8_sRGB);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        auto img = UncompressedVideoPayload::allocate(desc);
        std::memset(img.modify()->data()[0].data(), 0, img->plane(0).size());
        List<ImageDataEncoder::Item> encItems;
        encItems.pushToBack({  0, 16, pa });
        encItems.pushToBack({ 16, 16, pb });
        REQUIRE(enc.encode(*img.modify(), encItems).isOk());

        ImageDataDecoder dec(desc);
        REQUIRE(dec.isValid());
        List<ImageDataDecoder::Band> bands;
        bands.pushToBack({  0, 16 });
        bands.pushToBack({ 16, 16 });
        ImageDataDecoder::DecodedList out;
        REQUIRE(dec.decode(*img, bands, out).isOk());
        REQUIRE(out.size() == 2);
        CHECK(out[0].error.isOk());
        CHECK(out[1].error.isOk());
        CHECK(out[0].payload == pa);
        CHECK(out[1].payload == pb);
}

// ============================================================================
// Sample modes
// ============================================================================

TEST_CASE("ImageDataDecoder MiddleLine sample mode round-trip") {
        const uint64_t payload = 0x0123456789ABCDEFull;
        auto img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());
        dec.setSampleMode(ImageDataDecoder::SampleMode::MiddleLine);
        auto item = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

// ============================================================================
// Robustness: averaging defeats single-row corruption
// ============================================================================

TEST_CASE("ImageDataDecoder averaging recovers from a single corrupted row") {
        const uint64_t payload = 0x0123456789ABCDEFull;
        auto img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        const size_t stride0 = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        uint8_t *line = img.modify()->data()[0].data() + 8 * stride0;
        const size_t w = img->desc().width();
        for(size_t x = 0; x < w * 4; x++) {
                line[x] = static_cast<uint8_t>(x ^ 0x5a);
        }

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());

        dec.setSampleMode(ImageDataDecoder::SampleMode::AverageBand);
        auto avg = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(avg.error.isOk());
        CHECK(avg.payload == payload);
}

// ============================================================================
// CRC failure: bit-flipping inside the band must surface as CorruptData
// ============================================================================

TEST_CASE("ImageDataDecoder reports CorruptData when CRC fails") {
        const uint64_t payload = 0xFFFFFFFFFFFFFFFFull;
        auto img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        const size_t stride0 = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        for(uint32_t row = 0; row < 16; row++) {
                uint8_t *line = img.modify()->data()[0].data() + row * stride0;
                for(int x = 200; x < 250; x++) {
                        line[x * 4 + 0] = 0;
                        line[x * 4 + 1] = 0;
                        line[x * 4 + 2] = 0;
                }
        }

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isError());
        CHECK(item.error.code() == Error::CorruptData);
}

// ============================================================================
// Validation
// ============================================================================

TEST_CASE("ImageDataDecoder rejects mismatched image descriptor") {
        ImageDesc desc(1920, 32, PixelFormat::RGBA8_sRGB);
        ImageDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        auto other = UncompressedVideoPayload::allocate(
                ImageDesc(1280, 32, PixelFormat(PixelFormat::RGBA8_sRGB)));
        std::memset(other.modify()->data()[0].data(), 0, other->plane(0).size());
        auto item = dec.decode(*other, ImageDataDecoder::Band{0, 16});
        CHECK(item.error.isError());
        CHECK(item.error.code() == Error::InvalidArgument);
}

TEST_CASE("ImageDataDecoder rejects band that runs past the bottom") {
        ImageDesc desc(1920, 32, PixelFormat::RGBA8_sRGB);
        ImageDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        auto img = UncompressedVideoPayload::allocate(
                ImageDesc(1920, 32, PixelFormat(PixelFormat::RGBA8_sRGB)));
        std::memset(img.modify()->data()[0].data(), 0, img->plane(0).size());
        auto item = dec.decode(*img, ImageDataDecoder::Band{24, 16});
        CHECK(item.error.isError());
}

// ============================================================================
// Bit-width discovery and reporting
// ============================================================================

TEST_CASE("ImageDataDecoder reports the discovered bit width") {
        const uint64_t payload = 0x0123456789ABCDEFull;
        auto img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        ImageDataDecoder dec(img->desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(*img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.bitWidth >= 24.5);
        CHECK(item.bitWidth <= 25.5);
        CHECK(dec.expectedBitWidth() == 25);
}
