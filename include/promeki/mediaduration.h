/**
 * @file      mediaduration.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <iterator>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/result.h>
#include <promeki/framenumber.h>
#include <promeki/framecount.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief A run of media frames described by a start frame plus a length.
 * @ingroup time
 *
 * MediaDuration is the definitive storage for a "span of frames": it
 * owns a @ref FrameNumber @c start and a @ref FrameCount @c length.
 * The equivalent inclusive range @c [start .. start+length-1] is
 * exposed via @ref toFrameRange and @ref FrameRange.
 *
 * MediaDuration is a @ref simple "Simple data object": plain value,
 * no heap, no @c SharedPtr.  Both fields may independently carry
 * sentinel states (@c Unknown on either side, @c Infinity on the
 * length side).
 *
 * @par String format
 * - @c "<start>+<length>" — canonical form, e.g. @c "0+50f"
 * - @c "<start>-<end>"    — range form (inclusive), e.g. @c "0-9"
 *   parses to @c MediaDuration(0, 10)
 * - empty string — both fields @c Unknown
 * Whitespace around operators is ignored; parsing is case-insensitive
 * where the sub-types accept words such as @c "inf" or @c "unknown".
 *
 * @par Example
 * @code
 * MediaDuration d(FrameNumber(0), FrameCount(50));
 * assert(d.toString() == "0+50f");
 *
 * Error err;
 * MediaDuration r = MediaDuration::fromString("10 - 19", &err);
 * assert(err.isOk() && r.start().value() == 10 && r.length().value() == 10);
 *
 * auto [range, rangeErr] = d.toFrameRange();
 * assert(rangeErr.isOk() && range.end.value() == 49);
 * @endcode
 */
class MediaDuration {
        public:
                /**
                 * @brief Inclusive [start, end] frame range.
                 *
                 * Two @ref FrameNumber values.  Both bounds are inclusive,
                 * so @c {0, 9} covers ten frames.  A range is only
                 * considered valid when both endpoints are valid and
                 * @c end @c >= @c start.
                 */
                struct FrameRange {
                                FrameNumber start; ///< First frame of the range (inclusive).
                                FrameNumber end;   ///< Last frame of the range (inclusive).

                                /**
                         * @brief Forward iterator over the frames in a FrameRange.
                         *
                         * Yields @ref FrameNumber values from @c start up to and
                         * including @c end.  Used to enable range-for syntax:
                         * @code
                         *   for(FrameNumber n : range) { ... }
                         * @endcode
                         */
                                class Iterator {
                                        public:
                                                using iterator_category = std::forward_iterator_tag;
                                                using value_type = FrameNumber;
                                                using difference_type = std::ptrdiff_t;
                                                using pointer = const FrameNumber *;
                                                using reference = const FrameNumber &;

                                                Iterator() = default;
                                                explicit Iterator(const FrameNumber &v) : _cur(v) {}

                                                reference operator*() const { return _cur; }
                                                pointer   operator->() const { return &_cur; }
                                                Iterator &operator++() {
                                                        ++_cur;
                                                        return *this;
                                                }
                                                Iterator operator++(int) {
                                                        Iterator o = *this;
                                                        ++_cur;
                                                        return o;
                                                }
                                                bool operator==(const Iterator &other) const {
                                                        return _cur == other._cur;
                                                }
                                                bool operator!=(const Iterator &other) const {
                                                        return _cur != other._cur;
                                                }

                                        private:
                                                FrameNumber _cur;
                                };

                                /** @brief Constructs an invalid range (both endpoints @c Unknown). */
                                FrameRange() = default;

                                /** @brief Constructs a range from two frame numbers.
                         *  @param s First frame (inclusive).
                         *  @param e Last  frame (inclusive). */
                                FrameRange(const FrameNumber &s, const FrameNumber &e) : start(s), end(e) {}

                                /** @brief Returns true if both endpoints are valid and @c end @c >= @c start. */
                                bool isValid() const {
                                        return start.isValid() && end.isValid() && end.value() >= start.value();
                                }

