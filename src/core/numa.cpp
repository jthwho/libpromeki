/**
 * @file      numa.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Standalone NUMA wrapper.  Keep this file dependency-free beyond the
 * project's standard headers — the platform paths fork on
 * @c PROMEKI_PLATFORM_LINUX and everything else falls through to a
 * stub that reports unavailable.
 */

#include <promeki/numa.h>
#include <promeki/atomic.h>
#include <promeki/platform.h>
#include <promeki/logger.h>
#include <promeki/list.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(PROMEKI_PLATFORM_LINUX)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sched.h>
#include <unistd.h>
#include <linux/mempolicy.h>

// glibc doesn't expose set_mempolicy / mbind through libc headers, so
// we wrap them as raw syscalls.  These have been stable since
// Linux 2.6.7 (2004) for set_mempolicy and 2.6.16 for mbind, so the
// numbers are not going to change.
#ifndef MPOL_BIND
#define MPOL_BIND 2
#endif
#ifndef MPOL_PREFERRED
#define MPOL_PREFERRED 1
#endif
#ifndef MPOL_DEFAULT
#define MPOL_DEFAULT 0
#endif

namespace {
        // Wrap mbind as a raw syscall — glibc doesn't declare it, but
        // the syscall number has been stable since Linux 2.6.16 (2006)
        // so this is portable across every kernel we ship to.
        long sys_mbind(void *addr, unsigned long len, int mode, const unsigned long *nodemask,
                       unsigned long maxnode, unsigned int flags) {
                return syscall(SYS_mbind, addr, len, mode, nodemask, maxnode, flags);
        }
}

#endif // PROMEKI_PLATFORM_LINUX

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Topology cache (Linux only — non-Linux paths skip to the stub).
// ---------------------------------------------------------------------------

namespace {

        // Cached topology values, populated lazily on first
        // isAvailable() call.  -1 means "not yet initialised"; every
        // accessor below pulls them through ensureTopology().
        struct Topology {
                        Atomic<int>  nodeCount{-1};
                        Atomic<int>  maxNode{-1};
                        Atomic<bool> available{false};
                        Atomic<bool> initialised{false};
        };

        Topology &topology() {
                static Topology t;
                return t;
        }

#if defined(PROMEKI_PLATFORM_LINUX)
        // Parse a kernel "list" string like "0-3,5,7-8" into the
        // {min, max+1, count} triple we need.  Used for both
        // /sys/devices/system/node/possible (defines maxNode) and
        // /sys/devices/system/node/online (defines nodeCount).
        // Returns false on parse failure; caller treats that as
        // "no NUMA topology" and falls back to single-node defaults.
        // Walks raw bytes — /sys exposes pure ASCII here.
        bool parseNodeListBuf(const char *buf, int &outCount, int &outMaxPlusOne) {
                outCount      = 0;
                outMaxPlusOne = 0;
                if (buf == nullptr) return false;

                const char *p = buf;
                while (*p != '\0') {
                        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\n') ++p;
                        if (*p == '\0') break;
                        if (*p < '0' || *p > '9') return false;

                        char *end = nullptr;
                        long  lo  = std::strtol(p, &end, 10);
                        long  hi  = lo;
                        p         = end;
                        if (*p == '-') {
                                ++p;
                                if (*p < '0' || *p > '9') return false;
                                hi = std::strtol(p, &end, 10);
                                p  = end;
                        }
                        if (hi < lo) return false;
                        outCount += static_cast<int>(hi - lo + 1);
                        if (hi + 1 > outMaxPlusOne) outMaxPlusOne = static_cast<int>(hi + 1);
                }
                return outCount > 0;
        }

        // Read a small (< 4 KiB) text file from /sys into the
        // caller's buffer.  These are kernel-published ASCII strings
        // — short, never blocking, never partial.  Returns the byte
        // count read (excluding the trailing NUL) or -1 on failure.
        // Always NUL-terminates the buffer on success.
        ssize_t readSysFile(const char *path, char *buf, size_t bufSize) {
                if (bufSize == 0) return -1;
                int fd = ::open(path, O_RDONLY | O_CLOEXEC);
                if (fd < 0) return -1;
                ssize_t total = 0;
                for (;;) {
                        ssize_t n =
                                ::read(fd, buf + total, bufSize - 1 - static_cast<size_t>(total));
                        if (n < 0) {
                                if (errno == EINTR) continue;
                                ::close(fd);
                                return -1;
                        }
                        if (n == 0) break;
                        total += n;
                        if (static_cast<size_t>(total) >= bufSize - 1) break;
                }
                ::close(fd);
                buf[total] = '\0';
                return total;
        }

        bool sysPathExists(const char *path) {
                struct stat st;
                return ::stat(path, &st) == 0;
        }

