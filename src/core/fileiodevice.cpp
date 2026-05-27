/**
 * @file      fileiodevice.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/fileiodevice.h>
#include <promeki/logger.h>
#include <promeki/resource.h>

#include <cirf/runtime.h>

#include <cerrno>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Static stdio singletons
// ============================================================================

FileIODevice *FileIODevice::stdinDevice() {
        static FileIODevice dev(stdin, IODevice::ReadOnly);
        return &dev;
}

FileIODevice *FileIODevice::stdoutDevice() {
        static FileIODevice dev(stdout, IODevice::WriteOnly);
        return &dev;
}

FileIODevice *FileIODevice::stderrDevice() {
        static FileIODevice dev(stderr, IODevice::WriteOnly);
        return &dev;
}

// ============================================================================
// Constructors / Destructor
// ============================================================================

FileIODevice::FileIODevice(FILE *file, OpenMode mode, int flags, ObjectBase *parent)
    : IODevice(parent), _file(file), _ownsFile(flags & OwnsFile) {
        setOpenMode(mode);
}

FileIODevice::FileIODevice(const String &filename, ObjectBase *parent) : IODevice(parent), _filename(filename) {}

FileIODevice::FileIODevice(ObjectBase *parent) : IODevice(parent) {}

FileIODevice::~FileIODevice() {
        if (isOpen()) close();
}

// ============================================================================
// Configuration
// ============================================================================

void FileIODevice::setFilename(const String &filename) {
        _filename = filename;
        return;
}

// ============================================================================
// Ownership transfer
// ============================================================================

FILE *FileIODevice::takeFile() {
        FILE *f = _file;
        if (isOpen()) {
                aboutToCloseSignal.emit();
                setOpenMode(NotOpen);
        }
        _file = nullptr;
        _ownsFile = false;
        return f;
}

// ============================================================================
// Open / Close
// ============================================================================

Error FileIODevice::open(OpenMode mode) {
        if (isOpen()) {
                promekiWarn("FileIODevice::open('%s') refused: already open", _filename.cstr());
                return Error(Error::AlreadyOpen);
        }
        if (_filename.isEmpty()) {
                promekiWarn("FileIODevice::open() refused: filename is empty");
                return Error(Error::Invalid);
        }

        // Resource paths (":/...") are served from compiled-in cirf
        // data via cirf_fopen() which wraps the bytes in a memory
        // FILE* (POSIX fmemopen). Only ReadOnly is supported.
        if (Resource::isResourcePath(_filename)) {
                if (mode != ReadOnly) {
                        promekiWarn("FileIODevice::open('%s') refused: resource paths are ReadOnly (mode=%d)",
                                    _filename.cstr(), (int)mode);
                        return Error(Error::ReadOnly);
                }
                String virt = Resource::stripPrefix(_filename);
                _file = cirf_resolve_fopen(virt.cstr());
                if (_file == nullptr) {
                        promekiWarn("FileIODevice::open('%s') failed: resource not found (virt='%s')",
                                    _filename.cstr(), virt.cstr());
                        return Error(Error::NotExist);
                }
                _ownsFile = true;
                setOpenMode(mode);
                return Error();
        }

        const char *fmode = nullptr;
        switch (mode) {
                case ReadOnly: fmode = "rb"; break;
                case WriteOnly: fmode = "wb"; break;
                case ReadWrite: fmode = "w+b"; break;
                case Append: fmode = "ab"; break;
                default:
                        promekiWarn("FileIODevice::open('%s') refused: invalid OpenMode %d",
                                    _filename.cstr(), (int)mode);
                        return Error(Error::Invalid);
        }

        _file = std::fopen(_filename.cstr(), fmode);
        if (_file == nullptr) {
                int e = errno;
                promekiWarn("FileIODevice::open('%s', '%s') failed: %s (errno=%d)",
                            _filename.cstr(), fmode, std::strerror(e), e);
                return Error(Error::IOError);
        }
        _ownsFile = true;
        setOpenMode(mode);
        return Error();
}

Error FileIODevice::close() {
        if (!isOpen()) return Error(Error::NotOpen);
        aboutToCloseSignal.emit();
        setOpenMode(NotOpen);
        Error ret;
        if (_ownsFile && _file != nullptr) {
                if (std::fclose(_file) != 0) {
                        int e = errno;
                        promekiWarn("FileIODevice::close('%s') fclose failed: %s (errno=%d)",
                                    _filename.cstr(), std::strerror(e), e);
                        ret = Error(Error::IOError);
                }
        }
        _file = nullptr;
        _ownsFile = false;
        return ret;
}

// ============================================================================
// Flush
// ============================================================================

void FileIODevice::flush() {
        if (_file != nullptr) std::fflush(_file);
}

// ============================================================================
// State
// ============================================================================

bool FileIODevice::isOpen() const {
        return openMode() != NotOpen;
}

bool FileIODevice::isSequential() const {
        return true;
}

bool FileIODevice::atEnd() const {
        if (!isOpen() || _file == nullptr) return true;
        return std::feof(_file) != 0;
}

// ============================================================================
// Seek / Pos
// ============================================================================

Error FileIODevice::seek(int64_t pos) {
        if (!isOpen() || _file == nullptr) {
                promekiWarn("FileIODevice::seek('%s', %lld) refused: not open",
                            _filename.cstr(), (long long)pos);
                return Error(Error::NotOpen);
        }
        if (std::fseek(_file, static_cast<long>(pos), SEEK_SET) != 0) {
                int e = errno;
                promekiWarn("FileIODevice::seek('%s', %lld) failed: %s (errno=%d)",
                            _filename.cstr(), (long long)pos, std::strerror(e), e);
                return Error(Error::IOError);
        }
        return Error();
}

int64_t FileIODevice::pos() const {
        if (!isOpen() || _file == nullptr) return 0;
        long p = std::ftell(_file);
        return p < 0 ? 0 : static_cast<int64_t>(p);
}

// ============================================================================
// Read / Write
// ============================================================================

int64_t FileIODevice::read(void *data, int64_t maxSize) {
        if (!isOpen() || !isReadable() || _file == nullptr) {
                promekiWarn("FileIODevice::read('%s', %lld) refused: not open or not readable",
                            _filename.cstr(), (long long)maxSize);
                return -1;
        }
        size_t n = std::fread(data, 1, static_cast<size_t>(maxSize), _file);
        if (n == 0 && std::ferror(_file)) {
                int e = errno;
                promekiWarnThrottled(1000, "FileIODevice::read('%s', %lld) failed: %s (errno=%d)",
                                     _filename.cstr(), (long long)maxSize, std::strerror(e), e);
                return -1;
        }
        return static_cast<int64_t>(n);
}

int64_t FileIODevice::write(const void *data, int64_t maxSize) {
        if (!isOpen() || !isWritable() || _file == nullptr) {
                promekiWarn("FileIODevice::write('%s', %lld) refused: not open or not writable",
                            _filename.cstr(), (long long)maxSize);
                return -1;
        }
        size_t n = std::fwrite(data, 1, static_cast<size_t>(maxSize), _file);
        if (n == 0 && std::ferror(_file)) {
                int e = errno;
                promekiWarnThrottled(1000, "FileIODevice::write('%s', %lld) failed: %s (errno=%d)",
                                     _filename.cstr(), (long long)maxSize, std::strerror(e), e);
                return -1;
        }
        bytesWrittenSignal.emit(static_cast<int64_t>(n));
        return static_cast<int64_t>(n);
}

PROMEKI_NAMESPACE_END
