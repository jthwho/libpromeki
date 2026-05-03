/**
 * @file      enum.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/list.h>
#include <promeki/pair.h>
#include <promeki/error.h>
#include <promeki/result.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Runtime-typed enumeration value carrying a registered type and integer.
 * @ingroup util
 *
 * An Enum is a small value that pairs a @ref Type handle with an integer
 * value belonging to that type.  Each enum kind ("Codec", "Severity", ...) is
 * registered once at static-init time via registerType(), which records its
 * (name, int) value table and a mandatory default value in a global registry.
 * Once registered, individual Enum values can convert to/from their string
 * names without any per-value storage cost.
 *
 * Internally, both Enum and @ref Type store a direct pointer to the
 * immutable per-type Definition record.  Resolving an Enum's name, value
 * list, default, or type name is a lock-free, direct memory access — no
 * registry lookup or mutex acquisition on the hot path.
 *
 * Enum is a "Simple" data object — plain value semantics, no
 * PROMEKI_SHARED_FINAL — and is intended to live inside a Variant.
 * Its string form is "TypeName::ValueName", which is what Variant
 * produces when converting an Enum to String and what lookup() parses
 * when converting back.
 *
 * Re-registering a type name with a different value table triggers an
 * assertion in debug builds; each type must be registered exactly once.
 *
 * An Enum's integer value is @e not restricted to the registered value
 * table — any integer is accepted.  Use hasListedValue() to test whether
 * the current value has a named entry.  Out-of-list values still round-trip
 * cleanly through toString() and Enum::lookup() using their decimal form
 * ("Codec::100").
 *
 * @note The raw @ref Enum class is the low-level building block.  Any
 *       enum kind that crosses module boundaries or gets stored in a
 *       @ref Variant / @ref VariantDatabase / config file must use the
 *       @ref TypedEnum "TypedEnum<Self>" CRTP wrapper and live in
 *       @c include/promeki/enums.h — see @c CODING_STANDARDS.md for
 *       the rationale.  The @c Codec example below uses the bare
 *       @c struct @c { @c static @c inline @c const @c Enum @c X; @c }
 *       pattern only to illustrate the base-class API; for any new
 *       shared enum, copy one of the @ref TypedEnum "TypedEnum-derived" classes in
 *       @c enums.h instead.
 *
 * @par Example (base API — prefer TypedEnum for shared enums)
 * @code
 * struct Codec {
 *         static inline const Enum::Type Type = Enum::registerType("Codec",
 *                 {
 *                         { "H264", 1 },
 *                         { "H265", 2 },
 *                         { "VP9",  3 }
 *                 },
 *                 1);  // default value: H264
 *
 *         static inline const Enum H264{Type, 1};
 *         static inline const Enum H265{Type, 2};
 *         static inline const Enum VP9 {Type, 3};
 * };
 *
 * Enum c = Codec::H265;
 * c.valueName();   // "H265"
 * c.value();       // 2
 * c.typeName();    // "Codec"
 * c.toString();    // "Codec::H265"
 * c.hasListedValue();   // true
 *
 * Enum d(Codec::Type);          // default-valued (== Codec::H264)
 * Enum e(Codec::Type, "VP9");   // string -> int lookup
 * e.value();       // 3
 *
 * // Out-of-list value: still a valid Enum with a known type, just no name.
 * Enum x(Codec::Type, 100);
 * x.isValid();          // true  — type is known
 * x.hasListedValue();   // false — 100 is not in the value table
 * x.valueName();        // ""    — no registered name
 * x.toString();         // "Codec::100"
 *
 * Enum f = Enum::lookup("Codec::H264");  // "TypeName::ValueName" parse
 * Enum g = Enum::lookup("Codec::100");   // "TypeName::<int>" parse (out-of-list)
 *
 * for(const Enum::Value &v : Enum::values(Codec::Type)) {
 *         // v.first() == name, v.second() == int
 * }
 * @endcode
 *
 * @see TypedEnum for the compile-time-typed wrapper used by all
 *      well-known enums in @c include/promeki/enums.h.
 */
class Enum {
        public:
                /// @brief Sentinel returned by valueOf()/value() when a lookup fails.
                static constexpr int InvalidValue = -1;

