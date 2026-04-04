/**
 * @file      imagefileio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/imagefileio.h>

using namespace promeki;

TEST_CASE("ImageFileIO: lookup invalid ID returns invalid object") {
        const ImageFileIO *io = ImageFileIO::lookup(-1);
        CHECK(io != nullptr);
        CHECK_FALSE(io->isValid());
}

TEST_CASE("ImageFileIO: lookup zero returns invalid object") {
        const ImageFileIO *io = ImageFileIO::lookup(0);
        CHECK(io != nullptr);
        CHECK_FALSE(io->isValid());
}

TEST_CASE("ImageFileIO: default constructed is invalid") {
        ImageFileIO io;
        CHECK_FALSE(io.isValid());
}

TEST_CASE("ImageFileIO: registered handlers have valid ID") {
        // PNG handler should be registered if proav is built
        const ImageFileIO *io = ImageFileIO::lookup(1);
        if(io != nullptr && io->isValid()) {
                CHECK(io->id() > 0);
                CHECK_FALSE(io->name().isEmpty());
        }
}
