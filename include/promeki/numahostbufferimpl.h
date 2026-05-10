/**
 * @file      numahostbufferimpl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/hostbufferimpl.h>
#include <promeki/memspace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief @ref BufferImpl backed by NUMA-bound page-locked host memory.
 * @ingroup util
 *
 * Allocates via @ref Numa::allocOnNode (mmap + @c mbind on Linux,
 * page-aligned posix_memalign on stub platforms) so the region's
 * physical pages live on a specific NUMA node.  Then page-locks via
 * @ref secureLock so the kernel won't swap the region — same DMA-pin
 * benefit as @ref PinnedHostBufferImpl, with the additional locality
 * win that the buffer's physical pages are on the NIC's (or
 * consumer's) preferred socket rather than wherever the kernel
 * happened to first-touch them.
 *
 * @par Soft fail on @c RLIMIT_MEMLOCK or @c mbind
 * Both the locking step and the binding step are best-effort.  When
 * @c mlock fails the impl warns and keeps the unlocked allocation;
 * when @c mbind fails @ref Numa::allocOnNode warns and keeps the
 * unbound allocation.  Either way the Buffer is still valid and
 * usable — it just loses the corresponding optimisation.
 *
 * @par Per-node MemSpace IDs
 * NUMA placement is parameterised by node ID, but @ref MemSpace's
 * factory keys on a single integer ID.  The matching wrapper
 * @ref NumaHost lazily registers a unique @ref MemSpace::ID per node
 * (via @ref MemSpace::registerType) with a factory closure that
 * captures the node, then constructs this impl with the right node
 * binding.  The default @ref MemSpace::NumaHost ID corresponds to
 * @ref Numa::NodeAny (kernel-preferred placement).
 */
class NumaHostBufferImpl : public HostMappedBufferImpl {
        public:
                /**
                 * @brief Allocates a NUMA-bound page-locked host buffer.
                 *
                 * @param ms    The MemSpace this buffer belongs to.
                 *              Used for stats accounting + identity;
                 *              the node ID is the @p node argument
                 *              (the impl trusts the factory closure
                 *              to pass the right one).
                 * @param bytes Requested allocation size in bytes.
                 * @param align Requested alignment.  Recorded for
                 *              reporting; the kernel returns
                 *              page-aligned pointers regardless.
                 * @param node  NUMA node ID to bind to, or
                 *              @ref Numa::NodeAny for kernel-preferred.
                 */
                NumaHostBufferImpl(const MemSpace &ms, size_t bytes, size_t align, int node);

                /**
                 * @brief Releases the underlying allocation.
                 *
                 * Unlocks the region (when locked at construction)
                 * and returns the pages to the kernel via
                 * @ref Numa::free.
                 */
                ~NumaHostBufferImpl() override;

                /**
                 * @brief Deep-copy clone for @c Buffer::ensureExclusive.
                 *
                 * Routes through this class's NUMA-binding constructor
                 * so the clone lands on the same node.  When @c mbind
                 * or @c mlock fails on the clone, behaves the same as
                 * the original (warning + soft fail).
                 */
                NumaHostBufferImpl *_promeki_clone() const override;

                /** @brief Returns the NUMA node this buffer is bound to. */
                int node() const { return _node; }

        private:
                int  _node   = -1;     ///< NUMA node this buffer is bound to (-1 = NodeAny).
                bool _locked = false;  ///< True when @c mlock succeeded; controls @c munlock at destroy.
};

/**
 * @brief Factory + registry for per-node @ref MemSpace::NumaHost variants.
 * @ingroup util
 *
 * Wraps the lazy registration of one @ref MemSpace::ID per NUMA node.
 * The first call to @ref forNode(N) for a given @p N registers a new
 * MemSpace ID, name (`"NumaHost_NodeN"`), and BufferImpl factory
 * closure that captures @p N — subsequent calls return the cached
 * MemSpace.  Thread-safe.
 *
 * The default-node MemSpace (@ref MemSpace::NumaHost ID) is registered
 * statically alongside the other built-in MemSpaces and represents
 * @ref Numa::NodeAny placement (kernel-preferred).
 *
 * @par Usage
 * @code
 * MemSpace ms = NumaHost::forNode(0);
 * Buffer   buf(64 * 1024, Buffer::DefaultAlign, ms);
 * @endcode
 */
class NumaHost {
        public:
                /**
                 * @brief Returns a @ref MemSpace bound to the given NUMA node.
                 *
                 * Lazily registers a unique MemSpace ID on first call
                 * per node value (including negative values, which
                 * map to @ref Numa::NodeAny / kernel-preferred).
                 *
                 * @param node NUMA node ID (>= 0 for explicit binding;
                 *             negative values collapse to NodeAny).
                 */
                static MemSpace forNode(int node);

        private:
                NumaHost() = delete;
};

PROMEKI_NAMESPACE_END
