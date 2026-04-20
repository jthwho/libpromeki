/**
 * @file      variant.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <variant>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <promeki/config.h>
#include <promeki/namespace.h>
#include <promeki/variant_fwd.h>
#include <promeki/util.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/mediatimestamp.h>
#include <promeki/datetime.h>
#include <promeki/size2d.h>
#include <promeki/uuid.h>
#include <promeki/umid.h>
#include <promeki/timecode.h>
#include <promeki/rational.h>
#include <promeki/framerate.h>
#include <promeki/videoformat.h>
#include <promeki/stringlist.h>
#include <promeki/color.h>
#include <promeki/list.h>
#include <promeki/audiocodec.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/pixelformat.h>
#include <promeki/pixeldesc.h>
#include <promeki/videocodec.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/enum.h>
#include <promeki/enumlist.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/socketaddress.h>
#include <promeki/sdpsession.h>
#include <promeki/macaddress.h>
#include <promeki/eui64.h>
#endif
#include <nlohmann/json.hpp>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief X-macro that defines all supported Variant types.
 * @ingroup util
 *
 * Each entry has the form `X(EnumName, CppType)`.  The macro is expanded in
 * several contexts inside VariantImpl:
 *  - To generate the Type enum (TypeInvalid, TypeBool, ..., TypeRational).
 *  - To generate the `typeName()` lookup table.
 *  - To instantiate the `std::variant` template parameter list.
 *
 * The order of entries determines the numeric value of each Type enumerator
 * and **must** match the order of template arguments passed to `std::variant`.
 *
 * | Enumerator    | C++ type            |
 * |---------------|---------------------|
 * | TypeInvalid   | `std::monostate`    |
 * | TypeBool      | `bool`              |
 * | TypeU8        | `uint8_t`           |
 * | TypeS8        | `int8_t`            |
 * | TypeU16       | `uint16_t`          |
 * | TypeS16       | `int16_t`           |
 * | TypeU32       | `uint32_t`          |
 * | TypeS32       | `int32_t`           |
 * | TypeU64       | `uint64_t`          |
 * | TypeS64       | `int64_t`           |
 * | TypeFloat     | `float`             |
 * | TypeDouble    | `double`            |
 * | TypeString    | `String`            |
 * | TypeDateTime  | `DateTime`          |
 * | TypeTimeStamp | `TimeStamp`         |
 * | TypeSize2D    | `Size2Du32`            |
 * | TypeUUID      | `UUID`              |
 * | TypeUMID      | `UMID`              |
 * | TypeTimecode  | `Timecode`          |
 * | TypeRational  | `Rational<int>`     |
 * | TypeFrameRate | `FrameRate`         |
 * | TypeVideoFormat | `VideoFormat`     |
 * | TypeStringList| `StringList`        |
 * | TypeColor     | `Color`             |
 * | TypeColorModel | `ColorModel`      |
 * | TypeMemSpace  | `MemSpace`          |
 * | TypePixelFormat | `PixelFormat`     |
 * | TypePixelDesc | `PixelDesc`         |
 * | TypeVideoCodec | `VideoCodec`       |
 * | TypeAudioCodec | `AudioCodec`       |
 * | TypeEnum      | `Enum`              |
 * | TypeEnumList  | `EnumList`          |
 *
 * When @c PROMEKI_ENABLE_NETWORK is true, the following types are also available:
 *
 * | Enumerator        | C++ type            |
 * |-------------------|---------------------|
 * | TypeSocketAddress | `SocketAddress`     |
 * | TypeSdpSession    | `SdpSession`        |
 * | TypeMacAddress    | `MacAddress`        |
 * | TypeEUI64         | `EUI64`             |
 */
#if PROMEKI_ENABLE_NETWORK
#       define PROMEKI_VARIANT_TYPES_NETWORK    \
                X(TypeSocketAddress, SocketAddress) \
                X(TypeSdpSession, SdpSession) \
                X(TypeMacAddress, MacAddress) \
                X(TypeEUI64, EUI64)
#else
#       define PROMEKI_VARIANT_TYPES_NETWORK
#endif

