/**
 * @file      imagefileio_png.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/metadata.h>

using namespace promeki;

// ============================================================================
// Handler registration
// ============================================================================

TEST_CASE("ImageFileIO PNG: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::PNG);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "PNG");
}

// ============================================================================
// Round-trip helpers
// ============================================================================

// Fill an image plane with a deterministic, non-constant byte pattern so
// the encoder/decoder can't silently produce zeros and still pass.
static void fillPattern(Image &image) {
        uint8_t *data = static_cast<uint8_t *>(image.data(0));
        size_t bytes = image.lineStride(0) * image.height();
        for(size_t i = 0; i < bytes; ++i) {
                data[i] = static_cast<uint8_t>((i * 2654435761u) >> 24);
        }
}

static void pngRoundTrip(const char *fn, size_t w, size_t h, PixelDesc::ID pdId) {
        Image src(w, h, PixelDesc(pdId));
        REQUIRE(src.isValid());
        fillPattern(src);

        ImageFile sf(ImageFile::PNG);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNG);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        Image dst = lf.image();
        REQUIRE(dst.isValid());
        CHECK(dst.width() == w);
        CHECK(dst.height() == h);
        CHECK(dst.pixelDesc().id() == pdId);
        CHECK(std::memcmp(src.data(0), dst.data(0),
                          src.lineStride(0) * h) == 0);

        std::remove(fn);
}

// LE-variant round-trip: saving an LE 16-bit image triggers a byte-swap
// to big-endian inside the encoder. The loader always returns the BE
// variant, so we verify that the round-trip lands in the BE form and
// that byte-swapping it matches the original LE bytes.
static void pngLeRoundTrip(const char *fn, size_t w, size_t h,
                           PixelDesc::ID leId, PixelDesc::ID beId) {
        Image src(w, h, PixelDesc(leId));
        REQUIRE(src.isValid());
        fillPattern(src);

        ImageFile sf(ImageFile::PNG);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNG);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        Image dst = lf.image();
        REQUIRE(dst.isValid());
        CHECK(dst.pixelDesc().id() == beId);

        // Compare byte-swapped BE output to the original LE bytes.
        const uint16_t *srcPixels = static_cast<const uint16_t *>(src.data(0));
        const uint16_t *dstPixels = static_cast<const uint16_t *>(dst.data(0));
        const size_t pixelCount = (src.lineStride(0) * h) / 2;
        for(size_t i = 0; i < pixelCount; ++i) {
                const uint16_t swapped = static_cast<uint16_t>((dstPixels[i] >> 8) | (dstPixels[i] << 8));
                if(srcPixels[i] != swapped) {
                        FAIL("Mismatch at pixel " << i
                             << ": src=" << srcPixels[i]
                             << " swapped(dst)=" << swapped);
                        break;
                }
        }

        std::remove(fn);
}

// ============================================================================
// Round-trip: native PNG formats
// ============================================================================

TEST_CASE("ImageFileIO PNG: Mono8 round-trip") {
        pngRoundTrip("/tmp/promeki_png_mono8.png", 64, 48, PixelDesc::Mono8_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGB8 round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgb8.png", 64, 48, PixelDesc::RGB8_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGBA8 round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgba8.png", 64, 48, PixelDesc::RGBA8_sRGB);
}

TEST_CASE("ImageFileIO PNG: Mono16 BE round-trip") {
        pngRoundTrip("/tmp/promeki_png_mono16.png", 32, 24, PixelDesc::Mono16_BE_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGB16 BE round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgb16.png", 32, 24, PixelDesc::RGB16_BE_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGBA16 BE round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgba16.png", 32, 24, PixelDesc::RGBA16_BE_sRGB);
}

// ============================================================================
// Round-trip: LE variants (byte-swap on save, loader returns BE)
// ============================================================================

TEST_CASE("ImageFileIO PNG: Mono16 LE round-trip (byte-swapped)") {
        pngLeRoundTrip("/tmp/promeki_png_mono16le.png", 32, 24,
                       PixelDesc::Mono16_LE_sRGB, PixelDesc::Mono16_BE_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGB16 LE round-trip (byte-swapped)") {
        pngLeRoundTrip("/tmp/promeki_png_rgb16le.png", 32, 24,
                       PixelDesc::RGB16_LE_sRGB, PixelDesc::RGB16_BE_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGBA16 LE round-trip (byte-swapped)") {
        pngLeRoundTrip("/tmp/promeki_png_rgba16le.png", 32, 24,
                       PixelDesc::RGBA16_LE_sRGB, PixelDesc::RGBA16_BE_sRGB);
}

// ============================================================================
// Larger size — stresses stride / packed-buffer path
// ============================================================================

TEST_CASE("ImageFileIO PNG: RGBA8 1920x1080 round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgba8_hd.png", 1920, 1080, PixelDesc::RGBA8_sRGB);
}

// ============================================================================
// Unsupported pixel formats
// ============================================================================

TEST_CASE("ImageFileIO PNG: save BGR8 returns PixelFormatNotSupported") {
        Image src(16, 16, PixelDesc::BGR8_sRGB);
        REQUIRE(src.isValid());
        src.fill(0x42);

        ImageFile sf(ImageFile::PNG);
        sf.setFilename("/tmp/promeki_png_bgr8.png");
        sf.setImage(src);
        CHECK(sf.save() == Error::PixelFormatNotSupported);
        std::remove("/tmp/promeki_png_bgr8.png");
}

TEST_CASE("ImageFileIO PNG: save YUV returns PixelFormatNotSupported") {
        Image src(16, 16, PixelDesc::YUV8_422_UYVY_Rec709);
        REQUIRE(src.isValid());
        src.fill(0x80);

        ImageFile sf(ImageFile::PNG);
        sf.setFilename("/tmp/promeki_png_yuv.png");
        sf.setImage(src);
        CHECK(sf.save() == Error::PixelFormatNotSupported);
        std::remove("/tmp/promeki_png_yuv.png");
}

// ============================================================================
// Error paths
// ============================================================================

TEST_CASE("ImageFileIO PNG: load nonexistent file returns error") {
        ImageFile lf(ImageFile::PNG);
        lf.setFilename("/tmp/promeki_png_nonexist.png");
        CHECK(lf.load() != Error::Ok);
}

TEST_CASE("ImageFileIO PNG: load garbage file returns error") {
        const char *fn = "/tmp/promeki_png_bad.png";
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        const char garbage[] = "Definitely not a PNG file, just some text.";
        std::fwrite(garbage, 1, sizeof(garbage), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::PNG);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}

TEST_CASE("ImageFileIO PNG: load truncated file returns error") {
        // Build a real PNG first, then truncate it to something past the
        // signature but well short of the data.
        Image src(64, 64, PixelDesc::RGBA8_sRGB);
        REQUIRE(src.isValid());
        fillPattern(src);

        const char *fn = "/tmp/promeki_png_truncated.png";
        ImageFile sf(ImageFile::PNG);
        sf.setFilename(fn);
        sf.setImage(src);
        REQUIRE(sf.save() == Error::Ok);

        // Truncate to 32 bytes (signature + partial IHDR, no IDAT).
        FILE *fp = std::fopen(fn, "r+b");
        REQUIRE(fp);
        std::fseek(fp, 0, SEEK_END);
        long origLen = std::ftell(fp);
        REQUIRE(origLen > 32);
        std::fclose(fp);
        REQUIRE(truncate(fn, 32) == 0);

        ImageFile lf(ImageFile::PNG);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}

// ============================================================================
// Metadata — gAMA round-trip
// ============================================================================

TEST_CASE("ImageFileIO PNG: gAMA metadata round-trip") {
        const char *fn = "/tmp/promeki_png_gamma.png";
        Image src(32, 32, PixelDesc::RGB8_sRGB);
        REQUIRE(src.isValid());
        src.fill(0x7F);
        src.metadata().set(Metadata::Gamma, 2.2);

        ImageFile sf(ImageFile::PNG);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNG);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        Image dst = lf.image();
        REQUIRE(dst.isValid());
        REQUIRE(dst.metadata().contains(Metadata::Gamma));
        const double g = dst.metadata().get(Metadata::Gamma).get<double>();
        CHECK(g > 2.15);
        CHECK(g < 2.25);

        std::remove(fn);
}