                                /**
                         * @brief Returns the number of frames covered.
                         *
                         * Inclusive:  @c end - @c start + 1.
                         * @return @ref FrameCount, or @c Unknown when the range is invalid.
                         */
                                FrameCount count() const {
                                        if (!isValid()) return FrameCount::unknown();
                                        return FrameCount(end.value() - start.value() + 1);
                                }

                                /**
                         * @brief Returns true if @p frame falls within the inclusive range.
                         *
                         * Returns @c false for any @c Unknown @p frame, and @c false
                         * when this range is itself invalid.
                         *
                         * @param frame The frame to test.
                         * @return @c true if @c start @c <= @c frame @c <= @c end.
                         */
                                bool contains(const FrameNumber &frame) const {
                                        if (!isValid() || !frame.isValid()) return false;
                                        return frame.value() >= start.value() && frame.value() <= end.value();
                                }

                                /** @brief Equality. */
                                bool operator==(const FrameRange &other) const {
                                        return start == other.start && end == other.end;
                                }
                                /** @brief Inequality. */
                                bool operator!=(const FrameRange &other) const { return !(*this == other); }

                                /**
                         * @brief Slides both endpoints by @p n frames.
                         *
                         * Pure translation — start and end move together
                         * so the count is preserved.  Negative @p n shifts
                         * left.  An invalid range (or one whose endpoints
                         * become @c Unknown after the shift) stays invalid.
                         *
                         * @param n Number of frames to add to both
                         *          @c start and @c end.
                         * @return Reference to this range.
                         */
                                FrameRange &shift(int64_t n) {
                                        start += n;
                                        end += n;
                                        return *this;
                                }

                                /** @brief In-place shift by @p n frames; see @ref shift. */
                                FrameRange &operator+=(int64_t n) { return shift(n); }
                                /** @brief In-place shift by @c -n frames; see @ref shift. */
                                FrameRange &operator-=(int64_t n) { return shift(-n); }

                                /** @brief Returns @p r shifted by @p n frames. */
                                friend FrameRange operator+(FrameRange r, int64_t n) {
                                        r.shift(n);
                                        return r;
                                }
                                /** @brief Returns @p r shifted by @c -n frames. */
                                friend FrameRange operator-(FrameRange r, int64_t n) {
                                        r.shift(-n);
                                        return r;
                                }

                                /**
                         * @brief Range-for support (ADL @c begin).
                         *
                         * Defined as a friend free function rather than a member
                         * because @c FrameRange already exposes a @c FrameNumber
                         * field named @c end — a member function with the same
                         * name would collide with the standard range-for
                         * protocol.  Found via argument-dependent lookup.
                         */
                                friend Iterator begin(const FrameRange &r) { return Iterator(r.start); }

                                /**
                         * @brief Range-for support (ADL @c end).
                         *
                         * Returns one past @c r.end (i.e. @c FrameNumber(end.value()+1)).
                         * For an invalid range, returns the same iterator as
                         * @c begin so the loop body never runs.
                         */
                                friend Iterator end(const FrameRange &r) {
                                        if (!r.isValid()) return Iterator(r.start);
                                        return Iterator(FrameNumber(r.end.value() + 1));
                                }
                };

                /**
                 * @brief Parses a MediaDuration from a string.
                 *
                 * Recognised forms (whitespace around operators is tolerated):
                 * - empty string → default (both fields @c Unknown)
                 * - @c "<start>+<length>" — start FrameNumber plus length FrameCount
                 * - @c "<start>-<end>"    — inclusive range, converted via @ref fromFrameRange
                 *
                 * @param str The string to parse.
                 * @param err Optional error output; set to @c Error::Ok on
                 *            success or @c Error::ParseFailed on malformed
                 *            input.
                 * @return The parsed MediaDuration, or a default-constructed
                 *         MediaDuration on failure.
                 */
                static MediaDuration fromString(const String &str, Error *err = nullptr);

