/**
 * @file      datastream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <promeki/config.h>
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
#include <promeki/xyzcolor.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/result.h>
#include <promeki/iodevice.h>
#if PROMEKI_ENABLE_TLS
#include <promeki/sslcontext.h>
#endif

PROMEKI_NAMESPACE_BEGIN

class Variant;
class VariantList;
class VariantMap;
class JsonObject;
class JsonArray;
class XmlDocument;
class XmlElement;
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
class AncFormat;
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
 *   backward-compatible. The current version is 3.
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
 * After the header, every value is emitted as a self-describing
 * *frame* whose 8-byte header is naturally 4-byte aligned:
 * - Bytes 0-1: @c uint16_t TypeId tag (byte-order controlled).
 * - Bytes 2-3: @c uint16_t per-type version (byte-order
 *              controlled).  Lets individual types evolve their
 *              wire format independently of the stream-level
 *              version — a writer that learns a new representation
 *              of @c TypeUUID bumps just the UUID frame's version
 *              field, and readers that don't know the new version
 *              reject only that frame, not the whole stream.
 * - Bytes 4-7: @c uint32_t body size in bytes (byte-order
 *              controlled). Lets readers @ref skipFrame past a
 *              tag they don't understand without having to know
 *              the body layout. This is the foundation of forward
 *              compatibility: a future writer can introduce a new
 *              type and existing readers will skip it cleanly.
 * - Bytes 8…:  type-specific body bytes (exactly the size declared
 *              above).
 *
 * A mismatch between the expected tag and the actual tag during a
 * typed read sets the status to ReadCorruptData. A Variant read that
 * encounters an unknown tag consumes the body bytes via the size
 * field, yields a default Variant, and leaves the stream in @c Ok —
 * the explicit forward-compat path.
 *
 * @par Forward compatibility
 * The per-frame `[version][size]` pair is the foundation of the
 * format's forward-compatibility story:
 * - The @b version field lets a single TypeId evolve its body
 *   layout across releases.  When a type's encoding changes, its
 *   writer bumps the version literal and its reader gains a switch
 *   on @c ver.  Readers that haven't been updated reject the new
 *   version explicitly via the @ref readFrame @c maxVersion check
 *   instead of mis-parsing the body.
 * - The @b size field lets readers @ref skipFrame past types they
 *   don't know about at all.  This is what the Variant read does
 *   in its default case: an unknown tag is treated as a value the
 *   reader can't materialise, its body bytes are consumed via the
 *   declared size, and the stream stays usable for whatever comes
 *   next.  Older readers can decode the parts of a stream they
 *   understand even when a newer writer mixes in types they don't.
 *
 * @par Value encoding
 * Each per-frame body uses a type-specific encoding.  Multi-byte
 * integers are byte-order controlled via setByteOrder() (default
 * big-endian).  Common shapes used across the built-ins:
 * - Primitives — body is exactly the in-memory representation,
 *   byte-swapped if needed.  Floats and doubles use IEEE 754.
 * - Strings — body is `[uint32 length][UTF-8 bytes]` (no null
 *   terminator).  The length prefix is raw (not framed) so the
 *   body stays compact.
 * - Buffers — `[uint32 length][raw bytes]`, same convention.
 * - Data objects (UUID, DateTime, Timecode, FrameRate, …) — body
 *   is whatever native binary encoding the type defines.  Look at
 *   the per-overload doxygen for the exact shape.
 * - Containers (List, Map, Set, HashMap, HashSet) — body is a
 *   framed uint32 element count followed by each element as its
 *   own fully-framed value.  This means containers nest naturally
 *   and inherit the same forward-compat properties as the values
 *   they hold.
 * - Variants — emit exactly the same bytes as a direct write of the
 *   contained type.  A stream position holding a Variant carrying
 *   a UUID is byte-for-byte identical to one holding a direct UUID.
 *   Readers can flip between direct and Variant forms freely.
 *
 * @par Buffered writes (no seek required)
 * @ref beginFrame opens a frame whose body bytes accumulate in an
 * internal stack-allocated buffer.  Only when @ref endFrame is
 * called does the assembled `[tag][version][size][body]` group hit
 * the underlying device — that's the moment the size field is
 * computed from the body buffer's length.  The upshot is that
 * writers don't require a seekable IODevice; sockets, pipes,
 * file-descriptor adapters, and BufferIODevice all work.
 *
 * @par Extensibility
 * User types can be serialized by implementing free-standing
 * `operator<<(DataStream &, const MyType &)` and
 * `operator>>(DataStream &, MyType &)`. The extension API exposed on
 * DataStream is @ref beginFrame() / @ref endFrame() (write),
 * @ref readFrame() (typed read), and @ref setError() — wrap the body
 * in a frame so it's self-describing, and use setError to report
 * meaningful failures with context.
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
 * // Allocate a stable tag for Waypoint. Values >= UserTypeIdBegin
 * // are free for user types; the built-ins own everything below.
 * inline constexpr DataStream::TypeId TypeWaypoint =
 *     static_cast<DataStream::TypeId>(DataStream::UserTypeIdBegin);
 *
 * inline DataStream &operator<<(DataStream &s, const Waypoint &w) {
 *     // Open the frame at version 1.  Subsequent writes accumulate
 *     // in an internal body buffer; endFrame() emits the tag,
 *     // version, body size and the body in a single flush.
 *     s.beginFrame(TypeWaypoint, 1);
 *     s << w.name << w.lat << w.lon;
 *     s.endFrame();
 *     return s;
 * }
 *
 * inline DataStream &operator>>(DataStream &s, Waypoint &w) {
 *     // readFrame validates the tag (mismatch → ReadCorruptData)
 *     // and rejects versions newer than the maxVersion you pass
 *     // here.  When you later support v2, bump maxVersion and
 *     // branch on `ver`.
 *     uint16_t ver = 0;
 *     if(!s.readFrame(TypeWaypoint, 1, &ver)) {
 *         w = Waypoint();
 *         return s;
 *     }
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
                 * @brief Type identifiers written at the head of each frame.
                 *
                 * Every operator<< wraps its body in a frame whose
                 * header is `[tag(uint16)][version(uint16)][size(uint32)]`,
                 * honouring the stream's byte order.  Every operator>>
                 * reads the frame header and validates the tag — a
                 * mismatch sets status to ReadCorruptData.
                 *
                 * Raw byte methods (readRawData, writeRawData, skipRawData)
                 * do NOT emit or expect frame headers — they are unframed
                 * passthrough into the underlying device.
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
                                0x51, ///< @brief WindowedStatsBundle (uint32 count + N (String name, WindowedStat) pairs)
                        TypeAudioMarkerList =
                                0x52, ///< @brief AudioMarkerList (uint32 count + N (int64 offset, int64 length, int32 type))
                        TypeVariantList =
                                0x53, ///< @brief VariantList (uint32 count + N tagged Variants — same wire as List<Variant>)
                        TypeVariantMap =
                                0x54, ///< @brief VariantMap (uint32 count + N (String key, Variant value) — same wire as Map<String,Variant>)
                        TypeXmlDocument = 0x55, ///< @brief XmlDocument (length-prefixed serialized XML form)
                        TypeXmlElement = 0x56,  ///< @brief XmlElement (length-prefixed serialized XML form)
                        TypeSslContext = 0x57,  ///< @brief SslContext::Ptr — opaque, only the tag round-trips (read yields a null Ptr).
                        TypeAncFormat = 0x58,   ///< @brief AncFormat (length-prefixed name round-trip)
                        TypeAncPacket = 0x59,   ///< @brief AncPacket (tagged format + transport + Buffer + Metadata round-trip)
                        TypeAncDesc = 0x5A,     ///< @brief AncDesc (tagged raster + scan + rate + filter lists + metadata)
                        TypeCea708Cdp = 0x5B,   ///< @brief Cea708Cdp (tagged Buffer holding the SMPTE 334-2 CDP wire bytes)
                        TypeSubtitle = 0x5C,    ///< @brief Subtitle (start/end TimeStamps + anchor + region + speaker + Metadata + List<SubtitleSpan>)
                        TypeSubtitleSpan = 0x5D, ///< @brief SubtitleSpan (length-prefixed text + style flags + Color)
                        TypeScc = 0x5E,          ///< @brief Scc (length-prefixed List<Line> of {Timecode, List<uint16_t>})
                        TypeCea608 = 0x5F,       ///< @brief Cea608 (channel selector + cc_data triple list for one frame)
                        TypeCea708Service =
                                0x60, ///< @brief Cea708Service (uint8 service_number + length-prefixed Buffer of service-data bytes)
                        TypeCea708DtvccPacket =
                                0x61, ///< @brief Cea708DtvccPacket (uint8 sequence_number + uint32 count + N tagged Cea708Service entries)
                        TypeHdrStaticMetadata =
                                0x62, ///< @brief HdrStaticMetadata (tagged Buffer holding the CTA-861-G DRM InfoFrame body)
                        TypeHdrDynamic2094_40 =
                                0x63, ///< @brief HdrDynamic2094_40 (tagged Buffer holding the canonical SMPTE ST 2094-40 bitstream)
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
                 * Version 3 introduced the per-value frame header
                 * (`[tag(2)][version(2)][size(4)]`, naturally 4-byte
                 * aligned).  The size field lets readers skip past
                 * tags they don't understand; the version field lets
                 * individual types evolve their wire format
                 * independently.  Older streams (v1 / v2) are not
                 * forward-compatible — there's no way to derive a
                 * per-frame size from them.
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
                /**
                 * @brief Writes a Variant using the same tag as the value's direct form.
                 *
                 * Dispatches on the Variant's runtime type to the
                 * appropriate concrete @c operator<< overload, so the
                 * emitted bytes are bit-identical to writing the
                 * contained value directly.  A @c TypeInvalid
                 * (default-constructed) Variant emits a frame with
                 * a zero-byte body.
                 */
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
                /** @brief Writes an AncFormat as its name. */
                DataStream &operator<<(const AncFormat &val);
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
#if PROMEKI_ENABLE_TLS
                /**
                 * @brief Writes an SslContext::Ptr — opaque, only the tag is emitted.
                 *
                 * SslContext has no canonical persistent form (its
                 * mbedTLS state is process-local), so this overload
                 * exists purely so SslContext::Ptr can travel through
                 * the Variant infrastructure that DataStream's
                 * round-trip dispatch table requires.  Reading the
                 * stream back yields a null Ptr — callers that need
                 * the actual context must wire it up out of band.
                 */
                DataStream &operator<<(const SharedPtr<SslContext, false> &val);
