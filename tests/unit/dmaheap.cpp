/**
 * @file      tests/unit/dmaheap.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <unistd.h>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/config.h>
#include <promeki/dmaheap.h>
#include <promeki/memdomain.h>
#include <promeki/memspace.h>

using namespace promeki;

// ============================================================================
// Identity / registration — independent of whether any heap device exists.
// ============================================================================

TEST_CASE("DmaHeap::systemHeap maps to the built-in Dmabuf MemSpace") {
        MemSpace sys = DmaHeap::systemHeap();
        CHECK(sys.id() == MemSpace::Dmabuf);
        CHECK(sys.domain().id() == MemDomain::Dmabuf);
}

TEST_CASE("DmaHeap::forHeap is memoized and per-name") {
        // "system" always resolves to the built-in id.
        CHECK(DmaHeap::forHeap("system").id() == MemSpace::Dmabuf);

        if (DmaHeap::isSupported()) {
                MemSpace a1 = DmaHeap::forHeap("promeki-test-heap-a");
                MemSpace a2 = DmaHeap::forHeap("promeki-test-heap-a");
                MemSpace bb = DmaHeap::forHeap("promeki-test-heap-b");
                CHECK(a1.id() == a2.id());          // memoized
                CHECK(a1.id() != bb.id());          // per-name
                CHECK(a1.id() != MemSpace::Dmabuf); // distinct from the system default
                CHECK(a1.domain().id() == MemDomain::Dmabuf);
                CHECK(a1.name() == "DmaHeap:promeki-test-heap-a");
        }
}

// ============================================================================
// allocate() failure paths — exercised on every build.
// ============================================================================

TEST_CASE("DmaHeap::allocate reports failure cleanly") {
        // A name with a path separator is rejected on functional builds;
        // on unsupported builds every name short-circuits to NotSupported.
        Error err = Error::Ok;
        int   fd = DmaHeap::allocate("bad/name", 4096, &err);
        CHECK(fd < 0);
        CHECK(err == (DmaHeap::isSupported() ? Error::Invalid : Error::NotSupported));

        // A heap that does not exist.
        err = Error::Ok;
        fd = DmaHeap::allocate("promeki-no-such-heap", 4096, &err);
        CHECK(fd < 0);
        CHECK(err == (DmaHeap::isSupported() ? Error::NotFound : Error::NotSupported));
}

// ============================================================================
// Real allocation — only when the running kernel exposes a heap.
// ============================================================================

TEST_CASE("DmaHeap::isHeapAvailable handles bad and missing heaps") {
        CHECK_FALSE(DmaHeap::isHeapAvailable("bad/name"));
        CHECK_FALSE(DmaHeap::isHeapAvailable("promeki-no-such-heap"));
        // Consistency: a usable system heap implies isAvailable().
        if (DmaHeap::isHeapAvailable(DmaHeap::SystemHeapName)) CHECK(DmaHeap::isAvailable());
}

TEST_CASE("DmaHeap allocation round-trips when a heap is available") {
        constexpr size_t kSize = 64 * 1024;

        // isHeapAvailable() returns false when the system heap is absent
        // or unopenable (e.g. root-only on a dev box), so this skips
        // cleanly rather than failing.
        if (!DmaHeap::isHeapAvailable(DmaHeap::SystemHeapName)) {
                MESSAGE("skipping: system heap not available (absent or permission denied)");
                return;
        }

        // Allocate through the built-in MemSpace::Dmabuf (system heap).
        Buffer b(kSize, Buffer::DefaultAlign, MemSpace::Dmabuf);
        REQUIRE(b.isValid());
        CHECK(b.memSpace().id() == MemSpace::Dmabuf);
        CHECK(b.memSpace().domain().id() == MemDomain::Dmabuf);
        CHECK(b.dmabufFd() >= 0);
        CHECK(b.allocSize() >= kSize);

        // The allocated dma-buf is host-mappable and writable.
        BufferRequest req = b.mapAcquire(MemDomain::Host, MapFlags::ReadWrite);
        REQUIRE(req.wait() == Error::Ok);
        REQUIRE(b.data() != nullptr);
        std::memset(b.data(), 0x5A, kSize);
        CHECK(b.mapRelease(MemDomain::Host).wait() == Error::Ok);

        // Low-level allocate() yields a raw fd the caller owns.
        Error err = Error::Ok;
        int   fd = DmaHeap::allocate(DmaHeap::SystemHeapName, kSize, &err);
        CHECK(err == Error::Ok);
        REQUIRE(fd >= 0);
        ::close(fd);

        // The system heap shows up in the enumeration.
        bool hasSystem = false;
        for (const String &h : DmaHeap::availableHeaps()) {
                if (h == "system") hasSystem = true;
        }
        CHECK(hasSystem);
}
