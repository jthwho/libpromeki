/**
 * @file      dnsname.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/dnsname.h>
#include <promeki/list.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("dnsEscapeLabel: passes through plain bytes") {
        CHECK(dnsEscapeLabel(String("plain")) == String("plain"));
        CHECK(dnsEscapeLabel(String("with space")) == String("with space"));
}

TEST_CASE("dnsEscapeLabel: escapes dots and backslashes") {
        CHECK(dnsEscapeLabel(String("a.b")) == String("a\\.b"));
        CHECK(dnsEscapeLabel(String("a\\b")) == String("a\\\\b"));
        CHECK(dnsEscapeLabel(String("...")) == String("\\.\\.\\."));
}

TEST_CASE("dnsUnescapeLabel: round-trips backslash escapes") {
        CHECK(dnsUnescapeLabel(String("a\\.b")) == String("a.b"));
        CHECK(dnsUnescapeLabel(String("a\\\\b")) == String("a\\b"));
}

TEST_CASE("dnsUnescapeLabel: three-digit decimal escape") {
        CHECK(dnsUnescapeLabel(String("\\009")) == String("\t"));
        CHECK(dnsUnescapeLabel(String("\\032")) == String(" "));
}

TEST_CASE("dnsSplitName / dnsJoinName: round-trip a service-instance FQDN") {
        const String fqdn(R"(Studio\.B Camera._http._tcp.local.)");
        List<String> labels = dnsSplitName(fqdn);
        REQUIRE(labels.size() == 4);
        CHECK(labels[0] == String("Studio.B Camera"));
        CHECK(labels[1] == String("_http"));
        CHECK(labels[2] == String("_tcp"));
        CHECK(labels[3] == String("local"));
        CHECK(dnsJoinName(labels) == fqdn);
}

TEST_CASE("dnsCanonicalName: lower-cases and root-terminates") {
        CHECK(dnsCanonicalName(String("Example.COM")) == String("example.com."));
        CHECK(dnsCanonicalName(String("example.com.")) == String("example.com."));
        CHECK(dnsCanonicalName(String("")) == String("."));
}

TEST_CASE("encodeName / decodeName: round-trips a simple name") {
        List<uint8_t> wire;
        REQUIRE(encodeName(String("example.com."), wire, nullptr).isOk());
        // Expected: 7 'example' 3 'com' 0
        REQUIRE(wire.size() == 13);
        CHECK(wire[0]  == 7);
        CHECK(wire[8]  == 3);
        CHECK(wire[12] == 0);

        auto r = decodeName(&wire[0], wire.size(), 0);
        REQUIRE(r.second().isOk());
        CHECK(r.first().name == String("example.com."));
        CHECK(r.first().nextOffset == wire.size());
}

TEST_CASE("encodeName: compresses repeated suffixes via the dictionary") {
        List<uint8_t>         wire;
        DnsNameCompressionMap dict;
        REQUIRE(encodeName(String("a.example.com."), wire, &dict).isOk());
        const size_t firstLen = wire.size();
        REQUIRE(encodeName(String("b.example.com."), wire, &dict).isOk());
        const size_t secondLen = wire.size() - firstLen;
        // Second name should be 1 + 1 + 2 = 4 bytes
        // (label-length + 'b' + 2-byte pointer to the existing suffix).
        CHECK(secondLen == 4);
        // The pointer's top two bits should be set.
        CHECK((wire[firstLen + 2] & 0xC0) == 0xC0);
}

TEST_CASE("decodeName: follows compression pointers") {
        // Build: "ns1.example.com." then "example.com." emitted as a
        // compression pointer back into the first name.
        List<uint8_t>         wire;
        DnsNameCompressionMap dict;
        REQUIRE(encodeName(String("ns1.example.com."), wire, &dict).isOk());
        const size_t firstEnd = wire.size();
        REQUIRE(encodeName(String("example.com."), wire, &dict).isOk());

        auto r = decodeName(&wire[0], wire.size(), firstEnd);
        REQUIRE(r.second().isOk());
        CHECK(r.first().name == String("example.com."));
        // The pointer encoding is 2 bytes so nextOffset is firstEnd + 2.
        CHECK(r.first().nextOffset == firstEnd + 2);
}

TEST_CASE("decodeName: rejects forward / self compression pointers") {
        // 0xC0 0x00 at offset 0 points to itself.
        uint8_t bytes[] = { 0xC0, 0x00 };
        auto r = decodeName(bytes, sizeof(bytes), 0);
        CHECK_FALSE(r.second().isOk());
}

TEST_CASE("decodeName: rejects a label longer than 63 bytes") {
        // 0x40 = 64 is reserved (top bits 01).
        uint8_t bytes[] = { 0x40, 0 };
        auto r = decodeName(bytes, sizeof(bytes), 0);
        CHECK_FALSE(r.second().isOk());
}

TEST_CASE("decodeName: handles the root name (single zero)") {
        uint8_t bytes[] = { 0x00 };
        auto r = decodeName(bytes, sizeof(bytes), 0);
        REQUIRE(r.second().isOk());
        CHECK(r.first().name == String("."));
        CHECK(r.first().nextOffset == 1);
}

TEST_CASE("skipName: advances the cursor past the name") {
        List<uint8_t> wire;
        REQUIRE(encodeName(String("foo.bar."), wire, nullptr).isOk());
        size_t cursor = 0;
        Error  e      = skipName(&wire[0], wire.size(), cursor);
        REQUIRE(e.isOk());
        CHECK(cursor == wire.size());
}
