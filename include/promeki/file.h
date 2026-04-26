/**
 * @file      file.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/platform.h>
#include <promeki/string.h>
#include <promeki/filepath.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/bufferediodevice.h>
#include <promeki/uniqueptr.h>

// Forward-declared so file.h does not need to pull <cirf/types.h>
// into every consumer of File. The pointer is only ever stored as
// a member; methods that need to dereference it include
// <cirf/types.h> in file.cpp.
struct cirf_file;
typedef struct cirf_file cirf_file_t;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief File I/O device with buffered reading.
 * @ingroup io
 *
 * Derives from BufferedIODevice, providing a full IODevice interface
 * for file I/O. Supports direct I/O, synchronous writes, and
 * non-blocking mode via setDirectIO(), setSynchronous(), and
 * setNonBlocking().
 *
 * File-specific open flags (Create, Append, Truncate, Exclusive) are
 * passed via the open(OpenMode, int) overload. The IODevice open(OpenMode)
 * overload opens with no extra flags.
 *
 * When direct I/O is enabled, unbuffered mode is automatically forced.
 * The previous unbuffered state is saved and restored when direct I/O
 * is disabled.
 *
 * @par Thread Safety
 * Inherits @ref IODevice: thread-affine.  A single instance must
 * only be used from the thread that created it.
 */
class File : public BufferedIODevice {
        PROMEKI_OBJECT(File, BufferedIODevice)
        public:
                /** @brief Unique-ownership pointer to a File. */
                using UPtr = UniquePtr<File>;

                /**
                 * @brief File-specific open flags.
                 *
                 * These extend the IODevice OpenMode and are passed to
                 * open(OpenMode, int).
                 */
                enum Flags {
                        NoFlags         = 0x00,  ///< @brief No extra flags.
                        Create          = 0x01,  ///< @brief Create the file if it does not exist.
                        Append          = 0x02,  ///< @brief Append writes to the end of the file.
                        Truncate        = 0x04,  ///< @brief Truncate the file to zero length on open.
                        Exclusive       = 0x08   ///< @brief Fail if the file already exists (with Create).
                };

                /**
                 * @brief Scatter/gather buffer descriptor for vectored I/O.
                 *
                 * Platform-neutral alternative to POSIX struct iovec.
                 * Used with writev() to write multiple disjoint buffers
                 * in a single system call.
                 */
                struct IOVec {
                        const void *data;  ///< @brief Pointer to buffer.
                        size_t      size;  ///< @brief Size of buffer in bytes.
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

                /** @brief Deleted copy constructor (non-copyable). */
                File(const File &) = delete;
                /** @brief Deleted copy assignment operator (non-copyable). */
                File &operator=(const File &) = delete;