                /**
                 * @brief One (name, integer) entry in an enum's value table.
                 *
                 * A lightweight structural type suitable for placement in
                 * rodata via a `static constexpr` array — see
                 * @ref PROMEKI_REGISTER_ENUM_TYPE for the ergonomic wrapper
                 * used by well-known enums in @c include/promeki/enums.h.
                 */
                struct Entry {
                                const char *name;
                                int         value;
                };

                /**
                 * @brief Immutable per-type record stored in the global registry.
                 *
                 * Definitions live for the lifetime of the process and are
                 * referenced by direct const pointer from @ref Enum and
                 * @ref Type — every accessor below is therefore lock-free.
                 *
                 * Two storage models share the same struct layout so the rest
                 * of the class does not care which one it is looking at:
                 *
                 * - *Static* (the common case): declared via
                 *   @ref PROMEKI_REGISTER_ENUM_TYPE.  Both the Definition and
                 *   its Entry[] live in `.data` / `.rodata` — no heap
                 *   allocation, no Map, no String copy.  The caller keeps
                 *   ownership of the memory.
                 *
                 * - *Dynamic*: allocated on the heap by
                 *   @ref Enum::registerType for truly dynamic cases
                 *   (V4L2 menu controls, runtime-computed tables, …).
                 *   Ownership transfers to the registry and is never freed.
                 *
                 * The data members are public so the struct is a structural
                 * type, but treat them as read-only; the registry is the only
                 * writer, and it only writes @ref typeIndex once at
                 * registration time.
                 */
                struct Definition {
                                const char      *name = nullptr;              ///< Human-readable type name.
                                const Entry     *entries = nullptr;           ///< Pointer to the Entry table.
                                size_t           entryCount = 0;              ///< Number of entries in the table.
                                int              defaultValue = InvalidValue; ///< Default integer value.
                                mutable uint32_t typeIndex = 0;               ///< Monotonic id; set by the registry.

                                /**
                         * @brief Literal-backed @ref String cache for the type name.
                         *
                         * Populated by the registry at registration time.  Accessors
                         * that return the type name as a @ref String wrap this record
                         * via @ref String::fromLiteralData so no bytes are copied —
                         * the same @c const @c char* that lives in rodata (static
                         * path) or in the registry's leaked heap block (dynamic path)
                         * is reused on every call.
                         */
                                mutable StringLiteralData *nameLit = nullptr;

                                /**
                         * @brief Parallel array of literal-backed @ref String caches,
                         *        one per entry, with @c entryCount elements.
                         *
                         * Populated by the registry; same zero-copy story as
                         * @ref nameLit.  Indexed in lock-step with @ref entries.
                         */
                                mutable StringLiteralData *entryNameLits = nullptr;

                                /**
                         * @brief Parallel array of fully-qualified literal-backed
                         *        @ref String caches — `"TypeName::EntryName"`,
                         *        one per entry, with @c entryCount elements,
                         *        indexed in lock-step with @ref entries and
                         *        @ref entryNameLits.
                         *
                         * Populated by the registry at the same time as
                         * @ref entryNameLits.  Both parallel arrays share the
                         * same underlying @c "Type::Entry\0" buffer per entry —
                         * @ref entryNameLits points at offset @c typeNameLen+2
                         * into it — so the per-entry storage is one heap block.
                         *
                         * Used by @ref Enum::toString so the in-list case never
                         * has to concatenate; two Enums with the same value
                         * return Strings backed by the same immortal storage,
                         * so @ref String equality short-circuits on pointer
                         * match.
                         */
                                mutable StringLiteralData *entryQualifiedLits = nullptr;

                                /// @brief Linear-search lookup of an entry by name.
                                const Entry *findByName(const String &n) const;
                                /// @brief Linear-search lookup of an entry by integer value.
                                const Entry *findByValue(int v) const;
                };

                /**
                 * @brief Lightweight handle identifying a registered enum kind.
                 *
                 * A Type wraps a pointer to the per-type Definition.  It is
                 * cheap to copy, compares by identity, and is valid only for
                 * types returned from registerType() or findType().
                 */
                class Type {
                        public:
                                /// @brief Default-constructs an invalid Type (no associated definition).
                                Type() = default;

