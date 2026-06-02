/**
 * @file      bufferimpl.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <cstdint>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/error.h>
#include <promeki/function.h>
#include <promeki/mutex.h>
#include <promeki/map.h>
#include <promeki/memspace.h>
#include <promeki/memdomain.h>
#include <promeki/buffermapflags.h>
#include <promeki/bufferrequest.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Polymorphic backend for @ref Buffer.
 * @ingroup util
 *
 * Every @ref Buffer is a value-type handle wrapping a
 * @c SharedPtr<BufferImpl, false>.  BufferImpl is the abstract base
 * for every concrete backend — host memory, page-locked secure
 * memory, CUDA device memory, FPGA buffer indices, mmap'd file
 * regions, RDMA registrations, etc.  Each subclass owns the per-
 * backend behavior that used to live on @c MemSpace::Ops.
 *
 * @par Domain-directed mapping
 * Memory may not be host-accessible.  Callers that need to
 * dereference a buffer in a particular @ref MemDomain call
 * @ref mapAcquire on that domain; the request resolves once the
 * backing memory is reachable from there.  HostBufferImpl resolves
 * @c mapAcquire(Host) inline (host memory is always mapped to the
 * Host domain), while CudaDeviceBufferImpl stages a copy through
 * pinned host memory and resolves the request asynchronously.
 *
 * Mappings are refcounted per domain — multiple call sites can
 * @ref mapAcquire the same domain concurrently and the mapping is
 * released only when every matching @ref mapRelease has run.  Long-
 * term mappings are supported (host buffers stay mapped for their
 * lifetime, with the Host refcount permanently above zero).
 *
 * @par Logical content size and data shift
 * The current @ref Buffer API exposes a logical content size
 * (@c size / @c setSize) and a base-relative data shift (@c shiftData)
 * to support O\_DIRECT-style alignment dances.  Both pieces of state
 * live on BufferImpl rather than on the Buffer wrapper, so multiple
 * Buffer handles to the same backing storage observe a consistent
 * shift / size — matching the behavior the previous
 * @c Buffer-shared call sites rely on.
 *
 * @par Thread Safety
 * Distinct BufferImpl instances may be used concurrently.  The
 * per-domain mapping refcounts are guarded by an internal mutex.
 * Subclasses must protect any backend-internal state (staging
 * buffers, lazy allocations) with their own synchronization.  The
 * @ref BufferCommand wired into any returned @ref BufferRequest is
 * internally synchronized.
 */
class BufferImpl {
        public:
                /**
                 * @brief Polymorphic root for the BufferImpl hierarchy.
                 *
                 * Adds the reference count and a virtual clone hook.
                 * Concrete leaves are marked with
                 * @ref PROMEKI_SHARED_DERIVED; backends that do not
                 * support cloning override @ref canClone to return
                 * false (Buffer::ensureExclusive surfaces that as an
                 * error rather than aborting the process).
                 */
                PROMEKI_SHARED_BASE(BufferImpl)

                /**
                 * @brief Callback fired once when the impl is destroyed.
                 *
                 * See @ref setReleaseCallback.
                 */
                using ReleaseCallback = Function<void()>;

                BufferImpl() = default;

                /**
                 * @brief Destroys the impl and fires the release callback.
                 *
                 * If a @ref ReleaseCallback was armed via
                 * @ref setReleaseCallback it runs here, after the
                 * derived backend's destructor has already torn down its
                 * own state (closed fds, freed allocations).  The
                 * callback therefore observes the buffer as gone and
                 * must not touch it.
                 */
                virtual ~BufferImpl() {
                        if (_onRelease) _onRelease();
                }

                BufferImpl(const BufferImpl &) = default;
                BufferImpl &operator=(const BufferImpl &) = default;

                // ---- Identity / sizing ----

                /** @brief Returns the @ref MemSpace this buffer was allocated in. */
                virtual MemSpace memSpace() const = 0;

                /** @brief Returns the total allocation size in bytes. */
                virtual size_t allocSize() const = 0;

                /** @brief Returns the alignment requested for the allocation in bytes. */
                virtual size_t align() const = 0;

                // ---- Map state ----

                /**
                 * @brief Returns true when the buffer is currently
                 *        mapped to @p domain (per-domain refcount > 0).
                 */
                bool isMapped(MemDomain domain) const { return mapRefcount(domain.id()) > 0; }

                /**
                 * @brief Returns the host pointer when the buffer is
                 *        host-mapped, otherwise nullptr.
                 *
                 * Equivalent to @c isMapped(Host) ? hostBase() : nullptr.
                 * Callers that have just acquired a host mapping use
                 * this to read the pointer without a second virtual
                 * dispatch through @ref BufferRequest.
                 */
                virtual void *mappedHostData() const = 0;

