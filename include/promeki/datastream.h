/**
 * @file      datastream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/buffer.h>
#include <promeki/variant_fwd.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/set.h>
#include <promeki/hashmap.h>
#include <promeki/hashset.h>
#include <promeki/rect.h>
#include <promeki/point.h>
#include <promeki/size2d.h>
#include <promeki/rational.h>
#include <promeki/xyzcolor.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/result.h>
#include <promeki/iodevice.h>

PROMEKI_NAMESPACE_BEGIN

class JsonObject;
class JsonArray;
class MediaTimeStamp;
class MacAddress;
class EUI64;
class FrameNumber;
class FrameCount;
class MediaDuration;
class Duration;
// Forward-declared Variant-alternative types used only as `const T &` /
// `T &` parameters on DataStream's operator overloads.  Full definitions
// are included in datastream.cpp (via variant.h), so declarations here
// stay header-cheap.
class UUID;
class UMID;
class DateTime;
class TimeStamp;
class FrameRate;
class VideoFormat;
class Timecode;
class Color;
class ColorModel;
class MemSpace;
class PixelMemLayout;
class PixelFormat;
class AudioFormat;
class Enum;
class EnumList;
class StringList;
class Url;
class VideoCodec;
class AudioCodec;
class SocketAddress;
class SdpSession;

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
 * - Bytes 4-5: uint16_t version in big-endian order. The version
 *   identifies the wire format so future changes remain
 *   backward-compatible. The current version is 1.
 * - Byte 6: byte-order marker — `'B'` (0x42) for big-endian or
 *   `'L'` (0x4C) for little-endian. A reader uses this to
 *   auto-configure its byte order, so writers and readers do not
 *   need to agree out-of-band.
 * - Bytes 7-15: reserved for future header extensions. Writers must
 *   emit all zeros. Readers must verify all zeros — any non-zero
 *   value is treated as ReadCorruptData. This guarantees that when
 *   a future version repurposes a reserved byte, old readers fail
 *   loudly rather than silently mis-parsing.
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
 * - Data objects (UUID, DateTime, Timecode, etc.) each have their own
 *   tag and native binary encoding.
 * - Containers (List, Map, Set) are stored as a uint32_t count prefix
 *   followed by their fully-tagged elements.
 * - Variants are stored using the same tag as the value they carry, so
 *   a stream position holding a Variant containing a UUID is byte-for-byte
 *   identical to one holding a direct UUID. Readers can flip between
 *   direct and Variant forms freely.
 *
 * @par Extensibility
 * User types can be serialized by implementing free-standing
 * `operator<<(DataStream &, const MyType &)` and
 * `operator>>(DataStream &, MyType &)`. The extension API exposed on
 * DataStream is writeTag(), readTag(), and setError() — use the first
 * two to frame your data with a tag so type mismatches are caught on
 * read, and the last to report meaningful errors with context.
 *
 * @par Extension example
 * @code
 * // Suppose we have:
 * struct Waypoint {
 *     String name;
 *     double lat;
 *     double lon;
 * };
 *
 * // Allocate a stable tag for Waypoint. Values 0x80-0xFF are free for
 * // user types; the built-ins own 0x00-0x7F.
 * inline constexpr DataStream::TypeId TypeWaypoint =
 *     static_cast<DataStream::TypeId>(0x80);
 *
 * inline DataStream &operator<<(DataStream &s, const Waypoint &w) {
 *     s.writeTag(TypeWaypoint);
 *     s << w.name << w.lat << w.lon;
 *     return s;
 * }
 *
 * inline DataStream &operator>>(DataStream &s, Waypoint &w) {
 *     if(!s.readTag(TypeWaypoint)) { w = Waypoint(); return s; }
 *     s >> w.name >> w.lat >> w.lon;
 *     if(s.status() != DataStream::Ok) { w = Waypoint(); return s; }
 *     if(w.lat < -90.0 || w.lat > 90.0) {
 *         s.setError(DataStream::ReadCorruptData,
 *             String::sprintf("Waypoint latitude %f out of range", w.lat));
 *     }
 *     return s;
 * }
 *
 * // Waypoint now flows through every DataStream facility automatically:
 * // - Direct writes:      stream << waypoint;
 * // - Container writes:   stream << List<Waypoint>{...};
 * // - Result-based reads: auto [w, err] = stream.read<Waypoint>();
 * @endcode
 *
 * See @ref VariantDatabase for a non-trivial example that uses this
 * pattern for a whole key-value store.
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
 *
 * // Or, using the Result-based read API:
 * device.seek(0);
 * DataStream rs = DataStream::createReader(&device);
 * auto [n, err] = rs.read<int32_t>();
 * if(err.isOk()) { ... }
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
                 * @brief Type identifiers written before each value.
                 *
                 * Every operator<< writes a two-byte TypeId before the
                 * payload, honouring the stream's byte order.  Every
                 * operator>> reads and validates it.  A mismatch sets
                 * status to ReadCorruptData.
                 *
                 * Raw byte methods (readRawData, writeRawData, skipRawData)
                 * do NOT write or expect type tags — they are unframed.
                 *
                 * Variants share tag values with their direct counterparts:
                 * writing a `Variant` holding a `UUID` emits the same bytes
                 * as writing a direct `UUID`. This lets readers switch between
                 * direct and Variant forms without coordinating wire layouts.
                 *
                 * @par Tag namespace
                 * The 16-bit tag space is partitioned so library types and
                 * user-defined extensions never collide:
                 *  - @c 0x0000 – @c 0x3FFF — reserved for built-in library
                 *    types (values assigned by this enum).
                 *  - @c 0x4000 – @c 0xFFFF — open for user / application
                 *    extension tags.  Users should pick values at the top
                 *    end to stay clear of any future library growth.
                 * @see @c UserTypeIdBegin / @c UserTypeIdEnd below.
                 */
                enum TypeId : uint16_t {
                        // Primitives ---------------------------------------------
                        TypeInt8 = 0x01,    ///< @brief int8_t
                        TypeUInt8 = 0x02,   ///< @brief uint8_t
                        TypeInt16 = 0x03,   ///< @brief int16_t
                        TypeUInt16 = 0x04,  ///< @brief uint16_t
                        TypeInt32 = 0x05,   ///< @brief int32_t
                        TypeUInt32 = 0x06,  ///< @brief uint32_t
                        TypeInt64 = 0x07,   ///< @brief int64_t
                        TypeUInt64 = 0x08,  ///< @brief uint64_t
                        TypeFloat = 0x09,   ///< @brief float (IEEE 754)
                        TypeDouble = 0x0A,  ///< @brief double (IEEE 754)
                        TypeBool = 0x0B,    ///< @brief bool (as uint8_t)
                        TypeString = 0x0C,  ///< @brief Length-prefixed UTF-8 String
                        TypeBuffer = 0x0D,  ///< @brief Length-prefixed raw bytes
                        TypeInvalid = 0x0E, ///< @brief Explicit invalid marker (empty payload)

                        // Data objects (inner values are tagged primitives) -----
                        TypeUUID = 0x10,           ///< @brief UUID (16 raw bytes)
                        TypeDateTime = 0x11,       ///< @brief DateTime (tagged int64 ns since epoch)
                        TypeTimeStamp = 0x12,      ///< @brief TimeStamp (tagged int64 ns since epoch)
                        TypeSize2D = 0x13,         ///< @brief Size2DTemplate<T> (two tagged primitives)
                        TypeRational = 0x14,       ///< @brief Rational<T> (two tagged primitives)
                        TypeFrameRate = 0x15,      ///< @brief FrameRate (two tagged uint32)
                        TypeTimecode = 0x16,       ///< @brief Timecode (length-prefixed string)
                        TypeColor = 0x17,          ///< @brief Color (length-prefixed string)
                        TypeColorModel = 0x18,     ///< @brief ColorModel (length-prefixed name)
                        TypeMemSpace = 0x19,       ///< @brief MemSpace (tagged uint32 ID)
                        TypePixelMemLayout = 0x1A, ///< @brief PixelMemLayout (length-prefixed name)
                        TypePixelFormat = 0x1B,    ///< @brief PixelFormat (length-prefixed name)
                        TypeEnum = 0x1C,           ///< @brief Enum (length-prefixed qualified name)
                        TypeStringList = 0x1D,     ///< @brief StringList (tagged uint32 count + N strings)
                        TypeRect = 0x1E,           ///< @brief Rect<T> (four tagged primitives)
                        TypePoint = 0x1F,          ///< @brief Point<T,N> (tagged uint32 dims + N tagged primitives)

                        // Containers --------------------------------------------
                        TypeList = 0x20,    ///< @brief List<T> (tagged uint32 count + N tagged elements)
                        TypeMap = 0x21,     ///< @brief Map<K,V> (tagged uint32 count + N key/value pairs)
                        TypeSet = 0x22,     ///< @brief Set<T> (tagged uint32 count + N tagged elements)
                        TypeHashMap = 0x23, ///< @brief HashMap<K,V> (tagged uint32 count + N key/value pairs)
                        TypeHashSet = 0x24, ///< @brief HashSet<T> (tagged uint32 count + N tagged elements)

                        // Shareable types ---------------------------------------
                        TypeJsonObject = 0x30, ///< @brief JsonObject (length-prefixed serialized form)
                        TypeJsonArray = 0x31,  ///< @brief JsonArray (length-prefixed serialized form)
                        TypeXYZColor = 0x32,   ///< @brief XYZColor (three tagged doubles)
                        TypeAudioDesc = 0x33,  ///< @brief AudioDesc (format + sample rate + channels + metadata)
                        TypeImageDesc =
                                0x34, ///< @brief ImageDesc (size + pixel desc + line pad + align + interlaced + metadata)
                        TypeMediaDesc = 0x35, ///< @brief MediaDesc (frame rate + image list + audio list + metadata)
                        TypeUMID = 0x36,      ///< @brief UMID (uint8 length = 32 or 64, then N raw bytes)
                        TypeEnumList =
                                0x37, ///< @brief EnumList (type name + tagged uint32 count + N tagged int32 values)
                        TypeMediaTimeStamp = 0x38,      ///< @brief MediaTimeStamp (length-prefixed string round-trip)
                        TypeMacAddress = 0x39,          ///< @brief MacAddress (length-prefixed string round-trip)
                        TypeEUI64 = 0x3A,               ///< @brief EUI64 (length-prefixed string round-trip)
                        TypeMediaPipelineStage = 0x3B,  ///< @brief MediaPipelineConfig::Stage
                        TypeMediaPipelineRoute = 0x3C,  ///< @brief MediaPipelineConfig::Route
                        TypeMediaPipelineConfig = 0x3D, ///< @brief MediaPipelineConfig (metadata + stages + routes)
                        TypeMediaPipelineStats = 0x3E,  ///< @brief MediaPipelineStats (per-stage + aggregate)
                        TypeVideoFormat = 0x3F,         ///< @brief VideoFormat (length-prefixed string round-trip)

                        // HDR color metadata ------------------------------------
                        TypeMasteringDisplay = 0x40,  ///< @brief MasteringDisplay (SMPTE ST 2086): 10 tagged doubles
                        TypeContentLightLevel = 0x41, ///< @brief ContentLightLevel (CTA-861.3): two tagged uint32

                        // MediaIO introspection ---------------------------------
                        TypeMediaIODescription =
                                0x42, ///< @brief MediaIODescription (identity + role + format landscape + capabilities)

                        // Frame timeline types ----------------------------------
                        TypeFrameNumber = 0x43,   ///< @brief FrameNumber (length-prefixed string round-trip)
                        TypeFrameCount = 0x44,    ///< @brief FrameCount (length-prefixed string round-trip)
                        TypeMediaDuration = 0x45, ///< @brief MediaDuration (length-prefixed string round-trip)
                        TypeUrl = 0x46,           ///< @brief Url (length-prefixed string round-trip)
                        TypeAudioFormat = 0x47,   ///< @brief AudioFormat (length-prefixed name)
                        TypeMediaPayload =
                                0x48,        ///< @brief MediaPayload (FourCC + common state + subclass-serialised tail)
                        TypeDuration = 0x49, ///< @brief Duration (int64 nanoseconds)
                        TypeSocketAddress = 0x4A, ///< @brief SocketAddress (length-prefixed string round-trip)
                        TypeSdpSession = 0x4B,    ///< @brief SdpSession (length-prefixed string round-trip, RFC 4566)
                        TypeVideoCodec = 0x4C,    ///< @brief VideoCodec (length-prefixed "Codec[:Backend]" round-trip)
                        TypeAudioCodec = 0x4D,    ///< @brief AudioCodec (length-prefixed "Codec[:Backend]" round-trip)
                        TypeAudioChannelMap =
                                0x4E, ///< @brief AudioChannelMap (tagged uint32 count + N (streamName, role) pairs)
                        TypeAudioStreamDesc = 0x4F, ///< @brief AudioStreamDesc (length-prefixed registered name)
                        TypeWindowedStat =
                                0x50, ///< @brief WindowedStat (uint32 capacity + uint32 count + N tagged doubles)
                        TypeWindowedStatsBundle =
                                0x51 ///< @brief WindowedStatsBundle (uint32 count + N (String name, WindowedStat) pairs)
                };

                /**
                 * @brief First tag value available for user / application
                 *        extension types.
                 *
                 * The library reserves @c 0x0000 – @c 0x3FFF for its own
                 * use.  Anything at or above @c UserTypeIdBegin belongs
                 * to the application.  Pick values at the top end
                 * (approaching @c UserTypeIdEnd) to stay clear of
                 * potential library growth.
                 */
                static constexpr uint16_t UserTypeIdBegin = 0x4000;

                /** @brief Largest legal user tag value (inclusive). */
                static constexpr uint16_t UserTypeIdEnd = 0xFFFF;

                /**
                 * @brief Current wire format version.
                 *
                 * Version 2 widened the @ref TypeId tag from 8 to 16
                 * bits.  Older @c v1 streams are not forward-compatible.
                 */
                static constexpr uint16_t CurrentVersion = 2;

                /** @brief Total size of the stream header in bytes. */
                static constexpr size_t HeaderSize = 16;

                /** @brief Magic bytes identifying a DataStream ("PMDS"). */
                static constexpr uint8_t Magic[4] = {0x50, 0x4D, 0x44, 0x53};

                /**
                 * @brief Constructs a DataStream for writing on an IODevice.
                 *
                 * The device must already be open for writing. The header
                 * (magic + version + byte-order marker) is written
                 * immediately; the marker records @p order so readers can
                 * auto-configure. If the write fails, status() will
                 * reflect the error.
                 *
                 * @param device The IODevice to write to.
                 * @param order  Byte order for multi-byte values (default BigEndian).
                 */
                static DataStream createWriter(IODevice *device, ByteOrder order = BigEndian);

                /**
                 * @brief Constructs a DataStream for reading from an IODevice.
                 *
                 * The device must already be open for reading. The header
                 * (magic + version + byte-order marker) is read and
                 * validated immediately. The stream's byte order is
                 * auto-configured from the header marker. If the header
                 * is missing or invalid, status() will be set to
                 * ReadCorruptData.
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
                 * @brief Returns a human-readable description of the last error.
                 *
                 * The context string is set at the failure site and
                 * describes what specifically went wrong (e.g. "expected
                 * tag 0x11 (TypeDateTime), got 0x0C (TypeString)"). It is
                 * empty when the status is Ok.
                 *
                 * @return Reference to the context string.
                 */
                const String &errorContext() const { return _errorContext; }

                /**
                 * @brief Returns the status mapped to an Error code.
                 *
                 * Useful when the stream's status needs to participate in
                 * the library's standard Error-returning flow.
                 *
                 * @return Error::Ok, Error::EndOfFile, Error::CorruptData,
                 *         or Error::IOError.
                 */
                Error toError() const;

                /**
                 * @brief Resets the stream status to Ok and clears the context.
                 */
                void resetStatus();

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
                /** @brief Writes a shared Buffer as length-prefixed raw bytes. */
                DataStream &operator<<(const Buffer::Ptr &val);
                /** @brief Writes a Variant using the same tag as the value's direct form. */
                DataStream &operator<<(const Variant &val);

                // ============================================================
                // Write operators — data objects
                // ============================================================

                /** @brief Writes a UUID as 16 raw bytes. */
                DataStream &operator<<(const UUID &val);
                /** @brief Writes a UMID as a uint8 length (32 or 64) followed by N raw bytes. */
                DataStream &operator<<(const UMID &val);
                /** @brief Writes a DateTime as int64 nanoseconds since epoch. */
                DataStream &operator<<(const DateTime &val);
                /** @brief Writes a TimeStamp as int64 nanoseconds since epoch. */
                DataStream &operator<<(const TimeStamp &val);
                /** @brief Writes a FrameRate as two uint32 values. */
                DataStream &operator<<(const FrameRate &val);
                /** @brief Writes a VideoFormat as its canonical string representation. */
                DataStream &operator<<(const VideoFormat &val);
                /** @brief Writes a Timecode as its canonical string representation. */
                DataStream &operator<<(const Timecode &val);
                /** @brief Writes a Color as its lossless ModelFormat string. */
                DataStream &operator<<(const Color &val);
                /** @brief Writes a ColorModel as its name. */
                DataStream &operator<<(const ColorModel &val);
                /** @brief Writes a MemSpace as its numeric ID. */
                DataStream &operator<<(const MemSpace &val);
                /** @brief Writes a PixelMemLayout as its name. */
                DataStream &operator<<(const PixelMemLayout &val);
                /** @brief Writes a PixelFormat as its name. */
                DataStream &operator<<(const PixelFormat &val);
                /** @brief Writes an AudioFormat as its name. */
                DataStream &operator<<(const AudioFormat &val);
                /** @brief Writes an Enum as its qualified "TypeName::ValueName" string. */
                DataStream &operator<<(const Enum &val);
                /** @brief Writes an EnumList as its type name + tagged count + tagged int32 values. */
                DataStream &operator<<(const EnumList &val);
                /** @brief Writes a MediaTimeStamp as a length-prefixed string. */
                DataStream &operator<<(const MediaTimeStamp &val);
                /** @brief Writes a FrameNumber as a length-prefixed string. */
                DataStream &operator<<(const FrameNumber &val);
                /** @brief Writes a FrameCount as a length-prefixed string. */
                DataStream &operator<<(const FrameCount &val);
                /** @brief Writes a MediaDuration as a length-prefixed string. */
                DataStream &operator<<(const MediaDuration &val);
                /** @brief Writes a Duration as a tagged int64 nanoseconds count. */
                DataStream &operator<<(const Duration &val);
                /** @brief Writes a MacAddress as a length-prefixed string. */
                DataStream &operator<<(const MacAddress &val);
                /** @brief Writes an EUI64 as a length-prefixed string. */
                DataStream &operator<<(const EUI64 &val);
                /** @brief Writes a StringList as uint32 count + length-prefixed elements. */
                DataStream &operator<<(const StringList &val);
                /** @brief Writes a Url as a length-prefixed string (toString form). */
                DataStream &operator<<(const Url &val);
                /** @brief Writes a VideoCodec as a length-prefixed "Codec[:Backend]" string. */
                DataStream &operator<<(const VideoCodec &val);
                /** @brief Writes an AudioCodec as a length-prefixed "Codec[:Backend]" string. */
                DataStream &operator<<(const AudioCodec &val);
                /** @brief Writes a SocketAddress as a tagged "host:port" string. */
                DataStream &operator<<(const SocketAddress &val);
                /** @brief Writes an SdpSession as a tagged RFC 4566 SDP string. */
                DataStream &operator<<(const SdpSession &val);

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
                /** @brief Reads a shared Buffer, allocating a new Buffer and wrapping it. */
                DataStream &operator>>(Buffer::Ptr &val);
                /** @brief Reads any value into a Variant, dispatching on the tag. */
                DataStream &operator>>(Variant &val);

                // ============================================================
                // Read operators — data objects
                // ============================================================

                /** @brief Reads a UUID from 16 raw bytes. */
                DataStream &operator>>(UUID &val);
                /** @brief Reads a UMID from a uint8 length (32 or 64) followed by N raw bytes. */
                DataStream &operator>>(UMID &val);
                /** @brief Reads a DateTime from a tagged int64 ns-since-epoch. */
                DataStream &operator>>(DateTime &val);
                /** @brief Reads a TimeStamp from a tagged int64 ns-since-epoch. */
                DataStream &operator>>(TimeStamp &val);
                /** @brief Reads a FrameRate from two tagged uint32 values. */
                DataStream &operator>>(FrameRate &val);
                /** @brief Reads a VideoFormat from its canonical string representation. */
                DataStream &operator>>(VideoFormat &val);
                /** @brief Reads a Timecode from its canonical string representation. */
                DataStream &operator>>(Timecode &val);
                /** @brief Reads a Color from its lossless ModelFormat string. */
                DataStream &operator>>(Color &val);
                /** @brief Reads a ColorModel by name. */
                DataStream &operator>>(ColorModel &val);
                /** @brief Reads a MemSpace from a tagged uint32 ID. */
                DataStream &operator>>(MemSpace &val);
                /** @brief Reads a PixelMemLayout by name. */
                DataStream &operator>>(PixelMemLayout &val);
                /** @brief Reads a PixelFormat by name. */
                DataStream &operator>>(PixelFormat &val);
                /** @brief Reads an AudioFormat by name. */
                DataStream &operator>>(AudioFormat &val);
                /** @brief Reads an Enum from its qualified "TypeName::ValueName" string. */
                DataStream &operator>>(Enum &val);
                /** @brief Reads an EnumList from type name + tagged count + tagged int32 values. */
                DataStream &operator>>(EnumList &val);
                /** @brief Reads a MediaTimeStamp from a length-prefixed string. */
                DataStream &operator>>(MediaTimeStamp &val);
                /** @brief Reads a FrameNumber from a length-prefixed string. */
                DataStream &operator>>(FrameNumber &val);
                /** @brief Reads a FrameCount from a length-prefixed string. */
                DataStream &operator>>(FrameCount &val);
                /** @brief Reads a MediaDuration from a length-prefixed string. */
                DataStream &operator>>(MediaDuration &val);
                /** @brief Reads a Duration from a tagged int64 nanoseconds count. */
                DataStream &operator>>(Duration &val);
                /** @brief Reads a MacAddress from a length-prefixed string. */
                DataStream &operator>>(MacAddress &val);
                /** @brief Reads an EUI64 from a length-prefixed string. */
                DataStream &operator>>(EUI64 &val);
                /** @brief Reads a StringList from tagged count + length-prefixed elements. */
                DataStream &operator>>(StringList &val);
                /** @brief Reads a Url from a tagged length-prefixed string. */
                DataStream &operator>>(Url &val);
                /** @brief Reads a VideoCodec from a tagged "Codec[:Backend]" string. */
                DataStream &operator>>(VideoCodec &val);
                /** @brief Reads an AudioCodec from a tagged "Codec[:Backend]" string. */
                DataStream &operator>>(AudioCodec &val);
                /** @brief Reads a SocketAddress from a tagged "host:port" string. */
                DataStream &operator>>(SocketAddress &val);
                /** @brief Reads an SdpSession from a tagged RFC 4566 SDP string. */
                DataStream &operator>>(SdpSession &val);

                // ============================================================
                // Result<T>-returning read API
                // ============================================================

                /**
                 * @brief Reads a value of type @p T and returns it as a Result.
                 *
                 * If the stream is already in an error state or the read
                 * fails, the Result's Error is set (mapping from the stream's
                 * Status via toError()). Otherwise the Result contains the
                 * value with Error::Ok.
                 *
                 * @tparam T Any type with a matching operator>>(DataStream&, T&).
                 * @return A Result<T> with the value and success/error.
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
                 * @brief Writes a type tag byte to the stream.
                 *
                 * Public so that user-written operator<< overloads can emit
                 * the same framing as the built-in operators.
                 * @param id The TypeId to write.
                 */
                void writeTag(TypeId id);

                /**
                 * @brief Reads a type tag byte and validates it.
                 *
                 * If the tag does not match @p expected, sets status to
                 * ReadCorruptData with a descriptive context message. Public
                 * so that user-written operator>> overloads can validate
                 * their framing.
                 * @param expected The expected TypeId.
                 * @return True if the tag matched.
                 */
                bool readTag(TypeId expected);

                /**
                 * @brief Sets the status and a context message in one call.
                 *
                 * Public so that user-written operator<< / >> overloads can
                 * report meaningful errors. No-op if the stream is already
                 * in a non-Ok state (first error wins).
                 *
                 * @param s   The new status.
                 * @param ctx A descriptive context string.
                 */
                void setError(Status s, String ctx);

                // ============================================================
                // Raw byte access (untagged escape hatch)
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
                 * @brief Reads a 16-bit type tag without validation.
                 *
                 * Used by Variant reads and peek-style dispatch. Sets status
                 * to ReadPastEnd if the bytes cannot be read.
                 * @return The tag value, or 0 on failure.
                 */
                uint16_t readAnyTag();

                /**
                 * @brief Reads a Variant payload for the given TypeId tag.
                 *
                 * Called after the tag has been consumed. Dispatches to the
                 * appropriate readXXXData() helper and stores the resulting
                 * value in @p val.
                 * @param id The tag that was consumed from the stream.
                 * @param val The Variant to populate.
                 */
                void readVariantPayload(TypeId id, Variant &val);

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

                // Untagged value write helpers
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
                void writeBufferData(const Buffer &val);
                void writeUUIDData(const UUID &val);
                void writeUMIDData(const UMID &val);
                void writeDateTimeData(const DateTime &val);
                void writeTimeStampData(const TimeStamp &val);
                void writeFrameRateData(const FrameRate &val);
                void writeVideoFormatData(const VideoFormat &val);
                void writeTimecodeData(const Timecode &val);
                void writeColorData(const Color &val);
                void writeColorModelData(const ColorModel &val);
                void writeMemSpaceData(const MemSpace &val);
                void writePixelMemLayoutData(const PixelMemLayout &val);
                void writePixelFormatData(const PixelFormat &val);
                void writeAudioFormatData(const AudioFormat &val);
                void writeEnumData(const Enum &val);
                void writeEnumListData(const EnumList &val);
                void writeStringListData(const StringList &val);

                // Untagged value read helpers
                int8_t         readInt8();
                uint8_t        readUInt8();
                int16_t        readInt16();
                uint16_t       readUInt16();
                int32_t        readInt32();
                uint32_t       readUInt32();
                int64_t        readInt64();
                uint64_t       readUInt64();
                float          readFloat();
                double         readDouble();
                bool           readBoolValue();
                String         readStringData();
                Buffer         readBufferData();
                UUID           readUUIDData();
                UMID           readUMIDData();
                DateTime       readDateTimeData();
                TimeStamp      readTimeStampData();
                FrameRate      readFrameRateData();
                VideoFormat    readVideoFormatData();
                Timecode       readTimecodeData();
                Color          readColorData();
                ColorModel     readColorModelData();
                MemSpace       readMemSpaceData();
                PixelMemLayout readPixelMemLayoutData();
                PixelFormat    readPixelFormatData();
                AudioFormat    readAudioFormatData();
                Enum           readEnumData();
                EnumList       readEnumListData();
                StringList     readStringListData();

                /**
                 * @brief Swaps byte order of a value in-place if needed.
                 * @tparam T The type to byte-swap (2, 4, or 8 bytes).
                 * @param val The value to potentially swap.
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

                /**
                 * @brief Returns the native byte order of the platform.
                 * @return BigEndian or LittleEndian.
                 */
                static ByteOrder nativeByteOrder() {
                        static const uint16_t val = 1;
                        return (*reinterpret_cast<const uint8_t *>(&val) == 1) ? LittleEndian : BigEndian;
                }

                IODevice *_device = nullptr;
                ByteOrder _byteOrder = BigEndian;
                uint16_t  _version = 0;
                Status    _status = Ok;
                String    _errorContext;
};