                                /// @brief Returns true if this Type refers to a registered enum kind.
                                bool isValid() const { return _def != nullptr; }

                                /// @brief Returns the string name this type was registered with.
                                String name() const;

                                /**
                                 * @brief Returns a stable monotonic integer identifier.
                                 *
                                 * Each registered type is assigned a sequential id in
                                 * registration order.  The id is only meaningful within
                                 * the current process.
                                 *
                                 * @return The id, or UINT32_MAX if the Type is invalid.
                                 */
                                uint32_t id() const;

                                /**
                                 * @brief Returns the underlying Definition pointer.
                                 *
                                 * Intended for advanced users; ordinary code should go
                                 * through the static Enum accessors.
                                 *
                                 * @return The Definition, or nullptr if the Type is invalid.
                                 */
                                const Definition *definition() const { return _def; }

                                /// @brief Equality by definition identity.
                                bool operator==(const Type &o) const { return _def == o._def; }
                                bool operator!=(const Type &o) const { return _def != o._def; }

                                /// @brief Pointer ordering, useful for Map/Set keys.
                                bool operator<(const Type &o) const { return _def < o._def; }

                        private:
                                friend class Enum;
                                explicit Type(const Definition *def) : _def(def) {}
                                const Definition *_def = nullptr;
                };

                /// @brief One (name, integer) pair used by the runtime
                ///        @ref registerType overload.
                using Value = Pair<String, int>;

                /// @brief Ordered list of (name, integer) entries.
                using ValueList = ::promeki::List<Value>;

                /// @brief List of registered enum type names.
                using TypeList = StringList;

                /**
                 * @brief Registers a Definition that already lives in static
                 *        storage (the preferred, zero-heap path).
                 *
                 * Used by @ref PROMEKI_REGISTER_ENUM_TYPE.  The @p def pointer
                 * must remain valid for the process lifetime (typically
                 * `static inline`).  The registry does not take ownership —
                 * it only records the pointer and stamps
                 * @ref Definition::typeIndex.
                 *
                 * Asserts when another definition with the same name is
                 * already registered.  Also validates that the entry table
                 * is non-empty, contains no duplicate names or values, and
                 * that the default value is present.
                 *
                 * @param def  Static-storage Definition to register.
                 * @return The stable @ref Type handle.
                 */
                static Type registerDefinition(Definition *def);

                /**
                 * @brief Registers a new enum type with a runtime-computed value table.
                 *
                 * Used when the table cannot be known at compile time (for
                 * example V4L2 menu controls built from the device's advertised
                 * menu items).  Heap-allocates a @ref Definition plus copies of
                 * its name and entry array; ownership transfers to the registry
                 * and is never freed.
                 *
                 * Intended for use at static-init time for well-known enums,
                 * and at open-time for dynamic enums.  Re-registering a
                 * type name that has already been registered triggers an
                 * assertion (each enum type must be registered exactly once).
                 *
                 * @p defaultValue must appear in @p values; it is the integer
                 * returned by Enum(Type) and defaultValue(Type).  If it does
                 * not appear, an assertion is triggered.
                 *
                 * For the well-known library enums prefer
                 * @ref PROMEKI_REGISTER_ENUM_TYPE which produces the same
                 * result with the table pinned to rodata.
                 *
                 * @param typeName     Human-readable name of the enum kind.
                 * @param values       Complete set of (name, int) pairs.  Must
                 *                     be non-empty and contain no duplicate
                 *                     names or integer values.
                 * @param defaultValue Default integer for this type.  Must
                 *                     match one of the entries in @p values.
                 * @return The registered Type handle.
                 */
                static Type registerType(const String &typeName, const ValueList &values, int defaultValue);

                /**
                 * @brief Looks up an enum type by name without registering anything.
                 * @param typeName  Name passed to a previous registerType() call.
                 * @return The Type if known, or an invalid Type otherwise.
                 */
                static Type findType(const String &typeName);

                /// @brief Returns the names of all registered enum types, in registration order.
                static TypeList registeredTypes();

                /**
                 * @brief Returns the (name, integer) pairs registered for @p type.
                 *
                 * Entries are returned in registration order.
                 *
                 * @param type  Registered enum type.
                 * @return The value table, or an empty list if @p type is invalid.
                 */
                static ValueList values(Type type);

