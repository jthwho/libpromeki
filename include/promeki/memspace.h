/**
 * @file      memspace.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

struct MemAllocation;

/**
 * @brief Abstraction for memory allocation in different address spaces.
 * @ingroup util
 *
 * Uses the @ref typeregistry "TypeRegistry pattern": a lightweight inline
 * wrapper around an immutable Ops record, identified by an integer ID.
 * Well-known memory spaces are provided as ID constants; user-defined
 * spaces can be registered at runtime via registerType() and registerData().
 *
 * Provides a uniform interface for allocating, releasing, copying, and setting
 * memory that may reside in different memory spaces (e.g. system RAM, GPU memory).
 * Each memory space is identified by an ID and provides its own set of operations.
 *
 * Memory spaces handle their own lifecycle concerns internally. For example,
 * SystemSecure performs page locking on allocation and secure zeroing on release
 * without exposing those details through the MemSpace API.
 *
 * @see @ref typeregistry "TypeRegistry Pattern" for the design pattern.
 */
class MemSpace {
        public:
                /**
                 * @brief Identifies a memory space.
                 *
                 * Well-known spaces have named enumerators.  User-defined
                 * spaces obtain IDs from registerType().  The atomic counter
                 * starts at UserDefined.
                 */
                enum ID {
                        System       = 0,    ///< System (CPU) memory.
                        SystemSecure = 1,    ///< System memory with secure zeroing on free and page locking.
                        CudaDevice   = 2,    ///< CUDA device memory (GPU VRAM).  Not host-accessible.  Registered by CudaBootstrap when PROMEKI_ENABLE_CUDA is on.
                        CudaHost     = 3,    ///< CUDA pinned host memory (page-locked).  Host-accessible; enables async DMA with CudaDevice.  Registered by CudaBootstrap.
                        Default      = System, ///< Alias for System memory.
                        UserDefined  = 1024  ///< First ID available for user-registered types.
                };

                /** @brief List of MemSpace IDs. */
                using IDList = List<ID>;

                /**
                 * @brief Lock-free runtime statistics for a memory space.
                 *
                 * Every MemSpace instance with the same ID shares one
                 * Stats object owned by the registry.  All counters are
                 * `Atomic<uint64_t>` so updates and reads are safe from
                 * any thread without taking a lock.
                 *
                 * Counters are cumulative since process start (or since
                 * the most recent call to reset()).  The "live" pair
                 * tracks currently-outstanding allocations and shrinks
                 * on release(); the "peak" pair records their
                 * high-water marks.
                 *
                 * Non-copyable, non-movable — retrieve a plain-value
                 * Snapshot via snapshot() for reporting.
                 */
                struct Stats {
                        /** @brief Plain-value snapshot of a Stats for reporting. */
                        struct Snapshot {
                                uint64_t allocCount     = 0;    ///< Successful allocations.
                                uint64_t allocBytes     = 0;    ///< Total bytes allocated (success only).
                                uint64_t allocFailCount = 0;    ///< Allocations that returned a null pointer.
                                uint64_t maxAllocBytes  = 0;    ///< Largest single successful allocation, in bytes.
                                uint64_t releaseCount   = 0;    ///< Releases of non-null allocations.
                                uint64_t releaseBytes   = 0;    ///< Total bytes released.
                                uint64_t copyCount      = 0;    ///< Successful copy() calls.
                                uint64_t copyBytes      = 0;    ///< Total bytes copied.
                                uint64_t copyFailCount  = 0;    ///< copy() calls that returned false.
                                uint64_t fillCount      = 0;    ///< Successful fill() calls.
                                uint64_t fillBytes      = 0;    ///< Total bytes filled.
                                uint64_t liveCount      = 0;    ///< Outstanding allocations at snapshot time.
                                uint64_t liveBytes      = 0;    ///< Outstanding bytes at snapshot time.
                                uint64_t peakCount      = 0;    ///< Highest liveCount ever observed.
                                uint64_t peakBytes      = 0;    ///< Highest liveBytes ever observed.
                        };

