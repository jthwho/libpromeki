/**
 * @file      textstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstring>
#include <promeki/core/textstream.h>
#include <promeki/core/iodevice.h>
#include <promeki/core/buffer.h>
#include <promeki/core/bufferiodevice.h>
#include <promeki/core/stringiodevice.h>
#include <promeki/core/fileiodevice.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Constructors / Destructor
// ============================================================================

TextStream::TextStream(IODevice *device) : _device(device) { }

TextStream::TextStream(Buffer *buffer) {
        auto *dev = new BufferIODevice(buffer);
        dev->open(IODevice::ReadWrite);
        _ownedDevice = dev;
        _device = dev;
}

TextStream::TextStream(String *string) {
        auto *dev = new StringIODevice(string);
        dev->open(IODevice::ReadWrite);
        _ownedDevice = dev;
        _device = dev;
}

TextStream::TextStream(FILE *file) {
        auto *dev = new FileIODevice(file, IODevice::ReadWrite);
        _ownedDevice = dev;
        _device = dev;
}

TextStream::~TextStream() {
        delete _ownedDevice;
}

// ============================================================================
// Status
// ============================================================================

bool TextStream::atEnd() const {
        if(_device != nullptr) return _device->atEnd();
        return true;
}

void TextStream::flush() {
        if(_device) _device->flush();
}

// ============================================================================
// Encoding
// ============================================================================

void TextStream::setEncoding(const String &encoding) {
        _encoding = encoding;
}

// ============================================================================
// Internal: write a string to the underlying device
// ============================================================================

void TextStream::writeString(const String &str) {
        if(_status != Ok) return;
        if(str.isEmpty()) return;
        if(_device == nullptr) {
                _status = WriteFailed;
                return;
        }
        const char *data = str.cstr();
        size_t len = str.byteCount();
        size_t total = 0;
        const uint8_t *src = reinterpret_cast<const uint8_t *>(data);
        while(total < len) {
                int64_t n = _device->write(src + total, static_cast<int64_t>(len - total));
                if(n <= 0) {
                        _status = WriteFailed;
                        return;
                }
                total += static_cast<size_t>(n);
        }
}

// ============================================================================
// Internal: apply field-width padding
// ============================================================================

String TextStream::applyPadding(const String &str) {
        int width = _fieldWidth;
        if(width <= 0) return str;
        int strLen = static_cast<int>(str.length());
        if(strLen >= width) return str;
        int pad = width - strLen;
        String padStr(static_cast<size_t>(pad), _padChar);
        switch(_fieldAlignment) {
                case Left:
                        return str + padStr;
                case Right:
                        return padStr + str;
                case Center: {
                        int leftPad = pad / 2;
                        int rightPad = pad - leftPad;
                        return String(static_cast<size_t>(leftPad), _padChar) +
                               str +
                               String(static_cast<size_t>(rightPad), _padChar);
                }
        }
        return str;
}

// ============================================================================
// Internal: read helpers
// ============================================================================

bool TextStream::readChar(char &ch) {
        if(_status != Ok) return false;
        if(_device == nullptr) {
                _status = ReadPastEnd;
                return false;
        }
        int64_t n = _device->read(&ch, 1);
        if(n <= 0) {
                _status = ReadPastEnd;
                return false;
        }
        return true;
}

void TextStream::unreadChar(char ch) {
        (void)ch;
        if(_device != nullptr && !_device->isSequential()) {
                _device->seek(_device->pos() - 1);
        }
        if(_status == ReadPastEnd) _status = Ok;
}

void TextStream::skipWhitespace() {
        while(!atEnd()) {
                char ch;
                if(!readChar(ch)) return;
                if(ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                        unreadChar(ch);
                        return;
                }
        }
}

String TextStream::readToken() {
        skipWhitespace();
        std::string result;
        while(!atEnd()) {
                char ch;
                if(!readChar(ch)) break;
                if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                        unreadChar(ch);
                        break;
                }
                result += ch;
        }
        if(!result.empty() && _status == ReadPastEnd) _status = Ok;
        return String(result.c_str(), result.size());
}

// ============================================================================
// Write operators
// ============================================================================

TextStream &TextStream::operator<<(const String &val) {
        writeString(applyPadding(val));
        return *this;
}

TextStream &TextStream::operator<<(const char *val) {
        if(val == nullptr) return *this;
        writeString(applyPadding(String(val)));
        return *this;
}

TextStream &TextStream::operator<<(char val) {
        writeString(applyPadding(String(1, val)));
        return *this;
}

TextStream &TextStream::operator<<(int val) {
        char buf[64];
        switch(_integerBase) {
                case 16: std::snprintf(buf, sizeof(buf), "%X", val); break;
                case 8:  std::snprintf(buf, sizeof(buf), "%o", val); break;
                case 2: {
                        unsigned int uval = static_cast<unsigned int>(val);
                        char *p = buf + sizeof(buf) - 1;
                        *p = '\0';
                        if(uval == 0) { *(--p) = '0'; }
                        else { while(uval > 0) { *(--p) = '0' + (uval & 1); uval >>= 1; } }
                        writeString(applyPadding(String(p)));
                        return *this;
                }
                default: std::snprintf(buf, sizeof(buf), "%d", val); break;
        }
        writeString(applyPadding(String(buf)));
        return *this;
}

TextStream &TextStream::operator<<(unsigned int val) {
        char buf[64];
        switch(_integerBase) {
                case 16: std::snprintf(buf, sizeof(buf), "%X", val); break;
                case 8:  std::snprintf(buf, sizeof(buf), "%o", val); break;
                case 2: {
                        char *p = buf + sizeof(buf) - 1;
                        *p = '\0';
                        if(val == 0) { *(--p) = '0'; }
                        else { while(val > 0) { *(--p) = '0' + (val & 1); val >>= 1; } }
                        writeString(applyPadding(String(p)));
                        return *this;
                }
                default: std::snprintf(buf, sizeof(buf), "%u", val); break;
        }
        writeString(applyPadding(String(buf)));
        return *this;
}

TextStream &TextStream::operator<<(int64_t val) {
        char buf[64];
        switch(_integerBase) {
                case 16: std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(val)); break;
                case 8:  std::snprintf(buf, sizeof(buf), "%llo", static_cast<unsigned long long>(val)); break;
                case 2: {
                        uint64_t uval = static_cast<uint64_t>(val);
                        char *p = buf + sizeof(buf) - 1;
                        *p = '\0';
                        if(uval == 0) { *(--p) = '0'; }
                        else { while(uval > 0) { *(--p) = '0' + (uval & 1); uval >>= 1; } }
                        writeString(applyPadding(String(p)));
                        return *this;
                }
                default: std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(val)); break;
        }
        writeString(applyPadding(String(buf)));
        return *this;
}

TextStream &TextStream::operator<<(uint64_t val) {
        char buf[64];
        switch(_integerBase) {
                case 16: std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(val)); break;
                case 8:  std::snprintf(buf, sizeof(buf), "%llo", static_cast<unsigned long long>(val)); break;
                case 2: {
                        char *p = buf + sizeof(buf) - 1;
                        *p = '\0';
                        if(val == 0) { *(--p) = '0'; }
                        else { while(val > 0) { *(--p) = '0' + (val & 1); val >>= 1; } }
                        writeString(applyPadding(String(p)));
                        return *this;
                }
                default: std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(val)); break;
        }
        writeString(applyPadding(String(buf)));
        return *this;
}

TextStream &TextStream::operator<<(float val) {
        String str;
        switch(_realNumberNotation) {
                case Fixed:
                        str = String::number(static_cast<double>(val), _realNumberPrecision);
                        break;
                case Scientific: {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.*e", _realNumberPrecision, static_cast<double>(val));
                        str = String(buf);
                        break;
                }
                case SmartNotation:
                default: {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.*g", _realNumberPrecision, static_cast<double>(val));
                        str = String(buf);
                        break;
                }
        }
        writeString(applyPadding(str));
        return *this;
}

TextStream &TextStream::operator<<(double val) {
        String str;
        switch(_realNumberNotation) {
                case Fixed:
                        str = String::number(val, _realNumberPrecision);
                        break;
                case Scientific: {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.*e", _realNumberPrecision, val);
                        str = String(buf);
                        break;
                }
                case SmartNotation:
                default: {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.*g", _realNumberPrecision, val);
                        str = String(buf);
                        break;
                }
        }
        writeString(applyPadding(str));
        return *this;
}

TextStream &TextStream::operator<<(bool val) {
        writeString(applyPadding(String(val ? "true" : "false")));
        return *this;
}

TextStream &TextStream::operator<<(const Variant &val) {
        writeString(applyPadding(val.get<String>()));
        return *this;
}

TextStream &TextStream::operator<<(TextStream &(*manip)(TextStream &)) {
        return manip(*this);
}

// ============================================================================
// Read operators
// ============================================================================

TextStream &TextStream::operator>>(String &val) {
        val = readToken();
        return *this;
}

TextStream &TextStream::operator>>(char &val) {
        readChar(val);
        return *this;
}

TextStream &TextStream::operator>>(int &val) {
        String token = readToken();
        if(token.isEmpty()) {
                val = 0;
                return *this;
        }
        val = token.toInt();
        return *this;
}

TextStream &TextStream::operator>>(int64_t &val) {
        String token = readToken();
        if(token.isEmpty()) {
                val = 0;
                return *this;
        }
        val = static_cast<int64_t>(token.toInt());
        return *this;
}

TextStream &TextStream::operator>>(double &val) {
        String token = readToken();
        if(token.isEmpty()) {
                val = 0.0;
                return *this;
        }
        val = token.toDouble();
        return *this;
}

// ============================================================================
// Read methods
// ============================================================================

String TextStream::readLine() {
        std::string result;
        char ch;
        while(readChar(ch)) {
                if(ch == '\n') break;
                if(ch == '\r') {
                        // Handle \r\n
                        char next;
                        if(readChar(next)) {
                                if(next != '\n') {
                                        unreadChar(next);
                                }
                        } else {
                                if(_status == ReadPastEnd) _status = Ok;
                        }
                        break;
                }
                result += ch;
        }
        if(!result.empty() && _status == ReadPastEnd) _status = Ok;
        return String(result.c_str(), result.size());
}

String TextStream::readAll() {
        std::string result;
        char ch;
        while(readChar(ch)) {
                result += ch;
        }
        if(_status == ReadPastEnd) _status = Ok;
        return String(result.c_str(), result.size());
}

String TextStream::read(size_t maxLength) {
        std::string result;
        result.reserve(maxLength);
        char ch;
        for(size_t i = 0; i < maxLength; ++i) {
                if(!readChar(ch)) break;
                result += ch;
        }
        if(!result.empty() && _status == ReadPastEnd) _status = Ok;
        return String(result.c_str(), result.size());
}

PROMEKI_NAMESPACE_END