// ============================================================================
// Container template operators
// ============================================================================
//
// These let arbitrary List<T>, Map<K,V>, and Set<T> flow through a DataStream
// as long as T (and K, V) already have operator<< / operator>> overloads.
// They write a tag (TypeList / TypeMap / TypeSet) followed by a uint32_t
// count and then the fully-tagged elements.

namespace detail {
        /** @brief Maximum element count accepted by container reads, to prevent
 *         runaway allocations on corrupt input. */
        inline constexpr uint32_t DataStreamMaxContainerCount = 256u * 1024u * 1024u;
} // namespace detail

// ============================================================================
// Geometry template operators
// ============================================================================
//
// These share a single tag per kind (TypeSize2D, TypeRect, TypePoint,
// TypeRational) and rely on the inner values being written/read via their
// own primitive operators, which means the inner values carry their own
// type tags. This lets one Size2DTemplate<T> template cover uint32_t,
// int32_t, float, etc. transparently, and a reader automatically catches
// element-type mismatches via the existing tag-validation path.

/**
 * @brief Writes a Size2DTemplate as tag + tagged width + tagged height.
 * @tparam T Element type of the Size2DTemplate (e.g. uint32_t, int32_t).
 * @param stream The stream to write to.
 * @param sz     The size to serialize.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const Size2DTemplate<T> &sz) {
        stream.writeTag(DataStream::TypeSize2D);
        stream << sz.width() << sz.height();
        return stream;
}

/**
 * @brief Reads a Size2DTemplate, validating the tag and element types.
 * @tparam T Element type of the Size2DTemplate.
 * @param stream The stream to read from.
 * @param sz     The size to populate.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator>>(DataStream &stream, Size2DTemplate<T> &sz) {
        if (!stream.readTag(DataStream::TypeSize2D)) {
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
 * @tparam T Underlying integer type of the Rational.
 * @param stream The stream to write to.
 * @param r      The rational to serialize.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const Rational<T> &r) {
        stream.writeTag(DataStream::TypeRational);
        stream << r.numerator() << r.denominator();
        return stream;
}

/**
 * @brief Reads a Rational, validating the tag and element types.
 * @tparam T Underlying integer type of the Rational.
 * @param stream The stream to read from.
 * @param r      The rational to populate.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator>>(DataStream &stream, Rational<T> &r) {
        if (!stream.readTag(DataStream::TypeRational)) {
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
 * @tparam T Element type of the Rect.
 * @param stream The stream to write to.
 * @param rect   The rectangle to serialize.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const Rect<T> &rect) {
        stream.writeTag(DataStream::TypeRect);
        stream << rect.x() << rect.y() << rect.width() << rect.height();
        return stream;
}

/**
 * @brief Reads a Rect, validating the tag and element types.
 * @tparam T Element type of the Rect.
 * @param stream The stream to read from.
 * @param rect   The rectangle to populate.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator>>(DataStream &stream, Rect<T> &rect) {
        if (!stream.readTag(DataStream::TypeRect)) {
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
 *
 * Writing the dimension count lets readers validate that the stored
 * Point has the same N they expect; a mismatch is reported as
 * ReadCorruptData.
 *
 * @tparam T Element type of the Point.
 * @tparam N Number of dimensions of the Point.
 * @param stream The stream to write to.
 * @param point  The point to serialize.
 * @return The stream, for chaining.
 */
