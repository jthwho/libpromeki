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
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>

using namespace promeki;

namespace {

// Encode a single payload into a fresh black image and return it.
Image encodeOne(uint32_t width, uint32_t height, PixelFormat::ID id,
                uint64_t payload, uint32_t firstLine = 0, uint32_t lineCount = 16) {
        ImageDesc desc(width, height, id);
        ImageDataEncoder enc(desc);
        REQUIRE(enc.isValid());
        Image img(width, height, PixelFormat(id));
        img.fill(0);
        REQUIRE(enc.encode(img, ImageDataEncoder::Item{firstLine, lineCount, payload}).isOk());
        return img;
}

}  // namespace

// ============================================================================
// Round-trip across the formats the encoder explicitly tests
// ============================================================================

TEST_CASE("ImageDataDecoder RGBA8 round-trip") {
        const uint64_t payload = 0x0123456789ABCDEFull;
        Image img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
        CHECK(item.decodedSync == ImageDataDecoder::SyncNibble);
}

TEST_CASE("ImageDataDecoder YUV8_422 round-trip") {
        const uint64_t payload = 0xDEADBEEFCAFEBABEull;
        Image img = encodeOne(1920, 64, PixelFormat::YUV8_422_Rec709, payload);

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

TEST_CASE("ImageDataDecoder YUV8_422 planar round-trip") {
        const uint64_t payload = 0x1122334455667788ull;
        Image img = encodeOne(1920, 64, PixelFormat::YUV8_422_Planar_Rec709, payload);

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

TEST_CASE("ImageDataDecoder v210 round-trip") {
        const uint64_t payload = 0xF00DBABECAFEBEEFull;
        Image img = encodeOne(1920, 64, PixelFormat::YUV10_422_v210_Rec709, payload);

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

TEST_CASE("ImageDataDecoder NV12 (4:2:0 semi-planar) round-trip") {
        const uint64_t payload = 0x0011223344556677ull;
        // NV12 needs at least vSub*2 lines per band; use 16 (the default).
        Image img = encodeOne(1920, 64, PixelFormat::YUV8_420_SemiPlanar_Rec709, payload);

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(img, ImageDataDecoder::Band{0, 16});
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
        Image img(1920, 64, PixelFormat(PixelFormat::RGBA8_sRGB));
        img.fill(0);
        List<ImageDataEncoder::Item> encItems;
        encItems.pushToBack({  0, 16, pa });
        encItems.pushToBack({ 16, 16, pb });
        REQUIRE(enc.encode(img, encItems).isOk());

        ImageDataDecoder dec(desc);
        REQUIRE(dec.isValid());
        List<ImageDataDecoder::Band> bands;
        bands.pushToBack({  0, 16 });
        bands.pushToBack({ 16, 16 });
        ImageDataDecoder::DecodedList out;
        REQUIRE(dec.decode(img, bands, out).isOk());
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
        Image img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());
        dec.setSampleMode(ImageDataDecoder::SampleMode::MiddleLine);
        auto item = dec.decode(img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        CHECK(item.payload == payload);
}

// ============================================================================
// Robustness: averaging defeats single-row corruption
// ============================================================================

TEST_CASE("ImageDataDecoder averaging recovers from a single corrupted row") {
        const uint64_t payload = 0x0123456789ABCDEFull;
        Image img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        // Splatter row 8 of the band with random-ish noise so a single
        // scan line would no longer decode correctly.  (Use a fixed
        // pattern so the test is deterministic.)
        uint8_t *line = static_cast<uint8_t *>(img.data(0)) + 8 * img.lineStride(0);
        for(size_t x = 0; x < img.width() * 4; x++) {
                line[x] = static_cast<uint8_t>(x ^ 0x5a);
        }

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());

        // AverageBand mode should still recover the payload since the
        // corrupted row is averaged with 15 clean ones.
        dec.setSampleMode(ImageDataDecoder::SampleMode::AverageBand);
        auto avg = dec.decode(img, ImageDataDecoder::Band{0, 16});
        REQUIRE(avg.error.isOk());
        CHECK(avg.payload == payload);
}

// ============================================================================
// CRC failure: bit-flipping inside the band must surface as CorruptData
// ============================================================================

TEST_CASE("ImageDataDecoder reports CorruptData when CRC fails") {
        // Use an all-1s payload so every payload cell is white in the
        // encoded image — that way our "stomp to black" surely flips
        // a bit instead of landing on a column that was already black.
        const uint64_t payload = 0xFFFFFFFFFFFFFFFFull;
        Image img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        // Stomp every row of the band over a 50-pixel-wide swath
        // (two full bit cells worth at the encoder's bit width of
        // 25 px) inside the payload region.  A swath this wide is
        // guaranteed to land on at least one cell centre and flip
        // its decoded bit, regardless of the exact pitch the
        // decoder discovers from the sync nibble.
        for(uint32_t row = 0; row < 16; row++) {
                uint8_t *line = static_cast<uint8_t *>(img.data(0)) + row * img.lineStride(0);
                for(int x = 200; x < 250; x++) {
                        line[x * 4 + 0] = 0;  // R
                        line[x * 4 + 1] = 0;  // G
                        line[x * 4 + 2] = 0;  // B
                }
        }

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(img, ImageDataDecoder::Band{0, 16});
        // The corrupted bit should now flip, causing a CRC mismatch.
        // We don't strictly require the CRC path to fire — the row
        // could also be rejected at sync detection if the corruption
        // landed in the sync nibble — but it must NOT silently return
        // the original payload.
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

        Image other(1280, 32, PixelFormat(PixelFormat::RGBA8_sRGB));
        other.fill(0);
        auto item = dec.decode(other, ImageDataDecoder::Band{0, 16});
        CHECK(item.error.isError());
        CHECK(item.error.code() == Error::InvalidArgument);
}

TEST_CASE("ImageDataDecoder rejects band that runs past the bottom") {
        ImageDesc desc(1920, 32, PixelFormat::RGBA8_sRGB);
        ImageDataDecoder dec(desc);
        REQUIRE(dec.isValid());

        Image img(1920, 32, PixelFormat(PixelFormat::RGBA8_sRGB));
        img.fill(0);
        auto item = dec.decode(img, ImageDataDecoder::Band{24, 16});
        CHECK(item.error.isError());
}

// ============================================================================
// Bit-width discovery and reporting
// ============================================================================

TEST_CASE("ImageDataDecoder reports the discovered bit width") {
        const uint64_t payload = 0x0123456789ABCDEFull;
        Image img = encodeOne(1920, 64, PixelFormat::RGBA8_sRGB, payload);

        ImageDataDecoder dec(img.desc());
        REQUIRE(dec.isValid());
        auto item = dec.decode(img, ImageDataDecoder::Band{0, 16});
        REQUIRE(item.error.isOk());
        // Encoder picks bitWidth = 1920 / 76 = 25 for RGBA8.
        CHECK(item.bitWidth >= 24.5);
        CHECK(item.bitWidth <= 25.5);
        CHECK(dec.expectedBitWidth() == 25);
}
