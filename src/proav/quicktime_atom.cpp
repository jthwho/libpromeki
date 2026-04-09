/**
 * @file      quicktime_atom.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "quicktime_atom.h"

#include <promeki/iodevice.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace quicktime_atom {

// ---------------------------------------------------------------------------
// ReadStream
// ---------------------------------------------------------------------------

int64_t ReadStream::pos() const {
        if(_dev == nullptr) return -1;
        return _dev->pos();
}

Error ReadStream::seek(int64_t offset) {
        if(_dev == nullptr) {
                _error = true;
                return Error::NotOpen;
        }
        Error err = _dev->seek(offset);
        if(err.isError()) _error = true;
        return err;
}

Error ReadStream::readBytes(void *out, int64_t n) {
        if(_error) return Error::IOError;
        if(_dev == nullptr) {
                _error = true;
                return Error::NotOpen;
        }
        if(n < 0) {
                _error = true;
                return Error::InvalidArgument;
        }
        if(n == 0) return Error::Ok;
        int64_t got = _dev->read(out, n);
        if(got < 0) {
                _error = true;
                return Error::IOError;
        }
        if(got < n) {
                _error = true;
                return Error::EndOfFile;
        }
        return Error::Ok;
}

Error ReadStream::skip(int64_t n) {
        if(_dev == nullptr) {
                _error = true;
                return Error::NotOpen;
        }
        if(n == 0) return Error::Ok;
        Error err = _dev->seek(_dev->pos() + n);
        if(err.isError()) _error = true;
        return err;
}

uint8_t ReadStream::readU8() {
        uint8_t v = 0;
        if(readBytes(&v, 1).isError()) return 0;
        return v;
}

uint16_t ReadStream::readU16() {
        uint8_t b[2];
        if(readBytes(b, 2).isError()) return 0;
        return static_cast<uint16_t>((static_cast<uint16_t>(b[0]) << 8) | b[1]);
}

uint32_t ReadStream::readU32() {
        uint8_t b[4];
        if(readBytes(b, 4).isError()) return 0;
        return (static_cast<uint32_t>(b[0]) << 24) |
               (static_cast<uint32_t>(b[1]) << 16) |
               (static_cast<uint32_t>(b[2]) <<  8) |
                static_cast<uint32_t>(b[3]);
}

uint64_t ReadStream::readU64() {
        uint8_t b[8];
        if(readBytes(b, 8).isError()) return 0;
        return (static_cast<uint64_t>(b[0]) << 56) |
               (static_cast<uint64_t>(b[1]) << 48) |
               (static_cast<uint64_t>(b[2]) << 40) |
               (static_cast<uint64_t>(b[3]) << 32) |
               (static_cast<uint64_t>(b[4]) << 24) |
               (static_cast<uint64_t>(b[5]) << 16) |
               (static_cast<uint64_t>(b[6]) <<  8) |
                static_cast<uint64_t>(b[7]);
}

FourCC ReadStream::readFourCC() {
        uint8_t b[4];
        if(readBytes(b, 4).isError()) return FourCC('\0', '\0', '\0', '\0');
        return FourCC(static_cast<char>(b[0]),
                      static_cast<char>(b[1]),
                      static_cast<char>(b[2]),
                      static_cast<char>(b[3]));
}

double ReadStream::readFixed16_16() {
        int32_t v = readS32();
        return static_cast<double>(v) / 65536.0;
}

double ReadStream::readFixed8_8() {
        int16_t v = readS16();
        return static_cast<double>(v) / 256.0;
}

// ---------------------------------------------------------------------------
// Box header parsing
// ---------------------------------------------------------------------------

Error readBoxHeader(ReadStream &stream, Box &box, int64_t enforceEnd) {
        box = Box{};
        int64_t headerOffset = stream.pos();
        if(headerOffset < 0) return Error::IOError;
        if(enforceEnd > 0 && headerOffset >= enforceEnd) return Error::EndOfFile;

        uint32_t size32 = stream.readU32();
        if(stream.isError()) {
                // A clean EOF at the start of a box is normal — the caller
                // reached the end of the parent. Report it as EndOfFile.
                return Error::EndOfFile;
        }
        FourCC type = stream.readFourCC();
        if(stream.isError()) return Error::CorruptData;

        int64_t payloadOffset = headerOffset + 8;
        int64_t payloadSize = 0;

        if(size32 == 1) {
                // 64-bit largesize follows.
                uint64_t large = stream.readU64();
                if(stream.isError()) return Error::CorruptData;
                if(large < 16) {
                        promekiWarn("QuickTime: 64-bit box '%c%c%c%c' largesize %llu < 16",
                                    (type.value() >> 24) & 0xff,
                                    (type.value() >> 16) & 0xff,
                                    (type.value() >>  8) & 0xff,
                                    (type.value() >>  0) & 0xff,
                                    static_cast<unsigned long long>(large));
                        return Error::CorruptData;
                }
                payloadOffset = headerOffset + 16;
                payloadSize   = static_cast<int64_t>(large) - 16;
        } else if(size32 == 0) {
                // Extends to end of parent / file. If we have an enforced
                // end, use that; otherwise report size as "unknown" via
                // payloadSize == -1.
                if(enforceEnd > 0) {
                        payloadSize = enforceEnd - payloadOffset;
                } else {
                        payloadSize = -1;
                }
        } else {
                if(size32 < 8) {
                        promekiWarn("QuickTime: 32-bit box '%c%c%c%c' size %u < 8",
                                    (type.value() >> 24) & 0xff,
                                    (type.value() >> 16) & 0xff,
                                    (type.value() >>  8) & 0xff,
                                    (type.value() >>  0) & 0xff,
                                    size32);
                        return Error::CorruptData;
                }
                payloadSize = static_cast<int64_t>(size32) - 8;
        }

        if(enforceEnd > 0 && payloadSize >= 0 &&
           payloadOffset + payloadSize > enforceEnd) {
                promekiWarn("QuickTime: box '%c%c%c%c' at %lld extends past parent end",
                            (type.value() >> 24) & 0xff,
                            (type.value() >> 16) & 0xff,
                            (type.value() >>  8) & 0xff,
                            (type.value() >>  0) & 0xff,
                            static_cast<long long>(headerOffset));
                return Error::CorruptData;
        }

        box.type          = type;
        box.headerOffset  = headerOffset;
        box.payloadOffset = payloadOffset;
        box.payloadSize   = payloadSize;
        box.endOffset     = (payloadSize >= 0) ? payloadOffset + payloadSize : payloadOffset;
        return Error::Ok;
}

Error advanceToSibling(ReadStream &stream, const Box &box) {
        if(box.payloadSize < 0) return Error::EndOfFile;
        return stream.seek(box.endOffset);
}

Error findTopLevelBox(ReadStream &stream, FourCC type,
                      int64_t startOffset, int64_t endOffset, Box &out) {
        Error err = stream.seek(startOffset);
        if(err.isError()) return err;
        while(true) {
                Box box;
                err = readBoxHeader(stream, box, endOffset);
                if(err.isError()) return err;
                if(box.type == type) {
                        out = box;
                        return Error::Ok;
                }
                err = advanceToSibling(stream, box);
                if(err.isError()) return err;
        }
}

// ---------------------------------------------------------------------------
// AtomWriter
// ---------------------------------------------------------------------------

void AtomWriter::writeU8(uint8_t v) {
        _data.pushToBack(v);
}

void AtomWriter::writeU16(uint16_t v) {
        _data.pushToBack(static_cast<uint8_t>((v >> 8) & 0xff));
        _data.pushToBack(static_cast<uint8_t>(v & 0xff));
}

void AtomWriter::writeU24(uint32_t v) {
        _data.pushToBack(static_cast<uint8_t>((v >> 16) & 0xff));
        _data.pushToBack(static_cast<uint8_t>((v >>  8) & 0xff));
        _data.pushToBack(static_cast<uint8_t>(v & 0xff));
}

void AtomWriter::writeU32(uint32_t v) {
        _data.pushToBack(static_cast<uint8_t>((v >> 24) & 0xff));
        _data.pushToBack(static_cast<uint8_t>((v >> 16) & 0xff));
        _data.pushToBack(static_cast<uint8_t>((v >>  8) & 0xff));
        _data.pushToBack(static_cast<uint8_t>(v & 0xff));
}

void AtomWriter::writeU64(uint64_t v) {
        for(int i = 7; i >= 0; --i) {
                _data.pushToBack(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
        }
}

void AtomWriter::writeFourCC(FourCC fc) {
        uint32_t v = fc.value();
        writeU32(v);
}

void AtomWriter::writeFixed16_16(double v) {
        int32_t fixed = static_cast<int32_t>(v * 65536.0);
        writeS32(fixed);
}

void AtomWriter::writeFixed8_8(double v) {
        int16_t fixed = static_cast<int16_t>(v * 256.0);
        writeS16(fixed);
}

void AtomWriter::writeBytes(const void *p, size_t n) {
        const uint8_t *bytes = static_cast<const uint8_t *>(p);
        for(size_t i = 0; i < n; ++i) _data.pushToBack(bytes[i]);
}

void AtomWriter::writeZeros(size_t n) {
        for(size_t i = 0; i < n; ++i) _data.pushToBack(0);
}

void AtomWriter::writePascalString(const String &s, size_t totalBytes) {
        if(totalBytes == 0) return;
        size_t maxLen = totalBytes - 1;
        size_t len = s.size();
        if(len > maxLen) len = maxLen;
        writeU8(static_cast<uint8_t>(len));
        if(len > 0) writeBytes(s.cstr(), len);
        size_t pad = maxLen - len;
        writeZeros(pad);
}

void AtomWriter::patchU32(size_t offset, uint32_t v) {
        _data[offset + 0] = static_cast<uint8_t>((v >> 24) & 0xff);
        _data[offset + 1] = static_cast<uint8_t>((v >> 16) & 0xff);
        _data[offset + 2] = static_cast<uint8_t>((v >>  8) & 0xff);
        _data[offset + 3] = static_cast<uint8_t>(v & 0xff);
}

AtomWriter::Marker AtomWriter::beginBox(FourCC type) {
        Marker m;
        m.sizeOffset = _data.size();
        writeU32(0);             // placeholder size
        writeFourCC(type);
        return m;
}

void AtomWriter::endBox(Marker m) {
        size_t boxSize = _data.size() - m.sizeOffset;
        patchU32(m.sizeOffset, static_cast<uint32_t>(boxSize));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

String decodeLanguage(uint16_t packed) {
        // 1-bit pad + 3 × 5-bit letters, each offset by 0x60 ('`').
        int c0 = ((packed >> 10) & 0x1f) + 0x60;
        int c1 = ((packed >>  5) & 0x1f) + 0x60;
        int c2 = ((packed >>  0) & 0x1f) + 0x60;
        if(c0 < 'a' || c0 > 'z' || c1 < 'a' || c1 > 'z' || c2 < 'a' || c2 > 'z') {
                return String();
        }
        char buf[4] = { static_cast<char>(c0), static_cast<char>(c1), static_cast<char>(c2), 0 };
        return String(buf);
}

int64_t macEpochToUnix(uint64_t macSeconds) {
        // QuickTime epoch is 1904-01-01 00:00:00 UTC. Unix epoch is
        // 1970-01-01 00:00:00 UTC. Difference = 2082844800 seconds.
        constexpr uint64_t kEpochDelta = 2082844800ull;
        if(macSeconds <= kEpochDelta) return 0;
        return static_cast<int64_t>(macSeconds - kEpochDelta);
}

} // namespace quicktime_atom

PROMEKI_NAMESPACE_END
