/**
 * @file      tests/unit/numa.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests are written so they pass on UMA boxes (laptops, single-socket
 * workstations, CI runners) as well as real NUMA hardware.  Anything
 * that requires a multi-node topology branches on
 * @c Numa::isAvailable().
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/numa.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("Numa::isAvailable returns a sensible answer") {
        // Doesn't crash and returns either true (multi-node box) or
        // false (UMA / non-Linux / NUMA-disabled kernel).
        bool avail = Numa::isAvailable();
        // Tautology — we just care that the call returned without
        // crashing and returned a bool.
        CHECK((avail || !avail));
}

TEST_CASE("Numa::nodeCount is at least 1") {
        // The contract: the API behaves as if there's always at
        // least one node.  On UMA boxes that "node" is logical;
        // allocations route through the plain page-aligned path.
        CHECK(Numa::nodeCount() >= 1);
}

TEST_CASE("Numa::maxNode is non-negative") {
        CHECK(Numa::maxNode() >= 0);
        CHECK(Numa::maxNode() + 1 >= Numa::nodeCount());
}

TEST_CASE("Numa::allocOnNode(NodeAny) returns writable memory") {
        constexpr size_t kBytes = 64 * 1024;
        void *p = Numa::allocOnNode(kBytes, Numa::NodeAny);
        REQUIRE(p != nullptr);

        // Write a pattern across the whole region and read it back.
        // On a non-NUMA stub this exercises the posix_memalign path;
        // on Linux it exercises mmap + (no mbind, since NodeAny).
        std::memset(p, 0xA5, kBytes);
        unsigned char *bytes = static_cast<unsigned char *>(p);
        CHECK(bytes[0] == 0xA5);
        CHECK(bytes[kBytes / 2] == 0xA5);
        CHECK(bytes[kBytes - 1] == 0xA5);

        Error err = Numa::free(p, kBytes);
        CHECK(err.isOk());
}

TEST_CASE("Numa::allocOnNode(0 bytes) returns null") {
        void *p = Numa::allocOnNode(0, Numa::NodeAny);
        CHECK(p == nullptr);
}

TEST_CASE("Numa::free(nullptr, 0) is a clean no-op") {
        Error err = Numa::free(nullptr, 0);
        CHECK(err.isOk());
}

TEST_CASE("Numa::allocOnNode(node = 0) succeeds when NUMA is available") {
        if (!Numa::isAvailable()) {
                INFO("UMA box; specific-node allocation falls through to default path");
        }
        // Whether or not isAvailable() is true, node 0 always exists
        // (it's the kernel's universal default node) and the
        // allocator must produce writable memory.
        constexpr size_t kBytes = 16 * 1024;
        void *p = Numa::allocOnNode(kBytes, 0);
        REQUIRE(p != nullptr);
        std::memset(p, 0x33, kBytes);
        CHECK(static_cast<unsigned char *>(p)[0] == 0x33);
        CHECK(Numa::free(p, kBytes).isOk());
}

TEST_CASE("Numa::nodeOfNic returns NodeAny for a non-existent interface") {
        // Bogus interface name → NodeAny.  Tests the soft-fail path
        // where /sys/class/net/<bogus>/device/numa_node doesn't open.
        CHECK(Numa::nodeOfNic(String("definitely_not_an_interface_xyz123"))
              == Numa::NodeAny);
}

TEST_CASE("Numa::nodeOfNic returns NodeAny for empty interface name") {
        CHECK(Numa::nodeOfNic(String()) == Numa::NodeAny);
}

TEST_CASE("Numa::nodeOfNic for loopback returns NodeAny") {
        // Loopback (lo) on Linux has no PCI device, so the
        // device/numa_node path doesn't exist.  Result: NodeAny.
        // This is the documented behaviour for non-PCI interfaces.
        // On non-Linux platforms the stub already returns NodeAny.
        CHECK(Numa::nodeOfNic(String("lo")) == Numa::NodeAny);
}

TEST_CASE("Numa::nodeOfCpu(invalid) returns NodeAny") {
        CHECK(Numa::nodeOfCpu(-1) == Numa::NodeAny);
        CHECK(Numa::nodeOfCpu(99999) == Numa::NodeAny);
}

TEST_CASE("Numa::currentNode returns NodeAny on UMA, valid node on NUMA") {
        int n = Numa::currentNode();
        if (Numa::isAvailable()) {
                // On a real NUMA box, the current thread is running
                // on some CPU that lives on some node.
                CHECK(n >= 0);
                CHECK(n <= Numa::maxNode());
        } else {
                CHECK(n == Numa::NodeAny);
        }
}
