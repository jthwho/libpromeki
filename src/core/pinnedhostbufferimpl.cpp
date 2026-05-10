/**
 * @file      pinnedhostbufferimpl.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <cstring>
#include <promeki/pinnedhostbufferimpl.h>
#include <promeki/securemem.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// aligned_alloc requires the size to be a multiple of the alignment;
// the round-up matches HostBufferImpl's behaviour exactly.
size_t roundUpToAlign(size_t bytes, size_t align) {
        if (align <= 1) return bytes;
        return (bytes + align - 1) & ~(align - 1);
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
        // Document that to the operator at warn level so the symptom
        // surfaces in logs without taking the pipeline down.
        Error err = secureLock(ptr, rounded);
        if (err.isOk()) {
                _locked = true;
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
