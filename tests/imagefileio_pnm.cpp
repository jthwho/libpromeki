/**
 * @file      imagefileio_pnm.cpp
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

TEST_CASE("ImageFileIO PNM: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::PNM);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "PNM");
}

TEST_CASE("ImageFileIO PNM: PPM P6 RGB8 round-trip") {
        const char *fn = "/tmp/promeki_pnm_p6.ppm";
        Image src(64, 48, PixelDesc(PixelDesc::RGB8_sRGB));
        REQUIRE(src.isValid());
        uint8_t *data = static_cast<uint8_t *>(src.data());
        size_t bytes = src.pixelDesc().pixelFormat().planeSize(0, 64, 48);
        for(size_t i = 0; i < bytes; ++i) data[i] = static_cast<uint8_t>((i * 7) & 0xFF);

        ImageFile sf(ImageFile::PNM);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNM);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        Image dst = lf.image();
        REQUIRE(dst.isValid());
        CHECK(dst.width() == 64);
        CHECK(dst.height() == 48);
        CHECK(dst.pixelDesc().id() == PixelDesc::RGB8_sRGB);
        CHECK(std::memcmp(src.data(), dst.data(), bytes) == 0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO PNM: PGM P5 Mono8 round-trip") {
        const char *fn = "/tmp/promeki_pnm_p5.pgm";
        Image src(64, 48, PixelDesc(PixelDesc::Mono8_sRGB));
        REQUIRE(src.isValid());
        uint8_t *data = static_cast<uint8_t *>(src.data());
        size_t bytes = src.pixelDesc().pixelFormat().planeSize(0, 64, 48);
        for(size_t i = 0; i < bytes; ++i) data[i] = static_cast<uint8_t>(i & 0xFF);

        ImageFile sf(ImageFile::PNM);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNM);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        Image dst = lf.image();
        REQUIRE(dst.isValid());
        CHECK(dst.pixelDesc().id() == PixelDesc::Mono8_sRGB);
        CHECK(std::memcmp(src.data(), dst.data(), bytes) == 0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO PNM: PPM P6 RGB16 round-trip") {
        const char *fn = "/tmp/promeki_pnm_p6_16.ppm";
        Image src(32, 24, PixelDesc(PixelDesc::RGB16_BE_sRGB));
        REQUIRE(src.isValid());
        uint8_t *data = static_cast<uint8_t *>(src.data());
        size_t bytes = src.pixelDesc().pixelFormat().planeSize(0, 32, 24);
        for(size_t i = 0; i < bytes; ++i) data[i] = static_cast<uint8_t>((i * 3) & 0xFF);

        ImageFile sf(ImageFile::PNM);
        sf.setFilename(fn);
        sf.setImage(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNM);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        Image dst = lf.image();
        REQUIRE(dst.isValid());
        CHECK(dst.pixelDesc().id() == PixelDesc::RGB16_BE_sRGB);
        CHECK(std::memcmp(src.data(), dst.data(), bytes) == 0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO PNM: load nonexistent file returns error") {
        ImageFile lf(ImageFile::PNM);
        lf.setFilename("/tmp/promeki_pnm_nonexist.ppm");
        CHECK(lf.load() != Error::Ok);
}
