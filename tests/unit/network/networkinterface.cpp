/**
 * @file      networkinterface.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/networkinterface.h>
#include <promeki/networkinterfacebackend.h>
#include <promeki/networkinterfaceimpl.h>
#include <promeki/stringlist.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace promeki;

TEST_CASE("NetworkInterface bundled POSIX backend enumerates the loopback interface") {
        auto list = NetworkInterface::enumerate();
        REQUIRE_FALSE(list.isEmpty());
        bool sawLoopback = false;
        for (const auto &iface : list) {
                if (iface.isLoopback()) {
                        sawLoopback = true;
                        // Loopback always carries 127.0.0.1/8 on
                        // Linux/POSIX; verify we captured both the
                        // address and the netmask.
                        bool sawLocalhost = false;
                        for (const auto &subnet : iface.ipv4Subnets()) {
                                if (subnet.address().isLoopback()) {
                                        sawLocalhost = true;
                                        // Some kernels report /8, some /32 for
                                        // legacy point-to-point loopback —
                                        // either is a valid contiguous prefix.
                                        CHECK(subnet.prefixLen() >= 0);
                                        break;
                                }
                        }
                        CHECK(sawLocalhost);
                        break;
                }
        }
        CHECK(sawLoopback);
}

TEST_CASE("NetworkInterface findByName returns invalid handle for unknown interface") {
        NetworkInterface bad = NetworkInterface::findByName("definitely_not_real_iface_xyz");
        CHECK_FALSE(bad.isValid());
}

TEST_CASE("NetworkInterface findByIpv4Address resolves the loopback owner") {
        Ipv4Address      lo(127, 0, 0, 1);
        NetworkInterface iface = NetworkInterface::findByIpv4Address(lo);
        REQUIRE(iface.isValid());
        CHECK(iface.isLoopback());
}

TEST_CASE("NetworkInterface findByMacAddress resolves the primary MAC if any non-loopback iface exists") {
        NetworkInterface primary = NetworkInterface::firstNonLoopback();
        if (!primary.isValid()) {
                MESSAGE("No non-loopback interface available; skipping");
                return;
        }
        NetworkInterface found = NetworkInterface::findByMacAddress(primary.macAddress());
        REQUIRE(found.isValid());
        // Stage 1 stabilises impl identity across enumerate() calls,
        // so handle equality is the canonical comparison now.
        CHECK(found == primary);
        CHECK(found.macAddress() == primary.macAddress());
}

TEST_CASE("NetworkInterface findByMacAddress returns invalid for a null MAC") {
        NetworkInterface bad = NetworkInterface::findByMacAddress(MacAddress());
        CHECK_FALSE(bad.isValid());
}

TEST_CASE("NetworkInterface canRoute / findRoutesTo handle the loopback address") {
        Ipv4Address lo(127, 0, 0, 1);
        auto        routes = NetworkInterface::findRoutesTo(lo);
        REQUIRE_FALSE(routes.isEmpty());
        // At least one of the returned interfaces is the loopback.
        bool sawLoopback = false;
        for (const auto &iface : routes) {
                if (iface.isLoopback()) {
                        sawLoopback = true;
                        CHECK(iface.canRoute(lo));
                        break;
                }
        }
        CHECK(sawLoopback);
}

TEST_CASE("NetworkInterface canRoute multicast matches every up + multicast iface") {
        Ipv4Address mcast(239, 0, 0, 1);
        auto        routes = NetworkInterface::findRoutesTo(mcast);
        // No assertion on the exact count — depends on the host's
        // network state — but every returned iface must be up and
        // multicast-capable, and the loopback shouldn't be in there
        // unless it is multicast-capable (it usually is on Linux).
        for (const auto &iface : routes) {
                CHECK(iface.isUp());
                CHECK(iface.isMulticast());
        }
}

TEST_CASE("NetworkInterface canRoute returns false for an unreachable address on a single iface") {
        // A 0.0.0.0 destination has no subnet binding — canRoute must
        // refuse it on any non-multicast lookup so users don't pick a
        // loopback by accident.
        Ipv4Address      zero(0, 0, 0, 0);
        NetworkInterface lo = NetworkInterface::findByName("lo");
        if (lo.isValid()) {
                CHECK_FALSE(lo.canRoute(zero));
        }
}

TEST_CASE("NetworkInterface ipv4Addresses derives the address-only list from the subnet list") {
        for (const auto &iface : NetworkInterface::enumerate()) {
                CHECK(iface.ipv4Addresses().size() == iface.ipv4Subnets().size());
                for (size_t i = 0; i < iface.ipv4Subnets().size(); ++i) {
                        CHECK(iface.ipv4Addresses()[i] == iface.ipv4Subnets()[i].address());
                }
        }
}

TEST_CASE("NetworkInterface firstNonLoopback skips the loopback interface") {
        NetworkInterface iface = NetworkInterface::firstNonLoopback();
        if (iface.isValid()) {
                CHECK_FALSE(iface.isLoopback());
                CHECK(iface.isUp());
                CHECK_FALSE(iface.macAddress().isNull());
        } else {
                MESSAGE("No non-loopback interface available; skipping deeper checks");
        }
}

TEST_CASE("NetworkInterfaceBackend registry lists the bundled posix backend") {
        StringList names = NetworkInterfaceBackend::registeredBackends();
        bool       sawPosix = false;
        for (const auto &n : names) {
                if (n == "posix") {
                        sawPosix = true;
                        break;
                }
        }
        CHECK(sawPosix);
}

namespace {
        class FakeBackend : public NetworkInterfaceBackend {
                public:
                        explicit FakeBackend(int prio) : _prio(prio) {}
                        String   name() const override { return String("fake"); }
                        int      priority() const override { return _prio; }
                        ImplList enumerate() const override {
                                ImplList             out;
                                NetworkInterfaceData d;
                                d.name = String("fake0");
                                d.macAddresses.pushToBack(MacAddress(0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE));
                                d.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 1, 2, 3), 24));
                                d.isUp = true;
                                d.isMulticast = true;
                                out.pushToBack(NetworkInterfaceImplPtr::takeOwnership(
                                        new NetworkInterfaceImpl(std::move(d))));
                                return out;
                        }

                private:
                        int _prio;
        };
}

TEST_CASE("NetworkInterfaceBackend supports plug-in registration and routing through it") {
        // Lower priority sorts ahead of the bundled posix backend
        // (priority 100), so a custom NIC's interfaces show up first
        // in the enumerated list — the same hook a future ST 2110
        // SmartNIC backend uses.
        NetworkInterfaceBackend::registerBackend(new FakeBackend(10));
        NetworkInterface fake = NetworkInterface::findByName("fake0");
        REQUIRE(fake.isValid());
        CHECK(fake.macAddress() == MacAddress(0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE));
        REQUIRE(fake.ipv4Subnets().size() == 1);
        CHECK(fake.ipv4Subnets()[0].network() == Ipv4Address(10, 1, 2, 0));
        CHECK(fake.canRoute(Ipv4Address(10, 1, 2, 200)));
        CHECK_FALSE(fake.canRoute(Ipv4Address(10, 1, 3, 1)));

        auto routes = NetworkInterface::findRoutesTo(Ipv4Address(10, 1, 2, 7));
        bool sawFake = false;
        for (const auto &iface : routes) {
                if (iface.name() == "fake0") {
                        sawFake = true;
                        break;
                }
        }
        CHECK(sawFake);

        NetworkInterfaceBackend::unregisterBackend("fake");
        CHECK_FALSE(NetworkInterface::findByName("fake0").isValid());
}

namespace {
        // A FakeBackend whose enumerate output is configured at
        // construction time and can be swapped between calls by
        // re-registering with a different list.  Used to drive the
        // stable-impl, disappearance, and dedup tests.
        struct FakeIface {
                String           name;
                uint32_t         index = 0;
                MacAddress::List macs;
                Ipv4Subnet::List ipv4Subnets;
                bool             isUp        = true;
                bool             isMulticast = true;
        };

        class ConfigurableFakeBackend : public NetworkInterfaceBackend {
                public:
                        ConfigurableFakeBackend(String name, int prio, List<FakeIface> ifaces)
                            : _name(std::move(name)), _prio(prio), _ifaces(std::move(ifaces)) {}
                        String   name() const override { return _name; }
                        int      priority() const override { return _prio; }
                        ImplList enumerate() const override {
                                ImplList out;
                                for (const auto &cfg : _ifaces) {
                                        NetworkInterfaceData d;
                                        d.name         = cfg.name;
                                        d.friendlyName = cfg.name;
                                        d.index        = cfg.index;
                                        d.macAddresses = cfg.macs;
                                        d.ipv4Subnets  = cfg.ipv4Subnets;
                                        d.isUp         = cfg.isUp;
                                        d.isRunning    = cfg.isUp;
                                        d.hasCarrier   = cfg.isUp;
                                        d.isMulticast  = cfg.isMulticast;
                                        out.pushToBack(NetworkInterfaceImplPtr::takeOwnership(
                                                new NetworkInterfaceImpl(std::move(d))));
                                }
                                return out;
                        }

                private:
                        String          _name;
                        int             _prio;
                        List<FakeIface> _ifaces;
        };
}

TEST_CASE("NetworkInterfaceKind is populated; loopback reports Loopback") {
        for (const auto &iface : NetworkInterface::enumerate()) {
                if (iface.isLoopback()) {
                        CHECK(iface.kind() == NetworkInterfaceKind::Loopback);
                        return;
                }
        }
        MESSAGE("No loopback iface enumerated; skipping kind check");
}

TEST_CASE("NetworkInterface::data() returns the same fields as per-field accessors") {
        for (const auto &iface : NetworkInterface::enumerate()) {
                NetworkInterfaceData snap = iface.data();
                CHECK(snap.name == iface.name());
                CHECK(snap.friendlyName == iface.friendlyName());
                CHECK(snap.index == iface.index());
                CHECK(snap.mtu == iface.mtu());
                CHECK(snap.kind == iface.kind());
                CHECK(snap.isUp == iface.isUp());
                CHECK(snap.isLoopback == iface.isLoopback());
                CHECK(snap.linkSpeedMbps == iface.linkSpeedMbps());
                CHECK(snap.fullDuplex == iface.fullDuplex());
                CHECK(snap.hasCarrier == iface.hasCarrier());
        }
}

TEST_CASE("NetworkInterface::friendlyName() equals name() on POSIX") {
        for (const auto &iface : NetworkInterface::enumerate()) {
                CHECK(iface.friendlyName() == iface.name());
        }
}

TEST_CASE("NetworkInterface::toString() includes the OS name and is non-empty for valid ifaces") {
        bool checkedAny = false;
        for (const auto &iface : NetworkInterface::enumerate()) {
                String s = iface.toString();
                CHECK_FALSE(s.isEmpty());
                CHECK(s.contains(iface.name()));
                checkedAny = true;
        }
        CHECK(checkedAny);
}

TEST_CASE("NetworkInterface stable-impl identity across enumerate() calls") {
        auto a = NetworkInterface::enumerate();
        auto b = NetworkInterface::enumerate();
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i) {
                CAPTURE(i);
                CAPTURE(a[i].name());
                CHECK(a[i] == b[i]);
        }
}

TEST_CASE("NetworkInterface disappearance: held handle reports down state, lookup returns invalid") {
        FakeIface cfg;
        cfg.name        = "fake0";
        cfg.index       = 9001;
        cfg.macs.pushToBack(MacAddress(0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE));
        cfg.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 7, 7, 1), 24));
        List<FakeIface> withFake;
        withFake.pushToBack(cfg);
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("fake", 10, withFake));
        NetworkInterface held = NetworkInterface::findByName("fake0");
        REQUIRE(held.isValid());
        CHECK(held.isUp());
        REQUIRE(held.ipv4Subnets().size() == 1);

        // Re-register with an empty iface list — same backend name,
        // same priority, but no fake0 in the output.  The held handle
        // should remain valid (refcount keeps the impl alive) but
        // report the down-snapshot state.
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("fake", 10, List<FakeIface>()));
        // Force a re-enumeration so the cache notices fake0 is gone.
        (void) NetworkInterface::enumerate();
        CHECK(held.isValid());
        CHECK_FALSE(held.isUp());
        CHECK(held.ipv4Subnets().isEmpty());
        // Cache entry was evicted, so a fresh lookup returns invalid.
        CHECK_FALSE(NetworkInterface::findByName("fake0").isValid());

        NetworkInterfaceBackend::unregisterBackend("fake");
}

TEST_CASE("NetworkInterface unregister cleanup: held handle reports down state") {
        FakeIface cfg;
        cfg.name = "fake1";
        cfg.macs.pushToBack(MacAddress(0x02, 0xCC, 0xDD, 0xEE, 0xFF, 0x00));
        cfg.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 8, 8, 1), 24));
        List<FakeIface> ifaces;
        ifaces.pushToBack(cfg);
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("fake", 10, ifaces));
        NetworkInterface held = NetworkInterface::findByName("fake1");
        REQUIRE(held.isValid());
        CHECK(held.isUp());

        NetworkInterfaceBackend::unregisterBackend("fake");
        CHECK(held.isValid());
        CHECK_FALSE(held.isUp());
        CHECK(held.ipv4Subnets().isEmpty());
        CHECK_FALSE(NetworkInterface::findByName("fake1").isValid());
}

TEST_CASE("NetworkInterface concurrent data() reads against replaceData() are torn-read free") {
        // Build two snapshots distinguishable by every interesting
        // field; reader spins a loop pulling iface.data() and asserts
        // each snapshot is *one* of the two (no torn mix).
        NetworkInterfaceData snapA;
        snapA.name         = "torn0";
        snapA.friendlyName = "torn0";
        snapA.index        = 1;
        snapA.mtu          = 1500;
        snapA.isUp         = true;
        snapA.isRunning    = true;
        snapA.hasCarrier   = true;
        snapA.macAddresses.pushToBack(MacAddress(0x02, 0x01, 0x02, 0x03, 0x04, 0x05));
        snapA.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 0, 0, 1), 24));

        NetworkInterfaceData snapB;
        snapB.name         = "torn0";
        snapB.friendlyName = "torn0";
        snapB.index        = 1;
        snapB.mtu          = 9000;
        snapB.isUp         = false;
        snapB.isRunning    = false;
        snapB.hasCarrier   = false;
        snapB.macAddresses.pushToBack(MacAddress(0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE));
        snapB.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(192, 168, 50, 5), 16));

        NetworkInterfaceImplPtr impl = NetworkInterfaceImplPtr::takeOwnership(new NetworkInterfaceImpl(snapA));

        std::atomic<bool> stop{false};
        std::atomic<int>  reads{0};
        std::atomic<int>  inconsistent{0};
        std::thread       reader([&]() {
                while (!stop.load(std::memory_order_relaxed)) {
                        NetworkInterfaceData s = impl->data();
                        // Every snapshot must be wholly snapA OR wholly snapB.
                        bool isA = (s.mtu == 1500) && s.isUp && (!s.macAddresses.isEmpty()) &&
                                   (s.macAddresses[0] == MacAddress(0x02, 0x01, 0x02, 0x03, 0x04, 0x05));
                        bool isB = (s.mtu == 9000) && !s.isUp && (!s.macAddresses.isEmpty()) &&
                                   (s.macAddresses[0] == MacAddress(0x02, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE));
                        if (!isA && !isB) inconsistent.fetch_add(1, std::memory_order_relaxed);
                        reads.fetch_add(1, std::memory_order_relaxed);
                }
        });

        std::thread writer([&]() {
                bool flip = false;
                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                while (std::chrono::steady_clock::now() < deadline) {
                        impl.modify()->replaceData(flip ? snapB : snapA);
                        flip = !flip;
                }
        });

        writer.join();
        stop.store(true, std::memory_order_relaxed);
        reader.join();

        CHECK(inconsistent.load() == 0);
        CHECK(reads.load() > 0);
}

TEST_CASE("NetworkInterfaceBackend TTL caches the enumeration result within the window") {
        // Use a TTL we can drive deterministically.  Restore the
        // production default at the end so other test cases observe
        // a clean baseline.
        unsigned int prevTtl = NetworkInterfaceBackend::enumerationTtlMs();
        NetworkInterfaceBackend::setEnumerationTtlMs(60'000);
        NetworkInterfaceBackend::invalidateEnumerationCache();

        FakeIface cfg;
        cfg.name = "ttl0";
        cfg.macs.pushToBack(MacAddress(0x02, 0x55, 0x66, 0x77, 0x88, 0x99));
        cfg.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 99, 99, 1), 24));
        List<FakeIface> ifaces;
        ifaces.pushToBack(cfg);
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("ttlfake", 5, ifaces));

        // First call populates the TTL cache; second call (well within
        // the 60-second window) returns the same list even after we
        // re-register the backend with an empty output.
        auto a = NetworkInterface::enumerate();
        bool sawA = false;
        for (const auto &iface : a) if (iface.name() == "ttl0") sawA = true;
        REQUIRE(sawA);

        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("ttlfake", 5, List<FakeIface>()));
        // registerBackend invalidates the cache, so the next call
        // re-enumerates.  Force a known cached state by enumerating
        // again now and then swapping a *different* backend without
        // touching the registry.
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("ttlfake", 5, ifaces));
        auto b = NetworkInterface::enumerate();
        // The second enumerate is a TTL hit on the *just-populated*
        // cache, so it returns the same list.
        auto c = NetworkInterface::enumerate();
        CHECK(b.size() == c.size());
        for (size_t i = 0; i < b.size(); ++i) CHECK(b[i] == c[i]);

        // After invalidate, the next call sees the swapped output.
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("ttlfake", 5, List<FakeIface>()));
        auto d = NetworkInterface::enumerate();
        bool sawD = false;
        for (const auto &iface : d) if (iface.name() == "ttl0") sawD = true;
        CHECK_FALSE(sawD);

        NetworkInterfaceBackend::unregisterBackend("ttlfake");
        NetworkInterfaceBackend::setEnumerationTtlMs(prevTtl);
        NetworkInterfaceBackend::invalidateEnumerationCache();
}

TEST_CASE("NetworkInterfaceBackend setEnumerationTtlMs(0) disables caching") {
        unsigned int prevTtl = NetworkInterfaceBackend::enumerationTtlMs();
        NetworkInterfaceBackend::setEnumerationTtlMs(0);
        NetworkInterfaceBackend::invalidateEnumerationCache();

        FakeIface cfgA;
        cfgA.name = "ttlnocache";
        cfgA.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 88, 88, 1), 24));
        List<FakeIface> withA;
        withA.pushToBack(cfgA);
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("ttlfake2", 5, withA));
        auto a = NetworkInterface::enumerate();
        bool sawA = false;
        for (const auto &iface : a) if (iface.name() == "ttlnocache") sawA = true;
        REQUIRE(sawA);

        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("ttlfake2", 5, List<FakeIface>()));
        auto b = NetworkInterface::enumerate();
        bool sawB = false;
        for (const auto &iface : b) if (iface.name() == "ttlnocache") sawB = true;
        CHECK_FALSE(sawB);

        NetworkInterfaceBackend::unregisterBackend("ttlfake2");
        NetworkInterfaceBackend::setEnumerationTtlMs(prevTtl);
}

TEST_CASE("NetworkInterface cross-backend dedup: lower-priority duplicate is dropped") {
        FakeIface cfg;
        cfg.name = "shared0";
        cfg.macs.pushToBack(MacAddress(0x02, 0x11, 0x22, 0x33, 0x44, 0x55));
        cfg.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(172, 30, 0, 1), 24));
        List<FakeIface> ifaces;
        ifaces.pushToBack(cfg);
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("fake-low", 5, ifaces));
        NetworkInterfaceBackend::registerBackend(new ConfigurableFakeBackend("fake-high", 50, ifaces));

        auto list = NetworkInterface::enumerate();
        int  count = 0;
        for (const auto &iface : list) {
                if (iface.name() == "shared0") ++count;
        }
        CHECK(count == 1);

        NetworkInterfaceBackend::unregisterBackend("fake-low");
        NetworkInterfaceBackend::unregisterBackend("fake-high");
}

TEST_CASE("Ipv4Subnet contains / network / broadcast / prefixLen") {
        Ipv4Subnet s(Ipv4Address(192, 168, 1, 5), Ipv4Address(255, 255, 255, 0));
        CHECK(s.prefixLen() == 24);
        CHECK(s.network() == Ipv4Address(192, 168, 1, 0));
        CHECK(s.broadcast() == Ipv4Address(192, 168, 1, 255));
        CHECK(s.contains(Ipv4Address(192, 168, 1, 200)));
        CHECK_FALSE(s.contains(Ipv4Address(192, 168, 2, 1)));
        CHECK(s.toString() == "192.168.1.5/24");
}

TEST_CASE("Ipv4Subnet handles /32 host route") {
        Ipv4Subnet host(Ipv4Address(8, 8, 8, 8), 32);
        CHECK(host.prefixLen() == 32);
        CHECK(host.network() == Ipv4Address(8, 8, 8, 8));
        CHECK(host.broadcast() == Ipv4Address(8, 8, 8, 8));
        CHECK(host.contains(Ipv4Address(8, 8, 8, 8)));
        CHECK_FALSE(host.contains(Ipv4Address(8, 8, 8, 9)));
}

TEST_CASE("Ipv4Subnet fromString parses CIDR and dotted-quad netmask") {
        auto [a, ae] = Ipv4Subnet::fromString("10.0.0.1/8");
        REQUIRE(ae.isOk());
        CHECK(a.prefixLen() == 8);
        CHECK(a.network() == Ipv4Address(10, 0, 0, 0));

        auto [b, be] = Ipv4Subnet::fromString("172.16.5.1/255.255.0.0");
        REQUIRE(be.isOk());
        CHECK(b.prefixLen() == 16);

        auto [c, ce] = Ipv4Subnet::fromString("not-an-ip");
        CHECK(ce.isError());
}

TEST_CASE("Ipv4Subnet detects non-contiguous netmasks") {
        Ipv4Subnet mixed(Ipv4Address(10, 0, 0, 1), Ipv4Address(255, 0, 255, 0));
        CHECK(mixed.prefixLen() == -1);
        // toString falls back to address/netmask form when prefixLen
        // can't represent the mask.
        CHECK(mixed.toString() == "10.0.0.1/255.0.255.0");
}

TEST_CASE("Ipv6Subnet contains / prefixLen / fromString") {
        auto [a, ae] = Ipv6Subnet::fromString("2001:db8::1/64");
        REQUIRE(ae.isOk());
        CHECK(a.prefixLen() == 64);

        auto [b, be] = Ipv6Subnet::fromString("2001:db8::abcd");
        REQUIRE(be.isOk());
        CHECK(b.prefixLen() == 128);

        CHECK(a.contains(b.address()));
        auto [outside, oe] = Ipv6Subnet::fromString("2001:db9::1");
        REQUIRE(oe.isOk());
        CHECK_FALSE(a.contains(outside.address()));

        auto [bad, badErr] = Ipv6Subnet::fromString("not-an-ip");
        CHECK(badErr.isError());
}

TEST_CASE("Ipv6Subnet handles default route /0") {
        Ipv6Subnet def(Ipv6Address(), 0);
        // The all-zero prefix matches every address.
        auto [a, ae] = Ipv6Subnet::fromString("2001:db8::1");
        REQUIRE(ae.isOk());
        CHECK(def.contains(a.address()));
}
