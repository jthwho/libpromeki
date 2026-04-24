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
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

TEST_CASE("ImageFileIO PNM: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::PNM);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "PNM");
}

static void pnmRoundTrip(const char *fn, size_t w, size_t h, PixelFormat::ID pdId,
                         uint8_t seed) {
        auto src = UncompressedVideoPayload::allocate(
                ImageDesc(w, h, PixelFormat(pdId)));
        REQUIRE(src.isValid());
        uint8_t *data = src.modify()->data()[0].data();
        size_t bytes = src->plane(0).size();
        for(size_t i = 0; i < bytes; ++i) data[i] = static_cast<uint8_t>((i * seed) & 0xFF);

        ImageFile sf(ImageFile::PNM);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::PNM);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto dst = lf.uncompressedVideoPayload();
        REQUIRE(dst.isValid());
        CHECK(dst->desc().width() == w);
        CHECK(dst->desc().height() == h);
        CHECK(dst->desc().pixelFormat().id() == pdId);
        CHECK(std::memcmp(src->plane(0).data(), dst->plane(0).data(), bytes) == 0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO PNM: PPM P6 RGB8 round-trip") {
        pnmRoundTrip("/tmp/promeki_pnm_p6.ppm", 64, 48, PixelFormat::RGB8_sRGB, 7);
}

TEST_CASE("ImageFileIO PNM: PGM P5 Mono8 round-trip") {
        pnmRoundTrip("/tmp/promeki_pnm_p5.pgm", 64, 48, PixelFormat::Mono8_sRGB, 1);
}

TEST_CASE("ImageFileIO PNM: PPM P6 RGB16 round-trip") {
        pnmRoundTrip("/tmp/promeki_pnm_p6_16.ppm", 32, 24, PixelFormat::RGB16_BE_sRGB, 3);
}

TEST_CASE("ImageFileIO PNM: load nonexistent file returns error") {
        ImageFile lf(ImageFile::PNM);
        lf.setFilename("/tmp/promeki_pnm_nonexist.ppm");
        CHECK(lf.load() != Error::Ok);
}
