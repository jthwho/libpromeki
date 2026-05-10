/**
 * @file      numahostbufferimpl.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/numahostbufferimpl.h>
#include <promeki/numa.h>
#include <promeki/securemem.h>
#include <promeki/logger.h>
#include <promeki/bufferfactory.h>
#include <promeki/map.h>
#include <promeki/mutex.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Round @p bytes up to a page-multiple — Numa::allocOnNode rounds
// internally too, but mlock takes the byte count and we want to match
// the kernel's view of the allocation footprint.
size_t roundUpToPage(size_t bytes) {
#if defined(PROMEKI_PLATFORM_LINUX)
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0) return bytes;
        const size_t mask = static_cast<size_t>(pageSize) - 1;
        return (bytes + mask) & ~mask;
#else
        constexpr size_t kPage = 4096;
        return (bytes + kPage - 1) & ~(kPage - 1);
#endif
}

} // namespace

NumaHostBufferImpl::NumaHostBufferImpl(const MemSpace &ms, size_t bytes, size_t align, int node)
    : HostMappedBufferImpl(ms, nullptr, 0, align), _node(node) {
        if (bytes == 0) return;
        void *ptr = Numa::allocOnNode(bytes, _node);
        if (ptr == nullptr) {
                ms.stats().allocFailCount.fetchAndAdd(1);
                return;
        }
        // mlock-pinning is best-effort, same contract as
        // PinnedHostBufferImpl — if RLIMIT_MEMLOCK is exhausted we
        // keep the allocation and surface a warning rather than
        // tear it down.
        const size_t locked = roundUpToPage(bytes);
        Error        err    = secureLock(ptr, locked);
        if (err.isOk()) {
                _locked = true;
        } else {
                promekiWarn("NumaHostBufferImpl(%p, %zu, node=%d): mlock failed (%s); buffer is "
                            "allocated but not page-locked",
                            ptr, locked, _node, err.desc().cstr());
        }
        _hostPtr   = ptr;
        _allocSize = bytes;
        ms.stats().recordAlloc(static_cast<uint64_t>(bytes));
}

NumaHostBufferImpl::~NumaHostBufferImpl() {
        if (_hostPtr == nullptr) return;
        const uint64_t bytes  = static_cast<uint64_t>(_allocSize);
        const size_t   locked = roundUpToPage(_allocSize);
        if (_locked) {
                Error err = secureUnlock(_hostPtr, locked);
                if (err.isError()) {
                        promekiWarn("NumaHostBufferImpl(%p): munlock failed (%s)", _hostPtr,
                                    err.desc().cstr());
                }
        }
        Error fr = Numa::free(_hostPtr, _allocSize);
        if (fr.isError()) {
                promekiWarn("NumaHostBufferImpl(%p): Numa::free failed (%s)", _hostPtr,
                            fr.desc().cstr());
        }
        _memSpace.stats().recordRelease(bytes);
}

NumaHostBufferImpl *NumaHostBufferImpl::_promeki_clone() const {
        // Re-run the NUMA-bound constructor for the clone so it
        // lands on the same node (and re-attempts the mlock).  The
        // base BufferImpl members (Mutex, refcount Map) intentionally
        // don't carry over; we re-seed logicalSize / shift to match.
        auto *clone = new NumaHostBufferImpl(_memSpace, _allocSize, _align, _node);
        if (clone->_hostPtr != nullptr && _hostPtr != nullptr && _allocSize > 0) {
                std::memcpy(clone->_hostPtr, _hostPtr, _allocSize);
        }
        clone->_logicalSize = _logicalSize;
        clone->_shift       = _shift;
        return clone;
}

// ---------------------------------------------------------------------------
// NumaHost — lazy per-node MemSpace registration
// ---------------------------------------------------------------------------

namespace {

struct NumaHostRegistry {
                Mutex              mutex;
                Map<int, MemSpace::ID> nodeToId;
};

NumaHostRegistry &numaRegistry() {
        static NumaHostRegistry reg;
        return reg;
}

// Build the Ops record for a node-N MemSpace.  Real allocation lives
// on NumaHostBufferImpl (registered as a BufferImpl factory below);
// the Ops here exist so the MemSpace name + stats surface in operator
// dashboards.  Mirrors the SystemCow / PinnedHost shape.
MemSpace::Ops makeNumaHostOps(MemSpace::ID id, int node) {
        MemSpace::Ops ops{};
        ops.id   = id;
        if (node < 0) {
                ops.name = String("NumaHost");
        } else {
                ops.name = String::sprintf("NumaHost_Node%d", node);
        }
        ops.domainId         = MemDomain::Host;
        ops.isHostAccessible = [](const MemAllocation &) -> bool { return true; };
        ops.alloc = [](MemAllocation &) -> void {
                PROMEKI_ASSERT(false && "MemSpace::NumaHost alloc must go through BufferImpl factory");
        };
        ops.release = [](MemAllocation &) -> void {
                PROMEKI_ASSERT(false && "MemSpace::NumaHost release must go through BufferImpl");
        };
        ops.copy = [](const MemAllocation &src, const MemAllocation &dst, size_t bytes) -> Error {
                // Plain bytes from the CPU's perspective — copies into
                // any host MemSpace are a memcpy.
                PROMEKI_ASSERT(src.ptr != nullptr && dst.ptr != nullptr);
                MemSpace::ID did = dst.ms.id();
                if (did == MemSpace::System || did == MemSpace::SystemSecure ||
                    did == MemSpace::PinnedHost || did == MemSpace::NumaHost) {
                        std::memcpy(dst.ptr, src.ptr, bytes);
                        return Error::Ok;
                }
                // Generic host-to-host fallthrough — any host MemSpace
                // we don't recognise here can still be the destination
                // of a memcpy as long as it's host-accessible.  But
                // the test path keys on the well-known IDs above for
                // clarity.
                return Error::NotSupported;
        };
        ops.fill = [](void *ptr, size_t bytes, char value) -> Error {
                PROMEKI_ASSERT(ptr != nullptr);
                std::memset(ptr, value, bytes);
                return Error::Ok;
        };
        return ops;
}

} // namespace

MemSpace NumaHost::forNode(int node) {
        // Negative values collapse to the static NumaHost MemSpace
        // (Numa::NodeAny placement).  This is the only entry that
        // doesn't need lazy registration — bufferfactory.cpp /
        // memspace.cpp pre-register the well-known ID at startup.
        if (node < 0) return MemSpace(MemSpace::NumaHost);

        auto         &reg = numaRegistry();
        Mutex::Locker lock(reg.mutex);
        auto          it = reg.nodeToId.find(node);
        if (it != reg.nodeToId.end()) return MemSpace(it->second);

        // First time we've seen this node — register a new ID and
        // wire up its Ops + BufferImpl factory closure.
        MemSpace::ID id = MemSpace::registerType();
        MemSpace::registerData(makeNumaHostOps(id, node));
        registerBufferImplFactory(id, [node](const MemSpace &ms, size_t bytes, size_t align) -> BufferImplPtr {
                return BufferImplPtr::takeOwnership(new NumaHostBufferImpl(ms, bytes, align, node));
        });
        reg.nodeToId.insert(node, id);
        return MemSpace(id);
}

PROMEKI_NAMESPACE_END
