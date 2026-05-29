/**
 * @file      mdnstypebrowser.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/mdnsrecord.h>
#include <promeki/mdnstypebrowser.h>
#include <promeki/objectbase.tpp>
#include <promeki/socketaddress.h>

using namespace promeki;

namespace {

        SocketAddress dummySender() {
                return SocketAddress(Ipv4Address(192, 168, 1, 1), 5353);
        }

        // Builds a meta-browse response (PTR records owned by
        // "_services._dns-sd._udp.local." pointing at the listed
        // service type FQDNs) and returns the raw wire bytes.
        Buffer buildMetaResponse(const List<String> &typeFqdns,
                                 const Duration &ttl = Duration::fromSeconds(120)) {
                List<MdnsRecord> recs;
                for (const String &t : typeFqdns) {
                        recs += MdnsRecord::ptr("_services._dns-sd._udp.local.", t, ttl);
                }
                return mdnsBuildAnnounce(recs);
        }

} // namespace

TEST_CASE("MdnsTypeBrowser: default state is inactive with empty cache") {
        MdnsTypeBrowser tb;
        CHECK_FALSE(tb.isActive());
        CHECK(tb.types().isEmpty());
}

TEST_CASE("MdnsTypeBrowser: extracts the service-type list from a meta response") {
        MdnsTypeBrowser tb;

        std::atomic<int> foundCount{0};
        List<MdnsServiceType> seen;
        tb.typeFoundSignal.connect([&](MdnsServiceType t) {
                foundCount.fetch_add(1);
                seen += t;
        }, &tb);

        List<String> types;
        types += String("_http._tcp.local.");
        types += String("_ipp._tcp.local.");
        types += String("_ravenna._tcp.local.");

        tb.handlePacket(buildMetaResponse(types), dummySender(), NetworkInterface());

        CHECK(foundCount.load() == 3);
        REQUIRE(seen.size() == 3);
        CHECK(seen[0].app() == String("http"));
        CHECK(seen[1].app() == String("ipp"));
        CHECK(seen[2].app() == String("ravenna"));
}

TEST_CASE("MdnsTypeBrowser: a second observation of the same type does not "
          "re-emit typeFound") {
        MdnsTypeBrowser tb;
        std::atomic<int> foundCount{0};
        tb.typeFoundSignal.connect([&](MdnsServiceType) { foundCount.fetch_add(1); }, &tb);

        List<String> types;
        types += String("_http._tcp.local.");
        tb.handlePacket(buildMetaResponse(types), dummySender(), NetworkInterface());
        tb.handlePacket(buildMetaResponse(types), dummySender(), NetworkInterface());
        CHECK(foundCount.load() == 1);
}

TEST_CASE("MdnsTypeBrowser: Goodbye PTR removes the type and emits typeLost") {
        MdnsTypeBrowser tb;
        std::atomic<int> lostCount{0};
        tb.typeLostSignal.connect([&](MdnsServiceType) { lostCount.fetch_add(1); }, &tb);

        List<String> types;
        types += String("_http._tcp.local.");
        tb.handlePacket(buildMetaResponse(types), dummySender(), NetworkInterface());
        REQUIRE(tb.types().size() == 1);

        tb.handlePacket(buildMetaResponse(types, Duration::zero()),
                        dummySender(), NetworkInterface());
        CHECK(lostCount.load() == 1);
        CHECK(tb.types().isEmpty());
}

TEST_CASE("MdnsTypeBrowser: packets with no meta-browse PTR are ignored") {
        MdnsTypeBrowser tb;
        std::atomic<int> foundCount{0};
        tb.typeFoundSignal.connect([&](MdnsServiceType) { foundCount.fetch_add(1); }, &tb);

        // A regular PTR for a service type, not a meta-browse PTR —
        // owner is the service-type FQDN, not _services._dns-sd._udp.
        List<MdnsRecord> recs;
        recs += MdnsRecord::ptr("_http._tcp.local.",
                                "inst._http._tcp.local.",
                                Duration::fromSeconds(60));
        tb.handlePacket(mdnsBuildAnnounce(recs), dummySender(), NetworkInterface());
        CHECK(foundCount.load() == 0);
}

TEST_CASE("MdnsTypeBrowser: malformed PTR targets are skipped without crashing") {
        MdnsTypeBrowser tb;
        List<String> types;
        types += String("not-a-service-type");           // missing leading underscore
        types += String("_valid._tcp.local.");
        types += String("_app._invalidproto.local.");    // proto not tcp/udp
        tb.handlePacket(buildMetaResponse(types), dummySender(), NetworkInterface());
        REQUIRE(tb.types().size() == 1);
        CHECK(tb.types()[0].app() == String("valid"));
}

TEST_CASE("MdnsTypeBrowser: clearCache does not emit typeLost") {
        MdnsTypeBrowser tb;
        std::atomic<int> lostCount{0};
        tb.typeLostSignal.connect([&](MdnsServiceType) { lostCount.fetch_add(1); }, &tb);

        List<String> types;
        types += String("_http._tcp.local.");
        tb.handlePacket(buildMetaResponse(types), dummySender(), NetworkInterface());
        REQUIRE(tb.types().size() == 1);

        tb.clearCache();
        CHECK(tb.types().isEmpty());
        CHECK(lostCount.load() == 0);
}
