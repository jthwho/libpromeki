/**
 * @file      variant.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <variant>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/util.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/datetime.h>
#include <promeki/size2d.h>
#include <promeki/uuid.h>
#include <promeki/umid.h>
#include <promeki/timecode.h>
#include <promeki/rational.h>
#include <promeki/framerate.h>
#include <promeki/stringlist.h>
#include <promeki/color.h>
#include <promeki/list.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/pixelformat.h>
#include <promeki/pixeldesc.h>
#include <promeki/enum.h>
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
 * | TypeStringList| `StringList`        |
 * | TypeColor     | `Color`             |
 * | TypeColorModel | `ColorModel`      |
 * | TypeMemSpace  | `MemSpace`          |
 * | TypePixelFormat | `PixelFormat`     |
 * | TypePixelDesc | `PixelDesc`         |
 * | TypeEnum      | `Enum`              |
 */
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
        X(TypeSize2D, Size2Du32)           \
        X(TypeUUID, UUID)               \
        X(TypeUMID, UMID)               \
        X(TypeTimecode, Timecode)       \
        X(TypeRational, Rational<int>)  \
        X(TypeFrameRate, FrameRate)     \
        X(TypeStringList, StringList)   \
        X(TypeColor, Color)             \
        X(TypeColorModel, ColorModel)   \
        X(TypeMemSpace, MemSpace)       \
        X(TypePixelFormat, PixelFormat) \
        X(TypePixelDesc, PixelDesc)     \
        X(TypeEnum, Enum)

namespace detail {
        /** @brief Sentinel type used to absorb the trailing comma from X-macro expansion. */
        struct VariantEnd {
                bool operator==(const VariantEnd &) const { return true; }
                bool operator!=(const VariantEnd &) const { return false; }
        };

