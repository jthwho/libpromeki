/**
 * @file      imagefileio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>

using namespace promeki;

// ============================================================================
// ImageFileIO basics
// ============================================================================

TEST_CASE("ImageFileIO: lookup invalid ID returns invalid") {
        const ImageFileIO *io = ImageFileIO::lookup(-1);
        CHECK(io != nullptr);
        CHECK_FALSE(io->isValid());
}

TEST_CASE("ImageFileIO: RawYUV handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::RawYUV);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
}

// ============================================================================
// UYVY 8-bit round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV UYVY 8-bit round-trip") {
        const size_t w = 64, h = 48;
        const char *fn = "/tmp/promeki_test.uyvy";

        Image src(w, h, PixelFormat::YUV8_422_UYVY_Rec709);
        REQUIRE(src.isValid());
        uint8_t *data = static_cast<uint8_t *>(src.data());
        size_t bytes = src.pixelFormat().memLayout().planeSize(0, w, h);
        for(size_t i = 0; i < bytes; i++) data[i] = (uint8_t)(i & 0xFF);

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        Image hint(w, h, PixelFormat::YUV8_422_UYVY_Rec709);
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setImage(hint);
        CHECK(lf.load() == Error::Ok);
        CHECK(std::memcmp(data, lf.image().data(), bytes) == 0);

        std::remove(fn);
}

// ============================================================================
// YUYV 8-bit round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV YUYV 8-bit round-trip") {
        const size_t w = 64, h = 48;
        const char *fn = "/tmp/promeki_test.yuyv";

        Image src(w, h, PixelFormat::YUV8_422_Rec709);
        REQUIRE(src.isValid());
        uint8_t *data = static_cast<uint8_t *>(src.data());
        size_t bytes = src.pixelFormat().memLayout().planeSize(0, w, h);
        for(size_t i = 0; i < bytes; i++) data[i] = (uint8_t)((i * 7) & 0xFF);

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        Image hint(w, h, PixelFormat::YUV8_422_Rec709);
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setImage(hint);
        CHECK(lf.load() == Error::Ok);
        CHECK(std::memcmp(data, lf.image().data(), bytes) == 0);

        std::remove(fn);
}

// ============================================================================
// Dimension guessing
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV guesses 1920x1080 UYVY from file size") {
        const char *fn = "/tmp/promeki_test_guess.uyvy";
        Image src(1920, 1080, PixelFormat::YUV8_422_UYVY_Rec709);
        REQUIRE(src.isValid());
        src.fill(0x80);

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        CHECK(lf.image().width() == 1920);
        CHECK(lf.image().height() == 1080);

        std::remove(fn);
}

TEST_CASE("ImageFileIO: RawYUV guesses 1920x1080 v210 from file size") {
        const char *fn = "/tmp/promeki_test_guess.v210";
        Image src(1920, 1080, PixelFormat::YUV10_422_v210_Rec709);
        REQUIRE(src.isValid());
        src.fill(0x00);

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        CHECK(lf.image().width() == 1920);
        CHECK(lf.image().height() == 1080);

        std::remove(fn);
}

// ============================================================================
// v210 round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV v210 round-trip") {
        const char *fn = "/tmp/promeki_test.v210";
        Image src(1920, 1080, PixelFormat::YUV10_422_v210_Rec709);
        REQUIRE(src.isValid());
        src.fill(0x00);
        uint8_t *data = static_cast<uint8_t *>(src.data());
        for(size_t i = 0; i < 128; i++) data[i] = (uint8_t)i;

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        Image hint(1920, 1080, PixelFormat::YUV10_422_v210_Rec709);
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setImage(hint);
        CHECK(lf.load() == Error::Ok);
        CHECK(std::memcmp(data, lf.image().data(), 128) == 0);

        std::remove(fn);
}

// ============================================================================
// I420 planar round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV I420 planar round-trip") {
        const size_t w = 64, h = 48;
        const char *fn = "/tmp/promeki_test.i420";

        Image src(w, h, PixelFormat::YUV8_420_Planar_Rec709);
        REQUIRE(src.isValid());
        REQUIRE(src.pixelFormat().planeCount() == 3);

        for(size_t p = 0; p < 3; p++) {
                uint8_t *data = static_cast<uint8_t *>(src.data(p));
                size_t bytes = src.pixelFormat().memLayout().planeSize(p, w, h);
                for(size_t i = 0; i < bytes; i++) data[i] = (uint8_t)((i + p * 37) & 0xFF);
        }

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        Image hint(w, h, PixelFormat::YUV8_420_Planar_Rec709);
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setImage(hint);
        CHECK(lf.load() == Error::Ok);

        for(size_t p = 0; p < 3; p++) {
                size_t bytes = src.pixelFormat().memLayout().planeSize(p, w, h);
                CHECK(std::memcmp(src.data(p), lf.image().data(p), bytes) == 0);
        }

        std::remove(fn);
}

// ============================================================================
// NV12 semi-planar round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV NV12 round-trip") {
        const size_t w = 64, h = 48;
        const char *fn = "/tmp/promeki_test.nv12";

        Image src(w, h, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        REQUIRE(src.isValid());
        REQUIRE(src.pixelFormat().planeCount() == 2);

        for(size_t p = 0; p < 2; p++) {
                uint8_t *data = static_cast<uint8_t *>(src.data(p));
                size_t bytes = src.pixelFormat().memLayout().planeSize(p, w, h);
                for(size_t i = 0; i < bytes; i++) data[i] = (uint8_t)((i + p * 53) & 0xFF);
        }

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        Image hint(w, h, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setImage(hint);
        CHECK(lf.load() == Error::Ok);

        for(size_t p = 0; p < 2; p++) {
                size_t bytes = src.pixelFormat().memLayout().planeSize(p, w, h);
                CHECK(std::memcmp(src.data(p), lf.image().data(p), bytes) == 0);
        }

        std::remove(fn);
}

// ============================================================================
// Smart .yuv detection prefers I420
// ============================================================================

TEST_CASE("ImageFileIO: Smart .yuv guesses I420 for 1920x1080") {
        const char *fn = "/tmp/promeki_test_smart.yuv";
        Image src(1920, 1080, PixelFormat::YUV8_420_Planar_Rec709);
        REQUIRE(src.isValid());
        src.fill(0x80);

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        CHECK(lf.image().width() == 1920);
        CHECK(lf.image().height() == 1080);
        CHECK(lf.image().pixelFormat().id() == PixelFormat::YUV8_420_Planar_Rec709);

        std::remove(fn);
}

// ============================================================================
// Unsupported extension
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV unsupported extension returns error") {
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename("/tmp/test.bmp");
        CHECK(lf.load() != Error::Ok);
}