                        Atomic<uint64_t> allocCount     {0};    ///< @see Snapshot::allocCount
                        Atomic<uint64_t> allocBytes     {0};    ///< @see Snapshot::allocBytes
                        Atomic<uint64_t> allocFailCount {0};    ///< @see Snapshot::allocFailCount
                        Atomic<uint64_t> maxAllocBytes  {0};    ///< @see Snapshot::maxAllocBytes
                        Atomic<uint64_t> releaseCount   {0};    ///< @see Snapshot::releaseCount
                        Atomic<uint64_t> releaseBytes   {0};    ///< @see Snapshot::releaseBytes
                        Atomic<uint64_t> copyCount      {0};    ///< @see Snapshot::copyCount
                        Atomic<uint64_t> copyBytes      {0};    ///< @see Snapshot::copyBytes
                        Atomic<uint64_t> copyFailCount  {0};    ///< @see Snapshot::copyFailCount
                        Atomic<uint64_t> fillCount      {0};    ///< @see Snapshot::fillCount
                        Atomic<uint64_t> fillBytes      {0};    ///< @see Snapshot::fillBytes
                        Atomic<uint64_t> liveCount      {0};    ///< @see Snapshot::liveCount
                        Atomic<uint64_t> liveBytes      {0};    ///< @see Snapshot::liveBytes
                        Atomic<uint64_t> peakCount      {0};    ///< @see Snapshot::peakCount
                        Atomic<uint64_t> peakBytes      {0};    ///< @see Snapshot::peakBytes

                        Stats() = default;
                        Stats(const Stats &) = delete;
                        Stats &operator=(const Stats &) = delete;
                        Stats(Stats &&) = delete;
                        Stats &operator=(Stats &&) = delete;

                        /**
                         * @brief Atomically captures a plain-value snapshot.
                         *
                         * Each field is loaded independently, so a
                         * snapshot taken during heavy churn is
                         * consistent per-field but may not be globally
                         * consistent across fields.  This is fine for
                         * reporting and debugging.
                         */
                        Snapshot snapshot() const;

                        /** @brief Zeroes every counter. */
                        void reset();

                        /**
                         * @brief Internal: records a successful allocation.
                         *
                         * Called by MemSpace::alloc().  Updates the
                         * cumulative, live, peak, and max counters.
                         */
                        void recordAlloc(uint64_t bytes);

                        /**
                         * @brief Internal: records a release.
                         *
                         * Called by MemSpace::release().  Updates the
                         * cumulative and live counters.
                         */
                        void recordRelease(uint64_t bytes);
                };

                /** @brief Function table for memory space operations. */
                struct Ops {
                        ID id;                                                              ///< The memory space identifier.
                        String name;                                                        ///< Human-readable name of the memory space.
                        bool (*isHostAccessible)(const MemAllocation &alloc);                ///< Returns true if the allocation is directly accessible from the host CPU.
                        void (*alloc)(MemAllocation &alloc);                                ///< Allocate memory. Size and align are pre-filled.
                        void (*release)(MemAllocation &alloc);                              ///< Release previously allocated memory.
                        bool (*copy)(const MemAllocation &src, const MemAllocation &dst, size_t bytes); ///< Copy bytes from this space to another.
                        Error (*fill)(void *ptr, size_t bytes, char value);                 ///< Fill memory with a byte value.
                        Stats *stats = nullptr;                                             ///< Runtime counters; owned by the registry, auto-created by registerData() if null.
                };

                /**
                 * @brief Allocates and returns a unique ID for a user-defined memory space.
                 *
                 * Each call returns a new, never-before-used ID.  Thread-safe.
                 *
                 * @code
                 * // Register a GPU memory space backed by a custom allocator.
                 * MemSpace::ID gpuID = MemSpace::registerType();
                 *
                 * MemSpace::Ops ops;
                 * ops.id   = gpuID;
                 * ops.name = "GPU";
                 * ops.isHostAccessible = [](const MemAllocation &) { return false; };
                 * ops.alloc   = myGpuAlloc;
                 * ops.release = myGpuFree;
                 * ops.copy    = myGpuCopy;
                 * ops.fill    = myGpuFill;
                 * MemSpace::registerData(std::move(ops));
                 *
                 * MemSpace gpu(gpuID);  // now usable like any built-in space
                 * @endcode
                 *
                 * @return A unique ID value.
                 * @see registerData()
                 */
                static ID registerType();

