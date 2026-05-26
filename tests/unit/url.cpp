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

// ---- redactedString ----

TEST_CASE("Url: redactedString hides last path segment") {
        Url u = Url::fromString("rtmp://h/app/streamKey").first();
        CHECK(u.redactedString() == "rtmp://h/app/***");
        // Original toString unchanged.
        CHECK(u.toString() == "rtmp://h/app/streamKey");
}

TEST_CASE("Url: redactedString hides multi-segment app stream key") {
        Url u = Url::fromString("rtmps://live.example.com/x/y/z/key").first();
        CHECK(u.redactedString() == "rtmps://live.example.com/x/y/z/***");
}

TEST_CASE("Url: redactedString hides credential-bearing query values") {
        Url u = Url::fromString("rtmp://h/app/sk?token=xyz").first();
        CHECK(u.redactedString() == "rtmp://h/app/***?token=***");
}

TEST_CASE("Url: redactedString matches credential keys case-insensitively") {
        Url u = Url::fromString("rtmp://h/app/sk?Token=xyz&AUTH=abc&Signature=zzz").first();
        String r = u.redactedString();
        CHECK(r.find("Token=***") != String::npos);
        CHECK(r.find("AUTH=***") != String::npos);
        CHECK(r.find("Signature=***") != String::npos);
        // Stream key still hidden.
        CHECK(r.find("/app/***") != String::npos);
        // Original keys are preserved (only values are redacted).
        CHECK(r.find("xyz") == String::npos);
        CHECK(r.find("abc") == String::npos);
        CHECK(r.find("zzz") == String::npos);
}

TEST_CASE("Url: redactedString preserves non-credential query values") {
        Url u = Url::fromString("rtmp://h/app/sk?app=live&framerate=30&token=secret").first();
        String r = u.redactedString();
        CHECK(r.find("app=live") != String::npos);
        CHECK(r.find("framerate=30") != String::npos);
        CHECK(r.find("token=***") != String::npos);
        CHECK(r.find("secret") == String::npos);
}

TEST_CASE("Url: redactedString on URL without path/query is a no-op") {
        Url u = Url::fromString("pmfb://studio-a").first();
        CHECK(u.redactedString() == u.toString());
}

TEST_CASE("Url: redactedString of invalid Url is empty") {
        Url u;
        CHECK(u.redactedString().isEmpty());
}

TEST_CASE("Url: redactedString does not mutate the source") {
        Url    u = Url::fromString("rtmp://h/app/sk?token=xyz").first();
        String before = u.toString();
        (void)u.redactedString();
        CHECK(u.toString() == before);
        CHECK(u.queryValue("token") == "xyz");
        CHECK(u.path() == "/app/sk");
}

// ---- briefForLog ----

TEST_CASE("Url: briefForLog returns scheme://host/path on a simple URL") {
        Url u = Url::fromString("https://example.com/api/v1/status").first();
        CHECK(u.briefForLog() == "https://example.com/api/v1/status");
}

TEST_CASE("Url: briefForLog includes non-default port") {
        Url u = Url::fromString("https://example.com:8443/api").first();
        CHECK(u.briefForLog() == "https://example.com:8443/api");
}

TEST_CASE("Url: briefForLog substitutes / for an absent path") {
        Url u = Url::fromString("https://example.com").first();
        CHECK(u.briefForLog() == "https://example.com/");
}

TEST_CASE("Url: briefForLog collapses any query to '?…'") {
        // Hugging-Face-style signed CDN URL: provider-specific query
        // keys (Expires / Policy / Signature) that no allowlist can
        // sensibly mask, plus our long opaque token.  briefForLog
        // drops all of it.
        Url u = Url::fromString(
                            "https://cas-bridge.xethub.hf.co/xet-bridge/abc/def?"
                            "Expires=1700000000&Policy=eyJ&Signature=opaqueLong")
                        .first();
        const String s = u.briefForLog();
        CHECK(s == "https://cas-bridge.xethub.hf.co/xet-bridge/abc/def?…");
        // No leakage of any query parameter name OR value.
        CHECK(s.find("Expires") == String::npos);
        CHECK(s.find("Policy") == String::npos);
        CHECK(s.find("Signature") == String::npos);
        CHECK(s.find("opaqueLong") == String::npos);
}

