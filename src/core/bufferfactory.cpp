/**
 * @file      bufferfactory.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/bufferfactory.h>
#include <promeki/config.h>
#include <promeki/hostbufferimpl.h>
#include <promeki/numa.h>
#include <promeki/numahostbufferimpl.h>
#include <promeki/pinnedhostbufferimpl.h>
#include <promeki/map.h>
#include <promeki/mutex.h>

#if PROMEKI_ENABLE_MEMFD
#include <promeki/memfdbufferimpl.h>
#endif

PROMEKI_NAMESPACE_BEGIN

namespace {

struct FactoryRegistry {
                Mutex                                 mutex;
                Map<int, BufferImplFactory>           entries;

                FactoryRegistry() {
                        // Built-in factories — the System and
                        // SystemSecure MemSpaces are pre-registered
                        // by memspace.cpp's static initializer, and
                        // their BufferImpl subclasses live in
                        // hostbufferimpl.h.  CUDA-enabled builds
                        // register CudaHost / CudaDevice from inside
                        // CudaBootstrap::ensureRegistered.
                        entries.insert(MemSpace::System, [](const MemSpace &ms, size_t bytes, size_t align) -> BufferImplPtr {
                                return BufferImplPtr::takeOwnership(new HostBufferImpl(ms, bytes, align));
                        });
                        entries.insert(MemSpace::SystemSecure, [](const MemSpace &ms, size_t bytes, size_t align) -> BufferImplPtr {
                                return BufferImplPtr::takeOwnership(new HostSecureBufferImpl(ms, bytes, align));
                        });
                        // SystemCow: memfd-backed CoW backend on Linux,
                        // graceful fallback to plain HostBufferImpl on
                        // builds without memfd_create + F_ADD_SEALS.
                        // The MemSpace ID is registered either way, so
                        // call sites that don't care about CoW
                        // optimisation get correct behaviour
                        // transparently — they just lose the page-CoW
                        // savings on non-Linux.
                        entries.insert(MemSpace::SystemCow, [](const MemSpace &ms, size_t bytes, size_t align) -> BufferImplPtr {
#if PROMEKI_ENABLE_MEMFD
                                return BufferImplPtr::takeOwnership(new MemfdBufferImpl(ms, bytes, align));
#else
                                return BufferImplPtr::takeOwnership(new HostBufferImpl(ms, bytes, align));
#endif
                        });
                        // PinnedHost: aligned_alloc + mlock.  The mlock step
                        // is best-effort (RLIMIT_MEMLOCK / CAP_IPC_LOCK can
                        // make it fail), so the impl falls back to a plain
                        // unlocked allocation with a warning rather than
                        // failing the whole construction — see
                        // PinnedHostBufferImpl for the rationale.
                        entries.insert(MemSpace::PinnedHost, [](const MemSpace &ms, size_t bytes, size_t align) -> BufferImplPtr {
                                return BufferImplPtr::takeOwnership(new PinnedHostBufferImpl(ms, bytes, align));
                        });
                        // NumaHost (default-node / kernel-preferred).
                        // Per-specific-node MemSpaces are registered
                        // lazily by NumaHost::forNode() with their own
                        // factory closures that capture the node ID;
                        // this entry handles the @c MemSpace::NumaHost
                        // built-in ID, which always uses Numa::NodeAny.
                        entries.insert(MemSpace::NumaHost, [](const MemSpace &ms, size_t bytes, size_t align) -> BufferImplPtr {
                                return BufferImplPtr::takeOwnership(
                                        new NumaHostBufferImpl(ms, bytes, align, Numa::NodeAny));
                        });
                }
};

FactoryRegistry &registry() {
        static FactoryRegistry reg;
        return reg;
}

} // namespace

void registerBufferImplFactory(MemSpace::ID id, BufferImplFactory factory) {
        auto         &reg = registry();
        Mutex::Locker lock(reg.mutex);
        reg.entries.insert(static_cast<int>(id), factory);
}

BufferImplPtr makeBufferImpl(const MemSpace &ms, size_t bytes, size_t align) {
        BufferImplFactory factory = nullptr;
        {
                auto         &reg = registry();
                Mutex::Locker lock(reg.mutex);
                auto          it = reg.entries.find(static_cast<int>(ms.id()));
                if (it != reg.entries.end()) factory = it->second;
        }
        if (factory == nullptr) return BufferImplPtr();
        return factory(ms, bytes, align);
}

// ---------------------------------------------------------------------------
// Inter-MemSpace copy registry
// ---------------------------------------------------------------------------

namespace {

// Pack the (srcId, dstId) pair into a single 64-bit key so the
// underlying Map<int, ...> doesn't need a custom comparator.  IDs
// are 32-bit ints by convention; high 32 = src, low 32 = dst.
uint64_t copyKey(MemSpace::ID srcId, MemSpace::ID dstId) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(srcId)) << 32) |
               static_cast<uint64_t>(static_cast<uint32_t>(dstId));
}

struct CopyRegistry {
                Mutex                   mutex;
                Map<uint64_t, BufferCopyFn> entries;
};

CopyRegistry &copyRegistry() {
        static CopyRegistry reg;
        return reg;
}

} // namespace

void registerBufferCopy(MemSpace::ID srcId, MemSpace::ID dstId, BufferCopyFn fn) {
        auto         &reg = copyRegistry();
        Mutex::Locker lock(reg.mutex);
        reg.entries.insert(copyKey(srcId, dstId), fn);
}

BufferCopyFn lookupBufferCopy(MemSpace::ID srcId, MemSpace::ID dstId) {
        auto         &reg = copyRegistry();
        Mutex::Locker lock(reg.mutex);
        auto          it = reg.entries.find(copyKey(srcId, dstId));
        return (it == reg.entries.end()) ? nullptr : it->second;
}

PROMEKI_NAMESPACE_END
