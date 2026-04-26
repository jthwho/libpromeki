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
#include <promeki/enumlist.h>
#include <promeki/mediatimestamp.h>
#include <promeki/macaddress.h>
#include <promeki/eui64.h>

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

DataStream::DataStream(IODevice *device) : _device(device) { }

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
        if(!readBytes(buf, HeaderSize)) return;
        if(std::memcmp(buf, Magic, sizeof(Magic)) != 0) {
                setError(ReadCorruptData, String("bad magic bytes in header"));
                return;
        }
        _version = static_cast<uint16_t>((buf[4] << 8) | buf[5]);
        switch(buf[6]) {
                case 'B': _byteOrder = BigEndian; break;
                case 'L': _byteOrder = LittleEndian; break;
                default:
                        setError(ReadCorruptData,
                                String::sprintf(
                                        "invalid byte-order marker 0x%02X in header",
                                        static_cast<unsigned>(buf[6])));
                        return;
        }
        // Reserved bytes must be zero. Any non-zero value indicates a
        // future header extension this reader doesn't understand, so we
        // fail loudly rather than silently mis-parse.
        for(size_t i = 7; i < HeaderSize; ++i) {
                if(buf[i] != 0) {
                        setError(ReadCorruptData,
                                String::sprintf(
                                        "non-zero reserved byte 0x%02X at header offset %zu",
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
        if(_status != Ok) return;
        _status = s;
        _errorContext = std::move(ctx);
}

void DataStream::resetStatus() {
        _status = Ok;
        _errorContext = String();
}

Error DataStream::toError() const {
        switch(_status) {
                case Ok:              return Error::Ok;
                case ReadPastEnd:     return Error::EndOfFile;
                case ReadCorruptData: return Error::CorruptData;
                case WriteFailed:     return Error::IOError;
        }
        return Error::Invalid;
}

bool DataStream::atEnd() const {
        if(_device != nullptr) return _device->atEnd();
        return true;
}

// ============================================================================
// Type tags
// ============================================================================

// Tags are a fixed 16-bit width on the wire so the library can grow
// past 256 built-in types and still leave room for user extensions
// (see @c UserTypeIdBegin).  Byte order follows the stream's
// @c _byteOrder — the same rule every other multi-byte primitive
// observes — so a BigEndian and LittleEndian reader both decode the
// identical tag without a dedicated tag-order marker.

void DataStream::writeTag(TypeId id) {
        writeUInt16(static_cast<uint16_t>(id));
}

bool DataStream::readTag(TypeId expected) {
        if(_status != Ok) return false;
        uint16_t tag = readUInt16();
        if(_status != Ok) return false;
        if(tag != static_cast<uint16_t>(expected)) {
                setError(ReadCorruptData,
                        String::sprintf("expected tag 0x%04X, got 0x%04X",
                                static_cast<unsigned>(expected),
                                static_cast<unsigned>(tag)));
                return false;
        }
        return true;
}

uint16_t DataStream::readAnyTag() {
        if(_status != Ok) return 0;
        uint16_t tag = readUInt16();
        if(_status != Ok) return 0;
        return tag;
}

// ============================================================================
// Internal read/write helpers
// ============================================================================

bool DataStream::readBytes(void *buf, size_t len) {
        if(_status != Ok) return false;
        if(len == 0) return true;
        if(_device == nullptr) {
                setError(ReadPastEnd, String("no device attached"));
                return false;
        }
        size_t total = 0;
        uint8_t *dst = static_cast<uint8_t *>(buf);
        while(total < len) {
                int64_t n = _device->read(dst + total, static_cast<int64_t>(len - total));
                if(n <= 0) {
                        setError(ReadPastEnd,
                                String::sprintf("short read: wanted %zu bytes, got %zu",
                                        len, total));
                        return false;
                }
                total += static_cast<size_t>(n);
        }
        return true;
}

bool DataStream::writeBytes(const void *buf, size_t len) {
        if(_status != Ok) return false;
        if(len == 0) return true;
        if(_device == nullptr) {
                setError(WriteFailed, String("no device attached"));
                return false;
        }
        size_t total = 0;
        const uint8_t *src = static_cast<const uint8_t *>(buf);
        while(total < len) {
                int64_t n = _device->write(src + total, static_cast<int64_t>(len - total));
                if(n <= 0) {
                        setError(WriteFailed,
                                String::sprintf("short write: wanted %zu bytes, wrote %zu",
                                        len, total));
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
        if(_status != Ok) return -1;
        if(len == 0) return 0;
        if(_device == nullptr) {
                setError(ReadPastEnd, String("no device attached"));
                return -1;
        }
        int64_t n = _device->read(buf, static_cast<int64_t>(len));
        if(n < 0) {
                setError(ReadPastEnd, String("device read returned error"));
                return -1;
        }
        return static_cast<ssize_t>(n);
}

ssize_t DataStream::writeRawData(const void *buf, size_t len) {
        if(_status != Ok) return -1;
        if(len == 0) return 0;
        if(_device == nullptr) {
                setError(WriteFailed, String("no device attached"));
                return -1;
        }
        int64_t n = _device->write(buf, static_cast<int64_t>(len));
        if(n < 0) {
                setError(WriteFailed, String("device write returned error"));
                return -1;
        }
        return static_cast<ssize_t>(n);
}

ssize_t DataStream::skipRawData(size_t len) {
        if(_status != Ok) return -1;
        if(len == 0) return 0;
        if(_device == nullptr) {
                setError(ReadPastEnd, String("no device attached"));
                return -1;
        }
        if(!_device->isSequential()) {
                int64_t cur = _device->pos();
                Error err = _device->seek(cur + static_cast<int64_t>(len));
                if(err.isError()) {
                        setError(ReadPastEnd, String("seek failed in skipRawData"));
                        return -1;
                }
                return static_cast<ssize_t>(len);
        }
        // Sequential: read and discard
        uint8_t tmp[1024];
        size_t total = 0;
        while(total < len) {
                size_t chunk = len - total;
                if(chunk > sizeof(tmp)) chunk = sizeof(tmp);
                int64_t n = _device->read(tmp, static_cast<int64_t>(chunk));
                if(n <= 0) {
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

void DataStream::writeInt8(int8_t val)   { writeBytes(&val, 1); }
void DataStream::writeUInt8(uint8_t val) { writeBytes(&val, 1); }

void DataStream::writeInt16(int16_t val)   { swapIfNeeded(val); writeBytes(&val, sizeof(val)); }
void DataStream::writeUInt16(uint16_t val) { swapIfNeeded(val); writeBytes(&val, sizeof(val)); }
void DataStream::writeInt32(int32_t val)   { swapIfNeeded(val); writeBytes(&val, sizeof(val)); }
void DataStream::writeUInt32(uint32_t val) { swapIfNeeded(val); writeBytes(&val, sizeof(val)); }
void DataStream::writeInt64(int64_t val)   { swapIfNeeded(val); writeBytes(&val, sizeof(val)); }
void DataStream::writeUInt64(uint64_t val) { swapIfNeeded(val); writeBytes(&val, sizeof(val)); }

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
        if(val.isEmpty()) {
                writeUInt32(0);
                return;
        }
        const String *src = &val;
        String converted;
        if(val.encoding() == String::Latin1) {
                bool needsConvert = false;
                for(size_t i = 0; i < val.byteCount(); ++i) {
                        if(val.byteAt(i) > 0x7F) {
                                needsConvert = true;
                                break;
                        }
                }
                if(needsConvert) {
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
        if(len > 0) writeBytes(val.data(), len);
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
        if(n > 0) writeBytes(val.raw(), n);
}

void DataStream::writeDateTimeData(const DateTime &val) {
        int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                val.value().time_since_epoch()).count();
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
        // Color::toString() produces a lossless ModelFormat representation
        // that round-trips through fromString().
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
        for(size_t i = 0; i < values.size(); ++i) {
                *this << static_cast<int32_t>(values[i]);
        }
}

void DataStream::writeStringListData(const StringList &val) {
        // Use a tagged uint32_t count to match the List/Map/Set convention.
        *this << static_cast<uint32_t>(val.size());
        for(const String &s : val) writeStringData(s);
}

// ---------------------------------------------------------------------------
// Untagged read helpers
// ---------------------------------------------------------------------------

int8_t   DataStream::readInt8()   { int8_t   v = 0; readBytes(&v, 1); return v; }
uint8_t  DataStream::readUInt8()  { uint8_t  v = 0; readBytes(&v, 1); return v; }

int16_t  DataStream::readInt16()  { int16_t  v = 0; if(readBytes(&v, sizeof(v))) swapIfNeeded(v); else v = 0; return v; }
uint16_t DataStream::readUInt16() { uint16_t v = 0; if(readBytes(&v, sizeof(v))) swapIfNeeded(v); else v = 0; return v; }
int32_t  DataStream::readInt32()  { int32_t  v = 0; if(readBytes(&v, sizeof(v))) swapIfNeeded(v); else v = 0; return v; }
uint32_t DataStream::readUInt32() { uint32_t v = 0; if(readBytes(&v, sizeof(v))) swapIfNeeded(v); else v = 0; return v; }
int64_t  DataStream::readInt64()  { int64_t  v = 0; if(readBytes(&v, sizeof(v))) swapIfNeeded(v); else v = 0; return v; }
uint64_t DataStream::readUInt64() { uint64_t v = 0; if(readBytes(&v, sizeof(v))) swapIfNeeded(v); else v = 0; return v; }

float DataStream::readFloat() {
        uint32_t tmp = 0;
        if(readBytes(&tmp, sizeof(tmp))) {
                swapIfNeeded(tmp);
                float val;
                std::memcpy(&val, &tmp, sizeof(val));
                return val;
        }
        return 0.0f;
}

double DataStream::readDouble() {
        uint64_t tmp = 0;
        if(readBytes(&tmp, sizeof(tmp))) {
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
        if(_status != Ok) return String();
        if(len == 0) return String();
        if(len > 256 * 1024 * 1024) {
                setError(ReadCorruptData, String("String length exceeds sanity limit"));
                return String();
        }
        std::string buf(len, '\0');
        if(!readBytes(buf.data(), len)) return String();
        bool allAscii = true;
        for(size_t i = 0; i < len; ++i) {
                if(static_cast<uint8_t>(buf[i]) > 0x7F) {
                        allAscii = false;
                        break;
                }
        }
        if(allAscii) return String(buf.c_str(), len);
        return String::fromUtf8(buf.c_str(), len);
}

Buffer DataStream::readBufferData() {
        uint32_t len = readUInt32();
        if(_status != Ok) return Buffer();
        if(len == 0) return Buffer();
        if(len > 256 * 1024 * 1024) {
                setError(ReadCorruptData, String("Buffer length exceeds sanity limit"));
                return Buffer();
        }
        Buffer buf(len);
        if(!readBytes(buf.data(), len)) return Buffer();
        buf.setSize(len);
        return buf;
}

UUID DataStream::readUUIDData() {
        UUID::DataFormat raw;
        if(!readBytes(raw.data(), 16)) return UUID();
        return UUID(raw);
}

UMID DataStream::readUMIDData() {
        uint8_t n = readUInt8();
        if(_status != Ok) return UMID();
        if(n == 0) return UMID();
        if(n != UMID::BasicSize && n != UMID::ExtendedSize) {
                setError(ReadCorruptData,
                        String::sprintf("UMID payload length %u is neither 32 nor 64",
                                static_cast<unsigned>(n)));
                return UMID();
        }
        uint8_t buf[UMID::ExtendedSize];
        if(!readBytes(buf, n)) return UMID();
        return UMID::fromBytes(buf, n);
}

DateTime DataStream::readDateTimeData() {
        int64_t ns = 0;
        *this >> ns;
        if(_status != Ok) return DateTime();
        DateTime::Value tp{std::chrono::duration_cast<
                std::chrono::system_clock::duration>(
                std::chrono::nanoseconds(ns))};
        return DateTime(tp);
}

TimeStamp DataStream::readTimeStampData() {
        int64_t ns = 0;
        *this >> ns;
        if(_status != Ok) return TimeStamp();
        TimeStamp::Value tp{std::chrono::duration_cast<
                TimeStamp::Clock::duration>(
                std::chrono::nanoseconds(ns))};
        return TimeStamp(tp);
}

FrameRate DataStream::readFrameRateData() {
        uint32_t num = 0, den = 0;
        *this >> num >> den;
        if(_status != Ok) return FrameRate();
        return FrameRate(FrameRate::RationalType(num, den));
}

VideoFormat DataStream::readVideoFormatData() {
        String s = readStringData();
        if(_status != Ok) return VideoFormat();
        auto [vf, err] = VideoFormat::fromString(s);
        if(err.isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse VideoFormat from '%s'", s.cstr()));
                return VideoFormat();
        }
        return vf;
}

Timecode DataStream::readTimecodeData() {
        String s = readStringData();
        if(_status != Ok) return Timecode();
        auto [tc, err] = Timecode::fromString(s);
        if(err.isError()) {
                // Preserve the parse failure as corrupt data rather than
                // silently yielding an empty Timecode — callers need to
                // know their on-disk data is unparseable.
                setError(ReadCorruptData,
                        String("Timecode::fromString failed: ") + s);
                return Timecode();
        }
        return tc;
}

Color DataStream::readColorData() {
        String s = readStringData();
        if(_status != Ok) return Color();
        return Color::fromString(s);
}

ColorModel DataStream::readColorModelData() {
        String s = readStringData();
        if(_status != Ok) return ColorModel();
        return ColorModel::lookup(s);
}

MemSpace DataStream::readMemSpaceData() {
        uint32_t id = 0;
        *this >> id;
        if(_status != Ok) return MemSpace();
        return MemSpace(static_cast<MemSpace::ID>(id));
}

PixelMemLayout DataStream::readPixelMemLayoutData() {
        String s = readStringData();
        if(_status != Ok) return PixelMemLayout();
        return PixelMemLayout::lookup(s);
}

PixelFormat DataStream::readPixelFormatData() {
        String s = readStringData();
        if(_status != Ok) return PixelFormat();
        return PixelFormat::lookup(s);
}

AudioFormat DataStream::readAudioFormatData() {
        String s = readStringData();
        if(_status != Ok) return AudioFormat();
        return value(AudioFormat::lookup(s));
}

Enum DataStream::readEnumData() {
        String s = readStringData();
        if(_status != Ok) return Enum();
        Error err;
        Enum e = Enum::lookup(s, &err);
        if(err.isError()) {
                setError(ReadCorruptData,
                        String("Enum::lookup failed: ") + s);
                return Enum();
        }
        return e;
}

EnumList DataStream::readEnumListData() {
        String typeName = readStringData();
        if(_status != Ok) return EnumList();
        Enum::Type type = Enum::findType(typeName);
        if(!type.isValid()) {
                setError(ReadCorruptData,
                        String("EnumList: unknown type '") + typeName + "'");
                return EnumList();
        }
        uint32_t count = 0;
        *this >> count;  // tagged read
        if(_status != Ok) return EnumList();
        if(count > 256 * 1024 * 1024) {
                setError(ReadCorruptData,
                        String("EnumList count exceeds sanity limit"));
                return EnumList();
        }
        EnumList out(type);
        for(uint32_t i = 0; i < count; ++i) {
                int32_t v = 0;
                *this >> v;  // tagged read
                if(_status != Ok) return EnumList();
                out.append(static_cast<int>(v));
        }
        return out;
}

StringList DataStream::readStringListData() {
        uint32_t count = 0;
        *this >> count; // tagged read, matches writeStringListData
        if(_status != Ok) return StringList();
        if(count > 256 * 1024 * 1024) {
                setError(ReadCorruptData, String("StringList count exceeds sanity limit"));
                return StringList();
        }
        StringList list;
        list.reserve(count);
        for(uint32_t i = 0; i < count; ++i) {
                String s = readStringData();
                if(_status != Ok) return StringList();
                list.pushToBack(std::move(s));
        }
        return list;
}

// ============================================================================
// Write operators — primitives (tagged)
// ============================================================================

DataStream &DataStream::operator<<(int8_t val) {
        writeTag(TypeInt8);
        writeInt8(val);
        return *this;
}

DataStream &DataStream::operator<<(uint8_t val) {
        writeTag(TypeUInt8);
        writeUInt8(val);
        return *this;
}

DataStream &DataStream::operator<<(int16_t val) {
        writeTag(TypeInt16);
        writeInt16(val);
        return *this;
}

DataStream &DataStream::operator<<(uint16_t val) {
        writeTag(TypeUInt16);
        writeUInt16(val);
        return *this;
}

DataStream &DataStream::operator<<(int32_t val) {
        writeTag(TypeInt32);
        writeInt32(val);
        return *this;
}

DataStream &DataStream::operator<<(uint32_t val) {
        writeTag(TypeUInt32);
        writeUInt32(val);
        return *this;
}

DataStream &DataStream::operator<<(int64_t val) {
        writeTag(TypeInt64);
        writeInt64(val);
        return *this;
}

DataStream &DataStream::operator<<(uint64_t val) {
        writeTag(TypeUInt64);
        writeUInt64(val);
        return *this;
}

DataStream &DataStream::operator<<(float val) {
        writeTag(TypeFloat);
        writeFloat(val);
        return *this;
}

DataStream &DataStream::operator<<(double val) {
        writeTag(TypeDouble);
        writeDouble(val);
        return *this;
}

DataStream &DataStream::operator<<(bool val) {
        writeTag(TypeBool);
        writeBool(val);
        return *this;
}

// ============================================================================
// Write operators — complex types (tagged)
// ============================================================================

DataStream &DataStream::operator<<(const String &val) {
        writeTag(TypeString);
        writeStringData(val);
        return *this;
}

DataStream &DataStream::operator<<(const Buffer &val) {
        writeTag(TypeBuffer);
        writeBufferData(val);
        return *this;
}

DataStream &DataStream::operator<<(const Buffer::Ptr &val) {
        // Null Buffer::Ptr is encoded as a TypeInvalid marker so it
        // round-trips as null (not as an empty Buffer). Non-null uses
        // the same TypeBuffer framing as direct Buffer, so interop with
        // a direct Buffer read still works for the non-null case.
        if(!val) {
                writeTag(TypeInvalid);
                return *this;
        }
        writeTag(TypeBuffer);
        writeBufferData(*val);
        return *this;
}

// ============================================================================
// Write operators — data objects
// ============================================================================

DataStream &DataStream::operator<<(const UUID &val) {
        writeTag(TypeUUID);
        writeUUIDData(val);
        return *this;
}

DataStream &DataStream::operator<<(const UMID &val) {
        writeTag(TypeUMID);
        writeUMIDData(val);
        return *this;
}

DataStream &DataStream::operator<<(const DateTime &val) {
        writeTag(TypeDateTime);
        writeDateTimeData(val);
        return *this;
}

DataStream &DataStream::operator<<(const TimeStamp &val) {
        writeTag(TypeTimeStamp);
        writeTimeStampData(val);
        return *this;
}

DataStream &DataStream::operator<<(const FrameRate &val) {
        writeTag(TypeFrameRate);
        writeFrameRateData(val);
        return *this;
}

DataStream &DataStream::operator<<(const VideoFormat &val) {
        writeTag(TypeVideoFormat);
        writeVideoFormatData(val);
        return *this;
}

DataStream &DataStream::operator<<(const Timecode &val) {
        writeTag(TypeTimecode);
        writeTimecodeData(val);
        return *this;
}

DataStream &DataStream::operator<<(const Color &val) {
        writeTag(TypeColor);
        writeColorData(val);
        return *this;
}

DataStream &DataStream::operator<<(const ColorModel &val) {
        writeTag(TypeColorModel);
        writeColorModelData(val);
        return *this;
}

DataStream &DataStream::operator<<(const MemSpace &val) {
        writeTag(TypeMemSpace);
        writeMemSpaceData(val);
        return *this;
}

DataStream &DataStream::operator<<(const PixelMemLayout &val) {
        writeTag(TypePixelMemLayout);
        writePixelMemLayoutData(val);
        return *this;
}

DataStream &DataStream::operator<<(const PixelFormat &val) {
        writeTag(TypePixelFormat);
        writePixelFormatData(val);
        return *this;
}

DataStream &DataStream::operator<<(const AudioFormat &val) {
        writeTag(TypeAudioFormat);
        writeAudioFormatData(val);
        return *this;
}

DataStream &DataStream::operator<<(const Enum &val) {
        writeTag(TypeEnum);
        writeEnumData(val);
        return *this;
}

DataStream &DataStream::operator<<(const EnumList &val) {
        writeTag(TypeEnumList);
        writeEnumListData(val);
        return *this;
}

DataStream &DataStream::operator<<(const MediaTimeStamp &val) {
        writeTag(TypeMediaTimeStamp);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const FrameNumber &val) {
        writeTag(TypeFrameNumber);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const FrameCount &val) {
        writeTag(TypeFrameCount);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const MediaDuration &val) {
        writeTag(TypeMediaDuration);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const Duration &val) {
        // Duration is a simple int64 wall-clock nanoseconds value —
        // encode it directly rather than round-tripping through a
        // string so the wire form is both compact and preserves the
        // full 64-bit precision without parsing cost.
        writeTag(TypeDuration);
        writeInt64(val.nanoseconds());
        return *this;
}

DataStream &DataStream::operator<<(const Url &val) {
        writeTag(TypeUrl);
        // Serialize via the canonical string form — round-trips
        // through Url::fromString/toString preserve every component
        // we care about, and the string form is stable across
        // library versions.
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const MacAddress &val) {
        writeTag(TypeMacAddress);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const EUI64 &val) {
        writeTag(TypeEUI64);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const StringList &val) {
        writeTag(TypeStringList);
        writeStringListData(val);
        return *this;
}

DataStream &DataStream::operator<<(const VideoCodec &val) {
        // VideoCodec round-trips through its "Codec[:Backend]" string
        // form (see VideoCodec::toString / fromString), which preserves
        // both the codec identity and the backend pin when one is set.
        writeTag(TypeVideoCodec);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const AudioCodec &val) {
        // AudioCodec uses the same "Codec[:Backend]" string round-trip
        // as VideoCodec.
        writeTag(TypeAudioCodec);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const SocketAddress &val) {
        // SocketAddress round-trips through "host:port" (IPv6 uses the
        // bracketed form, e.g. "[::1]:5004") via toString / fromString.
        writeTag(TypeSocketAddress);
        writeStringData(val.toString());
        return *this;
}

DataStream &DataStream::operator<<(const SdpSession &val) {
        // SdpSession is serialised as an RFC 4566 SDP text blob — the
        // canonical external form understood by every SDP consumer.
        // Unknown extension attributes the parser doesn't recognise
        // may not survive round-trips, but the core session / media
        // description is lossless.
        writeTag(TypeSdpSession);
        writeStringData(val.toString());
        return *this;
}

// ============================================================================
// Variant write — dispatches by the Variant's current type
//
// The switch body is generated from @c PROMEKI_VARIANT_TYPES so every
// type registered in the Variant type list is guaranteed to have a
// case here.  The generated case delegates through
// @c writeVariantValue<T> — its @c *this << v.get<T>() expression
// fails to compile if there is no @c operator<<(DataStream&, const T&)
// for @c T, which is exactly the class of bug we want caught at build
// time rather than surfacing as a runtime @c WriteFailed.  Adding a
// new Variant type without the matching operator is now a compile
// error instead of silent data loss.
// ============================================================================

namespace {
        template <typename T>
        void writeVariantValue(DataStream &s, const Variant &v) {
                if constexpr (std::is_same_v<T, std::monostate>) {
                        // TypeInvalid carries no payload; only the tag.
                        s.writeTag(DataStream::TypeInvalid);
                } else {
                        // Important: the @c s << v.get<T>() form would
                        // silently satisfy any missing specific
                        // operator via the converting @c Variant ctor
                        // and then recurse right back here at runtime.
                        // The explicit invocation below uses ordinary
                        // overload resolution (no pointer-to-member
                        // casts needed — there's no ambiguity to
                        // disambiguate) so the coverage static_asserts
                        // below are what actually guard the fallback
                        // hazard at build time.
                        s << v.get<T>();
                }
        }
} // namespace


DataStream &DataStream::operator<<(const Variant &val) {
        switch(val.type()) {
#define X(name, type) \
                case Variant::name: writeVariantValue<type>(*this, val); break;
                PROMEKI_VARIANT_TYPES
#undef X
        }
        return *this;
}

// ============================================================================
// Read operators — primitives (tagged)
// ============================================================================

DataStream &DataStream::operator>>(int8_t &val) {
        if(!readTag(TypeInt8)) { val = 0; return *this; }
        val = readInt8();
        return *this;
}

DataStream &DataStream::operator>>(uint8_t &val) {
        if(!readTag(TypeUInt8)) { val = 0; return *this; }
        val = readUInt8();
        return *this;
}

DataStream &DataStream::operator>>(int16_t &val) {
        if(!readTag(TypeInt16)) { val = 0; return *this; }
        val = readInt16();
        return *this;
}

DataStream &DataStream::operator>>(uint16_t &val) {
        if(!readTag(TypeUInt16)) { val = 0; return *this; }
        val = readUInt16();
        return *this;
}

DataStream &DataStream::operator>>(int32_t &val) {
        if(!readTag(TypeInt32)) { val = 0; return *this; }
        val = readInt32();
        return *this;
}

DataStream &DataStream::operator>>(uint32_t &val) {
        if(!readTag(TypeUInt32)) { val = 0; return *this; }
        val = readUInt32();
        return *this;
}

DataStream &DataStream::operator>>(int64_t &val) {
        if(!readTag(TypeInt64)) { val = 0; return *this; }
        val = readInt64();
        return *this;
}

DataStream &DataStream::operator>>(uint64_t &val) {
        if(!readTag(TypeUInt64)) { val = 0; return *this; }
        val = readUInt64();
        return *this;
}

DataStream &DataStream::operator>>(float &val) {
        if(!readTag(TypeFloat)) { val = 0.0f; return *this; }
        val = readFloat();
        return *this;
}

DataStream &DataStream::operator>>(double &val) {
        if(!readTag(TypeDouble)) { val = 0.0; return *this; }
        val = readDouble();
        return *this;
}

DataStream &DataStream::operator>>(bool &val) {
        if(!readTag(TypeBool)) { val = false; return *this; }
        val = readBoolValue();
        return *this;
}

// ============================================================================
// Read operators — complex types (tagged)
// ============================================================================

DataStream &DataStream::operator>>(String &val) {
        if(!readTag(TypeString)) { val = String(); return *this; }
        val = readStringData();
        return *this;
}

DataStream &DataStream::operator>>(Buffer &val) {
        if(!readTag(TypeBuffer)) { val = Buffer(); return *this; }
        val = readBufferData();
        return *this;
}

DataStream &DataStream::operator>>(Buffer::Ptr &val) {
        // Peek the tag: TypeInvalid → null Ptr, TypeBuffer → allocated Ptr,
        // anything else → ReadCorruptData.
        uint16_t tag = readAnyTag();
        if(_status != Ok) { val = Buffer::Ptr(); return *this; }
        if(tag == TypeInvalid) {
                val = Buffer::Ptr();
                return *this;
        }
        if(tag != TypeBuffer) {
                setError(ReadCorruptData,
                        String::sprintf(
                                "expected tag 0x%04X (TypeBuffer) or 0x%04X (TypeInvalid), got 0x%04X",
                                static_cast<unsigned>(TypeBuffer),
                                static_cast<unsigned>(TypeInvalid),
                                static_cast<unsigned>(tag)));
                val = Buffer::Ptr();
                return *this;
        }
        Buffer buf = readBufferData();
        if(_status != Ok) { val = Buffer::Ptr(); return *this; }
        val = Buffer::Ptr::create(std::move(buf));
        return *this;
}

// ============================================================================
// Read operators — data objects
// ============================================================================

DataStream &DataStream::operator>>(UUID &val) {
        if(!readTag(TypeUUID)) { val = UUID(); return *this; }
        val = readUUIDData();
        return *this;
}

DataStream &DataStream::operator>>(UMID &val) {
        if(!readTag(TypeUMID)) { val = UMID(); return *this; }
        val = readUMIDData();
        return *this;
}

DataStream &DataStream::operator>>(DateTime &val) {
        if(!readTag(TypeDateTime)) { val = DateTime(); return *this; }
        val = readDateTimeData();
        return *this;
}

DataStream &DataStream::operator>>(TimeStamp &val) {
        if(!readTag(TypeTimeStamp)) { val = TimeStamp(); return *this; }
        val = readTimeStampData();
        return *this;
}

DataStream &DataStream::operator>>(FrameRate &val) {
        if(!readTag(TypeFrameRate)) { val = FrameRate(); return *this; }
        val = readFrameRateData();
        return *this;
}

DataStream &DataStream::operator>>(VideoFormat &val) {
        if(!readTag(TypeVideoFormat)) { val = VideoFormat(); return *this; }
        val = readVideoFormatData();
        return *this;
}

DataStream &DataStream::operator>>(Timecode &val) {
        if(!readTag(TypeTimecode)) { val = Timecode(); return *this; }
        val = readTimecodeData();
        return *this;
}

DataStream &DataStream::operator>>(Color &val) {
        if(!readTag(TypeColor)) { val = Color(); return *this; }
        val = readColorData();
        return *this;
}

DataStream &DataStream::operator>>(ColorModel &val) {
        if(!readTag(TypeColorModel)) { val = ColorModel(); return *this; }
        val = readColorModelData();
        return *this;
}

DataStream &DataStream::operator>>(MemSpace &val) {
        if(!readTag(TypeMemSpace)) { val = MemSpace(); return *this; }
        val = readMemSpaceData();
        return *this;
}

DataStream &DataStream::operator>>(PixelMemLayout &val) {
        if(!readTag(TypePixelMemLayout)) { val = PixelMemLayout(); return *this; }
        val = readPixelMemLayoutData();
        return *this;
}

DataStream &DataStream::operator>>(PixelFormat &val) {
        if(!readTag(TypePixelFormat)) { val = PixelFormat(); return *this; }
        val = readPixelFormatData();
        return *this;
}

DataStream &DataStream::operator>>(AudioFormat &val) {
        if(!readTag(TypeAudioFormat)) { val = AudioFormat(); return *this; }
        val = readAudioFormatData();
        return *this;
}

DataStream &DataStream::operator>>(Enum &val) {
        if(!readTag(TypeEnum)) { val = Enum(); return *this; }
        val = readEnumData();
        return *this;
}

DataStream &DataStream::operator>>(EnumList &val) {
        if(!readTag(TypeEnumList)) { val = EnumList(); return *this; }
        val = readEnumListData();
        return *this;
}

DataStream &DataStream::operator>>(MediaTimeStamp &val) {
        if(!readTag(TypeMediaTimeStamp)) { val = MediaTimeStamp(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = MediaTimeStamp(); return *this; }
        auto [mts, parseErr] = MediaTimeStamp::fromString(s);
        if(parseErr.isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse MediaTimeStamp from '%s'", s.cstr()));
                val = MediaTimeStamp();
                return *this;
        }
        val = mts;
        return *this;
}

DataStream &DataStream::operator>>(FrameNumber &val) {
        if(!readTag(TypeFrameNumber)) { val = FrameNumber(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = FrameNumber(); return *this; }
        Error pe;
        FrameNumber fn = FrameNumber::fromString(s, &pe);
        if(pe.isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse FrameNumber from '%s'", s.cstr()));
                val = FrameNumber();
                return *this;
        }
        val = fn;
        return *this;
}

DataStream &DataStream::operator>>(FrameCount &val) {
        if(!readTag(TypeFrameCount)) { val = FrameCount(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = FrameCount(); return *this; }
        Error pe;
        FrameCount fc = FrameCount::fromString(s, &pe);
        if(pe.isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse FrameCount from '%s'", s.cstr()));
                val = FrameCount();
                return *this;
        }
        val = fc;
        return *this;
}

DataStream &DataStream::operator>>(MediaDuration &val) {
        if(!readTag(TypeMediaDuration)) { val = MediaDuration(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = MediaDuration(); return *this; }
        Error pe;
        MediaDuration md = MediaDuration::fromString(s, &pe);
        if(pe.isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse MediaDuration from '%s'", s.cstr()));
                val = MediaDuration();
                return *this;
        }
        val = md;
        return *this;
}

DataStream &DataStream::operator>>(Duration &val) {
        if(!readTag(TypeDuration)) { val = Duration(); return *this; }
        const int64_t ns = readInt64();
        if(_status != Ok) { val = Duration(); return *this; }
        val = Duration::fromNanoseconds(ns);
        return *this;
}

DataStream &DataStream::operator>>(MacAddress &val) {
        if(!readTag(TypeMacAddress)) { val = MacAddress(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = MacAddress(); return *this; }
        auto [mac, parseErr] = MacAddress::fromString(s);
        if(parseErr.isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse MacAddress from '%s'", s.cstr()));
                val = MacAddress();
                return *this;
        }
        val = mac;
        return *this;
}

DataStream &DataStream::operator>>(EUI64 &val) {
        if(!readTag(TypeEUI64)) { val = EUI64(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = EUI64(); return *this; }
        auto [eui, parseErr] = EUI64::fromString(s);
        if(parseErr.isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse EUI64 from '%s'", s.cstr()));
                val = EUI64();
                return *this;
        }
        val = eui;
        return *this;
}

DataStream &DataStream::operator>>(StringList &val) {
        if(!readTag(TypeStringList)) { val = StringList(); return *this; }
        val = readStringListData();
        return *this;
}

DataStream &DataStream::operator>>(Url &val) {
        if(!readTag(TypeUrl)) { val = Url(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = Url(); return *this; }
        Result<Url> r = Url::fromString(s);
        if(r.second().isError() || !r.first().isValid()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse Url from '%s'", s.cstr()));
                val = Url();
                return *this;
        }
        val = r.first();
        return *this;
}

DataStream &DataStream::operator>>(VideoCodec &val) {
        if(!readTag(TypeVideoCodec)) { val = VideoCodec(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = VideoCodec(); return *this; }
        auto r = VideoCodec::fromString(s);
        if(error(r).isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse VideoCodec from '%s'", s.cstr()));
                val = VideoCodec();
                return *this;
        }
        val = value(r);
        return *this;
}

DataStream &DataStream::operator>>(AudioCodec &val) {
        if(!readTag(TypeAudioCodec)) { val = AudioCodec(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = AudioCodec(); return *this; }
        auto r = AudioCodec::fromString(s);
        if(error(r).isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse AudioCodec from '%s'", s.cstr()));
                val = AudioCodec();
                return *this;
        }
        val = value(r);
        return *this;
}

DataStream &DataStream::operator>>(SocketAddress &val) {
        if(!readTag(TypeSocketAddress)) { val = SocketAddress(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = SocketAddress(); return *this; }
        auto r = SocketAddress::fromString(s);
        if(error(r).isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse SocketAddress from '%s'", s.cstr()));
                val = SocketAddress();
                return *this;
        }
        val = value(r);
        return *this;
}

DataStream &DataStream::operator>>(SdpSession &val) {
        if(!readTag(TypeSdpSession)) { val = SdpSession(); return *this; }
        String s = readStringData();
        if(_status != Ok) { val = SdpSession(); return *this; }
        auto r = SdpSession::fromString(s);
        if(error(r).isError()) {
                setError(ReadCorruptData,
                        String::sprintf("Failed to parse SdpSession from SDP text"));
                val = SdpSession();
                return *this;
        }
        val = value(r);
        return *this;
}

// ============================================================================
// Variant read — peeks the tag and dispatches to the right type
// ============================================================================

void DataStream::readVariantPayload(TypeId id, Variant &val) {
        switch(id) {
                case TypeInvalid:     val = Variant(); break;
                case TypeBool:        val = readBoolValue(); break;
                case TypeUInt8:       val = readUInt8(); break;
                case TypeInt8:        val = readInt8(); break;
                case TypeUInt16:      val = readUInt16(); break;
                case TypeInt16:       val = readInt16(); break;
                case TypeUInt32:      val = readUInt32(); break;
                case TypeInt32:       val = readInt32(); break;
                case TypeUInt64:      val = readUInt64(); break;
                case TypeInt64:       val = readInt64(); break;
                case TypeFloat:       val = readFloat(); break;
                case TypeDouble:      val = readDouble(); break;
                case TypeString:      val = readStringData(); break;
                case TypeUUID:        val = readUUIDData(); break;
                case TypeUMID:        val = readUMIDData(); break;
                case TypeDateTime:    val = readDateTimeData(); break;
                case TypeTimeStamp:   val = readTimeStampData(); break;
                case TypeSize2D: {
                        // Outer tag already consumed; inner values are
                        // tagged primitives read via operator>>.
                        uint32_t w = 0, h = 0;
                        *this >> w >> h;
                        if(_status != Ok) { val = Variant(); break; }
                        val = Size2Du32(w, h);
                        break;
                }
                case TypeRational: {
                        int32_t num = 0, den = 1;
                        *this >> num >> den;
                        if(_status != Ok) { val = Variant(); break; }
                        val = Rational<int>(num, den);
                        break;
                }
                case TypeFrameRate:   val = readFrameRateData(); break;
                case TypeVideoFormat: val = readVideoFormatData(); break;
                case TypeTimecode:    val = readTimecodeData(); break;
                case TypeColor:       val = readColorData(); break;
                case TypeColorModel:  val = readColorModelData(); break;
                case TypeMemSpace:    val = readMemSpaceData(); break;
                case TypePixelMemLayout: val = readPixelMemLayoutData(); break;
                case TypePixelFormat:   val = readPixelFormatData(); break;
                case TypeAudioFormat:   val = readAudioFormatData(); break;
                case TypeEnum:        val = readEnumData(); break;
                case TypeEnumList:    val = readEnumListData(); break;
                case TypeMediaTimeStamp: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        auto [mts, parseErr] = MediaTimeStamp::fromString(s);
                        if(parseErr.isError()) {
                                setError(ReadCorruptData,
                                        String::sprintf("Failed to parse MediaTimeStamp from '%s'", s.cstr()));
                                val = Variant();
                                break;
                        }
                        val = mts;
                        break;
                }
                case TypeFrameNumber: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        Error pe;
                        FrameNumber fn = FrameNumber::fromString(s, &pe);
                        if(pe.isError()) {
                                setError(ReadCorruptData,
                                        String::sprintf("Failed to parse FrameNumber from '%s'", s.cstr()));
                                val = Variant();
                                break;
                        }
                        val = fn;
                        break;
                }
                case TypeFrameCount: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        Error pe;
                        FrameCount fc = FrameCount::fromString(s, &pe);
                        if(pe.isError()) {
                                setError(ReadCorruptData,
                                        String::sprintf("Failed to parse FrameCount from '%s'", s.cstr()));
                                val = Variant();
                                break;
                        }
                        val = fc;
                        break;
                }
                case TypeMediaDuration: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        Error pe;
                        MediaDuration md = MediaDuration::fromString(s, &pe);
                        if(pe.isError()) {
                                setError(ReadCorruptData,
                                        String::sprintf("Failed to parse MediaDuration from '%s'", s.cstr()));
                                val = Variant();
                                break;
                        }
                        val = md;
                        break;
                }
                case TypeDuration: {
                        const int64_t ns = readInt64();
                        if(_status != Ok) { val = Variant(); break; }
                        val = Duration::fromNanoseconds(ns);
                        break;
                }
                case TypeStringList:  val = readStringListData(); break;
                case TypeMasteringDisplay: {
                        // Outer tag already consumed; inner values are tagged
                        // doubles read via operator>>.  Ten doubles total:
                        // red.x, red.y, green.x, green.y, blue.x, blue.y,
                        // whitePoint.x, whitePoint.y, minLum, maxLum.
                        double rx = 0.0, ry = 0.0, gx = 0.0, gy = 0.0;
                        double bx = 0.0, by = 0.0, wx = 0.0, wy = 0.0;
                        double minL = 0.0, maxL = 0.0;
                        *this >> rx >> ry >> gx >> gy >> bx >> by >> wx >> wy >> minL >> maxL;
                        if(_status != Ok) { val = Variant(); break; }
                        val = MasteringDisplay(CIEPoint(rx, ry), CIEPoint(gx, gy),
                                               CIEPoint(bx, by), CIEPoint(wx, wy),
                                               minL, maxL);
                        break;
                }
                case TypeContentLightLevel: {
                        uint32_t maxCLL = 0, maxFALL = 0;
                        *this >> maxCLL >> maxFALL;
                        if(_status != Ok) { val = Variant(); break; }
                        val = ContentLightLevel(maxCLL, maxFALL);
                        break;
                }
                case TypeUrl: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        Result<Url> r = Url::fromString(s);
                        if(r.second().isError() || !r.first().isValid()) {
                                setError(ReadCorruptData,
                                        String::sprintf("Failed to parse Url from '%s'",
                                                s.cstr()));
                                val = Variant();
                                break;
                        }
                        val = r.first();
                        break;
                }
                case TypeVideoCodec: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        auto r = VideoCodec::fromString(s);
                        if(error(r).isError()) {
                                setError(ReadCorruptData,
                                        String::sprintf("Failed to parse VideoCodec from '%s'", s.cstr()));
                                val = Variant();
                                break;
                        }
                        val = value(r);
                        break;
                }
                case TypeAudioCodec: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        auto r = AudioCodec::fromString(s);
                        if(error(r).isError()) {
                                setError(ReadCorruptData,
                                        String::sprintf("Failed to parse AudioCodec from '%s'", s.cstr()));
                                val = Variant();
                                break;
                        }
                        val = value(r);
                        break;
                }
#if PROMEKI_ENABLE_NETWORK
                case TypeSocketAddress: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        auto r = SocketAddress::fromString(s);
                        if(error(r).isError()) {
                                setError(ReadCorruptData,
                                        String::sprintf("Failed to parse SocketAddress from '%s'", s.cstr()));
                                val = Variant();
                                break;
                        }
                        val = value(r);
                        break;
                }
                case TypeSdpSession: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        auto r = SdpSession::fromString(s);
                        if(error(r).isError()) {
                                setError(ReadCorruptData,
                                        "Failed to parse SdpSession from SDP text");
                                val = Variant();
                                break;
                        }
                        val = value(r);
                        break;
                }
                case TypeMacAddress: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        auto [mac, parseErr] = MacAddress::fromString(s);
                        if(parseErr.isError()) { setError(ReadCorruptData, String::sprintf("Failed to parse MacAddress from '%s'", s.cstr())); val = Variant(); break; }
                        val = mac;
                        break;
                }
                case TypeEUI64: {
                        String s = readStringData();
                        if(_status != Ok) { val = Variant(); break; }
                        auto [eui, parseErr] = EUI64::fromString(s);
                        if(parseErr.isError()) { setError(ReadCorruptData, String::sprintf("Failed to parse EUI64 from '%s'", s.cstr())); val = Variant(); break; }
                        val = eui;
                        break;
                }
#endif
                default:
                        setError(ReadCorruptData,
                                String::sprintf(
                                        "Variant::read: tag 0x%04X is not Variant-representable",
                                        static_cast<unsigned>(id)));
                        val = Variant();
                        break;
        }
}

DataStream &DataStream::operator>>(Variant &val) {
        uint16_t tag = readAnyTag();
        if(_status != Ok) { val = Variant(); return *this; }
        readVariantPayload(static_cast<TypeId>(tag), val);
        return *this;
}

// ============================================================================
// Compile-time coverage check
//
// For every type registered in @c PROMEKI_VARIANT_TYPES, assert that
// a dedicated wire-format @c operator<< / @c operator>> exists for
// the concrete type — not just an expression that happens to compile
// via @c Variant 's implicit converting constructor.  Without this
// strictness, a missing dedicated operator would silently fall back
// to the @c Variant overload, which then recurses right back here
// and either infinite-loops at runtime or writes a useless tag.
//
// Strategy:
//
//   - @c has_member_write / @c has_member_read detect an *exact-match*
//     member function via pointer-to-member-function cast.  Pointer
//     casts never apply user-defined conversions, so there's no way
//     for the @c Variant ctor to mask a missing specific overload.
//
//   - For the handful of Variant types whose wire operator is a free
//     function template (Size2D, Rational) or a non-member inline
//     (MasteringDisplay, ContentLightLevel), we explicitly trait them
//     as writable/readable.  These specialisations live right here so
//     anyone re-routing a type away from free-function templates
//     knows exactly what to update.
//
// @c std::monostate is whitelisted because @c TypeInvalid has no
// payload — it's handled by the X-macro write dispatch and the
// read-switch's explicit @c TypeInvalid case.
// ============================================================================

namespace {
        // Exact-match member function detection.  Primitives are
        // passed by value (see the many @c operator<<(int32_t val)
        // members); everything else is passed by @c const &.  Probe
        // both signatures so either pattern counts as "covered".
        template <typename T, typename = void>
        struct has_member_write : std::false_type {};
        template <typename T>
        struct has_member_write<T, std::void_t<decltype(
                static_cast<DataStream &(DataStream::*)(const T &)>(
                        &DataStream::operator<<))>> : std::true_type {};
        template <typename T>
        struct has_member_write<T, std::void_t<decltype(
                static_cast<DataStream &(DataStream::*)(T)>(
                        &DataStream::operator<<))>> : std::true_type {};

        template <typename T, typename = void>
        struct has_member_read : std::false_type {};
        template <typename T>
        struct has_member_read<T, std::void_t<decltype(
                static_cast<DataStream &(DataStream::*)(T &)>(
                        &DataStream::operator>>))>> : std::true_type {};

        // Free-function / inline-template allowlist.  Adding a new
        // entry here is a deliberate acknowledgement that the type's
        // wire operator lives outside of @c DataStream 's member
        // functions; prefer adding a member operator for new types
        // instead.
        template <typename T> struct has_free_write : std::false_type {};
        template <typename T> struct has_free_read  : std::false_type {};
        template <typename T> struct has_free_write<Size2DTemplate<T>> : std::true_type {};
        template <typename T> struct has_free_read<Size2DTemplate<T>>  : std::true_type {};
        template <typename T> struct has_free_write<Rational<T>> : std::true_type {};
        template <typename T> struct has_free_read<Rational<T>>  : std::true_type {};
        template <> struct has_free_write<MasteringDisplay>  : std::true_type {};
        template <> struct has_free_read<MasteringDisplay>   : std::true_type {};
        template <> struct has_free_write<ContentLightLevel> : std::true_type {};
        template <> struct has_free_read<ContentLightLevel>  : std::true_type {};

        template <typename T>
        inline constexpr bool has_datastream_write_v =
                has_member_write<T>::value || has_free_write<T>::value;
        template <typename T>
        inline constexpr bool has_datastream_read_v =
                has_member_read<T>::value || has_free_read<T>::value;

#define X(name, type) \
        static_assert(std::is_same_v<type, std::monostate> || \
                      has_datastream_write_v<type>, \
                      "Variant " #name " (" #type ") has no exact-match " \
                      "operator<<(DataStream&, const " #type "&) — add a " \
                      "DataStream member operator<< (preferred) or a free " \
                      "template specialisation entry above, then update the " \
                      "Variant write dispatch."); \
        static_assert(std::is_same_v<type, std::monostate> || \
                      has_datastream_read_v<type>, \
                      "Variant " #name " (" #type ") has no exact-match " \
                      "operator>>(DataStream&, " #type "&) — add a " \
                      "DataStream member operator>> (preferred) or a free " \
                      "template specialisation entry above, then update " \
                      "readVariantPayload().");
        PROMEKI_VARIANT_TYPES
#undef X
} // namespace

PROMEKI_NAMESPACE_END
