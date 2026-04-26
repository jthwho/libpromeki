/**
 * @file      imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/imagefile.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

TEST_CASE("ImageFile: default construction") {
        ImageFile f;
        CHECK(f.filename().isEmpty());
        CHECK(!f.videoPayload().isValid());
}

TEST_CASE("ImageFile: construction with ID") {
        ImageFile f(ImageFile::PNG);
        CHECK(f.filename().isEmpty());
        CHECK(!f.videoPayload().isValid());
}

TEST_CASE("ImageFile: filename accessors") {
        ImageFile f;
        CHECK(f.filename().isEmpty());
        f.setFilename("/tmp/test.png");
        CHECK(f.filename() == "/tmp/test.png");
        f.setFilename("/tmp/other.png");
        CHECK(f.filename() == "/tmp/other.png");
}

TEST_CASE("ImageFile: payload accessors") {
        ImageFile f;
        CHECK(!f.videoPayload().isValid());
        auto payload = UncompressedVideoPayload::allocate(ImageDesc(64, 64, PixelFormat::RGBA8_sRGB));
        REQUIRE(payload.isValid());
        f.setVideoPayload(payload);
        auto got = f.videoPayload();
        CHECK(got.isValid());
        CHECK(got->desc().width() == 64);
        CHECK(got->desc().height() == 64);
}

TEST_CASE("ImageFile: isValid states") {
        ImageFile invalid;
        CHECK(invalid.filename().isEmpty());
        CHECK(!invalid.videoPayload().isValid());

        ImageFile valid(ImageFile::PNG);
        valid.setFilename("/tmp/test.png");
        CHECK(!valid.filename().isEmpty());

        auto payload = UncompressedVideoPayload::allocate(ImageDesc(32, 32, PixelFormat::RGBA8_sRGB));
        valid.setVideoPayload(payload);
        CHECK(valid.videoPayload().isValid());
}
