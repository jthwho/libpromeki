/**
 * @file      st2110vrxmonitor.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>

#include <promeki/duration.h>
#include <promeki/st2110vrxmonitor.h>

using namespace promeki;

TEST_CASE("St2110VrxMonitor: default state is unconfigured + no-op") {
        St2110VrxMonitor m;
        CHECK_FALSE(m.isConfigured());
        // observePacket is a no-op until configured.
        m.observePacket(0, 1500);
        CHECK(m.observedPackets() == 0);
        CHECK(m.occupancyBytes() == 0);
        CHECK(m.peakOccupancyBytes() == 0);
}

TEST_CASE("St2110VrxMonitor: configure + observe a single packet") {
        St2110VrxMonitor m;
        // 100 Mbps drain rate, 4500-byte VRX, 16-packet CMAX.
        m.configure(12'500'000ULL, 4500, 16);
        CHECK(m.isConfigured());
        CHECK(m.vrxFullBytes() == 4500);
        CHECK(m.cmaxPackets() == 16);

        m.observePacket(0, 1500);
        CHECK(m.observedPackets() == 1);
        CHECK(m.observedBytes() == 1500);
        CHECK(m.occupancyBytes() == 1500);
        CHECK(m.peakOccupancyBytes() == 1500);
        CHECK(m.peakBurstPackets() == 1);
        CHECK(m.vrxViolations() == 0);
        CHECK(m.cmaxViolations() == 0);
        CHECK(m.isConformant());
}

TEST_CASE("St2110VrxMonitor: drain reduces occupancy between observations") {
        St2110VrxMonitor m;
        // 1 GB/s drain — 1 ns = 1 byte drained.  Easy mental math.
        m.configure(1'000'000'000ULL, 4500, 16);
        m.observePacket(0, 1500);
        CHECK(m.occupancyBytes() == 1500);
        // 1 µs later: 1000 bytes drained → occupancy = 500.  Then
        // add 1500: occupancy = 2000.
        m.observePacket(1000, 1500);
        CHECK(m.occupancyBytes() == 2000);
        // 10 µs later: 10000 bytes drained — but occupancy is
        // clamped at 0.  Then add 1500.
        m.observePacket(11000, 1500);
        CHECK(m.occupancyBytes() == 1500);
}

TEST_CASE("St2110VrxMonitor: VRX_FULL violation increments the counter") {
        St2110VrxMonitor m;
        // Tiny drain rate so the bucket fills up without leaking.
        m.configure(1'000ULL, 4500, 16);
        m.observePacket(0, 1500);
        m.observePacket(1, 1500);
        m.observePacket(2, 1500); // 4500 — at the bound, not over.
        CHECK(m.vrxViolations() == 0);
        m.observePacket(3, 100); // 4600 — over.
        CHECK(m.vrxViolations() == 1);
        CHECK(m.peakOccupancyBytes() >= 4500);
}

TEST_CASE("St2110VrxMonitor: CMAX violation increments the counter") {
        St2110VrxMonitor m;
        // Set CMAX = 3; default 5 µs burst window — all packets at
        // 1 µs intervals fall inside the same burst.
        m.configure(1'000'000'000ULL, 100'000, 3);
        m.observePacket(0, 1500);    // burst counter = 1
        m.observePacket(1000, 1500); // = 2
        m.observePacket(2000, 1500); // = 3
        CHECK(m.cmaxViolations() == 0);
        m.observePacket(3000, 1500); // = 4 → violation
        CHECK(m.cmaxViolations() == 1);
        CHECK(m.peakBurstPackets() == 4);
        // A gap larger than the burst window resets the counter.
        m.observePacket(3000 + 10'000, 1500); // burst = 1
        m.observePacket(3000 + 11'000, 1500); // burst = 2
        CHECK(m.cmaxViolations() == 1); // still 1, no new violations
}

TEST_CASE("St2110VrxMonitor: reset clears running state but keeps config") {
        St2110VrxMonitor m;
        m.configure(1'000ULL, 4500, 16);
        m.observePacket(0, 1500);
        m.observePacket(1, 1500);
        m.observePacket(2, 1500);
        m.observePacket(3, 100); // violation
        CHECK(m.vrxViolations() == 1);
        CHECK(m.observedPackets() == 4);

        m.reset();
        CHECK(m.vrxViolations() == 0);
        CHECK(m.observedPackets() == 0);
        CHECK(m.peakOccupancyBytes() == 0);
        CHECK(m.occupancyBytes() == 0);
        // Config preserved.
        CHECK(m.isConfigured());
        CHECK(m.vrxFullBytes() == 4500);
        CHECK(m.cmaxPackets() == 16);
}

TEST_CASE("St2110VrxMonitor: non-monotone timestamps clamp to zero advance") {
        St2110VrxMonitor m;
        m.configure(1'000'000'000ULL, 10'000, 16);
        m.observePacket(1000, 1500);
        CHECK(m.occupancyBytes() == 1500);
        // Backwards timestamp — drain is clamped at 0, packet still added.
        m.observePacket(500, 1500);
        CHECK(m.occupancyBytes() == 3000);
}

TEST_CASE("St2110VrxMonitor: zero-size packets are ignored") {
        St2110VrxMonitor m;
        m.configure(1'000'000'000ULL, 10'000, 16);
        m.observePacket(0, 0);
        m.observePacket(1000, -5);
        CHECK(m.observedPackets() == 0);
        CHECK(m.occupancyBytes() == 0);
}

TEST_CASE("St2110VrxMonitor: isConformant tracks any violation") {
        St2110VrxMonitor m;
        m.configure(1'000ULL, 4500, 3);
        m.observePacket(0, 1500);
        m.observePacket(1, 1500);
        m.observePacket(2, 1500);
        CHECK(m.isConformant());
        m.observePacket(3, 100); // VRX violation
        CHECK_FALSE(m.isConformant());
}

TEST_CASE("St2110VrxMonitor: custom burst window") {
        St2110VrxMonitor m;
        // 100 µs burst window — anything outside is a separate burst.
        m.configure(1'000'000'000ULL, 100'000, 3, Duration::fromMicroseconds(100));
        m.observePacket(0, 1500);
        m.observePacket(50'000, 1500);  // 50 µs gap → same burst (= 2)
        m.observePacket(100'000, 1500); // 50 µs gap → still same burst (= 3)
        CHECK(m.peakBurstPackets() == 3);
        m.observePacket(300'000, 1500); // 200 µs gap → new burst (= 1)
        CHECK(m.peakBurstPackets() == 3);
}
