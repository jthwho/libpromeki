/**
 * @file      tests/unit/numahostbufferimpl.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Tests pass on UMA boxes (the allocator falls back to plain page-
 * aligned host memory) as well as real NUMA hardware (specific-node
 * binding via mbind).
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/memdomain.h>
#include <promeki/memspace.h>
#include <promeki/numa.h>
#include <promeki/numahostbufferimpl.h>

using namespace promeki;

TEST_CASE("MemSpace::NumaHost (default-node) is registered") {
        MemSpace ms(MemSpace::NumaHost);
        CHECK(ms.id() == MemSpace::NumaHost);
        CHECK(ms.name() == "NumaHost");
        CHECK(ms.domain().id() == MemDomain::Host);
}

TEST_CASE("Buffer(NumaHost): construct, write, read") {
        Buffer a(64 * 1024, Buffer::DefaultAlign, MemSpace::NumaHost);
        REQUIRE(a.isValid());
        REQUIRE(a.data() != nullptr);
        CHECK(a.allocSize() >= 64 * 1024);
        CHECK(a.memSpace().id() == MemSpace::NumaHost);

        std::memset(a.data(), 0x77, a.allocSize());
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0x77);
        CHECK(static_cast<unsigned char *>(a.data())[a.allocSize() - 1] == 0x77);
}

TEST_CASE("Buffer(NumaHost): copy = refcount, ensureExclusive deep-copies") {
        Buffer a(8192, Buffer::DefaultAlign, MemSpace::NumaHost);
        REQUIRE(a.isValid());
        std::memset(a.data(), 0x44, a.allocSize());

        Buffer b = a;
        REQUIRE(b.isValid());
        CHECK(b.data() == a.data());

        b.ensureExclusive();
        REQUIRE(b.isValid());
        CHECK(b.data() != a.data());
        CHECK(b.memSpace().id() == MemSpace::NumaHost);
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0x44);

        std::memset(b.data(), 0x55, b.allocSize());
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0x44);
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0x55);
}

TEST_CASE("Buffer(NumaHost): seal is no-op success (not a CoW backend)") {
        Buffer a(4096, Buffer::DefaultAlign, MemSpace::NumaHost);
        REQUIRE(a.isValid());
        CHECK(a.seal() == Error::Ok);
        CHECK(a.seal() == Error::Ok);
        CHECK_FALSE(a.isCowBacked());
}

TEST_CASE("NumaHost::forNode(negative) collapses to default NumaHost MemSpace") {
        MemSpace ms = NumaHost::forNode(Numa::NodeAny);
        CHECK(ms.id() == MemSpace::NumaHost);
}

TEST_CASE("NumaHost::forNode caches per-node MemSpace IDs") {
        // Two calls for the same node return the same MemSpace ID;
        // calls for different nodes return different IDs.  The IDs
        // for specific nodes are dynamic (>= UserDefined) — they
        // don't collide with the static built-in IDs.
        MemSpace ms0a = NumaHost::forNode(0);
        MemSpace ms0b = NumaHost::forNode(0);
        MemSpace ms1  = NumaHost::forNode(1);
        CHECK(ms0a.id() == ms0b.id());
        CHECK(ms0a.id() != ms1.id());
        CHECK(ms0a.id() >= MemSpace::UserDefined);
        CHECK(ms1.id() >= MemSpace::UserDefined);
        CHECK(ms0a.name() == "NumaHost_Node0");
        CHECK(ms1.name() == "NumaHost_Node1");
}

TEST_CASE("Buffer via NumaHost::forNode(0): allocates and round-trips") {
        // Node 0 is universal — exists on UMA and NUMA boxes alike,
        // so the allocation always succeeds.  On a UMA box, mbind is
        // a soft fail (warning, no binding) but the buffer remains
        // valid; on a NUMA box, mbind binds to node 0.
        MemSpace ms = NumaHost::forNode(0);
        Buffer   a(16 * 1024, Buffer::DefaultAlign, ms);
        REQUIRE(a.isValid());
        CHECK(a.memSpace().id() == ms.id());
        CHECK(a.memSpace().name() == "NumaHost_Node0");
        std::memset(a.data(), 0xCC, a.allocSize());
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0xCC);
}

TEST_CASE("MemSpace::NumaHost stats reflect alloc / release") {
        MemSpace ms(MemSpace::NumaHost);
        ms.resetStats();
        const auto before = ms.statsSnapshot();
        {
                Buffer a(8192, Buffer::DefaultAlign, MemSpace::NumaHost);
                REQUIRE(a.isValid());
                const auto live = ms.statsSnapshot();
                CHECK(live.allocCount == before.allocCount + 1);
                CHECK(live.allocBytes >= before.allocBytes + 8192);
                CHECK(live.liveCount == before.liveCount + 1);
        }
        const auto after = ms.statsSnapshot();
        CHECK(after.releaseCount >= before.releaseCount + 1);
        CHECK(after.liveCount == before.liveCount);
}
