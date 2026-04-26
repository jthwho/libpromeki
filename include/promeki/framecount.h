/**
 * @file      framecount.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/framenumber.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Count of frames, optionally empty, unknown, or infinite.
 * @ingroup time
 *
 * Stores a single @c int64_t with four states encoded in the storage
 * value:
 * | State     | Storage value      | Meaning                              |
 * |-----------|--------------------|--------------------------------------|
 * | Unknown   | @c -1              | Count is not known (NaN-like).       |
 * | Infinity  | @c -2              | Unbounded / open-ended count.        |
 * | Empty     | @c 0               | Zero frames (a valid count).         |
 * | Valid     | @c >0              | Concrete positive frame count.       |
 *
 * FrameCount is a @ref simple "Simple data object": plain value,
 * no heap, no @c SharedPtr.  It pairs with @ref FrameNumber to form
 * @ref MediaDuration (a starting frame plus a length).
 *
 * @par Sentinel arithmetic
 * - Any operation involving @c Unknown yields @c Unknown (NaN-like
 *   poisoning).
 * - @c Infinity + finite = @c Infinity, @c Infinity + @c Infinity =
 *   @c Infinity, @c Infinity - @c Infinity = @c Unknown.
 * - Finite - @c Infinity = @c Unknown (no negative counts).
 * - Finite arithmetic that would produce a negative result also
 *   poisons to @c Unknown.
 *
 * @par Comparisons
 * - @c operator== compares storage values directly, so two @c Unknown
 *   counts compare equal and two @c Infinity counts compare equal.
 * - Ordering (@c <, @c <=, @c >, @c >=) returns @c false when either
 *   side is @c Unknown (NaN-like); among the remaining states @c Empty
 *   \< finite valid \< @c Infinity.
 *
 * @par String format
 * - @c Unknown → empty string
 * - @c Infinity → @c "inf"
 * - otherwise → decimal count followed by @c 'f' (e.g. @c "50f")
 *
 * @par Example
 * @code
 * FrameCount c(50);
 * assert(c.isValid() && c.isFinite());
 * assert(c.toString() == "50f");
 *
 * FrameCount inf = FrameCount::infinity();
 * assert(inf.isInfinite());
 * FrameCount u   = FrameCount::unknown();
 * assert(u.isUnknown());
 *
 * FrameCount sum = c + inf;   // Infinity
 * FrameCount bad = inf - inf; // Unknown
 * @endcode
 */
class FrameCount {
        public:
                /** @brief Storage sentinel meaning "unknown". */
                static constexpr int64_t UnknownValue = -1;
                /** @brief Storage sentinel meaning "infinity". */
                static constexpr int64_t InfinityValue = -2;

                /** @brief Returns an unknown FrameCount. */
                static constexpr FrameCount unknown() {
                        FrameCount c;
                        c._value = UnknownValue;
                        return c;
                }
                /** @brief Returns an infinite FrameCount. */
                static constexpr FrameCount infinity() {
                        FrameCount c;
                        c._value = InfinityValue;
                        return c;
                }
                /** @brief Returns an empty (zero-length) FrameCount. */
                static constexpr FrameCount empty() {
                        FrameCount c;
                        c._value = 0;
                        return c;
                }

                /**
                 * @brief Parses a FrameCount from a string.
                 *
                 * Accepted forms (leading/trailing whitespace ignored, case
                 * insensitive where relevant):
                 * - empty string → @c Unknown
                 * - @c "unknown" / @c "unk" / @c "?" → @c Unknown
                 * - @c "inf" / @c "infinity" / @c "∞" → @c Infinity
                 * - decimal integer @c >= 0, with an optional trailing
                 *   @c 'f' (e.g. @c "0", @c "0f", @c "50", @c "50f")
                 *
                 * @param str The string to parse.
                 * @param err Optional error output; set to @c Error::Ok on
                 *            success, @c Error::ParseFailed on malformed
                 *            input, or @c Error::OutOfRange if a parsed
                 *            integer is negative.
                 * @return The parsed FrameCount, or @c Unknown on failure.
                 */
                static FrameCount fromString(const String &str, Error *err = nullptr);

                /** @brief Default-constructs an @c Unknown FrameCount. */
                constexpr FrameCount() = default;

