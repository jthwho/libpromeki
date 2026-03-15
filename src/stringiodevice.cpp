/**
 * @file      stringiodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <algorithm>
#include <promeki/core/stringiodevice.h>

PROMEKI_NAMESPACE_BEGIN

StringIODevice::StringIODevice(String *string, ObjectBase *parent) :
        IODevice(parent), _string(string) { }

StringIODevice::StringIODevice(ObjectBase *parent) :
        IODevice(parent) { }

StringIODevice::~StringIODevice() {
        if(isOpen()) close();
}

void StringIODevice::setString(String *string) {
        _string = string;
        return;
}

Error StringIODevice::open(OpenMode mode) {
        if(isOpen()) return Error(Error::AlreadyOpen);
        if(_string == nullptr) return Error(Error::Invalid);
        setOpenMode(mode);
        _pos = 0;
        return Error();
}

Error StringIODevice::close() {
        if(!isOpen()) return Error(Error::NotOpen);
        aboutToCloseSignal.emit();
        setOpenMode(NotOpen);
        _pos = 0;
        return Error();
}

bool StringIODevice::isOpen() const {
        return openMode() != NotOpen;
}

int64_t StringIODevice::read(void *data, int64_t maxSize) {
        if(!isOpen() || !isReadable()) return -1;
        int64_t avail = static_cast<int64_t>(_string->byteCount()) - _pos;
        if(avail <= 0) return 0;
        int64_t toRead = std::min(maxSize, avail);
        std::memcpy(data, _string->cstr() + _pos, static_cast<size_t>(toRead));
        _pos += toRead;
        return toRead;
}

int64_t StringIODevice::write(const void *data, int64_t maxSize) {
        if(!isOpen() || !isWritable()) return -1;
        if(maxSize <= 0) return 0;
        const char *src = static_cast<const char *>(data);
        int64_t strLen = static_cast<int64_t>(_string->byteCount());
        if(_pos == strLen) {
                // Append
                *_string += String(src, static_cast<size_t>(maxSize));
        } else if(_pos < strLen) {
                // Overwrite from _pos, potentially extending
                // Build the result: prefix + new data + suffix (if any)
                std::string s(_string->cstr(), _string->byteCount());
                size_t pos = static_cast<size_t>(_pos);
                size_t count = static_cast<size_t>(maxSize);
                if(pos + count > s.size()) {
                        s.resize(pos + count);
                }
                std::memcpy(s.data() + pos, src, count);
                *_string = String(s.c_str(), s.size());
        } else {
                // Past end — pad with spaces, then append
                size_t gap = static_cast<size_t>(_pos - strLen);
                std::string pad(gap, ' ');
                *_string += String(pad.c_str(), gap);
                *_string += String(src, static_cast<size_t>(maxSize));
        }
        _pos += maxSize;
        bytesWrittenSignal.emit(maxSize);
        return maxSize;
}

int64_t StringIODevice::bytesAvailable() const {
        if(!isOpen()) return 0;
        int64_t avail = static_cast<int64_t>(_string->byteCount()) - _pos;
        return avail > 0 ? avail : 0;
}

bool StringIODevice::isSequential() const {
        return false;
}

Error StringIODevice::seek(int64_t pos) {
        if(!isOpen()) return Error(Error::NotOpen);
        if(pos < 0) return Error(Error::OutOfRange);
        _pos = pos;
        return Error();
}

int64_t StringIODevice::pos() const {
        return _pos;
}

Result<int64_t> StringIODevice::size() const {
        if(_string == nullptr) return makeError<int64_t>(Error(Error::Invalid));
        return makeResult(static_cast<int64_t>(_string->byteCount()));
}

bool StringIODevice::atEnd() const {
        if(!isOpen()) return true;
        return _pos >= static_cast<int64_t>(_string->byteCount());
}

PROMEKI_NAMESPACE_END
