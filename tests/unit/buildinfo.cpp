/**
 * @file      buildinfo.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/buildinfo.h>

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

TEST_CASE("BuildInfo: buildInfoString") {
        String s = buildInfoString();
        CHECK(!s.isEmpty());
        const BuildInfo *bi = getBuildInfo();
        CHECK(s.contains(bi->name));
        CHECK(s.contains(bi->version));
        CHECK(s.contains(bi->type));
}

TEST_CASE("BuildInfo: buildPlatformString") {
        String s = buildPlatformString();
        CHECK(!s.isEmpty());
        CHECK(s.contains("Platform:"));
        CHECK(s.contains("Compiler:"));
        CHECK(s.contains("C++:"));
}

TEST_CASE("BuildInfo: buildFeatureString") {
        String s = buildFeatureString();
        CHECK(!s.isEmpty());
        CHECK(s.contains("Features:"));
}

TEST_CASE("BuildInfo: runtimeInfoString") {
        String s = runtimeInfoString();
        CHECK(!s.isEmpty());
        CHECK(s.contains("Hardware:"));
        CHECK(s.contains("CPUs"));
        CHECK(s.contains("PID:"));
}

TEST_CASE("BuildInfo: debugStatusString") {
        String s = debugStatusString();
        CHECK(!s.isEmpty());
        CHECK(s.contains("promekiDebug()"));
}

TEST_CASE("BuildInfo: buildInfoStrings") {
        StringList lines = buildInfoStrings();
        CHECK(lines.size() == 5);
        CHECK(lines[0] == buildInfoString());
        CHECK(lines[1] == buildPlatformString());
        CHECK(lines[2] == buildFeatureString());
        CHECK(lines[3] == runtimeInfoString());
        CHECK(lines[4] == debugStatusString());
}
