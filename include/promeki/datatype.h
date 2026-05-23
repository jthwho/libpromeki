/**
 * @file      datatype.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class JsonObject;

/**
 * @brief Stable 16-bit identifier shared by DataStream wire frames and Variant runtime tags.
 *
 * The 16-bit space is partitioned:
 *  - @c 0x0000               — out-of-band "no type" sentinel.
 *  - @c 0x0001 – @c 0x3FFF   — reserved for library built-in types.
 *  - @c 0x4000 – @c 0xFFFF   — open for user / application extension types.
 *
 * Every type that participates in Variant or DataStream pins one value
 * from this enum.  The integer travels both ways: it is the tag in a
 * DataStream frame header @em and the runtime tag a Variant uses to
 * identify its payload.  There is no separate @c DataStream::TypeId or
 * @c Variant::Type — they were aliases that have been collapsed into
 * this single canonical enum.
 *
 * User types register through @ref PROMEKI_DATATYPE.  Library built-ins
 * use the explicit values below so the wire format is stable across
 * builds.
 */
enum DataTypeID : uint16_t {
        DataTypeInvalid = 0x00, ///< @brief Out-of-band sentinel meaning "no type".

        // Primitives ---------------------------------------------
        DataTypeInt8       = 0x01, ///< @brief int8_t
        DataTypeUInt8      = 0x02, ///< @brief uint8_t
        DataTypeInt16      = 0x03, ///< @brief int16_t
        DataTypeUInt16     = 0x04, ///< @brief uint16_t
        DataTypeInt32      = 0x05, ///< @brief int32_t
        DataTypeUInt32     = 0x06, ///< @brief uint32_t
        DataTypeInt64      = 0x07, ///< @brief int64_t
        DataTypeUInt64     = 0x08, ///< @brief uint64_t
        DataTypeFloat      = 0x09, ///< @brief float (IEEE 754)
        DataTypeDouble     = 0x0A, ///< @brief double (IEEE 754)
        DataTypeBool       = 0x0B, ///< @brief bool (as uint8_t)
        DataTypeString     = 0x0C, ///< @brief Length-prefixed UTF-8 String
        DataTypeBuffer     = 0x0D, ///< @brief Length-prefixed raw bytes
        DataTypeNoValue    = 0x0E, ///< @brief Explicit "value not set" marker (empty payload)

        // Data objects -------------------------------------------
        DataTypeUUID            = 0x10,
        DataTypeDateTime        = 0x11,
        DataTypeTimeStamp       = 0x12,
        DataTypeSize2D          = 0x13,
        DataTypeRational        = 0x14,
        DataTypeFrameRate       = 0x15,
        DataTypeTimecode        = 0x16,
        DataTypeColor           = 0x17,
        DataTypeColorModel      = 0x18,
        DataTypeMemSpace        = 0x19,
        DataTypePixelMemLayout  = 0x1A,
        DataTypePixelFormat     = 0x1B,
        DataTypeEnum            = 0x1C,
        DataTypeStringList      = 0x1D,
        DataTypeRect            = 0x1E,
        DataTypePoint           = 0x1F,

        // Containers --------------------------------------------
        DataTypeList    = 0x20,
        DataTypeMap     = 0x21,
        DataTypeSet     = 0x22,
        DataTypeHashMap = 0x23,
        DataTypeHashSet = 0x24,

        // Shareable types ---------------------------------------
        DataTypeJsonObject = 0x30,
        DataTypeJsonArray  = 0x31,
        DataTypeXYZColor   = 0x32,
        DataTypeAudioDesc  = 0x33,
        DataTypeImageDesc  = 0x34,
        DataTypeMediaDesc  = 0x35,
        DataTypeUMID       = 0x36,
        DataTypeEnumList   = 0x37,
        DataTypeMediaTimeStamp     = 0x38,
        DataTypeMacAddress         = 0x39,
        DataTypeEUI64              = 0x3A,
        DataTypeMediaPipelineStage  = 0x3B,
        DataTypeMediaPipelineRoute  = 0x3C,
        DataTypeMediaPipelineConfig = 0x3D,
        DataTypeMediaPipelineStats  = 0x3E,
        DataTypeVideoFormat = 0x3F,

