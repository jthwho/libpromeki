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
#include <promeki/framenumber.h>
#include <promeki/framecount.h>
#include <promeki/duration.h>
#include <promeki/mediaduration.h>
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
#include <promeki/map.h>
#include <promeki/uniqueptr.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiocodec.h>
#include <promeki/audioformat.h>
#include <promeki/audiomarker.h>
#include <promeki/audiostreamdesc.h>
#include <promeki/colormodel.h>
#include <promeki/memspace.h>
#include <promeki/pixelmemlayout.h>
#include <promeki/pixelformat.h>
#include <promeki/videocodec.h>
#include <promeki/masteringdisplay.h>
#include <promeki/contentlightlevel.h>
#include <promeki/enum.h>
#include <promeki/enumlist.h>
#include <promeki/url.h>
#include <promeki/windowedstat.h>
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
 * | TypeMediaTimeStamp | `MediaTimeStamp` |
 * | TypeFrameNumber | `FrameNumber`     |
 * | TypeFrameCount | `FrameCount`       |
 * | TypeMediaDuration | `MediaDuration` |
 * | TypeDuration  | `Duration`          |
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
 * | TypePixelMemLayout | `PixelMemLayout`     |
 * | TypePixelFormat | `PixelFormat`         |
 * | TypeVideoCodec | `VideoCodec`       |
 * | TypeAudioCodec | `AudioCodec`       |
 * | TypeAudioFormat | `AudioFormat`     |
 * | TypeAudioStreamDesc | `AudioStreamDesc` |
 * | TypeAudioChannelMap | `AudioChannelMap` |
 * | TypeAudioMarkerList | `AudioMarkerList` |
 * | TypeEnum      | `Enum`              |
 * | TypeEnumList  | `EnumList`          |
 * | TypeWindowedStat | `WindowedStat`   |
 * | TypeVariantList | `VariantList`     |
 * | TypeVariantMap  | `VariantMap`      |
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
#define PROMEKI_VARIANT_TYPES_NETWORK                                                                                  \
        X(TypeSocketAddress, SocketAddress)                                                                            \
        X(TypeSdpSession, SdpSession)                                                                                  \
        X(TypeMacAddress, MacAddress)                                                                                  \
        X(TypeEUI64, EUI64)
#else
#define PROMEKI_VARIANT_TYPES_NETWORK
#endif

#define PROMEKI_VARIANT_TYPES                                                                                          \
        X(TypeInvalid, std::monostate)                                                                                 \
        X(TypeBool, bool)                                                                                              \
        X(TypeU8, uint8_t)                                                                                             \
        X(TypeS8, int8_t)                                                                                              \
        X(TypeU16, uint16_t)                                                                                           \
        X(TypeS16, int16_t)                                                                                            \
        X(TypeU32, uint32_t)                                                                                           \
        X(TypeS32, int32_t)                                                                                            \
        X(TypeU64, uint64_t)                                                                                           \
        X(TypeS64, int64_t)                                                                                            \
        X(TypeFloat, float)                                                                                            \
        X(TypeDouble, double)                                                                                          \
        X(TypeString, String)                                                                                          \
        X(TypeDateTime, DateTime)                                                                                      \
        X(TypeTimeStamp, TimeStamp)                                                                                    \
        X(TypeMediaTimeStamp, MediaTimeStamp)                                                                          \
        X(TypeFrameNumber, FrameNumber)                                                                                \
        X(TypeFrameCount, FrameCount)                                                                                  \
        X(TypeMediaDuration, MediaDuration)                                                                            \
        X(TypeDuration, Duration)                                                                                      \
        X(TypeSize2D, Size2Du32)                                                                                       \
        X(TypeUUID, UUID)                                                                                              \
        X(TypeUMID, UMID)                                                                                              \
        X(TypeTimecode, Timecode)                                                                                      \
        X(TypeRational, Rational<int>)                                                                                 \
        X(TypeFrameRate, FrameRate)                                                                                    \
        X(TypeVideoFormat, VideoFormat)                                                                                \
        X(TypeStringList, StringList)                                                                                  \
        X(TypeColor, Color)                                                                                            \
        X(TypeColorModel, ColorModel)                                                                                  \
        X(TypeMemSpace, MemSpace)                                                                                      \
        X(TypePixelMemLayout, PixelMemLayout)                                                                          \
        X(TypePixelFormat, PixelFormat)                                                                                \
        X(TypeVideoCodec, VideoCodec)                                                                                  \
        X(TypeAudioCodec, AudioCodec)                                                                                  \
        X(TypeAudioFormat, AudioFormat)                                                                                \
        X(TypeAudioStreamDesc, AudioStreamDesc)                                                                        \
        X(TypeAudioChannelMap, AudioChannelMap)                                                                        \
        X(TypeAudioMarkerList, AudioMarkerList)                                                                        \
        X(TypeEnum, Enum)                                                                                              \
        X(TypeEnumList, EnumList)                                                                                      \
        X(TypeMasteringDisplay, MasteringDisplay)                                                                      \
        X(TypeContentLightLevel, ContentLightLevel)                                                                    \
        X(TypeUrl, Url)                                                                                                \
        X(TypeWindowedStat, WindowedStat)                                                                              \
        X(TypeVariantList, VariantList)                                                                                \
        X(TypeVariantMap, VariantMap)                                                                                  \
        PROMEKI_VARIANT_TYPES_NETWORK

