/**
 * @file      buffer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/error.h>
#include <promeki/sharedptr.h>
#include <promeki/memspace.h>
#include <promeki/memdomain.h>
#include <promeki/buffermapflags.h>
#include <promeki/bufferimpl.h>
#include <promeki/bufferrequest.h>
#include <promeki/bufferfactory.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Generic memory buffer descriptor with alignment and memory space support.
 * @ingroup util
 *
 * Buffer is a value-type handle to a polymorphic @ref BufferImpl
 * backend.  The handle wraps a @c SharedPtr<BufferImpl, false> so
 * copies are cheap (refcount increment, no deep copy) and a single
 * Buffer can be passed by value through pipelines without inflating
 * the refcount on every copy.  Backends can be host-resident
 * (HostBufferImpl, HostSecureBufferImpl, WrappedHostBufferImpl,
 * CudaHostBufferImpl), device-resident (CudaDeviceBufferImpl), or
 * exotic (FPGA buffer indices, RDMA registrations, mmap'd regions).
 *
 * @par Domain-directed mapping
 * Memory may not be host-accessible.  Callers that need to
 * dereference a buffer in a particular @ref MemDomain call
 * @ref mapAcquire on that domain; the request resolves once the
 * backing memory is reachable from there.  Host-resident backends
 * resolve @c mapAcquire(Host) inline; device-resident backends
 * stage a copy and resolve asynchronously.  Mappings are refcounted
 * per domain — multiple call sites can hold a mapping concurrently;
 * the mapping releases when every matching @ref mapRelease has run.
 *
 * @par Buffer sizes
 * Buffer tracks three distinct sizes:
 * - @ref allocSize — the total allocation size, always constant.
 * - @ref availSize — the usable space from data() to the end of the
 *   allocation (allocSize minus any data shift).
 * - @ref size — the logical content size, set via @ref setSize.
 *
 * @par Data shifting
 * The @ref shiftData method offsets the user-visible data pointer
 * from the allocation base.  This is primarily used for O\_DIRECT
 * I/O, where the underlying allocation must be page-aligned but the
 * actual content starts at some offset within that allocation.
 * Calling @c shiftData(0) resets the view to the base and zeroes
 * the logical content size.
 *
 * @par Copy semantics
 * Copying a Buffer copies the handle — both copies refer to the
 * same backing storage.  This matches the behavior the previous
 * @c Buffer-shared call sites rely on, while removing the
 * @c ::Ptr / @c ::PtrList ceremony from public APIs.  Code that
 * needs a private copy calls @ref ensureExclusive before mutating;
 * backends that cannot be cloned (mmap'd regions, FPGA indices)
 * surface @ref Error::NotSupported through @ref ensureExclusiveError
 * rather than aborting.
 *
 * @par Thread Safety
 * Distinct Buffer instances may be used concurrently — the underlying
 * refcount on the @ref BufferImpl is atomic, so copying a Buffer
 * across threads is safe even when the source is in use elsewhere.
 * Concurrent mutation of a single instance (write through @c data(),
 * @ref setSize, @ref shiftData, @ref fill, @ref copyFrom) must be
 * externally synchronized.
 *
 * @par Example
 * @code
 * // Allocate a 4 KB page-aligned buffer.
 * Buffer buf(4096);
 * buf.fill(0);
 * buf.setSize(128);
 *
 * // Copy by value — both handles share the same backing.
 * Buffer alias = buf;
 * assert(alias.data() == buf.data());
 *
 * // Detach for private mutation.
 * alias.ensureExclusive();
 * @endcode
 */
class Buffer {
        public:
                /** @brief List of Buffers (value semantics — handles share backing). */
                using List = ::promeki::List<Buffer>;

                /**
                 * @brief Returns the system memory page size in bytes.
                 * @return The page size.
                 */
                static size_t getPageSize();

                /** @brief Default memory alignment in bytes (matches the page size). */
                static const size_t DefaultAlign;

                /** @brief Constructs an invalid (empty) Buffer. */
                Buffer() = default;

