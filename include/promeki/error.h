/**
 * @file      error.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

class String;

class Error {
        public:
                enum Code {
                        Ok = 0,
                        UnsupportedSystemError,
                        LibraryFailure,
                        SingularMatrix,
                        NotImplemented,
                        PixelFormatNotSupported,
                        OutOfRange,
                        PermissionDenied,
                        TryAgain,
                        BadFileDesc,
                        Busy,
                        Exists,
                        BadAddress,
                        TooLarge,
                        Interrupt,
                        Invalid,
                        IOError,
                        IsDir,
                        TooManyOpenFiles,
                        TooManyOpenSysFiles,
                        NotExist,
                        NoMem,
                        NoSpace,
                        NotDir,
                        NoPermission,
                        ReadOnly,
                        IllegalSeek,
                        Timeout,
                        CrossDeviceLink,
                        NoFrameRate,
                        AlreadyOpen,
                        NotSupported,
                        OpenFailed,
                        NotOpen,
                        EndOfFile
                };

                // Returns an error code based on the current errno
                static Error syserr();
                Error(Code code = Ok) : _code(code) { }
                ~Error() { }
                bool operator==(const Error &other) const { return _code == other._code; }
                bool operator!=(const Error &other) const { return _code != other._code; }
                bool operator<(const Error &other) const { return _code < other._code; }
                bool operator<=(const Error &other) const { return _code <= other._code; }
                bool operator>(const Error &other) const { return _code > other._code; }
                bool operator>=(const Error &other) const { return _code >= other._code; }

                Code code() const { return _code; }
                bool isOk() const { return _code == 0; }
                bool isError() const { return _code != 0; }
                const String &name() const;
                const String &desc() const;
                const String &systemErrorName() const;
                int systemError() const;

        private:
                Code    _code;
};

PROMEKI_NAMESPACE_END

