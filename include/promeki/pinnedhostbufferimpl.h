/**
 * @file      pinnedhostbufferimpl.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/hostbufferimpl.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief @ref BufferImpl backed by @c aligned_alloc + @c mlock.
 * @ingroup util
 *
 * Backs @ref MemSpace::PinnedHost.  Allocates a host buffer with the
 * requested alignment, then page-locks the region via @ref secureLock
 * (which wraps @c mlock + @c MADV_DONTDUMP on Linux,
 * @c VirtualLock on Windows) so the kernel will not swap the pages
 * out from under a hardware DMA engine.  Used by I/O backends whose
 * SDKs DMA directly out of host memory — NDI is the first consumer.
 *
 * @par Soft failure on @c RLIMIT_MEMLOCK
 * If @c mlock fails (typically because @c RLIMIT_MEMLOCK is exhausted
 * or the process lacks @c CAP_IPC_LOCK), construction logs a warning
 * and falls through with the unlocked allocation.  The Buffer is
 * still valid and operationally identical to a plain
 * @c MemSpace::System Buffer; it just doesn't get the DMA-pin
 * benefit.  Backends that genuinely require a pinned page (and would
 * malfunction with a swapped-out region) must inspect the lock state
 * themselves — for the SDKs we currently care about, the soft-fail
 * is correct since they cope with non-pinned host memory by
 * staging-copying internally.
 *
 * @par CUDA escalation
 * Documented future work: when a CUDA runtime is detected at
 * construction time, prefer @c cudaMallocHost over
 * @c aligned_alloc + @c mlock so the region is also DMA-friendly to
 * the GPU.  @c MemSpace::CudaHost already covers the always-CUDA
 * case; the escalation here is for callers that don't know whether
 * CUDA is in the picture but would like the upgrade transparently.
 * Not implemented yet — keep the implementation simple until a real
 * use case appears.
 *
 * @par Thread Safety
 * Same as @ref HostBufferImpl — the construction / destruction cost
 * (mlock / munlock) is paid once on the calling thread.  All buffer
 * accesses go through the inherited @ref HostMappedBufferImpl path.
 */
class PinnedHostBufferImpl : public HostMappedBufferImpl {
        public:
                /**
                 * @brief Allocates a page-locked host buffer.
                 *
                 * @param ms    The MemSpace this buffer belongs to (PinnedHost).
                 * @param bytes Requested allocation size in bytes.
                 * @param align Required alignment in bytes.  Rounded up
                 *              to a multiple of the alignment to
                 *              satisfy @c aligned_alloc.
                 */
                PinnedHostBufferImpl(const MemSpace &ms, size_t bytes, size_t align);

                /**
                 * @brief Releases the underlying allocation.
                 *
                 * Unlocks any region that was successfully locked at
                 * construction time, then frees the allocation.  No
                 * secure-zero step — pinned memory is not assumed to
                 * be sensitive (use @ref MemSpace::SystemSecure for
                 * that case).
                 */
                ~PinnedHostBufferImpl() override;

                /**
                 * @brief Deep-copy clone for @c Buffer::ensureExclusive.
                 *
                 * Mirrors @ref HostBufferImpl::_promeki_clone but
                 * routes through this class's pinning constructor so
                 * the clone is also page-locked (when @c mlock
                 * succeeds for the clone).
                 */
                PinnedHostBufferImpl *_promeki_clone() const override;

        private:
                bool _locked = false; ///< True when @c mlock succeeded; controls @c munlock at destroy.
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
