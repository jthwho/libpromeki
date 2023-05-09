/*****************************************************************************
 * file.h
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

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class File {
	public:
                enum Flags {
                        NoFlags         = 0x00000000,
                        ReadOnly        = 0x00000001,
                        WriteOnly       = 0x00000002,
                        Create          = 0x00000004,
                        Append          = 0x00000008,
                        NonBlocking     = 0x00000010,
                        DirectIO        = 0x00000020,
                        Sync            = 0x00000040,
                        Truncate        = 0x00000080,
                        Exclusive       = 0x00000100,
                        ReadWrite       = (ReadOnly | WriteOnly)
                };

#if defined(_WIN32) || defined(_WIN64)
                using FileHandle = HANDLE;
                static constexpr FileHandle FileHandleClosedValue = nullptr;
#else
                using FileHandle = int;
                static constexpr FileHandle FileHandleClosedValue = -1;
#endif
                using FileBytes = int64_t;

                // Don't allow this class to be copied
                File(const File &) = delete;
                File &operator=(const File &) = delete;

		File() = default;
		File(const String &fn) : _filename(fn) { }
                ~File();

                const String &filename() const {
                        return _filename;
                }

                int flags() const {
                        return _flags;
                }

                bool isReadable() const {
                        return _flags & ReadOnly;
                }

                bool isWritable() const {
                        return _flags & WriteOnly;
                }

                bool isDirectIO() const {
                        return _flags & DirectIO;
                }

                bool isOpen() const {
                        return _handle != FileHandleClosedValue;
                }

                Error setDirectIOEnabled(bool val);

                Error open(int flags);
                void close();

                FileBytes write(const void *buf, size_t bytes) const;
                FileBytes read(void *buf, size_t bytes) const;
                FileBytes position() const;
                FileBytes seek(FileBytes offset) const;
                FileBytes seekFromCurrent(FileBytes offset) const;
                FileBytes seekFromEnd(FileBytes offset) const;
                Error truncate(FileBytes offset) const;

        private:
                String          _filename;
                int             _flags = NoFlags;
                FileHandle      _handle = FileHandleClosedValue;

};

PROMEKI_NAMESPACE_END

