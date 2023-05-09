/*****************************************************************************
 * error.cpp
 * April 26, 2023
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

#include <cerrno>
#include <cstring>
#include <promeki/error.h>
#include <promeki/structdatabase.h>
#include <promeki/logger.h>
#include <promeki/util.h>

#define DEFINE_ERROR(error, errno, description) \
{ \
        .id = Error::error, \
        .name = PROMEKI_STRINGIFY(error), \
        .desc = description, \
        .systemErrorName = PROMEKI_STRINGIFY(errno), \
        .systemError = errno \
}

#define NONE -1
#define OK 0

PROMEKI_NAMESPACE_BEGIN

struct ErrorData {
        Error::Code             id;
        String                  name;
        String                  desc;
        String                  systemErrorName;
        int                     systemError;            // Standard errno that provides the same function (or -1 if none)
};

static StructDatabase<Error::Code, ErrorData> db = {
        DEFINE_ERROR(Ok, OK, "Ok"),
        DEFINE_ERROR(UnsupportedSystemError, NONE, "Unsupported system error"),
        DEFINE_ERROR(LibraryFailure, NONE, "Library failure"),
        DEFINE_ERROR(PixelFormatNotSupported, NONE, "Pixel format not supported"),
        DEFINE_ERROR(SingularMatrix, NONE, "Matrix is singular"),
        DEFINE_ERROR(NoFrameRate, NONE, "No frame rate"),
        DEFINE_ERROR(NotImplemented, ENOSYS, "Not implemented"),
        DEFINE_ERROR(OutOfRange, ERANGE, "Out of range"),
        DEFINE_ERROR(PermissionDenied, EACCES, "Permission denied"),
        DEFINE_ERROR(TryAgain, EAGAIN, "Resource temporarily unavailable"),
        DEFINE_ERROR(BadFileDesc, EBADF, "Bad file descriptor"),
        DEFINE_ERROR(Busy, EBUSY, "Device or resource busy"),
        DEFINE_ERROR(Exists, EEXIST, "File or item exists already"),
        DEFINE_ERROR(BadAddress, EFAULT, "Bad address"),
        DEFINE_ERROR(TooLarge, EFBIG, "File or item too large"),
        DEFINE_ERROR(Interrupt, EINTR, "Interrupted system call or function"),
        DEFINE_ERROR(Invalid, EINVAL, "Invalid argument"),
        DEFINE_ERROR(IOError, EIO, "Input/output error"),
        DEFINE_ERROR(IsDir, EISDIR, "Is a directory"),
        DEFINE_ERROR(TooManyOpenFiles, EMFILE, "Too many process open files"),
        DEFINE_ERROR(TooManyOpenSysFiles, ENFILE, "Too many open system files"),
        DEFINE_ERROR(NotExist, ENOENT, "No such file or item"),
        DEFINE_ERROR(NoMem, ENOMEM, "Not enough memory"),
        DEFINE_ERROR(NoSpace, ENOSPC, "No space left on device"),
        DEFINE_ERROR(NotDir, ENOTDIR, "Not a directory"),
        DEFINE_ERROR(NoPermission, EPERM, "Operation not permitted"),
        DEFINE_ERROR(ReadOnly, EROFS, "Read-only file system"),
        DEFINE_ERROR(IllegalSeek, ESPIPE, "Illegal seek"),
        DEFINE_ERROR(Timeout, ETIMEDOUT, "Timed out"),
        DEFINE_ERROR(CrossDeviceLink, EXDEV, "Cross-device link")
};

Error Error::syserr() {
        static bool init = false;
        static StructDatabase<int, Code> sysdb;
        if(!init) {
                init = true;
                for(const auto &item : db.database()) {
                        int systemError = item.second.systemError;
                        if(systemError == NONE) continue;
                        sysdb.database()[systemError] = item.second.id;
                }
        }
        int e = errno;
        const auto &val = sysdb.database().find(e);
        if(val == sysdb.database().end()) {
                promekiWarn("Error::syserr() can't translate error %d:%s", e, std::strerror(e));
                return UnsupportedSystemError;
        }
        return val->second;
}

const String &Error::name() const {
        return db.get(_code).name;
}

const String &Error::desc() const {
        return db.get(_code).desc;
}

const String &Error::systemErrorName() const {
        return db.get(_code).systemErrorName;
}

int Error::systemError() const {
        return db.get(_code).systemError;
}

PROMEKI_NAMESPACE_END

