/**
 * @file      variantspec.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/variant.h>
#include <promeki/enum.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/textstream.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Describes the constraints and default value for a VariantDatabase ID.
 * @ingroup util
 *
 * VariantSpec captures the accepted type(s), default value, numeric
 * range, enum type, and human-readable description for a single
 * VariantDatabase key.  Every ID declared via
 * @ref VariantDatabase::declareID is associated with a VariantSpec,
 * enabling:
 *
 * - **Introspection** — callers can enumerate all keys a backend
 *   supports along with their types, ranges, and descriptions.
 * - **Validation** — @ref VariantDatabase::set can optionally warn
 *   or reject values that violate the spec.
 * - **Self-documenting defaults** — the spec carries the default
 *   value, eliminating the need for separate default-config
 *   factories.
 *
 * Specs are built using a fluent builder pattern:
 *
 * @par Example
 * @code
 * VariantSpec spec = VariantSpec()
 *     .setType(Variant::TypeS32)
 *     .setDefault(85)
 *     .setRange(1, 100)
 *     .setDescription("JPEG quality 1-100");
 *
 * bool ok = spec.validate(Variant(50));   // true
 * bool bad = spec.validate(Variant(200)); // false (out of range)
 * Variant def = spec;                     // Variant(85) via operator Variant()
 * @endcode
 *
 * Polymorphic specs accept multiple types:
 *
 * @par Example
 * @code
 * VariantSpec poly = VariantSpec()
 *     .setTypes({Variant::TypeString, Variant::TypeSdpSession})
 *     .setDescription("SDP input: file path or session object");
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance — including any combination of
 * the fluent setters with @c validate() / @c defaultValue() — must be
 * externally synchronized.  In typical usage a spec is built once at
 * registration time and read freely thereafter.
 */
class VariantSpec {
        public:
                /** @brief Convenience alias for the Variant type enumerator. */
                using Type = Variant::Type;

                /** @brief Convenience alias for a list of Variant type enumerators. */
                using TypeList = ::promeki::List<Type>;

                /** @brief Constructs an empty (invalid) spec with no type or default. */
                VariantSpec() = default;

                /**
                 * @brief Sets the single accepted Variant type.
                 * @param type The type this ID accepts.
                 * @return Reference to this spec for chaining.
                 */
                VariantSpec &setType(Type type) {
                        _types.clear();
                        _types.pushToBack(type);
                        return *this;
                }

                /**
                 * @brief Sets multiple accepted Variant types (polymorphic).
                 * @param types The types this ID accepts.
                 * @return Reference to this spec for chaining.
                 */
                VariantSpec &setTypes(std::initializer_list<Type> types) {
                        _types.clear();
                        for (auto t : types) _types.pushToBack(t);
                        return *this;
                }

                /**
                 * @brief Sets the default value for this ID.
                 * @param val The default value.
                 * @return Reference to this spec for chaining.
                 */
                VariantSpec &setDefault(const Variant &val) {
                        _default = val;
                        return *this;
                }

                /**
                 * @brief Sets a closed numeric range [min, max].
                 * @param min Minimum value (inclusive).
                 * @param max Maximum value (inclusive).
                 * @return Reference to this spec for chaining.
                 */
                VariantSpec &setRange(const Variant &min, const Variant &max) {
                        _min = min;
                        _max = max;
                        return *this;
                }

                /**
                 * @brief Sets the minimum allowed value (open-ended upper bound).
                 * @param min Minimum value (inclusive).
                 * @return Reference to this spec for chaining.
                 */
                VariantSpec &setMin(const Variant &min) {
                        _min = min;
                        return *this;
                }

                /**
                 * @brief Sets the maximum allowed value (open-ended lower bound).
                 * @param max Maximum value (inclusive).
                 * @return Reference to this spec for chaining.
                 */
                VariantSpec &setMax(const Variant &max) {
                        _max = max;
                        return *this;
                }

                /**
                 * @brief Sets the Enum::Type for IDs that hold Enum values.
                 *
                 * When set, validation checks that Enum values belong to
                 * this type.  The Enum::Type also enables introspection of
                 * the allowed value set via @ref Enum::values.
                 *
                 * @param enumType The expected Enum type.
                 * @return Reference to this spec for chaining.
                 */
                VariantSpec &setEnumType(Enum::Type enumType) {
                        _enumType = enumType;
                        return *this;
                }

                /**
                 * @brief Sets the human-readable description for this ID.
                 * @param desc The description text.
                 * @return Reference to this spec for chaining.
                 */
                VariantSpec &setDescription(const String &desc) {
                        _description = desc;
                        return *this;
                }

                /**
                 * @brief Returns the list of accepted Variant types.
                 * @return The type list.  Empty if no type constraint was set.
                 */
                const TypeList &types() const { return _types; }

                /**
                 * @brief Returns true if this spec accepts more than one type.
                 * @return True if polymorphic.
                 */
                bool isPolymorphic() const { return _types.size() > 1; }

                /**
                 * @brief Returns true if @p type is in the accepted type list.
                 *
                 * Returns true unconditionally if no type constraint was set.
                 *
                 * @param type The type to check.
                 * @return True if accepted.
                 */
                bool acceptsType(Type type) const;