                /**
                 * @brief Builds a MediaDuration from an inclusive FrameRange.
                 * @param range The inclusive range.
                 * @return A MediaDuration whose @c start is @c range.start and
                 *         whose @c length is @c range.count().
                 */
                static MediaDuration fromFrameRange(const FrameRange &range) {
                        return MediaDuration(range.start, range.count());
                }

                /** @brief Default-constructs an Unknown MediaDuration (both fields Unknown). */
                MediaDuration() = default;

                /** @brief Constructs a MediaDuration from explicit start and length.
                 *  @param start  Starting frame index.
                 *  @param length Number of frames. */
                MediaDuration(const FrameNumber &start, const FrameCount &length) : _start(start), _length(length) {}

                /**
                 * @brief Returns true if both fields are valid (non-Unknown).
                 *
                 * Note that a valid MediaDuration may still be @c Infinite
                 * (length is @c Infinity) or @c Empty (length is @c 0).
                 */
                bool isValid() const { return _start.isValid() && _length.isValid(); }

                /** @brief Returns true if either field is @c Unknown. */
                bool isUnknown() const { return _start.isUnknown() || _length.isUnknown(); }

                /** @brief Returns true if the length is @c Infinite. */
                bool isInfinite() const { return _length.isInfinite(); }

                /** @brief Returns true if the length is @c 0 (and not @c Unknown). */
                bool isEmpty() const { return _length.isEmpty(); }

                /** @brief Returns the start frame. */
                const FrameNumber &start() const { return _start; }

                /** @brief Returns the length in frames. */
                const FrameCount &length() const { return _length; }

                /** @brief Sets the start frame.
                 *  @param s New start frame. */
                void setStart(const FrameNumber &s) { _start = s; }

                /** @brief Sets the length.
                 *  @param c New length. */
                void setLength(const FrameCount &c) { _length = c; }

                /**
                 * @brief Sets the inclusive end frame, deriving the length.
                 *
                 * Recomputes @c length so that the duration covers
                 * @c [start .. e] inclusive.  When @c start is @c Unknown,
                 * or @p e is @c Unknown, or @p e precedes @c start, the
                 * resulting @c length is @c Unknown.
                 *
                 * @param e New inclusive end frame.
                 */
                void setEnd(const FrameNumber &e);

                /**
                 * @brief Returns the inclusive end frame, i.e. @c start + @c length - 1.
                 *
                 * When @c length is @c Empty the returned FrameNumber is @c Unknown
                 * (an empty duration has no end frame).  When @c length is
                 * @c Infinite the end is @c Unknown (no bounded end).
                 *
                 * @return The last frame of the duration, or @c Unknown if
                 *         no concrete end exists.
                 */
                FrameNumber end() const;

                /**
                 * @brief Returns true if @p frame falls within this duration.
                 *
                 * Equivalent to @c toFrameRange().first().contains(frame), but
                 * folds the validity / infinity checks inline.  Returns
                 * @c false for an @c Unknown duration, an @c Empty duration,
                 * an @c Unknown @p frame, or any @p frame outside @c [start..end].
                 * For an @c Infinite duration, returns @c true for any
                 * @p frame @c >= @c start.
                 *
                 * @param frame The frame to test.
                 * @return @c true when @p frame is part of the duration.
                 */
                bool contains(const FrameNumber &frame) const;

                /**
                 * @brief Moves the start forward by @p n frames.  Length is unchanged.
                 * @param n Number of frames to advance.  Negative values move backward.
                 */
                void addToStart(int64_t n) { _start += n; }

                /**
                 * @brief Moves the end (and therefore the length) by @p n frames.  Start is unchanged.
                 * @param n Number of frames to add to the length.
                 */
                void addToEnd(int64_t n) { _length += n; }

                /** @brief Extends the length by @p c.  Start is unchanged. */
                MediaDuration &operator+=(const FrameCount &c) {
                        _length += c;
                        return *this;
                }
                /** @brief Shrinks the length by @p c.  Start is unchanged. */
                MediaDuration &operator-=(const FrameCount &c) {
                        _length -= c;
                        return *this;
                }

