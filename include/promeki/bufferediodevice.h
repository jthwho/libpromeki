/**
 * @file      bufferediodevice.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/iodevice.h>
#include <promeki/buffer.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract IODevice with internal read and (optional) write buffers.
 * @ingroup io
 *
 * Derives from IODevice and provides buffering in both directions.
 * Subclasses implement the raw transfer primitives readFromDevice()
 * and writeToDevice(); this class layers read-ahead and write-behind
 * buffering on top.  open(), close() and isOpen() stay pure for
 * concrete subclasses like File to provide.
 *
 * @par Read buffering
 * read() serves from an internal Buffer, allocated lazily on first
 * open (default 8192 bytes) or replaceable via setReadBuffer() before
 * opening.  setUnbuffered() makes all reads bypass the buffer and go
 * straight to readFromDevice().
 *
 * @par Write buffering
 * Disabled by default, so write() passes straight through to
 * writeToDevice() and existing subclass semantics are unchanged.  When
 * enabled via setWriteBuffered(), write() accumulates into an internal
 * buffer that is drained to writeToDevice() on flush(), or
 * automatically once it reaches the write-buffer capacity.  This
 * coalesces a burst of small writes — the classic case being an
 * @ref AnsiStream emitting an escape sequence a few bytes at a time per
 * cell — into far fewer underlying writes.  A subclass that may buffer
 * writes MUST flush() before it releases its descriptor in close() and
 * before any path that bypasses write() (e.g. a positional or vectored
 * write), so buffered bytes are not lost or reordered.
 *
 * @par Thread Safety
 * Inherits @ref IODevice &mdash; thread-affine.  A single instance must
 * only be used from the thread that created it.
 */
