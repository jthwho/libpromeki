/**
 * @file      memfdregion.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/memfdregion.h>
#include <promeki/config.h>
#include <promeki/logger.h>

#if PROMEKI_ENABLE_MEMFD
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#endif

PROMEKI_NAMESPACE_BEGIN

#if PROMEKI_ENABLE_MEMFD

namespace {
        size_t roundUpToPage(size_t bytes) {
                long page = ::sysconf(_SC_PAGESIZE);
                if (page <= 0) page = 4096;
                size_t pg = static_cast<size_t>(page);
                if (bytes == 0) return pg;
                return ((bytes + pg - 1) / pg) * pg;
        }
} // namespace

bool MemfdRegion::isSupported() { return true; }

MemfdRegion::MemfdRegion()
    : _fd(-1), _size(0), _producer(nullptr), _sealed(false), _dead(false) {}

MemfdRegion::MemfdRegion(size_t bytes, const String &debugName)
    : _fd(-1), _size(0), _producer(nullptr), _sealed(false), _dead(false) {
        const size_t rounded = roundUpToPage(bytes);
        const char  *name    = debugName.isEmpty() ? "anonymous" : debugName.cstr();
        int          fd      = ::memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (fd < 0) {
                promekiWarn("MemfdRegion: memfd_create(\"%s\") failed: %s", name, std::strerror(errno));
                return;
        }
        if (::ftruncate(fd, static_cast<off_t>(rounded)) != 0) {
                promekiWarn("MemfdRegion: ftruncate(%zu) failed: %s", rounded, std::strerror(errno));
                ::close(fd);
                return;
        }
        _fd   = fd;
        _size = rounded;
}

MemfdRegion::~MemfdRegion() { closeAndReset(); }

MemfdRegion::MemfdRegion(MemfdRegion &&other) noexcept
    : _fd(other._fd),
      _size(other._size),
      _producer(other._producer),
      _sealed(other._sealed),
      _dead(other._dead) {
        other._fd       = -1;
        other._size     = 0;
        other._producer = nullptr;
        other._sealed   = false;
        other._dead     = false;
}

MemfdRegion &MemfdRegion::operator=(MemfdRegion &&other) noexcept {
        if (this != &other) {
                closeAndReset();
                _fd       = other._fd;
                _size     = other._size;
                _producer = other._producer;
                _sealed   = other._sealed;
                _dead     = other._dead;
                other._fd       = -1;
                other._size     = 0;
                other._producer = nullptr;
                other._sealed   = false;
                other._dead     = false;
        }
        return *this;
}

void MemfdRegion::closeAndReset() noexcept {
        if (_producer != nullptr) {
                ::munmap(_producer, _size);
                _producer = nullptr;
        }
        if (_fd >= 0) {
                ::close(_fd);
                _fd = -1;
        }
        _size   = 0;
        _sealed = false;
        _dead   = false;
}

bool MemfdRegion::isValid() const { return _fd >= 0 && !_dead; }
bool MemfdRegion::isSealed() const { return _sealed; }

void *MemfdRegion::producerView() {
        if (!isValid()) return nullptr;
        if (_sealed) return nullptr;
        if (_producer != nullptr) return _producer;
        void *p = ::mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, _fd, 0);
        if (p == MAP_FAILED) {
                promekiWarn("MemfdRegion::producerView: mmap failed: %s", std::strerror(errno));
                return nullptr;
        }
        _producer = p;
        return _producer;
}

Error MemfdRegion::seal(void **outFirstClone) {
        if (_dead) return Error::Invalid;
        if (_fd < 0) return Error::Invalid;
        if (_sealed) return Error::Ok;

        // Step 1: drop the producer view so no writable shared mapping
        // remains.  F_SEAL_WRITE refuses to apply while one exists.
        if (_producer != nullptr) {
                if (::munmap(_producer, _size) != 0) {
                        // Rare; usually a kernel/driver bug.  Leave _producer pointing at
                        // unmapped memory and latch the dead state — calling code is
                        // expected to discard the region.
                        promekiErr("MemfdRegion::seal: munmap producer failed: %s", std::strerror(errno));
                        _producer = nullptr;
                        _dead     = true;
                        return Error::syserr();
                }
                _producer = nullptr;
        }

        // Step 2: optional atomic first clone.  MAP_PRIVATE before
        // F_ADD_SEALS is intentional: F_SEAL_WRITE only refuses
        // writable *shared* mappings, and a MAP_PRIVATE|PROT_WRITE
        // mapping is a CoW-into-anonymous-pages mapping, not a write
        // path back to the file.  Do not "fix" this ordering.
        void *firstClone = nullptr;
        if (outFirstClone != nullptr) {
                firstClone = ::mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_PRIVATE, _fd, 0);
                if (firstClone == MAP_FAILED) {
                        promekiErr("MemfdRegion::seal: atomic clone mmap failed: %s", std::strerror(errno));
                        _dead = true;
                        return Error::syserr();
                }
        }

        // Step 3: apply the seals.
        if (::fcntl(_fd, F_ADD_SEALS, F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW) != 0) {
                Error err = Error::syserr();
                promekiErr("MemfdRegion::seal: F_ADD_SEALS failed: %s", std::strerror(errno));
                if (firstClone != nullptr) ::munmap(firstClone, _size);
                _dead = true;
                return err;
        }

        if (outFirstClone != nullptr) *outFirstClone = firstClone;
        _sealed = true;
        return Error::Ok;
}

void *MemfdRegion::cloneView(Error *err) {
        if (!isValid()) {
                if (err) *err = Error::Invalid;
                return nullptr;
        }
        if (!_sealed) {
                if (err) *err = Error::NotReady;
                return nullptr;
        }
        void *p = ::mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_PRIVATE, _fd, 0);
        if (p == MAP_FAILED) {
                Error e = Error::syserr();
                promekiWarn("MemfdRegion::cloneView: mmap failed: %s", std::strerror(errno));
                if (err) *err = e;
                return nullptr;
        }
        if (err) *err = Error::Ok;
        return p;
}

void *MemfdRegion::readOnlyView(Error *err) {
        if (!isValid()) {
                if (err) *err = Error::Invalid;
                return nullptr;
        }
        if (!_sealed) {
                if (err) *err = Error::NotReady;
                return nullptr;
        }
        void *p = ::mmap(nullptr, _size, PROT_READ, MAP_SHARED, _fd, 0);
        if (p == MAP_FAILED) {
                Error e = Error::syserr();
                promekiWarn("MemfdRegion::readOnlyView: mmap failed: %s", std::strerror(errno));
                if (err) *err = e;
                return nullptr;
        }
        if (err) *err = Error::Ok;
        return p;
}

Error MemfdRegion::releaseView(void *p) {
        if (p == nullptr) return Error::Invalid;
        if (::munmap(p, _size) != 0) {
                promekiWarn("MemfdRegion::releaseView: munmap failed: %s", std::strerror(errno));
                return Error::syserr();
        }
        return Error::Ok;
}

Error MemfdRegion::adviseProducer(int madviseFlag) {
        if (!isValid()) return Error::Invalid;
        if (_sealed || _producer == nullptr) return Error::NotReady;
        if (::madvise(_producer, _size, madviseFlag) != 0) return Error::syserr();
        return Error::Ok;
}

Error MemfdRegion::adviseView(void *p, int madviseFlag) {
        if (p == nullptr) return Error::Invalid;
        if (::madvise(p, _size, madviseFlag) != 0) return Error::syserr();
        return Error::Ok;
}

#else // !PROMEKI_ENABLE_MEMFD — non-Linux stub

bool MemfdRegion::isSupported() { return false; }

MemfdRegion::MemfdRegion()
    : _fd(-1), _size(0), _producer(nullptr), _sealed(false), _dead(true) {}

MemfdRegion::MemfdRegion(size_t, const String &)
    : _fd(-1), _size(0), _producer(nullptr), _sealed(false), _dead(true) {}

MemfdRegion::~MemfdRegion() {}

MemfdRegion::MemfdRegion(MemfdRegion &&) noexcept
    : _fd(-1), _size(0), _producer(nullptr), _sealed(false), _dead(true) {}

MemfdRegion &MemfdRegion::operator=(MemfdRegion &&) noexcept { return *this; }

void MemfdRegion::closeAndReset() noexcept {}

bool   MemfdRegion::isValid() const { return false; }
bool   MemfdRegion::isSealed() const { return false; }
void  *MemfdRegion::producerView() { return nullptr; }
Error  MemfdRegion::seal(void **) { return Error::NotSupported; }
void  *MemfdRegion::cloneView(Error *err) {
        if (err) *err = Error::NotSupported;
        return nullptr;
}
void  *MemfdRegion::readOnlyView(Error *err) {
        if (err) *err = Error::NotSupported;
        return nullptr;
}
Error  MemfdRegion::releaseView(void *) { return Error::NotSupported; }
Error  MemfdRegion::adviseProducer(int) { return Error::NotSupported; }
Error  MemfdRegion::adviseView(void *, int) { return Error::NotSupported; }

#endif // PROMEKI_ENABLE_MEMFD

PROMEKI_NAMESPACE_END
