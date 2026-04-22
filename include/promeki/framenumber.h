/**
 * @file      framenumber.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

class FrameCount;

/**
 * @brief Absolute frame index along a media timeline.
 * @ingroup time
 *
 * Stores a single @c int64_t.  Non-negative values identify a specific
 * frame (zero-based).  The sentinel value @c -1 means "unknown" — for
 * example, a decoder that has not yet produced a frame, or metadata
 * that was never populated.  Any other negative storage value is
 * canonicalised to @c Unknown on construction.
 *
 * FrameNumber is a @ref simple "Simple data object": plain value,
 * no heap, no @c SharedPtr.  It carries no frame-rate context and
 * therefore cannot convert to @ref Timecode on its own — use
 * @ref Timecode::fromFrameNumber when a rate is known.
 *
 * @par Sentinel semantics
 * - Any arithmetic operation involving an @c Unknown operand yields
 *   @c Unknown (NaN-like poisoning).
 * - Two @c Unknown values compare equal; ordering uses the raw
 *   @c int64_t value, so @c Unknown (@c -1) sorts below every valid
 *   frame number.  This makes @c FrameNumber usable in sorted
 *   containers without surprises.
 *
 * @par Example
 * @code
 * FrameNumber n = 42;
 * assert(n.isValid());
 * n += 3;                     // 45
 *
 * FrameNumber u;              // default = Unknown
 * assert(u.isUnknown());
 *
 * FrameNumber x = n + u;      // poisons to Unknown
 * assert(x.isUnknown());
 *
 * String s = n.toString();    // "45"
 * Error err;
 * FrameNumber p = FrameNumber::fromString("  100 ", &err);
 * assert(err.isOk() && p.value() == 100);
 * @endcode
 */
class FrameNumber {
        public:
                /** @brief Storage sentinel meaning "unknown". */
                static constexpr int64_t UnknownValue = -1;

                /**
                 * @brief Returns an unknown FrameNumber.
                 * @return A FrameNumber in the @c Unknown state.
                 */
                static constexpr FrameNumber unknown() { return FrameNumber(); }

                /**
                 * @brief Parses a FrameNumber from a string.
                 *
                 * Accepted forms (leading/trailing whitespace ignored, case
                 * insensitive where relevant):
                 * - empty string → @c Unknown
                 * - @c "unknown" / @c "unk" / @c "?" → @c Unknown
                 * - decimal integer @c >= 0 → the corresponding frame
                 *
                 * @param str The string to parse.
                 * @param err Optional error output; set to @c Error::Ok on
                 *            success, @c Error::ParseFailed on malformed
                 *            input, or @c Error::OutOfRange if a parsed
                 *            integer is negative.
                 * @return The parsed FrameNumber, or @c Unknown on failure.
                 */
                static FrameNumber fromString(const String &str, Error *err = nullptr);

                /** @brief Default-constructs an @c Unknown FrameNumber. */
                constexpr FrameNumber() = default;

                /**
                 * @brief Constructs a FrameNumber from a raw @c int64_t.
                 *
                 * Any negative input is canonicalised to @c Unknown so
                 * that downstream callers only ever see the canonical
                 * sentinel value.
                 *
                 * @param v Raw frame index; @c >= 0 is valid,
                 *          any negative value becomes @c Unknown.
                 */
                constexpr FrameNumber(int64_t v) : _value(v < 0 ? UnknownValue : v) {}

                /** @brief Returns true if this FrameNumber has a concrete, non-sentinel value. */
                constexpr bool isValid() const { return _value >= 0; }

                /** @brief Returns true if this FrameNumber is in the @c Unknown state. */
                constexpr bool isUnknown() const { return _value < 0; }

                /** @brief Returns the raw storage value (@c -1 for @c Unknown, else the frame index). */
                constexpr int64_t value() const { return _value; }

                /** @brief Pre-increment.  Advances by one frame; @c Unknown stays @c Unknown. */
                FrameNumber &operator++() {
                        if(isValid()) ++_value;
                        return *this;
                }

                /** @brief Post-increment. */
                FrameNumber operator++(int) {
                        FrameNumber old = *this;
                        ++(*this);
                        return old;
                }

                /** @brief Pre-decrement.  Moves back one frame; @c Unknown stays @c Unknown; stepping below zero becomes @c Unknown. */
                FrameNumber &operator--() {
                        if(isValid()) {
                                if(_value == 0) _value = UnknownValue;
                                else --_value;
                        }
                        return *this;
                }

                /** @brief Post-decrement. */
                FrameNumber operator--(int) {
                        FrameNumber old = *this;
                        --(*this);
                        return old;
                }

                /** @brief In-place addition of an integer offset.  @c Unknown is preserved; negative results become @c Unknown. */
                FrameNumber &operator+=(int64_t n) {
                        if(isValid()) {
                                int64_t nv = _value + n;
                                _value = nv < 0 ? UnknownValue : nv;
                        }
                        return *this;
                }

                /** @brief In-place subtraction of an integer offset. */
                FrameNumber &operator-=(int64_t n) {
                        if(isValid()) {
                                int64_t nv = _value - n;
                                _value = nv < 0 ? UnknownValue : nv;
                        }
                        return *this;
                }

                /** @brief In-place addition of a FrameCount. */
                FrameNumber &operator+=(const FrameCount &c);

                /** @brief In-place subtraction of a FrameCount. */
                FrameNumber &operator-=(const FrameCount &c);

                /** @brief Equality — two @c Unknown values compare equal. */
                constexpr bool operator==(const FrameNumber &other) const { return _value == other._value; }

                /** @brief Inequality. */
                constexpr bool operator!=(const FrameNumber &other) const { return _value != other._value; }

                /** @brief Less-than — @c Unknown sorts below every valid FrameNumber. */
                constexpr bool operator<(const FrameNumber &other) const { return _value < other._value; }
                constexpr bool operator<=(const FrameNumber &other) const { return _value <= other._value; }
                constexpr bool operator>(const FrameNumber &other) const { return _value > other._value; }
                constexpr bool operator>=(const FrameNumber &other) const { return _value >= other._value; }

                /**
                 * @brief Returns the canonical string form.
                 * @return Empty string for @c Unknown, otherwise the
                 *         decimal frame index without any suffix.
                 */
                String toString() const;

                /** @brief Implicit conversion to String via @ref toString. */
                operator String() const { return toString(); }

        private:
                int64_t _value = UnknownValue;
};

/** @brief Returns @p a advanced by @p n frames. */
inline FrameNumber operator+(FrameNumber a, int64_t n) { a += n; return a; }
/** @brief Returns @p a moved back by @p n frames. */
inline FrameNumber operator-(FrameNumber a, int64_t n) { a -= n; return a; }
/** @brief Commutative int-FrameNumber addition. */
inline FrameNumber operator+(int64_t n, FrameNumber a) { a += n; return a; }

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::FrameNumber);

/**
 * @brief Hash specialization so @ref promeki::FrameNumber can serve as a
 *        key in @c HashMap / @c HashSet (or any @c std::unordered_*).
 *
 * Hashes the raw @c int64_t storage, which folds the @c Unknown
 * sentinel @c (-1) and any valid frame index uniformly through
 * @c std::hash<int64_t>.
 */
template <>
struct std::hash<promeki::FrameNumber> {
        size_t operator()(const promeki::FrameNumber &v) const noexcept {
                return std::hash<int64_t>()(v.value());
        }
};

