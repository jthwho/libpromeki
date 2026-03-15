/**
 * @file      buildinfo.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/core/buildinfo.h>

using namespace promeki;

TEST_CASE("BuildInfo: getBuildInfo returns non-null") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
}

TEST_CASE("BuildInfo: name is set") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        CHECK(bi->name != nullptr);
}

TEST_CASE("BuildInfo: version is set") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        CHECK(bi->version != nullptr);
}

TEST_CASE("BuildInfo: date is set") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        CHECK(bi->date != nullptr);
}

TEST_CASE("BuildInfo: time is set") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        CHECK(bi->time != nullptr);
}

TEST_CASE("BuildInfo: type is set") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        CHECK(bi->type != nullptr);
}

TEST_CASE("BuildInfo: logBuildInfo does not crash") {
        logBuildInfo();
        CHECK(true);
}
