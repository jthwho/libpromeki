/**
 * @file      buildinfo.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/buildinfo.h>
#include <promeki/buildident.h>
#include <promeki/error.h>
#include <cstdio>
#include <cstring>

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

TEST_CASE("BuildInfo: numeric version components agree with version string") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        REQUIRE(bi->version != nullptr);
        // version string is always MAJOR.MINOR.PATCH — no CI build number,
        // no rc/beta tag (those live in their own fields).
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d.%d.%d",
                      bi->major, bi->minor, bi->patch);
        CHECK(std::strcmp(buf, bi->version) == 0);
}

TEST_CASE("BuildInfo: numeric components are non-negative") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        CHECK(bi->major >= 0);
        CHECK(bi->minor >= 0);
        CHECK(bi->patch >= 0);
        CHECK(bi->build >= 0);  // 0 for local dev builds, positive for CI
        CHECK(bi->stageNum >= 0);
}

TEST_CASE("BuildInfo: version string never contains CI build number or stage tag") {
        // BuildInfo::version is always MAJOR.MINOR.PATCH — the CI build number
        // and pre-release stage tag both live in their own fields and are only
        // re-composed for display via formatBuildInfo() / BuildInfo::ident.
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi->version != nullptr);
        CHECK(std::strchr(bi->version, '+') == nullptr);
        CHECK(std::strchr(bi->version, '-') == nullptr);
}

TEST_CASE("BuildInfo: stage / stageNum invariants") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        // Release ↔ stageNum==0 ; alpha/beta/rc ↔ stageNum>=1.
        if (bi->stage == BuildStage::Release) {
                CHECK(bi->stageNum == 0);
        } else {
                CHECK(bi->stageNum >= 1);
        }
}

TEST_CASE("BuildInfo: buildStageName matches the documented spellings") {
        CHECK(std::strcmp(buildStageName(BuildStage::Alpha),   "alpha")   == 0);
        CHECK(std::strcmp(buildStageName(BuildStage::Beta),    "beta")    == 0);
        CHECK(std::strcmp(buildStageName(BuildStage::RC),      "rc")      == 0);
        CHECK(std::strcmp(buildStageName(BuildStage::Release), "release") == 0);
}

TEST_CASE("formatBuildInfo: simple tokens against current BuildInfo") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        CHECK(formatBuildInfo("{version}") == String(bi->version));
        CHECK(formatBuildInfo("{name}")    == String(bi->name));
        CHECK(formatBuildInfo("{ident}")   == String(bi->ident));
}

TEST_CASE("BuildInfo: ref is set and routed through the formatter") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        REQUIRE(bi->ref != nullptr);
        // ref is always populated — empty would be a regression of the
        // CMake fallback chain that defaults the value to "unknown".
        CHECK(bi->ref[0] != '\0');
        CHECK(formatBuildInfo("{ref}") == String(bi->ref));
}

TEST_CASE("formatBuildInfo: every BuildInfo field is reachable via a token") {
        // Build a BuildInfo with a unique sentinel value for each field so
        // we can detect cross-wiring (e.g. {date} accidentally reading
        // bi->time).  If a field is added to the struct without a matching
        // token in formatBuildInfo, this test still passes — but the
        // exhaustive list immediately below it documents the expected
        // 1:1 mapping, so the missing token gets caught at review time.
        BuildInfo bi {};
        bi.name      = "S_name";
        bi.version   = "S_version";
        bi.repoident = "S_repoident";
        bi.ref       = "S_ref";
        bi.date      = "S_date";
        bi.time      = "S_time";
        bi.hostname  = "S_hostname";
        bi.type      = "S_type";
        bi.major     = 11;
        bi.minor     = 22;
        bi.patch     = 33;
        bi.build     = 44;
        bi.stage     = BuildStage::Beta;
        bi.stageNum  = 5;
        bi.ident     = "S_ident";

        // String fields — each token returns its own value, no cross-wiring.
        CHECK(formatBuildInfo("{name}",      &bi) == "S_name");
        CHECK(formatBuildInfo("{version}",   &bi) == "S_version");
        CHECK(formatBuildInfo("{repoident}", &bi) == "S_repoident");
        CHECK(formatBuildInfo("{ref}",       &bi) == "S_ref");
        CHECK(formatBuildInfo("{date}",      &bi) == "S_date");
        CHECK(formatBuildInfo("{time}",      &bi) == "S_time");
        CHECK(formatBuildInfo("{hostname}",  &bi) == "S_hostname");
        CHECK(formatBuildInfo("{type}",      &bi) == "S_type");
        CHECK(formatBuildInfo("{ident}",     &bi) == "S_ident");

        // Integer fields.
        CHECK(formatBuildInfo("{major}",    &bi) == "11");
        CHECK(formatBuildInfo("{minor}",    &bi) == "22");
        CHECK(formatBuildInfo("{patch}",    &bi) == "33");
        CHECK(formatBuildInfo("{build}",    &bi) == "44");
        CHECK(formatBuildInfo("{stageNum}", &bi) == "5");

        // Stage (enum) — surfaces as the lower-case short name.
        CHECK(formatBuildInfo("{stage}", &bi) == "beta");

        // {extra} is computed from stage+stageNum (not a struct field).
        // Beta with stageNum=5 → "-beta5" (numbered because stageNum > 1).
        CHECK(formatBuildInfo("{extra}", &bi) == "-beta5");

        // All 16 tokens composed at once (15 BuildInfo fields + {extra}):
        // sanity-check that the formatter has no token-position bugs.
        const String big =
                formatBuildInfo("{name}|{version}|{major}|{minor}|{patch}|{build}|"
                                "{stage}|{stageNum}|{extra}|{repoident}|{ref}|"
                                "{date}|{time}|{type}|{hostname}|{ident}",
                                &bi);
        CHECK(big ==
              "S_name|S_version|11|22|33|44|"
              "beta|5|-beta5|S_repoident|S_ref|"
              "S_date|S_time|S_type|S_hostname|S_ident");
}

TEST_CASE("formatBuildInfo: numeric tokens reconstruct the version triple") {
        // {major}.{minor}.{patch} should always equal {version} (since version
        // never contains the stage tag or build number).
        const BuildInfo *bi = getBuildInfo();
        CHECK(formatBuildInfo("{major}.{minor}.{patch}") == String(bi->version));
}

TEST_CASE("formatBuildInfo: brace escaping") {
        CHECK(formatBuildInfo("plain")              == "plain");
        CHECK(formatBuildInfo("{{escaped}}")        == "{escaped}");
        CHECK(formatBuildInfo("{{ {version} }}")    == String("{ ") + getBuildInfo()->version + " }");
}

TEST_CASE("formatBuildInfo: unknown tokens emit verbatim") {
        CHECK(formatBuildInfo("{notARealToken}") == "{notARealToken}");
        CHECK(formatBuildInfo("a{nope}b")        == "a{nope}b");
}

TEST_CASE("formatBuildInfo: synthetic BuildInfo for stage suffix rules") {
        // Build a BuildInfo we control so the test isn't tied to whatever stage
        // the live library happens to be in.
        BuildInfo bi {};
        bi.name      = "libtest";
        bi.version   = "1.2.3";
        bi.repoident = "abc";
        bi.date      = "2026-04-30";
        bi.time      = "12:00:00";
        bi.hostname  = "h";
        bi.type      = "Release";
        bi.major = 1; bi.minor = 2; bi.patch = 3; bi.build = 0;
        bi.ident = "ignored";

        // Release: empty {extra}.
        bi.stage = BuildStage::Release; bi.stageNum = 0;
        CHECK(formatBuildInfo("{version}{extra}", &bi) == "1.2.3");

        // Alpha 1: prints as "-alpha", no number.
        bi.stage = BuildStage::Alpha; bi.stageNum = 1;
        CHECK(formatBuildInfo("{version}{extra}", &bi) == "1.2.3-alpha");
        CHECK(formatBuildInfo("{stage}{stageNum}",  &bi) == "alpha1");  // raw access still has the 1

        // Alpha 2: prints with the number.
        bi.stage = BuildStage::Alpha; bi.stageNum = 2;
        CHECK(formatBuildInfo("{version}{extra}", &bi) == "1.2.3-alpha2");

        // Beta 1: same shorthand as alpha.
        bi.stage = BuildStage::Beta; bi.stageNum = 1;
        CHECK(formatBuildInfo("{version}{extra}", &bi) == "1.2.3-beta");
        bi.stageNum = 3;
        CHECK(formatBuildInfo("{version}{extra}", &bi) == "1.2.3-beta3");

        // RC always carries its number — even when stageNum == 1.
        bi.stage = BuildStage::RC; bi.stageNum = 1;
        CHECK(formatBuildInfo("{version}{extra}", &bi) == "1.2.3-rc1");
        bi.stageNum = 2;
        CHECK(formatBuildInfo("{version}{extra}", &bi) == "1.2.3-rc2");
}

TEST_CASE("BuildInfo: ident is set and non-empty") {
        const BuildInfo *bi = getBuildInfo();
        REQUIRE(bi != nullptr);
        REQUIRE(bi->ident != nullptr);
        CHECK(bi->ident[0] != '\0');
}

TEST_CASE("BuildInfo: ident matches PROMEKI_BUILD_IDENT (this TU is not stale)") {
        // Both this TU and the library's buildinfo.cpp captured the same
        // BUILD_INFO_IDENT at the same configure pass, so they must match.
        // If this fails, the library was rebuilt without rebuilding the test
        // executable — which is exactly the staleness condition the
        // mechanism is designed to catch.
        Error err;
        CHECK(verifyBuildIdent(PROMEKI_BUILD_IDENT, &err));
        CHECK(err == Error::Ok);
        CHECK(PROMEKI_VERIFY_BUILD_IDENT());
}

TEST_CASE("BuildInfo: verifyBuildIdent reports BuildIdentMismatch on mismatch") {
        Error err;
        CHECK_FALSE(verifyBuildIdent("not-a-real-build-ident", &err));
        CHECK(err == Error::BuildIdentMismatch);
}

TEST_CASE("BuildInfo: verifyBuildIdent tolerates null expected") {
        Error err;
        CHECK_FALSE(verifyBuildIdent(nullptr, &err));
        CHECK(err == Error::BuildIdentMismatch);
}

TEST_CASE("BuildInfo: verifyBuildIdent without err pointer returns bool") {
        CHECK(verifyBuildIdent(PROMEKI_BUILD_IDENT));
        CHECK_FALSE(verifyBuildIdent("definitely-not-the-ident"));
}
