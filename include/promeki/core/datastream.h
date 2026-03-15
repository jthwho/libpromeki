/**
 * @file      core/datastream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/buffer.h>
#include <promeki/core/variant.h>
#include <promeki/core/iodevice.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Binary stream for structured, portable serialization.
 *
 * DataStream provides a Qt-style interface for reading and writing binary
 * data in a portable, byte-order-aware format. It operates exclusively
 * over an IODevice. For in-memory serialization, use BufferIODevice as
 * the underlying device.
 *
 * Primary use cases include ObjectBase::saveState()/loadState(),
 * file format I/O, and network protocol encoding.
 *
 * @par Wire format
 * Every DataStream begins with a header: a 4-byte magic number
 * (`0x50 0x4D 0x44 0x53`, ASCII "PMDS") followed by a uint16_t
 * version number in big-endian byte order. The version identifies
 * the wire format so that future changes remain backward-compatible.
 * The current version is 1.
 *
 * After the header, each value is preceded by a one-byte TypeId tag
 * that identifies the type. On read, the tag is validated against the
 * expected type; a mismatch sets the status to ReadCorruptData. This
 * makes streams self-describing and catches type mismatches early.
 *
 * Value encoding:
 * - Multi-byte integers are byte-order controlled via setByteOrder(),
 *   defaulting to big-endian (network byte order).
 * - Strings are stored as a uint32_t byte-count prefix followed by
 *   UTF-8 encoded bytes (no null terminator).
 * - Buffers are stored as a uint32_t byte-count prefix followed by
 *   raw bytes.
 * - Floats and doubles use IEEE 754, byte-swapped if needed.
 *
 * @par Extensibility
 * User types can be serialized by implementing free-standing
 * `operator<<(DataStream &, const MyType &)` and
 * `operator>>(DataStream &, MyType &)`.
 */
class DataStream {
        public:
                /** @brief Byte order for multi-byte values. */
                enum ByteOrder {
                        BigEndian,    ///< @brief Network byte order (default).
                        LittleEndian  ///< @brief Intel/ARM byte order.
                };

                /** @brief Stream status codes. */
                enum Status {
                        Ok,              ///< @brief No error.
                        ReadPastEnd,     ///< @brief Attempted to read beyond available data.
                        ReadCorruptData, ///< @brief Data format is invalid.
                        WriteFailed      ///< @brief A write operation failed.
                };

                /**
                 * @brief Type identifiers written before each value.
                 *
                 * Every operator<< writes a one-byte TypeId before the
                 * payload. Every operator>> reads and validates it. A
                 * mismatch sets status to ReadCorruptData.
                 *
                 * Raw byte methods (readRawData, writeRawData, skipRawData)
                 * do NOT write or expect type tags — they are unframed.
                 */
                enum TypeId : uint8_t {
                        TypeInt8    = 0x01, ///< @brief int8_t
                        TypeUInt8   = 0x02, ///< @brief uint8_t
                        TypeInt16   = 0x03, ///< @brief int16_t
                        TypeUInt16  = 0x04, ///< @brief uint16_t
                        TypeInt32   = 0x05, ///< @brief int32_t
                        TypeUInt32  = 0x06, ///< @brief uint32_t
                        TypeInt64   = 0x07, ///< @brief int64_t
                        TypeUInt64  = 0x08, ///< @brief uint64_t
                        TypeFloat   = 0x09, ///< @brief float (IEEE 754)
                        TypeDouble  = 0x0A, ///< @brief double (IEEE 754)
                        TypeBool    = 0x0B, ///< @brief bool (as uint8_t)
                        TypeString  = 0x0C, ///< @brief Length-prefixed UTF-8 String
                        TypeBuffer  = 0x0D, ///< @brief Length-prefixed raw bytes
                        TypeVariant = 0x0E  ///< @brief Type-tagged Variant
                };

                /** @brief Current wire format version. */
                static constexpr uint16_t CurrentVersion = 1;

                /** @brief Magic bytes identifying a DataStream ("PMDS"). */
                static constexpr uint8_t Magic[4] = { 0x50, 0x4D, 0x44, 0x53 };

                /**
                 * @brief Constructs a DataStream for writing on an IODevice.
                 *
                 * The device must already be open for writing. A header
                 * (magic + version) is written immediately. If the write
                 * fails, status() will reflect the error.
                 *
                 * @param device The IODevice to write to.
                 */
                static DataStream createWriter(IODevice *device);

