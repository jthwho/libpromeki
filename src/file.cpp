/*****************************************************************************
 * file.cpp
 * May 03, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <promeki/file.h>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#elif __APPLE__
#include <fcntl.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace promeki {

static int fileFlagsToPOSIX(int flags) {
        int ret = O_LARGEFILE;
        if(flags & File::ReadOnly)      ret |= O_RDONLY;
        if(flags & File::WriteOnly)     ret |= O_WRONLY;
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

#ifdef _WIN32
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
        _handle = ::open(_filename.cstr(), flags);
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

} // namespace promeki