                /**
                 * @brief Returns the default integer value registered for @p type.
                 * @param type  Registered enum type.
                 * @return The default value, or InvalidValue if @p type is invalid.
                 */
                static int defaultValue(Type type);

                /**
                 * @brief Looks up the integer value associated with @p name in @p type.
                 *
                 * Returns a @ref Result so the failure case (unknown type or
                 * unregistered name) is unambiguous, since a registered value
                 * can legitimately be any int — including @c -1.
                 *
                 * @param type  Registered enum type.
                 * @param name  Value name to look up.
                 * @return A Result holding the integer value on success, or
                 *         @c Error::IdNotFound when @p type is invalid or
                 *         @p name is not registered.
                 */
                static Result<int> valueOf(Type type, const String &name);

                /**
                 * @brief Looks up the name associated with @p value in @p type.
                 * @param type   Registered enum type.
                 * @param value  Integer value to look up.
                 * @param err    Optional error pointer; set to Error::IdNotFound on miss.
                 * @return The name, or an empty String if @p value is not registered.
                 */
                static String nameOf(Type type, int value, Error *err = nullptr);

                /**
                 * @brief Parses a "TypeName::ValueName" string into an Enum.
                 *
                 * This is the inverse of toString() and is used by Variant
                 * when converting a String back into an Enum.  The value
                 * segment is first looked up as a registered name; if that
                 * fails, it is parsed as a signed decimal integer so that
                 * out-of-list values produced by toString() ("Codec::100")
                 * round-trip cleanly.
                 *
                 * @param text  A string of the form "TypeName::ValueName" or
                 *              "TypeName::<integer>".
                 * @param err   Optional error pointer; set to Error::InvalidArgument
                 *              if @p text is malformed, or Error::IdNotFound if
                 *              the type is unknown or the value segment is
                 *              neither a registered name nor a valid integer.
                 * @return The parsed Enum, or an invalid Enum on failure.
                 */
                static Enum lookup(const String &text, Error *err = nullptr);

                /// @brief Default-constructs an invalid Enum (no type, no value).
                Enum() = default;

                /**
                 * @brief Constructs an Enum holding @p type's default value.
                 *
                 * Equivalent to @c Enum(type, Enum::defaultValue(type)).  If
                 * @p type is invalid the result is invalid.
                 *
                 * @param type  Registered enum type.
                 */
                explicit Enum(Type type);

                /**
                 * @brief Constructs an Enum from a type and integer value.
                 *
                 * The value is not validated against the type's table; isValid()
                 * will return false if @p value is not registered for @p type.
                 *
                 * @param type   Registered enum type.
                 * @param value  Integer value belonging to that type.
                 */
                Enum(Type type, int value) : _def(type._def), _value(value) {}

                /**
                 * @brief Constructs an Enum from a type and a value name.
                 *
                 * If @p name is not registered for @p type, the resulting Enum
                 * holds InvalidValue and isValid() returns false.
                 *
                 * @param type  Registered enum type.
                 * @param name  Value name to look up.
                 */
                Enum(Type type, const String &name);

                /// @brief Returns the enum type handle.
                Type type() const { return Type(_def); }

                /// @brief Returns the integer value of this enum.
                int value() const { return _value; }

                /**
                 * @brief Returns the registered name for this enum's value.
                 * @return The value name, or an empty String if the type is
                 *         invalid or the value is not in the type's table
                 *         (out-of-list value).  Use hasListedValue() to
                 *         disambiguate an empty name from a genuine miss.
                 */
                String valueName() const;

                /// @brief Returns the name of the enum type, or empty if the type is invalid.
                String typeName() const;

                /**
                 * @brief Returns the string form "TypeName::ValueName".
                 *
                 * If the value has a registered name, returns
                 * "TypeName::ValueName".  If the value is out-of-list, the
                 * value segment is the signed decimal form of the integer
                 * ("Codec::100").  If the type is invalid, returns "::".
                 */
                String toString() const;

