/**
 * @file      file.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/file.h>
#include <promeki/resource.h>

#include <cirf/types.h>

#include <algorithm>
#include <cstring>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>
#endif

PROMEKI_NAMESPACE_BEGIN

File::File(ObjectBase *parent) :
        BufferedIODevice(parent) { }

File::File(const String &fn, ObjectBase *parent) :
        BufferedIODevice(parent),
        _filename(fn) { }

File::File(const char *fn, ObjectBase *parent) :
        BufferedIODevice(parent),
        _filename(fn) { }

File::File(const FilePath &fp, ObjectBase *parent) :
        BufferedIODevice(parent),
        _filename(fp.toString()) { }

File::~File() {
        if(isOpen()) close();
}

#if defined(PROMEKI_PLATFORM_WINDOWS)
// FIXME: The windows code here needs love.

bool File::isOpen() const {
        return false;
}

Error File::open(OpenMode mode) {
        return Error(Error::NotImplemented);
}

Error File::open(OpenMode mode, int fileFlags) {
        return Error(Error::NotImplemented);
}

Error File::close() {
        return Error();
}

int64_t File::write(const void *data, int64_t maxSize) {
        return -1;
}

int64_t File::readFromDevice(void *data, int64_t maxSize) {
        return -1;
}

int64_t File::deviceBytesAvailable() const {
        return 0;
}

bool File::isSequential() const {
        return false;
}

Error File::seek(int64_t offset) {
        resetReadBuffer();
        return Error(Error::NotImplemented);
}

int64_t File::pos() const {
        return 0;
}

Result<int64_t> File::size() const {
        return makeError<int64_t>(Error::NotImplemented);
}

bool File::atEnd() const {
        return true;
}

Result<int64_t> File::seekFromCurrent(int64_t offset) const {
        return makeError<int64_t>(Error::NotImplemented);
}

Result<int64_t> File::seekFromEnd(int64_t offset) const {
        return makeError<int64_t>(Error::NotImplemented);
}

Error File::truncate(int64_t offset) const {
        return Error(Error::NotImplemented);
}

int64_t File::writev(const IOVec *iov, int count) {
        (void)iov;
        (void)count;
        return -1;
}

Error File::preallocate(int64_t offset, int64_t length) {
        (void)offset;
        (void)length;
        return Error(Error::NotImplemented);
}

Result<size_t> File::directIOAlignment() const {
        return makeError<size_t>(Error::NotImplemented);
}

Error File::readBulk(Buffer &buf, int64_t size) {
        (void)buf;
        (void)size;
        return Error(Error::NotImplemented);
}

int64_t File::writeBulk(const void *data, int64_t size) {
        (void)data;
        (void)size;
        return -1;
}

Error File::sync(bool dataOnly) {
        (void)dataOnly;
        return Error(Error::NotImplemented);
}

Error File::setDirectIO(bool enable) {
        (void)enable;
        return Error(Error::NotImplemented);
}

Error File::setSynchronous(bool enable) {
        (void)enable;
        return Error(Error::NotImplemented);
}

Error File::setNonBlocking(bool enable) {
        (void)enable;
        return Error(Error::NotImplemented);
}

#else // POSIX

static int buildPosixFlags(IODevice::OpenMode mode, int fileFlags) {
        int ret = O_LARGEFILE;
        if(mode == IODevice::ReadWrite) {
                ret |= O_RDWR;
        } else if(mode == IODevice::ReadOnly) {
                ret |= O_RDONLY;
        } else if(mode == IODevice::WriteOnly) {
                ret |= O_WRONLY;
        }
        if(fileFlags & File::Create)    ret |= O_CREAT;
        if(fileFlags & File::Append)    ret |= O_APPEND;
        if(fileFlags & File::Truncate)  ret |= O_TRUNC;
        if(fileFlags & File::Exclusive) ret |= O_EXCL;
        return ret;
}

bool File::isOpen() const {
        return _handle != FileHandleClosedValue || _resourceFile != nullptr;
}

Error File::open(OpenMode mode) {
        return open(mode, NoFlags);
}

Error File::open(OpenMode mode, int fileFlags) {
        if(isOpen()) return Error(Error::AlreadyOpen);

        // Resource paths (":/...") map to compiled-in cirf data and
        // are served from memory. Only ReadOnly access is supported
        // — there is no writable backing store. Direct-I/O / sync /
        // non-blocking flags are silently ignored: the bytes are
        // already in RAM, so they are meaningless here.
        if(Resource::isResourcePath(_filename)) {
                if(mode != ReadOnly) return Error(Error::ReadOnly);
                _resourceFile = Resource::findFile(_filename);
                if(_resourceFile == nullptr) return Error(Error::NotExist);
                _resourcePos = 0;
                _fileFlags = fileFlags;
                setOpenMode(mode);
                // The buffered read path would just copy bytes that
                // are already in memory, so go unbuffered to keep
                // resource reads truly zero-copy from .rodata.
                setUnbuffered(true);
                return Error();
        }

        int posixFlags = buildPosixFlags(mode, fileFlags);

        // Apply current option state to open flags
        if(_directIO)    posixFlags |= O_DIRECT;
        if(_synchronous) posixFlags |= O_SYNC;
        if(_nonBlocking) posixFlags |= O_NONBLOCK;

        if(posixFlags & O_CREAT) {
                _handle = ::open(_filename.cstr(), posixFlags, 0666);
        } else {
                _handle = ::open(_filename.cstr(), posixFlags);
        }
        if(_handle == FileHandleClosedValue) return Error::syserr();
        _fileFlags = fileFlags;
        setOpenMode(mode);
        if(!_directIO) ensureReadBuffer();
        return Error();
}

Error File::close() {
        if(!isOpen()) return Error();
        aboutToCloseSignal.emit();
        Error err;
        if(_resourceFile != nullptr) {
                _resourceFile = nullptr;
                _resourcePos = 0;
        } else {
                int ret = ::close(_handle);
                err = (ret != 0) ? Error::syserr() : Error();
                _handle = FileHandleClosedValue;
        }
        _fileFlags = NoFlags;
        setOpenMode(NotOpen);
        resetReadBuffer();
        return err;
}

int64_t File::write(const void *data, int64_t maxSize) {
        if(_resourceFile != nullptr) {
                setError(Error(Error::ReadOnly));
                return -1;
        }
        if(!isOpen() || !isWritable()) return -1;
        ssize_t n = ::write(_handle, data, static_cast<size_t>(maxSize));
        if(n > 0) bytesWrittenSignal.emit(static_cast<int64_t>(n));
        if(n < 0) setError(Error::syserr());
        return static_cast<int64_t>(n);
}

int64_t File::readFromDevice(void *data, int64_t maxSize) {
        if(_resourceFile != nullptr) {
                int64_t remaining =
                        static_cast<int64_t>(_resourceFile->size) - _resourcePos;
                if(remaining <= 0) return 0;
                int64_t n = std::min<int64_t>(maxSize, remaining);
                ::memcpy(data, _resourceFile->data + _resourcePos,
                         static_cast<size_t>(n));
                _resourcePos += n;
                return n;
        }
        ssize_t n = ::read(_handle, data, static_cast<size_t>(maxSize));
        if(n < 0) setError(Error::syserr());
        return static_cast<int64_t>(n);
}

int64_t File::deviceBytesAvailable() const {
        if(_resourceFile != nullptr) {
                return static_cast<int64_t>(_resourceFile->size) - _resourcePos;
        }
        if(!isOpen()) return 0;
        int64_t cur = ::lseek64(_handle, 0, SEEK_CUR);
        int64_t end = ::lseek64(_handle, 0, SEEK_END);
        ::lseek64(_handle, cur, SEEK_SET);
        if(cur < 0 || end < 0) return 0;
        return end - cur;
}

bool File::isSequential() const {
        return false;
}

Error File::seek(int64_t offset) {
        if(!isOpen()) return Error(Error::NotOpen);
        resetReadBuffer();
        if(_resourceFile != nullptr) {
                if(offset < 0 ||
                   offset > static_cast<int64_t>(_resourceFile->size)) {
                        return Error(Error::IllegalSeek);
                }
                _resourcePos = offset;
                return Error();
        }
        off64_t result = ::lseek64(_handle, offset, SEEK_SET);
        if(result < 0) return Error::syserr();
        return Error();
}

int64_t File::pos() const {
        if(_resourceFile != nullptr) return _resourcePos;
        if(!isOpen()) return 0;
        int64_t rawPos = ::lseek64(_handle, 0, SEEK_CUR);
        // Subtract any bytes that the read buffer has consumed from
        // the device but not yet delivered to the caller, so that
        // pos() reflects the logical read position.
        return rawPos - static_cast<int64_t>(bufferedBytesUnconsumed());
}

Result<int64_t> File::size() const {
        if(_resourceFile != nullptr) {
                return makeResult<int64_t>(static_cast<int64_t>(_resourceFile->size));
        }
        if(!isOpen()) return makeResult<int64_t>(0);
        struct stat64 st;
        if(::fstat64(_handle, &st) != 0) return makeError<int64_t>(Error::syserr());
        return makeResult(st.st_size);
}

bool File::atEnd() const {
        auto [s, err] = size();
        if(err.isError()) return true;
        return pos() >= s;
}

Result<int64_t> File::seekFromCurrent(int64_t offset) const {
        if(_resourceFile != nullptr) {
                int64_t target = _resourcePos + offset;
                if(target < 0 ||
                   target > static_cast<int64_t>(_resourceFile->size)) {
                        return makeError<int64_t>(Error::IllegalSeek);
                }
                // const_cast because _resourcePos is logically mutable
                // for resource-mode lookups; the underlying class is
                // not actually const-correct around its read cursor
                // (matching the existing pattern with lseek above).
                const_cast<File *>(this)->_resourcePos = target;
                return makeResult<int64_t>(target);
        }
        if(!isOpen()) return makeError<int64_t>(Error::NotOpen);
        int64_t result = ::lseek64(_handle, offset, SEEK_CUR);
        if(result < 0) return makeError<int64_t>(Error::syserr());
        return makeResult(result);
}

Result<int64_t> File::seekFromEnd(int64_t offset) const {
        if(_resourceFile != nullptr) {
                int64_t target = static_cast<int64_t>(_resourceFile->size) + offset;
                if(target < 0 ||
                   target > static_cast<int64_t>(_resourceFile->size)) {
                        return makeError<int64_t>(Error::IllegalSeek);
                }
                const_cast<File *>(this)->_resourcePos = target;
                return makeResult<int64_t>(target);
        }
        if(!isOpen()) return makeError<int64_t>(Error::NotOpen);
        int64_t result = ::lseek64(_handle, offset, SEEK_END);
        if(result < 0) return makeError<int64_t>(Error::syserr());
        return makeResult(result);
}

Error File::truncate(int64_t offset) const {
        if(_resourceFile != nullptr) return Error(Error::ReadOnly);
        if(!isOpen()) return Error(Error::NotOpen);
        int val = ::ftruncate64(_handle, offset);
        return val == -1 ? Error::syserr() : Error();
}

int64_t File::writev(const IOVec *iov, int count) {
        if(_resourceFile != nullptr) {
                setError(Error(Error::ReadOnly));
                return -1;
        }
        if(!isOpen()) return -1;
        // Convert platform-neutral IOVec to POSIX iovec
        struct iovec posixIov[count];
        for(int i = 0; i < count; ++i) {
                posixIov[i].iov_base = const_cast<void *>(iov[i].data);
                posixIov[i].iov_len  = iov[i].size;
        }
        ssize_t n = ::writev(_handle, posixIov, count);
        if(n < 0) {
                setError(Error::syserr());
                return -1;
        }
        bytesWrittenSignal.emit(static_cast<int64_t>(n));
        return static_cast<int64_t>(n);
}

Error File::preallocate(int64_t offset, int64_t length) {
        if(_resourceFile != nullptr) return Error(Error::ReadOnly);
        if(!isOpen()) return Error(Error::NotOpen);
        int ret = ::posix_fallocate(_handle, offset, length);
        if(ret != 0) return Error::syserr(ret);
        return Error();
}

Result<size_t> File::directIOAlignment() const {
        if(_resourceFile != nullptr) return makeResult<size_t>(0);
        if(!isOpen()) return makeError<size_t>(Error::NotOpen);
        struct stat64 st;
        if(::fstat64(_handle, &st) != 0) return makeError<size_t>(Error::syserr());
        return makeResult(static_cast<size_t>(st.st_blksize));
}

/**
 * Helper: read exactly `size` bytes from the fd at the current position
 * into `dest`, looping on partial reads. Returns bytes actually read
 * (may be less than size at EOF), or -1 on error.
 */
