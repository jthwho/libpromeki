/**
 * @file      dmabufbufferimpl.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_DMABUF

#include <cerrno>
#include <cstring>
#include <cstdint>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <promeki/dmabufbufferimpl.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(DmabufBufferImpl)

namespace {

        // Translate Buffer MapFlags into PROT_* bits for mmap().  A
        // mapping with no explicit access defaults to read-only.
        int protFromMapFlags(MapFlags flags) {
                int prot = 0;
                if (hasMapFlag(flags, MapFlags::Read)) prot |= PROT_READ;
                if (hasMapFlag(flags, MapFlags::Write)) prot |= PROT_WRITE;
                if (prot == 0) prot = PROT_READ;
                return prot;
        }

        // Translate Buffer MapFlags into DMA_BUF_SYNC_* direction bits.
        uint64_t syncDirFromMapFlags(MapFlags flags) {
                uint64_t dir = 0;
                if (hasMapFlag(flags, MapFlags::Read)) dir |= DMA_BUF_SYNC_READ;
                if (hasMapFlag(flags, MapFlags::Write)) dir |= DMA_BUF_SYNC_WRITE;
                if (dir == 0) dir = DMA_BUF_SYNC_READ;
                return dir;
        }

        BufferRequest resolvedHostMap(MemDomain target, MapFlags flags, void *hostPtr, Error result) {
                auto *cmd = new BufferMapCommand();
                cmd->target = target;
                cmd->flags = flags;
                cmd->hostPtr = hostPtr;
                cmd->result = result;
                return BufferRequest::resolved(BufferCommand::Ptr::takeOwnership(cmd));
        }

        BufferRequest resolvedDmabufMap(MemDomain target, MapFlags flags, int fd, Error result) {
                auto *cmd = new BufferDmabufMapCommand();
                cmd->target = target;
                cmd->flags = flags;
                cmd->dmabufFd = fd;
                cmd->result = result;
                return BufferRequest::resolved(BufferCommand::Ptr::takeOwnership(cmd));
        }

        BufferRequest resolvedUnmap(MemDomain target, Error result) {
                auto *cmd = new BufferUnmapCommand();
                cmd->target = target;
                cmd->result = result;
                return BufferRequest::resolved(BufferCommand::Ptr::takeOwnership(cmd));
        }

} // namespace

DmabufBufferImpl::DmabufBufferImpl(const MemSpace &ms, int fd, size_t bytes, size_t align,
                                   DmabufFdOwnership ownership)
    : _align(align), _memSpace(ms) {
        // Acquire the owned fd reference.  Dup gives an independent
        // kernel reference (the caller keeps theirs); Adopt takes the
        // fd as-is (promeki minted it).
        if (fd >= 0) {
                if (ownership == DmabufFdOwnership::Dup) {
                        _fd = ::dup(fd);
                        if (_fd < 0) {
                                promekiWarn("DmabufBufferImpl: dup(fd=%d) failed: %s", fd, std::strerror(errno));
                        }
                } else {
                        _fd = fd; // Adopt — own the exact fd given.
                }
        }
        if (_fd < 0) {
                // Failed import: leave _allocSize at 0 so Buffer::isValid()
                // reports false.  Nothing was allocated to release.
                _memSpace.stats().allocFailCount.fetchAndAdd(1);
                return;
        }
        _allocSize = bytes;
        // Record on the MemSpace counters so live dma-buf footprint is
        // visible in operator dashboards.  The fd is reachable in the
        // Dmabuf domain for the impl's whole lifetime, so seed that
        // domain's refcount above zero — the fd is never "unmapped" the
        // way a torn-down host mmap is.
        seedMapRefcount(MemDomain::Dmabuf, 1);
        if (bytes > 0) _memSpace.stats().recordAlloc(static_cast<uint64_t>(bytes));
}

DmabufBufferImpl::~DmabufBufferImpl() {
        if (_hostMap != nullptr) {
                // Defensive: a well-behaved caller releases its Host
                // mapping before dropping the buffer, but if one leaked we
                // still tear the mmap down rather than leak address space.
                dmabufSync(DMA_BUF_SYNC_END | _hostSyncFlags);
                if (::munmap(_hostMap, _allocSize) != 0) {
                        promekiWarn("DmabufBufferImpl::~: munmap failed: %s", std::strerror(errno));
                }
                _hostMap = nullptr;
        }
        if (_fd >= 0) {
                if (::close(_fd) != 0) {
                        promekiWarn("DmabufBufferImpl::~: close(fd=%d) failed: %s", _fd, std::strerror(errno));
                }
                _fd = -1;
        }
        if (_allocSize > 0) _memSpace.stats().recordRelease(static_cast<uint64_t>(_allocSize));
}

BufferRequest DmabufBufferImpl::mapAcquire(MemDomain domain, MapFlags flags) {
        if (domain.id() == MemDomain::Dmabuf) {
                // The fd is always available; acquiring the Dmabuf domain
                // is a refcount bump that signals "this fd is in flight in
                // another DMA subsystem — do not retire the buffer".
                incrementMapRefcount(MemDomain::Dmabuf);
                return resolvedDmabufMap(domain, flags, _fd, Error::Ok);
        }
        if (domain.id() != MemDomain::Host) {
                return resolvedHostMap(domain, flags, nullptr, Error::NotSupported);
        }

        Mutex::Locker lock(_hostMutex);
        if (_hostMap == nullptr) {
                if (_fd < 0) return resolvedHostMap(domain, flags, nullptr, Error::Invalid);
                void *p = ::mmap(nullptr, _allocSize, protFromMapFlags(flags), MAP_SHARED, _fd, 0);
                if (p == MAP_FAILED) {
                        // Exporter does not allow CPU mmap of this dma-buf.
                        promekiDebug("DmabufBufferImpl: mmap(fd=%d, %zu bytes) failed: %s — not host-mappable",
                                     _fd, _allocSize, std::strerror(errno));
                        return resolvedHostMap(domain, flags, nullptr, Error::NotSupported);
                }
                _hostMap = p;
                _hostSyncFlags = syncDirFromMapFlags(flags);
                dmabufSync(DMA_BUF_SYNC_START | _hostSyncFlags);
        }
        incrementMapRefcount(MemDomain::Host);
        return resolvedHostMap(domain, flags, _hostMap, Error::Ok);
}

BufferRequest DmabufBufferImpl::mapRelease(MemDomain domain) {
        if (domain.id() == MemDomain::Dmabuf) {
                int newCount = decrementMapRefcount(MemDomain::Dmabuf);
                return resolvedUnmap(domain, newCount < 0 ? Error::Invalid : Error::Ok);
        }
        if (domain.id() != MemDomain::Host) {
                return resolvedUnmap(domain, Error::Invalid);
        }

        Mutex::Locker lock(_hostMutex);
        int newCount = decrementMapRefcount(MemDomain::Host);
        if (newCount < 0) return resolvedUnmap(domain, Error::Invalid);
        if (newCount == 0 && _hostMap != nullptr) {
                dmabufSync(DMA_BUF_SYNC_END | _hostSyncFlags);
                if (::munmap(_hostMap, _allocSize) != 0) {
                        promekiWarn("DmabufBufferImpl::mapRelease: munmap failed: %s", std::strerror(errno));
                }
                _hostMap = nullptr;
                _hostSyncFlags = 0;
        }
        return resolvedUnmap(domain, Error::Ok);
}

Error DmabufBufferImpl::fill(char value, size_t offset, size_t bytes) {
        Mutex::Locker lock(_hostMutex);
        if (_hostMap == nullptr) return Error::NotSupported;
        std::memset(static_cast<uint8_t *>(_hostMap) + offset, value, bytes);
        return Error::Ok;
}

Error DmabufBufferImpl::copyFromHost(const void *src, size_t bytes, size_t offset) {
        Mutex::Locker lock(_hostMutex);
        if (_hostMap == nullptr) return Error::NotSupported;
        std::memcpy(static_cast<uint8_t *>(_hostMap) + offset, src, bytes);
        return Error::Ok;
}

void DmabufBufferImpl::dmabufSync(uint64_t syncFlags) const {
        if (_fd < 0) return;
        struct dma_buf_sync sync;
        std::memset(&sync, 0, sizeof(sync));
        sync.flags = syncFlags;
        int r;
        do {
                r = ::ioctl(_fd, DMA_BUF_IOCTL_SYNC, &sync);
        } while (r < 0 && errno == EINTR);
        if (r < 0 && errno != ENOTTY) {
                // ENOTTY: exporter has no sync ioctl — its mapping is
                // already coherent, so the omission is benign.  Anything
                // else is worth surfacing.
                promekiWarn("DmabufBufferImpl: DMA_BUF_IOCTL_SYNC(0x%llx) failed: %s",
                            (unsigned long long)syncFlags, std::strerror(errno));
        }
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_DMABUF
