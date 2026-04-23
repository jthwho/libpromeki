/**
 * @file      iodevice.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base class for all I/O devices.
 * @ingroup io
 *
 * IODevice provides a uniform interface for reading and writing data
 * to devices such as files, sockets, pipes, and in-memory buffers.
 * All I/O device classes in promeki derive from this class.
 *
 * This class must only be used from the thread that created it
 * (or moved to via moveToThread()).
 */
class IODevice : public ObjectBase {
        PROMEKI_OBJECT(IODevice, ObjectBase)
        public:
                /** @brief Unique-ownership pointer to an IODevice. */
                using UPtr = UniquePtr<IODevice>;

                /** @brief Mode flags controlling how a device is opened. */
                enum OpenMode {
                        NotOpen   = 0x00, ///< @brief Device is not open.
                        ReadOnly  = 0x01, ///< @brief Open for reading only.
                        WriteOnly = 0x02, ///< @brief Open for writing only.
                        ReadWrite = ReadOnly | WriteOnly, ///< @brief Open for reading and writing.
                        Append    = 0x04 | WriteOnly ///< @brief Open for appending (implies WriteOnly).
                };

                /**
                 * @brief Constructs an IODevice.
                 * @param parent The parent object, or nullptr.
                 */
                IODevice(ObjectBase *parent = nullptr) : ObjectBase(parent) { }

                /** @brief Destructor. */
                virtual ~IODevice();

                /**
                 * @brief Opens the device with the specified mode.
                 * @param mode The open mode (ReadOnly, WriteOnly, ReadWrite, or Append).
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error open(OpenMode mode) = 0;

                /**
                 * @brief Closes the device.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error close() = 0;

                /**
                 * @brief Returns true if the device is open.
                 * @return true if the device is currently open.
                 */
                virtual bool isOpen() const = 0;

                /**
                 * @brief Reads up to maxSize bytes into data.
                 * @param data Pointer to the buffer to read into.
                 * @param maxSize Maximum number of bytes to read.
                 * @return The number of bytes read, or -1 on error.
                 */
                virtual int64_t read(void *data, int64_t maxSize) = 0;

                /**
                 * @brief Writes up to maxSize bytes from data.
                 * @param data Pointer to the data to write.
                 * @param maxSize Number of bytes to write.
                 * @return The number of bytes written, or -1 on error.
                 */
                virtual int64_t write(const void *data, int64_t maxSize) = 0;

                /**
                 * @brief Flushes any buffered output data to the underlying device.
                 *
                 * The default implementation is a no-op, suitable for unbuffered
                 * devices such as in-memory buffers. Subclasses that wrap buffered
                 * I/O (e.g. stdio FILE*) should override this.
                 */
                virtual void flush();

                /**
                 * @brief Returns the number of bytes available for reading.
                 *
                 * The default implementation returns 0.
                 * @return The number of bytes available.
                 */
                virtual int64_t bytesAvailable() const;

                /**
                 * @brief Waits until data is available for reading or timeout.
                 * @param timeoutMs Timeout in milliseconds (0 = wait forever).
                 * @return true if data became available, false on timeout.
                 */
                virtual bool waitForReadyRead(unsigned int timeoutMs = 0);

                /**
                 * @brief Waits until all pending data has been written or timeout.
                 * @param timeoutMs Timeout in milliseconds (0 = wait forever).
                 * @return true if all data was written, false on timeout.
                 */
                virtual bool waitForBytesWritten(unsigned int timeoutMs = 0);

                /**
                 * @brief Returns true if the device is sequential (non-seekable).
                 *
                 * Sequential devices (pipes, sockets) cannot be seeked. The
                 * default implementation returns false.
                 * @return true if the device is sequential.
                 */
                virtual bool isSequential() const;

                /**
                 * @brief Seeks to the given byte offset from the beginning.
                 * @param pos The byte offset to seek to.
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error seek(int64_t pos);

                /**
                 * @brief Returns the current read/write position.
                 *
                 * The default implementation returns 0.
                 * @return The current position in bytes.
                 */
                virtual int64_t pos() const;

                /**
                 * @brief Returns the total size of the device in bytes.
                 *
                 * The default implementation returns 0.
                 * @return A Result containing the device size, or an error.
                 */
                virtual Result<int64_t> size() const;

                /**
                 * @brief Returns true if the current position is at the end.
                 *
                 * The default implementation returns true if pos() >= size().
                 * @return true if at end of device.
                 */
                virtual bool atEnd() const;

                /**
                 * @brief Returns the current open mode.
                 * @return The OpenMode the device was opened with.
                 */
                OpenMode openMode() const {
                        return _openMode;
                }

                /**
                 * @brief Returns true if the device is readable.
                 * @return true if the ReadOnly bit is set in the open mode.
                 */
                bool isReadable() const {
                        return _openMode & ReadOnly;
                }

                /**
                 * @brief Returns true if the device is writable.
                 * @return true if the WriteOnly bit is set in the open mode.
                 */
                bool isWritable() const {
                        return _openMode & WriteOnly;
                }

                /**
                 * @brief Returns the current error state.
                 * @return The last error set on this device.
                 */
                Error error() const {
                        return _error;
                }

                /**
                 * @brief Clears the error state to Ok.
                 */
                void clearError() {
                        _error = Error();
                        return;
                }

                /** @brief Emitted when data is available for reading. @signal */
                PROMEKI_SIGNAL(readyRead);

                /** @brief Emitted when bytes have been written. @signal */
                PROMEKI_SIGNAL(bytesWritten, int64_t);

                /** @brief Emitted when an error occurs. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

                /** @brief Emitted just before the device is closed. @signal */
                PROMEKI_SIGNAL(aboutToClose);

        protected:
                /**
                 * @brief Sets the open mode.
                 *
                 * Call this from subclass open() implementations.
                 * @param mode The open mode to set.
                 */
                void setOpenMode(OpenMode mode) {
                        _openMode = mode;
                        return;
                }

                /**
                 * @brief Sets the error state and emits errorOccurred.
                 * @param err The error to set.
                 */
                void setError(const Error &err) {
                        _error = err;
                        if(err.isError()) errorOccurredSignal.emit(err);
                        return;
                }

        private:
                OpenMode                        _openMode = NotOpen;
                Error                           _error;
};

PROMEKI_NAMESPACE_END
