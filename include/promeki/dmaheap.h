/**
 * @file      dmaheap.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/error.h>
#include <promeki/memspace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Allocator for Linux dma-heap-backed Buffers.
 * @ingroup util
 *
 * The kernel dma-heap framework (@c /dev/dma_heap/<name>) is the
 * userspace way to allocate a fresh dma-buf: open the heap device,
 * @c DMA_HEAP_IOCTL_ALLOC a size, and receive a dma-buf fd that any
 * DMA-capable subsystem (V4L2 mem2mem codecs such as the Xilinx VCU,
 * DRM/KMS, GPUs) can import.  This is the @em producer side that
 * complements @ref Buffer::wrapDmabuf (the import side).
 *
 * Buffers allocated here live in @ref MemSpace::Dmabuf /
 * @ref MemDomain::Dmabuf and own their fd outright
 * (@ref DmabufFdOwnership::Adopt) — promeki minted it and is its sole
 * holder.  As with any dma-buf, the bytes are not host-addressable
 * until the Buffer is mapped to @ref MemDomain::Host.
 *
 * @par MemSpace integration
 * The built-in @ref MemSpace::Dmabuf is wired to the conventional
 * @c "system" heap, so @c Buffer(size, align, MemSpace::Dmabuf)
 * just works wherever that heap is present.  @ref forHeap selects a
 * specific heap by name (returning a memoized per-heap MemSpace) for
 * platforms that expose reserved / CMA / vendor heaps — e.g. a
 * carveout the VCU requires.
 *
 * @par Heap names
 * Heap names are the basenames under @c /dev/dma_heap.  @c "system"
 * is the cached system heap present on most modern kernels;
 * embedded targets commonly add @c "reserved", @c "linux,cma", or
 * vendor-specific heaps.  Use @ref availableHeaps to enumerate what
 * the running kernel exposes.
 *
 * @par Linux-only
 * Functional only when @c PROMEKI_ENABLE_DMABUF is set and the build
 * has @c <linux/dma-heap.h>.  Elsewhere @ref isSupported returns false,
 * @ref allocate fails with @ref Error::NotSupported, and @ref forHeap
 * returns a MemSpace with no working allocation path.
 *
 * @par Thread Safety
 * Fully thread-safe.  Heap device fds and per-heap MemSpace
 * registrations are memoized behind an internal mutex.
 */
class DmaHeap {
        public:
                /** @brief The conventional kernel system-heap name (@c "system"). */
                static const char *SystemHeapName;

                /**
                 * @brief Returns true when the build can allocate from dma-heaps.
                 *
                 * False without @c PROMEKI_ENABLE_DMABUF or without
                 * @c <linux/dma-heap.h> at compile time.  Independent of
                 * whether any heap device exists at runtime — see
                 * @ref isAvailable.
                 */
                static bool isSupported();

                /**
                 * @brief Returns true when at least one heap is actually usable.
                 *
                 * Probes @c /dev/dma_heap at runtime and returns true only
                 * if at least one heap can be @em opened — presence of a
                 * node is not enough, because a heap can ship @c root-only
                 * (the common case for the system heap) and be unusable by
                 * an unprivileged process.  When a heap exists but cannot
                 * be opened due to permissions, a one-time warning naming
                 * the device and the fix is logged.
                 *
                 * False on unsupported builds, when no heaps exist, or when
                 * none of the present heaps is openable.
                 *
                 * @note Side effect: a successful probe opens and caches the
                 *       heap device fd (reused by a later @ref allocate).
                 *       Use @ref isHeapAvailable to test one heap by name.
                 */
                static bool isAvailable();

                /**
                 * @brief Returns true when the named heap can be opened.
                 *
                 * The per-heap form of @ref isAvailable — checks that
                 * @c /dev/dma_heap/<name> exists and is openable by this
                 * process (so allocation will not fail on permissions).
                 * Logs the same one-time permission warning as
                 * @ref isAvailable when the node is present but unopenable.
                 *
                 * @param name Heap basename (e.g. @ref SystemHeapName).
                 */
                static bool isHeapAvailable(const String &name);

                /**
                 * @brief Enumerates the heap names exposed under @c /dev/dma_heap.
                 * @return One entry per heap basename; empty on unsupported
                 *         builds or when no heaps are present.
                 */
                static StringList availableHeaps();

                /**
                 * @brief Allocates a raw dma-buf fd from the named heap.
                 *
                 * Low-level primitive: opens (and caches) the heap device,
                 * rounds @p bytes up to a page, and issues
                 * @c DMA_HEAP_IOCTL_ALLOC.  The returned fd is a fresh
                 * dma-buf the caller owns and must @c close (or hand to
                 * @ref DmabufFdOwnership::Adopt).
                 *
                 * @param heapName Heap basename (e.g. @ref SystemHeapName).
                 * @param bytes    Requested size in bytes (rounded up to a
                 *                 page).
                 * @param err      Optional out-parameter: @ref Error::Ok on
                 *                 success, @ref Error::NotSupported on an
                 *                 unsupported build, @ref Error::NotFound
                 *                 when the heap device is missing,
                 *                 @ref Error::Invalid for a bad name, or an
                 *                 errno-derived error on @c ioctl failure.
                 * @return The dma-buf fd, or -1 on failure.
                 */
                static int allocate(const String &heapName, size_t bytes, Error *err = nullptr);

                /**
                 * @brief Returns a MemSpace that allocates from the named heap.
                 *
                 * Memoized: repeated calls for the same name return the
                 * same MemSpace.  @c forHeap(SystemHeapName) returns the
                 * built-in @ref MemSpace::Dmabuf; other names get a unique
                 * registered MemSpace named @c "DmaHeap:<name>".  A
                 * @c Buffer allocated in the returned space owns a
                 * freshly-allocated dma-buf fd.
                 *
                 * @param name Heap basename.
                 * @return The per-heap MemSpace (in @ref MemDomain::Dmabuf).
                 */
                static MemSpace forHeap(const String &name);

                /** @brief Convenience: @c forHeap(SystemHeapName). */
                static MemSpace systemHeap();
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
