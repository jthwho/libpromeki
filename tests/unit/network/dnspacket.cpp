/**
 * @file      dnspacket.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <doctest/doctest.h>
#include <promeki/dnspacket.h>
#include <promeki/dnsrecord.h>
#include <promeki/ipv4address.h>
#include <promeki/ipv6address.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("DnsPacket::Builder: builds a minimal A query") {
        DnsPacket::Builder b;
        b.setTransactionId(0x4242)
         .setRecursionDesired(true)
         .addQuestion(String("example.com."), DnsRecord::Type::A);
        Buffer pkt = b.finish();
        REQUIRE(pkt.size() > 0);
        const uint8_t *p = static_cast<const uint8_t *>(pkt.data());

        // Header: id, flags, qd, an, ns, ar.
        CHECK(p[0] == 0x42);
        CHECK(p[1] == 0x42);
        CHECK((p[2] & 0x01) == 0x01);   // RD bit
        CHECK(p[4] == 0x00);            // QDCOUNT hi
        CHECK(p[5] == 0x01);            // QDCOUNT lo

        // Parse it back and verify the question.
        auto r = DnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        CHECK(r.first().transactionId() == 0x4242);
        CHECK(r.first().isRecursionDesired());
        REQUIRE(r.first().questions().size() == 1);
        CHECK(r.first().questions()[0].name == String("example.com."));
        CHECK(r.first().questions()[0].type ==
              static_cast<uint16_t>(DnsRecord::Type::A));
}

TEST_CASE("DnsPacket: round-trips an A answer") {
        DnsRecord rec = DnsRecord::makeA(String("example.com."),
                                         Ipv4Address(93, 184, 216, 34),
                                         Duration::fromSeconds(300));
        DnsPacket::Builder b;
        b.setResponse(true).setRecursionAvailable(true);
        b.addAnswer(rec);
        Buffer pkt = b.finish();
        REQUIRE(pkt.size() > 0);
        auto r = DnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        const DnsRecord &got = r.first().records()[0];
        CHECK(got.type == DnsRecord::Type::A);
        CHECK(got.a    == Ipv4Address(93, 184, 216, 34));
        CHECK(got.ttl  == Duration::fromSeconds(300));
}

TEST_CASE("DnsPacket: round-trips an AAAA answer") {
        const uint8_t v6[16] = {
                0x20, 0x01, 0x4, 0x86, 0x4, 0x86, 0x4, 0x86,
                0x0,  0x0,  0x0, 0x0,  0x0, 0x0,  0x0, 0x36
        };
        DnsRecord rec = DnsRecord::makeAaaa(String("example.com."),
                                            Ipv6Address(v6),
                                            Duration::fromSeconds(600));
        DnsPacket::Builder b;
        b.setResponse(true).addAnswer(rec);
        Buffer pkt = b.finish();
        auto r = DnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 1);
        CHECK(r.first().records()[0].type == DnsRecord::Type::Aaaa);
        CHECK(r.first().records()[0].aaaa == Ipv6Address(v6));
}

TEST_CASE("DnsPacket: round-trips a CNAME chain") {
        DnsRecord cn = DnsRecord::makeCname(String("www.example.com."),
                                            String("example.com."),
                                            Duration::fromSeconds(600));
        DnsRecord a  = DnsRecord::makeA(String("example.com."),
                                        Ipv4Address(1, 2, 3, 4),
                                        Duration::fromSeconds(300));
        DnsPacket::Builder b;
        b.setResponse(true).addAnswer(cn).addAnswer(a);
        Buffer pkt = b.finish();
        auto r = DnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 2);
        CHECK(r.first().records()[0].type        == DnsRecord::Type::Cname);
        CHECK(r.first().records()[0].cnameTarget == String("example.com."));
        CHECK(r.first().records()[1].type        == DnsRecord::Type::A);
}

TEST_CASE("DnsPacket: round-trips an SRV record") {
        DnsRecord r = DnsRecord::makeSrv(String("_http._tcp.example.com."),
                                         String("host.example.com."),
                                         8080, 10, 20);
        DnsPacket::Builder b;
        b.setResponse(true).addAnswer(r);
        Buffer pkt = b.finish();
        auto pr = DnsPacket::parse(pkt);
        REQUIRE(pr.second().isOk());
        REQUIRE(pr.first().records().size() == 1);
        const DnsRecord &got = pr.first().records()[0];
        CHECK(got.type        == DnsRecord::Type::Srv);
        CHECK(got.srvPriority == 10);
        CHECK(got.srvWeight   == 20);
        CHECK(got.srvPort     == 8080);
        CHECK(got.srvTarget   == String("host.example.com."));
}

TEST_CASE("DnsPacket: round-trips an MX record") {
        DnsRecord r = DnsRecord::makeMx(String("example.com."),
                                        String("mail.example.com."), 10);
        DnsPacket::Builder b;
        b.setResponse(true).addAnswer(r);
        Buffer pkt = b.finish();
        auto pr = DnsPacket::parse(pkt);
        REQUIRE(pr.second().isOk());
        const DnsRecord &got = pr.first().records()[0];
        CHECK(got.type         == DnsRecord::Type::Mx);
        CHECK(got.mxPreference == 10);
        CHECK(got.mxExchange   == String("mail.example.com."));
}

TEST_CASE("DnsPacket: round-trips an NS record") {
        DnsRecord r = DnsRecord::makeNs(String("example.com."),
                                        String("ns1.example.com."));
        DnsPacket::Builder b;
        b.setResponse(true).addAnswer(r);
        Buffer pkt = b.finish();
        auto pr = DnsPacket::parse(pkt);
        REQUIRE(pr.second().isOk());
        const DnsRecord &got = pr.first().records()[0];
        CHECK(got.type     == DnsRecord::Type::Ns);
        CHECK(got.nsTarget == String("ns1.example.com."));
}

TEST_CASE("DnsPacket: SOA records survive the round-trip") {
        DnsRecord r;
        r.type       = DnsRecord::Type::Soa;
        r.name       = String("example.com.");
        r.ttl        = Duration::fromSeconds(3600);
        r.soaMname   = String("ns1.example.com.");
        r.soaRname   = String("hostmaster.example.com.");
        r.soaSerial  = 2026053000;
        r.soaRefresh = 3600;
        r.soaRetry   = 600;
        r.soaExpire  = 1209600;
        r.soaMinimum = 300;
        DnsPacket::Builder b;
        b.setResponse(true).addAnswer(r);
        Buffer pkt = b.finish();
        auto pr = DnsPacket::parse(pkt);
        REQUIRE(pr.second().isOk());
        const DnsRecord &got = pr.first().records()[0];
        CHECK(got.type       == DnsRecord::Type::Soa);
        CHECK(got.soaMname   == String("ns1.example.com."));
        CHECK(got.soaRname   == String("hostmaster.example.com."));
        CHECK(got.soaSerial  == 2026053000U);
        CHECK(got.soaMinimum == 300U);
}

TEST_CASE("DnsPacket: CAA round-trip") {
        DnsRecord r;
        r.type     = DnsRecord::Type::Caa;
        r.name     = String("example.com.");
        r.ttl      = Duration::fromSeconds(3600);
        r.caaFlags = 0;
        r.caaTag   = String("issue");
        r.caaValue = String("letsencrypt.org");
        DnsPacket::Builder b;
        b.setResponse(true).addAnswer(r);
        Buffer pkt = b.finish();
        auto pr = DnsPacket::parse(pkt);
        REQUIRE(pr.second().isOk());
        const DnsRecord &got = pr.first().records()[0];
        CHECK(got.type     == DnsRecord::Type::Caa);
        CHECK(got.caaTag   == String("issue"));
        CHECK(got.caaValue == String("letsencrypt.org"));
}

TEST_CASE("DnsPacket: rejects truncated input") {
        uint8_t bytes[6] = { 0 };
        auto r = DnsPacket::parse(bytes, sizeof(bytes));
        CHECK_FALSE(r.second().isOk());
}

TEST_CASE("DnsPacket: parses RCODE / TC / RA flags") {
        DnsPacket::Builder b;
        b.setResponse(true).setTruncated(true).setRecursionAvailable(true).setRcode(3);
        Buffer pkt = b.finish();
        auto r = DnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        CHECK(r.first().isResponse());
        CHECK(r.first().isTruncated());
        CHECK(r.first().isRecursionAvailable());
        CHECK(r.first().rcode() == 3);
}

TEST_CASE("DnsPacket: multi-section packet survives parsing") {
        DnsPacket::Builder b;
        b.setResponse(true)
         .addQuestion(String("example.com."), DnsRecord::Type::A)
         .addAnswer(DnsRecord::makeA(String("example.com."), Ipv4Address(1, 2, 3, 4)))
         .addAuthority(DnsRecord::makeNs(String("example.com."), String("ns1.example.com.")))
         .addAdditional(DnsRecord::makeA(String("ns1.example.com."), Ipv4Address(5, 6, 7, 8)));
        Buffer pkt = b.finish();
        auto pr = DnsPacket::parse(pkt);
        REQUIRE(pr.second().isOk());
        REQUIRE(pr.first().records().size() == 3);
        CHECK(pr.first().records()[0].section == DnsRecord::Section::Answer);
        CHECK(pr.first().records()[1].section == DnsRecord::Section::Authority);
        CHECK(pr.first().records()[2].section == DnsRecord::Section::Additional);
        CHECK(pr.first().recordsInSection(DnsRecord::Section::Answer).size() == 1);
}

TEST_CASE("DnsPacket::Builder::addEdns0: surfaces OPT pseudo-record in the wire") {
        DnsPacket::Builder b;
        b.setRecursionDesired(true)
         .addQuestion(String("example.com."), DnsRecord::Type::A)
         .addEdns0(1232, false);
        Buffer pkt = b.finish();
        REQUIRE(pkt.size() > 0);

        auto r = DnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        // EDNS0 is an Additional-section pseudo-record; surface it by
        // walking the records list and looking for type Opt.
        bool sawOpt = false;
        uint16_t udpSize = 0;
        for (const DnsRecord &rec : r.first().records()) {
                if (rec.type == DnsRecord::Type::Opt) {
                        sawOpt  = true;
                        udpSize = rec.klass;   // OPT overloads CLASS as the requestor's UDP size
                        CHECK(rec.section == DnsRecord::Section::Additional);
                        break;
                }
        }
        CHECK(sawOpt);
        CHECK(udpSize == 1232);
}