template <typename T, size_t N> DataStream &operator<<(DataStream &stream, const Point<T, N> &point) {
        stream.writeTag(DataStream::TypePoint);
        stream << static_cast<uint32_t>(N);
        const Array<T, N> &arr = point;
        for (size_t i = 0; i < N; ++i) stream << arr[i];
        return stream;
}

/**
 * @brief Reads a Point, validating the tag, dimension count, and element types.
 * @tparam T Element type of the Point.
 * @tparam N Number of dimensions of the Point.
 * @param stream The stream to read from.
 * @param point  The point to populate.
 * @return The stream, for chaining.
 */
template <typename T, size_t N> DataStream &operator>>(DataStream &stream, Point<T, N> &point) {
        if (!stream.readTag(DataStream::TypePoint)) {
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

// ============================================================================
// XYZColor operators
// ============================================================================
//
// XYZColor lives in xyzcolor.h which is already pulled in transitively via
// variant.h → color.h → colormodel.h, so we define its stream operators
// here to avoid a circular include.

/**
 * @brief Writes an XYZColor as tag + three tagged doubles.
 * @param stream The stream to write to.
 * @param col    The XYZColor to serialize.
 * @return The stream, for chaining.
 */
inline DataStream &operator<<(DataStream &stream, const XYZColor &col) {
        stream.writeTag(DataStream::TypeXYZColor);
        const XYZColor::DataType &arr = col.data();
        stream << arr[0] << arr[1] << arr[2];
        return stream;
}

/**
 * @brief Reads an XYZColor from tag + three tagged doubles.
 * @param stream The stream to read from.
 * @param col    The XYZColor to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, XYZColor &col) {
        if (!stream.readTag(DataStream::TypeXYZColor)) {
                col = XYZColor();
                return stream;
        }
        double x = 0.0, y = 0.0, z = 0.0;
        stream >> x >> y >> z;
        if (stream.status() != DataStream::Ok) {
                col = XYZColor();
                return stream;
        }
        col = XYZColor(x, y, z);
        return stream;
}

// ============================================================================
// MasteringDisplay / ContentLightLevel operators
// ============================================================================
//
// Both types flow in through variant.h (HDR color metadata carried by Variant
// and Metadata).  Inline here so they share datastream.h's include chain and
// stay near the rest of the compact color types.

/**
 * @brief Writes a MasteringDisplay as tag + ten tagged doubles.
 *
 * Encoded as: red.x, red.y, green.x, green.y, blue.x, blue.y,
 * whitePoint.x, whitePoint.y, minLuminance, maxLuminance.
 *
 * @param stream The stream to write to.
 * @param md     The MasteringDisplay to serialize.
 * @return The stream, for chaining.
 */
inline DataStream &operator<<(DataStream &stream, const MasteringDisplay &md) {
        stream.writeTag(DataStream::TypeMasteringDisplay);
        stream << md.red().x() << md.red().y() << md.green().x() << md.green().y() << md.blue().x() << md.blue().y()
               << md.whitePoint().x() << md.whitePoint().y() << md.minLuminance() << md.maxLuminance();
        return stream;
}

/**
 * @brief Reads a MasteringDisplay, validating the tag and ten tagged doubles.
 * @param stream The stream to read from.
 * @param md     The MasteringDisplay to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, MasteringDisplay &md) {
        if (!stream.readTag(DataStream::TypeMasteringDisplay)) {
                md = MasteringDisplay();
                return stream;
        }
        double rx = 0.0, ry = 0.0, gx = 0.0, gy = 0.0;
        double bx = 0.0, by = 0.0, wx = 0.0, wy = 0.0;
        double minL = 0.0, maxL = 0.0;
        stream >> rx >> ry >> gx >> gy >> bx >> by >> wx >> wy >> minL >> maxL;
        if (stream.status() != DataStream::Ok) {
                md = MasteringDisplay();
                return stream;
        }
        md = MasteringDisplay(CIEPoint(rx, ry), CIEPoint(gx, gy), CIEPoint(bx, by), CIEPoint(wx, wy), minL, maxL);
        return stream;
}

/**
 * @brief Writes a ContentLightLevel as tag + two tagged uint32 values.
 *
 * Encoded as: maxCLL, maxFALL.
 *
 * @param stream The stream to write to.
 * @param cll    The ContentLightLevel to serialize.
 * @return The stream, for chaining.
 */
inline DataStream &operator<<(DataStream &stream, const ContentLightLevel &cll) {
        stream.writeTag(DataStream::TypeContentLightLevel);
        stream << cll.maxCLL() << cll.maxFALL();
        return stream;
}

/**
 * @brief Reads a ContentLightLevel, validating the tag and two tagged uint32 values.
 * @param stream The stream to read from.
 * @param cll    The ContentLightLevel to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, ContentLightLevel &cll) {
        if (!stream.readTag(DataStream::TypeContentLightLevel)) {
                cll = ContentLightLevel();
                return stream;
        }
        uint32_t maxCLL = 0, maxFALL = 0;
        stream >> maxCLL >> maxFALL;
        if (stream.status() != DataStream::Ok) {
                cll = ContentLightLevel();
                return stream;
        }
        cll = ContentLightLevel(maxCLL, maxFALL);
        return stream;
}

/**
 * @brief Writes a List as tag + uint32 count + N tagged elements.
 * @tparam T Any element type with an existing operator<<(DataStream&, const T&).
 * @param stream The stream to write to.
 * @param list   The list to serialize.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const List<T> &list) {
        stream.writeTag(DataStream::TypeList);
        stream << static_cast<uint32_t>(list.size());
        for (const auto &item : list) stream << item;
        return stream;
}

/**
 * @brief Reads a List, verifying the tag and length prefix.
 * @tparam T Any element type with an existing operator>>(DataStream&, T&).
 * @param stream The stream to read from.
 * @param list   The list to populate (cleared first).
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator>>(DataStream &stream, List<T> &list) {
        list.clear();
        if (!stream.readTag(DataStream::TypeList)) return stream;
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
 * @tparam K Key type with an existing operator<<.
 * @tparam V Value type with an existing operator<<.
 * @param stream The stream to write to.
 * @param map    The map to serialize.
 * @return The stream, for chaining.
 */
template <typename K, typename V> DataStream &operator<<(DataStream &stream, const Map<K, V> &map) {
        stream.writeTag(DataStream::TypeMap);
        stream << static_cast<uint32_t>(map.size());
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
                stream << it->first << it->second;
        }
        return stream;
}

/**
 * @brief Reads a Map, verifying the tag and length prefix.
 * @tparam K Key type with an existing operator>>.
 * @tparam V Value type with an existing operator>>.
 * @param stream The stream to read from.
 * @param map    The map to populate (cleared first).
 * @return The stream, for chaining.
 */
template <typename K, typename V> DataStream &operator>>(DataStream &stream, Map<K, V> &map) {
        map.clear();
        if (!stream.readTag(DataStream::TypeMap)) return stream;
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
 * @tparam T Element type with an existing operator<<.
 * @param stream The stream to write to.
 * @param set    The set to serialize.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const Set<T> &set) {
        stream.writeTag(DataStream::TypeSet);
        stream << static_cast<uint32_t>(set.size());
        for (const auto &item : set) stream << item;
        return stream;
}

/**
 * @brief Reads a Set, verifying the tag and length prefix.
 * @tparam T Element type with an existing operator>>.
 * @param stream The stream to read from.
 * @param set    The set to populate (cleared first).
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator>>(DataStream &stream, Set<T> &set) {
        set.clear();
        if (!stream.readTag(DataStream::TypeSet)) return stream;
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
 * @tparam K Key type with an existing operator<<.
 * @tparam V Value type with an existing operator<<.
 * @param stream The stream to write to.
 * @param map    The hash map to serialize.
 * @return The stream, for chaining.
 */
template <typename K, typename V> DataStream &operator<<(DataStream &stream, const HashMap<K, V> &map) {
        stream.writeTag(DataStream::TypeHashMap);
        stream << static_cast<uint32_t>(map.size());
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
                stream << it->first << it->second;
        }
        return stream;
}

