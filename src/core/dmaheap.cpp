/**
 * @file      dmaheap.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/dmaheap.h>
#include <promeki/logger.h>

// The dma-heap allocator is functional only when the dma-buf backend is
// enabled and the kernel UAPI header is present at compile time.
#if PROMEKI_ENABLE_DMABUF && __has_include(<linux/dma-heap.h>)
#define PROMEKI_DMAHEAP_FUNCTIONAL 1
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <promeki/dmabufbufferimpl.h>
#include <promeki/bufferfactory.h>
#include <promeki/map.h>
#include <promeki/mutex.h>
#include <promeki/util.h>
#endif

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(DmaHeap)

const char *DmaHeap::SystemHeapName = "system";

namespace {

#ifdef PROMEKI_DMAHEAP_FUNCTIONAL

        // Fixed kernel ABI location — like /proc/self/pagemap, not an
        // application-configurable path.
        constexpr const char *kDmaHeapDir = "/dev/dma_heap";

        size_t pageRound(size_t bytes) {
                long pg = ::sysconf(_SC_PAGESIZE);
                size_t pageSize = pg > 0 ? static_cast<size_t>(pg) : 4096;
                return (bytes + pageSize - 1) & ~(pageSize - 1);
        }

        Error errnoToError(int e) {
                switch (e) {
                        case ENOMEM: return Error::NoMem;
                        case ENOENT: return Error::NotFound;
                        case EACCES: return Error::PermissionDenied;
                        case EPERM:  return Error::NoPermission;
                        case EINVAL: return Error::InvalidArgument;
                        case EBUSY:  return Error::Busy;
                        case EAGAIN: return Error::TryAgain;
                        default:     return Error::DeviceError;
                }
        }

        // Process-wide caches: open heap device fds (kept open for the
        // process lifetime, reused across allocations) and the per-heap
        // MemSpace registrations memoized by name.
        struct HeapRegistry {
                        Mutex                      mutex;
                        Map<String, int>           deviceFds;
                        Map<String, MemSpace::ID>  spaces;
                        Map<String, bool>          warnedPerms; // heaps we've already logged a perms warning for
        };

        HeapRegistry &heapRegistry() {
                static HeapRegistry reg;
                return reg;
        }

        bool validHeapName(const String &name) {
                if (name.isEmpty()) return false;
                // Basename only — refuse path separators.
                return !name.contains('/');
        }

        // Returns the cached open fd for /dev/dma_heap/<name>, opening it
        // on first use.  Caller must hold heapRegistry().mutex.
        //
        // A heap node can be present in the directory yet unopenable
        // (commonly the system heap ships @c root-only).  That is an
        // actionable real-world misconfiguration, so the first time we
        // hit it for a given heap we emit a warning naming the device and
        // the fix; subsequent attempts stay quiet to avoid log spam.
        int deviceFdLocked(const String &name) {
                HeapRegistry &reg = heapRegistry();
                auto          it = reg.deviceFds.find(name);
                if (it != reg.deviceFds.end()) return it->second;
                String path = String(kDmaHeapDir) + "/" + name;
                int    fd = ::open(path.cstr(), O_RDONLY | O_CLOEXEC);
                if (fd < 0) {
                        int e = errno;
                        if (e == EACCES || e == EPERM) {
                                if (!reg.warnedPerms.contains(name)) {
                                        reg.warnedPerms.insert(name, true);
                                        promekiWarn("DmaHeap: %s exists but cannot be opened (%s) — this process "
                                                    "lacks permission to allocate from it; grant access via a udev "
                                                    "rule or group ownership on the device node",
                                                    path.cstr(), std::strerror(e));
                                }
                        } else if (e != ENOENT) {
                                promekiDebug("DmaHeap: open(%s) failed: %s", path.cstr(), std::strerror(e));
                        }
                        return -1;
                }
                reg.deviceFds.insert(name, fd);
                return fd;
        }

        BufferImplPtr makeHeapImpl(const MemSpace &ms, const String &heapName, size_t bytes, size_t align) {
                Error  err;
                size_t rounded = pageRound(bytes);
                int    fd = DmaHeap::allocate(heapName, rounded, &err);
                if (fd < 0) {
                        promekiDebug("DmaHeap: allocate(%s, %zu) failed: %s", heapName.cstr(), rounded,
                                     err.desc().cstr());
                        return BufferImplPtr();
                }
                return BufferImplPtr::takeOwnership(
                        new DmabufBufferImpl(ms, fd, rounded, align, DmabufFdOwnership::Adopt));
        }

        // Static initializer: wire the built-in MemSpace::Dmabuf to the
        // system heap so Buffer(size, align, MemSpace::Dmabuf) works out
        // of the box.  registerBufferImplFactory uses a construct-on-first
        // -use registry, so calling it from a static initializer is safe.
        struct DmaHeapBootstrap {
                        DmaHeapBootstrap() {
                                registerBufferImplFactory(
                                        MemSpace::Dmabuf,
                                        [](const MemSpace &ms, size_t bytes, size_t align) -> BufferImplPtr {
                                                return makeHeapImpl(ms, String(DmaHeap::SystemHeapName), bytes, align);
                                        });
                        }
        };
        DmaHeapBootstrap _dmaHeapBootstrap;

#endif // PROMEKI_DMAHEAP_FUNCTIONAL

} // namespace

bool DmaHeap::isSupported() {
#ifdef PROMEKI_DMAHEAP_FUNCTIONAL
        return true;
#else
        return false;
#endif
}

StringList DmaHeap::availableHeaps() {
        StringList ret;
#ifdef PROMEKI_DMAHEAP_FUNCTIONAL
        DIR *dir = ::opendir(kDmaHeapDir);
        if (dir == nullptr) return ret;
        struct dirent *ent;
        while ((ent = ::readdir(dir)) != nullptr) {
                if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) continue;
                ret.pushToBack(String(ent->d_name));
        }
        ::closedir(dir);
#endif
        return ret;
}

bool DmaHeap::isHeapAvailable(const String &name) {
#ifdef PROMEKI_DMAHEAP_FUNCTIONAL
        if (!validHeapName(name)) return false;
        Mutex::Locker lock(heapRegistry().mutex);
        return deviceFdLocked(name) >= 0;
#else
        (void)name;
        return false;
#endif
}

bool DmaHeap::isAvailable() {
#ifdef PROMEKI_DMAHEAP_FUNCTIONAL
        // "Available" means at least one heap can actually be opened, not
        // merely that a node exists — a present-but-root-only heap is not
        // usable by this process.  Probing opens (and caches) the heap fd,
        // which also warms it for a subsequent allocate.
        StringList heaps = availableHeaps();
        if (heaps.isEmpty()) return false;
        Mutex::Locker lock(heapRegistry().mutex);
        for (const String &h : heaps) {
                if (deviceFdLocked(h) >= 0) return true;
        }
        return false;
#else
        return false;
#endif
}

int DmaHeap::allocate(const String &heapName, size_t bytes, Error *err) {
#ifdef PROMEKI_DMAHEAP_FUNCTIONAL
        if (!validHeapName(heapName)) {
                if (err != nullptr) *err = Error::Invalid;
                return -1;
        }
        int devFd;
        {
                Mutex::Locker lock(heapRegistry().mutex);
                devFd = deviceFdLocked(heapName);
        }
        if (devFd < 0) {
                if (err != nullptr) *err = Error::NotFound;
                return -1;
        }
        struct dma_heap_allocation_data data;
        std::memset(&data, 0, sizeof(data));
        data.len = pageRound(bytes);
        data.fd_flags = O_RDWR | O_CLOEXEC;
        int r;
        do {
                r = ::ioctl(devFd, DMA_HEAP_IOCTL_ALLOC, &data);
        } while (r < 0 && errno == EINTR);
        if (r < 0) {
                int e = errno;
                promekiWarn("DmaHeap: DMA_HEAP_IOCTL_ALLOC(%s, %llu) failed: %s", heapName.cstr(),
                            (unsigned long long)data.len, std::strerror(e));
                if (err != nullptr) *err = errnoToError(e);
                return -1;
        }
        if (err != nullptr) *err = Error::Ok;
        return static_cast<int>(data.fd);
#else
        (void)heapName;
        (void)bytes;
        if (err != nullptr) *err = Error::NotSupported;
        return -1;
#endif
}

MemSpace DmaHeap::forHeap(const String &name) {
#ifdef PROMEKI_DMAHEAP_FUNCTIONAL
        // The system heap is the built-in MemSpace::Dmabuf (wired by the
        // bootstrap), so it needs no per-heap registration.
        if (name == String(SystemHeapName)) return MemSpace(MemSpace::Dmabuf);

        HeapRegistry &reg = heapRegistry();
        Mutex::Locker lock(reg.mutex);
        auto          it = reg.spaces.find(name);
        if (it != reg.spaces.end()) return MemSpace(it->second);

        MemSpace::ID id = MemSpace::registerType();
        MemSpace::Ops ops{};
        ops.id = id;
        ops.name = String("DmaHeap:") + name;
        ops.domainId = MemDomain::Dmabuf;
        ops.isHostAccessible = [](const MemAllocation &) -> bool { return false; };
        ops.alloc = [](MemAllocation &) -> void {
                PROMEKI_ASSERT(false && "DmaHeap MemSpace alloc must go through the BufferImpl factory");
        };
        ops.release = [](MemAllocation &) -> void {
                PROMEKI_ASSERT(false && "DmaHeap MemSpace release is managed by DmabufBufferImpl");
        };
        ops.copy = [](const MemAllocation &, const MemAllocation &, size_t) -> Error { return Error::NotSupported; };
        ops.fill = [](void *, size_t, char) -> Error { return Error::NotSupported; };
        MemSpace::registerData(std::move(ops));

        String captured = name;
        registerBufferImplFactory(id, [captured](const MemSpace &ms, size_t bytes, size_t align) -> BufferImplPtr {
                return makeHeapImpl(ms, captured, bytes, align);
        });
        reg.spaces.insert(name, id);
        return MemSpace(id);
#else
        (void)name;
        return MemSpace(MemSpace::Dmabuf);
#endif
}

MemSpace DmaHeap::systemHeap() {
        return forHeap(String(SystemHeapName));
}

PROMEKI_NAMESPACE_END
