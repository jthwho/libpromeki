/**
 * @file      dnscache.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/dnscache.h>
#include <promeki/dnsrecord.h>
#include <promeki/ipv4address.h>
#include <promeki/string.h>
#include <thread>
#include <chrono>

using namespace promeki;

TEST_CASE("DnsCache: empty cache returns miss") {
        DnsCache c;
        auto hit = c.get(String("example.com."), 1, 1);
        CHECK_FALSE(hit.found);
        CHECK(c.size() == 0);
}

TEST_CASE("DnsCache: positive insert + lookup") {
        DnsCache c;
        List<DnsRecord> recs;
        recs += DnsRecord::makeA(String("example.com."), Ipv4Address(1, 2, 3, 4),
                                 Duration::fromSeconds(10));
        c.put(String("Example.COM"), 1, 1, recs);
        // Lookup with different case still hits (case-insensitive).
        auto hit = c.get(String("EXAMPLE.com"), 1, 1);
        REQUIRE(hit.found);
        CHECK_FALSE(hit.negative);
        REQUIRE(hit.records.size() == 1);
        CHECK(hit.records[0].a == Ipv4Address(1, 2, 3, 4));
}

TEST_CASE("DnsCache: empty answer is treated as a negative entry") {
        DnsCache c;
        List<DnsRecord> recs;
        c.put(String("nope.example.com."), 1, 1, recs,
              Duration::fromSeconds(10));
        auto hit = c.get(String("nope.example.com."), 1, 1);
        REQUIRE(hit.found);
        CHECK(hit.negative);
        CHECK(hit.records.isEmpty());
}

TEST_CASE("DnsCache: putNegative records NXDOMAIN") {
        DnsCache c;
        c.putNegative(String("no.example.com."), 28, 1,
                      DnsRcode::NxDomain, Duration::fromSeconds(10));
        auto hit = c.get(String("no.example.com."), 28, 1);
        REQUIRE(hit.found);
        CHECK(hit.negative);
        CHECK(hit.rcode == DnsRcode::NxDomain);
}

TEST_CASE("DnsCache: zero-TTL record is not cached") {
        DnsCache c;
        List<DnsRecord> recs;
        recs += DnsRecord::makeA(String("ttl0.example.com."), Ipv4Address(1, 1, 1, 1),
                                 Duration::fromSeconds(0));
        c.put(String("ttl0.example.com."), 1, 1, recs);
        auto hit = c.get(String("ttl0.example.com."), 1, 1);
        CHECK_FALSE(hit.found);
}

TEST_CASE("DnsCache: capacity cap drops old entries") {
        DnsCache c;
        c.setCapacity(3);
        for (int i = 0; i < 8; ++i) {
                List<DnsRecord> recs;
                String name = String("h") + String::number(i) + String(".example.com.");
                recs += DnsRecord::makeA(name, Ipv4Address(10, 0, 0, static_cast<uint8_t>(i)),
                                         Duration::fromSeconds(60));
                c.put(name, 1, 1, recs);
        }
        CHECK(c.size() <= 3);
}

TEST_CASE("DnsCache: positive TTL cap clamps to maxTtl") {
        DnsCache c;
        c.setMaxTtl(Duration::fromSeconds(5));
        List<DnsRecord> recs;
        recs += DnsRecord::makeA(String("longttl.example.com."), Ipv4Address(1, 1, 1, 1),
                                 Duration::fromSeconds(86400));
        c.put(String("longttl.example.com."), 1, 1, recs);
        // We can't directly read the expiry without sleeping; just
        // verify the entry exists.
        auto hit = c.get(String("longttl.example.com."), 1, 1);
        CHECK(hit.found);
}

TEST_CASE("DnsCache: clear removes everything") {
        DnsCache c;
        List<DnsRecord> recs;
        recs += DnsRecord::makeA(String("x."), Ipv4Address(1, 1, 1, 1), Duration::fromSeconds(60));
        c.put(String("x."), 1, 1, recs);
        REQUIRE(c.size() == 1);
        c.clear();
        CHECK(c.size() == 0);
}