static int64_t readFull(int fd, void *dest, int64_t size) {
        uint8_t *p = static_cast<uint8_t *>(dest);
        int64_t total = 0;
        while(total < size) {
                ssize_t n = ::read(fd, p + total, static_cast<size_t>(size - total));
                if(n < 0) return -1;
                if(n == 0) break;
                total += n;
        }
        return total;
}

Error File::readBulk(Buffer &buf, int64_t size) {
        if(!isOpen() || !isReadable()) return Error(Error::NotOpen);
        if(size <= 0) return Error(Error::InvalidArgument);
        if(!buf.isHostAccessible()) return Error(Error::NotHostAccessible);

        // Resource-mode reads have no fd, no alignment requirement,
        // and the bytes are already in RAM. Drop straight into a
        // simple memcpy via readFromDevice() — no DIO trickery, no
        // shiftData().
        if(_resourceFile != nullptr) {
                if(buf.allocSize() < static_cast<size_t>(size)) {
                        return Error(Error::BufferTooSmall);
                }
                buf.shiftData(0);
                int64_t n = readFromDevice(buf.data(), size);
                if(n < 0) return Error::syserr();
                buf.setSize(static_cast<size_t>(n));
                return Error();
        }

        int64_t fileOffset = pos();
        auto [align, alignErr] = directIOAlignment();

        // Compute the shift needed so the DIO portion is aligned.
        // shift = fileOffset % align (0 when already aligned or no alignment).
        size_t shift = (align > 0) ? static_cast<size_t>(fileOffset % static_cast<int64_t>(align)) : 0;

        if(buf.allocSize() < shift + static_cast<size_t>(size)) return Error(Error::BufferTooSmall);

        buf.shiftData(shift);
        uint8_t *dest = static_cast<uint8_t *>(buf.data());

        // If we can't determine alignment, fall back to a normal read.
        if(alignErr.isError() || align == 0) {
                int64_t n = readFull(_handle, dest, size);
                if(n < 0) return Error::syserr();
                buf.setSize(static_cast<size_t>(n));
                return Error();
        }

        int64_t A = static_cast<int64_t>(align);
        int64_t alignedStart = (fileOffset + A - 1) & ~(A - 1);
        int64_t alignedEnd   = (fileOffset + size) & ~(A - 1);
        int64_t dioBytes     = alignedEnd - alignedStart;

        size_t headBytes = static_cast<size_t>(alignedStart - fileOffset);
        size_t tailBytes = static_cast<size_t>((fileOffset + size) - alignedEnd);

        // If the region is too small for any aligned portion, just read normally.
        if(dioBytes <= 0) {
                int64_t n = readFull(_handle, dest, size);
                if(n < 0) return Error::syserr();
                buf.setSize(static_cast<size_t>(n));
                return Error();
        }

        int64_t totalRead = 0;

        // Read unaligned head with normal I/O
        if(headBytes > 0) {
                int64_t n = readFull(_handle, dest, static_cast<int64_t>(headBytes));
                if(n < 0) return Error::syserr();
                totalRead += n;
                if(n < static_cast<int64_t>(headBytes)) {
                        // Short read (hit EOF in head portion)
                        buf.setSize(static_cast<size_t>(totalRead));
                        return Error();
                }
        }

        // Read aligned middle with direct I/O.
        // If the DIO read fails (e.g. filesystem does not support O_DIRECT),
        // fall back to a normal read for this portion.
        setDirectIO(true);
        int64_t n = readFull(_handle, dest + headBytes, dioBytes);
        setDirectIO(false);
        if(n < 0) {
                // DIO failed — seek back to the aligned start and retry normally
                off64_t seekResult = ::lseek64(_handle, alignedStart, SEEK_SET);
                if(seekResult < 0) return Error::syserr();
                n = readFull(_handle, dest + headBytes, dioBytes);
                if(n < 0) return Error::syserr();
        }
        totalRead += n;
        if(n < dioBytes) {
                // Short read (hit EOF in DIO portion)
                buf.setSize(static_cast<size_t>(totalRead));
                return Error();
        }

        // Read unaligned tail with normal I/O
        if(tailBytes > 0) {
                n = readFull(_handle, dest + headBytes + static_cast<size_t>(dioBytes),
                             static_cast<int64_t>(tailBytes));
                if(n < 0) return Error::syserr();
                totalRead += n;
        }

        buf.setSize(static_cast<size_t>(totalRead));
        return Error();
}

