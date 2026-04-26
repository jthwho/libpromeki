/**
 * @file      klvframe.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/klvframe.h>
#include <promeki/iodevice.h>

#include <cstdint>
#include <cstring>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Read exactly n bytes, looping across short reads.  Returns Error::EndOfFile
        // on a clean zero-byte read at the start (use to detect end of stream), and
        // Error::IOError on a partial read after any bytes have been consumed.
        Error readExact(IODevice *dev, void *buf, size_t n) {
                if (dev == nullptr) return Error::NotOpen;
                auto  *p = static_cast<uint8_t *>(buf);
                size_t remaining = n;
                bool   anyRead = false;
                while (remaining > 0) {
                        int64_t got = dev->read(p, static_cast<int64_t>(remaining));
                        if (got < 0) return Error::IOError;
                        if (got == 0) {
                                return anyRead ? Error(Error::IOError) : Error(Error::EndOfFile);
                        }
                        anyRead = true;
                        p += got;
                        remaining -= static_cast<size_t>(got);
                }
                return Error::Ok;
        }

        Error writeExact(IODevice *dev, const void *buf, size_t n) {
                if (dev == nullptr) return Error::NotOpen;
                const auto *p = static_cast<const uint8_t *>(buf);
                size_t      remaining = n;
                while (remaining > 0) {
                        int64_t wrote = dev->write(p, static_cast<int64_t>(remaining));
                        if (wrote < 0) return Error::IOError;
                        if (wrote == 0) return Error::IOError;
                        p += wrote;
                        remaining -= static_cast<size_t>(wrote);
                }
                return Error::Ok;
        }

        inline uint32_t unpackBE32(const uint8_t *b) {
                return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
                       (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
        }

        inline void packBE32(uint8_t *b, uint32_t v) {
                b[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
                b[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
                b[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
                b[3] = static_cast<uint8_t>(v & 0xFF);
        }

} // namespace

Error KlvReader::readHeader(FourCC &key, uint32_t &valueSize) {
        uint8_t hdr[8] = {};
        Error   err = readExact(_device, hdr, sizeof(hdr));
        if (err.isError()) return err;
        // FourCC stores in big-endian already; rebuild its 4-character form.
        key = FourCC(static_cast<char>(hdr[0]), static_cast<char>(hdr[1]), static_cast<char>(hdr[2]),
                     static_cast<char>(hdr[3]));
        valueSize = unpackBE32(&hdr[4]);
        return Error::Ok;
}

Error KlvReader::readValue(void *buf, uint32_t size) {
        if (size == 0) return Error::Ok;
        return readExact(_device, buf, size);
}

Error KlvReader::skipValue(uint32_t size) {
        if (size == 0) return Error::Ok;
        uint8_t  scratch[512];
        uint32_t remaining = size;
        while (remaining > 0) {
                uint32_t chunk = remaining > sizeof(scratch) ? static_cast<uint32_t>(sizeof(scratch)) : remaining;
                Error    err = readExact(_device, scratch, chunk);
                if (err.isError()) return err;
                remaining -= chunk;
        }
        return Error::Ok;
}

Error KlvReader::readFrame(KlvFrame &out, uint32_t maxValueBytes) {
        FourCC   key(0, 0, 0, 0);
        uint32_t size = 0;
        Error    err = readHeader(key, size);
        if (err.isError()) return err;
        if (size > maxValueBytes) {
                out.key = key;
                out.value = Buffer();
                return Error::TooLarge;
        }
        Buffer buf(size);
        if (size > 0) {
                err = readValue(buf.data(), size);
                if (err.isError()) return err;
        }
        // Buffer allocates capacity; setSize sets the logical content size
        // so downstream code sees the expected byte count.
        buf.setSize(size);
        out.key = key;
        out.value = std::move(buf);
        return Error::Ok;
}

Error KlvWriter::writeHeader(FourCC key, uint32_t valueSize) {
        uint8_t hdr[8];
        // FourCC::value() is already big-endian packed; emit big-endian bytes.
        uint32_t k = key.value();
        packBE32(&hdr[0], k);
        packBE32(&hdr[4], valueSize);
        return writeExact(_device, hdr, sizeof(hdr));
}

Error KlvWriter::writeValue(const void *buf, uint32_t size) {
        if (size == 0) return Error::Ok;
        return writeExact(_device, buf, size);
}

Error KlvWriter::writeFrame(FourCC key, const void *value, uint32_t size) {
        Error err = writeHeader(key, size);
        if (err.isError()) return err;
        return writeValue(value, size);
}

Error KlvWriter::writeFrame(FourCC key) {
        return writeHeader(key, 0);
}

Error KlvWriter::writeFrame(FourCC key, const Buffer &value) {
        return writeFrame(key, value.data(), static_cast<uint32_t>(value.size()));
}

PROMEKI_NAMESPACE_END