namespace detail {
        /** @brief Sentinel type used to absorb the trailing comma from X-macro expansion. */
        struct VariantEnd {
                        bool operator==(const VariantEnd &) const { return true; }
                        bool operator!=(const VariantEnd &) const { return false; }
        };

        /** @brief True for TypeRegistry wrapper types that have an integer ID. */
        template <typename T> struct is_type_registry : std::false_type {};
        template <> struct is_type_registry<AudioCodec> : std::true_type {};
        template <> struct is_type_registry<AudioFormat> : std::true_type {};
        template <> struct is_type_registry<ColorModel> : std::true_type {};
        template <> struct is_type_registry<MemSpace> : std::true_type {};
        template <> struct is_type_registry<PixelMemLayout> : std::true_type {};
        template <> struct is_type_registry<PixelFormat> : std::true_type {};
        template <> struct is_type_registry<VideoCodec> : std::true_type {};
        template <typename T> inline constexpr bool is_type_registry_v = is_type_registry<T>::value;
}

/**
 * @brief Type-safe tagged union that can hold any of the types listed in PROMEKI_VARIANT_TYPES.
 *
 * VariantImpl is a thin wrapper around `std::variant` that adds a Type enum,
 * human-readable type names, and automatic type-conversion logic via the
 * templated get() method.  It is not intended to be used directly; instead use
 * the @ref Variant type alias which is instantiated with the concrete type
 * list.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance — including any combination of
 * @c set() / @c get() / @c reset() — must be externally synchronized.  The
 * underlying @c std::variant is not internally synchronized.
 *
 * @par Example
 * @code
 * Variant v = 42;
 * String s = v.get<String>();  // "42"
 * v.set(String("hello"));
 * bool valid = v.isValid();    // true
 * Variant::Type t = v.type();  // Variant::TypeString
 * @endcode
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
                enum Type {
                        PROMEKI_VARIANT_TYPES
                };
#undef X

#define X(name, type) PROMEKI_STRINGIFY(type),
                /**
                 * @brief Returns the human-readable C++ type name for the given Type enumerator.
                 * @param[in] id  The Type enumerator value.
                 * @return A null-terminated string such as `"bool"`, `"String"`, etc.
                 */
                static const char *typeName(Type id) {
                        static const char *items[] = {PROMEKI_VARIANT_TYPES};
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
                 *  - array             -> `VariantList` (recursive, every element converted via fromJson)
                 *  - object            -> `VariantMap`  (recursive, every value converted via fromJson)
                 *
                 * Arrays and objects produce a typed nested tree rather than the
                 * flattened-string fallback used historically — callers can
                 * walk the tree, validate it against a @ref VariantSpec, and
                 * round-trip back to JSON without losing structure.
                 *
                 * Defined out-of-line below the @ref VariantList / @ref VariantMap
                 * class definitions so the body can construct those types.
                 *
                 * @param val  The JSON value to convert.
                 * @return A VariantImpl holding the converted value.
                 */
                static VariantImpl fromJson(const nlohmann::json &val);

                /** @brief Default-constructs an invalid (empty) variant holding std::monostate. */
                VariantImpl() = default;

                /**
                 * @brief Constructs a variant holding a copy of @p value.
                 *
                 * Constrained so the converting constructor only participates
                 * in overload resolution when @c T is exactly one of the
                 * variant's alternatives — otherwise an unrelated single-arg
                 * call would shadow the copy/move constructors and fail with
                 * an opaque @c std::variant template error deep in the call.
                 *
                 * @tparam T  The type of the value; must be one of the supported variant types.
                 * @param value  The value to store.
                 */
                template <typename T,
                          typename = std::enable_if_t<std::disjunction_v<std::is_convertible<const T &, Types>...>>>
                VariantImpl(const T &value) : v(value) {}

                /** @brief Returns true if the variant holds a value other than std::monostate. */
                bool isValid() const { return v.index() != 0; }

                /**
                 * @brief Replaces the currently held value with @p value.
                 *
                 * Constrained the same way as the converting constructor —
                 * unrelated single-arg @c set calls otherwise produce an
                 * opaque @c std::variant assignment error.
                 *
                 * @tparam T  The type of the new value; must be one of the supported variant types.
                 * @param value  The value to store.
                 */
                template <typename T,
                          typename = std::enable_if_t<std::disjunction_v<std::is_convertible<const T &, Types>...>>>
                void set(const T &value) {
                        v = value;
                }

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

                /**
                 * @brief Borrows the held value when its type is exactly @p T.
                 *
                 * Returns a non-owning pointer into the underlying
                 * @c std::variant alternative when @p T matches the
                 * current type, or @c nullptr otherwise.  Cheap (no
                 * copy) and especially valuable for the heavy
                 * alternatives — @ref VariantList, @ref VariantMap,
                 * @ref String, @ref Buffer and similar container types — where
                 * calling @ref get repeatedly during a tree walk would deep-copy
                 * each container.
                 *
                 * @par Example
                 * @code
                 * if (const VariantMap *m = v.peek<VariantMap>()) {
                 *     // walk *m without paying for a deep copy
                 *     m->forEach([](const String &k, const Variant &v) { ... });
                 * }
                 * @endcode
                 *
                 * @tparam T  Any alternative listed in @c PROMEKI_VARIANT_TYPES.
                 * @return    Pointer to the stored value, or @c nullptr
                 *            when the current type is not @p T.
                 */
                template <typename T> const T *peek() const noexcept { return std::get_if<T>(&v); }

                /** @brief Returns the Type enumerator for the currently held value. */
                Type type() const { return static_cast<Type>(v.index()); }

                /** @brief Returns the human-readable type name of the currently held value. */
                const char *typeName() const { return typeName(type()); }

                /**
                 * @brief Converts complex types to their String representation, leaving simple types unchanged.
                 *
                 * Numeric types (integers, floating point, bool) and the
                 * Invalid alternative are returned as-is.  Every other
                 * Variant alternative is converted to String via
                 * `get<String>()`.  This is driven by a type trait so
                 * new alternatives added to PROMEKI_VARIANT_TYPES are
                 * picked up automatically without editing this method.
                 *
                 * @return A new VariantImpl containing either the original value or its
                 *         String representation.
                 */
                VariantImpl toStandardType() const {
                        return std::visit(
                                [this](const auto &val) -> VariantImpl {
                                        using T = std::decay_t<decltype(val)>;
                                        if constexpr (std::is_same_v<T, std::monostate> ||
                                                      std::is_same_v<T, detail::VariantEnd> ||
                                                      std::is_arithmetic_v<T>) {
                                                return *this;
                                        } else {
                                                return get<String>();
                                        }
                                },
                                v);
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
                        if (err != nullptr) *err = Error::Ok;
                        String defaultStr = get<String>();
                        if (spec.isEmpty()) return defaultStr;

                        std::string fmtStr;
                        fmtStr.reserve(spec.byteCount() + 3);
                        fmtStr += "{:";
                        fmtStr.append(spec.cstr(), spec.byteCount());
                        fmtStr += '}';

                        try {
                                return std::visit(
                                        [&fmtStr, &defaultStr](auto &&arg) -> String {
                                                using T = std::decay_t<decltype(arg)>;
                                                if constexpr (std::is_same_v<T, std::monostate>) {
                                                        (void)fmtStr;
                                                        return String();
                                                } else if constexpr (std::is_default_constructible_v<
                                                                             std::formatter<T, char>>) {
                                                        const auto &val = arg;
                                                        return String(std::vformat(fmtStr, std::make_format_args(val)));
                                                } else {
                                                        std::string_view sv(defaultStr.cstr(), defaultStr.byteCount());
                                                        return String(std::vformat(fmtStr, std::make_format_args(sv)));
                                                }
                                        },
                                        v);
                        } catch (const std::format_error &) {
                                if (err != nullptr) *err = Error::Invalid;
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
                        if (!enumType.isValid()) {
                                if (err != nullptr) *err = Error::InvalidArgument;
                                return Enum();
                        }
                        auto setOk = [err]() {
                                if (err != nullptr) *err = Error::Ok;
                        };
                        auto setErr = [err]() {
                                if (err != nullptr) *err = Error::Invalid;
                        };
                        switch (type()) {
                                case TypeInvalid: setOk(); return Enum(enumType);
                                case TypeEnum: {
                                        Enum e = get<Enum>();
                                        if (e.type() != enumType) {
                                                setErr();
                                                return Enum();
                                        }
                                        setOk();
                                        return e;
                                }
                                case TypeString: {
                                        String s = get<String>();
                                        // Qualified "TypeName::ValueName"?
                                        if (s.contains("::")) {
                                                Error parseErr;
                                                Enum  e = Enum::lookup(s, &parseErr);
                                                if (parseErr.isOk() && e.type() == enumType) {
                                                        setOk();
                                                        return e;
                                                }
                                                setErr();
                                                return Enum();
                                        }
                                        // Unqualified value name against the target type.
                                        Enum byName(enumType, s);
                                        if (byName.hasListedValue()) {
                                                setOk();
                                                return byName;
                                        }
                                        // Last resort: signed decimal integer.
                                        Error intErr;
                                        int   iv = s.template to<int>(&intErr);
                                        if (intErr.isOk()) {
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
                                        int   iv = get<int32_t>(&ge);
                                        if (ge.isError()) {
                                                setErr();
                                                return Enum();
                                        }
                                        setOk();
                                        return Enum(enumType, iv);
                                }
                                default: break;
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

/**
 * @brief Heterogeneous list of @ref Variant values.
 * @ingroup util
 *
 * @ref VariantList is a Variant alternative — i.e. a Variant can hold
 * a VariantList directly, enabling JSON-shaped trees and recursive
 * payloads to flow through the standard Variant pipeline.  To break
 * the size recursion (a VariantList contains Variants which can
 * themselves be VariantLists), the underlying @c List<Variant>
 * lives behind a @ref UniquePtr handle; @c sizeof(VariantList) is
 * one pointer regardless of how the contained Variants nest.
 *
 * Public API forwards the heavily-used @c List<Variant> operations
 * (size, indexing, push, clear, range-for) directly.  For operations
 * not surfaced here, callers can borrow the underlying list via
 * @ref list().
 *
 * @par Element iteration
 * Iteration is exposed via raw @c Variant @c * pointers — the underlying
 * @c std::vector<Variant> storage is contiguous, so range-for and
 * pointer arithmetic both work.  This avoids leaking
 * @c List<Variant>::iterator into the header (which would require
 * @ref Variant to be a complete type for the iterator's full
 * instantiation).
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 */
class VariantList {
        public:
                /** @cond INTERNAL */
                struct Impl;
                /** @endcond */

                /** @brief Mutable forward iterator. */
                using Iterator = Variant *;
                /** @brief Const forward iterator. */
                using ConstIterator = const Variant *;

                /** @brief Constructs an empty list. */
                VariantList();
                /** @brief Constructs from an initializer list of Variants. */
                VariantList(std::initializer_list<Variant> il);
                /** @brief Constructs from an existing @c List<Variant> (deep copy). */
                explicit VariantList(const List<Variant> &other);
                /** @brief Constructs from an existing @c List<Variant> (move). */
                explicit VariantList(List<Variant> &&other);

                VariantList(const VariantList &other);
                VariantList(VariantList &&other) noexcept;
                ~VariantList();

                VariantList &operator=(const VariantList &other);
                VariantList &operator=(VariantList &&other) noexcept;

                /** @brief Returns the number of stored Variants. */
                size_t size() const;
                /** @brief Returns true when the list has no elements. */
                bool isEmpty() const;
                /** @brief Removes all elements. */
                void clear();
                /** @brief Pre-allocates storage for at least @p capacity elements. */
                void reserve(size_t capacity);

                /** @brief Returns a mutable reference to the element at @p index (no bounds check). */
                Variant &operator[](size_t index);
                /** @brief Returns a const reference to the element at @p index (no bounds check). */
                const Variant &operator[](size_t index) const;
                /** @brief Returns a mutable reference to the element at @p index, throwing on OOB. */
                Variant &at(size_t index);
                /** @brief Returns a const reference to the element at @p index, throwing on OOB. */
                const Variant &at(size_t index) const;

                /** @brief Appends @p v to the end of the list. */
                void pushToBack(const Variant &v);
                /** @brief Appends @p v to the end of the list (move overload). */
                void pushToBack(Variant &&v);
                /** @brief Removes the last element.  Undefined when empty. */
                void popBack();

                /** @brief Returns a pointer to the underlying contiguous storage. */
                Variant *data();
                /// @copydoc data()
                const Variant *data() const;

                /** @brief Returns iterator to the first element. */
                Iterator begin();
                /** @brief Returns iterator to one past the last element. */
                Iterator end();
                /** @brief Returns const iterator to the first element. */
                ConstIterator begin() const;
                /** @brief Returns const iterator to one past the last element. */
                ConstIterator end() const;
                /// @copydoc begin() const
                ConstIterator cbegin() const;
                /// @copydoc end() const
                ConstIterator cend() const;

                /**
                 * @brief Borrows the underlying @c List<Variant> for advanced operations.
                 *
                 * Use this when you need a method not surfaced on
                 * VariantList directly (sort, contains, removeIf, etc.).
                 * The returned reference is valid for the lifetime of
                 * this VariantList.
                 */
                List<Variant> &list();
                /// @copydoc list()
                const List<Variant> &list() const;

                /** @brief Returns true iff both lists hold equal sequences of Variants. */
                bool operator==(const VariantList &other) const;
                /** @brief Returns true iff the lists differ. */
                bool operator!=(const VariantList &other) const { return !(*this == other); }

                /**
                 * @brief Renders the list as a JSON-array string.
                 *
                 * Each element is rendered via @ref JsonArray::addFromVariant,
                 * which itself recurses into nested VariantList / VariantMap
                 * entries.  Useful for passing a VariantList through interfaces
                 * that only accept @c String values (DataStream, log lines,
                 * config files).
                 */
                String toJsonString() const;

                /**
                 * @brief Parses a JSON-array string into a VariantList.
                 *
                 * @param json  JSON text of the form @c "[...]".  Anything
                 *              else (including a JSON object) sets @p err to
                 *              @ref Error::ParseFailed and returns an empty
                 *              list.
                 * @param err   Optional error output.
                 * @return      The parsed list, or an empty list on failure.
                 */
                static VariantList fromJsonString(const String &json, Error *err = nullptr);

        private:
                UniquePtr<Impl> _impl;
};

/**
 * @brief Heterogeneous string-keyed map of @ref Variant values.
 * @ingroup util
 *
 * @ref VariantMap is a Variant alternative — same shape as
 * @ref VariantList but keyed by @ref String.  Used for JSON-shaped
 * objects, RPC argument bags, dynamic config payloads.  As with
 * VariantList, the underlying @c Map<String, Variant> lives behind
 * a @ref UniquePtr handle so the type is complete with known size
 * even when @ref Variant is incomplete.
 *
 * @par Iteration
 * @c std::map storage is not contiguous, so map iteration is exposed
 * via @ref forEach and via @ref VariantMap::keys "keys" rather than raw
 * iterators.  Per-key access is via @ref value, @ref find, and
 * @ref contains.
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VariantList.
 */
class VariantMap {
        public:
                /** @cond INTERNAL */
                struct Impl;
                /** @endcond */

                /** @brief Constructs an empty map. */
                VariantMap();
                /** @brief Constructs from an initializer list of (key, value) pairs. */
                VariantMap(std::initializer_list<std::pair<const String, Variant>> il);
                /** @brief Constructs from an existing @c Map<String, Variant> (deep copy). */
                explicit VariantMap(const Map<String, Variant> &other);
                /** @brief Constructs from an existing @c Map<String, Variant> (move). */
                explicit VariantMap(Map<String, Variant> &&other);

                VariantMap(const VariantMap &other);
                VariantMap(VariantMap &&other) noexcept;
                ~VariantMap();

                VariantMap &operator=(const VariantMap &other);
                VariantMap &operator=(VariantMap &&other) noexcept;

                /** @brief Returns the number of (key, value) pairs. */
                size_t size() const;
                /** @brief Returns true when the map has no entries. */
                bool isEmpty() const;
                /** @brief Removes all entries. */
                void clear();

                /** @brief Returns true if @p key exists in the map. */
                bool contains(const String &key) const;

                /** @brief Inserts (or replaces) the entry for @p key. */
                void insert(const String &key, const Variant &value);
                /// @copydoc insert(const String &, const Variant &)
                void insert(const String &key, Variant &&value);

                /** @brief Removes the entry for @p key, returning true if it was present. */
                bool remove(const String &key);

                /**
                 * @brief Returns the value for @p key, or an invalid Variant if absent.
                 *
                 * Callers can distinguish "missing" from "present but invalid" via
                 * @ref contains.
                 */
                Variant value(const String &key) const;

                /** @brief Returns the value for @p key, or @p defaultValue if absent. */
                Variant value(const String &key, const Variant &defaultValue) const;

                /** @brief Returns a pointer to the entry for @p key, or @c nullptr. */
                Variant *find(const String &key);
                /// @copydoc find(const String &)
                const Variant *find(const String &key) const;

                /** @brief Returns a sorted list of all keys present in the map. */
                StringList keys() const;

                /** @brief Iterates every (key, value) pair in key order. */
                void forEach(std::function<void(const String &, const Variant &)> fn) const;

                /**
                 * @brief Borrows the underlying @c Map<String, Variant> for
                 *        operations not surfaced directly.
                 */
                Map<String, Variant> &map();
                /// @copydoc map()
                const Map<String, Variant> &map() const;

                /** @brief Returns true iff both maps hold equal entry sets. */
                bool operator==(const VariantMap &other) const;
                /** @brief Returns true iff the maps differ. */
                bool operator!=(const VariantMap &other) const { return !(*this == other); }

                /**
                 * @brief Renders the map as a JSON-object string.
                 *
                 * Recursive: nested VariantList / VariantMap values become
                 * nested JSON arrays / objects.
                 */
                String toJsonString() const;

                /**
                 * @brief Parses a JSON-object string into a VariantMap.
                 *
                 * @param json  JSON text of the form @c "{...}".  Anything
                 *              else (including a JSON array) sets @p err to
                 *              @ref Error::ParseFailed and returns an empty
                 *              map.
                 * @param err   Optional error output.
                 * @return      The parsed map, or an empty map on failure.
                 */
                static VariantMap fromJsonString(const String &json, Error *err = nullptr);

        private:
                UniquePtr<Impl> _impl;
};

#define X(name, type) type,
/**
 * @brief Concrete Variant type used throughout promeki.
 *
 * Implemented as a thin subclass of the @ref VariantImpl template
 * so that @c variant_fwd.h can declare @ref Variant as an incomplete
 * class and break header fan-out.  All behaviour is inherited
 * unchanged from @ref VariantImpl.
 *
 * @par JSON-shaped trees
 * @ref VariantList and @ref VariantMap are first-class Variant
 * alternatives, so a Variant can hold a recursive tree of values.
 * @ref fromJson decodes a @c nlohmann::json into a typed tree;
 * @ref VariantList::toJsonString / @ref VariantMap::toJsonString
 * round-trip back to the wire form.  Walk the tree with
 * @ref promekiResolveVariantPath using the standard
 * @c "name.sub[N].leaf" syntax.
 *
 * @code
 * nlohmann::json j = nlohmann::json::parse(R"({
 *     "title": "demo",
 *     "tags":  ["alpha", "beta"],
 *     "video": {"width": 1920, "height": 1080}
 * })");
 * Variant tree = Variant::fromJson(j);
 *
 * // Borrow the underlying VariantMap without a deep copy.
 * if (const VariantMap *m = tree.peek<VariantMap>()) {
 *     CHECK(m->value("title").get<String>() == "demo");
 * }
 *
 * // Deep lookup using the dotted/indexed path syntax.
 * Variant w = promekiResolveVariantPath(tree, "video.width");
 * CHECK(w.get<int32_t>() == 1920);
 *
 * Variant tag1 = promekiResolveVariantPath(tree, "tags[1]");
 * CHECK(tag1.get<String>() == "beta");
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref VariantImpl.
 */
class Variant : public VariantImpl<PROMEKI_VARIANT_TYPES detail::VariantEnd> {
        public:
                using Base = VariantImpl<PROMEKI_VARIANT_TYPES detail::VariantEnd>;
                using Base::Base;
                Variant() = default;
                Variant(const Base &b) : Base(b) {}
                Variant(Base &&b) : Base(std::move(b)) {}
};
#undef X

// Out-of-line VariantImpl::fromJson — must come after VariantList /
// VariantMap class definitions so the body can construct them.
template <typename... Types>
VariantImpl<Types...> VariantImpl<Types...>::fromJson(const nlohmann::json &val) {
        if (val.is_null()) return VariantImpl();
        if (val.is_boolean()) return val.get<bool>();
        if (val.is_number_integer()) {
                if (val.is_number_unsigned()) return val.get<uint64_t>();
                return val.get<int64_t>();
        }
        if (val.is_number_float()) return val.get<double>();
        if (val.is_string()) return String(val.get<std::string>());
        if (val.is_array()) {
                VariantList list;
                list.reserve(val.size());
                for (const auto &item : val) {
                        list.pushToBack(Variant(VariantImpl::fromJson(item)));
                }
                return list;
        }
        if (val.is_object()) {
                VariantMap map;
                for (auto it = val.begin(); it != val.end(); ++it) {
                        map.insert(String(it.key()), Variant(VariantImpl::fromJson(it.value())));
                }
                return map;
        }
        return String(val.dump());
}

// ---------------------------------------------------------------------------
// Extern template declarations.  The matching explicit instantiations live
// in src/core/variant.cpp, so consumer TUs don't each re-instantiate the
// ~250-line per-To get<T>() std::visit lambda or the 35²-branch operator==.
// Keeps Variant-heavy TUs from blowing past ~1.5 GB peak RSS at compile.
// `extern template class` requires a class-id (typedef names forbidden),
// so the Variant alternative list is expanded via the X-macro rather than
// spelled through the Variant::Base alias.
// ---------------------------------------------------------------------------

/// @cond INTERNAL
#define X(name, type) type,
extern template class VariantImpl<PROMEKI_VARIANT_TYPES detail::VariantEnd>;
#undef X

#define X(name, type) extern template type Variant::Base::get<type>(Error * err) const;
PROMEKI_VARIANT_TYPES
#undef X
/// @endcond

class DataStream;

/**
 * @brief Writes a VariantList to a DataStream.
 *
 * Forwards to the generic @c operator<<(DataStream &, const List<T> &)
 * via the underlying @c List<Variant>.  Defined out-of-line in
 * @c variant.cpp so the @c List<Variant> template instantiation
 * stays in one TU.
 */
DataStream &operator<<(DataStream &stream, const VariantList &list);

/** @brief Reads a VariantList from a DataStream. */
DataStream &operator>>(DataStream &stream, VariantList &list);

/** @brief Writes a VariantMap to a DataStream. */
DataStream &operator<<(DataStream &stream, const VariantMap &map);

/** @brief Reads a VariantMap from a DataStream. */
DataStream &operator>>(DataStream &stream, VariantMap &map);

/**
 * @brief Resolves a dotted/indexed path against a @ref Variant tree.
 * @ingroup util
 *
 * Walks @p root following the same key grammar used by
 * @ref VariantLookup (`segment ( '.' segment )*`, where
 * `segment := name ( '[' index ']' )?`), descending through nested
 * @ref VariantMap and @ref VariantList alternatives as needed.
 *
 * @par Examples
 *  - `"foo"` against a VariantMap → returns the value for key `"foo"`.
 *  - `"foo.bar"` against a VariantMap → looks up `foo`, descends if it
 *    holds a VariantMap, then looks up `bar`.
 *  - `"foo[0]"` → looks up `foo`, descends if it holds a VariantList,
 *    returns element 0.
 *  - `"[0]"` against a VariantList — the leading segment is empty
 *    so the walker treats the whole @p root as the list and returns
 *    element 0.
 *
 * @par Errors
 *  - @c Error::IdNotFound — a key was missing from a VariantMap.
 *  - @c Error::OutOfRange — a list index was beyond the VariantList size.
 *  - @c Error::ParseFailed — the path itself was malformed.
 *  - @c Error::Invalid     — descended into a Variant that wasn't
 *                            VariantMap / VariantList when the path
 *                            still had segments to walk.
 *
 * @param root  The Variant to walk.  Typically holds a VariantMap or VariantList.
 * @param path  The dotted/indexed path to follow.
 * @param err   Optional error output.
 * @return      The resolved Variant, or an invalid Variant on failure.
 */
Variant promekiResolveVariantPath(const Variant &root, const String &path, Error *err = nullptr);

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
template <typename... Types> struct std::formatter<promeki::VariantImpl<Types...>> : std::formatter<std::string_view> {
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
template <> struct std::formatter<promeki::Variant> : std::formatter<promeki::Variant::Base> {};
