/**
 * @file      bufferediodevice.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/iodevice.h>
#include <promeki/buffer.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract IODevice with an internal read buffer.
 * @ingroup io
 *
 * Derives from IODevice and overrides read() to serve data from an
 * internal Buffer. Subclasses implement readFromDevice() for raw I/O;
 * the remaining pure virtuals (open, close, isOpen, write) stay pure
 * for concrete subclasses like File to provide.
 *
 * The read buffer is allocated lazily on first open (default 8192 bytes)
 * or can be replaced via setReadBuffer() before opening.
 *
 * When unbuffered mode is enabled via setUnbuffered(), all reads
 * bypass the internal buffer and go directly to readFromDevice().
 *
 * @par Thread Safety
 * Inherits @ref IODevice: thread-affine.  A single instance must
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

                bool   _unbuffered = false;      ///< Unbuffered option storage.
                Buffer _readBuf;                 ///< The read buffer (replaceable).
                size_t _readBufPos = 0;          ///< Read cursor within buffer.
                size_t _readBufFill = 0;         ///< Bytes of valid data in buffer.
                bool   _bufferAllocated = false; ///< True after buffer is ready.

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