                /**
                 * @brief Increments the per-domain mapping refcount and
                 *        ensures the buffer is accessible to @p domain.
                 *
                 * Backends that already hold an open mapping to
                 * @p domain may resolve the returned request inline
                 * (via @c BufferRequest::resolved); backends that need
                 * to stage a copy or perform a DMA dispatch the work
                 * to a worker / strand and return the in-flight handle.
                 *
                 * Backends that cannot map to @p domain return
                 * @c BufferRequest::resolved with
                 * @ref Error::NotSupported.
                 */
                virtual BufferRequest mapAcquire(MemDomain domain, MapFlags flags) = 0;

                /**
                 * @brief Decrements the per-domain mapping refcount.
                 *
                 * When the count for @p domain reaches zero the
                 * backend performs any required write-back (when the
                 * acquire was Write or ReadWrite) and tears down the
                 * mapping.  Calling release on a domain that was never
                 * acquired returns @ref Error::Invalid.
                 */
                virtual BufferRequest mapRelease(MemDomain domain) = 0;

                // ---- Mutation ----

                /**
                 * @brief Fills @p bytes starting at @p offset with @p value.
                 *
                 * Backends decide how to satisfy the call — for host
                 * buffers it is a memset; for device-resident buffers
                 * it may dispatch a fill kernel or stage through host
                 * memory.  Returns @ref Error::Ok on success.
                 */
                virtual Error fill(char value, size_t offset, size_t bytes) = 0;

                /**
                 * @brief Copies @p bytes from a host-resident @p src
                 *        into the buffer at @p offset.
                 *
                 * For host backends this is a memcpy; for device
                 * backends it is a host-to-device transfer that may
                 * complete asynchronously (the synchronous overload
                 * blocks until the transfer is done).  Returns
                 * @ref Error::NotSupported when the backend has no
                 * efficient host-source path.
                 */
                virtual Error copyFromHost(const void *src, size_t bytes, size_t offset) = 0;

                // ---- Logical size + data shift (shared across handles) ----

                /** @brief Returns the logical content size in bytes (set via @ref setLogicalSize). */
                size_t logicalSize() const { return _logicalSize; }

                /**
                 * @brief Sets the logical content size in bytes.
                 *
                 * The value is bounded by the available size from the
                 * current shift to the end of the allocation.  No
                 * validation is performed here — Buffer surfaces the
                 * assertion at the public API boundary.
                 *
                 * @c mutable so the call signature on @ref Buffer can
                 * stay const-on-method, matching today's
                 * @c Buffer behavior.
                 */
                void setLogicalSize(size_t s) const { _logicalSize = s; }

                /** @brief Returns the data shift in bytes (offset from the allocation base). */
                size_t shift() const { return _shift; }

                /**
                 * @brief Sets the data shift in bytes.
                 *
                 * Resets the logical content size to zero, matching
                 * the existing @c Buffer::shiftData semantics.
                 */
                void setShift(size_t s) {
                        _shift = s;
                        _logicalSize = 0;
                }

                // ---- Cloning ----

                /**
                 * @brief Returns true if @ref _promeki_clone is meaningful.
                 *
                 * Backends whose buffer cannot be deep-copied (an FPGA
                 * buffer index, an RDMA region key) override this to
                 * return false; @c Buffer::ensureExclusive surfaces the
                 * inability as an error rather than aborting.  The
                 * default implementation returns true.
                 */
                virtual bool canClone() const { return true; }

                // ---- Producer→post-producer transition (seal) ----

                /**
                 * @brief Transitions the buffer out of its producer phase.
                 *
                 * "Seal" is a generic backend hook for the producer→
                 * post-producer transition that some backends support:
                 * the @ref MemSpace::SystemCow / memfd backend uses it
                 * to drop the writable shared mapping and switch to
                 * @c MAP_PRIVATE clone views (page-level CoW); a
                 * future read-only-snapshot or GPU-publish backend
                 * would use it to freeze a NIC-RX buffer or flush
                 * host→device.  Backends without a seal concept return
                 * @ref Error::Ok unconditionally — call sites can
                 * issue @c seal() without knowing the backend.
                 *
                 * Idempotent.  After a successful seal the impl is
                 * either cloneable (if @ref canClone is true) or
                 * permanently fixed.  Mutates internal state, but is
                 * declared @c const so a const @ref Buffer handle can
                 * forward through @c _d-> without losing const-ness;
                 * subclasses use @c mutable members or appropriate
                 * synchronization.
                 */
                virtual Error seal() const { return Error::Ok; }

