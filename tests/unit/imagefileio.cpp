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
#include <promeki/uncompressedvideopayload.h>

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
        const char  *fn = "/tmp/promeki_test.uyvy";

        auto src = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat::YUV8_422_UYVY_Rec709));
        REQUIRE(src.isValid());
        uint8_t *data = src.modify()->data()[0].data();
        size_t   bytes = src->plane(0).size();
        for (size_t i = 0; i < bytes; i++) data[i] = (uint8_t)(i & 0xFF);

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        auto      hint = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat::YUV8_422_UYVY_Rec709));
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setVideoPayload(hint);
        CHECK(lf.load() == Error::Ok);
        auto got = lf.uncompressedVideoPayload();
        REQUIRE(got.isValid());
        CHECK(std::memcmp(data, got->plane(0).data(), bytes) == 0);

        std::remove(fn);
}

// ============================================================================
// YUYV 8-bit round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV YUYV 8-bit round-trip") {
        const size_t w = 64, h = 48;
        const char  *fn = "/tmp/promeki_test.yuyv";

        auto src = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat::YUV8_422_Rec709));
        REQUIRE(src.isValid());
        uint8_t *data = src.modify()->data()[0].data();
        size_t   bytes = src->plane(0).size();
        for (size_t i = 0; i < bytes; i++) data[i] = (uint8_t)((i * 7) & 0xFF);

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        auto      hint = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat::YUV8_422_Rec709));
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setVideoPayload(hint);
        CHECK(lf.load() == Error::Ok);
        auto got = lf.uncompressedVideoPayload();
        REQUIRE(got.isValid());
        CHECK(std::memcmp(data, got->plane(0).data(), bytes) == 0);

        std::remove(fn);
}

// ============================================================================
// Dimension guessing
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV guesses 1920x1080 UYVY from file size") {
        const char *fn = "/tmp/promeki_test_guess.uyvy";
        auto        src = UncompressedVideoPayload::allocate(ImageDesc(1920, 1080, PixelFormat::YUV8_422_UYVY_Rec709));
        REQUIRE(src.isValid());
        std::memset(src.modify()->data()[0].data(), 0x80, src->plane(0).size());

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto got = lf.uncompressedVideoPayload();
        REQUIRE(got.isValid());
        CHECK(got->desc().width() == 1920);
        CHECK(got->desc().height() == 1080);

        std::remove(fn);
}

TEST_CASE("ImageFileIO: RawYUV guesses 1920x1080 v210 from file size") {
        const char *fn = "/tmp/promeki_test_guess.v210";
        auto        src = UncompressedVideoPayload::allocate(ImageDesc(1920, 1080, PixelFormat::YUV10_422_v210_Rec709));
        REQUIRE(src.isValid());
        std::memset(src.modify()->data()[0].data(), 0x00, src->plane(0).size());

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto got = lf.uncompressedVideoPayload();
        REQUIRE(got.isValid());
        CHECK(got->desc().width() == 1920);
        CHECK(got->desc().height() == 1080);

        std::remove(fn);
}

// ============================================================================
// v210 round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV v210 round-trip") {
        const char *fn = "/tmp/promeki_test.v210";
        auto        src = UncompressedVideoPayload::allocate(ImageDesc(1920, 1080, PixelFormat::YUV10_422_v210_Rec709));
        REQUIRE(src.isValid());
        std::memset(src.modify()->data()[0].data(), 0x00, src->plane(0).size());
        uint8_t *data = src.modify()->data()[0].data();
        for (size_t i = 0; i < 128; i++) data[i] = (uint8_t)i;

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        auto      hint = UncompressedVideoPayload::allocate(ImageDesc(1920, 1080, PixelFormat::YUV10_422_v210_Rec709));
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setVideoPayload(hint);
        CHECK(lf.load() == Error::Ok);
        auto got = lf.uncompressedVideoPayload();
        REQUIRE(got.isValid());
        CHECK(std::memcmp(data, got->plane(0).data(), 128) == 0);

        std::remove(fn);
}

// ============================================================================
// I420 planar round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV I420 planar round-trip") {
        const size_t w = 64, h = 48;
        const char  *fn = "/tmp/promeki_test.i420";

        auto src = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat::YUV8_420_Planar_Rec709));
        REQUIRE(src.isValid());
        REQUIRE(src->planeCount() == 3);

        for (size_t p = 0; p < 3; p++) {
                uint8_t *data = src.modify()->data()[p].data();
                size_t   bytes = src->plane(p).size();
                for (size_t i = 0; i < bytes; i++) data[i] = (uint8_t)((i + p * 37) & 0xFF);
        }

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        auto      hint = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat::YUV8_420_Planar_Rec709));
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setVideoPayload(hint);
        CHECK(lf.load() == Error::Ok);
        auto got = lf.uncompressedVideoPayload();
        REQUIRE(got.isValid());

        for (size_t p = 0; p < 3; p++) {
                size_t bytes = src->plane(p).size();
                CHECK(std::memcmp(src->plane(p).data(), got->plane(p).data(), bytes) == 0);
        }

        std::remove(fn);
}

// ============================================================================
// NV12 semi-planar round-trip
// ============================================================================

TEST_CASE("ImageFileIO: RawYUV NV12 round-trip") {
        const size_t w = 64, h = 48;
        const char  *fn = "/tmp/promeki_test.nv12";

        auto src = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat::YUV8_420_SemiPlanar_Rec709));
        REQUIRE(src.isValid());
        REQUIRE(src->planeCount() == 2);

        for (size_t p = 0; p < 2; p++) {
                uint8_t *data = src.modify()->data()[p].data();
                size_t   bytes = src->plane(p).size();
                for (size_t i = 0; i < bytes; i++) data[i] = (uint8_t)((i + p * 53) & 0xFF);
        }

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        auto      hint = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat::YUV8_420_SemiPlanar_Rec709));
        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        lf.setVideoPayload(hint);
        CHECK(lf.load() == Error::Ok);
        auto got = lf.uncompressedVideoPayload();
        REQUIRE(got.isValid());

        for (size_t p = 0; p < 2; p++) {
                size_t bytes = src->plane(p).size();
                CHECK(std::memcmp(src->plane(p).data(), got->plane(p).data(), bytes) == 0);
        }

        std::remove(fn);
}

// ============================================================================
// Smart .yuv detection prefers I420
// ============================================================================

TEST_CASE("ImageFileIO: Smart .yuv guesses I420 for 1920x1080") {
        const char *fn = "/tmp/promeki_test_smart.yuv";
        auto src = UncompressedVideoPayload::allocate(ImageDesc(1920, 1080, PixelFormat::YUV8_420_Planar_Rec709));
        REQUIRE(src.isValid());
        for (size_t p = 0; p < src->planeCount(); p++) {
                std::memset(src.modify()->data()[p].data(), 0x80, src->plane(p).size());
        }

        ImageFile sf(ImageFile::RawYUV);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::RawYUV);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto got = lf.uncompressedVideoPayload();
        REQUIRE(got.isValid());
        CHECK(got->desc().width() == 1920);
        CHECK(got->desc().height() == 1080);
        CHECK(got->desc().pixelFormat().id() == PixelFormat::YUV8_420_Planar_Rec709);

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