                /**
                 * @brief Converts to an inclusive FrameRange.
                 *
                 * Returns an error when the MediaDuration cannot be
                 * expressed as a finite, bounded range:
                 * - @c Error::DurationUnknown   — either field is @c Unknown
                 * - @c Error::FrameRangeInfinite — the length is @c Infinite
                 * - @c Error::Invalid            — the length is @c Empty
                 *                                  (no last frame)
                 *
                 * @return A @ref Result holding the @ref FrameRange or an Error.
                 */
                Result<FrameRange> toFrameRange() const;

                /** @brief Equality. */
                bool operator==(const MediaDuration &other) const {
                        return _start == other._start && _length == other._length;
                }
                /** @brief Inequality. */
                bool operator!=(const MediaDuration &other) const { return !(*this == other); }

                /**
                 * @brief Lexicographic ordering — by @c start first, then @c length.
                 *
                 * Lets MediaDurations live in @c Set / sorted @c List
                 * containers (e.g. EDL-style ordered cuts).  The
                 * ordering is NaN-like: @c operator< returns @c false
                 * whenever either side is @c Unknown (either field).
                 * @c Unknown MediaDurations are therefore considered
                 * incomparable, not sorted — pre-filter with
                 * @ref isUnknown before inserting into a strictly
                 * ordered container if a total order is required.
                 * Among valid durations, @c Unknown @c length delegates
                 * to @ref FrameCount NaN-like ordering and @c Infinity
                 * length sorts last.
                 */
                bool operator<(const MediaDuration &other) const {
                        if (isUnknown() || other.isUnknown()) return false;
                        if (_start != other._start) return _start < other._start;
                        return _length < other._length;
                }
                /**
                 * @brief Less-than-or-equal (NaN-like).  See @ref operator<.
                 *
                 * Returns @c false whenever either side is @c Unknown,
                 * even when both sides are @c Unknown and compare
                 * @c == — consistent with NaN semantics.
                 */
                bool operator<=(const MediaDuration &other) const {
                        if (isUnknown() || other.isUnknown()) return false;
                        return *this == other || *this < other;
                }
                /** @brief See @ref operator<. */
                bool operator>(const MediaDuration &other) const { return other < *this; }
                /** @brief Greater-than-or-equal (NaN-like).  See @ref operator<=. */
                bool operator>=(const MediaDuration &other) const {
                        if (isUnknown() || other.isUnknown()) return false;
                        return *this == other || other < *this;
                }

                /**
                 * @brief Returns true if @p other is fully contained in this duration.
                 *
                 * A finite duration contains @p other when @c other.start
                 * is @c >= @c start and @c other's last frame is @c <=
                 * this's last frame.  An @c Infinite duration contains
                 * any @p other whose start is @c >= @c start.  An
                 * @c Empty @p other is considered contained in any valid
                 * duration.  @c Unknown on either side returns @c false.
                 *
                 * @param other The duration to test.
                 * @return @c true if every frame of @p other is also in this.
                 */
                bool contains(const MediaDuration &other) const;

                /** @brief Returns true if this duration overlaps @p other in at least one frame. */
                bool overlaps(const MediaDuration &other) const;

                /**
                 * @brief Returns the intersection of this duration with @p other.
                 *
                 * The result is the maximal MediaDuration covering only
                 * frames that belong to both inputs.  When the two
                 * durations do not overlap (or either is @c Unknown /
                 * @c Empty), the result is a default-constructed
                 * (@c Unknown) MediaDuration.  @c Infinite operands are
                 * handled correctly — the intersection of a finite and
                 * an infinite range is the finite tail; the intersection
                 * of two infinite ranges starts at the later of the two
                 * starts and is itself infinite.
                 *
                 * @param other The duration to intersect with.
                 * @return The intersection, or @c Unknown when there is no overlap.
                 */
                MediaDuration intersect(const MediaDuration &other) const;

