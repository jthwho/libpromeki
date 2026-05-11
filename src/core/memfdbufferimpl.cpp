/**
 * @file      memfdbufferimpl.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/config.h>

#if PROMEKI_ENABLE_MEMFD

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <promeki/memfdbufferimpl.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        // /proc/self/pagemap bit layout (see Documentation/admin-guide/mm/pagemap.rst).
        // Bit 63 = page present, bit 62 = page swapped, bit 61 = page is file-page
        // or shared-anon, bit 56 = exclusively mapped.  Bits 0-54 (PFN) are zeroed
        // for unprivileged readers since Linux 4.0/4.2; the high status bits stay
        // readable.  We only need bits 63 and 61 to distinguish CoW'd anonymous
        // pages from kernel-CoW-from-file pages — neither requires CAP_SYS_ADMIN.
        constexpr uint64_t kPmPresent = 1ULL << 63;
        constexpr uint64_t kPmFile    = 1ULL << 61;

        // Copy only privately-CoW'd pages (anon pages that the source view
        // dirtied) from @p src to @p dst.  Pages that are still kernel-CoW
        // shared with the sealed file are skipped — the new MAP_PRIVATE
        // clone resolves them lazily to the same file pages on read.
        //
        // This is the heart of the SystemCow per-frame win: when the source
        // is a per-frame clone with a small dirty band (TPG burn-in,
        // BurnMediaIO overlay, etc.), we copy only the band's pages instead
        // of the whole frame.  An untouched 16 MiB frame's "modified" page
        // count is zero — the entire copy is skipped.
        //
        // Returns the number of bytes actually memcpy'd (for telemetry).
        // Falls back to a full memcpy on any pagemap-read failure so the
        // semantics stay correct even on kernels / sandboxes where the
        // pagemap isn't readable.  TOCTOU caveat: callers must ensure no
        // other thread is writing through @p src while this runs; the TPG
        // / payload-detach paths already serialise on a single strand.
        size_t copyDirtyPages(const void *src, void *dst, size_t bytes) {
                if (bytes == 0 || src == nullptr || dst == nullptr) return 0;
                long pgRaw = ::sysconf(_SC_PAGESIZE);
                if (pgRaw <= 0) {
                        std::memcpy(dst, src, bytes);
                        return bytes;
                }
                const size_t pageSize  = static_cast<size_t>(pgRaw);
                const size_t pageCount = (bytes + pageSize - 1) / pageSize;

                int fd = ::open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
                if (fd < 0) {
                        std::memcpy(dst, src, bytes);
                        return bytes;
                }

                const uintptr_t startVa       = reinterpret_cast<uintptr_t>(src);
                const off_t     pageMapOffset = static_cast<off_t>((startVa / pageSize) * sizeof(uint64_t));
                if (::lseek(fd, pageMapOffset, SEEK_SET) < 0) {
                        ::close(fd);
                        std::memcpy(dst, src, bytes);
                        return bytes;
                }

                constexpr size_t kChunkPages = 1024;
                uint64_t         entries[kChunkPages];
                size_t           bytesCopied    = 0;
                size_t           pagesProcessed = 0;
                while (pagesProcessed < pageCount) {
                        const size_t  toRead = std::min(kChunkPages, pageCount - pagesProcessed);
                        const ssize_t n      = ::read(fd, entries, toRead * sizeof(uint64_t));
                        if (n != static_cast<ssize_t>(toRead * sizeof(uint64_t))) {
                                // Short read or error — fall back to memcpy of the remainder.
                                const size_t remaining = bytes - pagesProcessed * pageSize;
                                std::memcpy(static_cast<uint8_t *>(dst) + pagesProcessed * pageSize,
                                            static_cast<const uint8_t *>(src) + pagesProcessed * pageSize, remaining);
                                bytesCopied += remaining;
                                break;
                        }
                        for (size_t i = 0; i < toRead; ++i) {
                                const uint64_t e = entries[i];
                                if ((e & kPmPresent) && !(e & kPmFile)) {
                                        // Privately CoW'd anonymous page — copy it.
                                        const size_t pageIdx    = pagesProcessed + i;
                                        const size_t pageOffset = pageIdx * pageSize;
                                        const size_t copyBytes  = std::min(pageSize, bytes - pageOffset);
                                        std::memcpy(static_cast<uint8_t *>(dst) + pageOffset,
                                                    static_cast<const uint8_t *>(src) + pageOffset, copyBytes);
                                        bytesCopied += copyBytes;
                                }
                        }
                        pagesProcessed += toRead;
                }
                ::close(fd);
                return bytesCopied;
        }

        // Read Private_Dirty (kB) for the VMA whose start address matches
        // @p addr from /proc/self/smaps.  Returns 0 if the VMA isn't
        // found or the file can't be opened.  Each VMA's smaps block
        // begins with "<start>-<end> ..." on its first line; subsequent
        // lines list properties until the next start-end header.
        size_t privateDirtyBytes(const void *addr) {
                if (addr == nullptr) return 0;
                std::FILE *fp = std::fopen("/proc/self/smaps", "r");
                if (fp == nullptr) return 0;
                char         line[512];
                bool         inBlock = false;
                size_t       result  = 0;
                const auto   want    = reinterpret_cast<uintptr_t>(addr);
                while (std::fgets(line, sizeof(line), fp) != nullptr) {
                        unsigned long start = 0, end = 0;
                        if (std::sscanf(line, "%lx-%lx", &start, &end) == 2) {
                                inBlock = (start == want);
                                continue;
                        }
                        if (inBlock && std::strncmp(line, "Private_Dirty:", 14) == 0) {
                                long kb = 0;
                                if (std::sscanf(line + 14, "%ld", &kb) == 1 && kb > 0) {
                                        result = static_cast<size_t>(kb) * 1024;
                                }
                                break;
                        }
                }
                std::fclose(fp);
                return result;
        }
}

MemfdBufferImpl::MemfdBufferImpl(const MemSpace &ms, size_t bytes, size_t align)
    : HostMappedBufferImpl(ms, nullptr, 0, align) {
        // Construct the region via SharedPtr so future clones share
        // the same fd; the producer view is owned by the region itself
        // and persists until seal() drops it.
        _region = RegionPtr::takeOwnership(new MemfdRegion(bytes, "MemfdBufferImpl"));
        if (!_region.isValid() || !_region->isValid()) {
                ms.stats().allocFailCount.fetchAndAdd(1);
                _dead = true;
                return;
        }
        // The non-const modify() returns a writable region; mapping
        // the producer view is a state change on the region.
        void *prod = _region.modify()->producerView();
        if (prod == nullptr) {
                ms.stats().allocFailCount.fetchAndAdd(1);
                _dead = true;
                _region = RegionPtr();
                return;
        }
        _hostPtr   = prod;
        _allocSize = bytes;
        ms.stats().recordAlloc(static_cast<uint64_t>(bytes));
}

MemfdBufferImpl::MemfdBufferImpl(const MemfdBufferImpl &source, void *cloneView)
    : HostMappedBufferImpl(source._memSpace, cloneView, source._allocSize, source._align) {
        // Sibling clone: shares the underlying region (refcount bump
        // on the SharedPtr); already born sealed because the source
        // had to seal before we got here.  Born _dirty=true so any
        // *later* detach from this clone memcpy's the current view
        // content into the new clone — preserving writes the caller
        // is presumably about to make, exactly mirroring the
        // deep-copy semantics HostBufferImpl::_promeki_clone has.
        _region        = source._region;
        _sealed        = true;
        _dirty         = true;
        _logicalSize   = source._logicalSize;
        _shift         = source._shift;
        _memSpace.stats().recordAlloc(static_cast<uint64_t>(_allocSize));
}

MemfdBufferImpl::~MemfdBufferImpl() {
        if (_hostPtr != nullptr) {
                // NOTE (former destructor-time peak-resident sample):
                // We used to call @c residentBytes() here so the MemSpace
                // peak watermark caught the high-water private-dirty
                // value before the mapping vanished.  In practice the
                // hot paths in libpromeki (TPG burn-in, FrameBridge,
                // etc.) destruct hundreds of MAP_PRIVATE clones per
                // second, and each call walks all of @c /proc/self/smaps
                // sequentially — observed at ~500 us per destruct in a
                // mediaplay TPG → NV12 pipeline.  The peak watermark is
                // not worth that cost on every drop; callers that want
                // resident-byte telemetry should sample explicitly via
                // @c Buffer::residentBytes() on live buffers.
                if (_sealed) {
                        // Caller-owned MAP_PRIVATE / sibling clone view —
                        // release through the region.
                        if (_region.isValid()) {
                                Error err = _region.modify()->releaseView(_hostPtr);
                                if (err.isError()) {
                                        promekiWarn("MemfdBufferImpl::~: releaseView failed: %s",
                                                    err.desc().cstr());
                                }
                        }
                } else {
                        // Producer phase — the producer view is owned by
                        // the region and torn down by ~MemfdRegion when
                        // the last shared reference drops.  Nothing to
                        // do here other than null out our copy of the
                        // pointer (debug hygiene).
                }
                _hostPtr = nullptr;
                _memSpace.stats().recordRelease(static_cast<uint64_t>(_allocSize));
        }
        _region = RegionPtr();
}

bool MemfdBufferImpl::canClone() const {
        if (_dead) return false;
        if (!_region.isValid() || !_region->isValid()) return false;
        return _hostPtr != nullptr;
}

Error MemfdBufferImpl::seal() const {
        Mutex::Locker lock(_sealMutex);
        if (_dead) return Error::Invalid;
        if (_sealed) return Error::Ok;
        if (!_region.isValid() || !_region->isValid()) return Error::Invalid;

        void *newView = nullptr;
        // Use modify() to reach the non-const region API.  modify()
        // is a no-op on CoW=false so it does not detach.
        Error err = const_cast<MemfdBufferImpl *>(this)->_region.modify()->seal(&newView);
        if (err.isError()) {
                // Region is now in its dead state — _hostPtr was
                // referencing the unmapped producer view.  Latch our
                // own dead flag, null the pointer, and surface the
                // error for the caller to drop the buffer.
                _hostPtr = nullptr;
                _dead    = true;
                return err;
        }
        _hostPtr = newView;
        _sealed  = true;
        return Error::Ok;
}

size_t MemfdBufferImpl::residentBytes() const {
        // Pre-seal: the producer wrote everything, so resident ==
        // allocation size minus whatever the kernel hasn't faulted in
        // yet.  /proc/self/smaps Private_Dirty doesn't help here
        // because the producer view is MAP_SHARED, not MAP_PRIVATE.
        // Reporting allocSize is the conservative + correct answer.
        if (!_sealed) return _allocSize;
        if (_hostPtr == nullptr) return 0;
        return privateDirtyBytes(_hostPtr);
}

MemfdBufferImpl *MemfdBufferImpl::_promeki_clone() const {
        // Auto-seal first if the caller forgot.  seal() is idempotent.
        if (!_sealed) {
                Error err = seal();
                if (err.isError()) return nullptr;
        }
        if (!_region.isValid() || !_region->isValid()) return nullptr;

        Error err     = Error::Ok;
        void *cloneVA = const_cast<MemfdBufferImpl *>(this)->_region.modify()->cloneView(&err);
        if (cloneVA == nullptr) {
                promekiWarn("MemfdBufferImpl::_promeki_clone: cloneView failed: %s", err.desc().cstr());
                return nullptr;
        }
        // Preserve modifications made through this view.  Sibling
        // clones inherit "_dirty=true" at construction (see the
        // sibling-clone constructor); when they're cloned in turn,
        // we copy this view's current content (sealed-source pages
        // plus any private CoW modifications) into the new clone.
        // The MAP_PRIVATE-from-sealed-file path would otherwise drop
        // private modifications since the kernel CoWs from file
        // pages, not from this view's anonymous pages.
        //
        // We use a pagemap-based per-page copy (copyDirtyPages) so
        // we only pay for pages that have actually been written
        // through this view.  Untouched pages stay shared with the
        // file's page cache via the new clone's MAP_PRIVATE — that
        // is the headline SystemCow win.  TPG burn-in writes a
        // small text band; only those pages copy here.
        //
        // Post-seal sources stay _dirty=false: the first detach from
        // the cached payload (TPG's per-frame transition, NDI's
        // ingest hand-off, etc.) skips this path entirely and uses
        // cheap kernel CoW from sealed file pages.
        if (_dirty && _hostPtr != nullptr) {
                copyDirtyPages(_hostPtr, cloneVA, _allocSize);
        }
        return new MemfdBufferImpl(*this, cloneVA);
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_MEMFD
