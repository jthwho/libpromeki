/**
 * @file      error.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cerrno>
#include <cstring>
#include <system_error>
#include <promeki/error.h>
#include <promeki/platform.h>
#include <promeki/structdatabase.h>
#include <promeki/logger.h>
#include <promeki/util.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#       include <Windows.h>
#endif

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
        DEFINE_ERROR(AlreadyOpen, NONE, "Already Open"),
        DEFINE_ERROR(NotSupported, NONE, "Not Supported"),
        DEFINE_ERROR(OpenFailed, NONE, "Open Failed"),
        DEFINE_ERROR(NotOpen, NONE, "Not Open"),
        DEFINE_ERROR(EndOfFile, NONE, "End Of File"),

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
        DEFINE_ERROR(CrossDeviceLink, EXDEV, "Cross-device link"),

        DEFINE_ERROR(InvalidArgument, NONE, "Invalid argument passed to function"),
        DEFINE_ERROR(InvalidDimension, NONE, "Invalid dimension for operation"),
        DEFINE_ERROR(NotHostAccessible, NONE, "Memory is not host-accessible"),
        DEFINE_ERROR(BufferTooSmall, NONE, "Buffer is too small for the operation"),
        DEFINE_ERROR(IdNotFound, NONE, "Requested ID was not found in the database"),
        DEFINE_ERROR(ConversionFailed, NONE, "Type conversion could not be performed"),
        DEFINE_ERROR(Stopped, NONE, "Operation interrupted because the object is stopping"),
        DEFINE_ERROR(Cancelled, NONE, "Operation was cancelled before completion"),
        DEFINE_ERROR(DecodeFailed, NONE, "Codec decoder reported a failure"),
        DEFINE_ERROR(EncodeFailed, NONE, "Codec encoder reported a failure"),
        DEFINE_ERROR(CorruptData, NONE, "Stored data is structurally corrupt"),
        DEFINE_ERROR(DeviceError, NONE, "Hardware device reported an error"),
        DEFINE_ERROR(DeviceNotFound, NONE, "Hardware device was not found or cannot be opened"),
        DEFINE_ERROR(FormatMismatch, NONE, "Audio/video format does not match the expected format")
};

static StructDatabase<int, Error::Code> &posixErrorDb() {
        static StructDatabase<int, Error::Code> sysdb = []() {
                StructDatabase<int, Error::Code> tmp;
                for(const auto &item : db.database()) {
                        int systemError = item.second.systemError;
                        if(systemError == NONE) continue;
                        tmp.database()[systemError] = item.second.id;
                }
                return tmp;
        }();
        return sysdb;
}

Error Error::syserr(int errnum) {
        auto &sysdb = posixErrorDb();
        const auto &val = sysdb.database().find(errnum);
        if(val == sysdb.database().end()) {
                promekiWarn("Error::syserr() can't translate error %d:%s", errnum, std::strerror(errnum));
                return UnsupportedSystemError;
        }
        return val->second;
}

Error Error::syserr() {
#if defined(PROMEKI_PLATFORM_WINDOWS)
        return syserr(GetLastError());
#else
        return syserr(errno);
#endif
}

#if defined(PROMEKI_PLATFORM_WINDOWS)
Error Error::syserr(DWORD e) {
        switch(e) {
                case ERROR_SUCCESS:             return Ok;
                case ERROR_ACCESS_DENIED:       return PermissionDenied;
                case ERROR_NOT_ENOUGH_MEMORY:   return NoMem;
                case ERROR_OUTOFMEMORY:         return NoMem;
                case ERROR_INVALID_PARAMETER:   return Invalid;
                case ERROR_INVALID_ADDRESS:     return BadAddress;
                case ERROR_BUSY:                return Busy;
                case ERROR_ALREADY_EXISTS:      return Exists;
                case ERROR_FILE_NOT_FOUND:      return NotExist;
                case ERROR_PATH_NOT_FOUND:      return NotExist;
                case ERROR_DISK_FULL:           return NoSpace;
                case ERROR_HANDLE_DISK_FULL:    return NoSpace;
                case ERROR_WRITE_PROTECT:       return ReadOnly;
                case ERROR_TIMEOUT:             return Timeout;
                case ERROR_SEM_TIMEOUT:         return Timeout;
                case ERROR_WORKING_SET_QUOTA:   return NoMem;
                case ERROR_INVALID_HANDLE:      return BadFileDesc;
                case ERROR_TOO_MANY_OPEN_FILES: return TooManyOpenFiles;
                case ERROR_NOT_SUPPORTED:       return NotSupported;
                case ERROR_SHARING_VIOLATION:   return Busy;
                case ERROR_LOCK_VIOLATION:      return Busy;
                case ERROR_DIR_NOT_EMPTY:       return Exists;
                case ERROR_DIRECTORY:           return NotDir;
                case ERROR_FILE_TOO_LARGE:      return TooLarge;
                case ERROR_BROKEN_PIPE:         return IOError;
                case ERROR_NO_DATA:             return IOError;
                case ERROR_OPERATION_ABORTED:   return Interrupt;
                default:
                        promekiWarn("Error::syserr() can't translate Windows error %lu", (unsigned long)e);
                        return UnsupportedSystemError;
        }
}
#endif

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

Error Error::syserr(const std::error_code &ec) {
        if(!ec) return Ok;
#if defined(PROMEKI_PLATFORM_WINDOWS)
        // On Windows, system_category uses Windows error codes.
        // generic_category uses POSIX errno values.
        if(ec.category() == std::system_category()) {
                return syserr(static_cast<DWORD>(ec.value()));
        }
#endif
        // On POSIX, both system_category and generic_category use errno values.
        // On Windows with generic_category, values are also POSIX errno.
        return syserr(ec.value());
}

PROMEKI_NAMESPACE_END