                /**
                 * @brief Default constructor. Creates a File with no filename.
                 * @param parent The parent object, or nullptr.
                 */
                File(ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a File with the given filename.
                 * @param fn The path to the file.
                 * @param parent The parent object, or nullptr.
                 */
                File(const String &fn, ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a File with the given C string filename.
                 * @param fn The path to the file.
                 * @param parent The parent object, or nullptr.
                 */
                File(const char *fn, ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a File with the given file path.
                 * @param fp The path to the file.
                 * @param parent The parent object, or nullptr.
                 */
                File(const FilePath &fp, ObjectBase *parent = nullptr);

                /** @brief Destructor. Closes the file if it is open. */
                ~File();

                /**
                 * @brief Returns the filename associated with this File.
                 * @return A const reference to the filename string.
                 */
                const String &filename() const { return _filename; }

                /**
                 * @brief Sets the filename.
                 *
                 * Only allowed when the file is not open.
                 * @param fn The new filename.
                 */
                void setFilename(const String &fn) { _filename = fn; }

                /**
                 * @brief Returns the file-specific flags used at open time.
                 * @return The Flags bitmask.
                 */
                int flags() const { return _fileFlags; }

                /**
                 * @brief Returns the native file handle.
                 * @return The platform-specific file handle.
                 */
                FileHandle handle() const { return _handle; }

                /**
                 * @brief Opens the file with the given mode and no extra flags.
                 * @param mode The open mode (ReadOnly, WriteOnly, or ReadWrite).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error open(OpenMode mode) override;

                /**
                 * @brief Opens the file with the given mode and file-specific flags.
                 * @param mode The open mode (ReadOnly, WriteOnly, or ReadWrite).
                 * @param fileFlags A bitmask of Flags (Create, Append, Truncate, Exclusive).
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error open(OpenMode mode, int fileFlags);

                /**
                 * @brief Closes the file if it is open.
                 * @return Error::Ok on success, or an error if the close syscall fails.
                 */
                Error close() override;

                /** @brief Returns true if the file is currently open. */
                bool isOpen() const override;

                /**
                 * @brief Writes data to the file at the current position.
                 * @param data Pointer to the data to write.
                 * @param maxSize Number of bytes to write.
                 * @return The number of bytes written, or -1 on error.
                 */
                int64_t write(const void *data, int64_t maxSize) override;

                /** @brief Returns false (files are seekable). */
                bool isSequential() const override;

                /**
                 * @brief Seeks to an absolute byte offset.
                 * @param offset The byte offset to seek to.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error seek(int64_t offset) override;

                /**
                 * @brief Returns the current read/write position.
                 * @return The current position in bytes.
                 */
                int64_t pos() const override;

                /**
                 * @brief Returns the total file size in bytes.
                 * @return A Result containing the file size, or an error
                 *         if fstat fails.
                 */
                Result<int64_t> size() const override;

                /**
                 * @brief Returns true if the position is at or past the end.
                 * @return true if at end of file.
                 */
                bool atEnd() const override;

                /**
                 * @brief Seeks relative to the current file position.
                 * @param offset The byte offset relative to the current position.
                 * @return A Result containing the new position, or an error.
                 */
                Result<int64_t> seekFromCurrent(int64_t offset) const;

                /**
                 * @brief Seeks relative to the end of the file.
                 * @param offset The byte offset relative to the end (typically negative or 0).
                 * @return A Result containing the new position, or an error.
                 */
                Result<int64_t> seekFromEnd(int64_t offset) const;

                /**
                 * @brief Truncates the file to the specified length.
                 * @param offset The new file size in bytes.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error truncate(int64_t offset) const;

                /**
                 * @brief Writes multiple buffers to the file in a single system call.
                 *
                 * Uses the platform's scatter/gather I/O (writev on POSIX) to
                 * write all buffers atomically from the application's perspective.
                 * This avoids the overhead of multiple write() calls and ensures
                 * that header + payload are written in one kernel transition,
                 * which is important for direct I/O writes where each transfer
                 * must be aligned.
                 *
                 * @param iov   Array of IOVec structures describing buffers.
                 * @param count Number of IOVec entries.
                 * @return The total number of bytes written, or -1 on error.
                 */
                int64_t writev(const IOVec *iov, int count);

                /**
                 * @brief Preallocates file space without writing data.
                 *
                 * Uses posix_fallocate() to ensure that the requested disk
                 * space is available, avoiding ENOSPC during subsequent writes.
                 * This is a best-effort operation; the file size is not changed.
                 *
                 * @param offset Starting byte offset for the allocation.
                 * @param length Number of bytes to preallocate.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error preallocate(int64_t offset, int64_t length);

                /**
                 * @brief Returns the direct I/O alignment requirement for this file.
                 *
                 * When the file is open, returns the filesystem block size via fstat.
                 * This is the alignment required for file offsets, buffer addresses,
                 * and transfer sizes when direct I/O is enabled.
                 *
                 * @return A Result containing the alignment in bytes, or an error
                 *         if the file is not open or fstat fails.
                 */
                Result<size_t> directIOAlignment() const;

                /**
                 * @brief Reads bulk data from the current file position using direct I/O.
                 *
                 * Reads @p size bytes from the current file position into @p buf,
                 * using direct I/O for the aligned interior of the region and
                 * normal I/O for any unaligned head and tail bytes. This is the
                 * optimal strategy for reading large media payloads (image data,
                 * audio samples) from container files, bypassing the OS page
                 * cache for the bulk transfer while handling misaligned
                 * boundaries gracefully.
                 *
                 * This method calls shiftData() on @p buf so that the direct
                 * I/O portion lands on an aligned destination address. After a
                 * successful call, buf.data() points to the first byte of the
                 * requested payload. The caller only needs to ensure the buffer
                 * is allocated with at least directIOAlignment() alignment and
                 * that allocSize() is at least @p size + directIOAlignment():
                 *
                 * @code
                 * size_t align = file.directIOAlignment();
                 * Buffer buf(payloadSize + align, align);
                 * Error err = file.readBulk(buf, payloadSize);
                 * // buf.data() now points to the start of the payload
                 * @endcode
                 *
                 * The buffer must be host-accessible and allocSize() must be
                 * large enough to accommodate the shift plus @p size bytes.
                 *
                 * If the region is smaller than one alignment block, or if the
                 * alignment cannot be determined, a normal (non-DIO) read is
                 * used for the entire region. If direct I/O fails at runtime
                 * (e.g. the filesystem does not support O_DIRECT), the method
                 * falls back to normal I/O for that portion automatically.
                 *
                 * If fewer than @p size bytes remain in the file, the read
                 * succeeds and buf.size() reflects the actual number of bytes
                 * read (which will be less than @p size).
                 *
                 * @note Not supported on non-blocking file descriptors.
                 * @c readBulk() returns @c Error::NotSupported immediately
                 * when the @c File is in non-blocking mode rather than
                 * risking partial transfers — the underlying loop treats
                 * EAGAIN as a hard error and would lose already-read data
                 * from earlier portions of the transfer.  Lifting this
                 * restriction requires reworking the internal read loop to
                 * retry on EAGAIN (likely via a poll/event-loop integration).
                 *
                 * @param buf  Destination buffer (must be host-accessible).
                 * @param size Number of bytes to read.
                 * @return Error::Ok on success (check buf.size() for actual
                 *         bytes read), or an appropriate error code.
                 */
                Error readBulk(Buffer &buf, int64_t size);

                /**
                 * @brief Writes bulk data to the current file position using direct I/O.
                 *
                 * Symmetric to @c readBulk(). Writes @p size bytes from
                 * @p data to the file starting at the current write
                 * position, using direct I/O for the aligned interior
                 * of the region and normal I/O for any unaligned head
                 * and tail bytes. This is the optimal strategy for
                 * writing large media payloads (image data, audio
                 * samples) to container files, bypassing the OS page
                 * cache for the bulk transfer so sustained captures
                 * don't thrash other applications' cached pages.
                 *
                 * Unlike @c readBulk() which receives a Buffer and uses
                 * @c shiftData() to land the payload on an aligned
                 * address, @c writeBulk() takes a raw pointer + size.
                 * For O_DIRECT writes to succeed, the source pointer
                 * must be aligned to @c directIOAlignment(); if it is
                 * not, @c writeBulk() falls back to non-DIO writes for
                 * the entire region. Passing the @c data() pointer of
                 * a page-aligned @c Buffer (e.g. the default) satisfies
                 * this requirement.
                 *
                 * If alignment cannot be determined, the DIO portion
                 * is below one alignment block, or the DIO write fails
                 * at runtime, @c writeBulk() falls back to a normal
                 * write automatically.
                 *
                 * @param data Source pointer, should be directIOAlignment()-aligned
                 *             for the aligned interior to go through DIO.
                 * @param size Number of bytes to write.
                 * @return The number of bytes written (equal to @p size on
                 *         success) or -1 on hard I/O failure.
                 */
                int64_t writeBulk(const void *data, int64_t size);

                /**
                 * @brief Forces any buffered writes to stable storage.
                 *
                 * Calls @c fdatasync() (or @c fsync() if @p dataOnly is
                 * false) on the underlying file descriptor. Blocks until
                 * the kernel confirms the data is durable — expensive,
                 * but required for crash-consistent checkpoints.
                 *
                 * @c fdatasync() only flushes file data + metadata that
                 * the data depends on (e.g. file size). @c fsync() also
                 * flushes inode metadata (timestamps, etc.) — rarely
                 * needed for media workloads.
                 *
                 * @param dataOnly  If true (default), use fdatasync; if
                 *                  false, use fsync.
                 * @return Error::Ok on success, or a system error.
                 */
                Error sync(bool dataOnly = true);

                /**
                 * @brief Enables or disables direct I/O (bypass OS page cache).
                 *
                 * When enabled, unbuffered mode is automatically forced on.
                 * When disabled, the previous unbuffered state is restored.
                 * If the file is open, the O_DIRECT flag is toggled via fcntl.
                 *
                 * @param enable true to enable direct I/O.
                 * @return Error::Ok on success, or an error if the fcntl call fails.
                 */
                Error setDirectIO(bool enable);

                /**
                 * @brief Returns true if direct I/O is enabled.
                 * @return true if direct I/O is active.
                 */
                bool isDirectIO() const { return _directIO; }

                /**
                 * @brief Enables or disables synchronous writes.
                 *
                 * If the file is open, the O_SYNC flag is toggled via fcntl.
                 *
                 * @param enable true to enable synchronous writes.
                 * @return Error::Ok on success, or an error if the fcntl call fails.
                 */
                Error setSynchronous(bool enable);

                /**
                 * @brief Returns true if synchronous writes are enabled.
                 * @return true if synchronous mode is active.
                 */
                bool isSynchronous() const { return _synchronous; }

                /**
                 * @brief Enables or disables non-blocking mode.
                 *
                 * If the file is open, the O_NONBLOCK flag is toggled via fcntl.
                 *
                 * @param enable true to enable non-blocking operations.
                 * @return Error::Ok on success, or an error if the fcntl call fails.
                 */
                Error setNonBlocking(bool enable);

                /**
                 * @brief Returns true if non-blocking mode is enabled.
                 * @return true if non-blocking mode is active.
                 */
                bool isNonBlocking() const { return _nonBlocking; }

                /**
                 * @brief Returns true if this File is currently serving a compiled-in resource.
                 *
                 * Resource-mode files are opened by passing a path that
                 * begins with @c ":/" — see @ref Resource. They behave
                 * like read-only memory-backed files: write operations,
                 * preallocate, truncate and direct-I/O knobs are no-ops
                 * or report @c Error::ReadOnly.
                 */
                bool isResource() const { return _resourceFile != nullptr; }

        protected:

                /** @brief Reads raw data from the file descriptor. */
                int64_t readFromDevice(void *data, int64_t maxSize) override;

                /** @brief Returns bytes available from the device (file). */
                int64_t deviceBytesAvailable() const override;

        private:
                bool                _directIO = false;         ///< Direct I/O mode.
                bool                _synchronous = false;      ///< Synchronous write mode.
                bool                _nonBlocking = false;      ///< Non-blocking mode.
                String              _filename;
                int                 _fileFlags = NoFlags;
                FileHandle          _handle = FileHandleClosedValue;
                bool                _savedUnbuffered = false;  ///< Unbuffered state saved before DirectIO forced it.
                const cirf_file_t  *_resourceFile = nullptr;   ///< Non-null when serving a ":/..." resource path.
                int64_t             _resourcePos = 0;          ///< Read cursor for resource-mode files.
};

PROMEKI_NAMESPACE_END