TEST_CASE("Url: briefForLog query indicator fires for programmatic queries too") {
        // Confirms the indicator triggers on the parsed-map form, not
        // only on the raw-query fast path.  A URL built programmatically
        // (no rawQuery roundtrip) should still get the "?…" marker.
        Url u;
        u.setScheme("https");
        u.setHost("api.example.com");
        u.setPath("/v1/lookup");
        // Direct query insertion goes through the parsed map only.
        // (We don't have a setRawQuery escape hatch in this test —
        // exercising the _query branch is the point.)
        u.setHasAuthority(true);
        Map<String, String> q;
        q["q"] = "open weather";
        u.setQuery(q);
        const String s = u.briefForLog();
        CHECK(s == "https://api.example.com/v1/lookup?…");
}

TEST_CASE("Url: briefForLog masks user-info credentials") {
        Url u = Url::fromString("https://alice:hunter2@example.com/api").first();
        const String s = u.briefForLog();
        CHECK(s == "https://***@example.com/api");
        CHECK(s.find("alice") == String::npos);
        CHECK(s.find("hunter2") == String::npos);
}

TEST_CASE("Url: briefForLog drops the fragment") {
        Url u = Url::fromString("https://example.com/docs/page#section-3").first();
        const String s = u.briefForLog();
        CHECK(s == "https://example.com/docs/page");
        CHECK(s.find("section-3") == String::npos);
}

TEST_CASE("Url: briefForLog of invalid Url is empty") {
        Url u;
        CHECK(u.briefForLog().isEmpty());
}

TEST_CASE("Url: briefForLog does not mutate the source") {
        Url    u = Url::fromString("https://example.com/api?token=secret").first();
        String before = u.toString();
        (void)u.briefForLog();
        CHECK(u.toString() == before);
        CHECK(u.queryValue("token") == "secret");
}

// ---- RTMP scheme parse — verifies the existing parser handles the form
//      RtmpClient / RtmpMediaIO will consume ----

TEST_CASE("Url: rtmp scheme parses with default port") {
        Url u = Url::fromString("rtmp://live.example.com/app/streamKey").first();
        CHECK(u.scheme() == "rtmp");
        CHECK(u.host() == "live.example.com");
        CHECK(u.port() == Url::PortUnset);
        CHECK(u.path() == "/app/streamKey");
}

TEST_CASE("Url: rtmps scheme parses with default port") {
        Url u = Url::fromString("rtmps://live.example.com/app/streamKey").first();
        CHECK(u.scheme() == "rtmps");
        CHECK(u.host() == "live.example.com");
        CHECK(u.path() == "/app/streamKey");
}

TEST_CASE("Url: rtmp explicit port") {
        Url u = Url::fromString("rtmp://example.com:1936/app/streamKey").first();
        CHECK(u.host() == "example.com");
        CHECK(u.port() == 1936);
        CHECK(u.path() == "/app/streamKey");
}

TEST_CASE("Url: rtmp multi-segment app preserves every path segment") {
        // Per the plan: leading segments form the app, the last segment
        // is the streamKey.  The URL parser keeps the full path verbatim
        // and the split is the consumer's responsibility — verify that
        // the parser at least preserves every segment.
        Url u = Url::fromString("rtmp://h/x/y/z/key?t=1").first();
        CHECK(u.path() == "/x/y/z/key");
        CHECK(u.queryValue("t") == "1");
}

TEST_CASE("Url: rtmps with token query parameter round-trips") {
        Url u = Url::fromString("rtmps://live.example.com/app/streamKey?token=xyz").first();
        CHECK(u.scheme() == "rtmps");
        CHECK(u.queryValue("token") == "xyz");
        CHECK(u.toString().contains("token=xyz"));
}