                /**
                 * @brief Allocates a buffer of the given size.
                 *
                 * Dispatches through @ref makeBufferImpl, which routes
                 * the allocation to the BufferImpl factory registered
                 * for @p ms.id.
                 *
                 * @param sz Size in bytes to allocate.
                 * @param an Alignment in bytes (defaults to @ref DefaultAlign).
                 * @param ms Memory space to allocate from.
                 *
                 * @note The allocation is sized for @p sz bytes; the
                 * logical content size is initialized to 0.  Call
                 * @ref setSize to record meaningful content.
                 */
                Buffer(size_t sz, size_t an = DefaultAlign, const MemSpace &ms = MemSpace::Default)
                    : _d(makeBufferImpl(ms, sz, an)) {}

                /**
                 * @brief Wraps an existing host pointer as a non-owning Buffer.
                 *
                 * The returned Buffer references @p p but never frees
                 * it; the caller retains responsibility for the
                 * lifetime of the underlying allocation.  The logical
                 * content size is initialized to 0; call @ref setSize
                 * if the wrapped region already holds meaningful data.
                 *
                 * @param p   Pointer to existing memory.
                 * @param sz  Size of the memory region in bytes.
                 * @param an  Alignment of the pointer (0 if unknown).
                 * @param ms  Memory space the pointer belongs to.
                 * @return    A non-owning Buffer view of @p p.
                 */
                static Buffer wrapHost(void *p, size_t sz, size_t an = 0, const MemSpace &ms = MemSpace::Default);

                /**
                 * @brief Constructs a Buffer that takes ownership of an externally-built BufferImpl.
                 *
                 * Used by custom backends (FPGA, RDMA, mmap) and by
                 * tests that want to inject a stub BufferImpl.
                 */
                static Buffer fromImpl(BufferImpl *impl) {
                        Buffer b;
                        if (impl != nullptr) b._d = BufferImplPtr::takeOwnership(impl);
                        return b;
                }

                /**
                 * @brief Returns true once the backing storage is allocated.
                 *
                 * False on default-constructed Buffers and on Buffers
                 * whose underlying allocation failed.
                 */
                bool isValid() const { return _d.isValid() && _d->allocSize() > 0; }

                /**
                 * @brief Returns the current host data pointer.
                 *
                 * For host-resident backends this is the (possibly
                 * shifted) host pointer; for backends that are not
                 * currently host-mapped this returns @c nullptr.
                 * Callers that need a host pointer for non-host
                 * backends call @ref mapAcquire(MemDomain::Host)
                 * first.
                 */
                void *data() const {
                        if (!_d.isValid()) return nullptr;
                        void *base = _d->mappedHostData();
                        if (base == nullptr) return nullptr;
                        return static_cast<uint8_t *>(base) + _d->shift();
                }

                /**
                 * @brief Returns the original allocation pointer.
                 *
                 * Unlike @ref data, this always points to the base of
                 * the allocation, regardless of any shift.  Returns
                 * @c nullptr when the buffer is not host-mapped.
                 */
                void *odata() const {
                        if (!_d.isValid()) return nullptr;
                        return _d->mappedHostData();
                }

                /**
                 * @brief Returns the logical content size in bytes.
                 *
                 * Defaults to 0 after allocation (the buffer has
                 * capacity but no content yet).  Set via @ref setSize.
                 */
                size_t size() const { return _d.isValid() ? _d->logicalSize() : 0; }

                /**
                 * @brief Sets the logical content size.
                 *
                 * The value must not exceed @ref availSize.  Marked
                 * @c const so call sites that hold a @c "const Buffer &"
                 * can still record the size after a successful read,
                 * matching the previous @c Buffer behavior.
                 */
                void setSize(size_t s) const {
                        assert(s <= availSize());
                        if (_d.isValid()) _d->setLogicalSize(s);
                        // setLogicalSize is a const method on BufferImpl
                        // (mutates a `mutable` member), so `_d->` const
                        // operator-> reaches it without modify().
                }