        // HDR color metadata ------------------------------------
        DataTypeMasteringDisplay  = 0x40,
        DataTypeContentLightLevel = 0x41,

        // MediaIO introspection ---------------------------------
        DataTypeMediaIODescription = 0x42,

        // Frame timeline types ----------------------------------
        DataTypeFrameNumber    = 0x43,
        DataTypeFrameCount     = 0x44,
        DataTypeMediaDuration  = 0x45,
        DataTypeUrl            = 0x46,
        DataTypeAudioFormat    = 0x47,
        DataTypeMediaPayload   = 0x48,
        DataTypeDuration       = 0x49,
        DataTypeSocketAddress  = 0x4A,
        DataTypeSdpSession     = 0x4B,
        DataTypeVideoCodec     = 0x4C,
        DataTypeAudioCodec     = 0x4D,
        DataTypeAudioChannelMap = 0x4E,
        DataTypeAudioStreamDesc = 0x4F,
        DataTypeWindowedStat        = 0x50,
        DataTypeWindowedStatsBundle = 0x51,
        DataTypeAudioMarkerList     = 0x52,
        DataTypeVariantList     = 0x53,
        DataTypeVariantMap      = 0x54,
        DataTypeXmlDocument     = 0x55,
        DataTypeXmlElement      = 0x56,
        DataTypeSslContext      = 0x57,
        DataTypeAncFormat       = 0x58,
        DataTypeAncPacket       = 0x59,
        DataTypeAncDesc         = 0x5A,
        DataTypeCea708Cdp       = 0x5B,
        DataTypeSubtitle        = 0x5C,
        DataTypeSubtitleSpan    = 0x5D,
        DataTypeScc             = 0x5E,
        DataTypeCea608          = 0x5F,
        DataTypeCea708Service       = 0x60,
        DataTypeCea708DtvccPacket   = 0x61,
        DataTypeHdrStaticMetadata   = 0x62,
        DataTypeHdrDynamic2094_40   = 0x63,

        // Generic video signal carriers --------------------------
        DataTypeVideoPortRef        = 0x64,
        DataTypeSdiSignalConfig     = 0x65,
        DataTypeHdmiSignalConfig    = 0x66,
        DataTypeVideoReferenceConfig = 0x67,
        DataTypeSdiOutputFanoutConfig = 0x68,
        DataTypeSdiVpid               = 0x69,
        DataTypeAncAtc                = 0x6A,
        DataTypeAncAfd                = 0x6B,
        DataTypeAncOp47Sdp            = 0x6C,
        DataTypeSt2020Audio           = 0x6D,
        DataTypeTimecodeUserbits      = 0x6E,
        DataTypeIpv4Address           = 0x6F,
        DataTypeIpv6Address           = 0x70,
        DataTypeNetworkAddress        = 0x71,
        DataTypePixelAspect           = 0x72,
};

/** @brief First @ref DataTypeID value available for user-defined types. */
inline constexpr DataTypeID DataTypeUserBegin = static_cast<DataTypeID>(0x4000);
/** @brief Largest legal user @ref DataTypeID (inclusive). */
inline constexpr DataTypeID DataTypeUserEnd = static_cast<DataTypeID>(0xFFFF);

/**
 * @brief Registers every library-provided built-in DataType record exactly once.
 *
 * Guarded by a process-lifetime static @c bool so subsequent calls are
 * no-ops.  The @ref Variant and @ref DataStream constructors call this
 * function so the registry is populated before any caller can use
 * either type.  @ref DataType::of<T> also calls it as a safety net for
 * lookups that happen before the first @ref Variant / @ref DataStream
 * is constructed.
 *
 * User-defined types are @em not registered here — callers must invoke
 * @ref registerDataType<T> for each of their own types explicitly,
 * Qt-style (@c qRegisterMetaType).
 */
void registerBuiltinDataTypes();