        void ensureTopologyLinux() {
                Topology &t = topology();
                if (t.initialised.load(MemoryOrder::Acquire)) return;
                static AtomicFlag inFlight;
                while (inFlight.testAndSet(MemoryOrder::Acquire)) {
                        if (t.initialised.load(MemoryOrder::Acquire)) return;
                }
                if (t.initialised.load(MemoryOrder::Acquire)) {
                        inFlight.clear(MemoryOrder::Release);
                        return;
                }

                // Read /sys/devices/system/node/online — present on
                // every kernel built with CONFIG_NUMA, missing on
                // single-node-only kernels.  When absent we treat
                // the system as UMA (one node, no binding).
                char onlineBuf[4096];
                char possibleBuf[4096];
                onlineBuf[0]   = '\0';
                possibleBuf[0] = '\0';
                bool gotOnline =
                        readSysFile("/sys/devices/system/node/online", onlineBuf, sizeof(onlineBuf)) >=
                        0;
                bool gotPossible = readSysFile("/sys/devices/system/node/possible", possibleBuf,
                                               sizeof(possibleBuf)) >= 0;

                int  onlineCount     = 0;
                int  onlineMaxPlus   = 0;
                int  possibleCount   = 0;
                int  possibleMaxPlus = 0;
                bool okOnline   = gotOnline && parseNodeListBuf(onlineBuf, onlineCount, onlineMaxPlus);
                bool okPossible =
                        gotPossible && parseNodeListBuf(possibleBuf, possibleCount, possibleMaxPlus);

                if (okOnline && onlineCount > 0) {
                        t.nodeCount.store(onlineCount, MemoryOrder::Release);
                        t.maxNode.store(okPossible ? possibleMaxPlus - 1 : onlineMaxPlus - 1,
                                        MemoryOrder::Release);
                        t.available.store(onlineCount > 1, MemoryOrder::Release);
                } else {
                        // No /sys topology — degrade gracefully.
                        t.nodeCount.store(1, MemoryOrder::Release);
                        t.maxNode.store(0, MemoryOrder::Release);
                        t.available.store(false, MemoryOrder::Release);
                }
                t.initialised.store(true, MemoryOrder::Release);
                inFlight.clear(MemoryOrder::Release);
        }
#endif // PROMEKI_PLATFORM_LINUX

        void ensureTopology() {
#if defined(PROMEKI_PLATFORM_LINUX)
                ensureTopologyLinux();
#else
                Topology &t = topology();
                if (t.initialised.load(MemoryOrder::Acquire)) return;
                t.nodeCount.store(1, MemoryOrder::Release);
                t.maxNode.store(0, MemoryOrder::Release);
                t.available.store(false, MemoryOrder::Release);
                t.initialised.store(true, MemoryOrder::Release);
#endif
        }

} // namespace

bool Numa::isAvailable() {
        ensureTopology();
        return topology().available.load(MemoryOrder::Acquire);
}

int Numa::nodeCount() {
        ensureTopology();
        return topology().nodeCount.load(MemoryOrder::Acquire);
}

int Numa::maxNode() {
        ensureTopology();
        return topology().maxNode.load(MemoryOrder::Acquire);
}

// ---------------------------------------------------------------------------
// allocOnNode / free
// ---------------------------------------------------------------------------

void *Numa::allocOnNode(size_t bytes, int node) {
        if (bytes == 0) return nullptr;

#if defined(PROMEKI_PLATFORM_LINUX)
        // Round up to page granularity — mmap rejects size 0 and
        // mbind requires page-aligned regions, but we let the kernel
        // do the rounding via mmap and ignore the trailing slack.
        const long pageSize = sysconf(_SC_PAGESIZE);
        const size_t rounded = pageSize > 0
                ? ((bytes + static_cast<size_t>(pageSize) - 1) & ~(static_cast<size_t>(pageSize) - 1))
                : bytes;

        void *ptr = mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
                Error err = Error::syserr();
                promekiWarn("Numa::allocOnNode: mmap(%zu) failed (%s)", rounded, err.desc().cstr());
                return nullptr;
        }

        if (node >= 0 && isAvailable()) {
                // Build a node mask covering exactly the requested
                // node.  maxnode is the highest bit + 1; we use
                // maxNode() + 1 to size the mask.  mbind permits a
                // larger mask than strictly needed, which is helpful
                // because the kernel rejects masks that are too small
                // for the requested node ID.
                const int       maxN     = maxNode();
                const unsigned  bitsNeeded = static_cast<unsigned>(maxN >= node ? maxN + 1 : node + 1);
                const unsigned  longs    = (bitsNeeded + 8 * sizeof(unsigned long) - 1) /
                                       (8 * sizeof(unsigned long));
                const unsigned long maxnodeArg = bitsNeeded;
                List<unsigned long> mask;
                mask.resize(longs);
                for (unsigned i = 0; i < longs; ++i) mask[i] = 0;
                mask[node / (8 * sizeof(unsigned long))] |=
                        (1UL << (node % (8 * sizeof(unsigned long))));

                long rc = sys_mbind(ptr, rounded, MPOL_BIND, mask.data(), maxnodeArg, 0);
                if (rc != 0) {
                        Error err = Error::syserr();
                        promekiWarn("Numa::allocOnNode: mbind(node=%d) failed (%s); "
                                    "allocation kept but unbound", node, err.desc().cstr());
                        // Don't fail the allocation — the region is still
                        // usable, just not node-bound.  Matches the
                        // PinnedHost soft-fail-on-mlock contract.
                }
        }
        return ptr;