#define PROMEKI_VARIANT_TYPES           \
        X(TypeInvalid, std::monostate)  \
        X(TypeBool, bool)               \
        X(TypeU8, uint8_t)              \
        X(TypeS8, int8_t)               \
        X(TypeU16, uint16_t)            \
        X(TypeS16, int16_t)             \
        X(TypeU32, uint32_t)            \
        X(TypeS32, int32_t)             \
        X(TypeU64, uint64_t)            \
        X(TypeS64, int64_t)             \
        X(TypeFloat, float)             \
        X(TypeDouble, double)           \
        X(TypeString, String)           \
        X(TypeDateTime, DateTime)       \
        X(TypeTimeStamp, TimeStamp)     \
        X(TypeMediaTimeStamp, MediaTimeStamp) \
        X(TypeSize2D, Size2Du32)           \
        X(TypeUUID, UUID)               \
        X(TypeUMID, UMID)               \
        X(TypeTimecode, Timecode)       \
        X(TypeRational, Rational<int>)  \
        X(TypeFrameRate, FrameRate)     \
        X(TypeVideoFormat, VideoFormat) \
        X(TypeStringList, StringList)   \
        X(TypeColor, Color)             \
        X(TypeColorModel, ColorModel)   \
        X(TypeMemSpace, MemSpace)       \
        X(TypePixelFormat, PixelFormat) \
        X(TypePixelDesc, PixelDesc)     \
        X(TypeVideoCodec, VideoCodec)   \
        X(TypeAudioCodec, AudioCodec)   \
        X(TypeEnum, Enum)               \
        X(TypeEnumList, EnumList)       \
        X(TypeMasteringDisplay, MasteringDisplay) \
        X(TypeContentLightLevel, ContentLightLevel) \
        PROMEKI_VARIANT_TYPES_NETWORK

namespace detail {
        /** @brief Sentinel type used to absorb the trailing comma from X-macro expansion. */
        struct VariantEnd {
                bool operator==(const VariantEnd &) const { return true; }
                bool operator!=(const VariantEnd &) const { return false; }
        };

        /** @brief True for TypeRegistry wrapper types that have an integer ID. */
        template <typename T> struct is_type_registry : std::false_type {};
        template <> struct is_type_registry<AudioCodec>   : std::true_type {};
        template <> struct is_type_registry<ColorModel>   : std::true_type {};
        template <> struct is_type_registry<MemSpace>     : std::true_type {};
        template <> struct is_type_registry<PixelFormat>  : std::true_type {};
        template <> struct is_type_registry<PixelDesc>    : std::true_type {};
        template <> struct is_type_registry<VideoCodec>   : std::true_type {};
        template <typename T> inline constexpr bool is_type_registry_v = is_type_registry<T>::value;
}

/**
 * @brief Type-safe tagged union that can hold any of the types listed in PROMEKI_VARIANT_TYPES.
 *
 * VariantImpl is a thin wrapper around `std::variant` that adds a Type enum,
 * human-readable type names, and automatic type-conversion logic via the
 * templated get() method.  It is not intended to be used directly; instead use
 *
 * @par Example
 * @code
 * Variant v = 42;
 * String s = v.get<String>();  // "42"
 * v.set(String("hello"));
 * bool valid = v.isValid();    // true
 * Variant::Type t = v.type();  // Variant::TypeString
 * @endcode
 * the `Variant` type alias which is instantiated with the concrete type list.
 *
 * @tparam Types  The set of types the variant can hold (generated from PROMEKI_VARIANT_TYPES).
 */