/**
 * @brief Reads a HashMap, verifying the tag and length prefix.
 *
 * Hash container iteration order is unspecified, so on-wire order
 * depends on the writer's hash seed. Round-trips preserve entries but
 * do not preserve insertion order.
 *
 * @tparam K Key type with an existing operator>>.
 * @tparam V Value type with an existing operator>>.
 * @param stream The stream to read from.
 * @param map    The hash map to populate (cleared first).
 * @return The stream, for chaining.
 */
template <typename K, typename V> DataStream &operator>>(DataStream &stream, HashMap<K, V> &map) {
        map.clear();
        if (!stream.readTag(DataStream::TypeHashMap)) return stream;
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
 * @tparam T Element type with an existing operator<<.
 * @param stream The stream to write to.
 * @param set    The hash set to serialize.
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator<<(DataStream &stream, const HashSet<T> &set) {
        stream.writeTag(DataStream::TypeHashSet);
        stream << static_cast<uint32_t>(set.size());
        for (const auto &item : set) stream << item;
        return stream;
}

/**
 * @brief Reads a HashSet, verifying the tag and length prefix.
 * @tparam T Element type with an existing operator>>.
 * @param stream The stream to read from.
 * @param set    The hash set to populate (cleared first).
 * @return The stream, for chaining.
 */
template <typename T> DataStream &operator>>(DataStream &stream, HashSet<T> &set) {
        set.clear();
        if (!stream.readTag(DataStream::TypeHashSet)) return stream;
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

PROMEKI_NAMESPACE_END