/**
 * @brief Runtime handle for one C++ type registered with the system.
 * @ingroup util
 *
 * @c DataType is a lightweight value-type handle wrapping a pointer to
 * an immutable @c Data record.  Each registered C++ type owns exactly
 * one record, which carries:
 *
 * - the type's @ref DataTypeID,
 * - a human-readable name (e.g. @c "UUID", @c "Timecode", @c "MyType"),
 * - the wire-format version the type currently emits,
 * - size / alignment information so callers can construct
 *   trailing-storage payloads without knowing the type statically,
 * - a @c std::type_index for the originating C++ type,
 * - an @ref Ops table of function pointers (copy / move / destroy /
 *   equal / toString / fromString / writeStream / readStream) populated
 *   automatically by @ref PROMEKI_DATATYPE.
 *
 * @par Registration
 * Use @ref PROMEKI_DATATYPE inside a class body to register the type
 * and tie it to its serialization member API.
 *
 * @par Thread Safety
 * Distinct DataType instances may be used concurrently.  The static
 * registry is internally synchronized and safe to call from any thread.
 *
 * @par Example
 * @code
 * class MyType {
 *     public:
 *         PROMEKI_DATATYPE(MyType, 0xFFE0, 1)
 *
 *         String toString() const;
 *         static Result<MyType> fromString(const String &s);
 *
 *         Error writeToStream(DataStream &s) const;
 *         template <uint32_t V>
 *         static Result<MyType> readFromStream(DataStream &s);
 * };
 *
 * // Anywhere — query.
 * DataType dt = DataType::of<MyType>();
 * promekiInfo("registered %s id=0x%04x ver=%u", dt.name(),
 *             static_cast<unsigned>(dt.id()), dt.version());
 * @endcode
 *
 * @see DataTypeID for the wire-format tag namespace.
 * @see PROMEKI_DATATYPE for the registration macro.
 */
class DataType {
        public:
                /**
                 * @brief Per-type function-pointer table populated at registration.
                 *
                 * Slots without a matching well-known operation are left
                 * @c nullptr; consumers must check before calling.
                 *
                 * @par Slot semantics
                 * - @c defaultConstruct(p) — placement-new a default T at @c p.
                 * - @c copyConstruct(dst,src) — placement-new copy of @c *src.
                 * - @c moveConstruct(dst,src) — placement-new move-from @c *src.
                 * - @c destroy(p) — invoke @c ~T() on the object at @c p.
                 * - @c equal(a,b) — true when @c *a == @c *b.
                 * - @c toString(p,err) — produce a String of @c *p.
                 * - @c fromString(s,out,err) — parse @c s into @c *out.
                 * - @c toInt(p,err) — extract a 64-bit signed integer
                 *   representation of @c *p (TypeRegistry @c id(),
                 *   @c FrameNumber / @c FrameCount @c value(),
                 *   @c Enum @c value()).
                 * - @c fromInt(v,out,err) — construct @c *out from a
                 *   64-bit integer (TypeRegistry @c T(T::ID),
                 *   @c FrameNumber / @c FrameCount @c T(int64_t)).
                 *   Slot stays null for types like @c Enum whose
                 *   construction needs additional context.
                 * - @c toFloat(p,err) — extract a @c double
                 *   representation of @c *p (FrameRate / Rational
                 *   @c toDouble(), FrameNumber / FrameCount
                 *   @c toDouble() with NaN / Inf sentinels for the
                 *   unknown / infinite cases).
                 * - @c fromFloat(v,out,err) — construct @c *out from
                 *   a @c double (FrameRate / Rational rationalize
                 *   to a fraction, FrameNumber / FrameCount honour
                 *   NaN / Inf sentinels).  Slot stays null for types
                 *   that have no meaningful numeric form.
                 * - @c writeStream(s,p) — full framed write of @c *p (begin/endFrame inclusive).
                 * - @c readStream(s,p) — full framed read into @c *p (consumes the frame header internally).
                 */
                struct Ops {
                        void       (*defaultConstruct)(void *p)                                = nullptr;
                        void       (*copyConstruct)(void *dst, const void *src)                = nullptr;
                        void       (*moveConstruct)(void *dst, void *src)                      = nullptr;
                        void       (*destroy)(void *p)                                         = nullptr;
                        bool       (*equal)(const void *a, const void *b)                      = nullptr;
                        String     (*toString)(const void *p, Error *err)                      = nullptr;
                        bool       (*fromString)(const String &s, void *out, Error *err)       = nullptr;
                        int64_t    (*toInt)(const void *p, Error *err)                         = nullptr;
                        bool       (*fromInt)(int64_t v, void *out, Error *err)                = nullptr;
                        double     (*toFloat)(const void *p, Error *err)                       = nullptr;
                        bool       (*fromFloat)(double v, void *out, Error *err)               = nullptr;
                        JsonObject (*toJson)(const void *p, Error *err)                        = nullptr;
                        bool       (*fromJson)(const JsonObject &j, void *out, Error *err)     = nullptr;
                        void       (*writeStream)(DataStream &s, const void *p)                = nullptr;
                        void       (*readStream)(DataStream &s, void *p)                       = nullptr;
                };

