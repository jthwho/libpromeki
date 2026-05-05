/**
 * @file      hostbufferimpl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstring>
#include <promeki/namespace.h>
#include <promeki/bufferimpl.h>
#include <promeki/buffercommand.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract intermediate base for backends backed by host memory.
 * @ingroup util
 *
 * Concrete subclasses (HostBufferImpl, HostSecureBufferImpl,
 * WrappedHostBufferImpl, CudaHostBufferImpl) supply the alloc /
 * release strategy in their constructor / destructor; everything
 * else — host pointer accessors, mapAcquire(Host) refcount bump,
 * fill, copyFromHost — is provided here.
 *
 * @par Implicit Host mapping
 * Backends that derive from this class are host-resident for their
 * entire lifetime.  The constructor seeds the @ref MemDomain::Host
 * refcount to 1 so existing call sites that simply dereference
 * @c data() without an explicit @c mapAcquire keep working.
 * Additional @c mapAcquire(Host) calls increment the count;
 * @c mapRelease(Host) decrements.  The host backing is owned by the
 * subclass (lifetime tied to the BufferImpl instance) so the
 * refcount has no teardown effect — it is bookkeeping only.
 *
 * @par Non-host domains
 * @c mapAcquire / @c mapRelease for any domain other than Host
 * resolve with @ref Error::NotSupported by default.  Subclasses that
 * support additional domains (e.g. CudaHostBufferImpl exposing the
 * pinned region for direct CUDA-device DMA) override the relevant
 * branch.
 */
class HostMappedBufferImpl : public BufferImpl {
        public:
                PROMEKI_SHARED_DERIVED(HostMappedBufferImpl)

                /**
                 * @brief Constructs the host-mapped backend.
                 *
                 * Subclasses pass the result of their allocation /
                 * wrap step.  The constructor seeds the Host
                 * refcount so subsequent @ref mapAcquire calls behave
                 * additively.
                 *
                 * @param ms        MemSpace this buffer was created in.
                 * @param hostPtr   Host pointer to the allocation base.
                 * @param allocSize Total allocation size in bytes.
                 * @param align     Alignment in bytes (0 if unknown).
                 */
                HostMappedBufferImpl(const MemSpace &ms, void *hostPtr, size_t allocSize, size_t align)
                    : _hostPtr(hostPtr), _allocSize(allocSize), _align(align), _memSpace(ms) {
                        seedMapRefcount(MemDomain::Host, 1);
                }

                MemSpace memSpace() const override { return _memSpace; }
                size_t   allocSize() const override { return _allocSize; }
                size_t   align() const override { return _align; }

                void *mappedHostData() const override { return _hostPtr; }

                BufferRequest mapAcquire(MemDomain domain, MapFlags flags) override {
                        (void)flags;
                        if (domain.id() != MemDomain::Host) return resolvedMap(domain, flags, nullptr, Error::NotSupported);
                        incrementMapRefcount(MemDomain::Host);
                        return resolvedMap(domain, flags, _hostPtr, Error::Ok);
                }

                BufferRequest mapRelease(MemDomain domain) override {
                        if (domain.id() != MemDomain::Host) return resolvedUnmap(domain, Error::Invalid);
                        int newCount = decrementMapRefcount(MemDomain::Host);
                        return resolvedUnmap(domain, newCount < 0 ? Error::Invalid : Error::Ok);
                }

                Error fill(char value, size_t offset, size_t bytes) override {
                        if (_hostPtr == nullptr) return Error::Invalid;
                        std::memset(static_cast<uint8_t *>(_hostPtr) + offset, value, bytes);
                        return Error::Ok;
                }

                Error copyFromHost(const void *src, size_t bytes, size_t offset) override {
                        if (_hostPtr == nullptr) return Error::Invalid;
                        std::memcpy(static_cast<uint8_t *>(_hostPtr) + offset, src, bytes);
                        return Error::Ok;
                }

        protected:
                /**
                 * @brief Subclass helper: builds a resolved Map command.
                 *
                 * Used by @ref mapAcquire on the inline-completion
                 * path to construct a @ref BufferRequest already
                 * marked completed with the requested result fields.
                 */
                static BufferRequest resolvedMap(MemDomain target, MapFlags flags, void *hostPtr, Error result) {
                        auto *cmd = new BufferMapCommand();
                        cmd->target = target;
                        cmd->flags = flags;
                        cmd->hostPtr = hostPtr;
                        cmd->result = result;
                        return BufferRequest::resolved(BufferCommand::Ptr::takeOwnership(cmd));
                }

