/**
 * @file      mdnsmanager.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/basicthread.h>
#include <promeki/buffer.h>
#include <promeki/mdnsmanager.h>
#include <promeki/mdnspacket.h>
#include <promeki/mutex.h>
#include <promeki/networkinterface.h>
#include <promeki/socketaddress.h>
#include <promeki/udpsocket.h>

using namespace promeki;

namespace {

        // Picks a free UDP port by binding then closing a throwaway
        // socket.  Mirrors the helper used by the MulticastReceiver
        // tests — the returned port may still race against another
        // process, but the engine binds with SO_REUSEADDR so a
        // collision degrades to a silent share rather than a fail.
        uint16_t pickFreePort() {
                UdpSocket sock;
                sock.open(IODevice::ReadWrite);
                sock.bind(SocketAddress::any(0));
                uint16_t port = sock.localAddress().port();
                sock.close();
                return port;
        }

        // Returns a single interface suitable for MdnsManager::start
        // in a unit-test environment.  We try loopback first (the
        // path the receive-plumbing test exercises); if that
        // interface is not present or rejects the multicast join,
        // fall back to the first up interface.  Linux's `lo` does
        // not report @c IFF_MULTICAST but the kernel still accepts
        // @c IP_ADD_MEMBERSHIP on it for testing, so this almost
        // always lands on loopback in CI.
        NetworkInterface::List firstUsableInterface() {
                NetworkInterface::List out;
                NetworkInterface       fallback;
                for (const NetworkInterface &i : NetworkInterface::enumerate()) {
                        if (!i.isValid() || !i.isUp()) continue;
                        if (i.isLoopback()) { out += i; return out; }
                        if (!fallback.isValid()) fallback = i;
                }
                if (fallback.isValid()) out += fallback;
                return out;
        }

} // namespace

TEST_CASE("MdnsManager: defaults") {
        MdnsManager m;
        CHECK_FALSE(m.isActive());
        CHECK(m.port() == MdnsManager::DefaultPort);
        CHECK(m.includeLoopback());
        CHECK(m.maxPacketSize() == MdnsManager::DefaultMaxPacketSize);
        CHECK(m.joinedInterfaces().isEmpty());
        CHECK(m.datagramCount() == 0);
        CHECK(m.byteCount() == 0);
        CHECK(m.socket() == nullptr);
}

TEST_CASE("MdnsManager: ipv4Group is the well-known mDNS address") {
        Ipv4Address g = MdnsManager::ipv4Group();
        CHECK(g == Ipv4Address(224, 0, 0, 251));
}

TEST_CASE("MdnsManager: setters round-trip") {
        MdnsManager m;
        m.setPort(5358);
        m.setIncludeLoopback(false);
        m.setMaxPacketSize(2048);
        CHECK(m.port() == 5358);
        CHECK_FALSE(m.includeLoopback());
        CHECK(m.maxPacketSize() == 2048);
}

TEST_CASE("MdnsManager: setMaxPacketSize(0) restores default") {
        MdnsManager m;
        m.setMaxPacketSize(0);
        CHECK(m.maxPacketSize() == MdnsManager::DefaultMaxPacketSize);
}

TEST_CASE("MdnsManager: start with empty interface list fails") {
        MdnsManager            m;
        NetworkInterface::List empty;
        Error                  err = m.start(empty);
        CHECK(err.isError());
        CHECK_FALSE(m.isActive());
}

TEST_CASE("MdnsManager: start on loopback then stop") {
        NetworkInterface::List ifaces = firstUsableInterface();
        REQUIRE_FALSE(ifaces.isEmpty());

        MdnsManager m;
        m.setPort(pickFreePort());

        Error err = m.start(ifaces);
        REQUIRE(err.isOk());
        CHECK(m.isActive());
        CHECK(m.socket() != nullptr);

        NetworkInterface::List joined = m.joinedInterfaces();
        REQUIRE(joined.size() == 1);
        CHECK(joined[0].name() == ifaces[0].name());

        m.stop();
        CHECK_FALSE(m.isActive());
        CHECK(m.joinedInterfaces().isEmpty());
        CHECK(m.socket() == nullptr);
}

TEST_CASE("MdnsManager: stop is idempotent") {
        NetworkInterface::List ifaces = firstUsableInterface();
        REQUIRE_FALSE(ifaces.isEmpty());

        MdnsManager m;
        m.setPort(pickFreePort());
        REQUIRE(m.start(ifaces).isOk());

        m.stop();
        m.stop(); // second call no-ops
        CHECK_FALSE(m.isActive());
}

TEST_CASE("MdnsManager: restart after stop") {
        NetworkInterface::List ifaces = firstUsableInterface();
        REQUIRE_FALSE(ifaces.isEmpty());

        MdnsManager m;
        m.setPort(pickFreePort());

        REQUIRE(m.start(ifaces).isOk());
        m.stop();
        REQUIRE(m.start(ifaces).isOk());
        CHECK(m.isActive());
        m.stop();
}

TEST_CASE("MdnsManager: receive plumbing — unicast datagram round-trip") {
        NetworkInterface::List ifaces = firstUsableInterface();
        REQUIRE_FALSE(ifaces.isEmpty());

        const uint16_t port = pickFreePort();

        MdnsManager m;
        m.setPort(port);

        std::atomic<int>    count{0};
        std::atomic<size_t> lastSize{0};
        uint8_t             lastFirstByte = 0;
        std::atomic<bool>   sawData{false};

        m.setPacketHook([&](NetworkInterface, SocketAddress, Buffer data) {
                lastSize.store(data.size());
                if (data.size() > 0) {
                        lastFirstByte = static_cast<const uint8_t *>(data.data())[0];
                }
                sawData.store(true);
                count.fetch_add(1);
        });
        REQUIRE(m.start(ifaces).isOk());

        // Send a unicast datagram to ourselves.  Hitting the wildcard
        // bind via 127.0.0.1 exercises the engine's bind + recv loop
        // without depending on multicast loopback routing decisions
        // (which differ across Linux distros, macOS, and Windows).
        UdpSocket tx;
        tx.open(IODevice::ReadWrite);
        const uint8_t payload[] = {0xAB, 0xCD, 0xEF, 0x01, 0x02, 0x03};
        const SocketAddress target(Ipv4Address::loopback(), port);
        int64_t             sent = tx.writeDatagram(payload, sizeof(payload), target);
        CHECK(sent == static_cast<int64_t>(sizeof(payload)));

        for (int i = 0; i < 50 && !sawData.load(); i++) {
                BasicThread::sleepMs(10);
        }
        m.stop();

        CHECK(sawData.load());
        CHECK(count.load() >= 1);
        CHECK(lastSize.load() == sizeof(payload));
        CHECK(lastFirstByte == 0xAB);
        CHECK(m.datagramCount() >= 1);
        CHECK(m.byteCount() >= sizeof(payload));
}

TEST_CASE("MdnsManager: buildQuery encodes header + question correctly") {
        Buffer pkt = MdnsManager::buildQuery("_http._tcp.local.", /*PTR*/ 12, /*txid*/ 0x1234);

        auto r = MdnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        const MdnsPacket &p = r.first();
        CHECK(p.transactionId() == 0x1234);
        CHECK_FALSE(p.isResponse());
        REQUIRE(p.questions().size() == 1);
        const MdnsParsedQuestion &q = p.questions()[0];
        CHECK(q.name == String("_http._tcp.local."));
        CHECK(q.type == 12);
        CHECK_FALSE(q.unicastResponse);   // class IN with QU bit clear
}

