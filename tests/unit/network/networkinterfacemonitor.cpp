/**
 * @file      networkinterfacemonitor.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/networkinterfacemonitor.h>
#include <promeki/networkinterface.h>
#include <promeki/networkinterfacebackend.h>
#include <promeki/eventloop.h>

#include <promeki/mutex.h>

#include <atomic>

using namespace promeki;

namespace {
        // FakeBackend whose enumerate output is rotated by index so
        // tests can drive deterministic add/remove/address-diff.
        struct FakeIface {
                String           name;
                MacAddress::List macs;
                Ipv4Subnet::List ipv4Subnets;
                Ipv6Subnet::List ipv6Subnets;
                bool             isUp        = true;
                bool             isMulticast = true;
        };

        // Process-global slot a test can populate before the backend
        // is registered.  The backend reads from this slot every time
        // enumerate() runs, so tests can mutate the iface list
        // in-place — keeping the impl-cache hot and exercising the
        // monitor's address-diff path without backend churn.
        struct SharedState {
                Mutex           mutex;
                List<FakeIface> ifaces;
        };

        SharedState &monitorTestState() {
                static SharedState s;
                return s;
        }

        class MonitorFakeBackend : public NetworkInterfaceBackend {
                public:
                        MonitorFakeBackend(String name, int prio) : _name(std::move(name)), _prio(prio) {}
                        String   name() const override { return _name; }
                        int      priority() const override { return _prio; }
                        ImplList enumerate() const override {
                                List<FakeIface> snapshot;
                                {
                                        SharedState  &s = monitorTestState();
                                        Mutex::Locker lock(s.mutex);
                                        snapshot = s.ifaces;
                                }
                                ImplList out;
                                for (const auto &cfg : snapshot) {
                                        NetworkInterfaceData d;
                                        d.name         = cfg.name;
                                        d.friendlyName = cfg.name;
                                        d.macAddresses = cfg.macs;
                                        d.ipv4Subnets  = cfg.ipv4Subnets;
                                        d.ipv6Subnets  = cfg.ipv6Subnets;
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
                        String _name;
                        int    _prio;
        };

        void setMonitorTestIfaces(List<FakeIface> ifaces) {
                SharedState  &s = monitorTestState();
                Mutex::Locker lock(s.mutex);
                s.ifaces = std::move(ifaces);
        }
}

TEST_CASE("NetworkInterfaceMonitor constructs idle and starts/stops cleanly") {
        EventLoop               loop;
        NetworkInterfaceMonitor monitor;
        CHECK_FALSE(monitor.isRunning());
        Error err = monitor.start();
        CHECK(err.isOk());
        CHECK(monitor.isRunning());
        monitor.stop();
        CHECK_FALSE(monitor.isRunning());
}

TEST_CASE("NetworkInterfaceMonitor priming fires no signals") {
        EventLoop               loop;
        NetworkInterfaceMonitor monitor;
        std::atomic<int>        addedCount{0};
        std::atomic<int>        changedCount{0};
        monitor.interfaceAddedSignal.connect(
                [&addedCount](NetworkInterface) { addedCount.fetch_add(1, std::memory_order_relaxed); });
        monitor.interfacesChangedSignal.connect([&changedCount]() { changedCount.fetch_add(1, std::memory_order_relaxed); });
        REQUIRE(monitor.start().isOk());
        // Process anything the loop queued during start (priming
        // shouldn't fire signals; this is just a sanity drain).
        for (int i = 0; i < 5; ++i) loop.processEvents();
        CHECK(addedCount.load() == 0);
        CHECK(changedCount.load() == 0);
        monitor.stop();
}

TEST_CASE("NetworkInterfaceMonitor testForceRescan on a new FakeBackend fires interfaceAdded") {
        EventLoop               loop;
        NetworkInterfaceMonitor monitor;
        REQUIRE(monitor.start().isOk());

        std::atomic<int> added{0};
        std::atomic<int> changed{0};
        bool             sawFakeName = false;
        monitor.interfaceAddedSignal.connect([&added, &sawFakeName](NetworkInterface i) {
                added.fetch_add(1, std::memory_order_relaxed);
                if (i.name() == "monfake0") sawFakeName = true;
        });
        monitor.interfacesChangedSignal.connect([&changed]() { changed.fetch_add(1, std::memory_order_relaxed); });

        FakeIface cfg;
        cfg.name = "monfake0";
        cfg.macs.pushToBack(MacAddress(0x02, 0x10, 0x20, 0x30, 0x40, 0x50));
        cfg.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 11, 12, 1), 24));
        List<FakeIface> ifaces;
        ifaces.pushToBack(cfg);
        setMonitorTestIfaces(ifaces);
        NetworkInterfaceBackend::registerBackend(new MonitorFakeBackend("monfake", 10));

        monitor.testForceRescan();
        CHECK(sawFakeName);
        CHECK(added.load() >= 1);
        CHECK(changed.load() == 1);

        setMonitorTestIfaces(List<FakeIface>());
        NetworkInterfaceBackend::unregisterBackend("monfake");
        monitor.stop();
}

TEST_CASE("NetworkInterfaceMonitor address-set diff fires per-address signals") {
        EventLoop               loop;
        NetworkInterfaceMonitor monitor;
        REQUIRE(monitor.start().isOk());

        std::atomic<int>     added{0};
        std::atomic<int>     removed{0};
        bool                 sawAdded   = false;
        bool                 sawRemoved = false;
        Ipv4Address          addedSeen;
        Ipv4Address          removedSeen;
        monitor.addressAddedIpv4Signal.connect([&](NetworkInterface, Ipv4Address a) {
                added.fetch_add(1, std::memory_order_relaxed);
                sawAdded  = true;
                addedSeen = a;
        });
        monitor.addressRemovedIpv4Signal.connect([&](NetworkInterface, Ipv4Address a) {
                removed.fetch_add(1, std::memory_order_relaxed);
                sawRemoved  = true;
                removedSeen = a;
        });

        FakeIface base;
        base.name = "monfake1";
        base.macs.pushToBack(MacAddress(0x02, 0x99, 0x88, 0x77, 0x66, 0x55));
        base.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 30, 40, 1), 24));
        List<FakeIface> initial;
        initial.pushToBack(base);
        setMonitorTestIfaces(initial);
        NetworkInterfaceBackend::registerBackend(new MonitorFakeBackend("monfake1", 10));
        monitor.testForceRescan();

        // Mutate the same backend's iface list in-place so the
        // registry refreshes the existing impl via replaceData
        // (preserving impl identity) and the monitor sees an
        // address-set diff rather than a full add/remove.
        FakeIface modified = base;
        modified.ipv4Subnets.clear();
        modified.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(192, 168, 77, 1), 24));
        List<FakeIface> updated;
        updated.pushToBack(modified);
        setMonitorTestIfaces(updated);
        monitor.testForceRescan();

        CHECK(sawAdded);
        CHECK(addedSeen == Ipv4Address(192, 168, 77, 1));
        CHECK(sawRemoved);
        CHECK(removedSeen == Ipv4Address(10, 30, 40, 1));

        setMonitorTestIfaces(List<FakeIface>());
        NetworkInterfaceBackend::unregisterBackend("monfake1");
        monitor.stop();
}

TEST_CASE("NetworkInterfaceMonitor handle delivered through signal compares equal to live lookup") {
        EventLoop               loop;
        NetworkInterfaceMonitor monitor;
        REQUIRE(monitor.start().isOk());

        FakeIface cfg;
        cfg.name = "monfake2";
        cfg.macs.pushToBack(MacAddress(0x02, 0x11, 0x22, 0x33, 0x44, 0x55));
        cfg.ipv4Subnets.pushToBack(Ipv4Subnet(Ipv4Address(10, 50, 50, 1), 24));
        List<FakeIface> ifaces;
        ifaces.pushToBack(cfg);
        setMonitorTestIfaces(ifaces);
        NetworkInterfaceBackend::registerBackend(new MonitorFakeBackend("monfake2", 10));

        NetworkInterface fromSignal;
        monitor.interfaceAddedSignal.connect([&fromSignal](NetworkInterface i) {
                if (i.name() == "monfake2") fromSignal = i;
        });
        monitor.testForceRescan();
        REQUIRE(fromSignal.isValid());
        CHECK(fromSignal == NetworkInterface::findByName("monfake2"));

        setMonitorTestIfaces(List<FakeIface>());
        NetworkInterfaceBackend::unregisterBackend("monfake2");
        monitor.stop();
}

TEST_CASE("NetworkInterfaceMonitor anyRunning returns the active monitor") {
        EventLoop loop;
        // Capture the existing global state so we don't break other
        // tests that may have registered monitors first (none today).
        NetworkInterfaceMonitor *before = NetworkInterfaceMonitor::anyRunning();
        NetworkInterfaceMonitor  monitor;
        CHECK(NetworkInterfaceMonitor::anyRunning() == before);
        REQUIRE(monitor.start().isOk());
        CHECK(NetworkInterfaceMonitor::anyRunning() != nullptr);
        monitor.stop();
        CHECK(NetworkInterfaceMonitor::anyRunning() == before);
}
