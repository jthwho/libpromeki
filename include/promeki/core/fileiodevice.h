/**
 * @file      core/fileiodevice.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdio>
#include <promeki/core/namespace.h>
#include <promeki/core/iodevice.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief IODevice wrapping a C stdio FILE pointer.
 * @ingroup core_io
 *
 * FileIODevice adapts a FILE* to the IODevice interface. It supports
 * two usage patterns:
 *
 * @par External FILE (e.g. stdin, stdout, stderr)
 * Construct with a FILE* and OpenMode. The device is immediately open.
 * Ownership is controlled by the OwnsFile flag — when set, the FILE
 * is fclose'd on close() or destruction. Use takeFile() to reclaim
 * ownership.
 * @code
 * FileIODevice dev(stdout, IODevice::WriteOnly);
 * dev << "hello";
 * @endcode
 *
 * @par Filename-based (fopen)
 * Set a filename via the constructor or setFilename(), then call
 * open(). The FILE is created internally and owned by the device.
 * @code
 * FileIODevice dev("/tmp/log.txt");
 * dev.open(IODevice::WriteOnly);
 * @endcode
 *
 * @par Sequential behavior
 * isSequential() returns true. For seekable files, seek() and pos()
 * delegate to fseek/ftell and will succeed if the underlying FILE
 * supports it.
 */
class FileIODevice : public IODevice {
        PROMEKI_OBJECT(FileIODevice, IODevice)
        public:
                /** @brief Flags for the FILE* constructor. */
                enum Flag {
                        NoFlags  = 0x00, ///< @brief Default: device does not own the FILE.
                        OwnsFile = 0x01  ///< @brief Device will fclose the FILE on close/destruct.
                };

                /**
                 * @brief Returns a singleton FileIODevice wrapping C stdin.
                 *
                 * The device is a lazy-initialized static local, guaranteed
                 * to outlive any other static object that calls this function.
                 * The FILE pointer is not owned (not fclose'd).
                 *
                 * @return A non-owning FileIODevice for stdin.
                 */
                static FileIODevice *stdinDevice();

                /**
                 * @brief Returns a singleton FileIODevice wrapping C stdout.
                 *
                 * The device is a lazy-initialized static local, guaranteed
                 * to outlive any other static object that calls this function.
                 * The FILE pointer is not owned (not fclose'd).
                 *
                 * @return A non-owning FileIODevice for stdout.
                 */
                static FileIODevice *stdoutDevice();

                /**
                 * @brief Returns a singleton FileIODevice wrapping C stderr.
                 *
                 * The device is a lazy-initialized static local, guaranteed
                 * to outlive any other static object that calls this function.
                 * The FILE pointer is not owned (not fclose'd).
                 *
                 * @return A non-owning FileIODevice for stderr.
                 */
                static FileIODevice *stderrDevice();

                /**
                 * @brief Constructs a FileIODevice wrapping an external FILE.
                 *
                 * The device is immediately open with the given mode. If
                 * OwnsFile is set, the FILE will be fclose'd on close()
                 * or destruction.
                 *
                 * @param file  The FILE pointer to wrap.
                 * @param mode  The open mode (ReadOnly, WriteOnly, or ReadWrite).
                 * @param flags Ownership flags.
                 * @param parent The parent object, or nullptr.
                 */
                FileIODevice(FILE *file, OpenMode mode, int flags = NoFlags,
                             ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a FileIODevice with a filename.
                 *
                 * The device is not open until open() is called. The FILE
                 * is created via fopen and owned by the device.
                 *
                 * @param filename The path to the file.
                 * @param parent The parent object, or nullptr.
                 */
                explicit FileIODevice(const String &filename, ObjectBase *parent = nullptr);

                /**
                 * @brief Constructs a FileIODevice with no FILE or filename.
                 *
                 * Set a filename via setFilename() or provide a FILE via
                 * the other constructor before opening.
                 *
                 * @param parent The parent object, or nullptr.
                 */
                explicit FileIODevice(ObjectBase *parent = nullptr);

                /** @brief Destructor. Closes and fclose's if owned. */
                ~FileIODevice() override;

                /** @brief Deleted copy constructor (non-copyable). */
                FileIODevice(const FileIODevice &) = delete;
                /** @brief Deleted copy assignment (non-copyable). */
                FileIODevice &operator=(const FileIODevice &) = delete;

                /**
                 * @brief Sets the filename for fopen-based opening.
                 *
                 * The device must not be open when calling this.
                 *
                 * @param filename The path to the file.
                 */
                void setFilename(const String &filename);

                /**
                 * @brief Returns the filename, or an empty string if none was set.
                 * @return The filename.
                 */
                const String &filename() const { return _filename; }

                /**
                 * @brief Returns the underlying FILE pointer, or nullptr.
                 * @return The FILE pointer.
                 */
                FILE *file() const { return _file; }

                /**
                 * @brief Returns true if the device owns the FILE pointer.
                 * @return True if the device will fclose the FILE.
                 */
                bool ownsFile() const { return _ownsFile; }

                /**
                 * @brief Transfers ownership of the FILE pointer to the caller.
                 *
                 * After this call, the device is closed and the FILE is no
                 * longer managed by the device. The caller is responsible
                 * for fclose'ing the returned FILE.
                 *
                 * @return The FILE pointer, or nullptr if the device has no FILE.
                 */
                FILE *takeFile();

                /**
                 * @brief Opens the device.
                 *
                 * If a filename is set, the FILE is created via fopen.
                 * If a FILE was provided at construction, this returns
                 * AlreadyOpen (the device is already open).
                 *
                 * @param mode The open mode.
                 * @return Error::Ok on success, or an error on failure.
                 */
                Error open(OpenMode mode) override;

                Error close() override;
                void flush() override;
                bool isOpen() const override;
                int64_t read(void *data, int64_t maxSize) override;
                int64_t write(const void *data, int64_t maxSize) override;
                bool isSequential() const override;
                Error seek(int64_t pos) override;
                int64_t pos() const override;
                bool atEnd() const override;

        private:
                FILE    *_file     = nullptr;
                String  _filename;
                bool    _ownsFile  = false;
};

PROMEKI_NAMESPACE_END
