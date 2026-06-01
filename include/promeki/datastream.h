/**
 * @file      datastream.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstdint>
#include <cstring>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/hashmap.h>
#include <promeki/hashset.h>
#include <promeki/rect.h>
#include <promeki/point.h>
#include <promeki/size2d.h>
#include <promeki/rational.h>
#include <promeki/result.h>
#include <promeki/iodevice.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Forward declaration of the canonical type-tag enum.
 *
 * The full definition lives in @c datatype.h.  DataStream uses
 * @ref DataTypeID values purely as opaque @c uint16_t-sized handles
 * (frame tags, lookup keys), so a forward declaration with the
 * explicit underlying type is enough — callers that need to spell
 * out specific enum members must include @c datatype.h directly.
 */
enum DataTypeID : uint16_t;

class Variant;

/**
 * @brief Binary stream for structured, portable serialization.
 * @ingroup streams
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
 * Every DataStream begins with a fixed 16-byte header:
 * - Bytes 0-3: ASCII magic `"PMDS"` (`0x50 0x4D 0x44 0x53`).
 * - Bytes 4-5: uint16_t version in big-endian order.
 * - Byte 6: byte-order marker — `'B'` (0x42) for big-endian or
 *   `'L'` (0x4C) for little-endian.
 * - Bytes 7-15: reserved for future header extensions (must be zero).
 *
 * After the header, every value is emitted as a self-describing
 * *frame* whose 8-byte header is naturally 4-byte aligned:
 * - Bytes 0-1: @c uint16_t @ref DataTypeID tag (byte-order controlled).
 * - Bytes 2-3: @c uint16_t per-type version (byte-order controlled).
 * - Bytes 4-7: @c uint32_t body size in bytes (byte-order controlled).
 * - Bytes 8…:  type-specific body bytes (exactly the declared size).
 *
 * Per-type read/write logic lives on each type — see @ref PROMEKI_DATATYPE
 * for the registration macro.  DataStream itself knows only how to
 * frame primitives (signed/unsigned int8..int64, float, double, bool,
 * String, Buffer), containers (@ref List, @ref Map, @ref Set,
 * @ref HashMap, @ref HashSet), the small geometry templates
 * (@ref Size2DTemplate, @ref Rational, @ref Rect, @ref Point), and
 * @ref Variant.  Everything else dispatches through its registered
 * @ref DataType ops.
 *
 * @par Forward compatibility
 * The per-frame `[version][size]` pair lets one tag evolve across
 * releases (writer bumps version literal, reader switches on it) and
 * lets readers @ref skipFrame past tags they don't understand.
 *
 * @par Extensibility
 * To make a type travel through DataStream, annotate it with
 * @ref PROMEKI_DATATYPE inside the class body and provide:
 *  - @c Error writeToStream(DataStream &) const            — emit the body bytes.
 *  - @c template<uint32_t V> static Result<T> readFromStream(DataStream &)
 *                                                           — read body for wire version @c V.
 *
 * The framework's generic @c operator<< / @c operator>> templates
 * (declared in @c datatype.h) wrap calls in @ref beginFrame /
 * @ref endFrame and route reads through a version-dispatch table the
 * macro generates, so types own their wire format end to end.
 *
 * @par Buffered writes (no seek required)
 * @ref beginFrame opens a frame whose body bytes accumulate in an
 * internal stack-allocated buffer.  Only when @ref endFrame is
 * called does the assembled `[tag][version][size][body]` group hit
 * the underlying device — that's the moment the size field is
 * computed from the body buffer's length.  Sockets, pipes, files,
 * and BufferIODevice all work as targets.
 *
 * @par Thread Safety
 * DataStream inherits the thread-affinity of its underlying
 * IODevice — concurrent use of one stream from multiple threads
 * requires external synchronization.  Distinct streams over
 * separate devices may be used concurrently.
 *
 * @par Basic usage example
 * @code
 * // Write to a buffer
 * BufferIODevice device;
 * device.open(IODevice::WriteOnly);
 * DataStream writer = DataStream::createWriter(&device);
 * writer << int32_t(42) << String("hello") << true;
 *
 * // Read it back
 * device.seek(0);
 * DataStream reader = DataStream::createReader(&device);
 * int32_t num; String str; bool flag;
 * reader >> num >> str >> flag;
 * @endcode
 */
class DataStream {
        public:
                /** @brief Byte order for multi-byte values. */
                enum ByteOrder {
                        BigEndian,   ///< @brief Network byte order (default).
                        LittleEndian ///< @brief Intel/ARM byte order.
                };

                /** @brief Stream status codes. */
                enum Status {
                        Ok,              ///< @brief No error.
                        ReadPastEnd,     ///< @brief Attempted to read beyond available data.
                        ReadCorruptData, ///< @brief Data format is invalid.
                        WriteFailed      ///< @brief A write operation failed.
                };

                /**
                 * @brief Current wire format version.
                 *
                 * Version 3 introduced the per-value frame header
                 * (`[tag(2)][version(2)][size(4)]`, naturally 4-byte
                 * aligned).
                 */
                static constexpr uint16_t CurrentVersion = 3;

                /** @brief Total size of the stream header in bytes. */
                static constexpr size_t HeaderSize = 16;