                /**
                 * @brief Registers an Ops record in the registry.
                 *
                 * After this call, constructing a MemSpace from @p ops.id
                 * will resolve to the registered operations.
                 *
                 * @note If CrashHandler is installed, its MemSpace
                 *       snapshot is captured at install() time and
                 *       will not automatically include MemSpaces
                 *       registered afterward.  To make a newly
                 *       registered MemSpace appear in crash reports,
                 *       call @ref Application::refreshCrashHandler
                 *       once all MemSpaces have been registered (for
                 *       example, at the end of application startup).
                 *       Refreshing is intentionally explicit so
                 *       bulk registrations at startup only pay the
                 *       snapshot cost once.
                 *
                 * @param ops The populated Ops struct with id set to a value from registerType().
                 * @see registerType()
                 * @see Application::refreshCrashHandler
                 */
                static void registerData(Ops &&ops);

                /**
                 * @brief Returns a list of all registered MemSpace IDs.
                 *
                 * Includes both well-known and user-registered types.
                 *
                 * @return A list of ID values.
                 */
                static IDList registeredIDs();

                /**
                 * @brief Constructs a MemSpace for the given memory space ID.
                 * @param id The memory space to use (default: Default, which is System).
                 */
                inline MemSpace(ID id = Default);

                /**
                 * @brief Returns the human-readable name of this memory space.
                 * @return The name string.
                 */
                const String &name() const { return d->name; }

                /**
                 * @brief Returns the memory space identifier.
                 * @return The ID.
                 */
                ID id() const { return d->id; }

                /**
                 * @brief Returns true if the given allocation is directly accessible from the host CPU.
                 * @param alloc The allocation to check.
                 * @return True if the memory can be directly read/written by the CPU.
                 */
                bool isHostAccessible(const MemAllocation &alloc) const {
                        return d->isHostAccessible(alloc);
                }

                /**
                 * @brief Allocates memory in this memory space.
                 * @param bytes Number of bytes to allocate.
                 * @param align Required alignment in bytes.
                 * @return A MemAllocation describing the allocated region.
                 */
                inline MemAllocation alloc(size_t bytes, size_t align) const;

                /**
                 * @brief Releases a previously allocated memory region.
                 * @param alloc The allocation to release. Cleared on return.
                 */
                inline void release(MemAllocation &alloc) const;

                /**
                 * @brief Copies bytes from a source allocation to a destination allocation.
                 *
                 * Called on the source's MemSpace. The source and destination may
                 * reside in different memory spaces.
                 * @param src   The source allocation.
                 * @param dst   The destination allocation.
                 * @param bytes Number of bytes to copy.
                 * @return True on success, false if either pointer is nullptr.
                 */
                inline bool copy(const MemAllocation &src, const MemAllocation &dst, size_t bytes) const;

                /**
                 * @brief Fills memory with a byte value.
                 * @param ptr   Destination pointer.
                 * @param bytes Number of bytes to fill.
                 * @param value The byte value to fill with.
                 * @return Error::Ok on success, or an error if @p ptr is nullptr.
                 */
                Error fill(void *ptr, size_t bytes, char value) const {
                        if(ptr == nullptr) return Error::Invalid;
                        Error err = d->fill(ptr, bytes, value);
                        if(err.isOk()) {
                                d->stats->fillCount.fetchAndAdd(1);
                                d->stats->fillBytes.fetchAndAdd(bytes);
                        }
                        return err;
                }

                /**
                 * @brief Returns the runtime stats for this memory space.
                 *
                 * Stats are shared by every MemSpace wrapper that
                 * references the same ID — every call to alloc(),
                 * release(), copy(), or fill() on any instance
                 * updates the same underlying counters atomically.
                 *
                 * @return A reference to the live (non-copyable) Stats object.
                 */
                Stats &stats() const { return *d->stats; }

