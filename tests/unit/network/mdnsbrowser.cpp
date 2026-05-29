/**
 * @file      mdnsbrowser.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <atomic>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/application.h>
#include <promeki/buffer.h>
#include <promeki/mdnsbrowser.h>
#include <promeki/mdnsmanager.h>
#include <promeki/mdnsservicetype.h>
#include <promeki/networkinterface.h>
#include <promeki/socketaddress.h>
#include <vector>

using namespace promeki;

namespace {

        // Small hand-rolled mDNS packet builder.  Same shape as the
        // helper used by the parser tests — duplicated rather than
        // shared because doctest test TUs do not link against a
        // common helper library and keeping the helper inline is
        // worth less than half a screen each.
        class PacketBuilder {
                public:
                        void writeHeader(uint16_t id, uint16_t flags, uint16_t qd, uint16_t an,
                                         uint16_t ns, uint16_t ar) {
                                writeU16(id); writeU16(flags); writeU16(qd);
                                writeU16(an); writeU16(ns); writeU16(ar);
                        }
                        void writeU16(uint16_t v) {
                                _data.push_back(static_cast<uint8_t>(v >> 8));
                                _data.push_back(static_cast<uint8_t>(v & 0xFF));
                        }
                        void writeU32(uint32_t v) {
                                _data.push_back(static_cast<uint8_t>(v >> 24));
                                _data.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
                                _data.push_back(static_cast<uint8_t>((v >> 8)  & 0xFF));
                                _data.push_back(static_cast<uint8_t>(v & 0xFF));
                        }
                        void writeName(const char *name) {
                                String s(name);
                                size_t start = 0;
                                size_t end   = 0;
                                size_t n     = s.size();
                                while (start < n) {
                                        end = start;
                                        while (end < n && s[end] != '.') ++end;
                                        size_t segLen = end - start;
                                        _data.push_back(static_cast<uint8_t>(segLen));
                                        for (size_t i = start; i < end; ++i) {
                                                _data.push_back(static_cast<uint8_t>(s[i]));
                                        }
                                        start = end + 1;
                                }
                                // RFC 1035 §3.1: every encoded name
                                // ends with a zero-length root label.
                                _data.push_back(0);
                        }
                        void patchU16(size_t pos, uint16_t v) {
                                _data[pos]     = static_cast<uint8_t>(v >> 8);
                                _data[pos + 1] = static_cast<uint8_t>(v & 0xFF);
                        }
                        size_t writeRecordHeader(const char *name, uint16_t type, uint16_t klass,
                                                 uint32_t ttl) {
                                writeName(name);
                                writeU16(type);
                                writeU16(klass);
                                writeU32(ttl);
                                size_t rdLengthPos = _data.size();
                                writeU16(0);
                                return rdLengthPos;
                        }
                        void writePtr(const char *owner, uint16_t klass, uint32_t ttl,
                                      const char *target) {
                                size_t rdLen = writeRecordHeader(owner, 12, klass, ttl);
                                size_t rdataStart = _data.size();
                                writeName(target);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                        }
                        void writeSrv(const char *owner, uint16_t klass, uint32_t ttl,
                                      uint16_t prio, uint16_t weight, uint16_t port,
                                      const char *target) {
                                size_t rdLen = writeRecordHeader(owner, 33, klass, ttl);
                                size_t rdataStart = _data.size();
                                writeU16(prio); writeU16(weight); writeU16(port);
                                writeName(target);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                        }
                        void writeTxt(const char *owner, uint16_t klass, uint32_t ttl,
                                      const std::vector<std::pair<std::string, std::string>> &entries) {
                                size_t rdLen = writeRecordHeader(owner, 16, klass, ttl);
                                size_t rdataStart = _data.size();
                                for (const auto &kv : entries) {
                                        size_t entryLen = kv.first.size() + 1 + kv.second.size();
                                        _data.push_back(static_cast<uint8_t>(entryLen));
                                        for (char c : kv.first)  _data.push_back(static_cast<uint8_t>(c));
                                        _data.push_back('=');
                                        for (char c : kv.second) _data.push_back(static_cast<uint8_t>(c));
                                }
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                        }
                        void writeA(const char *owner, uint16_t klass, uint32_t ttl,
                                    uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
                                size_t rdLen = writeRecordHeader(owner, 1, klass, ttl);
                                size_t rdataStart = _data.size();
                                _data.push_back(a); _data.push_back(b);
                                _data.push_back(c); _data.push_back(d);
                                patchU16(rdLen, static_cast<uint16_t>(_data.size() - rdataStart));
                        }
                        Buffer toBuffer() const {
                                Buffer b(_data.size());
                                std::memcpy(b.data(), _data.data(), _data.size());
                                b.setSize(_data.size());
                                return b;
                        }

                        // Public for tests that need to hand-craft
                        // label encodings (embedded-dot instance
                        // labels, for example).
                        std::vector<uint8_t> _data;
        };

        // Builds the canonical "PTR + SRV + A" announce packet for a
        // service called @p instance of type @p typeFqdn reachable
        // at @p hostFqdn:@p port with @p addr.  Mirrors what Avahi
        // / mDNSResponder send in their initial announce burst.
        Buffer buildAnnounce(const char *typeFqdn, const char *instanceFqdn,
                             const char *hostFqdn, uint16_t port,
                             uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                             uint32_t ttl = 120) {
                PacketBuilder builder;
                builder.writeHeader(0, 0x8400, 0, 1, 0, 2);
                builder.writePtr(typeFqdn, 0x0001, 4500, instanceFqdn);
                builder.writeSrv(instanceFqdn, 0x8001, ttl, 0, 0, port, hostFqdn);
                builder.writeA(hostFqdn, 0x8001, ttl, a, b, c, d);
                return builder.toBuffer();
        }

        SocketAddress dummySender() {
                return SocketAddress(Ipv4Address(192, 168, 1, 1), 5353);
        }

} // namespace

TEST_CASE("MdnsBrowser: default state") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        CHECK(browser.serviceType().app() == String("http"));
        CHECK(browser.manager() == nullptr);
        CHECK(browser.instances().isEmpty());
}

TEST_CASE("MdnsBrowser: ignores packets for other service types") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int> foundCount{0};
        browser.serviceFoundSignal.connect([&](MdnsServiceInstance) {
                foundCount.fetch_add(1);
        });

        // _ipp._tcp packet must not trip the _http browser's signal.
        Buffer announce = buildAnnounce(
                "_ipp._tcp.local.",
                "Office Printer._ipp._tcp.local.",
                "printer.local.", 631,
                10, 0, 0, 5);
        browser.handlePacket(announce, dummySender(), NetworkInterface());

        CHECK(foundCount.load() == 0);
        CHECK(browser.instances().isEmpty());
}

TEST_CASE("MdnsBrowser: PTR + SRV + A announce emits serviceFound exactly once") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int>    foundCount{0};
        MdnsServiceInstance found;
        browser.serviceFoundSignal.connect([&](MdnsServiceInstance inst) {
                foundCount.fetch_add(1);
                found = inst;
        });

        Buffer announce = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(announce, dummySender(), NetworkInterface());

        CHECK(foundCount.load() == 1);
        CHECK(found.instanceName() == String("Studio Camera"));
        CHECK(found.type().app()   == String("http"));
        CHECK(found.port()         == 80);
        CHECK(found.hostname()     == String("camera.local."));

        // The browser's snapshot lists the same instance.
        List<MdnsServiceInstance> snap = browser.instances();
        REQUIRE(snap.size() == 1);
        CHECK(snap[0].port() == 80);
}

TEST_CASE("MdnsBrowser: subsequent address record emits serviceUpdated") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int>    foundCount{0};
        std::atomic<int>    updateCount{0};
        MdnsServiceInstance lastUpdate;
        browser.serviceFoundSignal.connect([&](MdnsServiceInstance) {
                foundCount.fetch_add(1);
        });
        browser.serviceUpdatedSignal.connect([&](MdnsServiceInstance inst) {
                updateCount.fetch_add(1);
                lastUpdate = inst;
        });

        // First the initial announce gives port + hostname + v4 in one packet.
        Buffer first = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(first, dummySender(), NetworkInterface());

        // Now an additional A record on a different address arrives.
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writeA("camera.local.", 0x8001, 120, 10, 0, 0, 8);
        browser.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());

        CHECK(foundCount.load() == 1);
        CHECK(updateCount.load() >= 1);
        REQUIRE(lastUpdate.ipv4Addresses().size() >= 2);
}

TEST_CASE("MdnsBrowser: TXT update emits serviceUpdated") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int>    updateCount{0};
        MdnsServiceInstance lastUpdate;
        browser.serviceUpdatedSignal.connect([&](MdnsServiceInstance inst) {
                updateCount.fetch_add(1);
                lastUpdate = inst;
        });

        Buffer announce = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(announce, dummySender(), NetworkInterface());

        // Send a TXT update.
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writeTxt("Studio Camera._http._tcp.local.", 0x8001, 4500,
                   {{"path", "/api"}, {"version", "2.0"}});
        browser.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());

        CHECK(updateCount.load() >= 1);
        CHECK(lastUpdate.txt().value("path")    == String("/api"));
        CHECK(lastUpdate.txt().value("version") == String("2.0"));
}

TEST_CASE("MdnsBrowser: Goodbye PTR emits serviceLost") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int>    foundCount{0};
        std::atomic<int>    lostCount{0};
        MdnsServiceInstance lost;
        browser.serviceFoundSignal.connect([&](MdnsServiceInstance) { foundCount.fetch_add(1); });
        browser.serviceLostSignal.connect([&](MdnsServiceInstance inst) {
                lostCount.fetch_add(1);
                lost = inst;
        });

        // Announce.
        Buffer announce = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(announce, dummySender(), NetworkInterface());
        REQUIRE(foundCount.load() == 1);

        // Goodbye PTR — TTL=0.
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writePtr("_http._tcp.local.", 0x0001, 0, "Studio Camera._http._tcp.local.");
        browser.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());

        CHECK(lostCount.load() == 1);
        CHECK(lost.instanceName() == String("Studio Camera"));
        CHECK(browser.instances().isEmpty());
}

TEST_CASE("MdnsBrowser: case-insensitive name matching") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int> foundCount{0};
        browser.serviceFoundSignal.connect([&](MdnsServiceInstance) { foundCount.fetch_add(1); });

        // Type label arrives upper-case on the wire; browser stored
        // it lower-case.  DNS is case-insensitive so the match should
        // still hold.
        Buffer announce = buildAnnounce(
                "_HTTP._TCP.local.",
                "Studio Camera._HTTP._TCP.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(announce, dummySender(), NetworkInterface());

        CHECK(foundCount.load() == 1);
}

TEST_CASE("MdnsBrowser: serviceFound waits for SRV (PTR-only announce does not emit)") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int> foundCount{0};
        browser.serviceFoundSignal.connect([&](MdnsServiceInstance) { foundCount.fetch_add(1); });

        // PTR-only — no port / hostname yet.
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writePtr("_http._tcp.local.", 0x0001, 4500, "Studio Camera._http._tcp.local.");
        browser.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());

        CHECK(foundCount.load() == 0);
        // The placeholder lives in the entries map but is not yet
        // exposed through the public instances() snapshot.
        CHECK(browser.instances().isEmpty());

        // Now the SRV arrives.  Should emit serviceFound.
        PacketBuilder b2;
        b2.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b2.writeSrv("Studio Camera._http._tcp.local.", 0x8001, 120, 0, 0, 80, "camera.local.");
        browser.handlePacket(b2.toBuffer(), dummySender(), NetworkInterface());

        CHECK(foundCount.load() == 1);
}

TEST_CASE("MdnsBrowser: clearCache empties the snapshot without emitting signals") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int> lostCount{0};
        browser.serviceLostSignal.connect([&](MdnsServiceInstance) { lostCount.fetch_add(1); });

        Buffer announce = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(announce, dummySender(), NetworkInterface());
        REQUIRE_FALSE(browser.instances().isEmpty());

        browser.clearCache();
        CHECK(browser.instances().isEmpty());
        CHECK(lostCount.load() == 0);
}

TEST_CASE("MdnsBrowser: explicit manager wins over the application fallback") {
        // Construct an idle (never-started) manager and attach it.
        // sendQuery will return NotReady from that manager, but the
        // browser routes through it rather than the Application
        // singleton.  effectiveManager() reports the explicit one.
        MdnsManager mgr;
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        browser.setManager(&mgr);
        CHECK(browser.effectiveManager() == &mgr);

        Error err = browser.start();
        // Manager is idle, so start() forwards the NotReady from the
        // initial sendQuery — but the browser still flipped to
        // active.  Verifies the routing rather than the success of
        // the underlying send.
        CHECK(err == Error::NotReady);
        CHECK(browser.isActive());
        browser.stop();
        browser.setManager(nullptr);
}

TEST_CASE("MdnsBrowser: nullptr setManager re-engages the application fallback") {
        MdnsManager mgr;
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        browser.setManager(&mgr);
        REQUIRE(browser.effectiveManager() == &mgr);
        browser.setManager(nullptr);
        // After clearing the explicit manager, effectiveManager()
        // falls back to Application::mdnsManager() — which lazily
        // constructs the global engine.  We do not require it to be
        // active (the host may lack a multicast iface) — only that
        // the fallback was engaged.
        CHECK(browser.effectiveManager() == Application::mdnsManager());
        Application::stopMdnsManager();
}

TEST_CASE("MdnsBrowser: evictExpiredAt removes past-expiry entries and fires serviceLost") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int>    lostCount{0};
        MdnsServiceInstance lost;
        browser.serviceLostSignal.connect([&](MdnsServiceInstance inst) {
                lostCount.fetch_add(1);
                lost = inst;
        });

        // Wire TTL is 120 seconds on the canonical announce — well
        // short of the two-hour fast-forward we use to expire it.
        Buffer announce = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(announce, dummySender(), NetworkInterface());
        REQUIRE_FALSE(browser.instances().isEmpty());

        const TimeStamp future = TimeStamp::now() + Duration::fromHours(2);
        int n = browser.evictExpiredAt(future);
        CHECK(n == 1);
        CHECK(lostCount.load() == 1);
        CHECK(lost.instanceName() == String("Studio Camera"));
        CHECK(browser.instances().isEmpty());
}

TEST_CASE("MdnsBrowser: evictExpiredAt leaves fresh entries alone") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int> lostCount{0};
        browser.serviceLostSignal.connect([&](MdnsServiceInstance) { lostCount.fetch_add(1); });

        Buffer announce = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(announce, dummySender(), NetworkInterface());

        // "now" sample taken before any clock drift can age the
        // entry past its 120-second TTL.
        const TimeStamp now = TimeStamp::now();
        CHECK(browser.evictExpiredAt(now) == 0);
        CHECK(lostCount.load() == 0);
        CHECK_FALSE(browser.instances().isEmpty());
}

TEST_CASE("MdnsBrowser: onManagerTick evicts expired entries even without an active manager") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int> lostCount{0};
        browser.serviceLostSignal.connect([&](MdnsServiceInstance) { lostCount.fetch_add(1); });

        Buffer announce = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(announce, dummySender(), NetworkInterface());

        // No manager attached → backoff branch is a no-op, but
        // eviction still runs.
        browser.onManagerTick(TimeStamp::now() + Duration::fromHours(2));
        CHECK(lostCount.load() == 1);
        CHECK(browser.instances().isEmpty());
}

TEST_CASE("MdnsBrowser: onManagerTick before start is a no-op on the backoff side") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        // No assertion on queries (no manager) — we just verify the
        // call does not crash with an uninitialised backoff schedule.
        browser.onManagerTick(TimeStamp::now() + Duration::fromHours(1));
        CHECK_FALSE(browser.isActive());
        CHECK(browser.queryFireCount() == 0);
}

TEST_CASE("MdnsBrowser: continuous-query backoff doubles per tick fire") {
        // The browser needs a manager attached so the backoff branch
        // in onManagerTick runs; it does not need the manager to be
        // active.  start() will return NotReady because the manager
        // is not running, but the schedule is still seeded and the
        // tick-driven branch increments the fire counter.
        MdnsManager mgr;
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        browser.setManager(&mgr);

        const TimeStamp t0 = TimeStamp::now();
        // start() — schedules nextQueryAt = (now during start) + 1s
        // and fires the initial query (which fails because the
        // manager is inactive; we don't care about the return).
        (void)browser.start();
        CHECK(browser.isActive());
        CHECK(browser.currentBackoffInterval() == Duration::fromSeconds(1));
        CHECK(browser.queryFireCount() == 0);   // initial query is not via tick

        // Tick before the deadline — no fire, no double.
        browser.onManagerTick(t0 + Duration::fromMilliseconds(500));
        CHECK(browser.queryFireCount() == 0);
        CHECK(browser.currentBackoffInterval() == Duration::fromSeconds(1));

        // Tick past the deadline — fires, doubles to 2 s.
        browser.onManagerTick(t0 + Duration::fromMilliseconds(1500));
        CHECK(browser.queryFireCount() == 1);
        CHECK(browser.currentBackoffInterval() == Duration::fromSeconds(2));

        // The next deadline is (t0 + 1500ms) + 2s = t0 + 3500ms.
        // Tick at 3700ms past t0 → another fire, doubles to 4 s.
        browser.onManagerTick(t0 + Duration::fromMilliseconds(3700));
        CHECK(browser.queryFireCount() == 2);
        CHECK(browser.currentBackoffInterval() == Duration::fromSeconds(4));

        // Detach cleanly so the manager destructor runs without
        // a lingering registration entry.
        browser.setManager(nullptr);
}

TEST_CASE("MdnsBrowser: backoff caps at one hour") {
        MdnsManager mgr;
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        browser.setManager(&mgr);
        (void)browser.start();

        TimeStamp t = TimeStamp::now();
        // Walk the schedule forward by hours each tick.  After
        // enough fires the interval should hit the 1-hour cap and
        // stay there.
        for (int i = 0; i < 20; ++i) {
                t = t + Duration::fromHours(2);
                browser.onManagerTick(t);
        }
        CHECK(browser.currentBackoffInterval() == Duration::fromHours(1));
        browser.setManager(nullptr);
}

TEST_CASE("MdnsBrowser: stop resets the backoff schedule") {
        MdnsManager mgr;
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));
        browser.setManager(&mgr);
        (void)browser.start();
        browser.onManagerTick(TimeStamp::now() + Duration::fromSeconds(5));
        REQUIRE(browser.currentBackoffInterval() == Duration::fromSeconds(2));

        browser.stop();
        // After stop the schedule is cleared — currentBackoffInterval
        // returns default-constructed (invalid) Duration.
        CHECK_FALSE(browser.currentBackoffInterval().isValid());
        CHECK_FALSE(browser.isActive());
        browser.setManager(nullptr);
}

TEST_CASE("MdnsBrowser: cache-flush A records inside grace window accumulate") {
        // RFC 6762 §10.2 grace window: cache-flush A records that
        // land within @ref CacheFlushGraceMs of each other are
        // treated as parts of the same multi-record announce and
        // append to the address list rather than wiping it.  The
        // canonical sequence here is the original announce burst
        // (one A) followed milliseconds later by a second A on the
        // same host — both cache-flush.  Both addresses should
        // survive in the cache.
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        Buffer first = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(first, dummySender(), NetworkInterface());

        // A second cache-flush A on the same host with a different
        // address arrives a few microseconds later (the
        // handlePacket calls are synchronous in this test).
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writeA("camera.local.", 0x8001, 120, 10, 0, 0, 8);
        browser.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());

        List<MdnsServiceInstance> snap = browser.instances();
        REQUIRE(snap.size() == 1);
        CHECK(snap[0].ipv4Addresses().size() == 2);
}

TEST_CASE("MdnsBrowser: non-cache-flush A records accumulate without wiping") {
        // The cache-flush bit MUST be respected; the absence of it
        // must not cause the cache to wipe.  A regression here would
        // mean a misimplemented receiver dropping addresses when a
        // responder forgets the bit on a follow-up A record.
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        Buffer first = buildAnnounce(
                "_http._tcp.local.",
                "Studio Camera._http._tcp.local.",
                "camera.local.", 80,
                10, 0, 0, 7);
        browser.handlePacket(first, dummySender(), NetworkInterface());

        // Second A record carries class 0x0001 (no cache-flush bit).
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writeA("camera.local.", 0x0001, 120, 10, 0, 0, 9);
        browser.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());

        List<MdnsServiceInstance> snap = browser.instances();
        REQUIRE(snap.size() == 1);
        CHECK(snap[0].ipv4Addresses().size() == 2);
}

TEST_CASE("MdnsBrowser: instance label with embedded dot survives the round trip") {
        // RFC 6763 §4.1.1 permits @c '.' inside an instance label.
        // We hand-craft the wire form so the embedded dot stays
        // inside a single 15-byte label rather than getting split by
        // the helper's text-mode encoder.  Then we verify that the
        // browser produces an unescaped instanceName and an
        // escape-aware fqdn() that round-trips back to the original
        // label structure.
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int>    foundCount{0};
        MdnsServiceInstance lastFound;
        browser.serviceFoundSignal.connect([&](MdnsServiceInstance i) {
                foundCount.fetch_add(1);
                lastFound = i;
        });

        auto appendInstanceLabels = [](PacketBuilder &pb) {
                const char *inst = "Studio.B Camera";
                pb._data.push_back(static_cast<uint8_t>(std::strlen(inst)));
                for (size_t i = 0; i < std::strlen(inst); ++i) {
                        pb._data.push_back(static_cast<uint8_t>(inst[i]));
                }
                pb._data.push_back(5);
                for (char c : std::string("_http"))  pb._data.push_back(c);
                pb._data.push_back(4);
                for (char c : std::string("_tcp"))   pb._data.push_back(c);
                pb._data.push_back(5);
                for (char c : std::string("local"))  pb._data.push_back(c);
                pb._data.push_back(0);
        };

        // PTR + SRV in one packet so the browser goes addressable
        // immediately.
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 2, 0, 0);

        // PTR: owner="_http._tcp.local.", rdata = the instance fqdn
        // with the embedded-dot label.
        size_t ptrRdLen = b.writeRecordHeader("_http._tcp.local.", 12, 0x0001, 4500);
        size_t ptrRdataStart = b._data.size();
        appendInstanceLabels(b);
        b.patchU16(ptrRdLen, static_cast<uint16_t>(b._data.size() - ptrRdataStart));

        // SRV: owner = the embedded-dot fqdn, target = plain hostname.
        appendInstanceLabels(b);
        b.writeU16(33);        // SRV
        b.writeU16(0x8001);    // cache-flush + IN
        b.writeU32(120);
        size_t srvRdLen = b._data.size();
        b.writeU16(0);         // rdlen placeholder
        size_t srvRdataStart = b._data.size();
        b.writeU16(0); b.writeU16(0); b.writeU16(8080);
        b.writeName("host.local.");
        b.patchU16(srvRdLen, static_cast<uint16_t>(b._data.size() - srvRdataStart));

        browser.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());

        CHECK(foundCount.load() == 1);
        CHECK(lastFound.instanceName() == String("Studio.B Camera"));
        // The fqdn() output must escape the embedded dot so a
        // subsequent encoder round-trip splits on the same 4 label
        // boundaries the publisher used.
        CHECK(lastFound.fqdn() == String("Studio\\.B Camera._http._tcp.local."));
}

TEST_CASE("MdnsBrowser: evictExpired on unfound entries is silent") {
        MdnsBrowser browser(MdnsServiceType("http", MdnsServiceType::Protocol::Tcp));

        std::atomic<int> lostCount{0};
        browser.serviceLostSignal.connect([&](MdnsServiceInstance) { lostCount.fetch_add(1); });

        // PTR-only — entry exists but never reached foundEmitted.
        PacketBuilder b;
        b.writeHeader(0, 0x8400, 0, 1, 0, 0);
        b.writePtr("_http._tcp.local.", 0x0001, 120, "Studio Camera._http._tcp.local.");
        browser.handlePacket(b.toBuffer(), dummySender(), NetworkInterface());

        const TimeStamp future = TimeStamp::now() + Duration::fromHours(2);
        int n = browser.evictExpiredAt(future);
        CHECK(n == 0);                        // count is "lost emissions", not "removed entries"
        CHECK(lostCount.load() == 0);
}