                /**
                 * @brief Constructs a DataStream for reading from an IODevice.
                 *
                 * The device must already be open for reading. The header
                 * (magic + version) is read and validated immediately. If
                 * the header is missing or invalid, status() will be set
                 * to ReadCorruptData.
                 *
                 * @param device The IODevice to read from.
                 */
                static DataStream createReader(IODevice *device);

                /**
                 * @brief Constructs a DataStream on an IODevice without
                 *        writing or reading a header.
                 *
                 * This is a low-level constructor for cases where the
                 * caller manages the header externally or the stream
                 * is used for raw binary I/O without framing.
                 *
                 * @param device The IODevice to operate on.
                 */
                explicit DataStream(IODevice *device);

                /** @brief Destructor. */
                ~DataStream() = default;

                // ============================================================
                // Byte order
                // ============================================================

                /**
                 * @brief Sets the byte order for multi-byte value serialization.
                 * @param order The byte order to use.
                 */
                void setByteOrder(ByteOrder order) { _byteOrder = order; }

                /**
                 * @brief Returns the current byte order.
                 * @return The byte order.
                 */
                ByteOrder byteOrder() const { return _byteOrder; }

                // ============================================================
                // Version
                // ============================================================

                /**
                 * @brief Returns the wire format version read from the header.
                 *
                 * For writers, this is always CurrentVersion. For readers,
                 * this is the version found in the stream header. For
                 * streams constructed without a header, this is 0.
                 *
                 * @return The version number.
                 */
                uint16_t version() const { return _version; }

                // ============================================================
                // Status and error handling
                // ============================================================

                /**
                 * @brief Returns the current stream status.
                 * @return The status code.
                 */
                Status status() const { return _status; }

                /**
                 * @brief Resets the stream status to Ok.
                 */
                void resetStatus() { _status = Ok; }

                /**
                 * @brief Returns true if the read/write position is at the end.
                 * @return True if at end of data.
                 */
                bool atEnd() const;

                /**
                 * @brief Returns the underlying IODevice.
                 * @return The device pointer.
                 */
                IODevice *device() const { return _device; }

                // ============================================================
                // Write operators
                // ============================================================

                /** @brief Writes an int8_t. */
                DataStream &operator<<(int8_t val);
                /** @brief Writes a uint8_t. */
                DataStream &operator<<(uint8_t val);
                /** @brief Writes an int16_t. */
                DataStream &operator<<(int16_t val);
                /** @brief Writes a uint16_t. */
                DataStream &operator<<(uint16_t val);
                /** @brief Writes an int32_t. */
                DataStream &operator<<(int32_t val);
                /** @brief Writes a uint32_t. */
                DataStream &operator<<(uint32_t val);
                /** @brief Writes an int64_t. */
                DataStream &operator<<(int64_t val);
                /** @brief Writes a uint64_t. */
                DataStream &operator<<(uint64_t val);
                /** @brief Writes a float (IEEE 754). */
                DataStream &operator<<(float val);
                /** @brief Writes a double (IEEE 754). */
                DataStream &operator<<(double val);
                /** @brief Writes a bool (as uint8_t: 0 or 1). */
                DataStream &operator<<(bool val);
                /** @brief Writes a String as length-prefixed UTF-8. */
                DataStream &operator<<(const String &val);
                /** @brief Writes a Buffer as length-prefixed raw bytes. */
                DataStream &operator<<(const Buffer &val);
                /** @brief Writes a Variant as type tag + value. */
                DataStream &operator<<(const Variant &val);

                // ============================================================
                // Read operators
                // ============================================================

                /** @brief Reads an int8_t. */
                DataStream &operator>>(int8_t &val);
                /** @brief Reads a uint8_t. */
                DataStream &operator>>(uint8_t &val);
                /** @brief Reads an int16_t. */
                DataStream &operator>>(int16_t &val);
                /** @brief Reads a uint16_t. */
                DataStream &operator>>(uint16_t &val);
                /** @brief Reads an int32_t. */
                DataStream &operator>>(int32_t &val);
                /** @brief Reads a uint32_t. */
                DataStream &operator>>(uint32_t &val);
                /** @brief Reads an int64_t. */
                DataStream &operator>>(int64_t &val);
                /** @brief Reads a uint64_t. */
                DataStream &operator>>(uint64_t &val);
                /** @brief Reads a float (IEEE 754). */
                DataStream &operator>>(float &val);
                /** @brief Reads a double (IEEE 754). */
                DataStream &operator>>(double &val);
                /** @brief Reads a bool. */
                DataStream &operator>>(bool &val);
                /** @brief Reads a String from length-prefixed UTF-8. */
                DataStream &operator>>(String &val);
                /** @brief Reads a Buffer from length-prefixed raw bytes. */
                DataStream &operator>>(Buffer &val);
                /** @brief Reads a Variant from type tag + value. */
                DataStream &operator>>(Variant &val);

