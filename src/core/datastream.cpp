/**
 * @file      datastream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cstring>
#include <promeki/datastream.h>
#include <promeki/variant.h>
#include <promeki/uuid.h>
#include <promeki/umid.h>
#include <promeki/datetime.h>
#include <promeki/timestamp.h>
#include <promeki/timecode.h>
#include <promeki/framerate.h>
#include <promeki/videoformat.h>
#include <promeki/color.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/pixelformat.h>
#include <promeki/audioformat.h>
#include <promeki/ancformat.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiomarker.h>
#include <promeki/audiostreamdesc.h>
#include <promeki/videocodec.h>
#include <promeki/audiocodec.h>
#include <promeki/cea608packet.h>
#include <promeki/cea708cdp.h>
#include <promeki/subtitle.h>
#include <promeki/duration.h>
#include <promeki/framenumber.h>
#include <promeki/framecount.h>
#include <promeki/mediaduration.h>
#include <promeki/stringlist.h>
#include <promeki/url.h>
#include <promeki/windowedstat.h>
#include <promeki/xml.h>
#include <promeki/enum.h>
#include <promeki/enumlist.h>
#include <promeki/mediatimestamp.h>
#include <promeki/macaddress.h>
#include <promeki/eui64.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/socketaddress.h>
#include <promeki/sdpsession.h>
#endif
#if PROMEKI_ENABLE_TLS
#include <promeki/sslcontext.h>
#endif

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

DataStream::DataStream(IODevice *device) : _device(device) {}

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
        // Bytes 7-15 are already zero from the aggregate initializer.
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
        // Reserved bytes must be zero. Any non-zero value indicates a
        // future header extension this reader doesn't understand, so we
        // fail loudly rather than silently mis-parse.
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
        // First error wins — preserve the earliest failure context.
        if (_status != Ok) return;
        _status = s;
        _errorContext = std::move(ctx);
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
//   [tag:uint16 (byte-order controlled)][version:uint16 (byte-order controlled)][size:uint32 (byte-order controlled)]
//
// Two-byte version sits between the two-byte tag and the four-byte
// size, so the size field lands on a natural 4-byte boundary inside
// the header and the whole header is 8 bytes.
//
// Tags are 16-bit so the library can grow past 256 built-in types
// while still leaving room for user extensions (see UserTypeIdBegin).
// The version field lets each type evolve its body layout
// independently of the others.  The size field lets readers skip
// past tags they don't understand.
//
// Writing is buffered: beginFrame pushes a body accumulator onto
// _frameStack, every subsequent writeBytes call routes into the top
// of that stack, and endFrame pops the accumulator and emits
// [tag][ver][size][body] in one go to either the parent frame or the
// underlying device.  The buffered design lets us emit the size
// field without seeking the device — readers and writers alike work
// over plain sequential IODevices (sockets, pipes, files, in-memory
// buffers).

void DataStream::beginFrame(TypeId id, uint16_t version) {
        if (_status != Ok) return;
        PendingFrame frame;
        frame.tag = id;
        frame.version = version;
        // Reserve a modest amount so tiny payloads avoid an extra
        // grow; List<uint8_t> internally is a std::vector so this is
        // a real reservation, not a hint.
        frame.body.reserve(32);
        _frameStack.pushToBack(std::move(frame));
}

void DataStream::endFrame() {
        if (_frameStack.isEmpty()) {
                setError(WriteFailed, String("endFrame called with no open frame"));
                return;
        }
        // Pop into a local — we may need to write its bytes to
        // either the device or the new top-of-stack, and writeBytes
        // routes based on whether _frameStack is non-empty.  Doing
        // the pop *before* the write makes that routing correct.
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

        // Header layout: 2-byte tag (byte-order controlled), 2-byte
        // version (byte-order controlled), 4-byte size (byte-order
        // controlled).  Total 8 bytes; size lands on a 4-byte boundary.
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

bool DataStream::readFrameHeader(TypeId &outTag, uint16_t &outVersion, uint32_t &outSize) {
        if (_status != Ok) return false;
        // Cached frame header from a prior peekFrameHeader call — return
        // those parsed values and drop the cache so subsequent header
        // reads pull fresh bytes from the device.
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
        outTag = static_cast<TypeId>(tagRaw);
        outVersion = verRaw;
        outSize = sz;
        return true;
}

bool DataStream::peekFrameHeader(TypeId &outTag, uint16_t &outVersion, uint32_t &outSize) {
        // Already cached?  Return the cached parse without touching the
        // device.  The cache lives until the next readFrameHeader call.
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

bool DataStream::readFrame(TypeId expected, uint16_t maxVersion, uint16_t *outVersion, uint32_t *outSize) {
        TypeId   tag = static_cast<TypeId>(0);
        uint16_t ver = 0;
        uint32_t sz = 0;
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
        TypeId   tag = static_cast<TypeId>(0);
        uint16_t ver = 0;
        uint32_t sz = 0;
        if (!readFrameHeader(tag, ver, sz)) return;
        skipFrameBody(sz);
}

bool DataStream::skipFrameBody(uint32_t sz) {
        if (sz == 0) return true;
        // For seekable devices that report a known size, skipRawData
        // is just a seek — and the seek may succeed past the
        // content end on devices like BufferIODevice that don't
        // bound-check.  Validate up front so a truncated body
        // surfaces as ReadPastEnd here rather than silently leaving
        // the read position dangling.
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
        // When a frame is open, route bytes into its body
        // accumulator instead of the device.  endFrame is then the
        // single point that flushes the assembled frame.
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
        // Inside an open frame, raw bytes go into the body
        // accumulator like every other write — otherwise a caller
        // using writeRawData to fill a frame body would emit bytes
        // around the frame, corrupting the size accounting.  Outside
        // of any frame, writeRawData stays a passthrough to the
        // underlying device.
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
// Internal helpers for writing/reading values without type tags.
// Used both by direct operators and by Variant serialization.
// ============================================================================

void DataStream::writeInt8(int8_t val) {
        writeBytes(&val, 1);
}
void DataStream::writeUInt8(uint8_t val) {
        writeBytes(&val, 1);
}

void DataStream::writeInt16(int16_t val) {
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
}
void DataStream::writeUInt16(uint16_t val) {
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
}
void DataStream::writeInt32(int32_t val) {
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
}
void DataStream::writeUInt32(uint32_t val) {
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
}
void DataStream::writeInt64(int64_t val) {
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
}
void DataStream::writeUInt64(uint64_t val) {
        swapIfNeeded(val);
        writeBytes(&val, sizeof(val));
}

void DataStream::writeFloat(float val) {
        uint32_t tmp;
        std::memcpy(&tmp, &val, sizeof(tmp));
        swapIfNeeded(tmp);
        writeBytes(&tmp, sizeof(tmp));
}

void DataStream::writeDouble(double val) {
        uint64_t tmp;
        std::memcpy(&tmp, &val, sizeof(tmp));
        swapIfNeeded(tmp);
        writeBytes(&tmp, sizeof(tmp));
}

void DataStream::writeBool(bool val) {
        uint8_t v = val ? 1 : 0;
        writeBytes(&v, 1);
}

void DataStream::writeStringData(const String &val) {
        if (val.isEmpty()) {
                writeUInt32(0);
                return;
        }
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
        writeUInt32(len);
        writeBytes(src->cstr(), len);
}

void DataStream::writeBufferData(const Buffer &val) {
        uint32_t len = static_cast<uint32_t>(val.size());
        writeUInt32(len);
        if (len > 0) writeBytes(val.data(), len);
}

void DataStream::writeUUIDData(const UUID &val) {
        writeBytes(val.raw(), 16);
}

void DataStream::writeUMIDData(const UMID &val) {
        // Format: uint8 byte length (32 for Basic, 64 for Extended, 0
        // for Invalid), then `length` raw UMID bytes.  Invalid UMIDs
        // serialize as a bare zero byte with no payload.
        const size_t n = val.byteSize();
        writeUInt8(static_cast<uint8_t>(n));
        if (n > 0) writeBytes(val.raw(), n);
}

void DataStream::writeDateTimeData(const DateTime &val) {
        int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(val.value().time_since_epoch()).count();
        *this << ns;
}

void DataStream::writeTimeStampData(const TimeStamp &val) {
        *this << val.nanoseconds();
}

void DataStream::writeFrameRateData(const FrameRate &val) {
        *this << static_cast<uint32_t>(val.numerator());
        *this << static_cast<uint32_t>(val.denominator());
}

void DataStream::writeVideoFormatData(const VideoFormat &val) {
        writeStringData(val.toString());
}

void DataStream::writeTimecodeData(const Timecode &val) {
        // Timecode carries a mode (format) plus digits; the canonical
        // toString() form preserves all information and round-trips through
        // Timecode::fromString().
        writeStringData(val.toString().first());
}

void DataStream::writeColorData(const Color &val) {
        // A default-constructed Color has no ColorModel and would
        // serialise to "Invalid(0,0,0,0)" which fromString rejects.
        // Encode it as an empty string so the round-trip lands back on
        // a default Color (readColorData's empty-string fast path).
        // Valid Colors round-trip through the lossless ModelFormat
        // representation.
        if (!val.isValid()) {
                writeStringData(String());
                return;
        }
        writeStringData(val.toString());
}

void DataStream::writeColorModelData(const ColorModel &val) {
        writeStringData(val.name());
}

void DataStream::writeMemSpaceData(const MemSpace &val) {
        // MemSpace has no name->ID lookup; use the numeric ID. Tagged
        // for consistency with the other structured types' inner values.
        *this << static_cast<uint32_t>(val.id());
}

void DataStream::writePixelMemLayoutData(const PixelMemLayout &val) {
        writeStringData(val.name());
}

void DataStream::writePixelFormatData(const PixelFormat &val) {
        writeStringData(val.name());
}

void DataStream::writeAudioFormatData(const AudioFormat &val) {
        writeStringData(val.name());
}

void DataStream::writeAncFormatData(const AncFormat &val) { writeStringData(val.name()); }

void DataStream::writeEnumData(const Enum &val) {
        // Enum's qualified "TypeName::ValueName" form is the canonical
        // serialization consumed by Enum::lookup().
        writeStringData(val.toString());
}

void DataStream::writeEnumListData(const EnumList &val) {
        // Wire format mirrors the EnumList data model exactly:
        //   - type name (length-prefixed String)
        //   - count (tagged uint32)
        //   - each element as a tagged int32
        //
        // Storing each element as a plain integer keeps round-trips
        // lossless for out-of-list values — toString() would collapse
        // them to a decimal that then re-parses through fromString(),
        // but only via the string detour.  The integer form avoids
        // that round-trip and matches how we serialize primitives.
        writeStringData(val.elementType().name());
        *this << static_cast<uint32_t>(val.size());
        const List<int> &values = val.values();
        for (size_t i = 0; i < values.size(); ++i) {
                *this << static_cast<int32_t>(values[i]);
        }
}

void DataStream::writeStringListData(const StringList &val) {
        // Use a tagged uint32_t count to match the List/Map/Set convention.
        *this << static_cast<uint32_t>(val.size());
        for (const String &s : val) writeStringData(s);
}

// ---------------------------------------------------------------------------
// Untagged read helpers
// ---------------------------------------------------------------------------

int8_t DataStream::readInt8() {
        int8_t v = 0;
        readBytes(&v, 1);
        return v;
}
uint8_t DataStream::readUInt8() {
        uint8_t v = 0;
        readBytes(&v, 1);
        return v;
}

int16_t DataStream::readInt16() {
        int16_t v = 0;
        if (readBytes(&v, sizeof(v)))
                swapIfNeeded(v);
        else
                v = 0;
        return v;
}
uint16_t DataStream::readUInt16() {
        uint16_t v = 0;
        if (readBytes(&v, sizeof(v)))
                swapIfNeeded(v);
        else
                v = 0;
        return v;
}
int32_t DataStream::readInt32() {
        int32_t v = 0;
        if (readBytes(&v, sizeof(v)))
                swapIfNeeded(v);
        else
                v = 0;
        return v;
}
uint32_t DataStream::readUInt32() {
        uint32_t v = 0;
        if (readBytes(&v, sizeof(v)))
                swapIfNeeded(v);
        else
                v = 0;
        return v;
}
int64_t DataStream::readInt64() {
        int64_t v = 0;
        if (readBytes(&v, sizeof(v)))
                swapIfNeeded(v);
        else
                v = 0;
        return v;
}
uint64_t DataStream::readUInt64() {
        uint64_t v = 0;
        if (readBytes(&v, sizeof(v)))
                swapIfNeeded(v);
        else
                v = 0;
        return v;
}

float DataStream::readFloat() {
        uint32_t tmp = 0;
        if (readBytes(&tmp, sizeof(tmp))) {
                swapIfNeeded(tmp);
                float val;
                std::memcpy(&val, &tmp, sizeof(val));
                return val;
        }
        return 0.0f;
}

double DataStream::readDouble() {
        uint64_t tmp = 0;
        if (readBytes(&tmp, sizeof(tmp))) {
                swapIfNeeded(tmp);
                double val;
                std::memcpy(&val, &tmp, sizeof(val));
                return val;
        }
        return 0.0;
}

bool DataStream::readBoolValue() {
        uint8_t v = 0;
        readBytes(&v, 1);
        return v != 0;
}

String DataStream::readStringData() {
        uint32_t len = readUInt32();
        if (_status != Ok) return String();
        if (len == 0) return String();
        if (len > 256 * 1024 * 1024) {
                setError(ReadCorruptData, String("String length exceeds sanity limit"));
                return String();
        }
        std::string buf(len, '\0');
        if (!readBytes(buf.data(), len)) return String();
        bool allAscii = true;
        for (size_t i = 0; i < len; ++i) {
                if (static_cast<uint8_t>(buf[i]) > 0x7F) {
                        allAscii = false;
                        break;
                }
        }
        if (allAscii) return String(buf.c_str(), len);
        return String::fromUtf8(buf.c_str(), len);
}

Buffer DataStream::readBufferData() {
        uint32_t len = readUInt32();
        if (_status != Ok) return Buffer();
        if (len == 0) return Buffer();
        if (len > 256 * 1024 * 1024) {
                setError(ReadCorruptData, String("Buffer length exceeds sanity limit"));
                return Buffer();
        }
        Buffer buf(len);
        if (!readBytes(buf.data(), len)) return Buffer();
        buf.setSize(len);
        return buf;
}

UUID DataStream::readUUIDData() {
        UUID::DataFormat raw;
        if (!readBytes(raw.data(), 16)) return UUID();
        return UUID(raw);
}

UMID DataStream::readUMIDData() {
        uint8_t n = readUInt8();
        if (_status != Ok) return UMID();
        if (n == 0) return UMID();
        if (n != UMID::BasicSize && n != UMID::ExtendedSize) {
                setError(ReadCorruptData,
                         String::sprintf("UMID payload length %u is neither 32 nor 64", static_cast<unsigned>(n)));
                return UMID();
        }
        uint8_t buf[UMID::ExtendedSize];
        if (!readBytes(buf, n)) return UMID();
        return UMID::fromBytes(buf, n);
}

DateTime DataStream::readDateTimeData() {
        int64_t ns = 0;
        *this >> ns;
        if (_status != Ok) return DateTime();
        DateTime::Value tp{
                std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(ns))};
        return DateTime(tp);
}

TimeStamp DataStream::readTimeStampData() {
        int64_t ns = 0;
        *this >> ns;
        if (_status != Ok) return TimeStamp();
        TimeStamp::Value tp{std::chrono::duration_cast<TimeStamp::Clock::duration>(std::chrono::nanoseconds(ns))};
        return TimeStamp(tp);
}

FrameRate DataStream::readFrameRateData() {
        uint32_t num = 0, den = 0;
        *this >> num >> den;
        if (_status != Ok) return FrameRate();
        return FrameRate(FrameRate::RationalType(num, den));
}

VideoFormat DataStream::readVideoFormatData() {
        String s = readStringData();
        if (_status != Ok) return VideoFormat();
        if (s.isEmpty()) return VideoFormat();
        auto [vf, err] = VideoFormat::fromString(s);
        if (err.isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse VideoFormat from '%s'", s.cstr()));
                return VideoFormat();
        }
        return vf;
}

Timecode DataStream::readTimecodeData() {
        String s = readStringData();
        if (_status != Ok) return Timecode();
        auto [tc, err] = Timecode::fromString(s);
        if (err.isError()) {
                // Preserve the parse failure as corrupt data rather than
                // silently yielding an empty Timecode — callers need to
                // know their on-disk data is unparseable.
                setError(ReadCorruptData, String("Timecode::fromString failed: ") + s);
                return Timecode();
        }
        return tc;
}

Color DataStream::readColorData() {
        String s = readStringData();
        if (_status != Ok) return Color();
        if (s.isEmpty()) return Color();
        auto [c, e] = Color::fromString(s);
        if (e.isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse Color from '%s'", s.cstr()));
                return Color();
        }
        return c;
}

ColorModel DataStream::readColorModelData() {
        String s = readStringData();
        if (_status != Ok) return ColorModel();
        return ColorModel::lookup(s);
}

MemSpace DataStream::readMemSpaceData() {
        uint32_t id = 0;
        *this >> id;
        if (_status != Ok) return MemSpace();
        return MemSpace(static_cast<MemSpace::ID>(id));
}

PixelMemLayout DataStream::readPixelMemLayoutData() {
        String s = readStringData();
        if (_status != Ok) return PixelMemLayout();
        return PixelMemLayout::lookup(s);
}

PixelFormat DataStream::readPixelFormatData() {
        String s = readStringData();
        if (_status != Ok) return PixelFormat();
        return PixelFormat::lookup(s);
}

AudioFormat DataStream::readAudioFormatData() {
        String s = readStringData();
        if (_status != Ok) return AudioFormat();
        return value(AudioFormat::lookup(s));
}

AncFormat DataStream::readAncFormatData() {
        String s = readStringData();
        if (_status != Ok) return AncFormat();
        // AncFormat::idFromName returns Invalid for unknown names; the
        // resulting wrapper is harmless (isValid() == false) so the
        // round-trip is lossy-but-safe on unregistered names rather
        // than triggering a stream-level read error.
        return AncFormat(AncFormat::idFromName(s));
}

Enum DataStream::readEnumData() {
        String s = readStringData();
        if (_status != Ok) return Enum();
        // Empty body or the bare qualifier "::" is the wire form of a
        // default-constructed (invalid) Enum — Enum::toString() returns
        // "::" for it.  Both spellings reduce to the same default
        // without raising ReadCorruptData.
        if (s.isEmpty() || s == "::") return Enum();
        Error err;
        Enum  e = Enum::lookup(s, &err);
        if (err.isError()) {
                setError(ReadCorruptData, String("Enum::lookup failed: ") + s);
                return Enum();
        }
        return e;
}

EnumList DataStream::readEnumListData() {
        String typeName = readStringData();
        if (_status != Ok) return EnumList();
        // Empty type name is the wire form of a default-constructed
        // EnumList: write emits empty-name + count(0) + no entries.
        // Leave the Enum::Type unbound and let the rest of the read
        // drain the count tag below so the stream stays in sync.
        Enum::Type type;
        if (!typeName.isEmpty()) {
                type = Enum::findType(typeName);
                if (!type.isValid()) {
                        setError(ReadCorruptData, String("EnumList: unknown type '") + typeName + "'");
                        return EnumList();
                }
        }
        uint32_t count = 0;
        *this >> count; // tagged read
        if (_status != Ok) return EnumList();
        if (count > 256 * 1024 * 1024) {
                setError(ReadCorruptData, String("EnumList count exceeds sanity limit"));
                return EnumList();
        }
        EnumList out(type);
        for (uint32_t i = 0; i < count; ++i) {
                int32_t v = 0;
                *this >> v; // tagged read
                if (_status != Ok) return EnumList();
                out.append(static_cast<int>(v));
        }
        return out;
}

StringList DataStream::readStringListData() {
        uint32_t count = 0;
        *this >> count; // tagged read, matches writeStringListData
        if (_status != Ok) return StringList();
        if (count > 256 * 1024 * 1024) {
                setError(ReadCorruptData, String("StringList count exceeds sanity limit"));
                return StringList();
        }
        StringList list;
        list.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
                String s = readStringData();
                if (_status != Ok) return StringList();
                list.pushToBack(std::move(s));
        }
        return list;
}

// ============================================================================
// Write operators — primitives (tagged)
// ============================================================================

DataStream &DataStream::operator<<(int8_t val) {
        beginFrame(TypeInt8, 1);
        writeInt8(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(uint8_t val) {
        beginFrame(TypeUInt8, 1);
        writeUInt8(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(int16_t val) {
        beginFrame(TypeInt16, 1);
        writeInt16(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(uint16_t val) {
        beginFrame(TypeUInt16, 1);
        writeUInt16(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(int32_t val) {
        beginFrame(TypeInt32, 1);
        writeInt32(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(uint32_t val) {
        beginFrame(TypeUInt32, 1);
        writeUInt32(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(int64_t val) {
        beginFrame(TypeInt64, 1);
        writeInt64(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(uint64_t val) {
        beginFrame(TypeUInt64, 1);
        writeUInt64(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(float val) {
        beginFrame(TypeFloat, 1);
        writeFloat(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(double val) {
        beginFrame(TypeDouble, 1);
        writeDouble(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(bool val) {
        beginFrame(TypeBool, 1);
        writeBool(val);
        endFrame();
        return *this;
}

// ============================================================================
// Write operators — complex types (tagged)
// ============================================================================

DataStream &DataStream::operator<<(const String &val) {
        beginFrame(TypeString, 1);
        writeStringData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const Buffer &val) {
        // Null (default-constructed) Buffer is encoded as TypeInvalid
        // so it round-trips as null rather than as an empty Buffer.
        // Non-null uses the TypeBuffer framing.
        if (!val) {
                beginFrame(TypeInvalid, 1);
                endFrame();
                return *this;
        }
        beginFrame(TypeBuffer, 1);
        writeBufferData(val);
        endFrame();
        return *this;
}

// ============================================================================
// Write operators — data objects
// ============================================================================

DataStream &DataStream::operator<<(const UUID &val) {
        beginFrame(TypeUUID, 1);
        writeUUIDData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const UMID &val) {
        beginFrame(TypeUMID, 1);
        writeUMIDData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const DateTime &val) {
        beginFrame(TypeDateTime, 1);
        writeDateTimeData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const TimeStamp &val) {
        beginFrame(TypeTimeStamp, 1);
        writeTimeStampData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const FrameRate &val) {
        beginFrame(TypeFrameRate, 1);
        writeFrameRateData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const VideoFormat &val) {
        beginFrame(TypeVideoFormat, 1);
        writeVideoFormatData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const Timecode &val) {
        beginFrame(TypeTimecode, 1);
        writeTimecodeData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const Color &val) {
        beginFrame(TypeColor, 1);
        writeColorData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const ColorModel &val) {
        beginFrame(TypeColorModel, 1);
        writeColorModelData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const MemSpace &val) {
        beginFrame(TypeMemSpace, 1);
        writeMemSpaceData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const PixelMemLayout &val) {
        beginFrame(TypePixelMemLayout, 1);
        writePixelMemLayoutData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const PixelFormat &val) {
        beginFrame(TypePixelFormat, 1);
        writePixelFormatData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const AudioFormat &val) {
        beginFrame(TypeAudioFormat, 1);
        writeAudioFormatData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const AncFormat &val) {
        beginFrame(TypeAncFormat, 1);
        writeAncFormatData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const Enum &val) {
        beginFrame(TypeEnum, 1);
        writeEnumData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const EnumList &val) {
        beginFrame(TypeEnumList, 1);
        writeEnumListData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const MediaTimeStamp &val) {
        beginFrame(TypeMediaTimeStamp, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const FrameNumber &val) {
        beginFrame(TypeFrameNumber, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const FrameCount &val) {
        beginFrame(TypeFrameCount, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const MediaDuration &val) {
        beginFrame(TypeMediaDuration, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const Duration &val) {
        // Duration is a simple int64 wall-clock nanoseconds value —
        // encode it directly rather than round-tripping through a
        // string so the wire form is both compact and preserves the
        // full 64-bit precision without parsing cost.
        beginFrame(TypeDuration, 1);
        writeInt64(val.nanoseconds());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const Url &val) {
        beginFrame(TypeUrl, 1);
        // Serialize via the canonical string form — round-trips
        // through Url::fromString/toString preserve every component
        // we care about, and the string form is stable across
        // library versions.
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const MacAddress &val) {
        beginFrame(TypeMacAddress, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const EUI64 &val) {
        beginFrame(TypeEUI64, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const StringList &val) {
        beginFrame(TypeStringList, 1);
        writeStringListData(val);
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const VideoCodec &val) {
        // VideoCodec round-trips through its "Codec[:Backend]" string
        // form (see VideoCodec::toString / fromString), which preserves
        // both the codec identity and the backend pin when one is set.
        beginFrame(TypeVideoCodec, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const AudioCodec &val) {
        // AudioCodec uses the same "Codec[:Backend]" string round-trip
        // as VideoCodec.
        beginFrame(TypeAudioCodec, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const SocketAddress &val) {
        // SocketAddress round-trips through "host:port" (IPv6 uses the
        // bracketed form, e.g. "[::1]:5004") via toString / fromString.
        beginFrame(TypeSocketAddress, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

DataStream &DataStream::operator<<(const SdpSession &val) {
        // SdpSession is serialised as an RFC 4566 SDP text blob — the
        // canonical external form understood by every SDP consumer.
        // Unknown extension attributes the parser doesn't recognise
        // may not survive round-trips, but the core session / media
        // description is lossless.
        beginFrame(TypeSdpSession, 1);
        writeStringData(val.toString());
        endFrame();
        return *this;
}

#if PROMEKI_ENABLE_TLS
DataStream &DataStream::operator<<(const SharedPtr<SslContext, false> &val) {
        // SslContext is process-local opaque state with no persistent
        // form — we emit the tag so the Variant payload-dispatch table
        // round-trips, but no body bytes follow.  See the read overload.
        (void)val;
        beginFrame(TypeSslContext, 1);
        endFrame();
        return *this;
}
#endif

// ============================================================================
// Variant write — registry-driven dispatch
//
// The Variant's @ref DataType carries an @c ops.writeStream function
// pointer that was populated at registration time from the existing
// free @c operator<<(DataStream&, const T&) for the registered type.
// We just look it up and call it; the per-type operator emits its
// own frame, so the Variant write does not wrap an outer frame.
// An invalid Variant (or one whose registered type has no
// @c writeStream slot) emits an empty @c TypeInvalid frame.
// ============================================================================

DataStream &DataStream::operator<<(const Variant &val) {
        const DataType        dt = val.dataType();
        const DataType::Data *td = dt.data();
        if (td == nullptr || td->ops.writeStream == nullptr) {
                beginFrame(TypeInvalid, 1);
                endFrame();
                return *this;
        }
        td->ops.writeStream(*this, val.payloadPtr());
        return *this;
}

// ============================================================================
// Read operators — primitives (tagged)
// ============================================================================

DataStream &DataStream::operator>>(int8_t &val) {
        if (!readFrame(TypeInt8)) {
                val = 0;
                return *this;
        }
        val = readInt8();
        return *this;
}

DataStream &DataStream::operator>>(uint8_t &val) {
        if (!readFrame(TypeUInt8)) {
                val = 0;
                return *this;
        }
        val = readUInt8();
        return *this;
}

DataStream &DataStream::operator>>(int16_t &val) {
        if (!readFrame(TypeInt16)) {
                val = 0;
                return *this;
        }
        val = readInt16();
        return *this;
}

DataStream &DataStream::operator>>(uint16_t &val) {
        if (!readFrame(TypeUInt16)) {
                val = 0;
                return *this;
        }
        val = readUInt16();
        return *this;
}

DataStream &DataStream::operator>>(int32_t &val) {
        if (!readFrame(TypeInt32)) {
                val = 0;
                return *this;
        }
        val = readInt32();
        return *this;
}

DataStream &DataStream::operator>>(uint32_t &val) {
        if (!readFrame(TypeUInt32)) {
                val = 0;
                return *this;
        }
        val = readUInt32();
        return *this;
}

DataStream &DataStream::operator>>(int64_t &val) {
        if (!readFrame(TypeInt64)) {
                val = 0;
                return *this;
        }
        val = readInt64();
        return *this;
}

DataStream &DataStream::operator>>(uint64_t &val) {
        if (!readFrame(TypeUInt64)) {
                val = 0;
                return *this;
        }
        val = readUInt64();
        return *this;
}

DataStream &DataStream::operator>>(float &val) {
        if (!readFrame(TypeFloat)) {
                val = 0.0f;
                return *this;
        }
        val = readFloat();
        return *this;
}

DataStream &DataStream::operator>>(double &val) {
        if (!readFrame(TypeDouble)) {
                val = 0.0;
                return *this;
        }
        val = readDouble();
        return *this;
}

DataStream &DataStream::operator>>(bool &val) {
        if (!readFrame(TypeBool)) {
                val = false;
                return *this;
        }
        val = readBoolValue();
        return *this;
}

// ============================================================================
// Read operators — complex types (tagged)
// ============================================================================

DataStream &DataStream::operator>>(String &val) {
        if (!readFrame(TypeString)) {
                val = String();
                return *this;
        }
        val = readStringData();
        return *this;
}

DataStream &DataStream::operator>>(Buffer &val) {
        // Buffer accepts two tags: TypeInvalid → null Buffer (the
        // body is zero bytes), TypeBuffer → allocated Buffer (the
        // body is a length-prefixed byte run).  Use readFrameHeader
        // so we can dispatch on whichever tag we got.
        TypeId   tag = static_cast<TypeId>(0);
        uint16_t ver = 0;
        uint32_t sz = 0;
        if (!readFrameHeader(tag, ver, sz)) {
                val = Buffer();
                return *this;
        }
        if (tag == TypeInvalid) {
                val = Buffer();
                return *this;
        }
        if (tag != TypeBuffer) {
                setError(ReadCorruptData,
                         String::sprintf("expected tag 0x%04X (TypeBuffer) or 0x%04X (TypeInvalid), got 0x%04X",
                                         static_cast<unsigned>(TypeBuffer), static_cast<unsigned>(TypeInvalid),
                                         static_cast<unsigned>(tag)));
                val = Buffer();
                return *this;
        }
        val = readBufferData();
        return *this;
}

// ============================================================================
// Read operators — data objects
// ============================================================================

DataStream &DataStream::operator>>(UUID &val) {
        if (!readFrame(TypeUUID)) {
                val = UUID();
                return *this;
        }
        val = readUUIDData();
        return *this;
}

DataStream &DataStream::operator>>(UMID &val) {
        if (!readFrame(TypeUMID)) {
                val = UMID();
                return *this;
        }
        val = readUMIDData();
        return *this;
}

DataStream &DataStream::operator>>(DateTime &val) {
        if (!readFrame(TypeDateTime)) {
                val = DateTime();
                return *this;
        }
        val = readDateTimeData();
        return *this;
}

DataStream &DataStream::operator>>(TimeStamp &val) {
        if (!readFrame(TypeTimeStamp)) {
                val = TimeStamp();
                return *this;
        }
        val = readTimeStampData();
        return *this;
}

DataStream &DataStream::operator>>(FrameRate &val) {
        if (!readFrame(TypeFrameRate)) {
                val = FrameRate();
                return *this;
        }
        val = readFrameRateData();
        return *this;
}

DataStream &DataStream::operator>>(VideoFormat &val) {
        if (!readFrame(TypeVideoFormat)) {
                val = VideoFormat();
                return *this;
        }
        val = readVideoFormatData();
        return *this;
}

DataStream &DataStream::operator>>(Timecode &val) {
        if (!readFrame(TypeTimecode)) {
                val = Timecode();
                return *this;
        }
        val = readTimecodeData();
        return *this;
}

DataStream &DataStream::operator>>(Color &val) {
        if (!readFrame(TypeColor)) {
                val = Color();
                return *this;
        }
        val = readColorData();
        return *this;
}

DataStream &DataStream::operator>>(ColorModel &val) {
        if (!readFrame(TypeColorModel)) {
                val = ColorModel();
                return *this;
        }
        val = readColorModelData();
        return *this;
}

DataStream &DataStream::operator>>(MemSpace &val) {
        if (!readFrame(TypeMemSpace)) {
                val = MemSpace();
                return *this;
        }
        val = readMemSpaceData();
        return *this;
}

DataStream &DataStream::operator>>(PixelMemLayout &val) {
        if (!readFrame(TypePixelMemLayout)) {
                val = PixelMemLayout();
                return *this;
        }
        val = readPixelMemLayoutData();
        return *this;
}

DataStream &DataStream::operator>>(PixelFormat &val) {
        if (!readFrame(TypePixelFormat)) {
                val = PixelFormat();
                return *this;
        }
        val = readPixelFormatData();
        return *this;
}

DataStream &DataStream::operator>>(AudioFormat &val) {
        if (!readFrame(TypeAudioFormat)) {
                val = AudioFormat();
                return *this;
        }
        val = readAudioFormatData();
        return *this;
}

DataStream &DataStream::operator>>(AncFormat &val) {
        if (!readFrame(TypeAncFormat)) {
                val = AncFormat();
                return *this;
        }
        val = readAncFormatData();
        return *this;
}

DataStream &DataStream::operator>>(Enum &val) {
        if (!readFrame(TypeEnum)) {
                val = Enum();
                return *this;
        }
        val = readEnumData();
        return *this;
}

DataStream &DataStream::operator>>(EnumList &val) {
        if (!readFrame(TypeEnumList)) {
                val = EnumList();
                return *this;
        }
        val = readEnumListData();
        return *this;
}

DataStream &DataStream::operator>>(MediaTimeStamp &val) {
        if (!readFrame(TypeMediaTimeStamp)) {
                val = MediaTimeStamp();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = MediaTimeStamp();
                return *this;
        }
        // A default-constructed MediaTimeStamp serializes to an empty
        // string via toString(), so accept that as the default-value
        // wire form rather than trying to parse it.
        if (s.isEmpty()) {
                val = MediaTimeStamp();
                return *this;
        }
        auto [mts, parseErr] = MediaTimeStamp::fromString(s);
        if (parseErr.isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse MediaTimeStamp from '%s'", s.cstr()));
                val = MediaTimeStamp();
                return *this;
        }
        val = mts;
        return *this;
}

DataStream &DataStream::operator>>(FrameNumber &val) {
        if (!readFrame(TypeFrameNumber)) {
                val = FrameNumber();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = FrameNumber();
                return *this;
        }
        auto [fn, pe] = FrameNumber::fromString(s);
        if (pe.isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse FrameNumber from '%s'", s.cstr()));
                val = FrameNumber();
                return *this;
        }
        val = fn;
        return *this;
}

DataStream &DataStream::operator>>(FrameCount &val) {
        if (!readFrame(TypeFrameCount)) {
                val = FrameCount();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = FrameCount();
                return *this;
        }
        auto [fc, pe] = FrameCount::fromString(s);
        if (pe.isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse FrameCount from '%s'", s.cstr()));
                val = FrameCount();
                return *this;
        }
        val = fc;
        return *this;
}

DataStream &DataStream::operator>>(MediaDuration &val) {
        if (!readFrame(TypeMediaDuration)) {
                val = MediaDuration();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = MediaDuration();
                return *this;
        }
        // Default-constructed MediaDuration renders as "+" (empty
        // start + separator + empty length), which the parser
        // otherwise rejects.  Treat empty and the bare "+" placeholder
        // as the wire forms of a default-constructed value.
        if (s.isEmpty() || s == "+") {
                val = MediaDuration();
                return *this;
        }
        auto [md, pe] = MediaDuration::fromString(s);
        if (pe.isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse MediaDuration from '%s'", s.cstr()));
                val = MediaDuration();
                return *this;
        }
        val = md;
        return *this;
}

DataStream &DataStream::operator>>(Duration &val) {
        if (!readFrame(TypeDuration)) {
                val = Duration();
                return *this;
        }
        const int64_t ns = readInt64();
        if (_status != Ok) {
                val = Duration();
                return *this;
        }
        val = Duration::fromNanoseconds(ns);
        return *this;
}

DataStream &DataStream::operator>>(MacAddress &val) {
        if (!readFrame(TypeMacAddress)) {
                val = MacAddress();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = MacAddress();
                return *this;
        }
        auto [mac, parseErr] = MacAddress::fromString(s);
        if (parseErr.isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse MacAddress from '%s'", s.cstr()));
                val = MacAddress();
                return *this;
        }
        val = mac;
        return *this;
}

DataStream &DataStream::operator>>(EUI64 &val) {
        if (!readFrame(TypeEUI64)) {
                val = EUI64();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = EUI64();
                return *this;
        }
        auto [eui, parseErr] = EUI64::fromString(s);
        if (parseErr.isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse EUI64 from '%s'", s.cstr()));
                val = EUI64();
                return *this;
        }
        val = eui;
        return *this;
}

DataStream &DataStream::operator>>(StringList &val) {
        if (!readFrame(TypeStringList)) {
                val = StringList();
                return *this;
        }
        val = readStringListData();
        return *this;
}

DataStream &DataStream::operator>>(Url &val) {
        if (!readFrame(TypeUrl)) {
                val = Url();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = Url();
                return *this;
        }
        // A default-constructed Url serializes to an empty string; map
        // that back to a default-constructed Url rather than rejecting
        // it as a parse failure.
        if (s.isEmpty()) {
                val = Url();
                return *this;
        }
        Result<Url> r = Url::fromString(s);
        if (r.second().isError() || !r.first().isValid()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse Url from '%s'", s.cstr()));
                val = Url();
                return *this;
        }
        val = r.first();
        return *this;
}

DataStream &DataStream::operator>>(VideoCodec &val) {
        if (!readFrame(TypeVideoCodec)) {
                val = VideoCodec();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = VideoCodec();
                return *this;
        }
        auto r = VideoCodec::fromString(s);
        if (error(r).isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse VideoCodec from '%s'", s.cstr()));
                val = VideoCodec();
                return *this;
        }
        val = value(r);
        return *this;
}

DataStream &DataStream::operator>>(AudioCodec &val) {
        if (!readFrame(TypeAudioCodec)) {
                val = AudioCodec();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = AudioCodec();
                return *this;
        }
        auto r = AudioCodec::fromString(s);
        if (error(r).isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse AudioCodec from '%s'", s.cstr()));
                val = AudioCodec();
                return *this;
        }
        val = value(r);
        return *this;
}

DataStream &DataStream::operator>>(SocketAddress &val) {
        if (!readFrame(TypeSocketAddress)) {
                val = SocketAddress();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = SocketAddress();
                return *this;
        }
        // A default-constructed SocketAddress serializes to an empty
        // string; map it back to default rather than treating empty as
        // a parse failure.
        if (s.isEmpty()) {
                val = SocketAddress();
                return *this;
        }
        auto r = SocketAddress::fromString(s);
        if (error(r).isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse SocketAddress from '%s'", s.cstr()));
                val = SocketAddress();
                return *this;
        }
        val = value(r);
        return *this;
}

DataStream &DataStream::operator>>(SdpSession &val) {
        if (!readFrame(TypeSdpSession)) {
                val = SdpSession();
                return *this;
        }
        String s = readStringData();
        if (_status != Ok) {
                val = SdpSession();
                return *this;
        }
        auto r = SdpSession::fromString(s);
        if (error(r).isError()) {
                setError(ReadCorruptData, String::sprintf("Failed to parse SdpSession from SDP text"));
                val = SdpSession();
                return *this;
        }
        val = value(r);
        return *this;
}

#if PROMEKI_ENABLE_TLS
DataStream &DataStream::operator>>(SharedPtr<SslContext, false> &val) {
        // Pairs with the write overload — consume the tag, set @p val
        // to a default-constructed (null) Ptr.  The actual context
        // can't survive serialization; see the write overload.
        if (!readFrame(TypeSslContext)) {
                val = SharedPtr<SslContext, false>();
                return *this;
        }
        val = SharedPtr<SslContext, false>();
        return *this;
}
#endif

// ============================================================================
// Variant read — registry-driven dispatch
//
// The frame header tells us which DataType this Variant holds.  We peek
// the header so the per-type @c operator>> overload can re-read it
// through @c readFrame; the cached header lets that re-read succeed
// without touching the device a second time, so the path works on
// sequential devices (sockets, pipes) just as well as seekable ones.
//
// Tags the local registry doesn't know about hit the forward-compat
// path: drain the cached header, skip the body via its declared size,
// and yield an invalid Variant — older readers can drain streams
// produced by newer writers without going into a corrupt-data state.
// ============================================================================

DataStream &DataStream::operator>>(Variant &val) {
        TypeId   tag = static_cast<TypeId>(0);
        uint16_t ver = 0;
        uint32_t sz  = 0;
        if (!peekFrameHeader(tag, ver, sz)) {
                val = Variant();
                return *this;
        }
        const DataType       dt(tag);
        const DataType::Data *td = dt.data();
        if (td != nullptr && td->ops.readStream != nullptr && td->ops.defaultConstruct != nullptr) {
                val = Variant::readFromStream(*this, dt);
                return *this;
        }
        // Unknown tag — consume the cached header (so the body bytes
        // line up) and skip past the body for forward compatibility.
        readFrameHeader(tag, ver, sz);
        skipFrameBody(sz);
        val = Variant();
        return *this;
}

PROMEKI_NAMESPACE_END
