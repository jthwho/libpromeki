/**
 * @file      url.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/url.h>

using namespace promeki;

TEST_CASE("Url: default is invalid") {
        Url u;
        CHECK_FALSE(u.isValid());
        CHECK(u.toString().isEmpty());
}

TEST_CASE("Url: invalid strings fail to parse") {
        Result<Url> r = Url::fromString(String());
        CHECK_FALSE(r.first().isValid());
        CHECK(r.second().isError());

        r = Url::fromString("no-scheme");
        CHECK_FALSE(r.first().isValid());
        CHECK(r.second().isError());

        r = Url::fromString(":scheme-first");
        CHECK_FALSE(r.first().isValid());
        CHECK(r.second().isError());

        r = Url::fromString("1bad://x");
        CHECK_FALSE(r.first().isValid());
        CHECK(r.second().isError());
}

TEST_CASE("Url: pmfb authority form") {
        Url u = Url::fromString("pmfb://studio-a").first();
        CHECK(u.isValid());
        CHECK(u.scheme() == "pmfb");
        CHECK(u.hasAuthority());
        CHECK(u.host() == "studio-a");
        CHECK(u.port() == Url::PortUnset);
        CHECK(u.path().isEmpty());
        CHECK(u.query().isEmpty());
}

TEST_CASE("Url: scheme is lowercased") {
        Url u = Url::fromString("PMFB://Foo").first();
        CHECK(u.scheme() == "pmfb");
        // Host preserves the caller's case — only the scheme is normalized.
        CHECK(u.host() == "Foo");
}

TEST_CASE("Url: query parameters") {
        Url u = Url::fromString("pmfb://bridge?ring=4&sync=true&empty=&flag").first();
        CHECK(u.host() == "bridge");
        CHECK(u.queryValue("ring") == "4");
        CHECK(u.queryValue("sync") == "true");
        CHECK(u.hasQueryValue("empty"));
        CHECK(u.queryValue("empty").isEmpty());
        CHECK(u.hasQueryValue("flag"));
        CHECK(u.queryValue("flag").isEmpty());
        CHECK(u.queryValue("missing", "fallback") == "fallback");
}

TEST_CASE("Url: percent decoding in host and query") {
        Url u = Url::fromString("pmfb://hello%20world?k=v%20s&%3Dkey=%26val").first();
        CHECK(u.host() == "hello world");
        CHECK(u.queryValue("k") == "v s");
        CHECK(u.queryValue("=key") == "&val");
}

TEST_CASE("Url: userinfo and port") {
        Url u = Url::fromString("http://alice:secret@example.com:8080/path").first();
        CHECK(u.userInfo() == "alice:secret");
        CHECK(u.host() == "example.com");
        CHECK(u.port() == 8080);
        CHECK(u.path() == "/path");
}

TEST_CASE("Url: IPv6 literal") {
        Url u = Url::fromString("http://[::1]:8080/").first();
        CHECK(u.host() == "::1");
        CHECK(u.port() == 8080);
        CHECK(u.path() == "/");
}

TEST_CASE("Url: opaque form") {
        Url u = Url::fromString("mailto:foo@example.com").first();
        CHECK(u.isValid());
        CHECK(u.scheme() == "mailto");
        CHECK_FALSE(u.hasAuthority());
        CHECK(u.path() == "foo@example.com");
}

TEST_CASE("Url: fragment") {
        Url u = Url::fromString("http://x/path?q=1#top").first();
        CHECK(u.fragment() == "top");
        CHECK(u.queryValue("q") == "1");
}

TEST_CASE("Url: round-trip pmfb authority-only") {
        String orig("pmfb://studio-a");
        Url    u = Url::fromString(orig).first();
        CHECK(u.toString() == orig);
}

TEST_CASE("Url: round-trip with query") {
        // Reparse rather than string-compare — query map ordering is
        // not guaranteed to match the original serialized form.
        Url a = Url::fromString("pmfb://bridge?ring=4&sync=true").first();
        Url b = Url::fromString(a.toString()).first();
        CHECK(a == b);
}

TEST_CASE("Url: round-trip preserves percent-encoding need") {
        Url a = Url::fromString("pmfb://name%20with%20spaces").first();
        Url b = Url::fromString(a.toString()).first();
        CHECK(a == b);
        CHECK(b.host() == "name with spaces");
}

TEST_CASE("Url: builder API") {
        Url u;
        u.setScheme("pmfb").setHost("studio-a").setQueryValue("ring", "4");
        CHECK(u.isValid());
        CHECK(u.hasAuthority());
        Url roundTrip = Url::fromString(u.toString()).first();
        CHECK(roundTrip == u);
        CHECK(roundTrip.host() == "studio-a");
        CHECK(roundTrip.queryValue("ring") == "4");
}

TEST_CASE("Url: equality") {
        Url a = Url::fromString("pmfb://x?a=1&b=2").first();
        Url b = Url::fromString("pmfb://x?b=2&a=1").first();
        CHECK(a == b);

        Url c = Url::fromString("pmfb://x?a=1").first();
        CHECK(a != c);

        Url d = Url::fromString("pmfb://y?a=1&b=2").first();
        CHECK(a != d);
}

TEST_CASE("Url: removeQueryValue") {
        Url u = Url::fromString("pmfb://x?a=1&b=2").first();
        u.removeQueryValue("a");
        CHECK_FALSE(u.hasQueryValue("a"));
        CHECK(u.queryValue("b") == "2");
}

TEST_CASE("Url: percentEncode basics") {
        CHECK(Url::percentEncode("hello") == "hello");
        CHECK(Url::percentEncode("hello world") == "hello%20world");
        CHECK(Url::percentEncode("a/b", "/") == "a/b");
        CHECK(Url::percentEncode("a/b") == "a%2Fb");
        CHECK(Url::percentEncode("~-._") == "~-._");
}

TEST_CASE("Url: percentDecode basics") {
        CHECK(Url::percentDecode("hello") == "hello");
        CHECK(Url::percentDecode("hello%20world") == "hello world");
        CHECK(Url::percentDecode("a%2Fb") == "a/b");

        Error  err = Error::Ok;
        String out = Url::percentDecode("bad%", &err);
        CHECK(err.isError());

        err = Error::Ok;
        out = Url::percentDecode("bad%ZZ", &err);
        CHECK(err.isError());
}

TEST_CASE("Url: constructor conveniences") {
        Url a("pmfb://x");
        Url b(String("pmfb://x"));
        CHECK(a == b);
}
