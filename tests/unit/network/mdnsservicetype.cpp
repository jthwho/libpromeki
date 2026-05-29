/**
 * @file      mdnsservicetype.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mdnsservicetype.h>
#include <promeki/variant.h>
#include <promeki/datastream.h>
#include <promeki/buffer.h>
#include <promeki/bufferiodevice.h>

using namespace promeki;
using Protocol = MdnsServiceType::Protocol;

TEST_CASE("MdnsServiceType: default is invalid") {
        MdnsServiceType t;
        CHECK_FALSE(t.isValid());
        CHECK(t.proto() == Protocol::Invalid);
        CHECK(t.app().isEmpty());
        // Domain defaults to "local" even on the invalid type — toString
        // still returns empty because validation gates the output.
        CHECK(t.domain() == String("local"));
        CHECK(t.toString() == String());
        CHECK(t.toFqdn()   == String());
}

TEST_CASE("MdnsServiceType: explicit ctor") {
        MdnsServiceType t("http", Protocol::Tcp);
        CHECK(t.isValid());
        CHECK(t.app() == String("http"));
        CHECK(t.proto() == Protocol::Tcp);
        CHECK(t.domain() == String("local"));
        CHECK(t.toString() == String("_http._tcp.local"));
        CHECK(t.toFqdn() == String("_http._tcp.local."));
}

TEST_CASE("MdnsServiceType: explicit ctor strips trailing dot from domain") {
        MdnsServiceType t("http", Protocol::Tcp, "example.com.");
        CHECK(t.domain() == String("example.com"));
}

TEST_CASE("MdnsServiceType: explicit ctor restores default domain on empty input") {
        MdnsServiceType t("http", Protocol::Tcp, "");
        CHECK(t.domain() == String("local"));
}

TEST_CASE("MdnsServiceType: parse canonical form, no trailing dot, no domain") {
        auto r = MdnsServiceType::fromString("_http._tcp");
        REQUIRE(r.second().isOk());
        CHECK(r.first().app() == String("http"));
        CHECK(r.first().proto() == Protocol::Tcp);
        CHECK(r.first().domain() == String("local"));
}

TEST_CASE("MdnsServiceType: parse canonical form, explicit local domain, no trailing dot") {
        auto r = MdnsServiceType::fromString("_http._tcp.local");
        REQUIRE(r.second().isOk());
        CHECK(r.first().app() == String("http"));
        CHECK(r.first().proto() == Protocol::Tcp);
        CHECK(r.first().domain() == String("local"));
}

TEST_CASE("MdnsServiceType: parse canonical form, trailing dot") {
        auto r = MdnsServiceType::fromString("_http._tcp.local.");
        REQUIRE(r.second().isOk());
        CHECK(r.first().app() == String("http"));
        CHECK(r.first().proto() == Protocol::Tcp);
        CHECK(r.first().domain() == String("local"));
}

TEST_CASE("MdnsServiceType: parse UDP protocol") {
        auto r = MdnsServiceType::fromString("_ntp._udp.local.");
        REQUIRE(r.second().isOk());
        CHECK(r.first().proto() == Protocol::Udp);
        CHECK(r.first().toString() == String("_ntp._udp.local"));
}

TEST_CASE("MdnsServiceType: parse mixed-case protocol folds to lower-case canonical") {
        auto r = MdnsServiceType::fromString("_http._TCP.local.");
        REQUIRE(r.second().isOk());
        CHECK(r.first().proto() == Protocol::Tcp);
        CHECK(r.first().toString() == String("_http._tcp.local"));
}

TEST_CASE("MdnsServiceType: parse custom parent domain") {
        auto r = MdnsServiceType::fromString("_http._tcp.example.com.");
        // Three labels after stripping trailing dot is _http / _tcp /
        // example.com — parser splits on every dot so this gives 4
        // labels and fails.  Documenting the current strictness.
        CHECK_FALSE(r.second().isOk());
}

TEST_CASE("MdnsServiceType: rejects malformed input") {
        CHECK_FALSE(MdnsServiceType::fromString("").second().isOk());
        CHECK_FALSE(MdnsServiceType::fromString("http").second().isOk());
        CHECK_FALSE(MdnsServiceType::fromString("_http").second().isOk());
        CHECK_FALSE(MdnsServiceType::fromString("http._tcp").second().isOk());     // missing leading _
        CHECK_FALSE(MdnsServiceType::fromString("_http.tcp").second().isOk());     // missing proto _
        CHECK_FALSE(MdnsServiceType::fromString("_http._sctp").second().isOk());   // wrong proto
        CHECK_FALSE(MdnsServiceType::fromString("_._tcp").second().isOk());        // empty app
        CHECK_FALSE(MdnsServiceType::fromString("_-http._tcp").second().isOk());   // leading hyphen
        CHECK_FALSE(MdnsServiceType::fromString("_http-._tcp").second().isOk());   // trailing hyphen
        CHECK_FALSE(MdnsServiceType::fromString("_h--p._tcp").second().isOk());    // double hyphen
        CHECK_FALSE(MdnsServiceType::fromString("_http_._tcp").second().isOk());   // underscore in label
        CHECK_FALSE(MdnsServiceType::fromString("_aVeryLongAppName._tcp").second().isOk()); // 16 chars
}

TEST_CASE("MdnsServiceType: subtype browse FQDN") {
        MdnsServiceType t("http", Protocol::Tcp);
        CHECK(t.toSubtypeBrowseFqdn("printer") == String("_printer._sub._http._tcp.local."));
        // Empty subtype falls back to plain browse FQDN.
        CHECK(t.toSubtypeBrowseFqdn(String()) == t.toFqdn());
}

TEST_CASE("MdnsServiceType: protocol <-> string round-trip") {
        CHECK(MdnsServiceType::protocolToString(Protocol::Tcp)     == String("tcp"));
        CHECK(MdnsServiceType::protocolToString(Protocol::Udp)     == String("udp"));
        CHECK(MdnsServiceType::protocolToString(Protocol::Invalid) == String());
        CHECK(MdnsServiceType::protocolFromString("tcp") == Protocol::Tcp);
        CHECK(MdnsServiceType::protocolFromString("TCP") == Protocol::Tcp);
        CHECK(MdnsServiceType::protocolFromString("udp") == Protocol::Udp);
        CHECK(MdnsServiceType::protocolFromString("sctp") == Protocol::Invalid);
        CHECK(MdnsServiceType::protocolFromString("")    == Protocol::Invalid);
}

TEST_CASE("MdnsServiceType: equality is case-insensitive on labels") {
        auto a = MdnsServiceType::fromString("_HTTP._tcp.local").first();
        auto b = MdnsServiceType::fromString("_http._TCP.LOCAL").first();
        CHECK(a == b);
        CHECK_FALSE(a != b);
}

TEST_CASE("MdnsServiceType: ordering is total") {
        MdnsServiceType a("http",  Protocol::Tcp);
        MdnsServiceType b("https", Protocol::Tcp);
        MdnsServiceType c("http",  Protocol::Udp);
        CHECK(a < b);
        CHECK_FALSE(b < a);
        CHECK(a < c);                   // same app, Tcp(1) < Udp(2)
        CHECK_FALSE(c < a);
}

TEST_CASE("MdnsServiceType: Variant round-trip") {
        MdnsServiceType original("ravenna", Protocol::Tcp);
        Variant         v = original;
        CHECK(v.type() == DataTypeMdnsServiceType);

        MdnsServiceType retrieved = v.get<MdnsServiceType>();
        CHECK(retrieved == original);

        // String conversion path uses toString() via the registered
        // String <-> T converter.
        String s = v.get<String>();
        CHECK(s == original.toString());
}

TEST_CASE("MdnsServiceType: DataStream round-trip") {
        MdnsServiceType original("ravenna", Protocol::Tcp);

        Buffer          buf(4096);
        BufferIODevice  dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << original;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream      rs = DataStream::createReader(&dev);
                MdnsServiceType loaded;
                rs >> loaded;
                CHECK(rs.status() == DataStream::Ok);
                CHECK(loaded == original);
                CHECK(loaded.toString() == original.toString());
        }
}

TEST_CASE("MdnsServiceType: DataStream round-trip of invalid value") {
        MdnsServiceType original;

        Buffer          buf(1024);
        BufferIODevice  dev(&buf);
        dev.open(IODevice::ReadWrite);

        {
                DataStream ws = DataStream::createWriter(&dev);
                ws << original;
                CHECK(ws.status() == DataStream::Ok);
        }
        dev.seek(0);
        {
                DataStream      rs = DataStream::createReader(&dev);
                MdnsServiceType loaded("placeholder", Protocol::Udp); // sentinel
                rs >> loaded;
                CHECK(rs.status() == DataStream::Ok);
                CHECK_FALSE(loaded.isValid());
        }
}
