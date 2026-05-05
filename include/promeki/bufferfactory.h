/**
 * @file      bufferfactory.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/bufferimpl.h>
#include <promeki/memspace.h>

PROMEKI_NAMESPACE_BEGIN

class Buffer;

/**
 * @brief Convenience type for a SharedPtr to a polymorphic BufferImpl.
 * @ingroup util
 *
 * @c SharedPtr is parameterized with COW disabled — concrete
 * BufferImpl subclasses are never cloned through the SharedPtr;
 * Buffer's @c ensureExclusive uses a deliberate detach path that
 * routes through @ref BufferImpl::canClone instead.
 */
using BufferImplPtr = SharedPtr<BufferImpl, false>;

/**
 * @brief Function pointer type for constructing a BufferImpl for a MemSpace.
 * @ingroup util
 *
 * The factory receives the originating @ref MemSpace plus the size
 * and alignment requested by the caller.  It returns a fully-
 * constructed BufferImpl wrapped in a SharedPtr.  Returns an empty
 * @ref BufferImplPtr on failure (the underlying alloc returned
 * nullptr); the registered MemSpace stats already record the
 * failure count.
 */
using BufferImplFactory = BufferImplPtr (*)(const MemSpace &ms, size_t bytes, size_t align);

/**
 * @brief Registers a factory function for a MemSpace ID.
 *
 * Built-in MemSpace IDs (System, SystemSecure) have factories
 * registered by the library's static initialization.  CUDA-enabled
 * builds register CudaHost and CudaDevice during
 * @ref CudaBootstrap::ensureRegistered.  User-defined MemSpaces
 * register their own factory after calling
 * @ref MemSpace::registerType / @ref MemSpace::registerData.
 *
 * Re-registering an ID replaces the previous factory.  Thread-safe.
 */
void registerBufferImplFactory(MemSpace::ID id, BufferImplFactory factory);

/**
 * @brief Constructs a BufferImpl for the given @p ms / @p bytes / @p align.
 *
 * Looks up the factory registered for @c ms.id and invokes it.
 * Returns an empty @ref BufferImplPtr when no factory has been
 * registered for the requested space (the caller surfaces this
 * through @c Buffer::isValid() returning false).
 */
BufferImplPtr makeBufferImpl(const MemSpace &ms, size_t bytes, size_t align);

// ---------------------------------------------------------------------------
// Inter-MemSpace copy registry
// ---------------------------------------------------------------------------

/**
 * @brief Function pointer type for an inter-MemSpace copy.
 * @ingroup util
 *
 * The registered function is responsible for copying @p bytes from
 * @p src + @p srcOffset to @p dst + @p dstOffset.  Both buffers
 * remain valid for the duration of the call.  The function should
 * complete the transfer synchronously and return an @ref Error
 * indicating success or failure.
 *
 * @c Buffer::copyTo wraps the call in a @ref BufferRequest after
 * dispatching through this function pointer; backends that need
 * truly-async transfer (long-haul RDMA, multi-GPU staging) can
 * override @c Buffer::copyTo directly through a custom MemSpace
 * shim, but the registry path is always synchronous.
 */
using BufferCopyFn = Error (*)(const Buffer &src, Buffer &dst, size_t bytes, size_t srcOffset, size_t dstOffset);

/**
 * @brief Registers a copy function for the @p srcId / @p dstId MemSpace pair.
 *
 * Re-registering the same pair replaces the previous entry.
 * Thread-safe; the registry uses an internal mutex on writes and
 * reads alike (writes are rare so the contention cost is trivial).
 *
 * @param srcId Source MemSpace::ID.
 * @param dstId Destination MemSpace::ID.
 * @param fn    Copy function pointer.
 */
void registerBufferCopy(MemSpace::ID srcId, MemSpace::ID dstId, BufferCopyFn fn);

/**
 * @brief Returns the copy function registered for the given pair.
 *
 * Returns @c nullptr when no entry has been registered.  Callers
 * fall back to the host-mapped memcpy path or surface
 * @ref Error::NotSupported.
 */
BufferCopyFn lookupBufferCopy(MemSpace::ID srcId, MemSpace::ID dstId);

PROMEKI_NAMESPACE_END