                /** @brief Size of every per-value frame header in bytes
                 *         (uint16 tag + uint16 version + uint32 size). */
                static constexpr size_t FrameHeaderSize = 8;

                /**
                 * @brief Maximum frame body size accepted on read, used
                 *        as a sanity bound against corrupt size fields.
                 */
                static constexpr uint32_t MaxFrameBodySize = 1u << 30; // 1 GiB

                /** @brief Magic bytes identifying a DataStream ("PMDS"). */
                static constexpr uint8_t Magic[4] = {0x50, 0x4D, 0x44, 0x53};

                /**
                 * @brief Constructs a DataStream for writing on an IODevice.
                 *
                 * The device must already be open for writing. The header
                 * (magic + version + byte-order marker) is written
                 * immediately; the marker records @p order so readers can
                 * auto-configure.
                 *
                 * @param device The IODevice to write to.
                 * @param order  Byte order for multi-byte values (default BigEndian).
                 */
                static DataStream createWriter(IODevice *device, ByteOrder order = BigEndian);

                /**
                 * @brief Constructs a DataStream for reading from an IODevice.
                 *
                 * The device must already be open for reading. The header
                 * is read and validated immediately. The stream's byte
                 * order is auto-configured from the header marker.
                 *
                 * @param device The IODevice to read from.
                 */
                static DataStream createReader(IODevice *device);

                /**
                 * @brief Constructs a DataStream on an IODevice without
                 *        writing or reading a header.
                 *
                 * Low-level constructor for cases where the caller manages
                 * the header externally or the stream is used for raw
                 * binary I/O without framing.
                 *
                 * @param device The IODevice to operate on.
                 */
                explicit DataStream(IODevice *device);

                /** @brief Destructor. */
                ~DataStream() = default;

                // ============================================================
                // Byte order
                // ============================================================

                /** @brief Sets the byte order for multi-byte value serialization. */
                void setByteOrder(ByteOrder order) { _byteOrder = order; }

                /** @brief Returns the current byte order. */
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
                 */
                uint16_t version() const { return _version; }

                // ============================================================
                // Status and error handling
                // ============================================================

                /** @brief Returns the current stream status. */
                Status status() const { return _status; }

                /**
                 * @brief Returns a human-readable description of the last error.
                 */
                const String &errorContext() const { return _errorContext; }

                /**
                 * @brief Returns the status mapped to an Error code.
                 */
                Error toError() const;

                /**
                 * @brief Resets the stream status to Ok and clears the context.
                 */
                void resetStatus();

                /**
                 * @brief Returns true if the read/write position is at the end.
                 */
                bool atEnd() const;

                /** @brief Returns the underlying IODevice. */
                IODevice *device() const { return _device; }

                // ============================================================
                // Write operators — primitives
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
                /**
                 * @brief Writes a Variant using the same tag as the value's direct form.
                 *
                 * Dispatches on the Variant's runtime type to the matching
                 * concrete write op, so the emitted bytes are bit-identical
                 * to writing the contained value directly.  An invalid
                 * Variant emits a frame with a zero-byte body tagged
                 * @c DataTypeNoValue.
                 */
                DataStream &operator<<(const Variant &val);

                // ============================================================
                // Read operators — primitives
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
                /**
                 * @brief Reads any value into a Variant, dispatching on the tag.
                 *
                 * Peeks the frame header, looks the tag up in the
                 * @ref DataType registry, and forwards to that type's
                 * registered @c readStream op.  An unknown tag consumes
                 * the body via the size field and yields an invalid
                 * Variant — the explicit forward-compatibility path.
                 */
                DataStream &operator>>(Variant &val);

                // ============================================================
                // Result<T>-returning read API
                // ============================================================

                /**
                 * @brief Reads a value of type @p T and returns it as a Result.
                 *
                 * @tparam T Any type with a matching operator>>(DataStream&, T&).
                 */
                template <typename T> Result<T> read() {
                        T val{};
                        *this >> val;
                        if (_status != Ok) return makeError<T>(toError());
                        return makeResult(std::move(val));
                }

                // ============================================================
                // Extension API (for implementing custom operator<< / >>)
                // ============================================================

                /**
                 * @brief Opens a new frame on the write side.
                 *
                 * Pushes a fresh body buffer onto an internal frame stack.
                 * Every subsequent write — primitives, nested frames, raw
                 * bytes — accumulates in that buffer instead of going
                 * straight to the underlying device.  The matching
                 * @ref endFrame() pops the buffer and flushes the
                 * assembled `[tag][version][size][body]` group to the
                 * parent frame (if any) or to the device.
                 *
                 * Frames may nest freely.
                 *
                 * @param id      Tag identifying this frame's type.
                 * @param version Per-type wire-format version emitted in
                 *                the header.  Bump when a type's body
                 *                layout changes.
                 */
                void beginFrame(DataTypeID id, uint16_t version);

                /**
                 * @brief Closes the most recently opened frame.
                 *
                 * Emits the `[tag][version][size][body]` group either into
                 * the parent frame's body buffer or to the underlying
                 * device.  No-op if the stream is in an error state.
                 */
                void endFrame();