class BufferedIODevice : public IODevice {
                PROMEKI_OBJECT(BufferedIODevice, IODevice)
        public:
                /**
                 * @brief Constructs a BufferedIODevice.
                 * @param parent The parent object, or nullptr.
                 */
                BufferedIODevice(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                virtual ~BufferedIODevice();

                /**
                 * @brief Reads up to maxSize bytes into data.
                 *
                 * Serves data from the internal read buffer when possible.
                 * Large reads (>= buffer capacity) bypass the buffer and
                 * go directly to readFromDevice(). When unbuffered mode is enabled,
                 * all reads bypass the buffer.
                 *
                 * @param data Pointer to the buffer to read into.
                 * @param maxSize Maximum number of bytes to read.
                 * @return The number of bytes read, or -1 on error.
                 */
                int64_t read(void *data, int64_t maxSize) override;

                /**
                 * @brief Returns the number of bytes available for reading.
                 *
                 * Returns buffered bytes plus deviceBytesAvailable().
                 * When unbuffered mode is enabled, returns only deviceBytesAvailable().
                 * @return The number of bytes available.
                 */
                int64_t bytesAvailable() const override;

                /**
                 * @brief Replaces the internal read buffer.
                 *
                 * Only allowed when the device is not open. The buffer
                 * must be host-accessible (e.g. system RAM or secure memory).
                 *
                 * @param buf The replacement buffer.
                 * @return Error::Ok on success, Error::AlreadyOpen if the device
                 *         is open, or Error::NotHostAccessible if the buffer is
                 *         not host-accessible.
                 */
                Error setReadBuffer(Buffer &&buf);

                /**
                 * @brief Returns a const reference to the current read buffer.
                 * @return The internal read buffer.
                 */
                const Buffer &readBuffer() const { return _readBuf; }

                /**
                 * @brief Returns the capacity of the read buffer.
                 * @return The buffer size in bytes, or 0 if not yet allocated.
                 */
                size_t readBufferSize() const { return _readBuf.availSize(); }

                /**
                 * @brief Reads a line of text up to a newline character.
                 *
                 * Reads until a newline ('\\n') is found or maxLength bytes
                 * have been read. The newline is included in the result.
                 *
                 * @param maxLength Maximum bytes to read (0 = no limit).
                 * @return A Buffer containing the line data.
                 */
                Buffer readLine(size_t maxLength = 0);

                /**
                 * @brief Reads all available data from the device.
                 * @return A Buffer containing all data.
                 */
                Buffer readAll();

                /**
                 * @brief Reads up to maxBytes bytes and returns as a Buffer.
                 * @param maxBytes Maximum number of bytes to read.
                 * @return A Buffer containing the read data.
                 */
                Buffer readBytes(size_t maxBytes);

                /**
                 * @brief Returns true if a complete line is available for reading.
                 *
                 * Checks whether a newline character exists in the buffered data.
                 * Returns false when Unbuffered is true.
                 * @return true if a newline is present in the buffer.
                 */
                bool canReadLine() const;

                /**
                 * @brief Reads up to maxBytes without consuming them.
                 *
                 * Returns 0 when Unbuffered is true.
                 * @param buf Pointer to the destination buffer.
                 * @param maxBytes Maximum number of bytes to peek.
                 * @return The number of bytes peeked.
                 */
                int64_t peek(void *buf, size_t maxBytes) const;

                /**
                 * @brief Reads up to maxBytes without consuming them.
                 *
                 * Returns empty Buffer when Unbuffered is true.
                 * @param maxBytes Maximum number of bytes to peek.
                 * @return A Buffer containing the peeked data.
                 */
                Buffer peek(size_t maxBytes) const;

                /**
                 * @brief Enables or disables unbuffered mode.
                 *
                 * When switching to unbuffered while the device is open,
                 * the internal read buffer is drained and reset. When
                 * switching back to buffered while open, the read buffer
                 * is re-ensured.
                 *
                 * @param enable true to bypass the internal buffer.
                 */
                void setUnbuffered(bool enable);

                /**
                 * @brief Returns true if unbuffered mode is enabled.
                 * @return true if reads bypass the internal buffer.
                 */
                bool isUnbuffered() const { return _unbuffered; }

                /**
                 * @brief Writes up to maxSize bytes from data.
                 *
                 * When write buffering is enabled, the bytes are appended to
                 * the internal write buffer (auto-flushing once it reaches
                 * capacity) and @p maxSize is returned even though the bytes
                 * have not yet reached the device.  When disabled, the write
                 * passes straight through to writeToDevice().
                 *
                 * @param data Pointer to the data to write.
                 * @param maxSize Number of bytes to write.
                 * @return The number of bytes accepted, or -1 on error.
                 */
                int64_t write(const void *data, int64_t maxSize) override;

                /**
                 * @brief Drains any buffered output to the device.
                 *
                 * A no-op when write buffering is disabled or the buffer is
                 * empty.  On a non-blocking device that cannot accept all of
                 * it, the unwritten tail is retained for a later flush rather
                 * than dropped.
                 */
                void flush() override;

                /**
                 * @brief Enables or disables output (write) buffering.
                 *
                 * Disabling flushes any currently buffered output first, so no
                 * bytes are lost or reordered across the switch.
                 *
                 * @param enable true to buffer writes, false to write through.
                 */
                void setWriteBuffered(bool enable);

                /**
                 * @brief Returns true if output (write) buffering is enabled.
                 * @return true if writes accumulate until flushed.
                 */
                bool isWriteBuffered() const { return _writeBuffered; }

                /**
                 * @brief Returns the write buffer capacity in bytes.
                 * @return The capacity at which a buffered write auto-flushes.
                 */
                size_t writeBufferCapacity() const { return _writeBufCapacity; }

                /**
                 * @brief Sets the write buffer capacity.
                 *
                 * Buffered output is auto-flushed once the pending bytes reach
                 * this many bytes; a single write larger than the capacity is
                 * sent directly after flushing whatever was pending.
                 *
                 * @param bytes The new capacity (a zero is treated as 1).
                 */
                void setWriteBufferCapacity(size_t bytes);

        protected:
                /**
                 * @brief Reads raw data from the underlying device.
                 *
                 * Subclasses must implement this to perform actual I/O.
                 *
                 * @param data Pointer to the buffer to read into.
                 * @param maxSize Maximum number of bytes to read.
                 * @return The number of bytes read, or -1 on error.
                 */
                virtual int64_t readFromDevice(void *data, int64_t maxSize) = 0;

                /**
                 * @brief Writes raw data to the underlying device.
                 *
                 * Subclasses must implement this to perform actual I/O.  It is
                 * called directly by write() in write-through mode and by
                 * flush() when draining the write buffer.
                 *
                 * @param data Pointer to the data to write.
                 * @param maxSize Number of bytes to write.
                 * @return The number of bytes written (which may be less than
                 *         @p maxSize on a non-blocking device), or -1 on error.
                 */
                virtual int64_t writeToDevice(const void *data, int64_t maxSize) = 0;

                /**
                 * @brief Returns the number of bytes available from the device.
                 *
                 * The default implementation returns 0. Subclasses may
                 * override to report device-level availability.
                 *
                 * @return The number of bytes available from the device.
                 */
                virtual int64_t deviceBytesAvailable() const;

                /**
                 * @brief Returns the number of buffered bytes not yet consumed.
                 *
                 * Subclasses that override pos() should subtract this value
                 * from the raw device position so that pos() reflects the
                 * logical read position rather than the device-level position
                 * (which may be ahead due to read-ahead buffering).
                 *
                 * @return The number of unconsumed bytes in the read buffer.
                 */
                size_t bufferedBytesUnconsumed() const { return _readBufFill - _readBufPos; }

                /**
                 * @brief Returns the number of buffered output bytes not yet written.
                 *
                 * The write-behind counterpart to bufferedBytesUnconsumed():
                 * these bytes have been accepted by write() but not yet handed
                 * to writeToDevice(), so the device position lags the logical
                 * write position by this much.  Subclasses that override pos()
                 * should add this value to the raw device position.
                 *
                 * @return The number of pending bytes in the write buffer.
                 */
                size_t bufferedBytesPending() const { return _writeBuf.size(); }

                /**
                 * @brief Ensures the read buffer is allocated.
                 *
                 * Called during open(). If no custom buffer has been set,
                 * allocates a default 8192-byte buffer.
                 */
                void ensureReadBuffer();

                /**
                 * @brief Resets the read buffer cursors.
                 *
                 * Called during close() to reset the buffer state.
                 * The buffer itself is retained for reuse.
                 */
                void resetReadBuffer();

        private:
                /** @brief Default read buffer size in bytes. */
                static constexpr size_t DefaultReadBufSize = 8192;

                /** @brief Default write buffer capacity in bytes. */
                static constexpr size_t DefaultWriteBufSize = 8192;

                bool   _unbuffered = false;      ///< Unbuffered (read) option storage.
                Buffer _readBuf;                 ///< The read buffer (replaceable).
                size_t _readBufPos = 0;          ///< Read cursor within buffer.
                size_t _readBufFill = 0;         ///< Bytes of valid data in buffer.
                bool   _bufferAllocated = false; ///< True after buffer is ready.

                bool       _writeBuffered = false;                 ///< Write-buffering option storage.
                size_t     _writeBufCapacity = DefaultWriteBufSize; ///< Auto-flush threshold.
                List<char> _writeBuf;                              ///< Pending output bytes.

                /**
                 * @brief Fills the internal read buffer from the device.
                 * @return The number of new bytes read, or -1 on error.
                 */
                int64_t fillBuffer();

                /**
                 * @brief Compacts the buffer by moving unread data to the front.
                 */
                void compactBuffer();
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
