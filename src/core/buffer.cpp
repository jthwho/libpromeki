/**
 * @file      buffer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/buffer.h>
#include <promeki/buffercommand.h>
#include <promeki/hostbufferimpl.h>
#include <promeki/util.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <Windows.h>
#else
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

const size_t Buffer::DefaultAlign = getPageSize();

size_t Buffer::getPageSize() {
        static size_t ret = 0;
        if (ret == 0) {
#if defined(PROMEKI_PLATFORM_WINDOWS)
                SYSTEM_INFO sysInfo;
                GetSystemInfo(&sysInfo);
                ret = sysInfo.dwPageSize;
#else
                ret = sysconf(_SC_PAGESIZE);
#endif
        }
        return ret;
}

Buffer Buffer::wrapHost(void *p, size_t sz, size_t an, const MemSpace &ms) {
        Buffer b;
        if (p == nullptr || sz == 0) return b;
        b._d = BufferImplPtr::takeOwnership(new WrappedHostBufferImpl(ms, p, sz, an));
        return b;
}

Error Buffer::copyFrom(const void *src, size_t bytes, size_t offset) const {
        if (!_d.isValid()) return Error::Invalid;
        if (!isHostAccessible()) return Error::NotHostAccessible;
        const size_t avail = availSize();
        if (offset > avail || bytes > avail - offset) return Error::BufferTooSmall;
        return _d.modify()->copyFromHost(src, bytes, _d->shift() + offset);
}

namespace {

// Build a resolved BufferCopyCommand carrying the result Error and
// the input fields.  Used for every code path that completes the
// copy synchronously; if a future backend wants real async, it can
// post the command through a strand and return the un-resolved
// request from a custom @c Buffer::copyTo override.
BufferRequest resolvedCopy(size_t bytes, size_t srcOffset, size_t dstOffset, Error result) {
        auto *cmd = new BufferCopyCommand();
        cmd->bytes = bytes;
        cmd->srcOffset = srcOffset;
        cmd->dstOffset = dstOffset;
        cmd->result = result;
        return BufferRequest::resolved(BufferCommand::Ptr::takeOwnership(cmd));
}

} // namespace

BufferRequest Buffer::copyTo(Buffer &dst, size_t bytes, size_t srcOffset, size_t dstOffset) const {
        if (!_d.isValid() || !dst._d.isValid()) {
                return resolvedCopy(bytes, srcOffset, dstOffset, Error::Invalid);
        }
        if (bytes == 0) {
                return resolvedCopy(bytes, srcOffset, dstOffset, Error::Ok);
        }
        const size_t srcAvail = availSize();
        const size_t dstAvail = dst.availSize();
        if (srcOffset > srcAvail || bytes > srcAvail - srcOffset ||
            dstOffset > dstAvail || bytes > dstAvail - dstOffset) {
                return resolvedCopy(bytes, srcOffset, dstOffset, Error::BufferTooSmall);
        }

        const MemSpace::ID srcId = memSpace().id();
        const MemSpace::ID dstId = dst.memSpace().id();

        // Try the registered cross-space path first.  When the
        // backend supplies a specialized fn (cudaMemcpy etc.) it
        // wins over the generic host memcpy fallback.
        BufferCopyFn fn = lookupBufferCopy(srcId, dstId);
        if (fn != nullptr) {
                Error err = fn(*this, dst, bytes, srcOffset, dstOffset);
                return resolvedCopy(bytes, srcOffset, dstOffset, err);
        }

        // Generic fallback: when both ends are currently host-mapped,
        // the bytes are reachable from the CPU and a memcpy is the
        // right thing to do.
        if (isHostAccessible() && dst.isHostAccessible()) {
                const uint8_t *srcBytes = static_cast<const uint8_t *>(data()) + srcOffset;
                uint8_t       *dstBytes = static_cast<uint8_t *>(dst.data()) + dstOffset;
                std::memcpy(dstBytes, srcBytes, bytes);
                return resolvedCopy(bytes, srcOffset, dstOffset, Error::Ok);
        }

        return resolvedCopy(bytes, srcOffset, dstOffset, Error::NotSupported);
}

PROMEKI_NAMESPACE_END
