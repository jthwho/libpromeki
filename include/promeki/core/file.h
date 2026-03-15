/**
 * @file      core/file.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/platform.h>
#include <promeki/core/string.h>
#include <promeki/core/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Low-level file I/O wrapper.
 *
 * Provides a platform-independent interface for opening, reading, writing,
 * and seeking within files. Supports flags for controlling access mode,
 * direct I/O, synchronous writes, and other behaviors. This class is
 * non-copyable.
 */
class File {
	public:
                /** @brief Flags controlling how a file is opened and accessed. */
                enum Flags {
                        NoFlags         = 0x00000000, ///< @brief No flags set.
                        ReadOnly        = 0x00000001, ///< @brief Open for reading only.
                        WriteOnly       = 0x00000002, ///< @brief Open for writing only.
                        Create          = 0x00000004, ///< @brief Create the file if it does not exist.
                        Append          = 0x00000008, ///< @brief Append writes to the end of the file.
                        NonBlocking     = 0x00000010, ///< @brief Open in non-blocking mode.
                        DirectIO        = 0x00000020, ///< @brief Use direct I/O, bypassing OS page cache.
                        Sync            = 0x00000040, ///< @brief Synchronous write mode.
                        Truncate        = 0x00000080, ///< @brief Truncate the file to zero length on open.
                        Exclusive       = 0x00000100, ///< @brief Fail if the file already exists (with Create).
                        ReadWrite       = (ReadOnly | WriteOnly) ///< @brief Open for both reading and writing.
                };

#if defined(PROMEKI_PLATFORM_WINDOWS)
                /** @brief Platform-specific file handle type (Windows). */
                using FileHandle = HANDLE;
                /** @brief Sentinel value representing a closed file handle (Windows). */
                static constexpr FileHandle FileHandleClosedValue = nullptr;
#else
                /** @brief Platform-specific file handle type (POSIX). */
                using FileHandle = int;
                /** @brief Sentinel value representing a closed file handle (POSIX). */
                static constexpr FileHandle FileHandleClosedValue = -1;
#endif
                /** @brief Signed type used for byte counts and file offsets. */
                using FileBytes = int64_t;

                /** @brief Deleted copy constructor (non-copyable). */
                File(const File &) = delete;
                /** @brief Deleted copy assignment operator (non-copyable). */
                File &operator=(const File &) = delete;

                /** @brief Default constructor. Creates a File with no filename. */
		File() = default;

                /**
                 * @brief Constructs a File with the given filename.
                 * @param fn The path to the file.
                 */
		File(const String &fn) : _filename(fn) { }

                /** @brief Destructor. Closes the file if it is open. */
                ~File();

                /**
                 * @brief Returns the filename associated with this File.
                 * @return A const reference to the filename string.
                 */
                const String &filename() const {
                        return _filename;
                }

                /**
                 * @brief Returns the flags the file was opened with.
                 * @return The current flags bitmask.
                 */
                int flags() const {
                        return _flags;
                }

                /**
                 * @brief Returns true if the file was opened for reading.
                 * @return true if the ReadOnly flag is set.
                 */
                bool isReadable() const {
                        return _flags & ReadOnly;
                }

                /**
                 * @brief Returns true if the file was opened for writing.
                 * @return true if the WriteOnly flag is set.
                 */
                bool isWritable() const {
                        return _flags & WriteOnly;
                }

                /**
                 * @brief Returns true if direct I/O is enabled.
                 * @return true if the DirectIO flag is set.
                 */
                bool isDirectIO() const {
                        return _flags & DirectIO;
                }

                /**
                 * @brief Returns true if the file is currently open.
                 * @return true if the file handle is valid.
                 */
                bool isOpen() const {
                        return _handle != FileHandleClosedValue;
                }

                /**
                 * @brief Enables or disables direct I/O on an open file.
                 * @param val true to enable direct I/O, false to disable.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setDirectIOEnabled(bool val);

                /**
                 * @brief Opens the file with the specified flags.
                 * @param flags A bitmask of Flags values controlling the open mode.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error open(int flags);

                /** @brief Closes the file if it is open. */
                void close();

                /**
                 * @brief Writes data to the file at the current position.
                 * @param buf Pointer to the data to write.
                 * @param bytes Number of bytes to write.
                 * @return The number of bytes written, or a negative value on error.
                 */
                FileBytes write(const void *buf, size_t bytes) const;

                /**
                 * @brief Reads data from the file at the current position.
                 * @param buf Pointer to the buffer to read into.
                 * @param bytes Maximum number of bytes to read.
                 * @return The number of bytes read, or a negative value on error.
                 */
                FileBytes read(void *buf, size_t bytes) const;

                /**
                 * @brief Returns the current read/write position in the file.
                 * @return The current position in bytes from the beginning of the file.
                 */
                FileBytes position() const;

                /**
                 * @brief Seeks to an absolute byte offset from the beginning of the file.
                 * @param offset The byte offset to seek to.
                 * @return The resulting position, or a negative value on error.
                 */
                FileBytes seek(FileBytes offset) const;

                /**
                 * @brief Seeks relative to the current file position.
                 * @param offset The byte offset relative to the current position.
                 * @return The resulting position, or a negative value on error.
                 */
                FileBytes seekFromCurrent(FileBytes offset) const;

                /**
                 * @brief Seeks relative to the end of the file.
                 * @param offset The byte offset relative to the end (typically negative).
                 * @return The resulting position, or a negative value on error.
                 */
                FileBytes seekFromEnd(FileBytes offset) const;

                /**
                 * @brief Truncates the file to the specified length.
                 * @param offset The new file size in bytes.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error truncate(FileBytes offset) const;

        private:
                String          _filename;
                int             _flags = NoFlags;
                FileHandle      _handle = FileHandleClosedValue;

};

PROMEKI_NAMESPACE_END