TEST_CASE("MdnsManager: buildQuery accepts names with or without trailing dot") {
        Buffer noDot   = MdnsManager::buildQuery("_http._tcp.local",  12);
        Buffer withDot = MdnsManager::buildQuery("_http._tcp.local.", 12);
        REQUIRE(noDot.size()   == withDot.size());
        CHECK(std::memcmp(noDot.data(), withDot.data(), noDot.size()) == 0);
}

TEST_CASE("MdnsManager: buildQuery with QU bit sets the top bit of qclass") {
        Buffer pkt = MdnsManager::buildQuery("_http._tcp.local.", 12, 0, /*unicast*/ true);
        REQUIRE(pkt.size() >= 12);
        // Walk past the 12-byte header + the qname length-prefixed
        // labels to find the qtype/qclass pair at the end.
        const uint8_t *p = static_cast<const uint8_t *>(pkt.data());
        size_t pos = 12;
        while (pos < pkt.size() && p[pos] != 0) {
                pos += 1 + p[pos];
        }
        ++pos;                            // past the zero-length root label
        REQUIRE(pos + 4 <= pkt.size());   // qtype + qclass left
        const uint16_t qclass = static_cast<uint16_t>((p[pos + 2] << 8) | p[pos + 3]);
        CHECK((qclass & 0x8000) != 0);    // QU bit set
        CHECK((qclass & 0x7FFF) == 1);    // IN
}