template <typename... Types> class VariantImpl {
        public:
                #define X(name, type) name,
                /**
                 * @brief Enumerates every type the variant can hold.
                 *
                 * The enumerator order matches the template argument order so that
                 * `std::variant::index()` can be cast directly to a Type value.
                 */
                enum Type { PROMEKI_VARIANT_TYPES };
                #undef X

                #define X(name, type) PROMEKI_STRINGIFY(type),
                /**
                 * @brief Returns the human-readable C++ type name for the given Type enumerator.
                 * @param[in] id  The Type enumerator value.
                 * @return A null-terminated string such as `"bool"`, `"String"`, etc.
                 */
                static const char *typeName(Type id) {
                        static const char *items[] = { PROMEKI_VARIANT_TYPES };
                        PROMEKI_ASSERT(id < PROMEKI_ARRAY_SIZE(items));
                        return items[id];
                }
                #undef X

                /**
                 * @brief Constructs a VariantImpl from a JSON value, inferring the best native type.
                 *
                 * Mapping rules:
                 *  - `null`            -> invalid (default-constructed) variant
                 *  - boolean           -> `bool`
                 *  - unsigned integer  -> `uint64_t`
                 *  - signed integer    -> `int64_t`
                 *  - floating-point    -> `double`
                 *  - string            -> `String`
                 *  - array             -> `StringList` (elements converted to String)
                 *  - anything else     -> `String` (via `json::dump()`)
                 *
                 * @param val  The JSON value to convert.
                 * @return A VariantImpl holding the converted value.
                 */
                static VariantImpl fromJson(const nlohmann::json &val) {
                        if(val.is_null()) return VariantImpl();
                        if(val.is_boolean()) return val.get<bool>();
                        if(val.is_number_integer()) {
                                if(val.is_number_unsigned()) return val.get<uint64_t>();
                                return val.get<int64_t>();
                        }
                        if(val.is_number_float()) return val.get<double>();
                        if(val.is_string()) return String(val.get<std::string>());
                        if(val.is_array()) {
                                StringList list;
                                for(const auto &item : val) {
                                        if(item.is_string()) list.pushToBack(String(item.get<std::string>()));
                                        else list.pushToBack(String(item.dump()));
                                }
                                return list;
                        }
                        return String(val.dump());
                }

                /** @brief Default-constructs an invalid (empty) variant holding std::monostate. */
                VariantImpl() = default;

                /**
                 * @brief Constructs a variant holding a copy of @p value.
                 * @tparam T  The type of the value; must be one of the supported variant types.
                 * @param value  The value to store.
                 */
                template <typename T> VariantImpl(const T& value) : v(value) { }

                /** @brief Returns true if the variant holds a value other than std::monostate. */
                bool isValid() const {
                        return v.index() != 0;
                }

                /**
                 * @brief Replaces the currently held value with @p value.
                 * @tparam T  The type of the new value; must be one of the supported variant types.
                 * @param value  The value to store.
                 */
                template <typename T> void set(const T &value) { v = value; }

                /**
                 * @brief Converts the stored value to the requested type @p To.
                 *
                 * The method uses `std::visit` with a compile-time conversion matrix.
                 * If the stored type is already @p To, the value is returned directly.
                 * Otherwise an appropriate conversion is attempted (numeric casts,
                 * string parsing, `toString()` calls, etc.).  When no conversion path
                 * exists, a default-constructed @p To is returned and @p err (if
                 * non-null) is set to Error::Invalid.
                 *
                 * @tparam To  The desired result type.
                 * @param err  Optional pointer to an Error that receives Error::Ok on
                 *             success or Error::Invalid when the conversion is not
                 *             possible.
                 * @return The converted value, or a default-constructed @p To on failure.
                 */
                template <typename To> To get(Error *err = nullptr) const;

                /** @brief Returns the Type enumerator for the currently held value. */
                Type type() const {
                        return static_cast<Type>(v.index());
                }

                /** @brief Returns the human-readable type name of the currently held value. */
                const char *typeName() const {
                        return typeName(type());
                }

                /**
                 * @brief Converts complex types to their String representation, leaving simple types unchanged.
                 *
                 * Types such as String, DateTime, TimeStamp, Size2Du32, UUID, UMID, Timecode,
                 * Rational, and StringList are converted to String via `get<String>()`.
                 * All other types (numeric, bool, invalid) are returned as-is.
                 *
                 * @return A new VariantImpl containing either the original value or its
                 *         String representation.
                 */
                VariantImpl toStandardType() const {
                        switch(type()) {
                                case TypeString:
                                case TypeDateTime:
                                case TypeTimeStamp:
                                case TypeMediaTimeStamp:
                                case TypeSize2D:
                                case TypeUUID:
                                case TypeUMID:
                                case TypeTimecode:
                                case TypeRational:
                                case TypeFrameRate:
                                case TypeVideoFormat:
                                case TypeStringList:
                                case TypeColor:
                                case TypeColorModel:
                                case TypeMemSpace:
                                case TypePixelFormat:
                                case TypePixelDesc:
                                case TypeVideoCodec:
                                case TypeAudioCodec:
                                case TypeEnum:
                                case TypeEnumList:
#if PROMEKI_ENABLE_NETWORK
                                case TypeSocketAddress:
                                case TypeSdpSession:
                                case TypeMacAddress:
                                case TypeEUI64:
#endif
                                        return get<String>();
                                        break;
                        }
                        return *this;
                }

                /**
                 * @brief Formats the stored value using a type-specific format spec.
                 *
                 * Builds the format string @c "{:<spec>}" and routes it to
                 * @c std::vformat with the held value, so the type's own
                 * @c std::formatter specialization is invoked.  This means
                 * rich types use their native style keywords (Timecode
                 * accepts @c "smpte" / @c "smpte-fps" / @c "field",
                 * VideoFormat accepts @c "smpte" / @c "named", etc.) and
                 * primitives use the standard @c std::format spec grammar
                 * (@c "x", @c ".2f", @c "05d", ...).
                 *
                 * An empty @p spec is short-circuited to @ref get<String>(),
                 * which reproduces the existing default toString() behavior
                 * for every supported type.  Variant alternatives that have
                 * no @c std::formatter specialization fall back to formatting
                 * the spec against the value's String form, so width / fill
                 * / alignment still work for them.
                 *
                 * If @p spec is malformed (caught as @c std::format_error),
                 * the default String form is returned and @p err is set to
                 * @c Error::Invalid.
                 *
                 * @param spec  Type-specific format spec, without the
                 *              surrounding @c {} or leading @c ':'.
                 * @param err   Optional error output, set to @c Error::Ok on
                 *              success or @c Error::Invalid if the spec
                 *              could not be applied.
                 * @return      The formatted String.
                 */
                String format(const String &spec, Error *err = nullptr) const {
                        if(err != nullptr) *err = Error::Ok;
                        String defaultStr = get<String>();
                        if(spec.isEmpty()) return defaultStr;

                        std::string fmtStr;
                        fmtStr.reserve(spec.byteCount() + 3);
                        fmtStr += "{:";
                        fmtStr.append(spec.cstr(), spec.byteCount());
                        fmtStr += '}';

                        try {
                                return std::visit([&fmtStr, &defaultStr](auto &&arg) -> String {
                                        using T = std::decay_t<decltype(arg)>;
                                        if constexpr (std::is_same_v<T, std::monostate>) {
                                                (void)fmtStr;
                                                return String();
                                        } else if constexpr (std::is_default_constructible_v<std::formatter<T, char>>) {
                                                const auto &val = arg;
                                                return String(std::vformat(fmtStr, std::make_format_args(val)));
                                        } else {
                                                std::string_view sv(defaultStr.cstr(), defaultStr.byteCount());
                                                return String(std::vformat(fmtStr, std::make_format_args(sv)));
                                        }
                                }, v);
                        } catch(const std::format_error &) {
                                if(err != nullptr) *err = Error::Invalid;
                                return defaultStr;
                        }
                }

                /**
                 * @brief Resolves the stored value to an Enum of the given type.
                 *
                 * Unlike @c get<Enum>(), which has no context about the
                 * intended enum kind and therefore requires a fully qualified
                 * @c "TypeName::ValueName" string, this method takes a target
                 * @ref Enum::Type handle and uses it to resolve several more
                 * flexible forms.  Accepted inputs:
                 *
                 * - @c TypeInvalid (default-constructed / missing config
                 *   key) → returns @c Enum(enumType), i.e. the type's
                 *   registered default value, with @c Error::Ok.  This
                 *   makes the registered default double as the config
                 *   fallback so callers don't need to repeat it.
                 * - @c TypeEnum holding an @ref Enum of @p enumType → returned
                 *   directly.
                 * - @c TypeEnum holding an @ref Enum of a @em different type
                 *   → error, returns an invalid @ref Enum.
                 * - @c TypeString @c "TypeName::ValueName" (qualified) →
                 *   parsed via @ref Enum::lookup; must match @p enumType.
                 * - @c TypeString @c "ValueName" (unqualified) → resolved by
                 *   name against @p enumType.
                 * - @c TypeString decimal integer (e.g. @c "100", @c "-1")
                 *   → wrapped as @c Enum(enumType, int).  The result may be
                 *   out-of-list but is still valid and round-trippable.
                 * - Any integer @c Type (bool/u8..s64) → wrapped as
                 *   @c Enum(enumType, int).
                 * - Anything else → error, returns an invalid @ref Enum.
                 *
                 * This is the canonical way to pull a typed Enum out of a
                 * config value that may have been authored as a string,
                 * an integer, or a typed Enum.
                 *
                 * @par Example
                 * @code
                 * Variant v = cfg.get(ConfigVideoPattern);
                 * Error err;
                 * Enum pat = v.asEnum(VideoPattern::Type, &err);
                 * if(err.isError() || !pat.isValid()) pat = VideoPattern::ColorBars;
                 * @endcode
                 *
                 * @param enumType  Target @ref Enum type handle.
                 * @param err       Optional error output; set to
                 *                  @c Error::InvalidArgument if @p enumType
                 *                  is invalid, @c Error::Invalid on any
                 *                  other conversion failure, or
                 *                  @c Error::Ok on success.
                 * @return The converted @ref Enum, or a default-constructed
                 *         (invalid) @ref Enum on failure.
                 */
                Enum asEnum(Enum::Type enumType, Error *err = nullptr) const {
                        if(!enumType.isValid()) {
                                if(err != nullptr) *err = Error::InvalidArgument;
                                return Enum();
                        }
                        auto setOk = [err]() { if(err != nullptr) *err = Error::Ok; };
                        auto setErr = [err]() { if(err != nullptr) *err = Error::Invalid; };
                        switch(type()) {
                                case TypeInvalid:
                                        setOk();
                                        return Enum(enumType);
                                case TypeEnum: {
                                        Enum e = get<Enum>();
                                        if(e.type() != enumType) {
                                                setErr();
                                                return Enum();
                                        }
                                        setOk();
                                        return e;
                                }
                                case TypeString: {
                                        String s = get<String>();
                                        // Qualified "TypeName::ValueName"?
                                        if(s.contains("::")) {
                                                Error parseErr;
                                                Enum e = Enum::lookup(s, &parseErr);
                                                if(parseErr.isOk() && e.type() == enumType) {
                                                        setOk();
                                                        return e;
                                                }
                                                setErr();
                                                return Enum();
                                        }
                                        // Unqualified value name against the target type.
                                        Enum byName(enumType, s);
                                        if(byName.hasListedValue()) {
                                                setOk();
                                                return byName;
                                        }
                                        // Last resort: signed decimal integer.
                                        Error intErr;
                                        int iv = s.template to<int>(&intErr);
                                        if(intErr.isOk()) {
                                                setOk();
                                                return Enum(enumType, iv);
                                        }
                                        setErr();
                                        return Enum();
                                }
                                case TypeBool:
                                case TypeU8:
                                case TypeS8:
                                case TypeU16:
                                case TypeS16:
                                case TypeU32:
                                case TypeS32:
                                case TypeU64:
                                case TypeS64: {
                                        Error ge;
                                        int iv = get<int32_t>(&ge);
                                        if(ge.isError()) {
                                                setErr();
                                                return Enum();
                                        }
                                        setOk();
                                        return Enum(enumType, iv);
                                }
                                default:
                                        break;
                        }
                        setErr();
                        return Enum();
                }

                /**
                 * @brief Returns true if both variants hold equal values.
                 *
                 * Comparison is performed in three tiers:
                 *  1. **Same type** — uses the type's own operator==.
                 *  2. **Cross-type numeric** (including bool) — promotes to a
                 *     common representation: double when either operand is
                 *     floating-point, otherwise a safe signed/unsigned integer
                 *     comparison via uint64_t with a negative-value guard.
                 *  3. **Cross-type convertible** — attempts to convert one
                 *     operand to the other's type via get<T>().  Both
                 *     directions are tried; if either conversion succeeds
                 *     and the converted values compare equal, returns true.
                 *
                 * @par Example
                 * @code
                 * // Same type — direct comparison
                 * Variant(int32_t(42)) == Variant(int32_t(42));  // true
                 *
                 * // Cross-type numeric promotion
                 * Variant(int32_t(42)) == Variant(uint32_t(42)); // true
                 * Variant(int32_t(3))  == Variant(3.0);          // true
                 * Variant(int32_t(-1)) == Variant(uint32_t(0));  // false (negative guard)
                 *
                 * // Cross-type convertible
                 * Variant(int32_t(42)) == Variant(String("42")); // true
                 * @endcode
                 */
                bool operator==(const VariantImpl &other) const;

                /** @brief Returns true if the variants are not equal. */
                bool operator!=(const VariantImpl &other) const { return !(*this == other); }

        private:
                std::variant<Types...> v;
};

