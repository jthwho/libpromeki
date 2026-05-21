/**
 * @file      datastream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/datastream.h>
#include <promeki/datatype.h>
#include <promeki/logger.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Factory methods
// ============================================================================

DataStream DataStream::createWriter(IODevice *device, ByteOrder order) {
        DataStream ds(device);
        ds._version = CurrentVersion;
        ds._byteOrder = order;
        ds.writeHeader();
        return ds;
}

DataStream DataStream::createReader(IODevice *device) {
        DataStream ds(device);
        ds.readHeader();
        return ds;
}

// ============================================================================
// Constructor
// ============================================================================

DataStream::DataStream(IODevice *device) : _device(device) {
        // First-construction trigger for the library's built-in DataType
        // registry — guaranteed to be a cheap no-op after the first call
        // thanks to the static guard inside.  Either DataStream or
        // Variant being constructed populates the registry; whichever
        // fires first wins.
        registerBuiltinDataTypes();
}

// ============================================================================
// Header
// ============================================================================

void DataStream::writeHeader() {
        // 16-byte fixed header: 4 magic + 2 version + 1 byte-order + 9 reserved
        uint8_t buf[HeaderSize] = {};
        std::memcpy(buf, Magic, sizeof(Magic));
        // Version is always written big-endian regardless of stream byte order.
        buf[4] = static_cast<uint8_t>(_version >> 8);
        buf[5] = static_cast<uint8_t>(_version & 0xFF);
        // Byte-order marker: 'B' for big-endian, 'L' for little-endian.
        buf[6] = static_cast<uint8_t>(_byteOrder == BigEndian ? 'B' : 'L');
        writeBytes(buf, HeaderSize);
}

void DataStream::readHeader() {
        uint8_t buf[HeaderSize];
        if (!readBytes(buf, HeaderSize)) return;
        if (std::memcmp(buf, Magic, sizeof(Magic)) != 0) {
                setError(ReadCorruptData, String("bad magic bytes in header"));
                return;
        }
        _version = static_cast<uint16_t>((buf[4] << 8) | buf[5]);
        switch (buf[6]) {
                case 'B': _byteOrder = BigEndian; break;
                case 'L': _byteOrder = LittleEndian; break;
                default:
                        setError(ReadCorruptData, String::sprintf("invalid byte-order marker 0x%02X in header",
                                                                  static_cast<unsigned>(buf[6])));
                        return;
        }
        // Reserved bytes must be zero.
        for (size_t i = 7; i < HeaderSize; ++i) {
                if (buf[i] != 0) {
                        setError(ReadCorruptData, String::sprintf("non-zero reserved byte 0x%02X at header offset %zu",
                                                                  static_cast<unsigned>(buf[i]), i));
                        return;
                }
        }
}

// ============================================================================
// Status
// ============================================================================

void DataStream::setError(Status s, String ctx) {
        // First error wins.
        if (_status != Ok) return;
        _status = s;
        _errorContext = std::move(ctx);
        promekiWarn("DataStream error: status=%d ctx='%s'", (int)s, _errorContext.cstr());
}

void DataStream::resetStatus() {
        _status = Ok;
        _errorContext = String();
}

Error DataStream::toError() const {
        switch (_status) {
                case Ok: return Error::Ok;
                case ReadPastEnd: return Error::EndOfFile;
                case ReadCorruptData: return Error::CorruptData;
                case WriteFailed: return Error::IOError;
        }
        return Error::Invalid;
}

bool DataStream::atEnd() const {
        if (_device != nullptr) return _device->atEnd();
        return true;
}

// ============================================================================
// Frame header / framing API
// ============================================================================
//
// The per-value frame header is a fixed eight bytes:
//   [tag:uint16][version:uint16][size:uint32]
// all byte-order controlled.
//
// Writing is buffered: beginFrame pushes a body accumulator onto
// _frameStack, every subsequent writeBytes call routes into the top of
// that stack, and endFrame pops the accumulator and emits
// [tag][ver][size][body] in one go to either the parent frame or the
// underlying device.  The buffered design lets us emit the size field
// without seeking the device.

void DataStream::beginFrame(DataTypeID id, uint16_t version) {
        if (_status != Ok) return;
        PendingFrame frame;
        frame.tag = id;
        frame.version = version;
        frame.body.reserve(32);
        _frameStack.pushToBack(std::move(frame));
}

void DataStream::endFrame() {
        if (_frameStack.isEmpty()) {
                setError(WriteFailed, String("endFrame called with no open frame"));
                return;
        }
        PendingFrame frame = std::move(_frameStack.back());
        _frameStack.popFromBack();

        const size_t   bodyBytes = frame.body.size();
        const uint32_t bodySize = static_cast<uint32_t>(bodyBytes);
        if (bodyBytes > MaxFrameBodySize) {
                setError(WriteFailed,
                         String::sprintf("Frame body for tag 0x%04X is %zu bytes, "
                                         "exceeds MaxFrameBodySize (%u)",
                                         static_cast<unsigned>(frame.tag), bodyBytes,
                                         static_cast<unsigned>(MaxFrameBodySize)));
                return;
        }

        // Header layout: 2-byte tag, 2-byte version, 4-byte size — all byte-order controlled.
        uint8_t header[FrameHeaderSize];
        {
                uint16_t tag = static_cast<uint16_t>(frame.tag);
                if (_byteOrder != nativeByteOrder()) {
                        uint8_t *p = reinterpret_cast<uint8_t *>(&tag);
                        std::swap(p[0], p[1]);
                }
                std::memcpy(&header[0], &tag, sizeof(tag));
        }
        {
                uint16_t ver = frame.version;
                if (_byteOrder != nativeByteOrder()) {
                        uint8_t *p = reinterpret_cast<uint8_t *>(&ver);
                        std::swap(p[0], p[1]);
                }
                std::memcpy(&header[2], &ver, sizeof(ver));
        }
        {
                uint32_t sz = bodySize;
                if (_byteOrder != nativeByteOrder()) {
                        uint8_t *p = reinterpret_cast<uint8_t *>(&sz);
                        std::swap(p[0], p[3]);
                        std::swap(p[1], p[2]);
                }
                std::memcpy(&header[4], &sz, sizeof(sz));
        }

        writeBytes(header, FrameHeaderSize);
        if (bodyBytes > 0) writeBytes(frame.body.data(), bodyBytes);
}

bool DataStream::readFrameHeader(DataTypeID &outTag, uint16_t &outVersion, uint32_t &outSize) {
        if (_status != Ok) return false;
        if (_peekedHeaderValid) {
                outTag             = _peekedTag;
                outVersion         = _peekedVersion;
                outSize            = _peekedSize;
                _peekedHeaderValid = false;
                return true;
        }
        uint8_t header[FrameHeaderSize];
        if (!readBytes(header, FrameHeaderSize)) return false;
        uint16_t tagRaw = 0;
        std::memcpy(&tagRaw, &header[0], sizeof(tagRaw));
        if (_byteOrder != nativeByteOrder()) {
                uint8_t *p = reinterpret_cast<uint8_t *>(&tagRaw);
                std::swap(p[0], p[1]);
        }
        uint16_t verRaw = 0;
        std::memcpy(&verRaw, &header[2], sizeof(verRaw));
        if (_byteOrder != nativeByteOrder()) {
                uint8_t *p = reinterpret_cast<uint8_t *>(&verRaw);
                std::swap(p[0], p[1]);
        }
        uint32_t sz = 0;
        std::memcpy(&sz, &header[4], sizeof(sz));
        if (_byteOrder != nativeByteOrder()) {
                uint8_t *p = reinterpret_cast<uint8_t *>(&sz);
                std::swap(p[0], p[3]);
                std::swap(p[1], p[2]);
        }
        if (sz > MaxFrameBodySize) {
                setError(ReadCorruptData,
                         String::sprintf("Frame size %u exceeds MaxFrameBodySize (%u)",
                                         static_cast<unsigned>(sz),
                                         static_cast<unsigned>(MaxFrameBodySize)));
                return false;
        }
        outTag = static_cast<DataTypeID>(tagRaw);
        outVersion = verRaw;
        outSize = sz;
        return true;
}

bool DataStream::peekFrameHeader(DataTypeID &outTag, uint16_t &outVersion, uint32_t &outSize) {
        if (_peekedHeaderValid) {
                outTag     = _peekedTag;
                outVersion = _peekedVersion;
                outSize    = _peekedSize;
                return true;
        }
        if (!readFrameHeader(outTag, outVersion, outSize)) return false;
        _peekedTag         = outTag;
        _peekedVersion     = outVersion;
        _peekedSize        = outSize;
        _peekedHeaderValid = true;
        return true;
}

bool DataStream::readFrame(DataTypeID expected, uint16_t maxVersion, uint16_t *outVersion, uint32_t *outSize) {
        DataTypeID tag = DataTypeInvalid;
        uint16_t   ver = 0;
        uint32_t   sz = 0;
        if (!readFrameHeader(tag, ver, sz)) return false;
        if (tag != expected) {
                setError(ReadCorruptData,
                         String::sprintf("expected tag 0x%04X, got 0x%04X",
                                         static_cast<unsigned>(expected), static_cast<unsigned>(tag)));
                return false;
        }
        if (ver > maxVersion) {
                setError(ReadCorruptData,
                         String::sprintf("tag 0x%04X has version %u, reader knows up to %u",
                                         static_cast<unsigned>(expected), static_cast<unsigned>(ver),
                                         static_cast<unsigned>(maxVersion)));
                return false;
        }
        if (outVersion) *outVersion = ver;
        if (outSize) *outSize = sz;
        return true;
}

void DataStream::skipFrame() {
        DataTypeID tag = DataTypeInvalid;
        uint16_t   ver = 0;
        uint32_t   sz = 0;
        if (!readFrameHeader(tag, ver, sz)) return;
        skipFrameBody(sz);
}

bool DataStream::skipFrameBody(uint32_t sz) {
        if (sz == 0) return true;
        if (_device != nullptr && !_device->isSequential()) {
                Result<int64_t> total = _device->size();
                if (!error(total).isError()) {
                        const int64_t remaining = value(total) - _device->pos();
                        if (remaining < static_cast<int64_t>(sz)) {
                                setError(ReadPastEnd,
                                         String::sprintf("frame body of %u bytes exceeds "
                                                         "%lld remaining on device",
                                                         static_cast<unsigned>(sz),
                                                         static_cast<long long>(remaining)));
                                return false;
                        }
                }
        }
        ssize_t n = skipRawData(sz);
        if (n != static_cast<ssize_t>(sz)) {
                setError(ReadPastEnd,
                         String::sprintf("frame body skip: requested %u bytes, skipped %zd",
                                         static_cast<unsigned>(sz), n));
                return false;
        }
        return true;
}

// ============================================================================
// Internal read/write helpers
// ============================================================================

bool DataStream::readBytes(void *buf, size_t len) {
        if (_status != Ok) return false;
        if (len == 0) return true;
        if (_device == nullptr) {
                setError(ReadPastEnd, String("no device attached"));
                return false;
        }
        size_t   total = 0;
        uint8_t *dst = static_cast<uint8_t *>(buf);
        while (total < len) {
                int64_t n = _device->read(dst + total, static_cast<int64_t>(len - total));
                if (n <= 0) {
                        setError(ReadPastEnd, String::sprintf("short read: wanted %zu bytes, got %zu", len, total));
                        return false;
                }
                total += static_cast<size_t>(n);
        }
        return true;
}

bool DataStream::writeBytes(const void *buf, size_t len) {
        if (_status != Ok) return false;
        if (len == 0) return true;
        // When a frame is open, route bytes into its body accumulator.
        if (!_frameStack.isEmpty()) {
                List<uint8_t> &body = _frameStack.back().body;
                const uint8_t *src = static_cast<const uint8_t *>(buf);
                body.reserve(body.size() + len);
                for (size_t i = 0; i < len; ++i) body.pushToBack(src[i]);
                return true;
        }
        if (_device == nullptr) {
                setError(WriteFailed, String("no device attached"));
                return false;
        }
        size_t         total = 0;
        const uint8_t *src = static_cast<const uint8_t *>(buf);
        while (total < len) {
                int64_t n = _device->write(src + total, static_cast<int64_t>(len - total));
                if (n <= 0) {
                        setError(WriteFailed, String::sprintf("short write: wanted %zu bytes, wrote %zu", len, total));
                        return false;
                }
                total += static_cast<size_t>(n);
        }
        return true;
}

// ============================================================================
// Raw byte access (untagged)
// ============================================================================

ssize_t DataStream::readRawData(void *buf, size_t len) {
        if (_status != Ok) return -1;
        if (len == 0) return 0;
        if (_device == nullptr) {
                setError(ReadPastEnd, String("no device attached"));
                return -1;
        }
        int64_t n = _device->read(buf, static_cast<int64_t>(len));
        if (n < 0) {
                setError(ReadPastEnd, String("device read returned error"));
                return -1;
        }
        return static_cast<ssize_t>(n);
}

ssize_t DataStream::writeRawData(const void *buf, size_t len) {
        if (_status != Ok) return -1;
        if (len == 0) return 0;
        if (!_frameStack.isEmpty()) {
                List<uint8_t> &body = _frameStack.back().body;
                const uint8_t *src = static_cast<const uint8_t *>(buf);
                body.reserve(body.size() + len);
                for (size_t i = 0; i < len; ++i) body.pushToBack(src[i]);
                return static_cast<ssize_t>(len);
        }
        if (_device == nullptr) {
                setError(WriteFailed, String("no device attached"));
                return -1;
        }
        int64_t n = _device->write(buf, static_cast<int64_t>(len));
        if (n < 0) {
                setError(WriteFailed, String("device write returned error"));
                return -1;
        }
        return static_cast<ssize_t>(n);
}

ssize_t DataStream::skipRawData(size_t len) {
        if (_status != Ok) return -1;
        if (len == 0) return 0;
        if (_device == nullptr) {
                setError(ReadPastEnd, String("no device attached"));
                return -1;
        }
        if (!_device->isSequential()) {
                int64_t cur = _device->pos();
                Error   err = _device->seek(cur + static_cast<int64_t>(len));
                if (err.isError()) {
                        setError(ReadPastEnd, String("seek failed in skipRawData"));
                        return -1;
                }
                return static_cast<ssize_t>(len);
        }
        // Sequential: read and discard
        uint8_t tmp[1024];
        size_t  total = 0;
        while (total < len) {
                size_t chunk = len - total;
                if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
                int64_t n = _device->read(tmp, static_cast<int64_t>(chunk));
                if (n <= 0) {
                        setError(ReadPastEnd, String("short read in skipRawData"));
                        return total > 0 ? static_cast<ssize_t>(total) : -1;
                }
                total += static_cast<size_t>(n);
        }
        return static_cast<ssize_t>(total);
}

// ============================================================================
// Primitive value helpers (untagged body bytes — used inside frame bodies)
// ============================================================================

namespace {

template <typename T> void rawWrite(DataStream &s, T val);
template <typename T> T    rawRead(DataStream &s);

} // anonymous namespace

// ============================================================================
// Write operators — primitives (each frames itself with its own tag)
// ============================================================================

DataStream &DataStream::operator<<(int8_t val) {
        beginFrame(DataTypeInt8, 1);
        writeBytes(&val, 1);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(uint8_t val) {
        beginFrame(DataTypeUInt8, 1);
        writeBytes(&val, 1);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(int16_t val) {
        beginFrame(DataTypeInt16, 1);
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(uint16_t val) {
        beginFrame(DataTypeUInt16, 1);
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(int32_t val) {
        beginFrame(DataTypeInt32, 1);
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(uint32_t val) {
        beginFrame(DataTypeUInt32, 1);
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(int64_t val) {
        beginFrame(DataTypeInt64, 1);
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(uint64_t val) {
        beginFrame(DataTypeUInt64, 1);
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(float val) {
        beginFrame(DataTypeFloat, 1);
        uint32_t tmp;
        std::memcpy(&tmp, &val, sizeof(tmp));
        swapIfNeeded(tmp);
        writeBytes(&tmp, sizeof(tmp));
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(double val) {
        beginFrame(DataTypeDouble, 1);
        uint64_t tmp;
        std::memcpy(&tmp, &val, sizeof(tmp));
        swapIfNeeded(tmp);
        writeBytes(&tmp, sizeof(tmp));
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(bool val) {
        beginFrame(DataTypeBool, 1);
        uint8_t v = val ? 1 : 0;
        writeBytes(&v, 1);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const String &val) {
        beginFrame(DataTypeString, 1);
        // Strings are stored as length-prefixed UTF-8.  Convert Latin1
        // strings with non-ASCII bytes to UTF-8 first so the wire form
        // is always valid UTF-8.
        const String *src = &val;
        String        converted;
        if (val.encoding() == String::Latin1) {
                bool needsConvert = false;
                for (size_t i = 0; i < val.byteCount(); ++i) {
                        if (val.byteAt(i) > 0x7F) {
                                needsConvert = true;
                                break;
                        }
                }
                if (needsConvert) {
                        converted = val.toUnicode();
                        src = &converted;
                }
        }
        uint32_t len = static_cast<uint32_t>(src->byteCount());
        uint32_t lenSwapped = len;
        swapIfNeeded(lenSwapped);
        writeBytes(&lenSwapped, sizeof(lenSwapped));
        if (len > 0) writeBytes(src->cstr(), len);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const Buffer &val) {
        beginFrame(DataTypeBuffer, 1);
        uint32_t len = static_cast<uint32_t>(val.size());
        uint32_t lenSwapped = len;
        swapIfNeeded(lenSwapped);
        writeBytes(&lenSwapped, sizeof(lenSwapped));
        if (len > 0) writeBytes(val.data(), len);
        endFrame();
        return *this;
}

// ============================================================================
// Variant — dispatches through the registered DataType ops
// ============================================================================

DataStream &DataStream::operator<<(const Variant &val) {
        const DataType        dt = val.dataType();
        const DataType::Data *td = dt.data();
        if (td == nullptr || td->ops.writeStream == nullptr) {
                // No registered writer.  Emit an empty NoValue frame so
                // the reader can recover.
                promekiWarn("DataStream::operator<<(Variant): no registered writer for DataType id=%u; "
                            "emitting NoValue frame",
                            static_cast<unsigned>(val.dataType().id()));
                beginFrame(DataTypeNoValue, 1);
                endFrame();
                return *this;
        }
        td->ops.writeStream(*this, val.payloadPtr());
        return *this;
}

// ============================================================================
// Read operators — primitives (each consumes its own framed tag)
// ============================================================================

DataStream &DataStream::operator>>(int8_t &val) {
        if (!readFrame(DataTypeInt8)) {
                val = 0;
                return *this;
        }
        if (!readBytes(&val, 1)) val = 0;
        return *this;
}

DataStream &DataStream::operator>>(uint8_t &val) {
        if (!readFrame(DataTypeUInt8)) {
                val = 0;
                return *this;
        }
        if (!readBytes(&val, 1)) val = 0;
        return *this;
}

DataStream &DataStream::operator>>(int16_t &val) {
        if (!readFrame(DataTypeInt16)) {
                val = 0;
                return *this;
        }
        if (!readBytes(&val, sizeof(val))) val = 0;
        else swapIfNeeded(val);
        return *this;
}

DataStream &DataStream::operator>>(uint16_t &val) {
        if (!readFrame(DataTypeUInt16)) {
                val = 0;
                return *this;
        }
        if (!readBytes(&val, sizeof(val))) val = 0;
        else swapIfNeeded(val);
        return *this;
}

DataStream &DataStream::operator>>(int32_t &val) {
        if (!readFrame(DataTypeInt32)) {
                val = 0;
                return *this;
        }
        if (!readBytes(&val, sizeof(val))) val = 0;
        else swapIfNeeded(val);
        return *this;
}

DataStream &DataStream::operator>>(uint32_t &val) {
        if (!readFrame(DataTypeUInt32)) {
                val = 0;
                return *this;
        }
        if (!readBytes(&val, sizeof(val))) val = 0;
        else swapIfNeeded(val);
        return *this;
}

DataStream &DataStream::operator>>(int64_t &val) {
        if (!readFrame(DataTypeInt64)) {
                val = 0;
                return *this;
        }
        if (!readBytes(&val, sizeof(val))) val = 0;
        else swapIfNeeded(val);
        return *this;
}

DataStream &DataStream::operator>>(uint64_t &val) {
        if (!readFrame(DataTypeUInt64)) {
                val = 0;
                return *this;
        }
        if (!readBytes(&val, sizeof(val))) val = 0;
        else swapIfNeeded(val);
        return *this;
}

DataStream &DataStream::operator>>(float &val) {
        if (!readFrame(DataTypeFloat)) {
                val = 0.0f;
                return *this;
        }
        uint32_t tmp = 0;
        if (!readBytes(&tmp, sizeof(tmp))) { val = 0.0f; return *this; }
        swapIfNeeded(tmp);
        std::memcpy(&val, &tmp, sizeof(val));
        return *this;
}

DataStream &DataStream::operator>>(double &val) {
        if (!readFrame(DataTypeDouble)) {
                val = 0.0;
                return *this;
        }
        uint64_t tmp = 0;
        if (!readBytes(&tmp, sizeof(tmp))) { val = 0.0; return *this; }
        swapIfNeeded(tmp);
        std::memcpy(&val, &tmp, sizeof(val));
        return *this;
}

DataStream &DataStream::operator>>(bool &val) {
        if (!readFrame(DataTypeBool)) {
                val = false;
                return *this;
        }
        uint8_t v = 0;
        readBytes(&v, 1);
        val = (v != 0);
        return *this;
}

DataStream &DataStream::operator>>(String &val) {
        if (!readFrame(DataTypeString)) {
                val = String();
                return *this;
        }
        uint32_t len = 0;
        readBytes(&len, sizeof(len));
        swapIfNeeded(len);
        if (_status != Ok) {
                val = String();
                return *this;
        }
        if (len == 0) {
                val = String();
                return *this;
        }
        if (len > MaxFrameBodySize) {
                setError(ReadCorruptData,
                         String::sprintf("String length %u exceeds MaxFrameBodySize (%u)",
                                         static_cast<unsigned>(len),
                                         static_cast<unsigned>(MaxFrameBodySize)));
                val = String();
                return *this;
        }
        std::string buf(len, '\0');
        if (!readBytes(buf.data(), len)) {
                val = String();
                return *this;
        }
        bool allAscii = true;
        for (size_t i = 0; i < len; ++i) {
                if (static_cast<uint8_t>(buf[i]) > 0x7F) {
                        allAscii = false;
                        break;
                }
        }
        val = allAscii ? String(buf.c_str(), len) : String::fromUtf8(buf.c_str(), len);
        return *this;
}

DataStream &DataStream::operator>>(Buffer &val) {
        // Buffer accepts two tags: DataTypeNoValue → null Buffer, or
        // DataTypeBuffer → allocated Buffer.
        DataTypeID tag = DataTypeInvalid;
        uint16_t   ver = 0;
        uint32_t   sz = 0;
        if (!readFrameHeader(tag, ver, sz)) {
                val = Buffer();
                return *this;
        }
        if (tag == DataTypeNoValue) {
                val = Buffer();
                return *this;
        }
        if (tag != DataTypeBuffer) {
                setError(ReadCorruptData,
                         String::sprintf("expected tag 0x%04X (DataTypeBuffer) or 0x%04X (DataTypeNoValue), got 0x%04X",
                                         static_cast<unsigned>(DataTypeBuffer),
                                         static_cast<unsigned>(DataTypeNoValue),
                                         static_cast<unsigned>(tag)));
                val = Buffer();
                return *this;
        }
        uint32_t len = 0;
        readBytes(&len, sizeof(len));
        swapIfNeeded(len);
        if (_status != Ok) {
                val = Buffer();
                return *this;
        }
        if (len > MaxFrameBodySize) {
                setError(ReadCorruptData,
                         String::sprintf("Buffer length %u exceeds MaxFrameBodySize (%u)",
                                         static_cast<unsigned>(len),
                                         static_cast<unsigned>(MaxFrameBodySize)));
                val = Buffer();
                return *this;
        }
        if (len == 0) {
                val = Buffer();
                return *this;
        }
        Buffer tmp(len);
        if (!readBytes(tmp.data(), len)) {
                val = Buffer();
                return *this;
        }
        tmp.setSize(len);
        val = std::move(tmp);
        return *this;
}

// ============================================================================
// Variant read — peek the tag, dispatch through DataType ops
// ============================================================================
//
// The frame header tells us which DataType this Variant holds.  We peek
// the header so the per-type readStream op can re-read it through
// readFrame; the cached header lets that re-read succeed without
// touching the device a second time, so the path works on sequential
// devices (sockets, pipes) just as well as seekable ones.
//
// Tags the local registry doesn't know about hit the forward-compat
// path: drain the cached header, skip the body via its declared size,
// and yield an invalid Variant.

DataStream &DataStream::operator>>(Variant &val) {
        DataTypeID tag = DataTypeInvalid;
        uint16_t   ver = 0;
        uint32_t   sz  = 0;
        if (!peekFrameHeader(tag, ver, sz)) {
                val = Variant();
                return *this;
        }
        const DataType        dt(tag);
        const DataType::Data *td = dt.data();
        if (td != nullptr && td->ops.readStream != nullptr && td->ops.defaultConstruct != nullptr) {
                val = Variant::readFromStream(*this, dt);
                return *this;
        }
        // Unknown tag — consume the cached header and skip past the body.
        promekiWarn("DataStream::operator>>(Variant): unknown DataType tag 0x%04X "
                    "(version=%u, size=%u) — skipping body",
                    static_cast<unsigned>(tag), static_cast<unsigned>(ver),
                    static_cast<unsigned>(sz));
        readFrameHeader(tag, ver, sz);
        skipFrameBody(sz);
        val = Variant();
        return *this;
}

PROMEKI_NAMESPACE_END
