/**
 * @file      numa.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Standalone NUMA-aware memory and topology helpers.
 * @ingroup util
 *
 * Thin wrapper over the kernel's NUMA syscalls plus a few @c /sys
 * lookups for the bits libpromeki uses (per-node allocation, NIC →
 * node mapping, current thread's preferred node).  Kept as a
 * stateless static-method utility so it can be used standalone — the
 * @c MemSpace::NumaHost backend, the RTP TX/RX allocators, and any
 * future caller all reach for the same surface.
 *
 * @par Why we don't use libnuma
 * libnuma is a ~1500-line LGPL-2.1 wrapper over four kernel syscalls.
 * The Linux kernel provides the actual NUMA semantics; the userspace
 * wrapper is the easy part.  Rolling our own keeps the library
 * dependency-free, sidesteps the licence question, and lets us
 * pre-stub the macOS / Windows paths cleanly (see "Platform support"
 * below) — libnuma wouldn't help us there in any case.
 *
 * @par Platform support
 * - **Linux**: real implementation via @c mmap + @c set_mempolicy /
 *   @c mbind, @c /sys/devices/system/node for topology, and
 *   @c /sys/class/net/&lt;iface&gt;/device/numa_node for NIC node.
 * - **macOS / BSD / Windows**: stub.  @ref isAvailable returns false,
 *   @ref nodeCount returns 1, @ref allocOnNode falls back to plain
 *   @c posix_memalign / @c VirtualAlloc and ignores the @p node
 *   argument, NIC / CPU node lookups return @ref NodeAny.  Apple
 *   Silicon Macs are UMA so this is the correct behaviour; on Intel
 *   Mac Pros explicit per-node allocation isn't exposed by macOS at
 *   all, so the stub is the best we can do without going private-
 *   API.
 *
 * @par Soft fail on non-NUMA hardware
 * On a single-node Linux box (most laptops, single-socket
 * workstations) @ref isAvailable returns false and the allocator
 * paths fall back to plain page-aligned host memory.  Callers don't
 * need to branch on @ref isAvailable themselves — the API works
 * correctly on UMA boxes too, just without the per-node binding
 * benefit.
 *
 * @par Thread Safety
 * Fully thread-safe.  Topology accessors read kernel-published @c /sys
 * files and cache the result; the cache fill is guarded by a one-shot
 * mutex.  Allocation paths invoke the kernel directly with no shared
 * state.
 */
class Numa {
        public:
                /**
                 * @brief Sentinel for "no specific NUMA node".
                 *
                 * Matches the Linux kernel convention where
                 * @c /sys/class/net/&lt;iface&gt;/device/numa_node
                 * stores @c -1 for interfaces with no NUMA affinity.
                 * Used as both an input ("allocate where the kernel
                 * thinks is best") and an output ("this resource has
                 * no node binding").
                 */
                static constexpr int NodeAny = -1;

                /**
                 * @brief True when the OS exposes a NUMA topology with more than one node.
                 *
                 * Returns @c false on UMA boxes (single-socket Intel,
                 * Apple Silicon Macs, almost all laptops) even though
                 * the API surface is still safe to call — falling
                 * back to plain page-aligned host allocation.
                 */
                static bool isAvailable();

                /**
                 * @brief Number of NUMA nodes on this system.
                 *
                 * Returns @c 1 when NUMA is unavailable so callers
                 * can size per-node arrays without branching.
                 */
                static int nodeCount();

                /**
                 * @brief Highest valid node ID on this system.
                 *
                 * For sizing per-node arrays.  May be larger than
                 * @ref nodeCount when nodes have been hot-removed
                 * (rare).  Returns @c 0 when NUMA is unavailable.
                 */
                static int maxNode();

                /**
                 * @brief Allocates @p bytes of memory bound to the
                 *        given NUMA node.
                 *
                 * The returned pointer is page-aligned.  Pair with
                 * @ref free; the @p bytes argument must match across
                 * the alloc / free pair.
                 *
                 * @param bytes Allocation size; must be > 0.  When 0,
                 *        returns @c nullptr.
                 * @param node @ref NodeAny → kernel-preferred (uses
                 *        @c numa_alloc_local-equivalent semantics);
                 *        a specific node ID → bound to that node via
                 *        @c set_mempolicy(MPOL_BIND).
                 *
                 * @return Pointer to the allocated region, or
                 *         @c nullptr on failure.  Failure modes
                 *         include the requested node not existing
                 *         (cgroup restriction, hot-removed node) and
                 *         the kernel rejecting the binding.  When
                 *         NUMA is unavailable, falls back to plain
                 *         page-aligned host allocation and ignores
                 *         @p node — never null on a successful
                 *         underlying @c posix_memalign / @c mmap.
                 */
                static void *allocOnNode(size_t bytes, int node);

                /**
                 * @brief Releases a region returned by @ref allocOnNode.
                 *
                 * @param ptr   Pointer returned by @ref allocOnNode,
                 *              or @c nullptr (no-op).
                 * @param bytes Original allocation size.  Must
                 *              match the original request.
                 *
                 * @return @c Error::Ok on success, @c Error::Invalid
                 *         on a non-null @p ptr the kernel won't
                 *         release.  A null @p ptr returns @c Ok.
                 */
                static Error free(void *ptr, size_t bytes);

                /**
                 * @brief Returns the NUMA node a network interface is attached to.
                 *
                 * Reads @c /sys/class/net/&lt;iface&gt;/device/numa_node
                 * on Linux.  Used by the RTP TX/RX allocators to pick
                 * a node-local placement so DMA doesn't cross sockets.
                 *
                 * @param interfaceName Interface name (e.g. @c "eth0").
                 * @return The node ID, or @ref NodeAny when the
                 *         interface has no NUMA affinity, the file
                 *         can't be read, or NUMA is unavailable.
                 */
                static int nodeOfNic(const String &interfaceName);

                /**
                 * @brief Returns the NUMA node a CPU is on.
                 *
                 * Walks the per-node @c cpu&lt;N&gt; symlinks under
                 * @c /sys/devices/system/node on Linux.  Returns
                 * @ref NodeAny when the lookup fails or NUMA is
                 * unavailable.
                 */
                static int nodeOfCpu(int cpu);

                /**
                 * @brief Returns the NUMA node the calling thread is on.
                 *
                 * Equivalent to @c nodeOfCpu(sched_getcpu()).
                 * Returns @ref NodeAny on UMA boxes.
                 */
                static int currentNode();

        private:
                Numa() = delete;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
