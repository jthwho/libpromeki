/**
 * @file      file.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/file.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
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

Result<size_t> File::directIOAlignment() const {
        return makeError<size_t>(Error::NotImplemented);
}

Error File::readBulk(Buffer &buf, int64_t size) {
        (void)buf;
        (void)size;
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
        return _handle != FileHandleClosedValue;
}

Error File::open(OpenMode mode) {
        return open(mode, NoFlags);
}

Error File::open(OpenMode mode, int fileFlags) {
        if(isOpen()) return Error(Error::AlreadyOpen);
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
        int ret = ::close(_handle);
        Error err = (ret != 0) ? Error::syserr() : Error();
        _handle = FileHandleClosedValue;
        _fileFlags = NoFlags;
        setOpenMode(NotOpen);
        resetReadBuffer();
        return err;
}

int64_t File::write(const void *data, int64_t maxSize) {
        if(!isOpen() || !isWritable()) return -1;
        ssize_t n = ::write(_handle, data, static_cast<size_t>(maxSize));
        if(n > 0) bytesWrittenSignal.emit(static_cast<int64_t>(n));
        if(n < 0) setError(Error::syserr());
        return static_cast<int64_t>(n);
}

int64_t File::readFromDevice(void *data, int64_t maxSize) {
        ssize_t n = ::read(_handle, data, static_cast<size_t>(maxSize));
        if(n < 0) setError(Error::syserr());
        return static_cast<int64_t>(n);
}

int64_t File::deviceBytesAvailable() const {
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
        off64_t result = ::lseek64(_handle, offset, SEEK_SET);
        if(result < 0) return Error::syserr();
        return Error();
}

int64_t File::pos() const {
        if(!isOpen()) return 0;
        int64_t rawPos = ::lseek64(_handle, 0, SEEK_CUR);
        // Subtract any bytes that the read buffer has consumed from
        // the device but not yet delivered to the caller, so that
        // pos() reflects the logical read position.
        return rawPos - static_cast<int64_t>(bufferedBytesUnconsumed());
}

Result<int64_t> File::size() const {
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
        if(!isOpen()) return makeError<int64_t>(Error::NotOpen);
        int64_t result = ::lseek64(_handle, offset, SEEK_CUR);
        if(result < 0) return makeError<int64_t>(Error::syserr());
        return makeResult(result);
}

Result<int64_t> File::seekFromEnd(int64_t offset) const {
        if(!isOpen()) return makeError<int64_t>(Error::NotOpen);
        int64_t result = ::lseek64(_handle, offset, SEEK_END);
        if(result < 0) return makeError<int64_t>(Error::syserr());
        return makeResult(result);
}

Error File::truncate(int64_t offset) const {
        if(!isOpen()) return Error(Error::NotOpen);
        int val = ::ftruncate64(_handle, offset);
        return val == -1 ? Error::syserr() : Error();
}

Result<size_t> File::directIOAlignment() const {
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

Error File::setDirectIO(bool enable) {
        if(enable) {
                _savedUnbuffered = isUnbuffered();
                setUnbuffered(true);
        } else {
                setUnbuffered(_savedUnbuffered);
        }
        _directIO = enable;
        if(isOpen()) {
                int flags = ::fcntl(_handle, F_GETFL);
                if(flags == -1) return Error::syserr();
                flags = enable ? flags | O_DIRECT : flags & ~O_DIRECT;
                if(::fcntl(_handle, F_SETFL, flags) == -1) return Error::syserr();
        }
        return Error();
}

Error File::setSynchronous(bool enable) {
        _synchronous = enable;
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
