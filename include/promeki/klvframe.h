/**
 * @file      klvframe.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/fourcc.h>
#include <promeki/buffer.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class IODevice;

/**
 * @brief Key-Length-Value frame over a binary stream.
 * @ingroup network
 *
 * Implements a minimal KLV framing with a 4-byte @ref FourCC key, a
 * 4-byte big-endian length, and a variable-length value payload.  The
 * framing is intentionally unopinionated about what the value bytes
 * contain — callers are free to encode structured content (for
 * example with @ref DataStream over a @ref BufferIODevice) or pack
 * fixed binary layouts directly into the value.
 *
 * @par Wire format
 * @code
 * +--------+----------+--------------------+
 * | Key    | Length   | Value              |
 * | 4 B    | 4 B  BE  | Length bytes       |
 * | FourCC | uint32   | (opaque payload)   |
 * +--------+----------+--------------------+
 * @endcode
 *
 * @par Forward compatibility
 * Readers that encounter an unknown Key can discard the frame via
 * @ref KlvReader::skipValue without having to understand the payload,
 * which lets protocols add new message kinds without breaking older
 * peers.
 *
 * @par Comparison to SMPTE KLV
 * The SMPTE 336M / 377M KLV uses a 16-byte Universal Label key and
 * BER-encoded length.  This KLV uses a 4-byte FourCC key and a fixed
 * 4-byte big-endian length — smaller overhead, plenty of namespace
 * for internal protocols, and a single debuggable ASCII key that
 * shows up in packet captures.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 */
class KlvFrame {
        public:
                /** @brief Maximum value size accepted by readers, for sanity. */
                static constexpr uint32_t DefaultMaxValueBytes = 64u * 1024u * 1024u;

                /** @brief The Key identifier for this frame. */
                FourCC key;

                /** @brief The Value payload. */
                Buffer value;

                /** @brief Constructs an empty frame with an all-zero key. */
                KlvFrame() : key(0, 0, 0, 0) {}

                /**
                 * @brief Constructs a frame from a key and payload.
                 * @param k   The FourCC key.
                 * @param buf The payload buffer.
                 */
                KlvFrame(FourCC k, const Buffer &buf) : key(k), value(buf) {}
};

/**
 * @brief Binary-stream reader for @ref KlvFrame framing.
 * @ingroup network
 *
 * Reads KLV frames from any @ref IODevice.  Typical use is over a
 * @ref LocalSocket, @ref TcpSocket, or a @ref BufferIODevice for
 * in-memory testing.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * Thread-safety of the underlying @ref IODevice also applies.
 */
class KlvReader {
        public:
                /**
                 * @brief Constructs a reader bound to @p device.
                 * @param device The underlying IODevice (must outlive the reader).
                 */
                explicit KlvReader(IODevice *device) : _device(device) {}

                /**
                 * @brief Reads the next frame header (Key + Length).
                 *
                 * On success, @p key and @p valueSize are populated but
                 * the Value payload is not yet consumed.  Use
                 * @ref readValue or @ref skipValue to handle it.
                 *
                 * @param key       Output: the frame's FourCC key.
                 * @param valueSize Output: the frame's value length in bytes.
                 * @return @c Error::Ok, @c Error::EndOfFile, or an IO error.
                 */
                Error readHeader(FourCC &key, uint32_t &valueSize);

                /**
                 * @brief Reads exactly @p size bytes of value payload.
                 *
                 * Call after a successful @ref readHeader; @p size must
                 * equal the @c valueSize returned from the header.
                 *
                 * @param buf  Destination buffer.
                 * @param size Number of bytes to read.
                 * @return @c Error::Ok on success, or an IO error.
                 */
                Error readValue(void *buf, uint32_t size);

                /**
                 * @brief Discards @p size bytes of value payload.
                 *
                 * Used to skip frames with unrecognized keys.
                 *
                 * @param size Number of bytes to discard.
                 * @return @c Error::Ok on success, or an IO error.
                 */
                Error skipValue(uint32_t size);

                /**
                 * @brief Reads a complete frame (Key + Length + Value) in one call.
                 *
                 * The value payload is allocated into @p out.value.  If
                 * the frame's length exceeds @p maxValueBytes, returns
                 * @c Error::TooLarge without consuming the value (leaving
                 * the stream at an indeterminate position — typically
                 * the caller closes the connection).
                 *
                 * @param out            The output frame.
                 * @param maxValueBytes  Maximum accepted value size.
                 * @return @c Error::Ok, @c Error::EndOfFile, @c Error::TooLarge,
                 *         or an IO error.
                 */
                Error readFrame(KlvFrame &out, uint32_t maxValueBytes = KlvFrame::DefaultMaxValueBytes);

        private:
                IODevice *_device = nullptr;
};

/**
 * @brief Binary-stream writer for @ref KlvFrame framing.
 * @ingroup network
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * Thread-safety of the underlying @ref IODevice also applies.
 */
class KlvWriter {
        public:
                /**
                 * @brief Constructs a writer bound to @p device.
                 * @param device The underlying IODevice (must outlive the writer).
                 */
                explicit KlvWriter(IODevice *device) : _device(device) {}

                /**
                 * @brief Emits a frame header (Key + Length), no payload.
                 *
                 * Intended for streaming writers that want to emit the
                 * payload bytes themselves via the underlying IODevice.
                 *
                 * @param key       The FourCC key.
                 * @param valueSize The length of the value that will follow.
                 * @return @c Error::Ok on success, or an IO error.
                 */
                Error writeHeader(FourCC key, uint32_t valueSize);

                /**
                 * @brief Emits @p size bytes of value payload.
                 *
                 * Call after @ref writeHeader.  @p size must equal the
                 * @c valueSize previously declared.
                 *
                 * @param buf  Source bytes.
                 * @param size Number of bytes to write.
                 * @return @c Error::Ok on success, or an IO error.
                 */
                Error writeValue(const void *buf, uint32_t size);

                /**
                 * @brief Emits a complete frame (Key + Length + Value).
                 *
                 * @param key       The FourCC key.
                 * @param value     The payload bytes (may be null when @p size == 0).
                 * @param size      The payload length.
                 * @return @c Error::Ok on success, or an IO error.
                 */
                Error writeFrame(FourCC key, const void *value, uint32_t size);

                /** @brief Emits an empty-payload frame (Length = 0). */
                Error writeFrame(FourCC key);

                /** @brief Emits a frame carrying @p value as its payload. */
                Error writeFrame(FourCC key, const Buffer &value);

                /** @brief Emits the given frame. */
                Error writeFrame(const KlvFrame &frame) { return writeFrame(frame.key, frame.value); }

        private:
                IODevice *_device = nullptr;
};

PROMEKI_NAMESPACE_END