                /**
                 * @brief Immutable descriptor for one registered type.
                 *
                 * Lives in a static registry indexed by @c id (and
                 * cross-indexed by name and @c cppType).  Populated at
                 * registration and never mutated thereafter.
                 */
                struct Data {
                        DataTypeID      id      = DataTypeInvalid;
                        const char     *name    = "Invalid";
                        uint32_t        version = 0;
                        size_t          size    = 0;
                        size_t          align   = 1;
                        std::type_index cppType = std::type_index(typeid(void));
                        Ops             ops;
                };

                /** @brief Default-constructs an invalid handle (@c isValid() returns false). */
                DataType() = default;

                /** @brief Wraps an existing immutable record; primarily for the registry to construct handles. */
                explicit DataType(const Data *d) : _data(d) { return; }

                /**
                 * @brief Looks up the registered type with @p id.
                 *
                 * Implicit so existing comparison code that uses raw
                 * @ref DataTypeID values continues to work once
                 * call-site parameters are migrated from raw ID to
                 * @c const @c DataType @c &.
                 *
                 * @param id  The wire-format tag to resolve.
                 */
                DataType(DataTypeID id);

                /** @brief Returns true when this handle refers to a registered type. */
                bool isValid() const { return _data != nullptr; }

                /** @brief Returns the type's stable @ref DataTypeID, or @c DataTypeInvalid for an invalid handle. */
                DataTypeID id() const { return _data != nullptr ? _data->id : DataTypeInvalid; }

                /** @brief Returns the registered name, or @c "Invalid" for an invalid handle. */
                const char *name() const { return _data != nullptr ? _data->name : "Invalid"; }

                /** @brief Returns the current wire-format version for the type, or @c 0 for an invalid handle. */
                uint32_t version() const { return _data != nullptr ? _data->version : 0; }

                /** @brief Returns @c sizeof(T) for the underlying C++ type, or @c 0 for an invalid handle. */
                size_t size() const { return _data != nullptr ? _data->size : 0; }

                /** @brief Returns @c alignof(T) for the underlying C++ type, or @c 1 for an invalid handle. */
                size_t alignment() const { return _data != nullptr ? _data->align : 1; }

                /** @brief Returns the type_index for the underlying C++ type (typeid(void) when invalid). */
                std::type_index cppType() const {
                        return _data != nullptr ? _data->cppType : std::type_index(typeid(void));
                }

                /**
                 * @brief Returns the function-pointer table.
                 *
                 * Aborts when called on an invalid handle.  Check
                 * @ref isValid first when you don't statically know
                 * the handle is good.
                 */
                const Ops &ops() const;

                /** @brief Returns the underlying immutable record, or @c nullptr for an invalid handle. */
                const Data *data() const { return _data; }

                /** @brief Two handles are equal iff they refer to the same registered record. */
                bool operator==(const DataType &o) const { return _data == o._data; }
                bool operator!=(const DataType &o) const { return _data != o._data; }

                /**
                 * @brief Low-level registration entry point.
                 *
                 * Callers normally go through @ref registerDataType<T>,
                 * which derives @p id / @p name / @p version from
                 * @ref PROMEKI_DATATYPE (or accepts them explicitly
                 * for primitives) and builds the @c Ops table via
                 * @ref Detail::makeDefaultOps.  This is the underlying
                 * thunk both overloads call.
                 *
                 * @param id          Pinned @ref DataTypeID for the type
                 *                    (must be unique and not
                 *                    @c DataTypeInvalid).
                 * @param name        Human-readable type name; pointer-stored.
                 * @param version     Current wire-format version.
                 * @param ti          C++ type_index for the type.
                 * @param size        @c sizeof(T).
                 * @param align       @c alignof(T).
                 * @param ops         Populated function-pointer table.
                 * @return            A handle to the registered record,
                 *                    or an invalid handle on rejection.
                 */
                static DataType registerType(DataTypeID id, const char *name, uint32_t version,
                                             std::type_index ti, size_t size, size_t align, Ops ops);

