/**
 * @file      memspace.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/memspace.h>

using namespace promeki;

TEST_CASE("MemSpace: default construction") {
        MemSpace ms;
        // Default should be System/Default
        CHECK(true); // Construction doesn't crash
}

TEST_CASE("MemSpace: construction with System ID") {
        MemSpace ms(MemSpace::System);
        CHECK(true);
}

TEST_CASE("MemSpace: alloc and release") {
        MemSpace ms;
        MemAllocation a = ms.alloc(256, 16);
        CHECK(a.isValid());
        CHECK(a.size == 256);
        CHECK(a.align == 16);
        ms.release(a);
        CHECK(!a.isValid());
}

TEST_CASE("MemSpace: alloc alignment") {
        MemSpace ms;
        MemAllocation a = ms.alloc(1024, 64);
        CHECK(a.isValid());
        CHECK((reinterpret_cast<uintptr_t>(a.ptr) % 64) == 0);
        ms.release(a);
}

TEST_CASE("MemSpace: release nullptr is safe") {
        MemSpace ms;
        MemAllocation a;
        ms.release(a);
        CHECK(true);
}

TEST_CASE("MemSpace: fill memory") {
        MemSpace ms;
        MemAllocation a = ms.alloc(128, 16);
        REQUIRE(a.isValid());
        CHECK(ms.fill(a.ptr, 128, 0xAA).isOk());
        unsigned char *bytes = static_cast<unsigned char *>(a.ptr);
        CHECK(bytes[0] == 0xAA);
        CHECK(bytes[127] == 0xAA);
        ms.release(a);
}

TEST_CASE("MemSpace: fill with nullptr returns error") {
        MemSpace ms;
        CHECK(ms.fill(nullptr, 128, 0).isError());
}

TEST_CASE("MemSpace: copy") {
        MemSpace ms;
        MemAllocation src = ms.alloc(128, 16);
        MemAllocation dst = ms.alloc(128, 16);
        REQUIRE(src.isValid());
        REQUIRE(dst.isValid());
        CHECK(ms.fill(src.ptr, 128, 0x42).isOk());
        CHECK(ms.copy(src, dst, 128));
        unsigned char *d = static_cast<unsigned char *>(dst.ptr);
        CHECK(d[0] == 0x42);
        CHECK(d[127] == 0x42);
        ms.release(src);
        ms.release(dst);
}

TEST_CASE("MemSpace: copy with nullptr returns false") {
        MemSpace ms;
        MemAllocation a = ms.alloc(64, 16);
        MemAllocation empty;
        REQUIRE(a.isValid());
        CHECK_FALSE(ms.copy(empty, a, 64));
        CHECK_FALSE(ms.copy(a, empty, 64));
        ms.release(a);
}

TEST_CASE("MemSpace: alloc preserves MemSpace") {
        MemSpace ms(MemSpace::SystemSecure);
        MemAllocation a = ms.alloc(256, 16);
        REQUIRE(a.isValid());
        CHECK(a.ms.id() == MemSpace::SystemSecure);
        ms.release(a);
}

TEST_CASE("MemSpace: isHostAccessible") {
        MemSpace ms;
        MemAllocation a = ms.alloc(64, 16);
        REQUIRE(a.isValid());
        CHECK(ms.isHostAccessible(a));
        ms.release(a);
}

// ── TypeRegistry: registerType() / registerData() ─────────────────

TEST_CASE("MemSpace: registerType returns unique IDs above UserDefined") {
        MemSpace::ID id1 = MemSpace::registerType();
        MemSpace::ID id2 = MemSpace::registerType();
        CHECK(id1 >= MemSpace::UserDefined);
        CHECK(id2 >= MemSpace::UserDefined);
        CHECK(id1 != id2);
}

// ── Stats ─────────────────────────────────────────────────────────

TEST_CASE("MemSpace::Stats: alloc and release update counters") {
        // Use a private custom MemSpace so the test isn't affected
        // by other code allocating in the global System space.
        MemSpace::ID id = MemSpace::registerType();
        MemSpace::Ops ops;
        ops.id = id;
        ops.name = "StatsTest1";
        ops.isHostAccessible = [](const MemAllocation &) -> bool { return true; };
        ops.alloc = [](MemAllocation &a) {
                size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                a.ptr = std::aligned_alloc(a.align, allocSize);
        };
        ops.release = [](MemAllocation &a) { std::free(a.ptr); a.ptr = nullptr; };
        ops.copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                std::memcpy(dst.ptr, src.ptr, bytes);
                return true;
        };
        ops.fill = [](void *ptr, size_t bytes, char value) -> Error {
                std::memset(ptr, value, bytes);
                return Error::Ok;
        };
        MemSpace::registerData(std::move(ops));

        MemSpace ms(id);
        MemSpace::Stats::Snapshot before = ms.statsSnapshot();
        CHECK(before.allocCount == 0);
        CHECK(before.allocBytes == 0);
        CHECK(before.liveCount == 0);
        CHECK(before.liveBytes == 0);
        CHECK(before.peakCount == 0);
        CHECK(before.peakBytes == 0);
        CHECK(before.maxAllocBytes == 0);

        MemAllocation a = ms.alloc(256, 16);
        REQUIRE(a.isValid());

        MemSpace::Stats::Snapshot afterAlloc = ms.statsSnapshot();
        CHECK(afterAlloc.allocCount == 1);
        CHECK(afterAlloc.allocBytes == 256);
        CHECK(afterAlloc.liveCount == 1);
        CHECK(afterAlloc.liveBytes == 256);
        CHECK(afterAlloc.peakCount == 1);
        CHECK(afterAlloc.peakBytes == 256);
        CHECK(afterAlloc.maxAllocBytes == 256);

        ms.release(a);

        MemSpace::Stats::Snapshot afterRelease = ms.statsSnapshot();
        CHECK(afterRelease.releaseCount == 1);
        CHECK(afterRelease.releaseBytes == 256);
        CHECK(afterRelease.liveCount == 0);
        CHECK(afterRelease.liveBytes == 0);
        // Peak should persist across the release.
        CHECK(afterRelease.peakCount == 1);
        CHECK(afterRelease.peakBytes == 256);
}

TEST_CASE("MemSpace::Stats: peak tracks high-water mark") {
        MemSpace::ID id = MemSpace::registerType();
        MemSpace::Ops ops;
        ops.id = id;
        ops.name = "StatsTest2";
        ops.isHostAccessible = [](const MemAllocation &) -> bool { return true; };
        ops.alloc = [](MemAllocation &a) {
                size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                a.ptr = std::aligned_alloc(a.align, allocSize);
        };
        ops.release = [](MemAllocation &a) { std::free(a.ptr); a.ptr = nullptr; };
        ops.copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                std::memcpy(dst.ptr, src.ptr, bytes);
                return true;
        };
        ops.fill = [](void *ptr, size_t bytes, char value) -> Error {
                std::memset(ptr, value, bytes);
                return Error::Ok;
        };
        MemSpace::registerData(std::move(ops));

        MemSpace ms(id);

        MemAllocation a = ms.alloc(128, 16);
        MemAllocation b = ms.alloc(512, 16);
        MemAllocation c = ms.alloc(64, 16);
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        REQUIRE(c.isValid());

        MemSpace::Stats::Snapshot peak = ms.statsSnapshot();
        CHECK(peak.liveCount == 3);
        CHECK(peak.liveBytes == 128 + 512 + 64);
        CHECK(peak.peakCount == 3);
        CHECK(peak.peakBytes == 128 + 512 + 64);
        CHECK(peak.maxAllocBytes == 512);

        ms.release(b);
        ms.release(c);

        MemSpace::Stats::Snapshot later = ms.statsSnapshot();
        CHECK(later.liveCount == 1);
        CHECK(later.liveBytes == 128);
        // Peaks should not have decreased.
        CHECK(later.peakCount == 3);
        CHECK(later.peakBytes == 128 + 512 + 64);

        // A smaller follow-up allocation should not lower the
        // running max-alloc watermark.
        MemAllocation d = ms.alloc(32, 16);
        REQUIRE(d.isValid());
        CHECK(ms.statsSnapshot().maxAllocBytes == 512);

        ms.release(a);
        ms.release(d);
}

TEST_CASE("MemSpace::Stats: copy and fill update counters") {
        MemSpace::ID id = MemSpace::registerType();
        MemSpace::Ops ops;
        ops.id = id;
        ops.name = "StatsTest3";
        ops.isHostAccessible = [](const MemAllocation &) -> bool { return true; };
        ops.alloc = [](MemAllocation &a) {
                size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                a.ptr = std::aligned_alloc(a.align, allocSize);
        };
        ops.release = [](MemAllocation &a) { std::free(a.ptr); a.ptr = nullptr; };
        ops.copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                std::memcpy(dst.ptr, src.ptr, bytes);
                return true;
        };
        ops.fill = [](void *ptr, size_t bytes, char value) -> Error {
                std::memset(ptr, value, bytes);
                return Error::Ok;
        };
        MemSpace::registerData(std::move(ops));

        MemSpace ms(id);
        MemAllocation src = ms.alloc(256, 16);
        MemAllocation dst = ms.alloc(256, 16);
        REQUIRE(src.isValid());
        REQUIRE(dst.isValid());

        CHECK(ms.fill(src.ptr, 256, 0x11).isOk());
        CHECK(ms.fill(dst.ptr, 128, 0x22).isOk());
        CHECK(ms.copy(src, dst, 256));

        MemSpace::Stats::Snapshot s = ms.statsSnapshot();
        CHECK(s.fillCount == 2);
        CHECK(s.fillBytes == 256 + 128);
        CHECK(s.copyCount == 1);
        CHECK(s.copyBytes == 256);
        CHECK(s.copyFailCount == 0);

        ms.release(src);
        ms.release(dst);
}

TEST_CASE("MemSpace::Stats: reset zeroes every counter") {
        MemSpace::ID id = MemSpace::registerType();
        MemSpace::Ops ops;
        ops.id = id;
        ops.name = "StatsTest4";
        ops.isHostAccessible = [](const MemAllocation &) -> bool { return true; };
        ops.alloc = [](MemAllocation &a) {
                size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                a.ptr = std::aligned_alloc(a.align, allocSize);
        };
        ops.release = [](MemAllocation &a) { std::free(a.ptr); a.ptr = nullptr; };
        ops.copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                std::memcpy(dst.ptr, src.ptr, bytes);
                return true;
        };
        ops.fill = [](void *ptr, size_t bytes, char value) -> Error {
                std::memset(ptr, value, bytes);
                return Error::Ok;
        };
        MemSpace::registerData(std::move(ops));

        MemSpace ms(id);
        MemAllocation a = ms.alloc(1024, 16);
        REQUIRE(a.isValid());
        CHECK(ms.fill(a.ptr, 1024, 0).isOk());
        ms.release(a);

        MemSpace::Stats::Snapshot before = ms.statsSnapshot();
        CHECK(before.allocCount == 1);
        CHECK(before.releaseCount == 1);
        CHECK(before.fillCount == 1);
        CHECK(before.peakBytes == 1024);

        ms.resetStats();

        MemSpace::Stats::Snapshot after = ms.statsSnapshot();
        CHECK(after.allocCount == 0);
        CHECK(after.allocBytes == 0);
        CHECK(after.releaseCount == 0);
        CHECK(after.releaseBytes == 0);
        CHECK(after.fillCount == 0);
        CHECK(after.fillBytes == 0);
        CHECK(after.copyCount == 0);
        CHECK(after.copyBytes == 0);
        CHECK(after.liveCount == 0);
        CHECK(after.liveBytes == 0);
        CHECK(after.peakCount == 0);
        CHECK(after.peakBytes == 0);
        CHECK(after.maxAllocBytes == 0);
}

TEST_CASE("MemSpace::Stats: fill with nullptr doesn't count") {
        MemSpace ms(MemSpace::System);
        MemSpace::Stats::Snapshot before = ms.statsSnapshot();
        CHECK(ms.fill(nullptr, 128, 0).isError());
        MemSpace::Stats::Snapshot after = ms.statsSnapshot();
        CHECK(after.fillCount == before.fillCount);
        CHECK(after.fillBytes == before.fillBytes);
}

TEST_CASE("MemSpace::Stats: all built-in MemSpaces expose stats") {
        for(MemSpace::ID id : MemSpace::registeredIDs()) {
                MemSpace ms(id);
                // Must not crash and must return plausible values.
                MemSpace::Stats::Snapshot s = ms.statsSnapshot();
                (void)s;
                // Sanity: live count should never exceed alloc count.
                CHECK(s.liveCount <= s.allocCount);
        }
}

TEST_CASE("MemSpace: registerData and construction from custom ID") {
        MemSpace::ID id = MemSpace::registerType();

        // A custom memory space that delegates entirely to System memory,
        // but reports a unique ID so we can verify registration.
        MemSpace::Ops ops;
        ops.id   = id;
        ops.name = "TestSpace";
        ops.isHostAccessible = [](const MemAllocation &) -> bool { return true; };
        ops.alloc = [](MemAllocation &a) {
                size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                a.ptr = std::aligned_alloc(a.align, allocSize);
        };
        ops.release = [](MemAllocation &a) {
                std::free(a.ptr);
                a.ptr = nullptr;
        };
        ops.copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                std::memcpy(dst.ptr, src.ptr, bytes);
                return true;
        };
        ops.fill = [](void *ptr, size_t bytes, char value) -> Error {
                std::memset(ptr, value, bytes);
                return Error::Ok;
        };

        MemSpace::registerData(std::move(ops));

        MemSpace ms(id);
        CHECK(ms.id() == id);
        CHECK(ms.name() == "TestSpace");

        MemAllocation a = ms.alloc(128, 16);
        REQUIRE(a.isValid());
        CHECK(ms.isHostAccessible(a));
        CHECK(ms.fill(a.ptr, 128, 0x5A).isOk());
        unsigned char *bytes = static_cast<unsigned char *>(a.ptr);
        CHECK(bytes[0] == 0x5A);
        CHECK(bytes[127] == 0x5A);
        ms.release(a);
        CHECK_FALSE(a.isValid());
}