TEST_CASE("MdnsManager: buildQuery without QU bit clears the top bit") {
        Buffer pkt = MdnsManager::buildQuery("_http._tcp.local.", 12, 0, /*unicast*/ false);
        const uint8_t *p = static_cast<const uint8_t *>(pkt.data());
        size_t pos = 12;
        while (pos < pkt.size() && p[pos] != 0) pos += 1 + p[pos];
        ++pos;
        const uint16_t qclass = static_cast<uint16_t>((p[pos + 2] << 8) | p[pos + 3]);
        CHECK((qclass & 0x8000) == 0);
}

TEST_CASE("MdnsManager: buildQueryWithKnownAnswers stamps ancount and "
          "encodes the Answer section (RFC 6762 §7.1)") {
        List<MdnsRecord> known;
        known += MdnsRecord::ptr("_http._tcp.local.",
                                 "inst._http._tcp.local.",
                                 Duration::fromSeconds(60));
        known += MdnsRecord::ptr("_http._tcp.local.",
                                 "other._http._tcp.local.",
                                 Duration::fromSeconds(60));
        Buffer pkt = MdnsManager::buildQueryWithKnownAnswers(
                "_http._tcp.local.", 12, known, /*txId*/ 7);
        REQUIRE(pkt.size() > 12);
        const uint8_t *p = static_cast<const uint8_t *>(pkt.data());
        const uint16_t qdcount = static_cast<uint16_t>((p[4] << 8) | p[5]);
        const uint16_t ancount = static_cast<uint16_t>((p[6] << 8) | p[7]);
        const uint16_t nscount = static_cast<uint16_t>((p[8] << 8) | p[9]);
        CHECK(qdcount == 1);
        CHECK(ancount == 2);   // known answers belong here per §7.1
        CHECK(nscount == 0);

        // The known-answer records should round-trip through the
        // parser intact — proves the splice from mdnsBuildAnnounce
        // landed at the right offset.
        auto r = MdnsPacket::parse(pkt);
        REQUIRE(r.second().isOk());
        REQUIRE(r.first().records().size() == 2);
        CHECK(r.first().records()[0].type == MdnsParsedRecord::Type::Ptr);
        CHECK(r.first().records()[0].section == MdnsParsedRecord::Section::Answer);
        CHECK(r.first().records()[0].ptrTarget == String("inst._http._tcp.local."));
        CHECK(r.first().records()[1].ptrTarget == String("other._http._tcp.local."));
}

TEST_CASE("MdnsManager: sendQuery before start returns NotReady") {
        MdnsManager m;
        Error err = m.sendQuery("_http._tcp.local.", 12);
        CHECK(err == Error::NotReady);
}

TEST_CASE("MdnsManager: tick interval setter clamps zero to default") {
        MdnsManager m;
        m.setTickInterval(0);
        CHECK(m.tickInterval() == MdnsManager::DefaultTickIntervalMs);
        m.setTickInterval(20);
        CHECK(m.tickInterval() == 20);
}