                /**
                 * @brief Returns true if this Enum refers to a registered type.
                 *
                 * An Enum is considered valid as long as its type is known,
                 * regardless of whether the integer value has an entry in the
                 * type's value table.  To additionally require that the value
                 * has a registered name, use hasListedValue().
                 *
                 * A default-constructed Enum returns false.
                 */
                bool isValid() const;

                /**
                 * @brief Returns true if the current value is present in the type's value table.
                 *
                 * Equivalent to asking "does valueName() return a non-empty
                 * string?".  Returns false for default-constructed Enums and
                 * for Enums whose integer is outside the registered set.
                 */
                bool hasListedValue() const;

                /// @brief Equality: same type (by identity) and same integer value.
                bool operator==(const Enum &o) const { return _def == o._def && _value == o._value; }
                bool operator!=(const Enum &o) const { return !(*this == o); }

        private:
                const Definition *_def = nullptr;        ///< Direct pointer to the registered Definition.
                int               _value = InvalidValue; ///< Integer value within that type.
};

/**
 * @brief CRTP base pinning an @ref Enum value to a compile-time type.
 * @ingroup util
 *
 * A function that takes a runtime @ref Enum can receive any enum
 * value of any registered type — the kind of thing the caller meant
 * is only known at runtime.  @ref TypedEnum is a thin CRTP wrapper
 * that inherits publicly from @ref Enum and pins a derived class to
 * a specific registered enum type, so functions can take
 * `const Derived &` instead of `const Enum &` and gain compile-time
 * type checking at zero run-time cost.
 *
 * Public inheritance preserves full runtime compatibility: a
 * @ref TypedEnum value implicitly slices to @ref Enum, so it still
 * participates in @ref Variant, @ref VariantDatabase, and any API
 * that already accepts a plain @ref Enum.  The derived class adds
 * no data members of its own — the slice is safe by construction.
 *
 * The derived class is expected to expose a @c static @c inline
 * @c const @ref Enum::Type @c Type member, typically initialized
 * via @ref Enum::registerType at static-init time.  @ref TypedEnum
 * pulls that @c Type through @c Derived for every constructor.
 *
 * @par Usage
 * @code
 * class ByteCountStyle : public TypedEnum<ByteCountStyle> {
 * public:
 *     static inline const Enum::Type Type = Enum::registerType(
 *         "ByteCountStyle", { {"Metric",0}, {"Binary",1} }, 0);
 *
 *     using TypedEnum<ByteCountStyle>::TypedEnum;
 *
 *     static const ByteCountStyle Metric;
 *     static const ByteCountStyle Binary;
 * };
 *
 * inline const ByteCountStyle ByteCountStyle::Metric{0};
 * inline const ByteCountStyle ByteCountStyle::Binary{1};
 *
 * // Signature gains compile-time type checking:
 * String format(const ByteCountStyle &style);
 *
 * format(ByteCountStyle::Binary);  // OK
 * format(VideoPattern::ColorBars); // compile error — wrong type
 * @endcode
 *
 * @tparam Derived The concrete class inheriting from this template;
 *                 must expose a @c static @ref Enum::Type @c Type.
 */
template <typename Derived> class TypedEnum : public Enum {
        public:
                /**
                 * @brief Default-constructs with the registered default value.
                 *
                 * Equivalent to @c Enum(Derived::Type); the resulting
                 * value is valid and carries the @c defaultValue that
                 * was passed to @ref Enum::registerType.
                 */
                TypedEnum() : Enum(Derived::Type) {}

                /**
                 * @brief Constructs from a raw integer value.
                 *
                 * The integer is not validated against the type's
                 * value table; @ref Enum::hasListedValue reports
                 * whether it is a registered named entry.
                 */
                explicit TypedEnum(int value) : Enum(Derived::Type, value) {}

                /**
                 * @brief Constructs by looking up a value name in the type's table.
                 *
                 * If @p name is not registered for @c Derived::Type,
                 * the constructed value carries @ref Enum::InvalidValue
                 * and @ref Enum::hasListedValue returns false.
                 */
                explicit TypedEnum(const String &name) : Enum(Derived::Type, name) {}
};

namespace detail {

        /// @brief Null-terminated-string equality, usable in constant expressions.
        constexpr bool enumStrEqual(const char *a, const char *b) {
                while (*a != '\0' && *b != '\0') {
                        if (*a != *b) return false;
                        ++a;
                        ++b;
                }
                return *a == *b;
        }

