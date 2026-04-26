/**
 * @file      streamstring.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/iodevice.h>
#include <promeki/textstream.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Write-only IODevice that intercepts writes, accumulates
 *        characters, and invokes a callback on newlines or flush.
 * @ingroup streams
 *
 * This is an implementation detail of StreamString and should not be
 * used directly.
 */
class StreamStringIODevice : public IODevice {
        PROMEKI_OBJECT(StreamStringIODevice, IODevice)
        public:
                /** @brief Callback type invoked for each completed line. */
                using OnNewLineFunc = std::function<bool(String &)>;

                /**
                 * @brief Constructs a StreamStringIODevice.
                 * @param parent The parent object, or nullptr.
                 */
                StreamStringIODevice(ObjectBase *parent = nullptr) : IODevice(parent) { }

                /**
                 * @brief Sets the callback invoked when a line is complete.
                 * @param func Callback to invoke on each complete line.
                 */
                void setOnNewLine(OnNewLineFunc func) {
                        _onNewLine = std::move(func);
                }

                /**
                 * @brief Returns the current (possibly incomplete) line buffer.
                 * @return Const reference to the accumulated line.
                 */
                const String &line() const { return _line; }

                /** @brief Clears the accumulated line buffer. */
                void clearLine() {
                        _line.clear();
                }

                /** @brief Opens the device in the specified mode. */
                Error open(OpenMode mode) override {
                        if(isOpen()) return Error(Error::AlreadyOpen);
                        setOpenMode(mode);
                        return Error();
                }

                /** @brief Closes the device. */
                Error close() override {
                        setOpenMode(NotOpen);
                        return Error();
                }

                /** @brief Returns true if the device is open. */
                bool isOpen() const override {
                        return openMode() != NotOpen;
                }

                /** @brief Always returns -1 (write-only device). */
                int64_t read(void *, int64_t) override {
                        return -1;
                }

                /**
                 * @brief Writes data, splitting on newlines and invoking the callback.
                 * @param data Pointer to the data to write.
                 * @param maxSize Number of bytes to write.
                 * @return The number of bytes written, or -1 on error.
                 */
                int64_t write(const void *data, int64_t maxSize) override {
                        if(!isOpen() || !isWritable()) return -1;
                        const char *p = static_cast<const char *>(data);
                        for(int64_t i = 0; i < maxSize; i++) {
                                if(p[i] == '\n') {
                                        flushLine();
                                } else {
                                        _line += p[i];
                                }
                        }
                        return maxSize;
                }

                /** @brief Flushes the current line buffer via the callback. */
                void flush() override {
                        flushLine();
                }

                /** @brief Returns true (this device is sequential). */
                bool isSequential() const override { return true; }

        private:
                OnNewLineFunc   _onNewLine;
                String          _line;

                void flushLine() {
                        if(!_line.isEmpty() && _onNewLine) {
                                if(_onNewLine(_line)) _line.clear();
                        }
                }
};

/**
 * @brief Collects text output into a String, splitting on newlines.
 * @ingroup streams
 *
 * StreamString accumulates characters written to it. When a newline
 * or flush is encountered, it invokes the onNewLine callback with the
 * accumulated line.  The callback returns true to clear the line
 * buffer, or false to keep accumulating.
 *
 * Example usage (routing stream output to the logger):
 * @code
 *   StreamString ss([](String &line) {
 *           promekiInfo("%s", line.cstr());
 *           return true;
 *   });
 *   ss.stream() << "Hello " << 42 << promeki::endl;
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.  The optional new-line callback runs
 * on the same thread that performs the streaming write.
 */
class StreamString {
        public:
                /** @brief Callback type invoked for each completed line. */
                using OnNewLineFunc = std::function<bool(String &)>;

                /** @brief Constructs a StreamString with no callback. */
                StreamString() : _stream(&_device) {
                        _device.open(IODevice::WriteOnly);
                }

                /**
                 * @brief Constructs a StreamString with a newline callback.
                 * @param func Callback invoked when a complete line is available.
                 */
                StreamString(OnNewLineFunc func) : _stream(&_device) {
                        _device.setOnNewLine(std::move(func));
                        _device.open(IODevice::WriteOnly);
                }

                /** @brief Deleted copy constructor. */
                StreamString(const StreamString &) = delete;
                /** @brief Deleted copy assignment operator. */
                StreamString &operator=(const StreamString &) = delete;
                /** @brief Deleted move constructor. */
                StreamString(StreamString &&) = delete;
                /** @brief Deleted move assignment operator. */
                StreamString &operator=(StreamString &&) = delete;

                /**
                 * @brief Returns the TextStream associated with this buffer.
                 * @return Reference to the text stream.
                 */
                TextStream &stream() { return _stream; }

                /**
                 * @brief Sets or replaces the newline callback.
                 * @param func New callback to invoke on each complete line.
                 */
                void setOnNewLine(OnNewLineFunc func) {
                        _device.setOnNewLine(std::move(func));
                        return;
                }

                /**
                 * @brief Returns the current (possibly incomplete) line buffer.
                 * @return Const reference to the accumulated line.
                 */
                const String &line() const { return _device.line(); }

                /** @brief Clears the accumulated line buffer. */
                void clear() {
                        _device.clearLine();
                        return;
                }

        private:
                StreamStringIODevice    _device;
                TextStream              _stream;
};

PROMEKI_NAMESPACE_END