                /**
                 * @brief Convenience: returns a plain-value snapshot of the stats.
                 * @return A Stats::Snapshot capturing the current counters.
                 */
                Stats::Snapshot statsSnapshot() const { return d->stats->snapshot(); }

                /** @brief Zeroes every counter in this memory space's Stats. */
                void resetStats() const { d->stats->reset(); }

                /**
                 * @brief Formats this memory space's stats as a StringList.
                 *
                 * Each line is already prefixed with the memory space
                 * name, ready for display.  Safe to call at any time
                 * from any thread — the snapshot is taken atomically.
                 *
                 * @return One StringList entry per line of the report.
                 */
                StringList statsReport() const;

                /**
                 * @brief Formats stats for every registered MemSpace as a StringList.
                 *
                 * Walks registeredIDs() and concatenates each
                 * statsReport().  Useful when the caller wants to
                 * capture the report as structured text (tests,
                 * debug panels, file dumps).
                 */
                static StringList allStatsReport();

                /**
                 * @brief Writes statsReport() to the promeki log at Info level.
                 *
                 * Convenience wrapper: captures the StringList via
                 * statsReport() and emits each line through
                 * @c promekiInfo.
                 */
                void logStats() const;

                /**
                 * @brief Writes allStatsReport() to the promeki log at Info level.
                 *
                 * Convenience wrapper for shutdown dumps and periodic
                 * monitoring.
                 */
                static void logAllStats();

                /** @brief Returns the underlying Ops pointer. */
                const Ops *data() const { return d; }

                /** @brief Equality comparison (identity-based). */
                bool operator==(const MemSpace &o) const { return d == o.d; }

                /** @brief Inequality comparison. */
                bool operator!=(const MemSpace &o) const { return d != o.d; }

        private:
                const Ops *d = nullptr;
                static const Ops *lookup(ID id);
};

/**
 * @brief Describes a memory allocation from a MemSpace.
 *
 * Returned by MemSpace::alloc() and passed to MemSpace::release().
 * Contains the allocation pointer, size, alignment, the originating
 * MemSpace, and an opaque private pointer for allocator-specific data.
 */
struct MemAllocation {
        void    *ptr   = nullptr;       ///< Pointer to the allocated memory.
        size_t  size   = 0;             ///< Size of the allocation in bytes.
        size_t  align  = 0;             ///< Alignment of the allocation in bytes.
        MemSpace ms;                    ///< The memory space this was allocated from.
        void    *priv  = nullptr;       ///< Private data for the allocator implementation.

        /** @brief Returns true if this allocation is valid. */
        bool isValid() const { return ptr != nullptr; }
};

inline MemSpace::MemSpace(ID id) : d(lookup(id)) {}

inline bool MemSpace::copy(const MemAllocation &src, const MemAllocation &dst, size_t bytes) const {
        if(src.ptr == nullptr || dst.ptr == nullptr) return false;
        bool ok = d->copy(src, dst, bytes);
        if(ok) {
                d->stats->copyCount.fetchAndAdd(1);
                d->stats->copyBytes.fetchAndAdd(bytes);
        } else {
                d->stats->copyFailCount.fetchAndAdd(1);
        }
        return ok;
}

inline MemAllocation MemSpace::alloc(size_t bytes, size_t align) const {
        MemAllocation a;
        a.size = bytes;
        a.align = align;
        a.ms = *this;
        d->alloc(a);
        if(a.ptr != nullptr) {
                d->stats->recordAlloc(static_cast<uint64_t>(bytes));
        } else {
                d->stats->allocFailCount.fetchAndAdd(1);
        }
        return a;
}

inline void MemSpace::release(MemAllocation &alloc) const {
        if(alloc.ptr == nullptr) return;
        uint64_t bytes = static_cast<uint64_t>(alloc.size);
        d->release(alloc);
        d->stats->recordRelease(bytes);
        alloc.ptr = nullptr;
        alloc.priv = nullptr;
}

PROMEKI_NAMESPACE_END
