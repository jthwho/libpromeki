/**
 * @file      core/bufferiodevice.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/iodevice.h>
#include <promeki/core/buffer.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief IODevice backed by an in-memory Buffer.
 *
 * BufferIODevice provides seekable, random-access I/O over a promeki
 * Buffer. It is useful wherever an IODevice interface is expected but
 * the data lives in memory — for example, unit-testing code that
 * operates on IODevice, or wiring a DataStream to a memory buffer.
 *
 * @par Ownership
 * BufferIODevice does not own the Buffer; the caller must ensure the
 * Buffer outlives the device. If no external Buffer is provided, the
 * device uses an internal default-constructed Buffer (invalid/empty)
 * and all operations will fail until a valid Buffer is set.
 *
 * @par Write growth
 * When opened for writing, writes past the current logical size()
 * succeed as long as space remains in availSize(). The device
 * advances size() to track the high-water mark. Writes that would
 * exceed availSize() return an error.
 */
class BufferIODevice : public IODevice {
        PROMEKI_OBJECT(BufferIODevice, IODevice)
        public:
                /**
                 * @brief Constructs a BufferIODevice with an external Buffer.
                 * @param buffer Pointer to the Buffer to use. Must remain valid
                 *               for the lifetime of this device.
                 * @param parent The parent object, or nullptr.
                 */
                explicit BufferIODevice(Buffer *buffer, ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a BufferIODevice with no buffer.
                 *
                 * A buffer must be set via setBuffer() before the device
                 * can be opened.
                 * @param parent The parent object, or nullptr.
                 */
                explicit BufferIODevice(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~BufferIODevice() override;

                /**
                 * @brief Sets the underlying Buffer.
                 *
                 * The device must not be open when calling this.
                 * @param buffer Pointer to the Buffer to use.
                 */
                void setBuffer(Buffer *buffer);

                /**
                 * @brief Returns the underlying Buffer, or nullptr.
                 * @return The buffer pointer.
                 */
                Buffer *buffer() const { return _buffer; }

                Error open(OpenMode mode) override;
                Error close() override;
                bool isOpen() const override;
                int64_t read(void *data, int64_t maxSize) override;
                int64_t write(const void *data, int64_t maxSize) override;
                int64_t bytesAvailable() const override;
                bool isSequential() const override;
                Error seek(int64_t pos) override;
                int64_t pos() const override;
                Result<int64_t> size() const override;
                bool atEnd() const override;

        private:
                Buffer  *_buffer = nullptr;
                int64_t _pos     = 0;
};

PROMEKI_NAMESPACE_END
