/**
 * @file      file.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/file.h>
#include <promeki/util.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <Windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

static int fileFlagsToPOSIX(int flags) {
        int ret = O_LARGEFILE;
        if((flags & File::ReadWrite) == File::ReadWrite) {
                ret |= O_RDWR;
        } else if(flags & File::ReadOnly) {
                ret |= O_RDONLY;
        } else if(flags & File::WriteOnly) {
                ret |= O_WRONLY;
        }
        if(flags & File::Create)        ret |= O_CREAT;
        if(flags & File::Append)        ret |= O_APPEND;
        if(flags & File::NonBlocking)   ret |= O_NONBLOCK;
        if(flags & File::DirectIO)      ret |= O_DIRECT;
        if(flags & File::Sync)          ret |= O_SYNC;
        if(flags & File::Truncate)      ret |= O_TRUNC;
        if(flags & File::Exclusive)     ret |= O_EXCL;
        return ret;
}

File::~File() {
        close();
}

#if defined(PROMEKI_PLATFORM_WINDOWS)
// FIXME: The windows code here needs love.
bool File::isOpen() const {
        return false;
}

Error File::open(int flags) {
        return Error::NotImplemented;
}

void close() {
        return;
}

#else

Error File::setDirectIOEnabled(bool val) {
        if(val == isDirectIO()) return Error(); // Already in the requested state.
        int flags = ::fcntl(_handle, F_GETFL);
        if(flags == -1) return Error::syserr();
        flags = val ? flags | O_DIRECT : flags & ~O_DIRECT;
        int ret = fcntl(_handle, F_SETFL, flags);
        if(ret == -1) return Error::syserr();
        _flags = val ? _flags | DirectIO : _flags & ~DirectIO;
        return Error();
}

Error File::open(int flags) {
        int openFlags = fileFlagsToPOSIX(flags);
        if(openFlags & O_CREAT) {
                _handle = ::open(_filename.cstr(), openFlags, 0666);
        } else {
                _handle = ::open(_filename.cstr(), openFlags);
        }
        if(_handle == FileHandleClosedValue) return Error::syserr();
        _flags = flags;
        return Error();
}

void File::close() {
        if(_handle == FileHandleClosedValue) return;
        ::close(_handle);
        _handle = FileHandleClosedValue;
        return;
}

File::FileBytes File::write(const void *buf, size_t bytes) const {
        return ::write(_handle, buf, bytes);
}

File::FileBytes File::read(void *buf, size_t bytes) const {
        return ::read(_handle, buf, bytes);
}

File::FileBytes File::position() const {
        return ::lseek64(_handle, 0, SEEK_CUR);
}

File::FileBytes File::seek(FileBytes offset) const {
        return ::lseek64(_handle, offset, SEEK_SET);
}

File::FileBytes File::seekFromCurrent(FileBytes offset) const {
        return ::lseek64(_handle, offset, SEEK_CUR);
}

File::FileBytes File::seekFromEnd(FileBytes offset) const {
        return ::lseek64(_handle, offset, SEEK_END);
}

Error File::truncate(FileBytes offset) const {
        int val = ::ftruncate64(_handle, offset);
        return val == -1 ? Error::syserr() : Error();
}

#endif

PROMEKI_NAMESPACE_END