                /**
                 * @brief Returns the available space from data() to the end of the allocation.
                 *
                 * Equal to @ref allocSize minus any current data shift.
                 */
                size_t availSize() const {
                        if (!_d.isValid()) return 0;
                        size_t total = _d->allocSize();
                        size_t shift = _d->shift();
                        return shift > total ? 0 : total - shift;
                }

                /** @brief Returns the total allocation size in bytes. */
                size_t allocSize() const { return _d.isValid() ? _d->allocSize() : 0; }

                /** @brief Returns the alignment of the buffer in bytes. */
                size_t align() const { return _d.isValid() ? _d->align() : 0; }

                /**
                 * @brief Returns the memory space this buffer was allocated from.
                 *
                 * Returns the System MemSpace as a default for invalid
                 * buffers so the call always has a usable result.
                 */
                MemSpace memSpace() const { return _d.isValid() ? _d->memSpace() : MemSpace(MemSpace::System); }

                /**
                 * @brief Shifts the data pointer forward from the allocation base.
                 *
                 * Sets the data pointer to allocation base + bytes,
                 * creating a sub-buffer view.  The shift is always
                 * relative to the original allocation base, not the
                 * current data pointer.  Resets the logical content
                 * size to zero.
                 *
                 * @param bytes Number of bytes from the allocation base.
                 */
                void shiftData(size_t bytes) {
                        assert(_d.isValid());
                        assert(bytes <= _d->allocSize());
                        _d.modify()->setShift(bytes);
                }

                /**
                 * @brief Returns the data shift in bytes (offset from the allocation base).
                 */
                size_t shift() const { return _d.isValid() ? _d->shift() : 0; }

                // ---- Mapping ----

                /**
                 * @brief Returns true when the buffer is currently
                 *        mapped to @p domain.
                 */
                bool isMapped(MemDomain domain = MemDomain::Host) const {
                        return _d.isValid() && _d->isMapped(domain);
                }

                /**
                 * @brief Returns true when the buffer is host-mapped.
                 *
                 * Convenience for the common @c isMapped(Host) check.
                 */
                bool isHostAccessible() const { return isMapped(MemDomain::Host); }

                /**
                 * @brief Acquires a mapping in @p domain.
                 *
                 * Backends that already hold an open mapping resolve
                 * the request inline (refcount bump only); backends
                 * that need to stage a copy or perform a DMA dispatch
                 * to a worker / strand and return the in-flight
                 * handle.  Returns a request resolved with
                 * @ref Error::NotSupported when the backend cannot
                 * map to @p domain at all.
                 */
                BufferRequest mapAcquire(MemDomain domain = MemDomain::Host, MapFlags flags = MapFlags::Read) {
                        if (!_d.isValid()) return BufferRequest::resolved(Error::Invalid);
                        return _d.modify()->mapAcquire(domain, flags);
                }

                /**
                 * @brief Releases a previously-acquired @p domain mapping.
                 *
                 * Decrements the per-domain refcount; when the count
                 * reaches zero the backend tears down the mapping
                 * (writing back any caller-modified bytes when the
                 * acquire was Write or ReadWrite).  Returns
                 * @ref Error::Invalid when the domain was never
                 * mapped.
                 */
                BufferRequest mapRelease(MemDomain domain = MemDomain::Host) {
                        if (!_d.isValid()) return BufferRequest::resolved(Error::Invalid);
                        return _d.modify()->mapRelease(domain);
                }

                // ---- Mutation ----

                /**
                 * @brief Fills the entire buffer with the given byte value.
                 *
                 * The fill covers @ref availSize starting at @ref data;
                 * for backends that route through the host (the
                 * common case) this is a memset, but device-resident
                 * backends may dispatch a fill kernel instead.
                 */
                Error fill(char value) const {
                        if (!_d.isValid()) return Error::Invalid;
                        return _d.modify()->fill(value, _d->shift(), availSize());
                }

