/*****************************************************************************
 * error.h
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

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

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
                        NoFrameRate
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

