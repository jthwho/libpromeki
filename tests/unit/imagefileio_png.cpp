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
#include <promeki/uncompressedvideopayload.h>
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

static void fillPattern(UncompressedVideoPayload &image) {
        uint8_t *data = image.data()[0].data();
        size_t   bytes = image.plane(0).size();
        for (size_t i = 0; i < bytes; ++i) {
                data[i] = static_cast<uint8_t>((i * 2654435761u) >> 24);
        }
}

static void pngRoundTrip(const char *fn, size_t w, size_t h, PixelFormat::ID pdId) {
        auto src = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat(pdId)));
        REQUIRE(src.isValid());
        fillPattern(*src.modify());

        ImageFile sf(ImageFile::PNG);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNG);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto dst = lf.uncompressedVideoPayload();
        REQUIRE(dst.isValid());
        CHECK(dst->desc().width() == w);
        CHECK(dst->desc().height() == h);
        CHECK(dst->desc().pixelFormat().id() == pdId);
        CHECK(std::memcmp(src->plane(0).data(), dst->plane(0).data(), src->plane(0).size()) == 0);

        std::remove(fn);
}

static void pngLeRoundTrip(const char *fn, size_t w, size_t h, PixelFormat::ID leId, PixelFormat::ID beId) {
        auto src = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat(leId)));
        REQUIRE(src.isValid());
        fillPattern(*src.modify());

        ImageFile sf(ImageFile::PNG);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNG);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto dst = lf.uncompressedVideoPayload();
        REQUIRE(dst.isValid());
        CHECK(dst->desc().pixelFormat().id() == beId);

        const uint16_t *srcPixels = reinterpret_cast<const uint16_t *>(src->plane(0).data());
        const uint16_t *dstPixels = reinterpret_cast<const uint16_t *>(dst->plane(0).data());
        const size_t    pixelCount = src->plane(0).size() / 2;
        for (size_t i = 0; i < pixelCount; ++i) {
                const uint16_t swapped = static_cast<uint16_t>((dstPixels[i] >> 8) | (dstPixels[i] << 8));
                if (srcPixels[i] != swapped) {
                        FAIL("Mismatch at pixel " << i << ": src=" << srcPixels[i] << " swapped(dst)=" << swapped);
                        break;
                }
        }

        std::remove(fn);
}

// ============================================================================
// Round-trip: native PNG formats
// ============================================================================

TEST_CASE("ImageFileIO PNG: Mono8 round-trip") {
        pngRoundTrip("/tmp/promeki_png_mono8.png", 64, 48, PixelFormat::Mono8_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGB8 round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgb8.png", 64, 48, PixelFormat::RGB8_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGBA8 round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgba8.png", 64, 48, PixelFormat::RGBA8_sRGB);
}

TEST_CASE("ImageFileIO PNG: Mono16 BE round-trip") {
        pngRoundTrip("/tmp/promeki_png_mono16.png", 32, 24, PixelFormat::Mono16_BE_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGB16 BE round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgb16.png", 32, 24, PixelFormat::RGB16_BE_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGBA16 BE round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgba16.png", 32, 24, PixelFormat::RGBA16_BE_sRGB);
}

// ============================================================================
// Round-trip: LE variants (byte-swap on save, loader returns BE)
// ============================================================================

TEST_CASE("ImageFileIO PNG: Mono16 LE round-trip (byte-swapped)") {
        pngLeRoundTrip("/tmp/promeki_png_mono16le.png", 32, 24, PixelFormat::Mono16_LE_sRGB,
                       PixelFormat::Mono16_BE_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGB16 LE round-trip (byte-swapped)") {
        pngLeRoundTrip("/tmp/promeki_png_rgb16le.png", 32, 24, PixelFormat::RGB16_LE_sRGB, PixelFormat::RGB16_BE_sRGB);
}

TEST_CASE("ImageFileIO PNG: RGBA16 LE round-trip (byte-swapped)") {
        pngLeRoundTrip("/tmp/promeki_png_rgba16le.png", 32, 24, PixelFormat::RGBA16_LE_sRGB,
                       PixelFormat::RGBA16_BE_sRGB);
}

// ============================================================================
// Larger size — stresses stride / packed-buffer path
// ============================================================================

TEST_CASE("ImageFileIO PNG: RGBA8 1920x1080 round-trip") {
        pngRoundTrip("/tmp/promeki_png_rgba8_hd.png", 1920, 1080, PixelFormat::RGBA8_sRGB);
}

// ============================================================================
// Unsupported pixel formats
// ============================================================================

TEST_CASE("ImageFileIO PNG: save BGR8 returns PixelFormatNotSupported") {
        auto src = UncompressedVideoPayload::allocate(ImageDesc(16, 16, PixelFormat::BGR8_sRGB));
        REQUIRE(src.isValid());
        std::memset(src.modify()->data()[0].data(), 0x42, src->plane(0).size());

        ImageFile sf(ImageFile::PNG);
        sf.setFilename("/tmp/promeki_png_bgr8.png");
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::PixelFormatNotSupported);
        std::remove("/tmp/promeki_png_bgr8.png");
}

TEST_CASE("ImageFileIO PNG: save YUV returns PixelFormatNotSupported") {
        auto src = UncompressedVideoPayload::allocate(ImageDesc(16, 16, PixelFormat::YUV8_422_UYVY_Rec709));
        REQUIRE(src.isValid());
        std::memset(src.modify()->data()[0].data(), 0x80, src->plane(0).size());

        ImageFile sf(ImageFile::PNG);
        sf.setFilename("/tmp/promeki_png_yuv.png");
        sf.setVideoPayload(src);
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
        FILE       *fp = std::fopen(fn, "wb");
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
        auto src = UncompressedVideoPayload::allocate(ImageDesc(64, 64, PixelFormat::RGBA8_sRGB));
        REQUIRE(src.isValid());
        fillPattern(*src.modify());

        const char *fn = "/tmp/promeki_png_truncated.png";
        ImageFile   sf(ImageFile::PNG);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

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
        ImageDesc   desc(32, 32, PixelFormat::RGB8_sRGB);
        desc.metadata().set(Metadata::Gamma, 2.2);
        auto src = UncompressedVideoPayload::allocate(desc);
        REQUIRE(src.isValid());
        std::memset(src.modify()->data()[0].data(), 0x7F, src->plane(0).size());

        ImageFile sf(ImageFile::PNG);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNG);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto dst = lf.uncompressedVideoPayload();
        REQUIRE(dst.isValid());
        REQUIRE(dst->desc().metadata().contains(Metadata::Gamma));
        const double g = dst->desc().metadata().get(Metadata::Gamma).get<double>();
        CHECK(g > 2.15);
        CHECK(g < 2.25);

        std::remove(fn);
}
