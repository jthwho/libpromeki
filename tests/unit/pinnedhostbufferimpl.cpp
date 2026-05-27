/**
 * @file      tests/unit/pinnedhostbufferimpl.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/bufferview.h>
#include <promeki/memdomain.h>
#include <promeki/memspace.h>

using namespace promeki;

TEST_CASE("MemSpace::PinnedHost is registered") {
        MemSpace ms(MemSpace::PinnedHost);
        CHECK(ms.id() == MemSpace::PinnedHost);
        CHECK(ms.name() == "PinnedHost");
        CHECK(ms.domain().id() == MemDomain::Host);
}

TEST_CASE("Buffer(PinnedHost): construct, write, read") {
        Buffer a(64 * 1024, Buffer::DefaultAlign, MemSpace::PinnedHost);
        REQUIRE(a.isValid());
        REQUIRE(a.data() != nullptr);
        CHECK(a.allocSize() >= 64 * 1024);
        CHECK(a.memSpace().id() == MemSpace::PinnedHost);

        // Round-trip pattern.
        std::memset(a.data(), 0x5A, a.allocSize());
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0x5A);
        CHECK(static_cast<unsigned char *>(a.data())[a.allocSize() - 1] == 0x5A);
}

TEST_CASE("Buffer(PinnedHost): copy = refcount, ensureExclusive deep-copies") {
        Buffer a(4096, Buffer::DefaultAlign, MemSpace::PinnedHost);
        REQUIRE(a.isValid());
        std::memset(a.data(), 0x11, a.allocSize());

        Buffer b = a; // refcount bump, same backing
        REQUIRE(b.isValid());
        CHECK(b.data() == a.data());

        // Detach b from the shared impl — should produce a fresh
        // PinnedHost-backed allocation with the same content.
        b.ensureExclusive();
        REQUIRE(b.isValid());
        CHECK(b.data() != a.data());
        CHECK(b.memSpace().id() == MemSpace::PinnedHost);
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0x11);

        // Mutating b does not bleed back into a.
        std::memset(b.data(), 0x22, b.allocSize());
        CHECK(static_cast<unsigned char *>(a.data())[0] == 0x11);
        CHECK(static_cast<unsigned char *>(b.data())[0] == 0x22);
}

TEST_CASE("Buffer(PinnedHost): seal is no-op success (not a CoW backend)") {
        // PinnedHost has no producer→post-producer transition, so
        // seal() returns Ok and isCowBacked() reports false — same
        // as plain HostBufferImpl.
        Buffer a(4096, Buffer::DefaultAlign, MemSpace::PinnedHost);
        REQUIRE(a.isValid());
        CHECK(a.seal() == Error::Ok);
        CHECK(a.seal() == Error::Ok); // idempotent
        CHECK_FALSE(a.isCowBacked());
}

TEST_CASE("Buffer(PinnedHost): zero-byte allocation reports invalid") {
        // Buffer::isValid is keyed on allocSize > 0; the underlying
        // pointer may be implementation-defined (glibc's aligned_alloc
        // returns a unique non-null pointer for size 0), so we only
        // assert the framework-visible invariants.
        Buffer a(0, Buffer::DefaultAlign, MemSpace::PinnedHost);
        CHECK_FALSE(a.isValid());
        CHECK(a.allocSize() == 0);
}

TEST_CASE("MemSpace::PinnedHost stats reflect the alloc / release") {
        MemSpace ms(MemSpace::PinnedHost);
        ms.resetStats();
        const auto before = ms.statsSnapshot();
        {
                Buffer a(8192, Buffer::DefaultAlign, MemSpace::PinnedHost);
                REQUIRE(a.isValid());
                const auto live = ms.statsSnapshot();
                CHECK(live.allocCount == before.allocCount + 1);
                CHECK(live.allocBytes >= before.allocBytes + 8192);
                CHECK(live.liveCount == before.liveCount + 1);
        }
        // Buffer dropped — release should have ticked.
        const auto after = ms.statsSnapshot();
        CHECK(after.releaseCount >= before.releaseCount + 1);
        CHECK(after.liveCount == before.liveCount);
}