                // ============================================================
                // Raw byte access
                // ============================================================

                /**
                 * @brief Reads raw bytes from the stream.
                 * @param buf  Destination buffer.
                 * @param len  Number of bytes to read.
                 * @return The number of bytes actually read, or -1 on error.
                 */
                ssize_t readRawData(void *buf, size_t len);

                /**
                 * @brief Writes raw bytes to the stream.
                 * @param buf  Source buffer.
                 * @param len  Number of bytes to write.
                 * @return The number of bytes actually written, or -1 on error.
                 */
                ssize_t writeRawData(const void *buf, size_t len);

                /**
                 * @brief Skips over raw bytes in the stream without reading them.
                 * @param len  Number of bytes to skip.
                 * @return The number of bytes actually skipped, or -1 on error.
                 */
                ssize_t skipRawData(size_t len);

        private:
                /**
                 * @brief Writes the stream header (magic + version).
                 */
                void writeHeader();

                /**
                 * @brief Reads and validates the stream header.
                 */
                void readHeader();

                /**
                 * @brief Writes a type tag byte to the stream.
                 * @param id The TypeId to write.
                 */
                void writeTag(TypeId id);

                /**
                 * @brief Reads a type tag byte and validates it.
                 *
                 * If the tag does not match @p expected, sets status to
                 * ReadCorruptData.
                 * @param expected The expected TypeId.
                 * @return True if the tag matched.
                 */
                bool readTag(TypeId expected);

                /**
                 * @brief Reads exactly len bytes, setting status on failure.
                 * @param buf  Destination buffer.
                 * @param len  Number of bytes to read.
                 * @return True if all bytes were read successfully.
                 */
                bool readBytes(void *buf, size_t len);

                /**
                 * @brief Writes exactly len bytes, setting status on failure.
                 * @param buf  Source buffer.
                 * @param len  Number of bytes to write.
                 * @return True if all bytes were written successfully.
                 */
                bool writeBytes(const void *buf, size_t len);

                // Untagged value write helpers (used by Variant to avoid double-tagging)
                void writeInt8(int8_t val);
                void writeUInt8(uint8_t val);
                void writeInt16(int16_t val);
                void writeUInt16(uint16_t val);
                void writeInt32(int32_t val);
                void writeUInt32(uint32_t val);
                void writeInt64(int64_t val);
                void writeUInt64(uint64_t val);
                void writeFloat(float val);
                void writeDouble(double val);
                void writeBool(bool val);
                void writeStringData(const String &val);

                // Untagged value read helpers
                int8_t   readInt8();
                uint8_t  readUInt8();
                int16_t  readInt16();
                uint16_t readUInt16();
                int32_t  readInt32();
                uint32_t readUInt32();
                int64_t  readInt64();
                uint64_t readUInt64();
                float    readFloat();
                double   readDouble();
                bool     readBoolValue();
                String   readStringData();

                /**
                 * @brief Swaps byte order of a value in-place if needed.
                 * @tparam T The type to byte-swap (2, 4, or 8 bytes).
                 * @param val The value to potentially swap.
                 */
                template <typename T>
                void swapIfNeeded(T &val) const {
                        if constexpr (sizeof(T) == 1) return;
                        if(_byteOrder == nativeByteOrder()) return;
                        uint8_t *p = reinterpret_cast<uint8_t *>(&val);
                        if constexpr (sizeof(T) == 2) {
                                std::swap(p[0], p[1]);
                        } else if constexpr (sizeof(T) == 4) {
                                std::swap(p[0], p[3]);
                                std::swap(p[1], p[2]);
                        } else if constexpr (sizeof(T) == 8) {
                                std::swap(p[0], p[7]);
                                std::swap(p[1], p[6]);
                                std::swap(p[2], p[5]);
                                std::swap(p[3], p[4]);
                        }
                }

                /**
                 * @brief Returns the native byte order of the platform.
                 * @return BigEndian or LittleEndian.
                 */
                static ByteOrder nativeByteOrder() {
                        static const uint16_t val = 1;
                        return (*reinterpret_cast<const uint8_t *>(&val) == 1)
                                ? LittleEndian : BigEndian;
                }

                IODevice        *_device    = nullptr;
                ByteOrder       _byteOrder  = BigEndian;
                uint16_t        _version    = 0;
                Status          _status     = Ok;
};

PROMEKI_NAMESPACE_END
