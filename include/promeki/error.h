/**
 * @file      error.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <system_error>
#include <promeki/namespace.h>
#include <promeki/platform.h>

PROMEKI_NAMESPACE_BEGIN

class String;

/**
 * @brief Lightweight error code wrapper for the promeki library.
 * @ingroup util
 *
 * Encapsulates an error code from the Code enumeration and provides
 * comparison operators, human-readable names, descriptions, and
 *
 * @par Example
 * @code
 * Error err;
 * // Functions use Error* for fallible results:
 * auto val = someFunction(&err);
 * if(err.isError()) {
 *     String msg = err.description();
 * }
 *
 * // Direct comparison
 * if(err == Error::NotExist) { ... }
 * @endcode
 * a mapping to the corresponding POSIX errno value when applicable.
 */
class Error {
        public:
                /**
                 * @brief Error codes for the promeki library.
                 *
                 * New codes should be added here whenever an existing code
                 * would be an imprecise fit. Prefer specific, descriptive
                 * codes over reusing generic ones. Codes that map to POSIX
                 * errno values are noted in error.cpp; domain-specific codes
                 * have no errno mapping.
                 */
                enum Code {
                        Ok = 0,                 ///< No error.
                        UnsupportedSystemError,  ///< Unmapped system error.
                        LibraryFailure,          ///< An external library call failed.
                        SingularMatrix,          ///< Matrix is singular and cannot be inverted.
                        NotImplemented,          ///< Feature is not implemented.
                        PixelFormatNotSupported,  ///< Pixel format is not supported.
                        OutOfRange,              ///< Value is out of range.
                        PermissionDenied,        ///< Permission denied (EACCES).
                        TryAgain,                ///< Resource temporarily unavailable (EAGAIN).
                        BadFileDesc,             ///< Bad file descriptor (EBADF).
                        Busy,                    ///< Resource is busy (EBUSY).
                        Exists,                  ///< File or resource already exists (EEXIST).
                        BadAddress,              ///< Bad memory address (EFAULT).
                        TooLarge,                ///< File too large (EFBIG).
                        Interrupt,               ///< Interrupted system call (EINTR).
                        Invalid,                 ///< Invalid value or argument (EINVAL).
                        IOError,                 ///< Input/output error (EIO).
                        IsDir,                   ///< Is a directory (EISDIR).
                        TooManyOpenFiles,         ///< Too many open files in process (EMFILE).
                        TooManyOpenSysFiles,      ///< Too many open files system-wide (ENFILE).
                        NotExist,                ///< File or resource does not exist (ENOENT).
                        NoMem,                   ///< Out of memory (ENOMEM).
                        NoSpace,                 ///< No space left on device (ENOSPC).
                        NotDir,                  ///< Not a directory (ENOTDIR).
                        NoPermission,            ///< Operation not permitted (EPERM).
                        ReadOnly,                ///< Read-only file system (EROFS).
                        IllegalSeek,             ///< Illegal seek (ESPIPE).
                        Timeout,                 ///< Operation timed out.
                        CrossDeviceLink,         ///< Cross-device link (EXDEV).
                        NoFrameRate,             ///< No frame rate has been set.
                        AlreadyOpen,             ///< Resource is already open.
                        NotSupported,            ///< Operation is not supported.
                        OpenFailed,              ///< Failed to open a resource.
                        NotOpen,                 ///< Resource is not open.
                        EndOfFile,               ///< End of file reached.
                        InvalidArgument,         ///< Invalid argument supplied.
                        InvalidDimension,        ///< Invalid dimension value.
                        NotHostAccessible,       ///< Memory is not host-accessible.
                        BufferTooSmall,          ///< Buffer is too small for the operation.
                        IdNotFound,              ///< Requested ID was not found in the database.
                        ConversionFailed,        ///< Type conversion could not be performed.
                        Stopped,                 ///< Operation interrupted because the object is stopping.
                        Cancelled,               ///< Operation was cancelled before completion.
                        DecodeFailed,            ///< Codec decoder reported a failure.
                        EncodeFailed,            ///< Codec encoder reported a failure.
                        CorruptData,             ///< Stored data is structurally corrupt.
                        DeviceError,             ///< Hardware device reported an error.
                        DeviceNotFound,          ///< Hardware device was not found or cannot be opened.
                        FormatMismatch           ///< Audio/video format does not match the expected format.
                };

                /**
                 * @brief Creates an Error from the last system error.
                 *
                 * On POSIX, reads errno. On Windows, reads GetLastError().
                 * @return An Error whose code maps to the system error.
                 */
                static Error syserr();

                /**
                 * @brief Creates an Error from an explicit POSIX errno value.
                 * @param errnum The errno value to translate.
                 * @return An Error whose code maps to the given errno.
                 */
                static Error syserr(int errnum);

#if defined(PROMEKI_PLATFORM_WINDOWS)
                /**
                 * @brief Creates an Error from a Windows error code.
                 * @param winErr The GetLastError() value to translate.
                 * @return An Error whose code maps to the given Windows error.
                 */
                static Error syserr(DWORD winErr);
#endif

                /**
                 * @brief Creates an Error from a std::error_code.
                 *
                 * Handles both POSIX (generic_category / system_category on POSIX)
                 * and Windows (system_category on Windows) error codes correctly.
                 * Returns Ok when the error_code has no error.
                 *
                 * @param ec The std::error_code to translate.
                 * @return An Error whose code maps to the given error_code.
                 */
                static Error syserr(const std::error_code &ec);

                /**
                 * @brief Constructs an Error with the given code.
                 * @param code The error code (default: Ok).
                 */
                Error(Code code = Ok) : _code(code) { }

                /** @brief Destructor. */
                ~Error() { }

                /** @brief Returns true if both errors have the same code. */
                bool operator==(const Error &other) const { return _code == other._code; }
                /** @brief Returns true if the errors have different codes. */
                bool operator!=(const Error &other) const { return _code != other._code; }
                /** @brief Less-than comparison by error code. */
                bool operator<(const Error &other) const { return _code < other._code; }
                /** @brief Less-than-or-equal comparison by error code. */
                bool operator<=(const Error &other) const { return _code <= other._code; }
                /** @brief Greater-than comparison by error code. */
                bool operator>(const Error &other) const { return _code > other._code; }
                /** @brief Greater-than-or-equal comparison by error code. */
                bool operator>=(const Error &other) const { return _code >= other._code; }

                /**
                 * @brief Returns the error code.
                 * @return The Code enumeration value.
                 */
                Code code() const { return _code; }

                /**
                 * @brief Returns true if the error code is Ok (no error).
                 * @return True when the code is zero.
                 */
                bool isOk() const { return _code == 0; }

                /**
                 * @brief Returns true if the error code indicates an error.
                 * @return True when the code is non-zero.
                 */
                bool isError() const { return _code != 0; }

                /**
                 * @brief Returns the human-readable name of the error code.
                 * @return A String such as "Ok", "Invalid", "IOError", etc.
                 */
                const String &name() const;

                /**
                 * @brief Returns a human-readable description of the error.
                 * @return A descriptive String for the error code.
                 */
                const String &desc() const;

                /**
                 * @brief Returns the name of the corresponding POSIX errno symbol.
                 * @return A String such as "EINVAL", or an empty string if unmapped.
                 */
                const String &systemErrorName() const;

                /**
                 * @brief Returns the POSIX errno value that maps to this error code.
                 * @return The errno value, or 0 if there is no mapping.
                 */
                int systemError() const;

        private:
                Code    _code;
};

PROMEKI_NAMESPACE_END