TEST_CASE("MdnsManager: metaBrowseFqdn is the RFC 6763 §9 well-known name") {
        CHECK(MdnsManager::metaBrowseFqdn() == String("_services._dns-sd._udp.local."));
}

TEST_CASE("MdnsManager: sendMetaQuery before start returns NotReady") {
        MdnsManager m;
        CHECK(m.sendMetaQuery() == Error::NotReady);
}

TEST_CASE("MdnsManager: ipv6Group is the well-known link-local mDNS address") {
        // ff02::fb — the RFC 6762 §3 link-local IPv6 group.
        Ipv6Address g = MdnsManager::ipv6Group();
        const uint8_t expected[16] = {
                0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfb,
        };
        for (int i = 0; i < 16; ++i) {
                CHECK(static_cast<int>(g.raw()[i]) == static_cast<int>(expected[i]));
        }
}

TEST_CASE("MdnsManager: defaults to dual-family") {
        MdnsManager m;
        CHECK(m.ipFamily() == MdnsManager::IpFamily::Both);
}

TEST_CASE("MdnsManager: setIpFamily round-trips") {
        MdnsManager m;
        m.setIpFamily(MdnsManager::IpFamily::IPv4Only);
        CHECK(m.ipFamily() == MdnsManager::IpFamily::IPv4Only);
        m.setIpFamily(MdnsManager::IpFamily::IPv6Only);
        CHECK(m.ipFamily() == MdnsManager::IpFamily::IPv6Only);
        m.setIpFamily(MdnsManager::IpFamily::Both);
        CHECK(m.ipFamily() == MdnsManager::IpFamily::Both);
}

TEST_CASE("MdnsManager: IPv4-only start does not open the IPv6 socket") {
        NetworkInterface::List ifaces = firstUsableInterface();
        REQUIRE_FALSE(ifaces.isEmpty());

        MdnsManager m;
        m.setIpFamily(MdnsManager::IpFamily::IPv4Only);
        m.setAutoTrackInterfaces(false);   // keep the test single-shot
        m.setPort(pickFreePort());

        REQUIRE(m.start(ifaces).isOk());
        CHECK(m.socket() != nullptr);
        CHECK(m.socketV6() == nullptr);
        m.stop();
}

TEST_CASE("MdnsManager: dual-family start opens both sockets") {
        // Skipped quietly on hosts without IPv6 support — the bind
        // to @c [::]:port fails and the engine logs a warning, but
        // start() still succeeds in IPv4-only fallback.  We assert
        // only that the v4 path is up; if v6 is available we
        // additionally check it.
        NetworkInterface::List ifaces = firstUsableInterface();
        REQUIRE_FALSE(ifaces.isEmpty());

        MdnsManager m;
        m.setAutoTrackInterfaces(false);
        m.setPort(pickFreePort());

        Error err = m.start(ifaces);
        REQUIRE(err.isOk());
        CHECK(m.socket() != nullptr);
        // Don't require v6 — environments without it land in the
        // graceful fallback branch.
        if (m.socketV6() != nullptr) {
                MESSAGE("IPv6 socket opened");
        }
        m.stop();
}

TEST_CASE("MdnsManager: zero-arg start auto-selects multicast interfaces") {
        // The auto-selection set is host-dependent; we only check
        // that *something* gets joined when the host has at least
        // one multicast-capable interface (which a CI Linux box
        // always does via loopback when includeLoopback is on).
        MdnsManager m;
        m.setPort(pickFreePort());

        Error err = m.start();
        if (err.isError()) {
                // Host environment lacks any multicast iface — skip
                // the rest cleanly instead of failing the suite.
                MESSAGE("zero-arg start could not find a multicast interface; skipping");
                return;
        }
        CHECK(m.isActive());
        CHECK_FALSE(m.joinedInterfaces().isEmpty());
        m.stop();
        CHECK_FALSE(m.isActive());
}