                /** @brief Returns the IDs of every registered type, in ascending order. */
                static List<DataTypeID> registeredIds();

                /** @brief Looks up the registered type with @p id; returns an invalid handle when absent. */
                static DataType byId(DataTypeID id);

                /** @brief Looks up by name; returns an invalid handle when absent.  Case-sensitive. */
                static DataType byName(const char *name);

                /**
                 * @brief Looks up the DataType for the given C++ type.
                 *
                 * If @p T provides a nested @c promekiDataType struct
                 * (i.e. was annotated with @ref PROMEKI_DATATYPE), the
                 * type is registered lazily on first call so callers
                 * don't have to wire up any explicit init.  Explicit
                 * registration via @ref registerDataType is still the
                 * recommended path — the lazy fallback exists so that
                 * legacy callsites and tests continue to work without
                 * change.
                 *
                 * Always triggers @ref registerBuiltinDataTypes first so
                 * library built-ins are visible.
                 *
                 * @tparam T  The C++ type whose DataType should be returned.
                 */
                template <typename T> static DataType of();

                /** @brief Looks up by C++ type_index; returns invalid handle when absent. */
                static DataType byCppType(std::type_index ti);

        private:
                const Data *_data = nullptr;
};

/** @brief Concept detectors and template helpers used by @ref DataType registration. */
namespace Detail {

template <typename T>
concept HasEqualityOp = requires(const T &a, const T &b) {
        { a == b } -> std::convertible_to<bool>;
};

template <typename T>
concept HasMemberToString = requires(const T &t) {
        { t.toString() } -> std::convertible_to<String>;
};

/**
 * @brief Detects @c static Result<T> T::fromString(const String &).
 *
 * Standard parse-method shape across the codebase.  Populates the
 * @c fromString slot in the type's @ref Ops table.
 */
template <typename T>
concept HasResultFromString = requires(const String &s) {
        { T::fromString(s) } -> std::convertible_to<Result<T>>;
};

/**
 * @brief Detects @c T::value() returning an integral type
 *        (FrameNumber, FrameCount, Enum, ...).
 *
 * Populates the @c toInt slot in the type's @ref Ops table.
 */
template <typename T>
concept HasMemberValueInt = requires(const T &t) {
        { t.value() } -> std::integral;
};

/**
 * @brief Detects @c T::id() returning an integral or enum type
 *        (TypeRegistry-shaped wrappers: ColorModel, PixelFormat, ...).
 *
 * Populates the @c toInt slot when @ref HasMemberValueInt does not.
 */
template <typename T>
concept HasMemberIdInt = (!HasMemberValueInt<T>) && requires(const T &t) {
        requires std::integral<decltype(t.id())> || std::is_enum_v<decltype(t.id())>;
};

/**
 * @brief Detects @c double T::toDouble() const.
 *
 * Populates the @c toFloat slot in the type's @ref Ops table.  The
 * library's @c FrameRate and @c Rational types already use this
 * shape; @c FrameNumber and @c FrameCount expose it with NaN / Inf
 * sentinels for the unknown / infinite cases.
 */
template <typename T>
concept HasMemberToDouble = requires(const T &t) {
        { t.toDouble() } -> std::convertible_to<double>;
};

/**
 * @brief Detects @c static Result<T> T::fromDouble(double).
 *
 * Populates the @c fromFloat slot.  Types either rationalize the
 * @c double (FrameRate, Rational) or fold the IEEE sentinels back
 * into their domain (FrameNumber, FrameCount).
 */
template <typename T>
concept HasResultFromDouble = requires(double d) {
        { T::fromDouble(d) } -> std::convertible_to<Result<T>>;
};

/**
 * @brief Detects @c JsonObject T::toJson() const.
 *
 * Populates the @c toJson slot in the type's @ref Ops table.  Used
 * by the HDR, CEA-608/708 and Subtitle family today; a forward
 * investment for any type that grows a JSON path later.  The check
 * is just the call expression's validity — @c JsonObject does not
 * need to be complete here, only at the @ref makeDefaultOps
 * instantiation site (where @c json.h is in scope).
 */
template <typename T>
concept HasMemberToJson = requires(const T &t) {
        t.toJson();
};

/**
 * @brief Detects @c static Result<T> T::fromJson(const JsonObject &).
 *
 * Standard parse-method shape the registry recognises.  Populates
 * the @c fromJson slot.  Types whose existing @c fromJson uses the
 * @c (JsonObject, Error *) shape need a thin @ref Result wrapper.
 * @c JsonObject only needs to be complete at the instantiation site.
 */
template <typename T>
concept HasResultFromJson = requires(JsonObject *jp) {
        T::fromJson(*jp);
};

/**
 * @brief Detects a TypeRegistry @c T(T::ID) constructor.
 *
 * Used to populate the @c fromInt slot for ColorModel, MemSpace,
 * PixelFormat, VideoCodec, AudioCodec, AudioFormat, AudioStreamDesc,
 * PixelMemLayout, AncFormat.
 */
template <typename T>
concept HasIdCtor = requires { typename T::ID; } && std::is_constructible_v<T, typename T::ID>;

/**
 * @brief Detects a single-argument @c T(int64_t) constructor.
 *
 * Used to populate the @c fromInt slot for @ref FrameNumber and
 * @ref FrameCount.  Excludes @ref HasIdCtor matches so the
 * TypeRegistry path wins for wrappers that have both shapes.
 */
template <typename T>
concept HasInt64Ctor = std::is_constructible_v<T, int64_t> && !HasIdCtor<T>;

/**
 * @brief Detects the nested @c promekiDataType trait struct emitted
 *        by the @ref PROMEKI_DATATYPE macro.
 */
template <typename T>
concept HasPromekiDataType = requires {
        { T::promekiDataType::id } -> std::convertible_to<DataTypeID>;
        { T::promekiDataType::name } -> std::convertible_to<const char *>;
        { T::promekiDataType::version } -> std::convertible_to<uint32_t>;
};

// HasMemberWriteToStream / HasMemberReadFromStream live in
// @c datastream.h's bottom half — they reference @c DataStream which
// must be complete at concept instantiation time, and putting them
// here would force every consumer of @c datatype.h to also pull in
// @c datastream.h.

/**
 * @brief Trait specializable to mark a type as serializable through a
 *        free-function @c operator<<(DataStream &, const T &).
 *
 * Specialized in @c datastream.h for the geometry template kinds
 * (@ref Size2DTemplate, @ref Rational) whose serialization lives in
 * a free-function template rather than a member API.  Concept-based
 * detection cannot match free operators safely because the
 * @c Variant converting constructor masks the substitution failure,
 * so the explicit specialization tells @ref makeDefaultOps that a
 * free operator exists.  All other library types use the
 * @ref PROMEKI_DATATYPE member-API path and need no specialization
 * here.
 */
template <typename T> struct HasFreeDataStreamWrite : std::false_type {};

/** @brief Read-direction counterpart to @ref HasFreeDataStreamWrite. */
template <typename T> struct HasFreeDataStreamRead : std::false_type {};

/**
 * @brief Recursive helper used by @ref PROMEKI_DATATYPE to dispatch
 *        a runtime version value to the matching @c readFromStream<V>.
 *
 * Walks downward from @p MaxV to 1, returning @c Error::CorruptData
 * when the wire version is outside that range.
 */
template <typename T, uint32_t MaxV, uint32_t CurV = MaxV>
struct VersionedReader {
        static Result<T> read(DataStream &s, uint32_t version) {
                if (version == CurV) return T::template readFromStream<CurV>(s);
                if constexpr (CurV > 1) return VersionedReader<T, MaxV, CurV - 1>::read(s, version);
                else return makeError<T>(Error::CorruptData);
        }
};

// Primary templates for the "T has an exact-match operator<< / >>
// member on DataStream" detectors.  Partial specializations live in
// @c datastream.h once the @ref DataStream class is complete — they
// dereference member function pointers on @ref DataStream and so
// need its full declaration.  Forward-declaring the primary here
// (without pulling in @c datastream.h) keeps the cycle broken.
template <typename T, typename = void>
struct ExactDataStreamWrite : std::false_type {};

template <typename T, typename = void>
struct ExactDataStreamRead : std::false_type {};

template <typename T>
inline constexpr bool ExactDataStreamWriteV = ExactDataStreamWrite<T>::value;

template <typename T>
inline constexpr bool ExactDataStreamReadV = ExactDataStreamRead<T>::value;

/** @brief True when @p T has any populated DataStream write operator. */
template <typename T>
inline constexpr bool HasDataStreamWriteV = ExactDataStreamWriteV<T> || HasFreeDataStreamWrite<T>::value;

/** @brief True when @p T has any populated DataStream read operator. */
template <typename T>
inline constexpr bool HasDataStreamReadV = ExactDataStreamReadV<T> || HasFreeDataStreamRead<T>::value;

/**
 * @brief Builds an Ops table for @p T, populating slots for whichever
 *        well-known operations @p T satisfies.
 */
template <typename T>
DataType::Ops makeDefaultOps();

/**
 * @brief Wires up the auto-discoverable @c String <-> @p T converters
 *        against the Variant registry, for any newly-registered type
 *        whose @ref DataType::Ops table populates @c toString /
 *        @c fromString.
 *
 * Defined in variant.h after @ref Variant is complete.  Invoked from
 * @ref registerDataType after the type's record is in the registry.
 * Hand-rolled bespoke converter pairs (numeric Cartesian, FrameRate
 * arithmetic, ...) are unaffected — those continue to live in
 * @ref Variant::registerBuiltinConverters.
 */
template <typename T> void registerAutoConverters(const DataType &dt);

} // namespace Detail