                /**
                 * @brief Constructs a FrameCount from a raw @c int64_t.
                 *
                 * Non-negative inputs are stored as-is (@c 0 is @c Empty,
                 * @c >0 is a valid count).  Any negative input is
                 * canonicalised to @c Unknown.
                 *
                 * @param v The raw count; @c >= 0 valid,
                 *          any negative value becomes @c Unknown.
                 */
                constexpr FrameCount(int64_t v) : _value(v < 0 ? UnknownValue : v) {}

                /** @brief Returns true if this count is not @c Unknown (i.e. it is @c Empty, finite, or @c Infinity). */
                constexpr bool isValid() const { return _value != UnknownValue; }

                /** @brief Returns true if this count is @c Unknown. */
                constexpr bool isUnknown() const { return _value == UnknownValue; }

                /** @brief Returns true if this count is @c Infinity. */
                constexpr bool isInfinite() const { return _value == InfinityValue; }

                /** @brief Returns true if this count is a finite, non-sentinel value (including @c 0). */
                constexpr bool isFinite() const { return _value >= 0; }

                /** @brief Returns true if this count is exactly @c 0 (and known/finite). */
                constexpr bool isEmpty() const { return _value == 0; }

                /** @brief Returns the raw storage value (includes sentinel values). */
                constexpr int64_t value() const { return _value; }

                /** @brief In-place addition of an integer.  Unknown poisons; Infinity is absorbing; negative results poison to Unknown. */
                FrameCount &operator+=(int64_t n);

                /** @brief In-place subtraction of an integer. */
                FrameCount &operator-=(int64_t n);

                /** @brief In-place addition of another FrameCount. */
                FrameCount &operator+=(const FrameCount &other);

                /** @brief In-place subtraction of another FrameCount. */
                FrameCount &operator-=(const FrameCount &other);

                /** @brief Pre-increment.  Unknown/Infinity are preserved. */
                FrameCount &operator++() {
                        *this += 1;
                        return *this;
                }
                /** @brief Post-increment. */
                FrameCount operator++(int) {
                        FrameCount o = *this;
                        *this += 1;
                        return o;
                }
                /** @brief Pre-decrement. */
                FrameCount &operator--() {
                        *this -= 1;
                        return *this;
                }
                /** @brief Post-decrement. */
                FrameCount operator--(int) {
                        FrameCount o = *this;
                        *this -= 1;
                        return o;
                }

                /** @brief Equality — compares raw storage, so two @c Unknown or two @c Infinity counts compare equal. */
                constexpr bool operator==(const FrameCount &other) const { return _value == other._value; }
                /** @brief Inequality. */
                constexpr bool operator!=(const FrameCount &other) const { return _value != other._value; }

                /**
                 * @brief Less-than ordering.
                 *
                 * Returns @c false if either operand is @c Unknown
                 * (NaN-like).  Otherwise @c Empty (0) \< any finite
                 * positive count \< @c Infinity.
                 */
                bool operator<(const FrameCount &other) const;
                /** @brief Greater-than ordering.  See @ref operator<. */
                bool operator>(const FrameCount &other) const { return other < *this; }
                /**
                 * @brief Less-than-or-equal ordering (NaN-like).
                 *
                 * Returns @c false if either operand is @c Unknown
                 * — so @c Unknown @c <= @c Unknown is @c false even
                 * though @c Unknown @c == @c Unknown is @c true.  This
                 * preserves the NaN-like invariant that no ordering
                 * comparison against @c Unknown ever yields @c true.
                 */
                bool operator<=(const FrameCount &other) const {
                        if (isUnknown() || other.isUnknown()) return false;
                        return !(other < *this);
                }
                /** @brief Greater-than-or-equal ordering (NaN-like).  See @ref operator<=. */
                bool operator>=(const FrameCount &other) const {
                        if (isUnknown() || other.isUnknown()) return false;
                        return !(*this < other);
                }

                /**
                 * @brief Returns the canonical string form.
                 * @return Empty string for @c Unknown, @c "inf" for
                 *         @c Infinity, otherwise the decimal count with
                 *         a trailing @c 'f' (e.g. @c "50f").
                 */
                String toString() const;

                /** @brief Implicit conversion to String via @ref toString. */
                operator String() const { return toString(); }

        private:
                int64_t _value = UnknownValue;
};

