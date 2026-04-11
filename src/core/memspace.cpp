/**
 * @file      memspace.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <promeki/memspace.h>
#include <promeki/securemem.h>
#include <promeki/atomic.h>
#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MemSpace)

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered types
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{MemSpace::UserDefined};

MemSpace::ID MemSpace::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// MemSpace::Stats
// ---------------------------------------------------------------------------

MemSpace::Stats::Snapshot MemSpace::Stats::snapshot() const {
        Snapshot s;
        s.allocCount     = allocCount.value();
        s.allocBytes     = allocBytes.value();
        s.allocFailCount = allocFailCount.value();
        s.maxAllocBytes  = maxAllocBytes.value();
        s.releaseCount   = releaseCount.value();
        s.releaseBytes   = releaseBytes.value();
        s.copyCount      = copyCount.value();
        s.copyBytes      = copyBytes.value();
        s.copyFailCount  = copyFailCount.value();
        s.fillCount      = fillCount.value();
        s.fillBytes      = fillBytes.value();
        s.liveCount      = liveCount.value();
        s.liveBytes      = liveBytes.value();
        s.peakCount      = peakCount.value();
        s.peakBytes      = peakBytes.value();
        return s;
}

void MemSpace::Stats::reset() {
        allocCount.setValue(0);
        allocBytes.setValue(0);
        allocFailCount.setValue(0);
        maxAllocBytes.setValue(0);
        releaseCount.setValue(0);
        releaseBytes.setValue(0);
        copyCount.setValue(0);
        copyBytes.setValue(0);
        copyFailCount.setValue(0);
        fillCount.setValue(0);
        fillBytes.setValue(0);
        liveCount.setValue(0);
        liveBytes.setValue(0);
        peakCount.setValue(0);
        peakBytes.setValue(0);
}

void MemSpace::Stats::recordAlloc(uint64_t bytes) {
        allocCount.fetchAndAdd(1);
        allocBytes.fetchAndAdd(bytes);

        // Update the max-single-alloc watermark via CAS.
        uint64_t prevMax = maxAllocBytes.value();
        while(bytes > prevMax && !maxAllocBytes.compareAndSwap(prevMax, bytes)) {
                // prevMax is updated by compareAndSwap on failure.
        }

        // fetch_add returns the old value; the new live value is old+delta.
        const uint64_t newLiveCount = liveCount.fetchAndAdd(1) + 1;
        const uint64_t newLiveBytes = liveBytes.fetchAndAdd(bytes) + bytes;

        uint64_t prevPeakCount = peakCount.value();
        while(newLiveCount > prevPeakCount
              && !peakCount.compareAndSwap(prevPeakCount, newLiveCount)) {
                // prevPeakCount updated by compareAndSwap on failure.
        }
        uint64_t prevPeakBytes = peakBytes.value();
        while(newLiveBytes > prevPeakBytes
              && !peakBytes.compareAndSwap(prevPeakBytes, newLiveBytes)) {
                // prevPeakBytes updated by compareAndSwap on failure.
        }
}

void MemSpace::Stats::recordRelease(uint64_t bytes) {
        releaseCount.fetchAndAdd(1);
        releaseBytes.fetchAndAdd(bytes);
        liveCount.fetchAndSub(1);
        liveBytes.fetchAndSub(bytes);
}

// ---------------------------------------------------------------------------
// Construct-on-first-use registry
// ---------------------------------------------------------------------------

struct MemSpaceRegistry {
        Map<MemSpace::ID, MemSpace::Ops> entries;

        // Stats are allocated with `new` and their addresses are
        // stashed in Ops::stats; the registry is a function-local
        // static that lives for the process lifetime, so the Stats
        // objects are intentionally never freed.
        static MemSpace::Stats *makeStats() { return new MemSpace::Stats(); }

        MemSpaceRegistry() {
                entries[MemSpace::System] = {
                        .id = MemSpace::System,
                        .name = "System",
                        .isHostAccessible = [](const MemAllocation &) -> bool { return true; },
                        .alloc = [](MemAllocation &a) -> void {
                                size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                                a.ptr = std::aligned_alloc(a.align, allocSize);
                                PROMEKI_ASSERT(a.ptr != nullptr);
                                promekiDebug("%p: system allocate %d (aligned %d), align %d", a.ptr, (int)a.size, (int)allocSize, (int)a.align);
                        },
                        .release = [](MemAllocation &a) -> void {
                                promekiDebug("%p: system free", a.ptr);
                                std::free(a.ptr);
                        },
                        .copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                                MemSpace::ID did = dst.ms.id();
                                if(did == MemSpace::System || did == MemSpace::SystemSecure) {
                                        std::memcpy(dst.ptr, src.ptr, bytes);
                                        return true;
                                }
                                promekiErr("(%p -> %p, %llu bytes) Copy from System to memspace %d not supported",
                                        src.ptr, dst.ptr, (unsigned long long)bytes, dst.ms.id());
                                return false;
                        },
                        .fill = [](void *ptr, size_t bytes, char value) -> Error {
                                PROMEKI_ASSERT(ptr != nullptr);
                                std::memset(ptr, value, bytes);
                                return Error::Ok;
                        },
                        .stats = makeStats()
                };

                entries[MemSpace::SystemSecure] = {
                        .id = MemSpace::SystemSecure,
                        .name = "SystemSecure",
                        .isHostAccessible = [](const MemAllocation &) -> bool { return true; },
                        .alloc = [](MemAllocation &a) -> void {
                                size_t allocSize = (a.size + a.align - 1) & ~(a.align - 1);
                                a.ptr = std::aligned_alloc(a.align, allocSize);
                                PROMEKI_ASSERT(a.ptr != nullptr);
                                promekiDebug("%p: secure allocate %d (aligned %d), align %d", a.ptr, (int)a.size, (int)allocSize, (int)a.align);
                                Error err = promeki::secureLock(a.ptr, allocSize);
                                if(err.isError()) {
                                        promekiDebug("%p: secureLock failed (%s), buffer may be swapped to disk",
                                                a.ptr, err.desc().cstr());
                                }
                        },
                        .release = [](MemAllocation &a) -> void {
                                promekiDebug("%p: secure free", a.ptr);
                                promeki::secureZero(a.ptr, a.size);
                                Error err = promeki::secureUnlock(a.ptr, a.size);
                                if(err.isError()) {
                                        promekiDebug("%p: secureUnlock failed (%s)", a.ptr, err.desc().cstr());
                                }
                                std::free(a.ptr);
                        },
                        .copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> bool {
                                MemSpace::ID did = dst.ms.id();
                                if(did == MemSpace::System || did == MemSpace::SystemSecure) {
                                        std::memcpy(dst.ptr, src.ptr, bytes);
                                        return true;
                                }
                                promekiErr("(%p -> %p, %llu bytes) Copy from SystemSecure to memspace %d not supported",
                                        src.ptr, dst.ptr, (unsigned long long)bytes, dst.ms.id());
                                return false;
                        },
                        .fill = [](void *ptr, size_t bytes, char value) -> Error {
                                PROMEKI_ASSERT(ptr != nullptr);
                                std::memset(ptr, value, bytes);
                                return Error::Ok;
                        },
                        .stats = makeStats()
                };
        }
};

static MemSpaceRegistry &registry() {
        static MemSpaceRegistry reg;
        return reg;
}

const MemSpace::Ops *MemSpace::lookup(ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return &it->second;
        return &reg.entries[System];
}

void MemSpace::registerData(Ops &&ops) {
        auto &reg = registry();
        // Users don't need to touch Ops::stats — allocate one here
        // if they didn't.  The Stats object is owned by the registry
        // and lives for the process lifetime.
        if(ops.stats == nullptr) ops.stats = MemSpaceRegistry::makeStats();
        reg.entries[ops.id] = std::move(ops);
}

StringList MemSpace::statsReport() const {
        MemSpace::Stats::Snapshot s = d->stats->snapshot();
        StringList lines;
        lines.pushToBack(String::sprintf("MemSpace[%d:%s] stats:",
                (int)d->id, d->name.cstr()));
        lines.pushToBack(String::sprintf(
                "  alloc:   %llu calls, %s  (fail: %llu, max single: %s)",
                (unsigned long long)s.allocCount,
                String::fromByteCount(s.allocBytes).cstr(),
                (unsigned long long)s.allocFailCount,
                String::fromByteCount(s.maxAllocBytes).cstr()));
        lines.pushToBack(String::sprintf(
                "  release: %llu calls, %s",
                (unsigned long long)s.releaseCount,
                String::fromByteCount(s.releaseBytes).cstr()));
        lines.pushToBack(String::sprintf(
                "  live:    %llu outstanding, %s  (peak: %llu, %s)",
                (unsigned long long)s.liveCount,
                String::fromByteCount(s.liveBytes).cstr(),
                (unsigned long long)s.peakCount,
                String::fromByteCount(s.peakBytes).cstr()));
        lines.pushToBack(String::sprintf(
                "  copy:    %llu calls, %s  (fail: %llu)",
                (unsigned long long)s.copyCount,
                String::fromByteCount(s.copyBytes).cstr(),
                (unsigned long long)s.copyFailCount));
        lines.pushToBack(String::sprintf(
                "  fill:    %llu calls, %s",
                (unsigned long long)s.fillCount,
                String::fromByteCount(s.fillBytes).cstr()));
        return lines;
}

StringList MemSpace::allStatsReport() {
        StringList lines;
        for(ID id : registeredIDs()) {
                StringList sub = MemSpace(id).statsReport();
                for(const String &line : sub) lines.pushToBack(line);
        }
        return lines;
}

void MemSpace::logStats() const {
        StringList lines = statsReport();
        for(const String &line : lines) {
                promekiInfo("%s", line.cstr());
        }
}

void MemSpace::logAllStats() {
        StringList lines = allStatsReport();
        for(const String &line : lines) {
                promekiInfo("%s", line.cstr());
        }
}

MemSpace::IDList MemSpace::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
