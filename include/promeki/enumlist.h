/**
 * @file      enumlist.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/enum.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Ordered, duplicate-preserving list of @ref Enum values sharing one type.
 * @ingroup util
 *
 * An @c EnumList carries an @ref Enum::Type handle and a sequence of
 * integer values belonging to that type.  Every element must belong to
 * the list's declared element type — appending a value from a different
 * type is rejected at runtime — so the list is uniform by construction,
 * even though the element kind is chosen at runtime rather than at
 * compile time.
 *
 * The list preserves insertion order and accepts duplicates.  Use
 * @ref uniqueSorted to obtain a copy with duplicates removed and the
 * remaining elements ordered by their registered integer value — that
 * form is useful as a normalized representation for equality checks or
 * for driving a per-element decision loop that only needs to visit each
 * kind once.
 *
 * String round-trip is a comma-separated value name list:
 * @code
 * EnumList list(AudioPattern::Type);
 * list.append(AudioPattern::Tone);
 * list.append(AudioPattern::Silence);
 * list.append(AudioPattern::Tone);
 * String s = list.toString();          // "Tone,Silence,Tone"
 *
 * Error err;
 * EnumList parsed = EnumList::fromString(AudioPattern::Type,
 *                                        "Tone,Silence", &err);
 * @endcode
 *
 * @note Enum value names used by libpromeki are expected to be
 *       C-identifier safe (A-Z, a-z, 0-9, underscore; not starting
 *       with a digit) so comma-splitting on a string representation
 *       never has to worry about quoting.
 *
 * @c EnumList is @ref Variant compatible — it can be stored in a
 * @ref Variant, looked up in a @ref VariantDatabase, and used as a
 * @ref VariantSpec value type.
 */
class EnumList {
        public:
                /** @brief Default-constructs an invalid EnumList (no element type bound). */
                EnumList() = default;

                /**
                 * @brief Constructs an empty EnumList bound to @p type.
                 * @param type The Enum::Type every element must belong to.
                 */
                explicit EnumList(Enum::Type type) : _type(type) {}

                /**
                 * @brief Convenience constructor from a TypedEnum class.
                 *
                 * Equivalent to `EnumList(T::Type)`.  Lets call sites that
                 * know the element kind at compile time write
                 * `EnumList::forType<AudioPattern>()` without repeating
                 * the `AudioPattern::Type` handle.
                 *
                 * @tparam T A class deriving from @ref TypedEnum with a
                 *           `static const Enum::Type Type` member.
                 * @return A fresh, empty list bound to @c T::Type.
                 */
                template <typename T>
                static EnumList forType() {
                        return EnumList(T::Type);
                }

                // --------------------------------------------------------------
                // State
                // --------------------------------------------------------------

                /** @brief Returns the bound element type. */
                Enum::Type elementType() const { return _type; }

                /** @brief Returns true when an element type has been bound. */
                bool isValid() const { return _type.isValid(); }

                /** @brief Returns the number of elements. */
                size_t size() const { return _values.size(); }

                /** @brief Returns true when the list has no elements. */
                bool isEmpty() const { return _values.isEmpty(); }

                /** @brief Removes every element, keeping the bound element type. */
                void clear() { _values.clear(); }

                // --------------------------------------------------------------
                // Append
                // --------------------------------------------------------------

                /**
                 * @brief Appends an Enum value.
                 *
                 * Fails when the list has no bound element type, or when
                 * @p e's type does not match the bound element type.
                 *
                 * @param e   The value to append.
                 * @param err Optional error output.
                 * @return @c true on success.
                 */
                bool append(const Enum &e, Error *err = nullptr);

                /**
                 * @brief Appends a raw integer value belonging to the bound element type.
                 * @param value Integer value; not validated against the type's
                 *              registered value table.
                 * @param err   Optional error output.
                 * @return @c true on success.
                 */
                bool append(int value, Error *err = nullptr);

                /**
                 * @brief Appends a value looked up by registered name.
                 * @param name Registered value name for the bound element type.
                 * @param err  Optional error output; @c Error::IdNotFound when
                 *             the name is not registered.
                 * @return @c true on success.
                 */
                bool append(const String &name, Error *err = nullptr);

                // --------------------------------------------------------------
                // Access
                // --------------------------------------------------------------

                /**
                 * @brief Returns the @p i'th element as an @ref Enum.
                 * @param i Zero-based index.
                 */
                Enum at(size_t i) const {
                        return Enum(_type, _values[i]);
                }

                /** @brief Subscript access; equivalent to @ref at. */
                Enum operator[](size_t i) const { return at(i); }

                /** @brief Returns the underlying list of integer values in insertion order. */
                const List<int> &values() const { return _values; }

                // --------------------------------------------------------------
                // Transformations
                // --------------------------------------------------------------

                /**
                 * @brief Returns a copy with duplicates removed and remaining
                 *        elements sorted by their integer value.
                 *
                 * Useful when the caller only cares about the *set* of kinds
                 * referenced by the list — e.g. to drive a per-kind decision
                 * loop that should visit each kind at most once.  The
                 * original list is unchanged.
                 */
                EnumList uniqueSorted() const;

                // --------------------------------------------------------------
                // String
                // --------------------------------------------------------------

                /**
                 * @brief Serializes the list as a comma-separated value name list.
                 *
                 * Each element is rendered via @ref Enum::valueName.  Elements
                 * whose integer value has no registered name are rendered as
                 * the decimal integer so the round-trip through
                 * @ref fromString still succeeds.
                 *
                 * @return The comma-joined string, or an empty string when the
                 *         list is empty.  An invalid (unbound) list returns
                 *         the empty string.
                 */
                String toString() const;

                /**
                 * @brief Parses a comma-separated value name list into an EnumList.
                 *
                 * Whitespace around each entry is trimmed.  An entry whose
                 * text is not a registered name is parsed as a decimal
                 * integer before failing.  Empty strings produce an empty
                 * (but valid) list bound to @p type.
                 *
                 * @param type Element type the list will be bound to.
                 * @param text Comma-separated input.
                 * @param err  Optional error output; @c Error::InvalidArgument
                 *             when @p type is invalid or an entry fails to
                 *             resolve.
                 * @return The parsed list, or an invalid (unbound) list on
                 *         total failure.
                 */
                static EnumList fromString(Enum::Type type, const String &text,
                                           Error *err = nullptr);

                // --------------------------------------------------------------
                // Equality
                // --------------------------------------------------------------

                /**
                 * @brief Equality: same element type and identical integer sequence.
                 */
                bool operator==(const EnumList &o) const {
                        return _type == o._type && _values == o._values;
                }
                bool operator!=(const EnumList &o) const { return !(*this == o); }

        private:
                Enum::Type _type;
                List<int>  _values;
};

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::EnumList);