/**
 * @brief Reinterprets a FrameCount as a FrameNumber.
 *
 * This is the explicit "use a count as an index" conversion — typically
 * because a counter (e.g. "I've written N frames") doubles as the index
 * of the next frame.  @c Unknown / @c Infinity FrameCounts return an
 * @c Unknown FrameNumber.
 *
 * @param c The FrameCount to convert.
 * @return @c FrameNumber(c.value()) for finite counts, otherwise @c Unknown.
 */
inline FrameNumber toFrameNumber(const FrameCount &c) {
        return c.isFinite() ? FrameNumber(c.value()) : FrameNumber::unknown();
}

/**
 * @brief Reinterprets a FrameNumber as a FrameCount.
 *
 * This is the explicit "treat this index as a count" conversion — useful
 * when "the index of the next frame" doubles as "the number of frames
 * written so far".  An @c Unknown FrameNumber returns an @c Unknown
 * FrameCount.
 *
 * @param n The FrameNumber to convert.
 * @return @c FrameCount(n.value()) for valid frame numbers, otherwise @c Unknown.
 */
inline FrameCount toFrameCount(const FrameNumber &n) {
        return n.isValid() ? FrameCount(n.value()) : FrameCount::unknown();
}

/** @brief Returns @p a + @p b (sentinel-aware, see @ref FrameCount). */
inline FrameCount operator+(FrameCount a, const FrameCount &b) {
        a += b;
        return a;
}
/** @brief Returns @p a - @p b. */
inline FrameCount operator-(FrameCount a, const FrameCount &b) {
        a -= b;
        return a;
}
/** @brief Returns @p a + @p n. */
inline FrameCount operator+(FrameCount a, int64_t n) {
        a += n;
        return a;
}
/** @brief Returns @p a - @p n. */
inline FrameCount operator-(FrameCount a, int64_t n) {
        a -= n;
        return a;
}
/** @brief Commutative int-FrameCount addition. */
inline FrameCount operator+(int64_t n, FrameCount a) {
        a += n;
        return a;
}

// ----------------------------------------------------------------------
// FrameNumber / FrameCount cross-type arithmetic.
//
// Defined here because framenumber.h cannot see the full FrameCount
// definition (it forward-declares FrameCount), but framecount.h knows
// both types.  Inline so the compiler can still optimise them away.
// ----------------------------------------------------------------------

/** @brief Returns @p a advanced by @p c frames.  Any @c Unknown poisons; @c Infinity poisons FrameNumber (no unbounded frame index). */
inline FrameNumber operator+(FrameNumber a, const FrameCount &c) {
        if (a.isUnknown() || c.isUnknown() || c.isInfinite()) return FrameNumber::unknown();
        return FrameNumber(a.value() + c.value());
}

/** @brief Returns @p a moved back by @p c frames.  Negative results poison to @c Unknown. */
inline FrameNumber operator-(FrameNumber a, const FrameCount &c) {
        if (a.isUnknown() || c.isUnknown() || c.isInfinite()) return FrameNumber::unknown();
        int64_t nv = a.value() - c.value();
        return nv < 0 ? FrameNumber::unknown() : FrameNumber(nv);
}

/**
 * @brief Returns the distance (in frames) from @p b to @p a, i.e. @c a - @c b as a FrameCount.
 *
 * If either operand is @c Unknown, or the result would be negative
 * (@c a precedes @c b), the result is @c FrameCount::unknown().
 */
inline FrameCount operator-(const FrameNumber &a, const FrameNumber &b) {
        if (a.isUnknown() || b.isUnknown()) return FrameCount::unknown();
        int64_t d = a.value() - b.value();
        return d < 0 ? FrameCount::unknown() : FrameCount(d);
}

inline FrameNumber &FrameNumber::operator+=(const FrameCount &c) {
        *this = *this + c;
        return *this;
}
inline FrameNumber &FrameNumber::operator-=(const FrameCount &c) {
        *this = *this - c;
        return *this;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::FrameCount);

/**
 * @brief Hash specialization so @ref promeki::FrameCount can serve as a
 *        key in @c HashMap / @c HashSet (or any @c std::unordered_*).
 *
 * Hashes the raw @c int64_t storage, which gives every state
 * (Unknown @c -1, Infinity @c -2, Empty @c 0, finite @c >0) a
 * distinct hash through @c std::hash<int64_t>.
 */
template <> struct std::hash<promeki::FrameCount> {
                size_t operator()(const promeki::FrameCount &v) const noexcept {
                        return std::hash<int64_t>()(v.value());
                }
};
