/**
 * @file      frame.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/frame.h>

using namespace promeki;

TEST_CASE("Frame: default construction") {
        Frame f;
        CHECK(f.imageList().isEmpty());
        CHECK(f.audioList().isEmpty());
}

TEST_CASE("Frame: metadata access") {
        Frame f;
        const auto &md = f.metadata();
        CHECK(md.isEmpty());
}
