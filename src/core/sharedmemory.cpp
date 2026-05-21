/**
 * @file      sharedmemory.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/list.h>
#include <promeki/sharedmemory.h>
#include <promeki/platform.h>
#include <promeki/logger.h>

#include <cstring>
#include <utility>
#include <vector>

#if defined(PROMEKI_PLATFORM_POSIX)
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

namespace {

#if defined(PROMEKI_PLATFORM_POSIX)

        // Canonicalize a caller-supplied name: must start with a single '/' and
        // contain no other slashes.  Returns Error::Invalid on a bad name; on
        // success, @p out holds the canonical form and errnoOut is untouched.
        Error canonicalizeShmName(const String &name, String &out) {
                if (name.isEmpty()) {
                        promekiWarn("SharedMemory: refused empty name");
                        return Error::Invalid;
                }
                String canonical;
                if (name.byteAt(0) != '/') {
                        canonical = String("/") + name;
                } else {
                        canonical = name;
                }
                // After the leading slash there must be no further '/'.
                for (size_t i = 1; i < canonical.byteCount(); ++i) {
                        if (canonical.byteAt(i) == '/') {
                                promekiWarn("SharedMemory: invalid name '%s' (contains '/' after position 0)",
                                            name.cstr());
                                return Error::Invalid;
                        }
                }
                out = canonical;
                return Error::Ok;
        }

#endif // PROMEKI_PLATFORM_POSIX

} // namespace

bool SharedMemory::isSupported() {
#if defined(PROMEKI_PLATFORM_POSIX)
        return true;
#else
        return false;
#endif
}

#if defined(PROMEKI_PLATFORM_POSIX)

Error SharedMemory::unlink(const String &name) {
        String canonical;
        Error  err = canonicalizeShmName(name, canonical);
        if (err.isError()) return err;
        if (::shm_unlink(canonical.cstr()) != 0 && errno != ENOENT) {
                Error e = Error::syserr();
                promekiWarn("SharedMemory::unlink('%s') failed: %s (errno=%d)",
                            canonical.cstr(), e.name().cstr(), e.systemError());
                return e;
        }
        return Error::Ok;
}

#else // !PROMEKI_PLATFORM_POSIX

Error SharedMemory::unlink(const String &name) {
        promekiWarnOnce("SharedMemory::unlink('%s') refused: not supported on this platform", name.cstr());
        return Error::NotSupported;
}

#endif // PROMEKI_PLATFORM_POSIX

SharedMemory::SharedMemory() = default;

SharedMemory::~SharedMemory() {
        close();
}

SharedMemory::SharedMemory(SharedMemory &&other) noexcept
    : _name(std::move(other._name)), _data(other._data), _size(other._size), _handle(other._handle),
      _owner(other._owner), _access(other._access) {
        other._data = nullptr;
        other._size = 0;
        other._handle = -1;
        other._owner = false;
        other._access = ReadOnly;
}

SharedMemory &SharedMemory::operator=(SharedMemory &&other) noexcept {
        if (this != &other) {
                close();
                _name = std::move(other._name);
                _data = other._data;
                _size = other._size;
                _handle = other._handle;
                _owner = other._owner;
                _access = other._access;
                other._data = nullptr;
                other._size = 0;
                other._handle = -1;
                other._owner = false;
                other._access = ReadOnly;
        }
        return *this;
}

#if defined(PROMEKI_PLATFORM_POSIX)

Error SharedMemory::create(const String &name, size_t size, uint32_t mode, const String &groupName) {
        if (isValid()) {
                promekiWarn("SharedMemory::create('%s') refused: already open as '%s'",
                            name.cstr(), _name.cstr());
                return Error::AlreadyOpen;
        }
        if (size == 0) {
                promekiWarn("SharedMemory::create('%s') refused: size=0", name.cstr());
                return Error::Invalid;
        }

        String canonical;
        Error  err = canonicalizeShmName(name, canonical);
        if (err.isError()) return err;

        // O_EXCL makes this fail loudly if the name is already in use,
        // matching the documented contract.
        int fd = ::shm_open(canonical.cstr(), O_CREAT | O_EXCL | O_RDWR, static_cast<mode_t>(mode));
        if (fd < 0) {
                Error e = Error::syserr();
                promekiWarn("SharedMemory::create('%s', size=%zu) shm_open failed: %s (errno=%d)",
                            canonical.cstr(), size, e.name().cstr(), e.systemError());
                return e;
        }

        // umask on the running process may have masked bits off the
        // requested mode; force the exact mode explicitly.
        if (::fchmod(fd, static_cast<mode_t>(mode)) != 0) {
                Error fmErr = Error::syserr();
                promekiWarn("SharedMemory::create('%s') fchmod(0%o) failed: %s (errno=%d)",
                            canonical.cstr(), (unsigned)mode, fmErr.name().cstr(), fmErr.systemError());
                ::close(fd);
                ::shm_unlink(canonical.cstr());
                return fmErr;
        }

        if (!groupName.isEmpty()) {
                // Resolve the group; getgrnam_r avoids the static-buffer
                // races of getgrnam(3).
                struct group  gr;
                struct group *result = nullptr;
                long          bufMax = ::sysconf(_SC_GETGR_R_SIZE_MAX);
                if (bufMax <= 0) bufMax = 16384;
                List<char> buf(static_cast<size_t>(bufMax));
                int               rc = ::getgrnam_r(groupName.cstr(), &gr, buf.data(), buf.size(), &result);
                if (rc != 0 || result == nullptr) {
                        Error grErr = (rc == 0) ? Error(Error::NotExist) : Error::syserr(rc);
                        promekiWarn("SharedMemory::create('%s') getgrnam_r('%s') failed: %s (rc=%d)",
                                    canonical.cstr(), groupName.cstr(), grErr.name().cstr(), rc);
                        ::close(fd);
                        ::shm_unlink(canonical.cstr());
                        return grErr;
                }
                if (::fchown(fd, static_cast<uid_t>(-1), gr.gr_gid) != 0) {
                        Error chErr = Error::syserr();
                        promekiWarn("SharedMemory::create('%s') fchown(group='%s') failed: %s (errno=%d)",
                                    canonical.cstr(), groupName.cstr(), chErr.name().cstr(),
                                    chErr.systemError());
                        ::close(fd);
                        ::shm_unlink(canonical.cstr());
                        return chErr;
                }
        }

        if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
                Error ftErr = Error::syserr();
                promekiWarn("SharedMemory::create('%s') ftruncate(%zu) failed: %s (errno=%d)",
                            canonical.cstr(), size, ftErr.name().cstr(), ftErr.systemError());
                ::close(fd);
                ::shm_unlink(canonical.cstr());
                return ftErr;
        }

        void *addr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
                Error mmErr = Error::syserr();
                promekiWarn("SharedMemory::create('%s') mmap(%zu) failed: %s (errno=%d)",
                            canonical.cstr(), size, mmErr.name().cstr(), mmErr.systemError());
                ::close(fd);
                ::shm_unlink(canonical.cstr());
                return mmErr;
        }

        _name = std::move(canonical);
        _data = addr;
        _size = size;
        _handle = static_cast<intptr_t>(fd);
        _owner = true;
        _access = ReadWrite;
        return Error::Ok;
}

Error SharedMemory::open(const String &name, Access access) {
        if (isValid()) {
                promekiWarn("SharedMemory::open('%s') refused: already open as '%s'",
                            name.cstr(), _name.cstr());
                return Error::AlreadyOpen;
        }

        String canonical;
        Error  err = canonicalizeShmName(name, canonical);
        if (err.isError()) return err;

        int openFlags = (access == ReadOnly) ? O_RDONLY : O_RDWR;
        int fd = ::shm_open(canonical.cstr(), openFlags, 0);
        if (fd < 0) {
                Error e = Error::syserr();
                promekiWarn("SharedMemory::open('%s', access=%d) shm_open failed: %s (errno=%d)",
                            canonical.cstr(), (int)access, e.name().cstr(), e.systemError());
                return e;
        }

        // Learn the size from the file; callers don't need to know it.
        struct stat st{};
        if (::fstat(fd, &st) != 0) {
                Error stErr = Error::syserr();
                promekiWarn("SharedMemory::open('%s') fstat failed: %s (errno=%d)",
                            canonical.cstr(), stErr.name().cstr(), stErr.systemError());
                ::close(fd);
                return stErr;
        }
        if (st.st_size <= 0) {
                promekiWarn("SharedMemory::open('%s') refused: zero-sized region", canonical.cstr());
                ::close(fd);
                return Error::Invalid;
        }
        size_t sz = static_cast<size_t>(st.st_size);

        int   prot = (access == ReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE);
        void *addr = ::mmap(nullptr, sz, prot, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
                Error mmErr = Error::syserr();
                promekiWarn("SharedMemory::open('%s') mmap(%zu) failed: %s (errno=%d)",
                            canonical.cstr(), sz, mmErr.name().cstr(), mmErr.systemError());
                ::close(fd);
                return mmErr;
        }

        _name = std::move(canonical);
        _data = addr;
        _size = sz;
        _handle = static_cast<intptr_t>(fd);
        _owner = false;
        _access = access;
        return Error::Ok;
}

void SharedMemory::close() {
        if (_data != nullptr) {
                ::munmap(_data, _size);
                _data = nullptr;
        }
        if (_handle >= 0) {
                ::close(static_cast<int>(_handle));
                _handle = -1;
        }
        if (_owner && !_name.isEmpty()) {
                if (::shm_unlink(_name.cstr()) != 0 && errno != ENOENT) {
                        promekiWarn("SharedMemory: shm_unlink('%s') failed: %s", _name.cstr(), ::strerror(errno));
                }
        }
        // Reset via move-assign rather than clear() — clear() dereferences
        // the underlying SharedPtr, which may be null after a move.
        _name = String();
        _size = 0;
        _owner = false;
        _access = ReadOnly;
}

#else // !PROMEKI_PLATFORM_POSIX

Error SharedMemory::create(const String &name, size_t, uint32_t, const String &) {
        promekiWarnOnce("SharedMemory::create('%s') refused: not supported on this platform", name.cstr());
        return Error::NotSupported;
}

Error SharedMemory::open(const String &name, Access) {
        promekiWarnOnce("SharedMemory::open('%s') refused: not supported on this platform", name.cstr());
        return Error::NotSupported;
}

void SharedMemory::close() {
        _data = nullptr;
        _size = 0;
        _handle = -1;
        _owner = false;
        _access = ReadOnly;
        // Reset via move-assign rather than clear() — clear() dereferences
        // the underlying SharedPtr, which may be null after a move.
        _name = String();
}

#endif // PROMEKI_PLATFORM_POSIX

PROMEKI_NAMESPACE_END
