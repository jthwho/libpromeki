/**
 * @file      hostbufferimpl.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdlib>
#include <promeki/hostbufferimpl.h>
#include <promeki/securemem.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Round @p bytes up to a multiple of @p align.  Used to satisfy
// std::aligned_alloc, which requires the size to be a multiple of
// the alignment.
size_t roundUpToAlign(size_t bytes, size_t align) {
        if (align <= 1) return bytes;
        return (bytes + align - 1) & ~(align - 1);
}

} // namespace

// ---------------------------------------------------------------------------
// HostBufferImpl
// ---------------------------------------------------------------------------

HostBufferImpl::HostBufferImpl(const MemSpace &ms, size_t bytes, size_t align)
    : HostMappedBufferImpl(ms, nullptr, 0, align) {
        // aligned_alloc requires size to be a multiple of alignment;
        // round up for the syscall but report the original request as
        // allocSize so callers see what they asked for, matching the
        // pre-refactor MemSpace::alloc contract.
        const size_t rounded = roundUpToAlign(bytes, align);
        void        *ptr = std::aligned_alloc(align, rounded);
        if (ptr == nullptr) {
                ms.stats().allocFailCount.fetchAndAdd(1);
                return;
        }
        _hostPtr = ptr;
        _allocSize = bytes;
        ms.stats().recordAlloc(static_cast<uint64_t>(bytes));
}

HostBufferImpl::~HostBufferImpl() {
        if (_hostPtr == nullptr) return;
        const uint64_t bytes = static_cast<uint64_t>(_allocSize);
        std::free(_hostPtr);
        _memSpace.stats().recordRelease(bytes);
}

HostBufferImpl *HostBufferImpl::_promeki_clone() const {
        // Construct a fresh allocation (this also re-records stats),
        // then memcpy the bytes across.  The base BufferImpl members
        // (Mutex, Map) are intentionally NOT copied — the clone gets
        // a brand-new mutex and an empty refcount map, then we
        // re-seed the Host refcount and the logicalSize / shift
        // state to match the original.
        auto *clone = new HostBufferImpl(_memSpace, _allocSize, _align);
        if (clone->_hostPtr != nullptr && _hostPtr != nullptr && _allocSize > 0) {
                std::memcpy(clone->_hostPtr, _hostPtr, _allocSize);
        }
        clone->_logicalSize = _logicalSize;
        clone->_shift = _shift;
        return clone;
}

// ---------------------------------------------------------------------------
// HostSecureBufferImpl
// ---------------------------------------------------------------------------

HostSecureBufferImpl::HostSecureBufferImpl(const MemSpace &ms, size_t bytes, size_t align)
    : HostMappedBufferImpl(ms, nullptr, 0, align) {
        const size_t rounded = roundUpToAlign(bytes, align);
        void        *ptr = std::aligned_alloc(align, rounded);
        if (ptr == nullptr) {
                ms.stats().allocFailCount.fetchAndAdd(1);
                return;
        }
        Error err = secureLock(ptr, rounded);
        if (err.isError()) {
                promekiWarn("%p: secureLock failed (%s), buffer may be swapped to disk", ptr, err.desc().cstr());
        }
        _hostPtr = ptr;
        _allocSize = bytes;
        ms.stats().recordAlloc(static_cast<uint64_t>(bytes));
}

HostSecureBufferImpl::~HostSecureBufferImpl() {
        if (_hostPtr == nullptr) return;
        const uint64_t bytes = static_cast<uint64_t>(_allocSize);
        const size_t   locked = roundUpToAlign(_allocSize, _align);
        secureZero(_hostPtr, locked);
        Error err = secureUnlock(_hostPtr, locked);
        if (err.isError()) {
                promekiWarn("%p: secureUnlock failed (%s)", _hostPtr, err.desc().cstr());
        }
        std::free(_hostPtr);
        _memSpace.stats().recordRelease(bytes);
}

HostSecureBufferImpl *HostSecureBufferImpl::_promeki_clone() const {
        auto *clone = new HostSecureBufferImpl(_memSpace, _allocSize, _align);
        if (clone->_hostPtr != nullptr && _hostPtr != nullptr && _allocSize > 0) {
                std::memcpy(clone->_hostPtr, _hostPtr, _allocSize);
        }
        clone->_logicalSize = _logicalSize;
        clone->_shift = _shift;
        return clone;
}

PROMEKI_NAMESPACE_END
