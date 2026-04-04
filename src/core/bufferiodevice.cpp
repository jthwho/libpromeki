/**
 * @file      bufferiodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <algorithm>
#include <promeki/bufferiodevice.h>

PROMEKI_NAMESPACE_BEGIN

BufferIODevice::BufferIODevice(Buffer *buffer, ObjectBase *parent) :
        IODevice(parent), _buffer(buffer) { }

BufferIODevice::BufferIODevice(ObjectBase *parent) :
        IODevice(parent) { }

BufferIODevice::~BufferIODevice() {
        if(isOpen()) close();
}

void BufferIODevice::setBuffer(Buffer *buffer) {
        _buffer = buffer;
        return;
}

Error BufferIODevice::open(OpenMode mode) {
        if(isOpen()) return Error(Error::AlreadyOpen);
        if(_buffer == nullptr || !_buffer->isValid()) return Error(Error::Invalid);
        setOpenMode(mode);
        _pos = 0;
        return Error();
}

Error BufferIODevice::close() {
        if(!isOpen()) return Error(Error::NotOpen);
        aboutToCloseSignal.emit();
        setOpenMode(NotOpen);
        _pos = 0;
        return Error();
}

bool BufferIODevice::isOpen() const {
        return openMode() != NotOpen;
}

int64_t BufferIODevice::read(void *data, int64_t maxSize) {
        if(!isOpen() || !isReadable()) return -1;
        int64_t avail = static_cast<int64_t>(_buffer->size()) - _pos;
        if(avail <= 0) return 0;
        int64_t toRead = std::min(maxSize, avail);
        std::memcpy(data, static_cast<const uint8_t *>(_buffer->data()) + _pos,
                static_cast<size_t>(toRead));
        _pos += toRead;
        return toRead;
}

int64_t BufferIODevice::write(const void *data, int64_t maxSize) {
        if(!isOpen() || !isWritable()) return -1;
        int64_t endPos = _pos + maxSize;
        if(endPos > static_cast<int64_t>(_buffer->availSize())) {
                setError(Error(Error::BufferTooSmall));
                return -1;
        }
        std::memcpy(static_cast<uint8_t *>(_buffer->data()) + _pos, data,
                static_cast<size_t>(maxSize));
        _pos += maxSize;
        if(static_cast<size_t>(_pos) > _buffer->size()) {
                _buffer->setSize(static_cast<size_t>(_pos));
        }
        bytesWrittenSignal.emit(maxSize);
        return maxSize;
}

int64_t BufferIODevice::bytesAvailable() const {
        if(!isOpen()) return 0;
        int64_t avail = static_cast<int64_t>(_buffer->size()) - _pos;
        return avail > 0 ? avail : 0;
}

bool BufferIODevice::isSequential() const {
        return false;
}

Error BufferIODevice::seek(int64_t pos) {
        if(!isOpen()) return Error(Error::NotOpen);
        if(pos < 0) return Error(Error::OutOfRange);
        // Allow seeking up to availSize for writers, up to size for readers
        int64_t limit = isWritable()
                ? static_cast<int64_t>(_buffer->availSize())
                : static_cast<int64_t>(_buffer->size());
        if(pos > limit) return Error(Error::OutOfRange);
        _pos = pos;
        return Error();
}

int64_t BufferIODevice::pos() const {
        return _pos;
}

Result<int64_t> BufferIODevice::size() const {
        if(_buffer == nullptr) return makeError<int64_t>(Error(Error::Invalid));
        return makeResult(static_cast<int64_t>(_buffer->size()));
}

bool BufferIODevice::atEnd() const {
        if(!isOpen()) return true;
        return _pos >= static_cast<int64_t>(_buffer->size());
}

PROMEKI_NAMESPACE_END