        /** @brief True for TypeRegistry wrapper types that have an integer ID. */
        template <typename T> struct is_type_registry : std::false_type {};
        template <> struct is_type_registry<ColorModel>   : std::true_type {};
        template <> struct is_type_registry<MemSpace>     : std::true_type {};
        template <> struct is_type_registry<PixelFormat>  : std::true_type {};
        template <> struct is_type_registry<PixelDesc>    : std::true_type {};
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
                template <typename To> To get(Error *err = nullptr) const {
                        return std::visit([err](auto &&arg) -> To {
                                using From = std::decay_t<decltype(arg)>;
                                if(err != nullptr) *err = Error::Ok;
                                if constexpr (std::is_same_v<From, To>) {
                                        return arg;

                                } else if constexpr (std::is_same_v<To, bool>) {
                                        if constexpr (std::is_integral<From>::value ||
                                                std::is_floating_point<From>::value) return arg ? true : false;
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(err);

                                } else if constexpr (std::is_integral<To>::value) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value ||
                                                std::is_floating_point<From>::value) return promekiConvert<To>(arg, err);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(err);
                                        if constexpr (detail::is_type_registry_v<From>) return static_cast<To>(arg.id());
                                        if constexpr (std::is_same_v<From, Enum>) return static_cast<To>(arg.value());

                                } else if constexpr (std::is_same_v<To, float>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value ||
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, err);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(err);
                                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toDouble();
                                        if constexpr (std::is_same_v<From, FrameRate>) return static_cast<float>(arg.toDouble());

                                } else if constexpr (std::is_same_v<To, double>) {
                                        if constexpr (std::is_same_v<From, bool>) return !!arg;
                                        if constexpr (std::is_integral<From>::value ||
                                                        std::is_floating_point<From>::value) return promekiConvert<To>(arg, err);
                                        if constexpr (std::is_same_v<From, String>) return arg.template to<To>(err);
                                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toDouble();
                                        if constexpr (std::is_same_v<From, FrameRate>) return arg.toDouble();

                                } else if constexpr (std::is_same_v<To, DateTime>) {
                                        if constexpr (std::is_same_v<From, String>) return DateTime::fromString(
                                                        arg, DateTime::DefaultFormat, err);

                                } else if constexpr (std::is_same_v<To, UUID>) {
                                        if constexpr (std::is_same_v<From, String>) {
                                                Error e;
                                                UUID ret = UUID::fromString(arg, &e);
                                                if(e.isError()) {
                                                        if(err != nullptr) *err = Error::Invalid;
                                                        return UUID();
                                                }
                                                return ret;
                                        }

                                } else if constexpr (std::is_same_v<To, UMID>) {
                                        if constexpr (std::is_same_v<From, String>) {
                                                Error e;
                                                UMID ret = UMID::fromString(arg, &e);
                                                if(e.isError()) {
                                                        if(err != nullptr) *err = Error::Invalid;
                                                        return UMID();
                                                }
                                                return ret;
                                        }

                                } else if constexpr (std::is_same_v<To, Timecode>) {
                                        if constexpr (std::is_same_v<From, String>) {
                                                Result<Timecode> ret = Timecode::fromString(arg);
                                                if(ret.second().isError()) {
                                                        if(err != nullptr) *err = Error::Invalid;
                                                        return Timecode();
                                                }
                                                return ret.first();
                                        }


                                } else if constexpr (std::is_same_v<To, FrameRate>) {
                                        if constexpr (std::is_same_v<From, String>) {
                                                auto [fr, e] = FrameRate::fromString(arg);
                                                if(e.isError()) {
                                                        if(err != nullptr) *err = Error::Invalid;
                                                        return FrameRate();
                                                }
                                                return fr;
                                        }
                                        if constexpr (std::is_same_v<From, Rational<int>>) {
                                                return FrameRate(FrameRate::RationalType(
                                                        static_cast<unsigned int>(arg.numerator()),
                                                        static_cast<unsigned int>(arg.denominator())));
                                        }

                                } else if constexpr (std::is_same_v<To, StringList>) {
                                        if constexpr (std::is_same_v<From, String>) return arg.split(",");

                                } else if constexpr (std::is_same_v<To, Color>) {
                                        if constexpr (std::is_same_v<From, String>) return Color::fromString(arg);

                                } else if constexpr (std::is_same_v<To, ColorModel>) {
                                        if constexpr (std::is_same_v<From, String>) return ColorModel::lookup(arg);
                                        if constexpr (std::is_integral<From>::value) return ColorModel(static_cast<ColorModel::ID>(arg));

                                } else if constexpr (std::is_same_v<To, MemSpace>) {
                                        if constexpr (std::is_integral<From>::value) return MemSpace(static_cast<MemSpace::ID>(arg));

                                } else if constexpr (std::is_same_v<To, PixelFormat>) {
                                        if constexpr (std::is_same_v<From, String>) return PixelFormat::lookup(arg);
                                        if constexpr (std::is_integral<From>::value) return PixelFormat(static_cast<PixelFormat::ID>(arg));

                                } else if constexpr (std::is_same_v<To, PixelDesc>) {
                                        if constexpr (std::is_same_v<From, String>) return PixelDesc::lookup(arg);
                                        if constexpr (std::is_integral<From>::value) return PixelDesc(static_cast<PixelDesc::ID>(arg));

                                } else if constexpr (std::is_same_v<To, Enum>) {
                                        // Only String->Enum is supported; integer->Enum is intentionally
                                        // disallowed because an Enum needs its type context, which a bare
                                        // integer does not carry.  The consumer of the Variant must build
                                        // the Enum themselves when they know the intended type.
                                        if constexpr (std::is_same_v<From, String>) {
                                                Error e;
                                                Enum ret = Enum::lookup(arg, &e);
                                                if(e.isError()) {
                                                        if(err != nullptr) *err = Error::Invalid;
                                                        return Enum();
                                                }
                                                return ret;
                                        }

                                } else if constexpr (std::is_same_v<To, String>) {
                                        if constexpr (std::is_same_v<From, bool>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, int8_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, uint8_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, int16_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, uint16_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, int32_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, uint32_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, int64_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, uint64_t>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, float>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, double>) return String::number(arg);
                                        if constexpr (std::is_same_v<From, DateTime>) return arg.toString();
                                        if constexpr (std::is_same_v<From, TimeStamp>) return arg.toString();
                                        if constexpr (std::is_same_v<From, Size2Du32>) return arg.toString();
                                        if constexpr (std::is_same_v<From, UUID>) return arg.toString();
                                        if constexpr (std::is_same_v<From, UMID>) return arg.toString();
                                        if constexpr (std::is_same_v<From, Timecode>) return arg.toString().first();
                                        if constexpr (std::is_same_v<From, Rational<int>>) return arg.toString();
                                        if constexpr (std::is_same_v<From, FrameRate>) return arg.toString();
                                        if constexpr (std::is_same_v<From, StringList>) return arg.join(",");
                                        if constexpr (std::is_same_v<From, Color>) return arg.toString();
                                        if constexpr (detail::is_type_registry_v<From>) return arg.name();
                                        if constexpr (std::is_same_v<From, Enum>) return arg.toString();

                                }
                                if(err != nullptr) *err = Error::Invalid;
                                return To{};
                        }, v);
                }

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
                                case TypeSize2D:
                                case TypeUUID:
                                case TypeUMID:
                                case TypeTimecode:
                                case TypeRational:
                                case TypeFrameRate:
                                case TypeStringList:
                                case TypeColor:
                                case TypeColorModel:
                                case TypeMemSpace:
                                case TypePixelFormat:
                                case TypePixelDesc:
                                case TypeEnum:
                                        return get<String>();
                                        break;
                        }
                        return *this;
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
                bool operator==(const VariantImpl &other) const {
                        return std::visit([this, &other](auto &&a, auto &&b) -> bool {
                                using A = std::decay_t<decltype(a)>;
                                using B = std::decay_t<decltype(b)>;
                                if constexpr (std::is_same_v<A, B>) {
                                        return a == b;
                                } else if constexpr (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>) {
                                        if constexpr (std::is_floating_point_v<A> || std::is_floating_point_v<B>) {
                                                return static_cast<double>(a) == static_cast<double>(b);
                                        } else if constexpr (std::is_signed_v<A> && !std::is_signed_v<B>) {
                                                if(a < 0) return false;
                                                return static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
                                        } else if constexpr (!std::is_signed_v<A> && std::is_signed_v<B>) {
                                                if(b < 0) return false;
                                                return static_cast<uint64_t>(a) == static_cast<uint64_t>(b);
                                        } else {
                                                using Common = std::common_type_t<A, B>;
                                                return static_cast<Common>(a) == static_cast<Common>(b);
                                        }
                                } else {
                                        Error err;
                                        A ca = other.template get<A>(&err);
                                        if(err.isOk() && a == ca) return true;
                                        B cb = get<B>(&err);
                                        if(err.isOk() && cb == b) return true;
                                        return false;
                                }
                        }, v, other.v);
                }

                /** @brief Returns true if the variants are not equal. */
                bool operator!=(const VariantImpl &other) const { return !(*this == other); }

        private:
                std::variant<Types...> v;
};

#define X(name, type) type,
/** @brief Concrete variant type instantiated with every type from PROMEKI_VARIANT_TYPES. */
using Variant = VariantImpl< PROMEKI_VARIANT_TYPES detail::VariantEnd >;
#undef X

/** @brief Convenience alias for a List of Variant values. */
using VariantList = List<Variant>;

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

