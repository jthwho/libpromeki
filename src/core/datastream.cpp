/**
 * @file      datastream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/datastream.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Factory methods
// ============================================================================

DataStream DataStream::createWriter(IODevice *device) {
        DataStream ds(device);
        ds._version = CurrentVersion;
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
        writeBytes(Magic, sizeof(Magic));
        // Version is always written big-endian regardless of stream byte order
        uint16_t ver = _version;
        uint8_t buf[2] = {
                static_cast<uint8_t>(ver >> 8),
                static_cast<uint8_t>(ver & 0xFF)
        };
        writeBytes(buf, 2);
}

void DataStream::readHeader() {
        uint8_t magic[4];
        if(!readBytes(magic, sizeof(magic))) return;
        if(std::memcmp(magic, Magic, sizeof(Magic)) != 0) {
                _status = ReadCorruptData;
                return;
        }
        uint8_t buf[2];
        if(!readBytes(buf, 2)) return;
        _version = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
}

// ============================================================================
// Type tags
// ============================================================================

void DataStream::writeTag(TypeId id) {
        uint8_t tag = static_cast<uint8_t>(id);
        writeBytes(&tag, 1);
}

bool DataStream::readTag(TypeId expected) {
        uint8_t tag = 0;
        if(!readBytes(&tag, 1)) return false;
        if(tag != static_cast<uint8_t>(expected)) {
                _status = ReadCorruptData;
                return false;
        }
        return true;
}

// ============================================================================
// Status
// ============================================================================

bool DataStream::atEnd() const {
        if(_device != nullptr) return _device->atEnd();
        return true;
}

// ============================================================================
// Internal read/write helpers
// ============================================================================

bool DataStream::readBytes(void *buf, size_t len) {
        if(_status != Ok) return false;
        if(len == 0) return true;
        if(_device == nullptr) { _status = ReadPastEnd; return false; }
        size_t total = 0;
        uint8_t *dst = static_cast<uint8_t *>(buf);
        while(total < len) {
                int64_t n = _device->read(dst + total, static_cast<int64_t>(len - total));
                if(n <= 0) {
                        _status = ReadPastEnd;
                        return false;
                }
                total += static_cast<size_t>(n);
        }
        return true;
}

bool DataStream::writeBytes(const void *buf, size_t len) {
        if(_status != Ok) return false;
        if(len == 0) return true;
        if(_device == nullptr) { _status = WriteFailed; return false; }
        size_t total = 0;
        const uint8_t *src = static_cast<const uint8_t *>(buf);
        while(total < len) {
                int64_t n = _device->write(src + total, static_cast<int64_t>(len - total));
                if(n <= 0) {
                        _status = WriteFailed;
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
        if(_device == nullptr) { _status = ReadPastEnd; return -1; }
        int64_t n = _device->read(buf, static_cast<int64_t>(len));
        if(n < 0) { _status = ReadPastEnd; return -1; }
        return static_cast<ssize_t>(n);
}

ssize_t DataStream::writeRawData(const void *buf, size_t len) {
        if(_status != Ok) return -1;
        if(len == 0) return 0;
        if(_device == nullptr) { _status = WriteFailed; return -1; }
        int64_t n = _device->write(buf, static_cast<int64_t>(len));
        if(n < 0) { _status = WriteFailed; return -1; }
        return static_cast<ssize_t>(n);
}

ssize_t DataStream::skipRawData(size_t len) {
        if(_status != Ok) return -1;
        if(len == 0) return 0;
        if(_device == nullptr) { _status = ReadPastEnd; return -1; }
        if(!_device->isSequential()) {
                int64_t cur = _device->pos();
                Error err = _device->seek(cur + static_cast<int64_t>(len));
                if(err.isError()) { _status = ReadPastEnd; return -1; }
                return static_cast<ssize_t>(len);
        }
        // Sequential: read and discard
        uint8_t tmp[1024];
        size_t total = 0;
        while(total < len) {
                size_t chunk = len - total;
                if(chunk > sizeof(tmp)) chunk = sizeof(tmp);
                int64_t n = _device->read(tmp, static_cast<int64_t>(chunk));
                if(n <= 0) { _status = ReadPastEnd; return total > 0 ? static_cast<ssize_t>(total) : -1; }
                total += static_cast<size_t>(n);
        }
        return static_cast<ssize_t>(total);
}

// ============================================================================
// Internal helpers for writing/reading values without type tags.
// Used by Variant serialization to avoid double-tagging.
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
                _status = ReadCorruptData;
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
        uint32_t len = static_cast<uint32_t>(val.size());
        writeUInt32(len);
        if(len > 0) writeBytes(val.data(), len);
        return *this;
}

DataStream &DataStream::operator<<(const Variant &val) {
        writeTag(TypeVariant);
        // Variant-internal type byte (Variant::Type enum value)
        writeUInt8(static_cast<uint8_t>(val.type()));
        switch(val.type()) {
                case Variant::TypeInvalid:
                        break;
                case Variant::TypeBool:
                        writeBool(val.get<bool>());
                        break;
                case Variant::TypeU8:
                        writeUInt8(val.get<uint8_t>());
                        break;
                case Variant::TypeS8:
                        writeInt8(val.get<int8_t>());
                        break;
                case Variant::TypeU16:
                        writeUInt16(val.get<uint16_t>());
                        break;
                case Variant::TypeS16:
                        writeInt16(val.get<int16_t>());
                        break;
                case Variant::TypeU32:
                        writeUInt32(val.get<uint32_t>());
                        break;
                case Variant::TypeS32:
                        writeInt32(val.get<int32_t>());
                        break;
                case Variant::TypeU64:
                        writeUInt64(val.get<uint64_t>());
                        break;
                case Variant::TypeS64:
                        writeInt64(val.get<int64_t>());
                        break;
                case Variant::TypeFloat:
                        writeFloat(val.get<float>());
                        break;
                case Variant::TypeDouble:
                        writeDouble(val.get<double>());
                        break;
                case Variant::TypeString:
                        writeStringData(val.get<String>());
                        break;
                case Variant::TypeDateTime:
                case Variant::TypeTimeStamp:
                case Variant::TypeSize2D:
                case Variant::TypeUUID:
                case Variant::TypeTimecode:
                case Variant::TypeRational:
                case Variant::TypeColor:
                        writeStringData(val.get<String>());
                        break;
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
        uint32_t len = readUInt32();
        if(_status != Ok) { val = Buffer(); return *this; }
        if(len == 0) { val = Buffer(); return *this; }
        if(len > 256 * 1024 * 1024) {
                _status = ReadCorruptData;
                val = Buffer();
                return *this;
        }
        Buffer buf(len);
        if(readBytes(buf.data(), len)) {
                buf.setSize(len);
                val = std::move(buf);
        } else {
                val = Buffer();
        }
        return *this;
}

DataStream &DataStream::operator>>(Variant &val) {
        if(!readTag(TypeVariant)) { val = Variant(); return *this; }
        uint8_t variantType = readUInt8();
        if(_status != Ok) { val = Variant(); return *this; }

        switch(static_cast<Variant::Type>(variantType)) {
                case Variant::TypeInvalid:
                        val = Variant();
                        break;
                case Variant::TypeBool:
                        val = readBoolValue();
                        break;
                case Variant::TypeU8:
                        val = readUInt8();
                        break;
                case Variant::TypeS8:
                        val = readInt8();
                        break;
                case Variant::TypeU16:
                        val = readUInt16();
                        break;
                case Variant::TypeS16:
                        val = readInt16();
                        break;
                case Variant::TypeU32:
                        val = readUInt32();
                        break;
                case Variant::TypeS32:
                        val = readInt32();
                        break;
                case Variant::TypeU64:
                        val = readUInt64();
                        break;
                case Variant::TypeS64:
                        val = readInt64();
                        break;
                case Variant::TypeFloat:
                        val = readFloat();
                        break;
                case Variant::TypeDouble:
                        val = readDouble();
                        break;
                case Variant::TypeString:
                        val = readStringData();
                        break;
                case Variant::TypeDateTime: {
                        String v = readStringData();
                        if(_status == Ok) val = DateTime::fromString(v);
                        break;
                }
                case Variant::TypeTimeStamp:
                case Variant::TypeSize2D:
                case Variant::TypeRational:
                        val = readStringData();
                        break;
                case Variant::TypeUUID: {
                        String v = readStringData();
                        if(_status == Ok) val = UUID::fromString(v);
                        break;
                }
                case Variant::TypeTimecode: {
                        String v = readStringData();
                        if(_status == Ok) {
                                auto [tc, err] = Timecode::fromString(v);
                                if(err.isError()) val = v;
                                else val = tc;
                        }
                        break;
                }
                case Variant::TypeColor: {
                        String v = readStringData();
                        if(_status == Ok) val = Color::fromString(v);
                        break;
                }
                default:
                        _status = ReadCorruptData;
                        val = Variant();
                        break;
        }
        return *this;
}

PROMEKI_NAMESPACE_END