#else
        // Stub path: plain page-aligned allocation.  numa_node is
        // ignored.  Use posix_memalign so non-Linux POSIX (BSD,
        // Apple) still gets a page-aligned pointer; on Windows we
        // fall back to _aligned_malloc.
        (void)node;
#  if defined(PROMEKI_PLATFORM_WINDOWS)
        // _aligned_malloc requires the alignment to be a power of two,
        // and matches free via _aligned_free.
        const size_t pageSize = 4096; // not always exactly the page size, but a safe default
        return ::_aligned_malloc(bytes, pageSize);
#  else
        void *ptr = nullptr;
        const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        if (posix_memalign(&ptr, pageSize, bytes) != 0) {
                promekiWarn("Numa::allocOnNode posix_memalign(%zu, %zu) failed", pageSize, bytes);
                return nullptr;
        }
        return ptr;
#  endif
#endif
}

Error Numa::free(void *ptr, size_t bytes) {
        if (ptr == nullptr) return Error::Ok;
#if defined(PROMEKI_PLATFORM_LINUX)
        const long pageSize = sysconf(_SC_PAGESIZE);
        const size_t rounded = pageSize > 0
                ? ((bytes + static_cast<size_t>(pageSize) - 1) & ~(static_cast<size_t>(pageSize) - 1))
                : bytes;
        if (munmap(ptr, rounded) != 0) {
                Error e = Error::syserr();
                promekiWarn("Numa::free munmap(%p, %zu) failed: %s (errno=%d)",
                            ptr, rounded, e.name().cstr(), e.systemError());
                return e;
        }
        return Error::Ok;
#elif defined(PROMEKI_PLATFORM_WINDOWS)
        (void)bytes;
        ::_aligned_free(ptr);
        return Error::Ok;
#else
        (void)bytes;
        std::free(ptr);
        return Error::Ok;
#endif
}

// ---------------------------------------------------------------------------
// Topology lookups
// ---------------------------------------------------------------------------

int Numa::nodeOfNic(const String &interfaceName) {
        if (interfaceName.isEmpty()) return NodeAny;
#if defined(PROMEKI_PLATFORM_LINUX)
        if (!isAvailable()) return NodeAny;
        // Path: /sys/class/net/<iface>/device/numa_node — present
        // when the interface is backed by a PCI device; loopback
        // (lo), bridges, and virtual interfaces typically have no
        // device subdirectory and we report NodeAny for those.
        char path[512];
        std::snprintf(path, sizeof(path), "/sys/class/net/%s/device/numa_node",
                      interfaceName.cstr());
        char    raw[64];
        ssize_t n = readSysFile(path, raw, sizeof(raw));
        if (n <= 0) return NodeAny;
        long v = std::strtol(raw, nullptr, 10);
        if (v < 0) return NodeAny;
        return static_cast<int>(v);
#else
        return NodeAny;
#endif
}

int Numa::nodeOfCpu(int cpu) {
#if defined(PROMEKI_PLATFORM_LINUX)
        if (cpu < 0) return NodeAny;
        if (!isAvailable()) return NodeAny;
        // Walk /sys/devices/system/node/node*/cpu<cpu> existence —
        // the CPU's node owns a symlink with that name.  Bound by
        // maxNode() + 1 to keep the loop finite even when the
        // topology is sparse.
        const int maxN = maxNode();
        for (int n = 0; n <= maxN; ++n) {
                char path[256];
                std::snprintf(path, sizeof(path), "/sys/devices/system/node/node%d/cpu%d", n, cpu);
                if (sysPathExists(path)) return n;
        }
        return NodeAny;
#else
        (void)cpu;
        return NodeAny;
#endif
}

int Numa::currentNode() {
#if defined(PROMEKI_PLATFORM_LINUX)
        int cpu = sched_getcpu();
        if (cpu < 0) return NodeAny;
        return Numa::nodeOfCpu(cpu);
#else
        return NodeAny;
#endif
}

PROMEKI_NAMESPACE_END
