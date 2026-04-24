/**
 * @file      imagefileio_sgi.cpp
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

TEST_CASE("ImageFileIO SGI: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::SGI);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "SGI");
}

static void sgiRoundTrip(const char *fn, size_t w, size_t h, PixelFormat::ID pdId) {
        auto src = UncompressedVideoPayload::allocate(
                ImageDesc(w, h, PixelFormat(pdId)));
        REQUIRE(src.isValid());
        uint8_t *data = src.modify()->data()[0].data();
        size_t bytes = src->plane(0).size();
        for(size_t i = 0; i < bytes; ++i) data[i] = static_cast<uint8_t>((i * 7 + 31) & 0xFF);

        ImageFile sf(ImageFile::SGI);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::SGI);
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

TEST_CASE("ImageFileIO SGI: Mono8 round-trip") {
        sgiRoundTrip("/tmp/promeki_sgi_mono8.sgi", 64, 48, PixelFormat::Mono8_sRGB);
}

TEST_CASE("ImageFileIO SGI: RGB8 round-trip") {
        sgiRoundTrip("/tmp/promeki_sgi_rgb8.sgi", 64, 48, PixelFormat::RGB8_sRGB);
}

TEST_CASE("ImageFileIO SGI: RGBA8 round-trip") {
        sgiRoundTrip("/tmp/promeki_sgi_rgba8.sgi", 64, 48, PixelFormat::RGBA8_sRGB);
}

TEST_CASE("ImageFileIO SGI: Mono16 BE round-trip") {
        sgiRoundTrip("/tmp/promeki_sgi_mono16.sgi", 32, 24, PixelFormat::Mono16_BE_sRGB);
}

TEST_CASE("ImageFileIO SGI: RGB16 BE round-trip") {
        sgiRoundTrip("/tmp/promeki_sgi_rgb16.sgi", 32, 24, PixelFormat::RGB16_BE_sRGB);
}

TEST_CASE("ImageFileIO SGI: RGBA16 BE round-trip") {
        sgiRoundTrip("/tmp/promeki_sgi_rgba16.sgi", 32, 24, PixelFormat::RGBA16_BE_sRGB);
}

TEST_CASE("ImageFileIO SGI: load nonexistent file returns error") {
        ImageFile lf(ImageFile::SGI);
        lf.setFilename("/tmp/promeki_sgi_nonexist.sgi");
        CHECK(lf.load() != Error::Ok);
}

TEST_CASE("ImageFileIO SGI: load invalid file returns error") {
        const char *fn = "/tmp/promeki_sgi_bad.sgi";
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        const char garbage[] = "Not an SGI file";
        std::fwrite(garbage, 1, sizeof(garbage), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::SGI);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}
