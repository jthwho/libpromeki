/**
 * @file      mdnsrecord.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/mdnspacket.h>
#include <promeki/mdnsrecord.h>

using namespace promeki;

namespace {

        // Big-endian 16-bit read from a raw byte view.
        uint16_t rdU16(const uint8_t *p) {
                return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        }

} // namespace

TEST_CASE("MdnsRecord: ptr factory has cache-flush off (RFC 6762 §10.2)") {
        MdnsRecord r = MdnsRecord::ptr("_http._tcp.local.",
                                       "Studio Camera._http._tcp.local.",
                                       Duration::fromSeconds(120));
        CHECK(r.type() == MdnsRecord::Type::Ptr);
        CHECK(r.cacheFlush() == false);
        CHECK(r.name() == String("_http._tcp.local."));
        CHECK(r.ptrTarget() == String("Studio Camera._http._tcp.local."));
        CHECK(r.ttl() == Duration::fromSeconds(120));
}

TEST_CASE("MdnsRecord: srv factory has cache-flush on by default") {
        MdnsRecord r = MdnsRecord::srv("Studio Camera._http._tcp.local.",
                                       "camera.local.", 8080, 10, 20,
                                       Duration::fromSeconds(120));
        CHECK(r.type() == MdnsRecord::Type::Srv);
        CHECK(r.cacheFlush() == true);
        CHECK(r.srvPort() == 8080);
        CHECK(r.srvPriority() == 10);
        CHECK(r.srvWeight() == 20);
        CHECK(r.srvTarget() == String("camera.local."));
}

TEST_CASE("MdnsRecord: txt factory carries the payload") {
        MdnsTxtRecord t;
        t.set("path", "/admin");
        MdnsRecord r = MdnsRecord::txt("inst._http._tcp.local.", t,
                                       Duration::fromSeconds(60));
        CHECK(r.type() == MdnsRecord::Type::Txt);
        CHECK(r.cacheFlush() == true);
        CHECK(r.txtRecord().value("path") == String("/admin"));
}

TEST_CASE("MdnsRecord: a / aaaa factories") {
        MdnsRecord ra = MdnsRecord::a("camera.local.", Ipv4Address(192, 168, 1, 7));
        CHECK(ra.type() == MdnsRecord::Type::A);
        CHECK(ra.aAddress() == Ipv4Address(192, 168, 1, 7));

        uint8_t v6[16] = {0xfe, 0x80, 0,0,0,0,0,0, 0,0,0,0,0,0,0,0x01};
        MdnsRecord rb = MdnsRecord::aaaa("camera.local.", Ipv6Address(v6));
        CHECK(rb.type() == MdnsRecord::Type::Aaaa);
        CHECK(rb.aaaaAddress() == Ipv6Address(v6));
}

TEST_CASE("MdnsRecord: equality compares every field") {
        MdnsRecord a = MdnsRecord::srv("inst._x._tcp.local.", "host.local.", 80);
        MdnsRecord b = MdnsRecord::srv("inst._x._tcp.local.", "host.local.", 80);
        CHECK(a == b);
        b.setCacheFlush(false);
        CHECK(a != b);
}

TEST_CASE("MdnsRecord: isGoodbye flags zero-TTL records") {
        MdnsRecord r = MdnsRecord::ptr("_x._tcp.local.", "inst._x._tcp.local.", Duration::zero());
        CHECK(r.isGoodbye());
        MdnsRecord live = MdnsRecord::ptr("_x._tcp.local.", "inst._x._tcp.local.",
                                          Duration::fromSeconds(120));
        CHECK_FALSE(live.isGoodbye());
}

TEST_CASE("mdnsBuildAnnounce: header carries QR=1 AA=1, qdcount=0, ancount matches records") {
        List<MdnsRecord> recs;
        recs += MdnsRecord::ptr("_http._tcp.local.",
                                "Studio Camera._http._tcp.local.",
                                Duration::fromSeconds(120));
        recs += MdnsRecord::srv("Studio Camera._http._tcp.local.", "host.local.", 8080,
                                0, 0, Duration::fromSeconds(120));
        Buffer pkt = mdnsBuildAnnounce(recs);
        REQUIRE(pkt.size() >= 12);
        const uint8_t *p = static_cast<const uint8_t *>(pkt.data());
        CHECK(rdU16(p + 2) == 0x8400);
        CHECK(rdU16(p + 4) == 0);
        CHECK(rdU16(p + 6) == 2);
        CHECK(rdU16(p + 8) == 0);
        CHECK(rdU16(p + 10) == 0);
}

TEST_CASE("mdnsBuildAnnounce: PTR cache-flush bit is masked off per RFC 6762 §10.2") {
        List<MdnsRecord> recs;
        MdnsRecord ptr = MdnsRecord::ptr("_http._tcp.local.",
                                         "Studio Camera._http._tcp.local.",
                                         Duration::fromSeconds(120));
        ptr.setCacheFlush(true);    // try to force it on — encoder must suppress
        recs += ptr;
        Buffer pkt = mdnsBuildAnnounce(recs);
        auto r = MdnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        CHECK_FALSE(r.first().records()[0].cacheFlush);
}

TEST_CASE("mdnsBuildAnnounce: encoded SRV round-trips through MdnsPacket::parse") {
        List<MdnsRecord> recs;
        recs += MdnsRecord::srv("inst._http._tcp.local.", "host.local.",
                                8080, 10, 20, Duration::fromSeconds(120));
        Buffer pkt = mdnsBuildAnnounce(recs);
        auto r = MdnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        const MdnsParsedRecord &got = r.first().records()[0];
        CHECK(got.type == MdnsParsedRecord::Type::Srv);
        CHECK(got.srvPort == 8080);
        CHECK(got.srvPriority == 10);
        CHECK(got.srvWeight == 20);
        CHECK(got.srvTarget == String("host.local."));
        CHECK(got.cacheFlush == true);
}

TEST_CASE("mdnsBuildAnnounce: encoded TXT round-trips") {
        MdnsTxtRecord t;
        t.set("path", "/admin");
        t.setKey("tls");
        List<MdnsRecord> recs;
        recs += MdnsRecord::txt("inst._http._tcp.local.", t, Duration::fromSeconds(60));
        Buffer pkt = mdnsBuildAnnounce(recs);
        auto r = MdnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        const MdnsTxtRecord &back = r.first().records()[0].txt;
        CHECK(back.value("path") == String("/admin"));
        CHECK(back.presence("tls") == MdnsTxtRecord::Presence::KeyOnly);
}

TEST_CASE("mdnsBuildAnnounce: encoded A and AAAA round-trip") {
        List<MdnsRecord> recs;
        recs += MdnsRecord::a("host.local.", Ipv4Address(192, 168, 1, 7),
                              Duration::fromSeconds(60));
        uint8_t v6[16] = {0xfe, 0x80, 0,0,0,0,0,0, 0,0,0,0,0,0,0,0x01};
        recs += MdnsRecord::aaaa("host.local.", Ipv6Address(v6),
                                 Duration::fromSeconds(60));
        Buffer pkt = mdnsBuildAnnounce(recs);
        auto r = MdnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 2);
        CHECK(r.first().records()[0].a == Ipv4Address(192, 168, 1, 7));
        CHECK(r.first().records()[1].aaaa == Ipv6Address(v6));
}

TEST_CASE("mdnsBuildGoodbye: every record has TTL=0") {
        List<MdnsRecord> recs;
        recs += MdnsRecord::ptr("_http._tcp.local.", "inst._http._tcp.local.",
                                Duration::fromSeconds(120));
        recs += MdnsRecord::srv("inst._http._tcp.local.", "host.local.", 80, 0, 0,
                                Duration::fromSeconds(120));
        Buffer pkt = mdnsBuildGoodbye(recs);
        auto r = MdnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 2);
        CHECK(r.first().records()[0].ttl == Duration::zero());
        CHECK(r.first().records()[1].ttl == Duration::zero());
}

TEST_CASE("mdnsBuildProbe: questions in qdcount, tentative records in nscount") {
        List<MdnsRecord> recs;
        recs += MdnsRecord::srv("inst._http._tcp.local.", "host.local.", 8080,
                                0, 0, Duration::fromSeconds(120));
        recs += MdnsRecord::a("host.local.", Ipv4Address(192, 168, 1, 7),
                              Duration::fromSeconds(120));
        Buffer pkt = mdnsBuildProbe(recs);
        REQUIRE(pkt.size() >= 12);
        const uint8_t *p = static_cast<const uint8_t *>(pkt.data());
        // Two distinct owner names → two questions.  Two tentative
        // records → nscount = 2.
        CHECK(rdU16(p + 2) == 0x0000);   // standard query
        CHECK(rdU16(p + 4) == 2);
        CHECK(rdU16(p + 6) == 0);
        CHECK(rdU16(p + 8) == 2);
}