/**
 * @brief Writes @p size bytes from @p p to @p fd, looping on partial writes.
 * Returns bytes actually written or -1 on error.
 */
static int64_t writeFull(int fd, const void *p, int64_t size) {
        const uint8_t *src = static_cast<const uint8_t *>(p);
        int64_t total = 0;
        while(total < size) {
                ssize_t n = ::write(fd, src + total, static_cast<size_t>(size - total));
                if(n < 0) return -1;
                if(n == 0) break;
                total += n;
        }
        return total;
}

int64_t File::writeBulk(const void *data, int64_t size) {
        if(!isOpen() || !isWritable()) { setError(Error(Error::NotOpen)); return -1; }
        if(size <= 0) { setError(Error(Error::InvalidArgument)); return -1; }
        if(data == nullptr) { setError(Error(Error::InvalidArgument)); return -1; }

        // Resource-mode writers don't exist (resources are read-only),
        // but guard anyway.
        if(_resourceFile != nullptr) {
                setError(Error(Error::ReadOnly));
                return -1;
        }

        int64_t fileOffset = ::lseek64(_handle, 0, SEEK_CUR);
        if(fileOffset < 0) { setError(Error::syserr()); return -1; }

        auto [align, alignErr] = directIOAlignment();

        // If we don't know the alignment, or the source pointer isn't
        // aligned to it, we can't do DIO. Fall back to a normal write.
        uintptr_t srcAddr = reinterpret_cast<uintptr_t>(data);
        bool srcAligned = (align > 0) && (srcAddr % align == 0);
        if(alignErr.isError() || align == 0 || !srcAligned) {
                int64_t n = writeFull(_handle, data, size);
                if(n < 0) { setError(Error::syserr()); return -1; }
                if(n > 0) bytesWrittenSignal.emit(n);
                return n;
        }

        int64_t A = static_cast<int64_t>(align);
        int64_t alignedStart = (fileOffset + A - 1) & ~(A - 1);
        int64_t alignedEnd   = (fileOffset + size) & ~(A - 1);
        int64_t dioBytes     = alignedEnd - alignedStart;

        size_t headBytes = static_cast<size_t>(alignedStart - fileOffset);
        size_t tailBytes = static_cast<size_t>((fileOffset + size) - alignedEnd);

        // Not enough aligned interior to be worth DIO — just write normally.
        if(dioBytes <= 0) {
                int64_t n = writeFull(_handle, data, size);
                if(n < 0) { setError(Error::syserr()); return -1; }
                if(n > 0) bytesWrittenSignal.emit(n);
                return n;
        }

        const uint8_t *src = static_cast<const uint8_t *>(data);
        int64_t totalWritten = 0;

        // Head: unaligned, normal write.
        if(headBytes > 0) {
                int64_t n = writeFull(_handle, src, static_cast<int64_t>(headBytes));
                if(n < 0) { setError(Error::syserr()); return -1; }
                totalWritten += n;
                if(n < static_cast<int64_t>(headBytes)) {
                        // Short write — return what we managed.
                        if(totalWritten > 0) bytesWrittenSignal.emit(totalWritten);
                        return totalWritten;
                }
        }

        // Middle: aligned DIO write. Toggle O_DIRECT on the fd for this
        // portion only, so head/tail fall-through writes continue to work.
        setDirectIO(true);
        int64_t n = writeFull(_handle, src + headBytes, dioBytes);
        setDirectIO(false);
        if(n < 0) {
                // DIO failed at runtime (e.g. filesystem or page cache
                // weirdness). Seek back and retry with normal I/O.
                off64_t seekResult = ::lseek64(_handle, alignedStart, SEEK_SET);
                if(seekResult < 0) { setError(Error::syserr()); return -1; }
                n = writeFull(_handle, src + headBytes, dioBytes);
                if(n < 0) { setError(Error::syserr()); return -1; }
        }
        totalWritten += n;
        if(n < dioBytes) {
                if(totalWritten > 0) bytesWrittenSignal.emit(totalWritten);
                return totalWritten;
        }

        // Tail: unaligned, normal write.
        if(tailBytes > 0) {
                n = writeFull(_handle, src + headBytes + static_cast<size_t>(dioBytes),
                              static_cast<int64_t>(tailBytes));
                if(n < 0) { setError(Error::syserr()); return -1; }
                totalWritten += n;
        }

        if(totalWritten > 0) bytesWrittenSignal.emit(totalWritten);
        return totalWritten;
}