/**
 * @brief Registers @p T explicitly with the @ref DataType system.
 *
 * The Qt-style explicit-registration entry point: callers invoke this
 * once at program init (or before first use of @p T through
 * @ref Variant / @ref DataStream) so the runtime knows about the type
 * and any auto-discoverable converters get wired up against the
 * @ref Variant converter registry.
 *
 * The overload requires @p T to carry the @ref PROMEKI_DATATYPE
 * annotation — the macro's nested @c promekiDataType struct supplies
 * the pinned wire @c id, the human-readable @c name, and the current
 * wire @c version.  For primitives and template instantiations that
 * cannot host the macro, see the explicit
 * @ref registerDataType(DataTypeID,const char*,uint32_t) overload.
 *
 * Idempotent — second and subsequent calls for the same @p T return
 * the cached @ref DataType handle.
 */
template <typename T> DataType registerDataType();

/**
 * @brief Registers @p T with an explicit @p id, @p name, and @p version.
 *
 * The Qt-style entry point for types that cannot carry a nested
 * @c promekiDataType struct — primitives (@c int32_t, @c double, ...),
 * template instantiations (@c Rational<int>, @c Size2Du32, ...), and
 * library types whose wire operators live as free functions.  The
 * caller supplies the pinned wire @p id (must be unique) and the
 * type's human-readable @p name and current @p version.
 *
 * Idempotent — second and subsequent calls for the same @p T return
 * the cached @ref DataType handle.
 */