#define X(name, type) type,
/**
 * @brief Concrete Variant type used throughout promeki.
 *
 * Implemented as a thin subclass of the @ref VariantImpl template
 * so that @c variant_fwd.h can declare @ref Variant as an incomplete
 * class and break header fan-out.  All behaviour is inherited
 * unchanged from @ref VariantImpl.
 */
class Variant : public VariantImpl< PROMEKI_VARIANT_TYPES detail::VariantEnd > {
        public:
                using Base = VariantImpl< PROMEKI_VARIANT_TYPES detail::VariantEnd >;
                using Base::Base;
                Variant() = default;
                Variant(const Base &b) : Base(b) {}
                Variant(Base &&b) : Base(std::move(b)) {}
};
#undef X

/**
 * @brief List of @ref Variant values used for type-erased argument marshalling.
 *
 * Implemented as a thin subclass of @c List<Variant> so that
 * @c variant_fwd.h can declare it as an incomplete class.
 */
class VariantList : public List<Variant> {
        public:
                using List<Variant>::List;
                VariantList() = default;
                VariantList(std::initializer_list<Variant> il) {
                        for(const auto &v : il) pushToBack(v);
                }
                VariantList(const List<Variant> &other) : List<Variant>(other) {}
                VariantList(List<Variant> &&other) : List<Variant>(std::move(other)) {}
};