Error File::sync(bool dataOnly) {
        if(!isOpen()) return Error(Error::NotOpen);
        if(_resourceFile != nullptr) return Error();  // read-only, nothing to sync
        int rc;
        if(dataOnly) {
#if defined(__linux__) || defined(_POSIX_SYNCHRONIZED_IO)
                rc = ::fdatasync(_handle);
#else
                rc = ::fsync(_handle);
#endif
        } else {
                rc = ::fsync(_handle);
        }
        if(rc == -1) return Error::syserr();
        return Error();
}

Error File::setDirectIO(bool enable) {
        // Snapshot the previous unbuffered state so we can restore it if
        // the fcntl below fails — the old implementation left _directIO
        // out of sync with the fd state on failure.
        bool prevUnbuffered = isUnbuffered();
        bool prevSavedUnbuffered = _savedUnbuffered;
        if(enable) {
                _savedUnbuffered = prevUnbuffered;
                setUnbuffered(true);
        } else {
                setUnbuffered(_savedUnbuffered);
        }

        // Resource-mode files have no kernel fd to fcntl. Track the
        // option flag (so isDirectIO() reports the user's setting)
        // but skip the fcntl entirely.
        if(_resourceFile != nullptr) {
                _directIO = enable;
                return Error();
        }
        if(isOpen()) {
                int flags = ::fcntl(_handle, F_GETFL);
                if(flags == -1) {
                        // Restore buffered state on failure.
                        setUnbuffered(prevUnbuffered);
                        _savedUnbuffered = prevSavedUnbuffered;
                        return Error::syserr();
                }
                int newFlags = enable ? flags | O_DIRECT : flags & ~O_DIRECT;
                if(::fcntl(_handle, F_SETFL, newFlags) == -1) {
                        setUnbuffered(prevUnbuffered);
                        _savedUnbuffered = prevSavedUnbuffered;
                        return Error::syserr();
                }
        }
        _directIO = enable;
        return Error();
}

Error File::setSynchronous(bool enable) {
        _synchronous = enable;
        if(_resourceFile != nullptr) return Error();
        if(isOpen()) {
                int flags = ::fcntl(_handle, F_GETFL);
                if(flags == -1) return Error::syserr();
                flags = enable ? flags | O_SYNC : flags & ~O_SYNC;
                if(::fcntl(_handle, F_SETFL, flags) == -1) return Error::syserr();
        }
        return Error();
}

Error File::setNonBlocking(bool enable) {
        _nonBlocking = enable;
        if(_resourceFile != nullptr) return Error();
        if(isOpen()) {
                int flags = ::fcntl(_handle, F_GETFL);
                if(flags == -1) return Error::syserr();
                flags = enable ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
                if(::fcntl(_handle, F_SETFL, flags) == -1) return Error::syserr();
        }
        return Error();
}

#endif

PROMEKI_NAMESPACE_END