                /**
                 * @brief Returns the resident-set size of this allocation in bytes.
                 *
                 * For the default (non-CoW) backends the entire
                 * allocation is resident, so the default implementation
                 * returns @ref allocSize.  Sparse / CoW backends
                 * (@ref MemfdBufferImpl post-seal, future swap-aware
                 * backends) override with the actual page-resident
                 * count read from the kernel (e.g. via
                 * @c /proc/self/smaps Private_Dirty).
                 *
                 * Used by production telemetry to surface the difference
                 * between virtual address space (@c allocSize, what
                 * @ref MemSpace::Stats::liveBytes counts) and physical
                 * page residency.  May be relatively expensive on
                 * sparse backends — call from monitoring paths, not
                 * the per-frame hot path.
                 */
                virtual size_t residentBytes() const { return allocSize(); }

                /**
                 * @brief Returns true when this backend uses copy-on-write semantics.
                 *
                 * CoW backends (today: @ref MemfdBufferImpl backed by
                 * @ref MemSpace::SystemCow) carry a stricter
                 * concurrent-access contract — @ref seal and
                 * @c Buffer::ensureExclusive race with concurrent
                 * @c data() reads on sibling handles.  Generic code
                 * branches on this predicate to honour the contract
                 * without a downcast.  Default: false.
                 */
                virtual bool isCowBacked() const { return false; }

                // ---- Release notification ----

                /**
                 * @brief Arms a callback fired once when this impl is destroyed.
                 *
                 * A @ref Buffer is a value handle over a refcounted
                 * @c SharedPtr<BufferImpl>; the impl is destroyed when
                 * its last handle is dropped.  Arming a release callback
                 * turns that final-reference teardown into an event,
                 * letting a producer learn when every consumer has
                 * finished with a buffer without polling
                 * @c referenceCount().
                 *
                 * The callback runs exactly once, from the destructor
                 * (@ref ~BufferImpl), on whatever thread drops the last
                 * reference — which may not be the thread that armed it.
                 * It must be self-contained: by the time it fires the
                 * buffer (and any derived backend state) is gone, so it
                 * must not capture or dereference the impl / @ref Buffer.
                 *
                 * The primary user is the V4L2 dma-buf capture pool,
                 * which re-queues a kernel capture buffer to the driver
                 * once the exported dma-buf's last reference is released.
                 *
                 * @param cb The callback to fire on destruction.  Pass an
                 *           empty @ref ReleaseCallback to disarm.
                 */
                void setReleaseCallback(ReleaseCallback cb) { _onRelease = std::move(cb); }

        protected:
                /**
                 * @brief Subclass helper: increments the refcount for @p domain.
                 *
                 * Returns the new count.  Used by subclass
                 * @ref mapAcquire implementations after they have
                 * confirmed @p domain is supported.
                 */
                int incrementMapRefcount(MemDomain::ID domain) {
                        Mutex::Locker lock(_mapMutex);
                        auto          it = _mapRefcounts.find(domain);
                        if (it == _mapRefcounts.end()) {
                                _mapRefcounts.insert(domain, 1);
                                return 1;
                        }
                        return ++it->second;
                }

                /**
                 * @brief Subclass helper: decrements the refcount for @p domain.
                 *
                 * Returns the new count, or -1 when the domain had no
                 * outstanding mapping (caller surfaces as
                 * @ref Error::Invalid).
                 */
                int decrementMapRefcount(MemDomain::ID domain) {
                        Mutex::Locker lock(_mapMutex);
                        auto          it = _mapRefcounts.find(domain);
                        if (it == _mapRefcounts.end()) return -1;
                        if (it->second == 0) return -1;
                        return --it->second;
                }

                /**
                 * @brief Subclass helper: returns the current refcount for @p domain.
                 *
                 * Returns 0 when the domain was never mapped.
                 */
                int mapRefcount(MemDomain::ID domain) const {
                        Mutex::Locker lock(_mapMutex);
                        auto          it = _mapRefcounts.find(domain);
                        return (it == _mapRefcounts.end()) ? 0 : it->second;
                }

                /**
                 * @brief Subclass helper: pre-seeds the refcount for the
                 *        backend's native domain.
                 *
                 * Called by subclass constructors that hold a permanent
                 * mapping in their native domain (host backends seed
                 * @ref MemDomain::Host so the implicit construction-
                 * time mapping never expires).  Bypasses the mutex
                 * because subclass constructors are single-threaded.
                 */
                void seedMapRefcount(MemDomain::ID domain, int count) { _mapRefcounts.insert(domain, count); }

                mutable size_t _logicalSize = 0;
                size_t         _shift = 0;

        private:
                mutable Mutex          _mapMutex;
                Map<MemDomain::ID, int> _mapRefcounts;
                ReleaseCallback         _onRelease;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
