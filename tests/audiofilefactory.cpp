/**
 * @file      audiofilefactory.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/audiofilefactory.h>
#include <promeki/core/string.h>

using namespace promeki;

TEST_CASE("AudioFileFactory: lookup with invalid params returns nullptr") {
        const AudioFileFactory *f = AudioFileFactory::lookup(-1, "nonexistent.xyz");
        CHECK(f == nullptr);
}

TEST_CASE("AudioFileFactory: default constructed has empty name") {
        AudioFileFactory f;
        CHECK(f.name().isEmpty());
}

TEST_CASE("AudioFileFactory: lookup unsupported extension returns nullptr") {
        const AudioFileFactory *f = AudioFileFactory::lookup(0, "test.unsupported_format_xyz");
        CHECK(f == nullptr);
}