                /**
                 * @brief Reads a frame header and validates the tag.
                 *
                 * Consumes the 8-byte
                 * `[tag(uint16)][version(uint16)][size(uint32)]` header.
                 * If @p expected doesn't match the tag, sets status to
                 * @c ReadCorruptData and returns @c false.  If the
                 * tag matches but the wire version exceeds @p maxVersion,
                 * sets status to @c ReadCorruptData and returns @c false.
                 *
                 * @param expected   Required tag.
                 * @param maxVersion Highest version this reader understands.
                 * @param outVersion Optional out-parameter receiving the
                 *                   actual version from the frame.
                 * @param outSize    Optional out-parameter receiving the
                 *                   declared body size.
                 * @return @c true if the tag matched and the version was
                 *         acceptable.
                 */
                bool readFrame(DataTypeID expected, uint16_t maxVersion = 1,
                               uint16_t *outVersion = nullptr, uint32_t *outSize = nullptr);

                /**
                 * @brief Reads a frame header without validating its tag.
                 *
                 * Useful when a reader has to accept one of several tags
                 * (e.g. Variant dispatch).  The header is fully consumed;
                 * on success the stream is positioned at the first body
                 * byte.
                 */
                bool readFrameHeader(DataTypeID &outTag, uint16_t &outVersion, uint32_t &outSize);

                /**
                 * @brief Reads a frame header but leaves it cached for the next read.
                 *
                 * Pulls the 8 header bytes from the device and caches them
                 * so the next @ref readFrameHeader / @ref readFrame /
                 * @ref skipFrame returns the same values without touching
                 * the device a second time.
                 *
                 * The cache holds at most one frame header.  Calling
                 * @c peekFrameHeader twice in a row returns the same
                 * cached header without re-reading.
                 *
                 * @par Constraint
                 * Calling raw byte methods (@c readBytes, @c readRawData,
                 * @c skipRawData) while a peeked header is pending
                 * consumes bytes from the device past the header without
                 * draining the cache.  In debug builds those entry points
                 * assert that the cache is empty.
                 *
                 * @param outTag     Receives the tag.
                 * @param outVersion Receives the version.
                 * @param outSize    Receives the body size.
                 * @return @c true on success.
                 */
                bool peekFrameHeader(DataTypeID &outTag, uint16_t &outVersion, uint32_t &outSize);

                /**
                 * @brief Consumes a complete frame, discarding its body.
                 *
                 * Reads the frame header via @ref readFrameHeader and
                 * advances past the declared body size.  Provided primarily
                 * for forward compatibility: when a reader encounters a tag
                 * it doesn't understand it can call @ref skipFrame() to
                 * step past the value cleanly.
                 */
                void skipFrame();

                /**
                 * @brief Sets the status and a context message in one call.
                 *
                 * Public so that user-written operator<< / >> overloads
                 * can report meaningful errors. No-op if the stream is
                 * already in a non-Ok state (first error wins).
                 *
                 * @param s   The new status.
                 * @param ctx A descriptive context string.
                 */
                void setError(Status s, String ctx);

                // ============================================================
                // Raw byte access (untagged escape hatch)
                // ============================================================

                /** @brief Reads raw bytes from the stream. */
                ssize_t readRawData(void *buf, size_t len);

                /**
                 * @brief Writes raw bytes — no tag, no frame header.
                 *
                 * Outside of any frame, the bytes pass straight through to
                 * the underlying device.  When a frame is open, the bytes
                 * are appended to that frame's body so they're covered by
                 * the frame's size field.
                 */
                ssize_t writeRawData(const void *buf, size_t len);

                /** @brief Skips over raw bytes in the stream without reading them. */
                ssize_t skipRawData(size_t len);

        private:
                void writeHeader();
                void readHeader();

                /**
                 * @brief Reads exactly len bytes, setting status on failure.
                 * @return True if all bytes were read successfully.
                 */
                bool readBytes(void *buf, size_t len);

                /**
                 * @brief Advances past @p sz body bytes, validating the
                 *        device has them available.
                 */
                bool skipFrameBody(uint32_t sz);

                /**
                 * @brief Writes exactly len bytes, setting status on failure.
                 * @return True if all bytes were written successfully.
                 */
                bool writeBytes(const void *buf, size_t len);

