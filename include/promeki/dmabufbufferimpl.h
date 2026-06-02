/**
 * @file      dmabufbufferimpl.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_DMABUF
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/bufferimpl.h>
#include <promeki/buffercommand.h>
#include <promeki/mutex.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief How a @ref DmabufBufferImpl acquires its owned fd reference.
 * @ingroup util
 *
 * A dma-buf fd is one refcounted reference to the underlying buffer;
 * the impl always owns exactly one such reference and @c close()s it
 * on destruction.  This enum only selects how that owned reference is
 * obtained.
 */
enum class DmabufFdOwnership {
        /**
         * @brief @c dup() the supplied fd and own the dup.
         *
         * The caller keeps ownership of the fd it passed in and may
         * @c close it at any time after construction — the dup is an
         * independent kernel reference, so the buffer stays alive.
         * This is the safe default for importing an fd from another
         * subsystem (e.g. V4L2 @c VIDIOC_EXPBUF), decoupling the
         * Buffer's lifetime from the producer's fd.
         */
        Dup,
        /**
         * @brief Take the supplied fd as-is and own it.
         *
         * No @c dup is performed; the impl @c close()s the exact fd it
         * was given.  Used when promeki minted the fd itself (the
         * dma-heap allocator) and is its sole holder, so a dup would
         * just waste an fd.
         */
        Adopt,
};

/**
 * @brief BufferImpl backed by a Linux dma-buf file descriptor.
 * @ingroup util
 *
 * Wraps a single dma-buf fd — the kernel handle to a buffer that can
 * be shared zero-copy between DMA-capable subsystems (V4L2 capture
 * devices, V4L2 mem2mem codecs such as the Xilinx VCU, DRM/KMS,
 * GPUs).  Lives in @ref MemSpace::Dmabuf / @ref MemDomain::Dmabuf.
 * The fd is the buffer's native representation; a consumer that wants
 * to feed it to another driver acquires the @ref MemDomain::Dmabuf
 * mapping (a refcount-only bump) and reads the fd from the resolved
 * @ref BufferDmabufMapCommand.
 *
 * @par Host access is on demand
 * A dma-buf is not assumed to be CPU-addressable.  @c data() returns
 * @c nullptr until the caller maps the buffer to @ref MemDomain::Host,
 * at which point the impl lazily @c mmaps the fd and brackets CPU
 * access with the @c DMA_BUF_IOCTL_SYNC cache-coherency ioctl
 * (@c DMA_BUF_SYNC_START on first acquire, @c DMA_BUF_SYNC_END on
 * final release).  Buffers whose exporter does not support CPU mmap
 * resolve @c mapAcquire(Host) with @ref Error::NotSupported.  The
 * sync ioctl is best-effort: exporters that do not implement it
 * (@c ENOTTY) are tolerated silently, since for those the mapping is
 * already coherent.
 *
 * @par Ownership
 * The impl always owns exactly one fd reference and @c close()s it on
 * destruction; @ref DmabufFdOwnership selects whether that reference
 * is a @c dup of the caller's fd (@ref DmabufFdOwnership::Dup — the
 * import default, which decouples the Buffer's lifetime from the
 * producer's fd) or the caller's fd adopted directly
 * (@ref DmabufFdOwnership::Adopt — for fds promeki minted itself, e.g.
 * the dma-heap allocator).  There is no non-owning / borrow mode: the
 * kernel refcounts the dma-buf, so owning an independent reference is
 * cheap and removes the dangling-fd footgun a borrow would carry.
 *
 * @par Not cloneable
 * @ref canClone returns false: duplicating a dma-buf meaningfully
 * requires allocating a fresh backing buffer from a heap and copying,
 * which this backend has no allocator for.  @c Buffer::ensureExclusive
 * surfaces @ref Error::NotSupported rather than deep-copying.
 *
 * @par Thread Safety
 * The lazy host mmap / munmap and the per-domain refcount transitions
 * are guarded by an internal mutex, so concurrent @c mapAcquire /
 * @c mapRelease from different threads are safe.  As with every
 * backend, concurrent mutation of the mapped bytes through @c data()
 * must be synchronized by the caller.
 *
 * @par Linux-only
 * Compiled only when @c PROMEKI_ENABLE_DMABUF is set (Linux with
 * @c <linux/dma-buf.h>).  On other builds @c Buffer::wrapDmabuf
 * returns an invalid Buffer.
 */
class DmabufBufferImpl : public BufferImpl {
        public:
                PROMEKI_SHARED_DERIVED(DmabufBufferImpl)

                /**
                 * @brief Wraps a dma-buf fd, owning a reference to it.
                 *
                 * On @ref DmabufFdOwnership::Dup the supplied fd is
                 * @c dup()'d; on a @c dup failure (or a negative @p fd)
                 * the impl is left invalid (@c allocSize 0).
                 *
                 * @param ms        The MemSpace (e.g. @ref MemSpace::Dmabuf
                 *                  or a @c DmaHeap per-heap space).
                 * @param fd        The dma-buf file descriptor.
                 * @param bytes     Size of the buffer in bytes.
                 * @param align     Alignment hint in bytes (0 if unknown);
                 *                  recorded for reporting.
                 * @param ownership Whether to @c dup the fd or adopt it
                 *                  directly (see @ref DmabufFdOwnership).
                 */
                DmabufBufferImpl(const MemSpace &ms, int fd, size_t bytes, size_t align, DmabufFdOwnership ownership);

                /** @brief Unmaps any host view and closes the owned fd. */
                ~DmabufBufferImpl() override;

                MemSpace memSpace() const override { return _memSpace; }
                size_t   allocSize() const override { return _allocSize; }
                size_t   align() const override { return _align; }

                /**
                 * @brief Returns the dma-buf file descriptor, or -1.
                 *
                 * Always valid for the lifetime of the impl (until the
                 * destructor runs).  Used by consumers that import the
                 * buffer into another DMA subsystem without going
                 * through the @ref MemDomain::Dmabuf mapping request.
                 */
                int dmabufFd() const { return _fd; }

                /** @brief Host pointer when host-mapped, otherwise nullptr. */
                void *mappedHostData() const override { return _hostMap; }

                BufferRequest mapAcquire(MemDomain domain, MapFlags flags) override;
                BufferRequest mapRelease(MemDomain domain) override;

                /** @brief Fills the host view; requires an active Host mapping. */
                Error fill(char value, size_t offset, size_t bytes) override;

                /** @brief Copies host data into the view; requires an active Host mapping. */
                Error copyFromHost(const void *src, size_t bytes, size_t offset) override;

                /** @brief A dma-buf cannot be deep-copied without a heap allocator. */
                bool canClone() const override { return false; }

        private:
                /**
                 * @brief Issues @c DMA_BUF_IOCTL_SYNC with @p syncFlags.
                 *
                 * Best-effort: retries on @c EINTR, tolerates @c ENOTTY
                 * (exporter without a sync ioctl), warns on other errors.
                 */
                void dmabufSync(uint64_t syncFlags) const;

                int      _fd          = -1;
                size_t   _allocSize   = 0;
                size_t   _align       = 0;
                MemSpace _memSpace;

                // Guards the lazy host mmap and the Host-domain refcount
                // 0<->1 transitions so mmap/munmap happen exactly once.
                mutable Mutex _hostMutex;
                void         *_hostMap       = nullptr;
                uint64_t      _hostSyncFlags = 0; // DMA_BUF_SYNC_READ/WRITE bits in effect
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_DMABUF
