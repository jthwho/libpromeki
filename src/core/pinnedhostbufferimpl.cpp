/**
 * @file      pinnedhostbufferimpl.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <promeki/atomic.h>
#include <promeki/pinnedhostbufferimpl.h>
#include <promeki/securemem.h>
#include <promeki/logger.h>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <sys/resource.h>
#endif

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(PinnedHostBuffer)

namespace {

// aligned_alloc requires the size to be a multiple of the alignment;
// the round-up matches HostBufferImpl's behaviour exactly.
size_t roundUpToAlign(size_t bytes, size_t align) {
        if (align <= 1) return bytes;
        return (bytes + align - 1) & ~(align - 1);
}

// Emit a single human-readable warning the first time mlock() trips
// RLIMIT_MEMLOCK so operators see *why* page-locking failed without
// having every buffer spam its own warn line.
void warnRlimitMemlockOnce(size_t requestedBytes) {
        static Atomic<bool> issued{false};
        bool                expected = false;
        if (!issued.compareExchangeStrong(expected, true)) return;
#if defined(PROMEKI_PLATFORM_POSIX)
        struct rlimit rl {};
        if (getrlimit(RLIMIT_MEMLOCK, &rl) == 0) {
                promekiWarn("PinnedHostBufferImpl: mlock failed because RLIMIT_MEMLOCK is too low "
                            "(soft=%llu bytes, hard=%llu bytes; this allocation needed %zu bytes). "
                            "Buffers will remain allocated but unpinned (no DMA-pin benefit). "
                            "Raise the limit via 'ulimit -l', /etc/security/limits.conf "
                            "(LimitMEMLOCK=infinity for systemd units), or grant CAP_IPC_LOCK to the "
                            "process. Further per-buffer ENOMEM mlock failures are logged on the "
                            "PinnedHostBuffer debug channel.",
                            static_cast<unsigned long long>(rl.rlim_cur),
                            static_cast<unsigned long long>(rl.rlim_max), requestedBytes);
                return;
        }
#endif
        promekiWarn("PinnedHostBufferImpl: mlock failed (out of locked-memory quota) for a %zu-byte "
                    "buffer. Buffers will remain allocated but unpinned (no DMA-pin benefit). Raise "
                    "RLIMIT_MEMLOCK or grant CAP_IPC_LOCK. Further per-buffer ENOMEM mlock failures "
                    "are logged on the PinnedHostBuffer debug channel.",
                    requestedBytes);
}

} // namespace

PinnedHostBufferImpl::PinnedHostBufferImpl(const MemSpace &ms, size_t bytes, size_t align)
    : HostMappedBufferImpl(ms, nullptr, 0, align) {
        const size_t rounded = roundUpToAlign(bytes, align);
        void        *ptr = std::aligned_alloc(align, rounded);
        if (ptr == nullptr) {
                ms.stats().allocFailCount.fetchAndAdd(1);
                return;
        }
        // mlock-pinning is best-effort: when RLIMIT_MEMLOCK is
        // exhausted or the process lacks CAP_IPC_LOCK we keep the
        // allocation and surface a warning rather than fail the whole
        // construction.  The Buffer remains correct (host-accessible
        // memory in Host domain); it just loses the DMA-pin benefit.
        // ENOMEM (rlimit exhaustion) is the common operational case and
        // gets a one-shot human-readable warn plus per-buffer debug; any
        // other failure stays at warn since it's genuinely surprising.
        Error err = secureLock(ptr, rounded);
        if (err.isOk()) {
                _locked = true;
        } else if (err == Error::NoMem) {
                warnRlimitMemlockOnce(rounded);
                promekiDebug("PinnedHostBufferImpl(%p, %zu): mlock failed (%s); buffer is allocated "
                             "but not page-locked",
                             ptr, rounded, err.desc().cstr());
        } else {
                promekiWarn("PinnedHostBufferImpl(%p, %zu): mlock failed (%s); buffer is allocated "
                            "but not page-locked",
                            ptr, rounded, err.desc().cstr());
        }
        _hostPtr = ptr;
        _allocSize = bytes;
        ms.stats().recordAlloc(static_cast<uint64_t>(bytes));
}

PinnedHostBufferImpl::~PinnedHostBufferImpl() {
        if (_hostPtr == nullptr) return;
        const uint64_t bytes = static_cast<uint64_t>(_allocSize);
        if (_locked) {
                const size_t rounded = roundUpToAlign(_allocSize, _align);
                Error        err = secureUnlock(_hostPtr, rounded);
                if (err.isError()) {
                        promekiWarn("PinnedHostBufferImpl(%p): munlock failed (%s)", _hostPtr,
                                    err.desc().cstr());
                }
        }
        std::free(_hostPtr);
        _memSpace.stats().recordRelease(bytes);
}

PinnedHostBufferImpl *PinnedHostBufferImpl::_promeki_clone() const {
        // Construct a fresh pinned allocation (this also re-records
        // stats and re-runs the mlock attempt), then memcpy the bytes
        // across.  The base BufferImpl members (Mutex, refcount Map)
        // are intentionally not copied — the clone gets a brand-new
        // mutex and an empty refcount map; we re-seed the Host
        // refcount and the logicalSize / shift state to match the
        // original.
        auto *clone = new PinnedHostBufferImpl(_memSpace, _allocSize, _align);
        if (clone->_hostPtr != nullptr && _hostPtr != nullptr && _allocSize > 0) {
                std::memcpy(clone->_hostPtr, _hostPtr, _allocSize);
        }
        clone->_logicalSize = _logicalSize;
        clone->_shift = _shift;
        return clone;
}

PROMEKI_NAMESPACE_END