                /**
                 * @brief Returns the default value.
                 * @return The default, or an invalid Variant if none was set.
                 */
                const Variant &defaultValue() const { return _default; }

                /**
                 * @brief Returns true if a minimum bound was set.
                 * @return True if the minimum is valid.
                 */
                bool hasMin() const { return _min.isValid(); }

                /**
                 * @brief Returns true if a maximum bound was set.
                 * @return True if the maximum is valid.
                 */
                bool hasMax() const { return _max.isValid(); }

                /**
                 * @brief Returns true if any range constraint was set.
                 * @return True if min or max is valid.
                 */
                bool hasRange() const { return _min.isValid() || _max.isValid(); }

                /**
                 * @brief Returns the minimum bound.
                 * @return The minimum, or an invalid Variant if not set.
                 */
                const Variant &rangeMin() const { return _min; }

                /**
                 * @brief Returns the maximum bound.
                 * @return The maximum, or an invalid Variant if not set.
                 */
                const Variant &rangeMax() const { return _max; }

                /**
                 * @brief Returns true if an Enum::Type was set.
                 * @return True if this spec constrains to a specific enum type.
                 */
                bool hasEnumType() const { return _enumType.isValid(); }

                /**
                 * @brief Returns the Enum::Type constraint.
                 * @return The enum type, or an invalid Type if not set.
                 */
                Enum::Type enumType() const { return _enumType; }

                /**
                 * @brief Returns the human-readable description.
                 * @return The description text, or an empty string if not set.
                 */
                const String &description() const { return _description; }

                /**
                 * @brief Returns true if this spec has at least one type or a default value.
                 * @return True if the spec carries useful information.
                 */
                bool isValid() const { return !_types.isEmpty() || _default.isValid(); }

                /**
                 * @brief Validates a value against this spec.
                 *
                 * Checks performed in order:
                 * 1. **Type** — if types() is non-empty, the value's type must
                 *    be in the list.
                 * 2. **Range** — if min/max are set and the value is numeric,
                 *    it must fall within bounds.
                 * 3. **Enum type** — if an Enum::Type is set and the value is
                 *    an Enum, its type must match.
                 *
                 * @param value The value to validate.
                 * @param err   Optional error output:
                 *              - @ref Error::InvalidArgument if the type check fails.
                 *              - @ref Error::OutOfRange if the range check fails.
                 * @return True if the value passes all checks.
                 */
                bool validate(const Variant &value, Error *err = nullptr) const;

                // ============================================================
                // Formatting
                // ============================================================

                /**
                 * @brief Returns a human-readable type name string.
                 *
                 * For single-type specs the result is a short label like
                 * @c "int", @c "bool", @c "String".  Enum types include
                 * the enum type name: @c "Enum VideoPattern".  Polymorphic
                 * specs join types with @c " | ".
                 *
                 * @return The type name, or @c "(any)" if no types are set.
                 */
                String typeName() const;

                /**
                 * @brief Returns a human-readable range string.
                 *
                 * @return E.g. @c "1 - 100", @c ">= 0", @c "<= 127",
                 *         or an empty string if no range is set.
                 */
                String rangeString() const;

                /**
                 * @brief Returns the default value formatted as a string.
                 *
                 * Enum defaults use the short value name (e.g. @c "ColorBars").
                 * Invalid defaults return @c "(none)".
                 *
                 * @return The formatted default value.
                 */
                String defaultString() const;

                // ============================================================
                // String parsing
                // ============================================================

                /**
                 * @brief Parses a CLI-style string into a Variant of the correct type.
                 *
                 * The spec's type list drives the parser — no template
                 * value is needed.  For Enum types the spec's enumType()
                 * resolves value names.  For polymorphic specs the types
                 * are tried in declaration order and the first successful
                 * parse wins.
                 *
                 * Supported types: bool (true/false/yes/no/1/0), all
                 * integer and float widths, String, Size2Du32, FrameRate,
                 * Rational, Timecode, DateTime, Color, PixelFormat,
                 * PixelMemLayout, ColorModel, Enum, StringList, and
                 * SocketAddress (when network is enabled).
                 *
                 * @param str The string to parse.
                 * @param err Optional error output.
                 * @return The parsed Variant, or an invalid Variant on failure.
                 */
                Variant parseString(const String &str, Error *err = nullptr) const;

                // ============================================================
                // Help output
                // ============================================================

                /**
                 * @brief Returns the "details" column string for help output.
                 *
                 * Concatenates the type name, the optional range, and the
                 * default value into the compact form
                 * `"(type) [range] [def: value]"` (range omitted when
                 * unset).  Callers render help output as three columns —
                 * name, details, description — and use this string for
                 * the middle column; @ref VariantDatabase::writeSpecMapHelp
                 * is the canonical caller.
                 *
                 * @return The details column string.
                 */
                String detailsString() const;

                // ============================================================
                // Conversion
                // ============================================================

                /**
                 * @brief Converts to the default Variant value.
                 *
                 * Allows a VariantSpec to be used anywhere a Variant is expected,
                 * yielding its default value.
                 *
                 * @return The default value.
                 */
                operator Variant() const { return _default; }

        private:
                TypeList   _types;
                Variant    _default;
                Variant    _min;
                Variant    _max;
                Enum::Type _enumType;
                String     _description;
};

PROMEKI_NAMESPACE_END
