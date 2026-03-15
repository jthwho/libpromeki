/**
 * @file      core/iodevice.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/objectbase.h>
#include <promeki/core/error.h>
#include <promeki/core/variant.h>
#include <promeki/core/result.h>
#include <promeki/core/map.h>
#include <promeki/core/atomic.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base class for all I/O devices.
 *
 * IODevice provides a uniform interface for reading and writing data
 * to devices such as files, sockets, pipes, and in-memory buffers.
 * All I/O device classes in promeki derive from this class.
 *
 * @par Options
 * Device capabilities are expressed through options that subclasses
 * register via registerOption(). Each option has a runtime-assigned
 * type ID (allocated by registerOptionType()) and a Variant value.
 * Client code queries and sets options by type ID:
 * @code
 * if(dev.optionSupported(IODevice::DirectIO)) {
 *         dev.setOption(IODevice::DirectIO, true);
 * }
 * auto [val, err] = dev.option(IODevice::DirectIO);
 * @endcode
 *
 * This class must only be used from the thread that created it
 * (or moved to via moveToThread()).
 */
class IODevice : public ObjectBase {
        PROMEKI_OBJECT(IODevice, ObjectBase)
        public:
                /** @brief Integer type used to identify option kinds. */
                using Option = uint32_t;

                /** @brief Sentinel value representing an invalid or unset option type. */
                static constexpr Option InvalidOption = 0;

                /**
                 * @brief Allocates and returns a unique option type ID.
                 *
                 * Each call returns a new, never-before-used ID. Thread-safe.
                 * @return A unique Option value.
                 */
                static Option registerOptionType();

                /** @brief Option for direct I/O (bypass OS page cache). */
                static const Option DirectIO;

                /** @brief Option for synchronous writes. */
                static const Option Synchronous;

                /** @brief Option for non-blocking operations. */
                static const Option NonBlocking;

                /** @brief Option for unbuffered I/O. */
                static const Option Unbuffered;

                /** @brief Mode flags controlling how a device is opened. */
                enum OpenMode {
                        NotOpen   = 0x00, ///< @brief Device is not open.
                        ReadOnly  = 0x01, ///< @brief Open for reading only.
                        WriteOnly = 0x02, ///< @brief Open for writing only.
                        ReadWrite = ReadOnly | WriteOnly ///< @brief Open for reading and writing.
                };

                /** @brief Initializer list type for declaring supported options. */
                using OptionList = std::initializer_list<std::pair<const Option, Variant>>;

                /**
                 * @brief Constructs an IODevice.
                 * @param parent The parent object, or nullptr.
                 */
                IODevice(ObjectBase *parent = nullptr) : ObjectBase(parent) { }

                /**
                 * @brief Constructs an IODevice with supported options.
                 * @param opts Initializer list of {Option, defaultValue} pairs.
                 * @param parent The parent object, or nullptr.
                 */
                IODevice(OptionList opts, ObjectBase *parent = nullptr) :
                        ObjectBase(parent), _options(opts) { }

                /** @brief Destructor. */
                virtual ~IODevice();

                /**
                 * @brief Opens the device with the specified mode.
                 * @param mode The open mode (ReadOnly, WriteOnly, or ReadWrite).
                 * @return Error::Ok on success, or an error on failure.
                 */
                virtual Error open(OpenMode mode) = 0;

                /**
                 * @brief Closes the device.
                 */
                virtual void close() = 0;

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
                 * @return true on success, false on failure or if not seekable.
                 */
                virtual bool seek(int64_t pos);

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
                 * @return The device size, or 0 if unknown.
                 */
                virtual int64_t size() const;

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

                /**
                 * @brief Sets an option value by type ID.
                 *
                 * Returns NotSupported if the device does not support this option.
                 * On success, calls onOptionChanged().
                 *
                 * @param opt The option type identifier.
                 * @param value The value to set.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error setOption(Option opt, const Variant &value);

                /**
                 * @brief Returns the current value of an option.
                 *
                 * Returns NotSupported error if the option is not registered.
                 *
                 * @param opt The option type identifier.
                 * @return A Result containing the Variant value or an error.
                 */
                Result<Variant> option(Option opt) const;

                /**
                 * @brief Returns true if the device supports the given option.
                 * @param opt The option type identifier.
                 * @return true if the option is registered on this device.
                 */
                bool optionSupported(Option opt) const;

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

                /**
                 * @brief Registers an option with a default value.
                 *
                 * Call this from subclass constructors to declare
                 * supported options.
                 *
                 * @param opt The option type identifier.
                 * @param defaultValue The initial value for this option.
                 */
                void registerOption(Option opt, const Variant &defaultValue = Variant());

                /**
                 * @brief Called when an option value changes via setOption().
                 *
                 * Override this in subclasses to react to option changes
                 * (e.g., toggle O_DIRECT on the file descriptor). The
                 * default implementation does nothing.
                 *
                 * @param opt The option that changed.
                 * @param value The new value.
                 */
                virtual void onOptionChanged(Option opt, const Variant &value);

        private:
                OpenMode                        _openMode = NotOpen;
                Error                           _error;
                Map<Option, Variant>            _options;
};

PROMEKI_NAMESPACE_END