template <typename T> DataType registerDataType(DataTypeID id, const char *name, uint32_t version = 1);

/**
 * @brief Looks up the DataType for @p T at runtime.
 *
 * Lazy-registers @p T if it carries a @ref PROMEKI_DATATYPE trait
 * struct and was not previously registered — equivalent to calling
 * @ref registerDataType<T> on first use.  Triggers
 * @ref registerBuiltinDataTypes first.
 */
template <typename T> DataType DataType::of() {
        registerBuiltinDataTypes();
        if constexpr (Detail::HasPromekiDataType<T>) {
                static const DataType once = registerDataType<T>();
                return once;
        } else {
                return DataType::byCppType(std::type_index(typeid(T)));
        }
}

template <typename T> DataType registerDataType() {
        static_assert(Detail::HasPromekiDataType<T>,
                      "registerDataType<T>() with no arguments requires PROMEKI_DATATYPE on T; "
                      "use registerDataType<T>(id, name, version) for primitives and template "
                      "instantiations that cannot host the macro.");
        DataType existing = DataType::byCppType(std::type_index(typeid(T)));
        if (existing.isValid()) return existing;
        DataType dt = DataType::registerType(
                T::promekiDataType::id, T::promekiDataType::name, T::promekiDataType::version,
                std::type_index(typeid(T)), sizeof(T), alignof(T),
                Detail::makeDefaultOps<T>());
        if (dt.isValid()) Detail::registerAutoConverters<T>(dt);
        return dt;
}