                /**
                 * @brief Returns true if @p other can be appended to the end of this duration.
                 *
                 * Requires this duration to have a defined end (valid
                 * @c start, finite non-empty @c length) and @p other to
                 * have a defined start (valid @c start, valid @c length —
                 * finite-positive or @c Infinity).  Adjacency means
                 * @c other.start equals the frame right after this
                 * duration's last frame.
                 *
                 * @param other The duration to append.
                 * @return @c true if @ref append would succeed.
                 */
                bool canAppend(const MediaDuration &other) const;

                /**
                 * @brief Appends @p other to the end of this duration in place.
                 *
                 * On success, @c length grows by @c other.length using
                 * normal @ref FrameCount arithmetic so an @c Infinity
                 * @p other turns this into an @c Infinity duration.
                 * On failure (@ref canAppend returns @c false) this
                 * duration is unchanged.
                 *
                 * @param other The duration to append.
                 * @return @c Error::Ok on success, @c Error::NotAdjacent
                 *         when @p other does not begin at the frame after
                 *         this duration's last frame.
                 */
                Error append(const MediaDuration &other);

                /**
                 * @brief Returns true if @p other can be prepended to the start of this duration.
                 *
                 * Requires @p other to have a defined end (valid
                 * @c start, finite non-empty @c length) and this
                 * duration to have a defined start (valid @c start,
                 * valid @c length — finite-positive or @c Infinity).
                 * Adjacency means this duration's @c start equals the
                 * frame right after @p other's last frame.
                 *
                 * @param other The duration to prepend.
                 * @return @c true if @ref prepend would succeed.
                 */
                bool canPrepend(const MediaDuration &other) const;

                /**
                 * @brief Prepends @p other onto the start of this duration in place.
                 *
                 * On success, @c start moves back to @p other.start and
                 * @c length grows by @c other.length.  On failure
                 * (@ref canPrepend returns @c false) this duration is
                 * unchanged.
                 *
                 * @param other The duration to prepend.
                 * @return @c Error::Ok on success, @c Error::NotAdjacent
                 *         when @p other does not end at the frame before
                 *         this duration's start.
                 */
                Error prepend(const MediaDuration &other);

                /**
                 * @brief Returns the canonical string form.
                 * @return @c "<start>+<length>", where each part uses its
                 *         own canonical form.  Empty fields render empty
                 *         (so an Unknown duration is @c "+").
                 */
                String toString() const;

                /** @brief Implicit conversion to String via @ref toString. */
                operator String() const { return toString(); }

        private:
                FrameNumber _start;
                FrameCount  _length;
};

/** @brief Returns @p d with its length extended by @p c. */
inline MediaDuration operator+(MediaDuration d, const FrameCount &c) {
        d += c;
        return d;
}
/** @brief Returns @p d with its length shrunk by @p c. */
inline MediaDuration operator-(MediaDuration d, const FrameCount &c) {
        d -= c;
        return d;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::MediaDuration);

/**
 * @brief Hash specialization so @ref promeki::MediaDuration can serve as
 *        a key in @c HashMap / @c HashSet.
 *
 * Combines the hashes of @c start and @c length using the standard
 * boost-style mix.
 */
template <> struct std::hash<promeki::MediaDuration> {
                size_t operator()(const promeki::MediaDuration &v) const noexcept {
                        size_t h = std::hash<promeki::FrameNumber>()(v.start());
                        size_t k = std::hash<promeki::FrameCount>()(v.length());
                        // Standard boost::hash_combine mix.
                        return h ^ (k + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
                }
};

/**
 * @brief Hash specialization for @ref promeki::MediaDuration::FrameRange.
 */
template <> struct std::hash<promeki::MediaDuration::FrameRange> {
                size_t operator()(const promeki::MediaDuration::FrameRange &v) const noexcept {
                        size_t h = std::hash<promeki::FrameNumber>()(v.start);
                        size_t k = std::hash<promeki::FrameNumber>()(v.end);
                        return h ^ (k + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
                }
};