                /**
                 * @brief Copies host-resident data into the buffer.
                 *
                 * Copies @p bytes from @p src into the buffer
                 * starting @p offset bytes after @ref data.  The
                 * buffer must be valid; @c offset + @c bytes must not
                 * exceed @ref availSize.
                 *
                 * @param src    Pointer to the host source data.
                 * @param bytes  Number of bytes to copy.
                 * @param offset Destination offset from @ref data (default 0).
                 */
                Error copyFrom(const void *src, size_t bytes, size_t offset = 0) const;

                /**
                 * @brief Copies @p bytes from this buffer into @p dst.
                 *
                 * Looks up the registered copy function for the
                 * @c (memSpace().id(), dst.memSpace().id()) pair via
                 * @ref lookupBufferCopy.  When no entry is registered
                 * but both buffers are currently host-mapped, the
                 * framework falls through to a host-side memcpy.
                 * Otherwise the returned request resolves with
                 * @ref Error::NotSupported.
                 *
                 * Both @p srcOffset / @p dstOffset are byte offsets
                 * from the respective buffer's @ref data base.  The
                 * request is resolved synchronously by every
                 * currently-registered backend, so callers can call
                 * @c req.wait() and observe the outcome immediately.
                 *
                 * @param dst       Destination buffer.
                 * @param bytes     Number of bytes to copy.
                 * @param srcOffset Byte offset within this buffer (default 0).
                 * @param dstOffset Byte offset within @p dst        (default 0).
                 */
                BufferRequest copyTo(Buffer &dst, size_t bytes, size_t srcOffset = 0, size_t dstOffset = 0) const;

                // ---- Detach ----

                /**
                 * @brief Ensures this Buffer is the sole holder of its backing storage.
                 *
                 * If the underlying @ref BufferImpl is shared with
                 * other handles, clones it via @ref BufferImpl::canClone
                 * and detaches.  When the backend cannot be cloned
                 * (mmap'd regions, FPGA indices), this method has no
                 * effect — call @ref ensureExclusiveError if you need
                 * to surface the inability as an explicit error.
                 */
                void ensureExclusive() {
                        if (!_d.isValid()) return;
                        if (_d.referenceCount() < 2) return;
                        if (!_d->canClone()) return;
                        _d.detach();
                }

                /**
                 * @brief Detach variant that surfaces a non-clonable backend as an error.
                 *
                 * Mirrors @ref ensureExclusive but returns
                 * @ref Error::NotSupported when the backend's
                 * @ref BufferImpl::canClone reports false.  Callers
                 * that genuinely require a private copy use this
                 * overload instead of the no-op variant.
                 */
                Error ensureExclusiveError() {
                        if (!_d.isValid()) return Error::Invalid;
                        if (_d.referenceCount() < 2) return Error::Ok;
                        if (!_d->canClone()) return Error::NotSupported;
                        _d.detach();
                        return Error::Ok;
                }

                /** @brief Returns true when this Buffer is the sole holder of its backing storage. */
                bool isExclusive() const { return _d.isValid() && _d.referenceCount() < 2; }

                /**
                 * @brief Returns the underlying BufferImpl handle.
                 *
                 * Used by @ref BufferView's deduplication table to
                 * compare backing storage identity across slices.
                 * Most callers should never need this.
                 */
                const BufferImplPtr &impl() const { return _d; }

                /**
                 * @brief Equality on backing-storage identity.
                 *
                 * Two Buffers compare equal iff they share the same
                 * underlying @ref BufferImpl (refcount sharing).  Two
                 * separate allocations of identical content are
                 * @em not equal.
                 */
                bool operator==(const Buffer &o) const { return _d == o._d; }

                /** @brief Inequality. */
                bool operator!=(const Buffer &o) const { return _d != o._d; }

                /** @brief Returns true when the handle is non-null. */
                explicit operator bool() const { return _d.isValid(); }

        private:
                // mutable so const-on-method mutators (setSize, fill,
                // copyFrom) can call _d.modify() to reach the
                // BufferImpl's non-const interface without
                // const_cast.  The Buffer wrapper's own identity (the
                // impl pointer) is never reassigned through a const
                // path; only the pointed-to BufferImpl is mutated.
                mutable BufferImplPtr _d;
};

PROMEKI_NAMESPACE_END