#endif

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
                 * Consumes the frame header, dispatches on the tag
                 * to the matching concrete reader, and stores the
                 * result in @p val.  When the tag isn't in the
                 * dispatch table — typically because a newer writer
                 * emitted a type this reader doesn't know — the
                 * body bytes are consumed via the size field, @p
                 * val is left default-constructed, and the stream
                 * status stays @c Ok.  This is what makes a Variant
                 * read forward-compatible: older code can drain a
                 * stream produced by newer code without erroring on
                 * the unknown frames it contains.
                 */
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
                /** @brief Reads an AncFormat by name. */
                DataStream &operator>>(AncFormat &val);
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
#if PROMEKI_ENABLE_TLS
                /**
                 * @brief Reads an SslContext::Ptr — yields a null Ptr.
                 *
                 * Pairs with the write overload above.  Sets @p val to
                 * a default-constructed (null) Ptr regardless of the
                 * stream contents.  See the write overload for why.
                 */
                DataStream &operator>>(SharedPtr<SslContext, false> &val);
#endif

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
                 * @brief Opens a new frame on the write side.
                 *
                 * Pushes a fresh body buffer onto an internal frame
                 * stack.  Every subsequent write — primitives, nested
                 * frames, raw bytes — accumulates in that buffer
                 * instead of going straight to the underlying device.
                 * The matching @ref endFrame() pops the buffer and
                 * flushes the assembled
                 * `[tag][version][size][body]` group to the parent
                 * frame (if any) or to the device.
                 *
                 * Frames may nest freely; for example, a List<UUID>
                 * write opens a TypeList frame whose body in turn
                 * contains a count plus N UUID frames.
                 *
                 * @param id      Tag identifying this frame's type.
                 * @param version Per-type wire-format version
                 *                emitted in the header.  Pass 1 for
                 *                the initial wire format; bump when
                 *                the type's body layout changes so
                 *                readers can dispatch on it.
                 */
                void beginFrame(TypeId id, uint16_t version);

                /**
                 * @brief Closes the most recently opened frame.
                 *
                 * Pops the body buffer pushed by the matching
                 * @ref beginFrame() call, emits the
                 * `[tag][version][size]` header followed by the body
                 * bytes, and routes them either into the parent
                 * frame's body buffer (when frames are nested) or to
                 * the underlying device.  If the stream is already in
                 * an error state this is a no-op.  If @ref endFrame()
                 * is called with no matching @ref beginFrame() open,
                 * the stream status is set to @c WriteFailed with a
                 * descriptive context message.
                 */
                void endFrame();

                /**
                 * @brief Reads a frame header and validates the tag.
                 *
                 * Consumes the 8-byte
                 * `[tag(uint16)][version(uint16)][size(uint32)]`
                 * header from the stream.  If @p expected doesn't
                 * match the tag in the wire, sets status to
                 * ReadCorruptData and returns @c false; the size
                 * bytes are NOT skipped, so the stream is positioned
                 * at the start of the unexpected body and further
                 * reads will fail in turn.
                 *
                 * If the tag matches but @p version exceeds
                 * @p maxVersion, sets status to ReadCorruptData and
                 * returns @c false.  This lets readers reject newer
                 * encodings of a type they know without having to
                 * write the version check by hand.
                 *
                 * @param expected   Required tag.
                 * @param maxVersion Highest version this reader
                 *                   understands.  Defaults to 1 —
                 *                   built-ins all live at version 1
                 *                   today.
                 * @param outVersion Optional out-parameter receiving
                 *                   the actual version from the
                 *                   frame, useful for branching when
                 *                   the reader supports multiple
                 *                   versions.
                 * @param outSize    Optional out-parameter receiving
                 *                   the declared body size; useful
                 *                   for sanity-checking how many
                 *                   bytes the body reader consumed.
                 * @return @c true if the tag matched and the version
                 *         was acceptable.
                 */
                bool readFrame(TypeId expected, uint16_t maxVersion = 1, uint16_t *outVersion = nullptr,
                               uint32_t *outSize = nullptr);

                /**
                 * @brief Reads a frame header without validating its tag.
                 *
                 * Useful when a reader has to accept one of several
                 * tags (e.g. Variant dispatch, Buffer accepting both
                 * TypeBuffer and TypeInvalid).  The header is fully
                 * consumed; on success the stream is positioned at
                 * the first body byte.
                 *
                 * @param outTag     Receives the tag found in the
                 *                   header.
                 * @param outVersion Receives the per-type version.
                 * @param outSize    Receives the body size.
                 * @return @c true if all 8 header bytes were read.
                 *         On failure (truncation) status is set to
                 *         ReadPastEnd and @c false is returned.
                 */
                bool readFrameHeader(TypeId &outTag, uint16_t &outVersion, uint32_t &outSize);

                /**
                 * @brief Reads a frame header but leaves it cached for the next read.
                 *
                 * Pulls the 8 header bytes from the device (parsing
                 * them according to the current byte order) and
                 * caches the parsed @c tag / @c version / @c size so
                 * that the next call to @ref readFrameHeader (or any
                 * @ref readFrame / @ref skipFrame, which both go
                 * through @c readFrameHeader internally) returns the
                 * same values without touching the device a second
                 * time.
                 *
                 * The cache holds at most one frame header.  Calling
                 * @c peekFrameHeader twice in a row returns the same
                 * cached header without re-reading.  Consuming the
                 * cache happens implicitly via the next
                 * @c readFrameHeader call — there is no separate
                 * "discard" entry point.
                 *
                 * Used by @c operator>>(Variant&) so that the
                 * dispatch code can look at the tag to pick a
                 * registered reader, then hand off to the per-type
                 * @c operator>> overload which re-reads the header
                 * through @c readFrame and consumes the cache
                 * transparently.  This keeps the read path working
                 * on sequential devices (sockets, pipes) where
                 * seeking back is not possible.
                 *
                 * @par Constraint
                 * Calling raw byte methods (@c readBytes,
                 * @c readRawData, @c skipRawData) while a peeked
                 * header is pending consumes bytes from the device
                 * past the header without draining the cache, which
                 * would mis-align subsequent frame reads.  In debug
                 * builds those entry points assert that the cache is
                 * empty.
                 *
                 * @param outTag     Receives the tag.
                 * @param outVersion Receives the version.
                 * @param outSize    Receives the body size.
                 * @return @c true on success.
                 */
                bool peekFrameHeader(TypeId &outTag, uint16_t &outVersion, uint32_t &outSize);

                /**
                 * @brief Consumes a complete frame, discarding its body.
                 *
                 * Reads the frame header via @ref readFrameHeader()
                 * and advances past the declared body size.  Provided
                 * primarily for forward compatibility: when a reader
                 * encounters a tag it doesn't understand it can call
                 * @ref skipFrame() to step past the value cleanly,
                 * leaving the stream positioned at the next frame.
                 */
                void skipFrame();

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
                 * @brief Writes raw bytes — no tag, no frame header.
                 *
                 * Outside of any frame, the bytes pass straight
                 * through to the underlying device.  When a frame
                 * is open, the bytes are appended to that frame's
                 * body so they're covered by the frame's size
                 * field; this lets a typed @c operator<< overload
                 * stream out a precomputed body without going
                 * through the framed-value @c operator<< machinery.
                 * @param buf Source buffer.
                 * @param len Number of bytes to write.
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
                 * @brief Reads exactly len bytes, setting status on failure.
                 * @param buf  Destination buffer.
                 * @param len  Number of bytes to read.
                 * @return True if all bytes were read successfully.
                 */
                bool readBytes(void *buf, size_t len);

                /**
                 * @brief Advances past @p sz body bytes, validating
                 *        the device has them available.
                 *
                 * Shared by @ref skipFrame and the Variant
                 * unknown-tag forward-compat path.  On a seekable
                 * device that knows its size, the remaining-bytes
                 * check catches truncation before skipRawData's
                 * seek silently positions past the content end.
                 * Sequential devices fall back to skipRawData's
                 * read-and-discard behaviour, which already detects
                 * short reads.
                 *
                 * @param sz  Number of body bytes to consume.
                 * @return @c true when the body was consumed; @c
                 *         false when truncation was detected (and
                 *         status was set to @ref ReadPastEnd).
                 */
                bool skipFrameBody(uint32_t sz);

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
                void writeAncFormatData(const AncFormat &val);
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
                AncFormat      readAncFormatData();
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

                /**
                 * @brief One entry on the in-flight frame stack.
                 *
                 * Captures the bytes accumulated between a
                 * @ref beginFrame() and its matching
                 * @ref endFrame().  At endFrame, the parent target
                 * (either the frame underneath this one, or the
                 * device when this was the outermost frame) receives
                 * the assembled `[tag][version][size][body]` group.
                 */
                struct PendingFrame {
                        TypeId        tag;     ///< @brief Tag this frame will emit.
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
                // inspect the tag without requiring a seekable
                // device.
                bool                 _peekedHeaderValid = false;
                TypeId               _peekedTag         = static_cast<TypeId>(0);
                uint16_t             _peekedVersion     = 0;
                uint32_t             _peekedSize        = 0;
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
        stream.beginFrame(DataStream::TypeSize2D, 1);
        stream << sz.width() << sz.height();
        stream.endFrame();
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
        if (!stream.readFrame(DataStream::TypeSize2D)) {
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
        stream.beginFrame(DataStream::TypeRational, 1);
        stream << r.numerator() << r.denominator();
        stream.endFrame();
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
        if (!stream.readFrame(DataStream::TypeRational)) {
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
        stream.beginFrame(DataStream::TypeRect, 1);
        stream << rect.x() << rect.y() << rect.width() << rect.height();
        stream.endFrame();
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
        if (!stream.readFrame(DataStream::TypeRect)) {
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
        stream.beginFrame(DataStream::TypePoint, 1);
        stream << static_cast<uint32_t>(N);
        const Array<T, N> &arr = point;
        for (size_t i = 0; i < N; ++i) stream << arr[i];
        stream.endFrame();
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
        if (!stream.readFrame(DataStream::TypePoint)) {
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
        stream.beginFrame(DataStream::TypeXYZColor, 1);
        const XYZColor::DataType &arr = col.data();
        stream << arr[0] << arr[1] << arr[2];
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads an XYZColor from tag + three tagged doubles.
 * @param stream The stream to read from.
 * @param col    The XYZColor to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, XYZColor &col) {
        if (!stream.readFrame(DataStream::TypeXYZColor)) {
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
        stream.beginFrame(DataStream::TypeMasteringDisplay, 1);
        stream << md.red().x() << md.red().y() << md.green().x() << md.green().y() << md.blue().x() << md.blue().y()
               << md.whitePoint().x() << md.whitePoint().y() << md.minLuminance() << md.maxLuminance();
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a MasteringDisplay, validating the tag and ten tagged doubles.
 * @param stream The stream to read from.
 * @param md     The MasteringDisplay to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, MasteringDisplay &md) {
        if (!stream.readFrame(DataStream::TypeMasteringDisplay)) {
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
        stream.beginFrame(DataStream::TypeContentLightLevel, 1);
        stream << cll.maxCLL() << cll.maxFALL();
        stream.endFrame();
        return stream;
}

/**
 * @brief Reads a ContentLightLevel, validating the tag and two tagged uint32 values.
 * @param stream The stream to read from.
 * @param cll    The ContentLightLevel to populate.
 * @return The stream, for chaining.
 */
inline DataStream &operator>>(DataStream &stream, ContentLightLevel &cll) {
        if (!stream.readFrame(DataStream::TypeContentLightLevel)) {
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
        stream.beginFrame(DataStream::TypeList, 1);
        stream << static_cast<uint32_t>(list.size());
        for (const auto &item : list) stream << item;
        stream.endFrame();
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
        if (!stream.readFrame(DataStream::TypeList)) return stream;
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
        stream.beginFrame(DataStream::TypeMap, 1);
        stream << static_cast<uint32_t>(map.size());
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
                stream << it->first << it->second;
        }
        stream.endFrame();
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
        if (!stream.readFrame(DataStream::TypeMap)) return stream;
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
        stream.beginFrame(DataStream::TypeSet, 1);
        stream << static_cast<uint32_t>(set.size());
        for (const auto &item : set) stream << item;
        stream.endFrame();
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
        if (!stream.readFrame(DataStream::TypeSet)) return stream;
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
        stream.beginFrame(DataStream::TypeHashMap, 1);
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
        if (!stream.readFrame(DataStream::TypeHashMap)) return stream;
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
        stream.beginFrame(DataStream::TypeHashSet, 1);
        stream << static_cast<uint32_t>(set.size());
        for (const auto &item : set) stream << item;
        stream.endFrame();
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
        if (!stream.readFrame(DataStream::TypeHashSet)) return stream;
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
