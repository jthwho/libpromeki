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
 * @par Example
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
 */
class Enum {
        public:
                /// @brief Sentinel returned by valueOf()/value() when a lookup fails.
                static constexpr int InvalidValue = -1;

                /**
                 * @brief Opaque per-type record stored in the global registry.
                 *
                 * Definitions are allocated once at registerType() time and
                 * live for the lifetime of the process; pointers to them are
                 * stable and safe to hold without locking.  The type is only
                 * forward-declared in the public header — its contents are
                 * private to enum.cpp.
                 */
                class Definition;

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

                /// @brief One (name, integer) entry in an enum's value table.
                using Value = Pair<String, int>;

                /// @brief Ordered list of (name, integer) entries.
                using ValueList = List<Value>;

                /// @brief List of registered enum type names.
                using TypeList = StringList;

                /**
                 * @brief Registers a new enum type with its complete value table.
                 *
                 * Intended for use at static-init time.  The returned Type is
                 * stable for the lifetime of the process.  Re-registering a
                 * type name that has already been registered triggers an
                 * assertion (each enum type must be registered exactly once).
                 *
                 * @p defaultValue must appear in @p values; it is the integer
                 * returned by Enum(Type) and defaultValue(Type).  If it does
                 * not appear, an assertion is triggered.
                 *
                 * Callers may pass a brace-init-list directly:
                 * @code
                 * Enum::registerType("Codec",
                 *         { {"H264",1}, {"H265",2} },
                 *         1);
                 * @endcode
                 *
                 * @param typeName     Human-readable name of the enum kind.
                 * @param values       Complete set of (name, int) pairs.  Must
                 *                     be non-empty and contain no duplicate
                 *                     names or integer values.
                 * @param defaultValue Default integer for this type.  Must
                 *                     match one of the entries in @p values.
                 * @return The registered Type handle.
                 */
                static Type registerType(const String &typeName,
                                         const ValueList &values,
                                         int defaultValue);

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
                 * @param type  Registered enum type.
                 * @param name  Value name to look up.
                 * @param err   Optional error pointer; set to Error::IdNotFound on miss.
                 * @return The integer value, or InvalidValue (-1) if @p name is
                 *         not registered.  Note that -1 may itself be a
                 *         legitimate registered value; use @p err to
                 *         disambiguate.
                 */
                static int valueOf(Type type, const String &name, Error *err = nullptr);

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
                bool operator==(const Enum &o) const {
                        return _def == o._def && _value == o._value;
                }
                bool operator!=(const Enum &o) const { return !(*this == o); }

        private:
                const Definition *_def   = nullptr;           ///< Direct pointer to the registered Definition.
                int               _value = InvalidValue;      ///< Integer value within that type.
};

PROMEKI_NAMESPACE_END

