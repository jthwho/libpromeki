/**
 * @file      ancdetails.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Exercises the AncDetails value type: line accumulation, issue
 * severity bookkeeping, and the toString / toJson renderings.
 */

#include <doctest/doctest.h>
#include <promeki/ancdetails.h>
#include <promeki/enums_anc.h>
#include <promeki/json.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("AncDetails: default-constructed is empty") {
        AncDetails d;
        CHECK(d.isEmpty());
        CHECK(d.lines().isEmpty());
        CHECK(d.issues().isEmpty());
        CHECK_FALSE(d.hasWarnings());
        CHECK_FALSE(d.hasErrors());
}

TEST_CASE("AncDetails: addField formats name = value lines") {
        AncDetails d;
        d.addField("Payload", "VITC1");
        d.addField("Timecode", "01:00:00:00");
        REQUIRE(d.lines().size() == 2);
        CHECK(d.lines()[0] == "Payload = VITC1");
        CHECK(d.lines()[1] == "Timecode = 01:00:00:00");
        CHECK_FALSE(d.isEmpty());
}

TEST_CASE("AncDetails: addLine appends free-form lines verbatim") {
        AncDetails d;
        d.addLine("-- ST 291 header --");
        REQUIRE(d.lines().size() == 1);
        CHECK(d.lines()[0] == "-- ST 291 header --");
}

TEST_CASE("AncDetails: issue severity bookkeeping") {
        AncDetails d;
        d.addInfo("bar data present");
        d.addWarning("DC=4, expected 8");
        d.addWarning("reserved bit set");
        d.addError("checksum mismatch");

        REQUIRE(d.issues().size() == 4);
        CHECK(d.issueCount(AncDetailSeverity::Info) == 1);
        CHECK(d.issueCount(AncDetailSeverity::Warning) == 2);
        CHECK(d.issueCount(AncDetailSeverity::Error) == 1);
        CHECK(d.hasWarnings());
        CHECK(d.hasErrors());

        // Severity + message recorded faithfully.
        CHECK(d.issues()[0].severity == AncDetailSeverity::Info);
        CHECK(d.issues()[0].message == "bar data present");
        CHECK(d.issues()[3].severity == AncDetailSeverity::Error);
        CHECK(d.issues()[3].message == "checksum mismatch");
}

TEST_CASE("AncDetails: addIssue with explicit severity") {
        AncDetails d;
        d.addIssue(AncDetailSeverity::Warning, "explicit warning");
        REQUIRE(d.issues().size() == 1);
        CHECK(d.issues()[0].severity == AncDetailSeverity::Warning);
        CHECK(d.hasWarnings());
}

TEST_CASE("AncDetails: toString renders lines then severity-prefixed issues") {
        AncDetails d;
        d.addField("Payload", "VITC1");
        d.addWarning("DC=4, expected 8");
        String s = d.toString();
        CHECK(s == String("Payload = VITC1\n[Warning] DC=4, expected 8"));
}

TEST_CASE("AncDetails: toJson carries lines and issues arrays") {
        AncDetails d;
        d.addField("Payload", "VITC1");
        d.addError("checksum mismatch");

        JsonObject obj = d.toJson();
        JsonArray  lines = obj.value("lines").toArray();
        JsonArray  issues = obj.value("issues").toArray();

        REQUIRE(lines.size() == 1);
        CHECK(lines.at(0).toString() == "Payload = VITC1");

        REQUIRE(issues.size() == 1);
        JsonObject issue = issues.at(0).toObject();
        CHECK(issue.value("severity").toString() == "Error");
        CHECK(issue.value("message").toString() == "checksum mismatch");
}

TEST_CASE("AncDetails: equality compares lines and issues") {
        AncDetails a;
        a.addField("Payload", "VITC1");
        a.addWarning("w");

        AncDetails b;
        b.addField("Payload", "VITC1");
        b.addWarning("w");

        CHECK(a == b);

        b.addInfo("extra");
        CHECK(a != b);
}
