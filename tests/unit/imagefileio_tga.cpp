/**
 * @file      imagefileio_tga.cpp
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

TEST_CASE("ImageFileIO TGA: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::TGA);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "TGA");
}

TEST_CASE("ImageFileIO TGA: RGBA8 round-trip") {
        const char *fn = "/tmp/promeki_tga_rgba8.tga";
        auto        src = UncompressedVideoPayload::allocate(ImageDesc(64, 48, PixelFormat(PixelFormat::RGBA8_sRGB)));
        REQUIRE(src.isValid());
        uint8_t *data = src.modify()->data()[0].data();
        size_t   bytes = src->plane(0).size();
        for (size_t i = 0; i < bytes; ++i) data[i] = static_cast<uint8_t>((i * 11) & 0xFF);

        ImageFile sf(ImageFile::TGA);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::TGA);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);
        auto dst = lf.uncompressedVideoPayload();
        REQUIRE(dst.isValid());
        CHECK(dst->desc().width() == 64);
        CHECK(dst->desc().height() == 48);
        CHECK(dst->desc().pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        CHECK(std::memcmp(src->plane(0).data(), dst->plane(0).data(), bytes) == 0);

        std::remove(fn);
}

TEST_CASE("ImageFileIO TGA: load nonexistent file returns error") {
        ImageFile lf(ImageFile::TGA);
        lf.setFilename("/tmp/promeki_tga_nonexist.tga");
        CHECK(lf.load() != Error::Ok);
}

TEST_CASE("ImageFileIO TGA: load invalid file returns error") {
        const char *fn = "/tmp/promeki_tga_bad.tga";
        FILE       *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        const char garbage[] = "Not a TGA file";
        std::fwrite(garbage, 1, sizeof(garbage), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::TGA);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}
