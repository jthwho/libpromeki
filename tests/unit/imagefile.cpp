/**
 * @file      imagefile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/imagefile.h>

using namespace promeki;

TEST_CASE("ImageFile: default construction") {
        ImageFile f;
        CHECK(f.filename().isEmpty());
        CHECK(!f.image().isValid());
}

TEST_CASE("ImageFile: construction with ID") {
        ImageFile f(ImageFile::PNG);
        CHECK(f.filename().isEmpty());
        CHECK(!f.image().isValid());
}

TEST_CASE("ImageFile: filename accessors") {
        ImageFile f;
        CHECK(f.filename().isEmpty());
        f.setFilename("/tmp/test.png");
        CHECK(f.filename() == "/tmp/test.png");
        f.setFilename("/tmp/other.png");
        CHECK(f.filename() == "/tmp/other.png");
}

TEST_CASE("ImageFile: image accessors") {
        ImageFile f;
        CHECK(!f.image().isValid());
        Image img(64, 64, PixelFormat::RGBA8_sRGB);
        REQUIRE(img.isValid());
        f.setImage(img);
        CHECK(f.image().isValid());
        CHECK(f.image().width() == 64);
        CHECK(f.image().height() == 64);
}

TEST_CASE("ImageFile: isValid states") {
        // Default constructed with Invalid ID
        ImageFile invalid;
        // No filename and no image, not in a loadable/savable state
        CHECK(invalid.filename().isEmpty());
        CHECK(!invalid.image().isValid());

        // Constructed with PNG ID and given a filename
        ImageFile valid(ImageFile::PNG);
        valid.setFilename("/tmp/test.png");
        CHECK(!valid.filename().isEmpty());

        // With an image set as well
        Image img(32, 32, PixelFormat::RGBA8_sRGB);
        valid.setImage(img);
        CHECK(valid.image().isValid());
}
