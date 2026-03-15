/**
 * @file      bufferediodevice.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <algorithm>
#include <promeki/core/bufferediodevice.h>

PROMEKI_NAMESPACE_BEGIN

BufferedIODevice::~BufferedIODevice() = default;

int64_t BufferedIODevice::deviceBytesAvailable() const {
        return 0;
}

void BufferedIODevice::ensureReadBuffer() {
        if(!_bufferAllocated) {
                if(!_readBuf.isValid()) {
                        _readBuf = Buffer(DefaultReadBufSize);
                }
                _bufferAllocated = true;
        }
        return;
}

void BufferedIODevice::resetReadBuffer() {
        _readBufPos = 0;
        _readBufFill = 0;
        return;
}

Error BufferedIODevice::setReadBuffer(Buffer &&buf) {
        if(isOpen()) return Error(Error::AlreadyOpen);
        if(!buf.isHostAccessible()) return Error(Error::NotHostAccessible);
        _readBuf = std::move(buf);
        _bufferAllocated = true;
        _readBufPos = 0;
        _readBufFill = 0;
        return Error();
}

int64_t BufferedIODevice::bytesAvailable() const {
        int64_t buffered = static_cast<int64_t>(_readBufFill - _readBufPos);
        return buffered + deviceBytesAvailable();
}

int64_t BufferedIODevice::fillBuffer() {
        if(!_readBuf.isValid()) return -1;
        compactBuffer();
        size_t capacity = _readBuf.size();
        size_t space = capacity - _readBufFill;
        if(space == 0) return 0;
        uint8_t *bufData = static_cast<uint8_t *>(_readBuf.data());
        int64_t n = readFromDevice(bufData + _readBufFill, static_cast<int64_t>(space));
        if(n > 0) _readBufFill += static_cast<size_t>(n);
        return n;
}

void BufferedIODevice::compactBuffer() {
        if(_readBufPos > _readBufFill / 2 && _readBufPos > 0) {
                size_t remaining = _readBufFill - _readBufPos;
                if(remaining > 0) {
                        uint8_t *bufData = static_cast<uint8_t *>(_readBuf.data());
                        std::memmove(bufData, bufData + _readBufPos, remaining);
                }
                _readBufFill = remaining;
                _readBufPos = 0;
        }
        return;
}

int64_t BufferedIODevice::read(void *data, int64_t maxSize) {
        if(!isOpen() || !isReadable()) return -1;
        if(maxSize <= 0) return 0;

        ensureReadBuffer();

        // Large reads bypass the buffer entirely
        size_t capacity = _readBuf.size();
        if(static_cast<size_t>(maxSize) >= capacity) {
                // First drain any buffered data
                size_t buffered = _readBufFill - _readBufPos;
                int64_t total = 0;
                if(buffered > 0) {
                        uint8_t *bufData = static_cast<uint8_t *>(_readBuf.data());
                        std::memcpy(data, bufData + _readBufPos, buffered);
                        _readBufPos = 0;
                        _readBufFill = 0;
                        total = static_cast<int64_t>(buffered);
                }
                int64_t n = readFromDevice(
                        static_cast<uint8_t *>(data) + total,
                        maxSize - total
                );
                if(n > 0) total += n;
                return total > 0 ? total : n;
        }

        // Serve from buffer
        uint8_t *dst = static_cast<uint8_t *>(data);
        int64_t total = 0;

        while(total < maxSize) {
                size_t buffered = _readBufFill - _readBufPos;
                if(buffered > 0) {
                        size_t toRead = std::min(
                                buffered,
                                static_cast<size_t>(maxSize - total)
                        );
                        uint8_t *bufData = static_cast<uint8_t *>(_readBuf.data());
                        std::memcpy(dst + total, bufData + _readBufPos, toRead);
                        _readBufPos += toRead;
                        total += static_cast<int64_t>(toRead);
                } else {
                        int64_t n = fillBuffer();
                        if(n <= 0) break;
                }
        }

        return total > 0 ? total : 0;
}

Buffer BufferedIODevice::readLine(size_t maxLength) {
        if(!isOpen() || !isReadable()) return Buffer();

        ensureReadBuffer();

        // Collect bytes into a temporary vector until newline or maxLength
        std::vector<uint8_t> result;
        while(maxLength == 0 || result.size() < maxLength) {
                // Ensure we have buffered data
                size_t buffered = _readBufFill - _readBufPos;
                if(buffered == 0) {
                        int64_t n = fillBuffer();
                        if(n <= 0) break;
                        buffered = _readBufFill - _readBufPos;
                }

                uint8_t *bufData = static_cast<uint8_t *>(_readBuf.data());
                uint8_t *start = bufData + _readBufPos;
                size_t searchLen = buffered;
                if(maxLength > 0 && result.size() + searchLen > maxLength) {
                        searchLen = maxLength - result.size();
                }

                // Search for newline
                uint8_t *nl = static_cast<uint8_t *>(
                        std::memchr(start, '\n', searchLen)
                );
                if(nl != nullptr) {
                        size_t lineLen = static_cast<size_t>(nl - start) + 1;
                        result.insert(result.end(), start, start + lineLen);
                        _readBufPos += lineLen;
                        break;
                }

                // No newline found, consume all searched bytes
                result.insert(result.end(), start, start + searchLen);
                _readBufPos += searchLen;
        }

        if(result.empty()) return Buffer();
        Buffer buf(result.size());
        std::memcpy(buf.data(), result.data(), result.size());
        return buf;
}

Buffer BufferedIODevice::readAll() {
        if(!isOpen() || !isReadable()) return Buffer();

        ensureReadBuffer();

        std::vector<uint8_t> result;

        // Drain buffered data first
        size_t buffered = _readBufFill - _readBufPos;
        if(buffered > 0) {
                uint8_t *bufData = static_cast<uint8_t *>(_readBuf.data());
                result.insert(result.end(), bufData + _readBufPos, bufData + _readBufFill);
                _readBufPos = 0;
                _readBufFill = 0;
        }

        // Read remaining data from device
        size_t capacity = _readBuf.size();
        std::vector<uint8_t> tmp(capacity);
        for(;;) {
                int64_t n = readFromDevice(tmp.data(), static_cast<int64_t>(capacity));
                if(n <= 0) break;
                result.insert(result.end(), tmp.data(), tmp.data() + n);
        }

        if(result.empty()) return Buffer();
        Buffer buf(result.size());
        std::memcpy(buf.data(), result.data(), result.size());
        return buf;
}

Buffer BufferedIODevice::readBytes(size_t maxBytes) {
        if(maxBytes == 0) return Buffer();
        Buffer buf(maxBytes);
        int64_t n = read(buf.data(), static_cast<int64_t>(maxBytes));
        if(n <= 0) return Buffer();
        if(static_cast<size_t>(n) < maxBytes) {
                // Return a correctly-sized buffer
                Buffer result(static_cast<size_t>(n));
                std::memcpy(result.data(), buf.data(), static_cast<size_t>(n));
                return result;
        }
        return buf;
}

bool BufferedIODevice::canReadLine() const {
        if(!isOpen() || !isReadable()) return false;
        size_t buffered = _readBufFill - _readBufPos;
        if(buffered == 0) return false;
        const uint8_t *bufData = static_cast<const uint8_t *>(_readBuf.data());
        return std::memchr(bufData + _readBufPos, '\n', buffered) != nullptr;
}

int64_t BufferedIODevice::peek(void *buf, size_t maxBytes) const {
        if(!isOpen() || !isReadable()) return -1;
        size_t buffered = _readBufFill - _readBufPos;
        size_t toPeek = std::min(buffered, maxBytes);
        if(toPeek == 0) return 0;
        const uint8_t *bufData = static_cast<const uint8_t *>(_readBuf.data());
        std::memcpy(buf, bufData + _readBufPos, toPeek);
        return static_cast<int64_t>(toPeek);
}

Buffer BufferedIODevice::peek(size_t maxBytes) const {
        if(!isOpen() || !isReadable()) return Buffer();
        size_t buffered = _readBufFill - _readBufPos;
        size_t toPeek = std::min(buffered, maxBytes);
        if(toPeek == 0) return Buffer();
        Buffer result(toPeek);
        const uint8_t *bufData = static_cast<const uint8_t *>(_readBuf.data());
        std::memcpy(result.data(), bufData + _readBufPos, toPeek);
        return result;
}

PROMEKI_NAMESPACE_END
