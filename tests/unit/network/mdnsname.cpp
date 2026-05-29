/**
 * @file      mdnsname.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mdnsname.h>

using namespace promeki;

TEST_CASE("mdnsEscapeLabel: passes plain ASCII unchanged") {
        CHECK(mdnsEscapeLabel(String("Studio Camera")) == String("Studio Camera"));
        CHECK(mdnsEscapeLabel(String("_http")) == String("_http"));
}

TEST_CASE("mdnsEscapeLabel: backslashes literal dot and literal backslash") {
        CHECK(mdnsEscapeLabel(String("Studio.B Camera")) == String("Studio\\.B Camera"));
        CHECK(mdnsEscapeLabel(String("path\\to")) == String("path\\\\to"));
}

TEST_CASE("mdnsUnescapeLabel: round-trips the escape pair") {
        const String raw = String("Studio.B Camera");
        CHECK(mdnsUnescapeLabel(mdnsEscapeLabel(raw)) == raw);
}

TEST_CASE("mdnsUnescapeLabel: accepts three-digit decimal byte escapes") {
        // \\009 → byte 0x09 (tab)
        String esc = String("a\\009b");
        String out = mdnsUnescapeLabel(esc);
        REQUIRE(out.size() == 3);
        CHECK(out[0] == 'a');
        CHECK(out[1] == '\t');
        CHECK(out[2] == 'b');
}

TEST_CASE("mdnsSplitName: ignores escaped dots inside a label") {
        List<String> labels = mdnsSplitName(String("Studio\\.B Camera._http._tcp.local."));
        REQUIRE(labels.size() == 4);
        CHECK(labels[0] == String("Studio.B Camera"));
        CHECK(labels[1] == String("_http"));
        CHECK(labels[2] == String("_tcp"));
        CHECK(labels[3] == String("local"));
}

TEST_CASE("mdnsSplitName: plain name without escapes splits on dots") {
        List<String> labels = mdnsSplitName(String("_http._tcp.local."));
        REQUIRE(labels.size() == 3);
        CHECK(labels[0] == String("_http"));
        CHECK(labels[1] == String("_tcp"));
        CHECK(labels[2] == String("local"));
}

TEST_CASE("mdnsSplitName: trailing root dot is dropped") {
        // With or without — both yield the same label list.
        List<String> a = mdnsSplitName(String("_http._tcp.local."));
        List<String> b = mdnsSplitName(String("_http._tcp.local"));
        CHECK(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i) CHECK(a[i] == b[i]);
}

TEST_CASE("mdnsJoinName: round-trips through mdnsSplitName") {
        List<String> labels;
        labels += String("Studio.B Camera");
        labels += String("_http");
        labels += String("_tcp");
        labels += String("local");
        String joined = mdnsJoinName(labels);
        List<String> back = mdnsSplitName(joined);
        REQUIRE(back.size() == labels.size());
        for (size_t i = 0; i < labels.size(); ++i) CHECK(back[i] == labels[i]);
}
