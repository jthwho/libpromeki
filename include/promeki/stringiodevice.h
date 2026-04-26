/**
 * @file      stringiodevice.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/iodevice.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief IODevice backed by a promeki String.
 * @ingroup io
 *
 * StringIODevice provides seekable, random-access I/O over a String
 * pointer. It is useful wherever an IODevice interface is expected but
 * the data lives in a String — for example, wiring a TextStream to a
 * String for in-memory text formatting and parsing.
 *
 * @par Ownership
 * StringIODevice does not own the String; the caller must ensure the
 * String outlives the device.
 *
 * @par Write growth
 * When opened for writing, writes append to or overwrite the String
 * content at the current position. The String grows as needed.
 *
 * @par Thread Safety
 * Inherits @ref IODevice: thread-affine.  A single instance must
 * only be used from the thread that created it.
 */
class StringIODevice : public IODevice {
                PROMEKI_OBJECT(StringIODevice, IODevice)
        public:
                /**
                 * @brief Constructs a StringIODevice with an external String.
                 * @param string Pointer to the String to use. Must remain valid
                 *               for the lifetime of this device.
                 * @param parent The parent object, or nullptr.
                 */
                explicit StringIODevice(String *string, ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a StringIODevice with no string.
                 *
                 * A string must be set via setString() before the device
                 * can be opened.
                 * @param parent The parent object, or nullptr.
                 */
                explicit StringIODevice(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~StringIODevice() override;

                /**
                 * @brief Sets the underlying String.
                 *
                 * The device must not be open when calling this.
                 * @param string Pointer to the String to use.
                 */
                void setString(String *string);

                /**
                 * @brief Returns the underlying String, or nullptr.
                 * @return The string pointer.
                 */
                String *string() const { return _string; }

                Error           open(OpenMode mode) override;
                Error           close() override;
                bool            isOpen() const override;
                int64_t         read(void *data, int64_t maxSize) override;
                int64_t         write(const void *data, int64_t maxSize) override;
                int64_t         bytesAvailable() const override;
                bool            isSequential() const override;
                Error           seek(int64_t pos) override;
                int64_t         pos() const override;
                Result<int64_t> size() const override;
                bool            atEnd() const override;

        private:
                String *_string = nullptr;
                int64_t _pos = 0;
};

PROMEKI_NAMESPACE_END