// ---------------------------------------------------------------------------
// Extern template declarations.  The matching explicit instantiations live
// in src/core/variant.cpp, so consumer TUs don't each re-instantiate the
// ~250-line per-To get<T>() std::visit lambda or the 35²-branch operator==.
// Keeps Variant-heavy TUs from blowing past ~1.5 GB peak RSS at compile.
// `extern template class` requires a class-id (typedef names forbidden),
// so the Variant alternative list is expanded via the X-macro rather than
// spelled through the Variant::Base alias.
// ---------------------------------------------------------------------------

#define X(name, type) type,
extern template class VariantImpl< PROMEKI_VARIANT_TYPES detail::VariantEnd >;
#undef X

#define X(name, type) \
        extern template type Variant::Base::get<type>(Error *err) const;
PROMEKI_VARIANT_TYPES
#undef X

PROMEKI_NAMESPACE_END

/**
 * @brief @c std::formatter partial specialization for @ref promeki::VariantImpl.
 *
 * Routes the held value through @c VariantImpl::get<String>(), which
 * already knows how to convert every variant alternative to a String
 * (numbers via @c String::number, library types via their @c toString,
 * collections like @c StringList via @c join, etc.).  This means
 * @ref promeki::Variant — and any other @c VariantImpl instantiation —
 * is usable as a @ref promeki::String::format argument out of the box,
 * regardless of what type it currently holds.
 *
 * Standard string format specifiers (width, fill, alignment) are
 * inherited from @c std::formatter<std::string_view>.
 *
 * @code
 *   Variant v;
 *   v.set(42);                              // holds int
 *   String s = String::format("v = {}", v); // "v = 42"
 *   v.set(promeki::UUID::generate());       // now holds UUID
 *   s = String::format("v = {}", v);        // "v = <uuid>"
 * @endcode
 */
template <typename... Types>
struct std::formatter<promeki::VariantImpl<Types...>>
        : std::formatter<std::string_view> {
        using Base = std::formatter<std::string_view>;
        template <typename FormatContext>
        auto format(const promeki::VariantImpl<Types...> &v, FormatContext &ctx) const {
                promeki::String s = v.template get<promeki::String>();
                return Base::format(std::string_view(s.cstr(), s.byteCount()), ctx);
        }
};

/**
 * @brief @c std::formatter specialization for @ref promeki::Variant.
 *
 * Because @ref promeki::Variant is now a concrete subclass of
 * @c promeki::VariantImpl, the template partial specialization above
 * does not match it directly — @c std::formatter selects by exact
 * type.  This specialization reuses the VariantImpl formatter via
 * its base subobject.
 */
template <>
struct std::formatter<promeki::Variant>
        : std::formatter<promeki::Variant::Base> {};