                /**
                 * @brief Swaps byte order of a value in-place if needed.
                 */
                template <typename T> void swapIfNeeded(T &val) const {
                        if constexpr (sizeof(T) == 1) return;
                        if (_byteOrder == nativeByteOrder()) return;
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

                /** @brief Returns the native byte order of the platform. */
                static ByteOrder nativeByteOrder() {
                        static const uint16_t val = 1;
                        return (*reinterpret_cast<const uint8_t *>(&val) == 1) ? LittleEndian : BigEndian;
                }

                /**
                 * @brief One entry on the in-flight frame stack.
                 */
                struct PendingFrame {
                        DataTypeID    tag;     ///< @brief Tag this frame will emit.
                        uint16_t      version; ///< @brief Per-type version this frame will emit.
                        List<uint8_t> body;    ///< @brief Body bytes accumulated so far.
                };

                IODevice *           _device = nullptr;
                ByteOrder            _byteOrder = BigEndian;
                uint16_t             _version = 0;
                Status               _status = Ok;
                String               _errorContext;
                List<PendingFrame>   _frameStack;

                // One-deep frame-header lookahead.  Populated by
                // @ref peekFrameHeader and drained by the next
                // @ref readFrameHeader so that Variant dispatch can
                // inspect the tag without requiring a seekable device.
                bool                 _peekedHeaderValid = false;
                DataTypeID           _peekedTag;
                uint16_t             _peekedVersion     = 0;
                uint32_t             _peekedSize        = 0;
};

// ============================================================================
// Container template operators
// ============================================================================
//
// These let arbitrary List<T>, Map<K,V>, and Set<T> flow through a DataStream
// as long as T (and K, V) already have operator<< / operator>> overloads.
// They write a tag (DataTypeList / DataTypeMap / DataTypeSet) followed by a
// uint32_t count and then the fully-tagged elements.

namespace detail {
        /** @brief Maximum element count accepted by container reads, to prevent
         *         runaway allocations on corrupt input. */
        inline constexpr uint32_t DataStreamMaxContainerCount = 256u * 1024u * 1024u;
} // namespace detail

PROMEKI_NAMESPACE_END

// The container / geometry operator templates below need DataTypeID
// values (DataTypeList, DataTypeSize2D, ...) by name, which means they
// need the full enum definition.  Pull in datatype.h here.
//
// enum.h is included AFTER datatype.h so the slicing operator<< / >>
// overloads for @ref TypedEnum subclasses can resolve the underlying
// Enum::promekiDataType inside their body.  Order matters: enum.h
// itself includes datatype.h, but datatype.h is already loaded by the
// time we get here, so the second inclusion is a no-op.
#include <promeki/datatype.h>
#include <promeki/enum.h>
// json.h is pulled in here — after DataStream is complete but before
// the @ref Detail::makeDefaultOps definition below — so the
// @c toJson / @c fromJson Ops slots can be populated from lambdas
// whose bodies reference @ref JsonObject directly.  json.h re-includes
// datastream.h but the pragma-once guard makes the inner include a
// no-op; by this point in this header DataStream is fully declared,
// which is everything json.h's bottom-half operators need.
#include <promeki/json.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Free-function DataStream operator allowlist (geometry templates only)
// ============================================================================
//
// Size2DTemplate<T> and Rational<T> ship with free-function operator<< /
// operator>> templates rather than a member API.  Flagging them here
// lets @ref Detail::makeDefaultOps populate the writeStream / readStream
// slots through the free-function path.  These specializations live in
// the header (rather than in variant.cpp) so they are visible at every
// makeDefaultOps instantiation site — including the call sites in
// datatype.cpp's registerBuiltinDataTypes.

namespace Detail {

template <typename T> struct HasFreeDataStreamWrite<Size2DTemplate<T>> : std::true_type {};
template <typename T> struct HasFreeDataStreamRead<Size2DTemplate<T>>  : std::true_type {};
template <typename T> struct HasFreeDataStreamWrite<Rational<T>>       : std::true_type {};
template <typename T> struct HasFreeDataStreamRead<Rational<T>>        : std::true_type {};

} // namespace Detail

// ============================================================================
// Geometry template operators
// ============================================================================
//
// These share a single tag per kind (DataTypeSize2D, DataTypeRect, DataTypePoint,
// DataTypeRational) and rely on the inner values being written/read via their
// own primitive operators, which means the inner values carry their own
// type tags. This lets one Size2DTemplate<T> template cover uint32_t,
// int32_t, float, etc. transparently.

/**
 * @brief Writes a Size2DTemplate as tag + tagged width + tagged height.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const Size2DTemplate<T> &sz) {
        stream.beginFrame(DataTypeSize2D, 1);
        stream << sz.width() << sz.height();
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a Size2DTemplate, validating the tag and element types.
 */
template <typename T> DataStream &operator>>(DataStream &stream, Size2DTemplate<T> &sz) {
        if (!stream.readFrame(DataTypeSize2D)) {
                sz = Size2DTemplate<T>();
                return stream;
        }
        T w{}, h{};
        stream >> w >> h;
        if (stream.status() != DataStream::Ok) {
                sz = Size2DTemplate<T>();
                return stream;
        }
        sz = Size2DTemplate<T>(w, h);
        return stream;
}

/**
 * @brief Writes a Rational as tag + tagged numerator + tagged denominator.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const Rational<T> &r) {
        stream.beginFrame(DataTypeRational, 1);
        stream << r.numerator() << r.denominator();
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a Rational, validating the tag and element types.
 */
template <typename T> DataStream &operator>>(DataStream &stream, Rational<T> &r) {
        if (!stream.readFrame(DataTypeRational)) {
                r = Rational<T>();
                return stream;
        }
        T num{}, den{1};
        stream >> num >> den;
        if (stream.status() != DataStream::Ok) {
                r = Rational<T>();
                return stream;
        }
        r = Rational<T>(num, den);
        return stream;
}

/**
 * @brief Writes a Rect as tag + tagged x + y + width + height.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const Rect<T> &rect) {
        stream.beginFrame(DataTypeRect, 1);
        stream << rect.x() << rect.y() << rect.width() << rect.height();
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a Rect, validating the tag and element types.
 */
template <typename T> DataStream &operator>>(DataStream &stream, Rect<T> &rect) {
        if (!stream.readFrame(DataTypeRect)) {
                rect = Rect<T>();
                return stream;
        }
        T x{}, y{}, w{}, h{};
        stream >> x >> y >> w >> h;
        if (stream.status() != DataStream::Ok) {
                rect = Rect<T>();
                return stream;
        }
        rect = Rect<T>(x, y, w, h);
        return stream;
}

/**
 * @brief Writes a Point as tag + uint32 dims + N tagged values.
 */
template <typename T, size_t N> DataStream &operator<<(DataStream &stream, const Point<T, N> &point) {
        stream.beginFrame(DataTypePoint, 1);
        stream << static_cast<uint32_t>(N);
        const Array<T, N> &arr = point;
        for (size_t i = 0; i < N; ++i) stream << arr[i];
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a Point, validating the tag, dimension count, and element types.
 */
template <typename T, size_t N> DataStream &operator>>(DataStream &stream, Point<T, N> &point) {
        if (!stream.readFrame(DataTypePoint)) {
                point = Point<T, N>();
                return stream;
        }
        uint32_t dims = 0;
        stream >> dims;
        if (stream.status() != DataStream::Ok) {
                point = Point<T, N>();
                return stream;
        }
        if (dims != N) {
                stream.setError(DataStream::ReadCorruptData,
                                String::sprintf("Point dimension mismatch: expected %zu, got %u", N,
                                                static_cast<unsigned>(dims)));
                point = Point<T, N>();
                return stream;
        }
        Array<T, N> arr;
        for (size_t i = 0; i < N; ++i) {
                T val{};
                stream >> val;
                if (stream.status() != DataStream::Ok) {
                        point = Point<T, N>();
                        return stream;
                }
                arr[i] = val;
        }
        point = Point<T, N>(arr);
        return stream;
}

/**
 * @brief Writes a List as tag + uint32 count + N tagged elements.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const List<T> &list) {
        stream.beginFrame(DataTypeList, 1);
        stream << static_cast<uint32_t>(list.size());
        for (const auto &item : list) stream << item;
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a List, verifying the tag and length prefix.
 */
template <typename T> DataStream &operator>>(DataStream &stream, List<T> &list) {
        list.clear();
        if (!stream.readFrame(DataTypeList)) return stream;
        uint32_t count = 0;
        stream >> count;
        if (stream.status() != DataStream::Ok) return stream;
        if (count > detail::DataStreamMaxContainerCount) {
                stream.setError(DataStream::ReadCorruptData, String("List element count exceeds sanity limit"));
                return stream;
        }
        list.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
                T item{};
                stream >> item;
                if (stream.status() != DataStream::Ok) return stream;
                list.pushToBack(std::move(item));
        }
        return stream;
}

/**
 * @brief Writes a Map as tag + uint32 count + N key/value pairs.
 */
template <typename K, typename V> DataStream &operator<<(DataStream &stream, const Map<K, V> &map) {
        stream.beginFrame(DataTypeMap, 1);
        stream << static_cast<uint32_t>(map.size());
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
                stream << it->first << it->second;
        }
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a Map, verifying the tag and length prefix.
 */
template <typename K, typename V> DataStream &operator>>(DataStream &stream, Map<K, V> &map) {
        map.clear();
        if (!stream.readFrame(DataTypeMap)) return stream;
        uint32_t count = 0;
        stream >> count;
        if (stream.status() != DataStream::Ok) return stream;
        if (count > detail::DataStreamMaxContainerCount) {
                stream.setError(DataStream::ReadCorruptData, String("Map entry count exceeds sanity limit"));
                return stream;
        }
        for (uint32_t i = 0; i < count; ++i) {
                K key{};
                V value{};
                stream >> key >> value;
                if (stream.status() != DataStream::Ok) return stream;
                map.insert(std::move(key), std::move(value));
        }
        return stream;
}

/**
 * @brief Writes a Set as tag + uint32 count + N tagged elements.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const Set<T> &set) {
        stream.beginFrame(DataTypeSet, 1);
        stream << static_cast<uint32_t>(set.size());
        for (const auto &item : set) stream << item;
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a Set, verifying the tag and length prefix.
 */
template <typename T> DataStream &operator>>(DataStream &stream, Set<T> &set) {
        set.clear();
        if (!stream.readFrame(DataTypeSet)) return stream;
        uint32_t count = 0;
        stream >> count;
        if (stream.status() != DataStream::Ok) return stream;
        if (count > detail::DataStreamMaxContainerCount) {
                stream.setError(DataStream::ReadCorruptData, String("Set element count exceeds sanity limit"));
                return stream;
        }
        for (uint32_t i = 0; i < count; ++i) {
                T item{};
                stream >> item;
                if (stream.status() != DataStream::Ok) return stream;
                set.insert(std::move(item));
        }
        return stream;
}

/**
 * @brief Writes a HashMap as tag + uint32 count + N key/value pairs.
 */
template <typename K, typename V> DataStream &operator<<(DataStream &stream, const HashMap<K, V> &map) {
        stream.beginFrame(DataTypeHashMap, 1);
        stream << static_cast<uint32_t>(map.size());
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
                stream << it->first << it->second;
        }
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a HashMap, verifying the tag and length prefix.
 *
 * Hash container iteration order is unspecified, so on-wire order depends
 * on the writer's hash seed.  Round-trips preserve entries but not insertion
 * order.
 */
template <typename K, typename V> DataStream &operator>>(DataStream &stream, HashMap<K, V> &map) {
        map.clear();
        if (!stream.readFrame(DataTypeHashMap)) return stream;
        uint32_t count = 0;
        stream >> count;
        if (stream.status() != DataStream::Ok) return stream;
        if (count > detail::DataStreamMaxContainerCount) {
                stream.setError(DataStream::ReadCorruptData, String("HashMap entry count exceeds sanity limit"));
                return stream;
        }
        for (uint32_t i = 0; i < count; ++i) {
                K key{};
                V value{};
                stream >> key >> value;
                if (stream.status() != DataStream::Ok) return stream;
                map.insert(std::move(key), std::move(value));
        }
        return stream;
}

/**
 * @brief Writes a HashSet as tag + uint32 count + N tagged elements.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const HashSet<T> &set) {
        stream.beginFrame(DataTypeHashSet, 1);
        stream << static_cast<uint32_t>(set.size());
        for (const auto &item : set) stream << item;
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a HashSet, verifying the tag and length prefix.
 */
template <typename T> DataStream &operator>>(DataStream &stream, HashSet<T> &set) {
        set.clear();
        if (!stream.readFrame(DataTypeHashSet)) return stream;
        uint32_t count = 0;
        stream >> count;
        if (stream.status() != DataStream::Ok) return stream;
        if (count > detail::DataStreamMaxContainerCount) {
                stream.setError(DataStream::ReadCorruptData, String("HashSet element count exceeds sanity limit"));
                return stream;
        }
        for (uint32_t i = 0; i < count; ++i) {
                T item{};
                stream >> item;
                if (stream.status() != DataStream::Ok) return stream;
                set.insert(std::move(item));
        }
        return stream;
}

// ============================================================================
// DataStream <<, >> for JsonObject / JsonArray
//
// These live here (rather than in @c json.h) so @c json.h can stay
// free of a @c datastream.h dependency — which is what lets the
// @ref Detail::makeDefaultOps populator below reference @ref JsonObject
// directly without forming a circular include.
//
// Wire form (v1): compact JSON text as a tagged, length-prefixed
// String body.  Readers that see a truncated or malformed payload
// flag the stream @c ReadCorruptData.
// ============================================================================

inline DataStream &operator<<(DataStream &stream, const JsonObject &obj) {
        stream.beginFrame(DataTypeJsonObject, 1);
        stream << obj.toString(0);
        stream.endFrame();
        return stream;
}

inline DataStream &operator>>(DataStream &stream, JsonObject &obj) {
        if (!stream.readFrame(DataTypeJsonObject)) {
                obj = JsonObject();
                return stream;
        }
        String text;
        stream >> text;
        if (stream.status() != DataStream::Ok) {
                obj = JsonObject();
                return stream;
        }
        Error err;
        obj = JsonObject::parse(text, &err);
        if (err.isError()) {
                stream.setError(DataStream::ReadCorruptData, String("JsonObject::parse failed"));
        }
        return stream;
}

inline DataStream &operator<<(DataStream &stream, const JsonArray &arr) {
        stream.beginFrame(DataTypeJsonArray, 1);
        stream << arr.toString(0);
        stream.endFrame();
        return stream;
}

inline DataStream &operator>>(DataStream &stream, JsonArray &arr) {
        if (!stream.readFrame(DataTypeJsonArray)) {
                arr = JsonArray();
                return stream;
        }
        String text;
        stream >> text;
        if (stream.status() != DataStream::Ok) {
                arr = JsonArray();
                return stream;
        }
        Error err;
        arr = JsonArray::parse(text, &err);
        if (err.isError()) {
                stream.setError(DataStream::ReadCorruptData, String("JsonArray::parse failed"));
        }
        return stream;
}

namespace Detail {
template <> struct HasFreeDataStreamWrite<JsonObject> : std::true_type {};
template <> struct HasFreeDataStreamRead<JsonObject>  : std::true_type {};
template <> struct HasFreeDataStreamWrite<JsonArray>  : std::true_type {};
template <> struct HasFreeDataStreamRead<JsonArray>   : std::true_type {};
} // namespace Detail

// ============================================================================
// Concept detection + ops-table population that requires DataStream
// to be complete.
//
// These live here (rather than in @c datatype.h) because @c datatype.h
// is included by many types that themselves get pulled in transitively
// from this file (Buffer, MemSpace, StringList, ...).  Cycling through
// @c datatype.h → @c datastream.h would re-enter mid-flight and break
// dependent types' declarations.
// ============================================================================

namespace Detail {

/**
 * @brief Detects @c Error T::writeToStream(DataStream &) const member.
 */
template <typename T>
concept HasMemberWriteToStream = requires(const T &t, DataStream &s) {
        { t.writeToStream(s) } -> std::convertible_to<Error>;
};

/**
 * @brief Detects @c static Result<T> T::readFromStream<V>(DataStream &) for any V.
 */
template <typename T>
concept HasMemberReadFromStream = requires(DataStream &s) {
        { T::template readFromStream<1>(s) } -> std::convertible_to<Result<T>>;
};

/**
 * @brief Detects an *exact-match* @c operator<<(DataStream &, const T &)
 *        member on DataStream.
 *
 * Pointer-cast technique picks exactly one overload by signature.
 */
template <typename T>
struct ExactDataStreamWrite<T, std::void_t<decltype(static_cast<DataStream &(DataStream::*)(const T &)>(
                                          &DataStream::operator<<))>> : std::true_type {};

template <typename T>
struct ExactDataStreamWrite<T, std::void_t<decltype(static_cast<DataStream &(DataStream::*)(T)>(
                                          &DataStream::operator<<))>> : std::true_type {};

/**
 * @brief Detects an *exact-match* @c operator>>(DataStream &, T &) member.
 */
template <typename T>
struct ExactDataStreamRead<T, std::void_t<decltype(static_cast<DataStream &(DataStream::*)(T &)>(
                                          &DataStream::operator>>))>> : std::true_type {};

/**
 * @brief Builds an Ops table for @p T, populating slots for whichever
 *        well-known operations @p T satisfies.
 */
template <typename T>
DataType::Ops makeDefaultOps() {
        DataType::Ops ops;
        if constexpr (std::is_default_constructible_v<T>) {
                ops.defaultConstruct = [](void *p) {
                        ::new (p) T();
                        return;
                };
        }
        ops.copyConstruct = [](void *dst, const void *src) {
                ::new (dst) T(*static_cast<const T *>(src));
                return;
        };
        ops.moveConstruct = [](void *dst, void *src) {
                ::new (dst) T(std::move(*static_cast<T *>(src)));
                return;
        };
        ops.destroy = [](void *p) {
                static_cast<T *>(p)->~T();
                return;
        };
        if constexpr (HasEqualityOp<T>) {
                ops.equal = [](const void *a, const void *b) -> bool {
                        return *static_cast<const T *>(a) == *static_cast<const T *>(b);
                };
        }
        if constexpr (HasMemberToString<T>) {
                ops.toString = [](const void *p, Error *err) -> String {
                        if (err != nullptr) *err = Error::Ok;
                        return static_cast<const T *>(p)->toString();
                };
        }
        if constexpr (HasResultFromString<T>) {
                ops.fromString = [](const String &s, void *out, Error *err) -> bool {
                        auto r = T::fromString(s);
                        if (r.second().isError()) {
                                if (err != nullptr) *err = r.second();
                                return false;
                        }
                        *static_cast<T *>(out) = r.first();
                        if (err != nullptr) *err = Error::Ok;
                        return true;
                };
        }
        if constexpr (HasMemberValueInt<T>) {
                // FrameNumber / FrameCount / Enum — value() returns an
                // integral so the Variant <-> integer path can fall
                // straight through without a type-specific branch.
                ops.toInt = [](const void *p, Error *err) -> int64_t {
                        if (err != nullptr) *err = Error::Ok;
                        return static_cast<int64_t>(static_cast<const T *>(p)->value());
                };
        } else if constexpr (HasMemberIdInt<T>) {
                // TypeRegistry wrappers (ColorModel, MemSpace,
                // PixelFormat, ...) — id() is the canonical integer
                // form of the wrapper.
                ops.toInt = [](const void *p, Error *err) -> int64_t {
                        if (err != nullptr) *err = Error::Ok;
                        return static_cast<int64_t>(static_cast<const T *>(p)->id());
                };
        }
        if constexpr (HasIdCtor<T>) {
                ops.fromInt = [](int64_t v, void *out, Error *err) -> bool {
                        if (err != nullptr) *err = Error::Ok;
                        *static_cast<T *>(out) = T(static_cast<typename T::ID>(v));
                        return true;
                };
        } else if constexpr (HasInt64Ctor<T>) {
                ops.fromInt = [](int64_t v, void *out, Error *err) -> bool {
                        if (err != nullptr) *err = Error::Ok;
                        *static_cast<T *>(out) = T(v);
                        return true;
                };
        }
        if constexpr (HasMemberToDouble<T>) {
                ops.toFloat = [](const void *p, Error *err) -> double {
                        if (err != nullptr) *err = Error::Ok;
                        return static_cast<const T *>(p)->toDouble();
                };
        }
        if constexpr (HasResultFromDouble<T>) {
                ops.fromFloat = [](double v, void *out, Error *err) -> bool {
                        auto r = T::fromDouble(v);
                        if (r.second().isError()) {
                                if (err != nullptr) *err = r.second();
                                return false;
                        }
                        *static_cast<T *>(out) = r.first();
                        if (err != nullptr) *err = Error::Ok;
                        return true;
                };
        }
        if constexpr (HasMemberToJson<T>) {
                ops.toJson = +[](const void *p, Error *err) -> JsonObject {
                        if (err != nullptr) *err = Error::Ok;
                        return static_cast<const T *>(p)->toJson();
                };
        }
        if constexpr (HasResultFromJson<T>) {
                ops.fromJson = +[](const JsonObject &j, void *out, Error *err) -> bool {
                        auto r = T::fromJson(j);
                        if (r.second().isError()) {
                                if (err != nullptr) *err = r.second();
                                return false;
                        }
                        *static_cast<T *>(out) = r.first();
                        if (err != nullptr) *err = Error::Ok;
                        return true;
                };
        }
        if constexpr (HasMemberWriteToStream<T> && HasPromekiDataType<T>) {
                // PROMEKI_DATATYPE member-API path: wrap writeToStream
                // in a frame using the macro's pinned id + version.
                ops.writeStream = [](DataStream &s, const void *p) {
                        const T &val = *static_cast<const T *>(p);
                        s.beginFrame(T::promekiDataType::id, T::promekiDataType::version);
                        Error err = val.writeToStream(s);
                        s.endFrame();
                        if (err.isError()) {
                                if (s.status() == DataStream::Ok) {
                                        s.setError(DataStream::WriteFailed,
                                                   String("writeToStream returned ") + err.name());
                                }
                        }
                        return;
                };
        } else if constexpr (HasDataStreamWriteV<T>) {
                // Free-function / member-on-DataStream operator<<: the
                // operator already handles its own framing.
                ops.writeStream = [](DataStream &s, const void *p) {
                        s << *static_cast<const T *>(p);
                        return;
                };
        }
        if constexpr (HasMemberReadFromStream<T> && HasPromekiDataType<T>) {
                // PROMEKI_DATATYPE member-API read path: validate frame
                // header, then dispatch to the matching readFromStream<V>
                // via the macro-generated dispatch table.
                ops.readStream = [](DataStream &s, void *p) {
                        uint16_t ver = 0;
                        if (!s.readFrame(T::promekiDataType::id, T::promekiDataType::version, &ver,
                                         nullptr)) {
                                *static_cast<T *>(p) = T();
                                return;
                        }
                        Result<T> r = T::promekiDataType::dispatchRead(s, ver);
                        if (r.second().isError()) {
                                if (s.status() == DataStream::Ok) {
                                        s.setError(DataStream::ReadCorruptData,
                                                   String("readFromStream returned ") + r.second().name());
                                }
                                *static_cast<T *>(p) = T();
                                return;
                        }
                        *static_cast<T *>(p) = std::move(r.first());
                        return;
                };
        } else if constexpr (HasDataStreamReadV<T>) {
                ops.readStream = [](DataStream &s, void *p) {
                        s >> *static_cast<T *>(p);
                        return;
                };
        }
        return ops;
}

} // namespace Detail

// Forward-declare Enum so the constraint below can refer to it without
// forcing <promeki/enum.h> to load up here.  TypedEnum subclasses get
// their own overload (further down) which slices through their Enum
// base; the macro-driven generic template must NOT match those or
// overload resolution between the two becomes ambiguous.
class Enum;

namespace Detail {

/** @brief True when T is a TypedEnum<Self>-derived subclass (i.e. derives
 *         from Enum but is not Enum itself). */
template <typename T>
concept IsTypedEnumSubclass = std::is_base_of_v<Enum, T> && !std::is_same_v<T, Enum>;

} // namespace Detail

/**
 * @brief Generic write operator for any type carrying a @c promekiDataType trait struct.
 */
template <typename T>
        requires Detail::HasPromekiDataType<T> && Detail::HasMemberWriteToStream<T> &&
                 (!Detail::IsTypedEnumSubclass<T>)
inline DataStream &operator<<(DataStream &stream, const T &val) {
        stream.beginFrame(T::promekiDataType::id, T::promekiDataType::version);
        Error err = val.writeToStream(stream);
        stream.endFrame();
        if (err.isError() && stream.status() == DataStream::Ok) {
                stream.setError(DataStream::WriteFailed,
                                String("writeToStream returned ") + err.name());
        }
        return stream;
}

/**
 * @brief Generic read operator for any type carrying a @c promekiDataType trait struct.
 */
template <typename T>
        requires Detail::HasPromekiDataType<T> && Detail::HasMemberReadFromStream<T> &&
                 (!Detail::IsTypedEnumSubclass<T>)
inline DataStream &operator>>(DataStream &stream, T &val) {
        uint16_t ver = 0;
        if (!stream.readFrame(T::promekiDataType::id, T::promekiDataType::version, &ver, nullptr)) {
                val = T();
                return stream;
        }
        Result<T> r = T::promekiDataType::dispatchRead(stream, ver);
        if (r.second().isError()) {
                if (stream.status() == DataStream::Ok) {
                        stream.setError(DataStream::ReadCorruptData,
                                        String("readFromStream returned ") + r.second().name());
                }
                val = T();
                return stream;
        }
        val = std::move(r.first());
        return stream;
}

// ============================================================================
// Slicing overloads for TypedEnum<Self> subclasses
//
// TypedEnum<X> derives publicly from Enum.  When user code does
// `stream >> someTypedEnumValue`, the generic operator>> above is
// disqualified by the IsTypedEnumSubclass clause; these forward
// through the Enum base, exactly mirroring the slicing read the
// hand-rolled @c operator>>(Enum&) provided in the legacy design.
// ============================================================================

template <typename T>
        requires Detail::IsTypedEnumSubclass<T>
inline DataStream &operator<<(DataStream &stream, const T &val) {
        const Enum &base = val;
        return stream << base;
}

template <typename T>
        requires Detail::IsTypedEnumSubclass<T>
inline DataStream &operator>>(DataStream &stream, T &val) {
        Enum &base = val;
        return stream >> base;
}

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