template <typename T> DataType registerDataType(DataTypeID id, const char *name, uint32_t version) {
        DataType existing = DataType::byCppType(std::type_index(typeid(T)));
        if (existing.isValid()) return existing;
        DataType dt = DataType::registerType(
                id, name, version, std::type_index(typeid(T)), sizeof(T), alignof(T),
                Detail::makeDefaultOps<T>());
        if (dt.isValid()) Detail::registerAutoConverters<T>(dt);
        return dt;
}

PROMEKI_NAMESPACE_END

/**
 * @brief Declares that a class type participates in the DataType registry.
 *
 * Place inside the class body.  Generates a nested @c promekiDataType
 * struct exposing:
 *  - @c static @c constexpr @c DataTypeID @c id        — the pinned wire tag,
 *  - @c static @c constexpr @c const @c char @c *name  — the type's name,
 *  - @c static @c constexpr @c uint32_t @c version     — the current wire version,
 *  - @c static @c Result<Type> @c dispatchRead(DataStream &, uint32_t)
 *    — switches on the wire version and dispatches to the matching
 *    @c readFromStream<V> specialization.
 *
 * The class must additionally provide:
 *  - @c String toString() const                        (recommended),
 *  - @c static Result<Type> fromString(const String &) (recommended),
 *  - @c Error writeToStream(DataStream &) const,
 *  - @c template <uint32_t V> static Result<Type> readFromStream(DataStream &),
 *    with one explicit specialization per supported version from
 *    @c 1 through @c VERSION.
 *
 * Wire-format dispatch:
 * The framework's generic @c operator<<(DataStream &, const Type &)
 * emits a frame whose tag = @p ID and version = @p VERSION, then calls
 * @c writeToStream() to lay down the body.  The matching
 * @c operator>>(DataStream &, Type &) reads the frame header, looks up
 * the wire version, calls @c dispatchRead which forwards to the
 * matching @c readFromStream<V>.  Unknown versions yield
 * @c Error::CorruptData and reset the destination.
 *
 * @param TYPE     The C++ class type.
 * @param ID       The pinned @ref DataTypeID (a member of the enum for
 *                 library built-ins, a raw @c uint16_t for user types).
 * @param VERSION  Current wire-format version literal (>=1).
 */
#define PROMEKI_DATATYPE(TYPE, ID, VERSION)                                                                                     \
        struct promekiDataType {                                                                                                \
                using Self = TYPE;                                                                                              \
                static constexpr ::promeki::DataTypeID id = static_cast<::promeki::DataTypeID>(ID);                             \
                static constexpr const char           *name = #TYPE;                                                            \
                static constexpr uint32_t              version = (VERSION);                                                     \
                static ::promeki::Result<Self> dispatchRead(::promeki::DataStream &s, uint32_t v) {                             \
                        return ::promeki::Detail::VersionedReader<Self, (VERSION)>::read(s, v);                                 \
                }                                                                                                               \
        };

// Concept detection of the new member API needs DataStream complete,
// so it lives in @c datastream.h's bottom half (where it can rely on
// the class being fully declared without pulling @c datastream.h
// transitively from here — which would form an unmanageable cycle
// when @ref PROMEKI_DATATYPE is used inside types that get included
// from @c datastream.h itself).
//
// Users that need the generic @c operator<< / @c operator>>
// templates, or @c makeDefaultOps for explicit registration, include
// @c <promeki/datastream.h>.

#endif // PROMEKI_ENABLE_CORE