        /// @brief Returns true if no two entries share a name or an integer value.
        consteval bool enumEntriesUnique(const Enum::Entry *entries, size_t count) {
                for (size_t i = 0; i < count; ++i) {
                        for (size_t j = i + 1; j < count; ++j) {
                                if (enumStrEqual(entries[i].name, entries[j].name)) return false;
                                if (entries[i].value == entries[j].value) return false;
                        }
                }
                return true;
        }

        /// @brief Returns true if @p def matches the value of some entry.
        consteval bool enumHasDefault(const Enum::Entry *entries, size_t count, int def) {
                for (size_t i = 0; i < count; ++i) {
                        if (entries[i].value == def) return true;
                }
                return false;
        }

} // namespace detail

PROMEKI_NAMESPACE_END

/**
 * @brief Declares an @ref promeki::Enum type with a table placed in rodata.
 * @ingroup util
 *
 * Expands inside a class body (typically one deriving from
 * @ref promeki::TypedEnum) to three declarations that together give the
 * type a zero-heap, compile-time-validated registration:
 *
 * 1. `static constexpr Enum::Entry _promeki_enum_entries_[]` — the
 *    (name, value) table, placed in rodata.
 * 2. `static inline Enum::Definition _promeki_enum_def_` — a single
 *    Definition struct pointing at the table.  The registry stamps
 *    its @c typeIndex at static-init time; nothing else is ever
 *    written.
 * 3. `static inline const Enum::Type Type` — the handle consumed by
 *    @ref promeki::TypedEnum's constructors and by user code.
 *
 * Three `static_assert`s verify at compile time that:
 *
 * - the entry table is non-empty,
 * - no two entries share a name or an integer value,
 * - the default value is present in the table.
 *
 * @param TypeName String literal — the human-readable type name.
 * @param Default  Integer default value, which must appear in the table.
 * @param ...      Brace-initialized `Enum::Entry` rows —
 *                 `{ "Name", integer }` comma-separated.
 *
 * @par Example
 * @code
 * class VideoPattern : public TypedEnum<VideoPattern> {
 *     public:
 *         PROMEKI_REGISTER_ENUM_TYPE("VideoPattern", 0,
 *                 { "ColorBars",   0 },
 *                 { "ColorBars75", 1 },
 *                 { "Ramp",        2 });
 *         using TypedEnum<VideoPattern>::TypedEnum;
 *         static const VideoPattern ColorBars;
 * };
 * @endcode
 */
#define PROMEKI_REGISTER_ENUM_TYPE(TypeName, Default, ...)                                                             \
        static constexpr ::promeki::Enum::Entry _promeki_enum_entries_[] = {__VA_ARGS__};                              \
        static_assert(sizeof(_promeki_enum_entries_) / sizeof(_promeki_enum_entries_[0]) > 0,                          \
                      "Enum type '" TypeName "' has no entries");                                                      \
        static_assert(::promeki::detail::enumEntriesUnique(_promeki_enum_entries_,                                     \
                                                           sizeof(_promeki_enum_entries_) /                            \
                                                                   sizeof(_promeki_enum_entries_[0])),                 \
                      "Enum type '" TypeName "' has duplicate name or value");                                         \
        static_assert(::promeki::detail::enumHasDefault(                                                               \
                              _promeki_enum_entries_,                                                                  \
                              sizeof(_promeki_enum_entries_) / sizeof(_promeki_enum_entries_[0]), (Default)),          \
                      "Enum type '" TypeName "' default value not in entry table");                                    \
        static inline ::promeki::Enum::Definition _promeki_enum_def_{.name = TypeName,                                 \
                                                                     .entries = _promeki_enum_entries_,                \
                                                                     .entryCount = sizeof(_promeki_enum_entries_) /    \
                                                                                   sizeof(_promeki_enum_entries_[0]),  \
                                                                     .defaultValue = (Default),                        \
                                                                     .typeIndex = 0};                                  \
        static inline const ::promeki::Enum::Type Type = ::promeki::Enum::registerDefinition(&_promeki_enum_def_)

PROMEKI_FORMAT_VIA_TOSTRING(promeki::Enum);