                /**
                 * @brief Subclass helper: builds a resolved Unmap command.
                 *
                 * Used by @ref mapRelease on the inline-completion
                 * path.
                 */
                static BufferRequest resolvedUnmap(MemDomain target, Error result) {
                        auto *cmd = new BufferUnmapCommand();
                        cmd->target = target;
                        cmd->result = result;
                        return BufferRequest::resolved(BufferCommand::Ptr::takeOwnership(cmd));
                }

                void    *_hostPtr = nullptr;
                size_t   _allocSize = 0;
                size_t   _align = 0;
                MemSpace _memSpace;
};

/**
 * @brief BufferImpl backed by std::aligned_alloc / std::free.
 * @ingroup util
 *
 * The default backend for @ref MemSpace::System.  Allocates
 * @p bytes rounded up to a multiple of @p align, zero-initializes
 * the @ref BufferImpl logical-size / shift state, and frees on
 * destruction.
 *
 * Records the alloc / release on the originating
 * @ref MemSpace::Stats counters so per-space telemetry continues
 * to work after the polymorphic-backend refactor.
 */
class HostBufferImpl : public HostMappedBufferImpl {
        public:
                /**
                 * @brief Allocates a host buffer of @p bytes (rounded up to @p align).
                 * @param ms    The MemSpace this buffer belongs to.
                 * @param bytes Requested allocation size in bytes.
                 * @param align Required alignment in bytes.
                 */
                HostBufferImpl(const MemSpace &ms, size_t bytes, size_t align);

                /** @brief Releases the underlying allocation. */
                ~HostBufferImpl() override;

                /**
                 * @brief Deep-copy clone for @c Buffer::ensureExclusive.
                 *
                 * Allocates a fresh aligned region in the same
                 * MemSpace, copies the host bytes across, and
                 * preserves the @ref BufferImpl logicalSize / shift
                 * state.  Returns a typed pointer so
                 * @c SharedPtr<HostBufferImpl>::detach lands without
                 * a downcast.
                 */
                HostBufferImpl *_promeki_clone() const override;
};

/**
 * @brief HostBufferImpl variant with page locking and secure zeroing.
 * @ingroup util
 *
 * Backs @ref MemSpace::SystemSecure.  On construction the
 * allocation is page-locked via @ref secureLock so its contents do
 * not get swapped to disk.  On destruction the buffer is
 * @ref secureZero'd before being unlocked and freed, matching the
 * old @c MemSpace::Ops behavior.
 */
class HostSecureBufferImpl : public HostMappedBufferImpl {
        public:
                /**
                 * @brief Allocates a page-locked host buffer.
                 * @param ms    The MemSpace this buffer belongs to.
                 * @param bytes Requested allocation size in bytes.
                 * @param align Required alignment in bytes.
                 */
                HostSecureBufferImpl(const MemSpace &ms, size_t bytes, size_t align);

                /** @brief Securely wipes, unlocks, and frees the allocation. */
                ~HostSecureBufferImpl() override;

                /**
                 * @brief Deep-copy clone — allocates a fresh
                 *        page-locked region and copies bytes across.
                 *
                 * Mirrors @c HostBufferImpl::_promeki_clone but
                 * routes through the secure constructor so the clone
                 * is also page-locked.
                 */
                HostSecureBufferImpl *_promeki_clone() const override;
};

/**
 * @brief Non-owning HostMappedBufferImpl over an external host pointer.
 * @ingroup util
 *
 * Used by @c Buffer::wrapHost to expose externally-managed memory
 * (mmap regions, hardware DMA staging, stack arrays) through the
 * BufferImpl interface without taking ownership.  The destructor is
 * a no-op; the caller retains lifetime responsibility for the
 * underlying allocation.
 *
 * @ref canClone returns false — wrapping memory we do not own makes
 * "clone" meaningless.  Buffer::ensureExclusive on a wrapped buffer
 * surfaces @ref Error::NotSupported rather than detaching.
 */
class WrappedHostBufferImpl : public HostMappedBufferImpl {
        public:
                PROMEKI_SHARED_DERIVED(WrappedHostBufferImpl)

                /**
                 * @brief Constructs a non-owning wrapper.
                 * @param ms     The MemSpace the pointer belongs to.
                 * @param ptr    Host pointer to the externally-managed region.
                 * @param bytes  Size of the region in bytes.
                 * @param align  Alignment of the pointer (0 if unknown).
                 */
                WrappedHostBufferImpl(const MemSpace &ms, void *ptr, size_t bytes, size_t align)
                    : HostMappedBufferImpl(ms, ptr, bytes, align) {}

                /** @brief No-op — the underlying memory is not owned. */
                ~WrappedHostBufferImpl() override = default;

                /** @brief Wrapped memory cannot be cloned. */
                bool canClone() const override { return false; }
};

PROMEKI_NAMESPACE_END
