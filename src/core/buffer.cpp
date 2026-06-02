/**
 * @file      buffer.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/buffer.h>
#include <promeki/buffercommand.h>
#include <promeki/hostbufferimpl.h>
#include <promeki/logger.h>
#include <promeki/string.h>
#include <promeki/util.h>

#if PROMEKI_ENABLE_DMABUF
#include <promeki/dmabufbufferimpl.h>
#endif

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

Buffer Buffer::wrapDmabuf(int fd, size_t sz, size_t an) {
        Buffer b;
#if PROMEKI_ENABLE_DMABUF
        if (fd < 0 || sz == 0) return b;
        b._d = BufferImplPtr::takeOwnership(
                new DmabufBufferImpl(MemSpace(MemSpace::Dmabuf), fd, sz, an, DmabufFdOwnership::Dup));
        if (!b.isValid()) b = Buffer(); // dup failed — return a clean empty handle
#else
        (void)fd;
        (void)sz;
        (void)an;
        promekiWarn("Buffer::wrapDmabuf: build configured without PROMEKI_ENABLE_DMABUF");
#endif
        return b;
}

int Buffer::dmabufFd() const {
#if PROMEKI_ENABLE_DMABUF
        if (!_d.isValid()) return -1;
        const auto *d = dynamic_cast<const DmabufBufferImpl *>(_d.ptr());
        return d != nullptr ? d->dmabufFd() : -1;
#else
        return -1;
#endif
}

namespace {

        int hexNibble(char c) {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                return -1;
        }

        bool isHexSkip(char c) {
                return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '-';
        }

}

Result<Buffer> Buffer::fromHex(const String &hex) {
        const char *cp = hex.cstr();
        size_t      n = hex.byteCount();
        // Two passes: first count significant nibbles to size the
        // destination, then decode.  Keeps a single allocation.
        size_t nibbles = 0;
        for (size_t i = 0; i < n; ++i) {
                char c = cp[i];
                if (isHexSkip(c)) continue;
                if (hexNibble(c) < 0) {
                        promekiWarn("Buffer::fromHex failed: non-hex character '%c' (0x%02x) at offset %zu",
                                    c, (unsigned)(uint8_t)c, i);
                        return makeError<Buffer>(Error::ParseFailed);
                }
                ++nibbles;
        }
        if ((nibbles & 1u) != 0) {
                promekiWarn("Buffer::fromHex failed: odd number of hex nibbles (%zu)", nibbles);
                return makeError<Buffer>(Error::ParseFailed);
        }
        const size_t bytes = nibbles / 2;
        Buffer       out(bytes == 0 ? 1 : bytes);
        out.setSize(bytes);
        if (bytes == 0) return makeResult<Buffer>(std::move(out));
        auto *dp = static_cast<uint8_t *>(out.data());
        int   hi = -1;
        size_t outIdx = 0;
        for (size_t i = 0; i < n; ++i) {
                char c = cp[i];
                if (isHexSkip(c)) continue;
                int v = hexNibble(c);
                if (hi < 0) {
                        hi = v;
                } else {
                        dp[outIdx++] = static_cast<uint8_t>((hi << 4) | v);
                        hi = -1;
                }
        }
        return makeResult<Buffer>(std::move(out));
}

String Buffer::toHex(const String &sep) const {
        String out;
        size_t n = size();
        if (n == 0) return out;
        const auto *p = static_cast<const uint8_t *>(data());
        if (p == nullptr) return out;
        const size_t sepBytes = sep.byteCount();
        out.reserve(n * 2 + (n > 1 ? (n - 1) * sepBytes : 0));
        for (size_t i = 0; i < n; ++i) {
                if (i != 0 && sepBytes > 0) out += sep;
                out += String::sprintf("%02x", static_cast<unsigned>(p[i]));
        }
        return out;
}

String Buffer::toHex() const {
        static const String kSpace(" ");
        return toHex(kSpace);
}

Error Buffer::copyFrom(const void *src, size_t bytes, size_t offset) const {
        if (!_d.isValid()) {
                promekiWarn("Buffer::copyFrom(%zu @ %zu) refused: invalid destination buffer", bytes, offset);
                return Error::Invalid;
        }
        if (!isHostAccessible()) {
                promekiWarn("Buffer::copyFrom(%zu @ %zu) refused: destination not host-accessible "
                            "(memSpace=%s)", bytes, offset, memSpace().toString().cstr());
                return Error::NotHostAccessible;
        }
        const size_t avail = availSize();
        if (offset > avail || bytes > avail - offset) {
                promekiWarn("Buffer::copyFrom refused: out of range (bytes=%zu, offset=%zu, avail=%zu)",
                            bytes, offset, avail);
                return Error::BufferTooSmall;
        }
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
                promekiWarn("Buffer::copyTo refused: invalid source (%s) or destination (%s)",
                            _d.isValid() ? "ok" : "bad", dst._d.isValid() ? "ok" : "bad");
                return resolvedCopy(bytes, srcOffset, dstOffset, Error::Invalid);
        }
        if (bytes == 0) {
                return resolvedCopy(bytes, srcOffset, dstOffset, Error::Ok);
        }
        const size_t srcAvail = availSize();
        const size_t dstAvail = dst.availSize();
        if (srcOffset > srcAvail || bytes > srcAvail - srcOffset ||
            dstOffset > dstAvail || bytes > dstAvail - dstOffset) {
                promekiWarn("Buffer::copyTo refused: out of range (bytes=%zu, srcOff=%zu, dstOff=%zu, "
                            "srcAvail=%zu, dstAvail=%zu)",
                            bytes, srcOffset, dstOffset, srcAvail, dstAvail);
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

        promekiWarn("Buffer::copyTo refused: no registered copy fn for srcSpace=%s dstSpace=%s "
                    "(bytes=%zu)", memSpace().toString().cstr(), dst.memSpace().toString().cstr(), bytes);
        return resolvedCopy(bytes, srcOffset, dstOffset, Error::NotSupported);
}

bool Buffer::operator==(const Buffer &o) const {
        // Identity short-circuit: shared backing storage is equal by
        // construction and is the common CoW case.
        if (_d == o._d) return true;

        const size_t sz = size();
        if (sz != o.size()) return false;
        // Equal-sized empty buffers (or one valid + one invalid both with
        // zero logicalSize) compare equal here, matching value semantics
        // for "no bytes."
        if (sz == 0) return true;

        // Distinct impls with content — only compare bytes when both
        // sides are host-accessible.  Cross-domain (CUDA device, FPGA)
        // contents cannot be inspected synchronously through this
        // operator; conservatively report unequal.
        if (!isHostAccessible() || !o.isHostAccessible()) return false;

        const void *a = data();
        const void *b = o.data();
        if (a == nullptr || b == nullptr) return false;
        return std::memcmp(a, b, sz) == 0;
}

PROMEKI_NAMESPACE_END
