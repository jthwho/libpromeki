/**
 * @file      subtitle.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/color.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/namespace.h>
#include <promeki/rect.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

class DataStream;
class FrameRate;
class JsonObject;
class Metadata;
class SubtitleList;
struct SubtitleImpl; // Pimpl — defined in subtitle.cpp.

/**
 * @brief One styled run within a @ref Subtitle.
 * @ingroup proav
 *
 * Subtitle authoring formats (SRT, ASS/SSA, WebVTT) routinely mark up
 * a single cue with inline runs that differ in weight, slant, or
 * colour — `<i>...</i>`, `<b>...</b>`, `<u>...</u>`,
 * `<font color="#FF0000">...</font>` in SubRip; `\b1\i1` /
 * `\c&HFFFF00&` overrides in ASS; `<c.warning>` classes in WebVTT.
 * A flat string drops that information; carrying it round-trip
 * requires representing each cue as an ordered list of styled
 * @ref SubtitleSpan entries.
 *
 * Each span is a plain value type with the text it covers plus a
 * minimal style payload:
 *
 *  - @ref bold / @ref italic / @ref underline — boolean flags
 *    that combine independently.
 *  - @ref color — explicit per-span colour.  A default-constructed
 *    @ref Color (`isValid() == false`) means *inherit* — the renderer
 *    or downstream format adapter substitutes its own default.
 *
 * Span coverage is contiguous: the spans of a cue are concatenated
 * in order to form the cue's plain text.  Newlines inside a span's
 * @ref text remain literal `\n` (the SubRip wire format mandates
 * a separate physical line per `\n`); spans never carry trailing
 * whitespace differences that the renderer is expected to suppress.
 *
 * @par When to construct directly
 *
 * The expected source of styled spans is a format parser
 * (@ref SubRip, future @c Ass / @c WebVtt, the CEA-608 / CEA-708
 * decoders).  Application code can also build spans by hand when
 * synthesising subtitles for the test pattern generator or a
 * burn-in stage — the constructors below are deliberately ergonomic
 * for that case.
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronised.
 *
 * @see Subtitle, SubRip
 */
class SubtitleSpan {
        public:
                /** @brief Default-constructs an empty unstyled span. */
                SubtitleSpan() = default;

                /** @brief Constructs an unstyled span carrying @p text. */
                explicit SubtitleSpan(String text) : _text(std::move(text)) {}

                /** @brief Full-style constructor. */
                SubtitleSpan(String text, bool bold, bool italic, bool underline, Color color = Color())
                    : _text(std::move(text)), _bold(bold), _italic(italic), _underline(underline), _color(color) {}

                /** @brief Span text (UTF-8). */
                const String &text() const { return _text; }

                /** @brief @c true when the span is rendered bold. */
                bool bold() const { return _bold; }

                /** @brief @c true when the span is rendered italic. */
                bool italic() const { return _italic; }

                /** @brief @c true when the span is rendered with an underline. */
                bool underline() const { return _underline; }

                /**
                 * @brief Per-span colour override.
                 *
                 * Returns a default-constructed @ref Color (@c isValid()
                 * @c false) when the span carries no explicit colour —
                 * the renderer or downstream encoder is expected to
                 * substitute its own default.
                 */
                const Color &color() const { return _color; }

                /// @brief @c true when this span carries any style override.
                bool hasStyle() const { return _bold || _italic || _underline || _color.isValid(); }

                /// @brief @c true when the span carries no visible text.
                bool isEmpty() const { return _text.isEmpty(); }

                void setText(String v) { _text = std::move(v); }
                void setBold(bool v) { _bold = v; }
                void setItalic(bool v) { _italic = v; }
                void setUnderline(bool v) { _underline = v; }
                void setColor(const Color &v) { _color = v; }

                bool operator==(const SubtitleSpan &o) const {
                        return _bold == o._bold && _italic == o._italic && _underline == o._underline
                                && _color == o._color && _text == o._text;
                }
                bool operator!=(const SubtitleSpan &o) const { return !(*this == o); }

                /// @brief Structured JSON dump for inspection / tooling.
                JsonObject toJson() const;

                /// @brief Short human-readable summary.
                String toString() const;

                /// @brief Convenience alias for a list of spans.
                using List = ::promeki::List<SubtitleSpan>;

        private:
                String _text;
                bool   _bold = false;
                bool   _italic = false;
                bool   _underline = false;
                Color  _color; // Default-invalid colour means "inherit".
};

/** @brief Writes a @ref SubtitleSpan to a @ref DataStream. */
DataStream &operator<<(DataStream &stream, const SubtitleSpan &span);

/** @brief Reads a @ref SubtitleSpan from a @ref DataStream. */
DataStream &operator>>(DataStream &stream, SubtitleSpan &span);

/**
 * @brief A single timed subtitle cue.
 * @ingroup proav
 *
 * Format-agnostic value type modelling one entry in any subtitle
 * file (SubRip, WebVTT, SCC, ASS/SSA, MicroDVD, …) or any subtitle
 * carriage on the wire (CEA-608/708 in ANC, DVB subtitles, teletext,
 * MPEG-TS PSI).  The same @c Subtitle value flows through every
 * format adapter (@ref SubRip, future @c Scc, @c WebVtt, …) and
 * through the @ref Cea608Encoder / @ref Cea608Decoder pair, so
 * subtitle content stays portable across formats and wire forms.
 *
 * @par Field set
 *
 *  - @ref start / @ref end — display time window.
 *  - @ref spans — ordered list of styled @ref SubtitleSpan runs that
 *    make up the cue's text.  An unstyled cue has a single span.
 *  - @ref text — flat concatenation of the spans' text, suitable for
 *    code that doesn't care about per-run styling.
 *  - @ref anchor — 9-position anchor (matches ASS @c \\anN).
 *  - @ref region — optional pixel-space bounding box hint.
 *  - @ref speaker — accessibility / voice attribution.
 *  - @ref metadata — escape hatch for format-specific keys carried
 *                    as a @ref Metadata (typed key/value store).
 *
 * Format-specific extensions that aren't first-class members
 * (cue id, BCP 47 language tag, WebVTT classes, ASS layer / style,
 * etc.) ride on @ref metadata.  Well-known @ref Metadata IDs land
 * alongside the format adapters that need them.
 *
 * @par Time semantics
 *
 * @c start and @c end are @ref TimeStamp values whose epoch is
 * media t=0 of the source file.  They are *not* wall-clock times.
 * @ref MediaTimeStamp (TimeStamp + ClockDomain) is the right type
 * once a subtitle is bound to a running pipeline, but a standalone
 * subtitle parsed from a file has no clock domain yet — the file
 * is the only reference.  Consumers map file-relative timestamps to
 * pipeline-domain timestamps at the seam where they bind the
 * subtitle to a frame source.
 *
 * @par Storage and copy semantics
 *
 * Value-type handle backed by an internal
 * @c SharedPtr<SubtitleImpl> (pimpl).  Copying a @c Subtitle is
 * O(1) — the underlying state is shared until a mutator forces a
 * copy-on-write detach.  The handle has no @c ::Ptr alias (per the
 * post-2026-05-07 convention).  Hides the impl in the .cpp so the
 * subtitle's @ref Metadata member doesn't drag the full
 * @c VariantDatabase machinery into every translation unit that
 * just wants to *forward* a subtitle.
 *
 * @par Variant / DataStream integration
 *
 * Registered as @c Variant::TypeSubtitle with tag
 * @c DataStream::TypeSubtitle (0x5C).  Frame metadata: a Subtitle
 * can ride on a @c Metadata::Subtitle key on the @ref Frame where
 * its display window starts.
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe.  Distinct handles can be used
 * concurrently; concurrent access to a single handle must be
 * externally synchronised.
 *
 * @see SubtitleSpan, SubtitleList, SubRip, Cea608Encoder, Cea608Decoder
 */
class Subtitle {
        public:
                // -- Construction / destruction (out-of-line for pimpl) ---

                /** @brief Default-constructs an empty subtitle. */
                Subtitle();

                /**
                 * @brief Constructs an unstyled subtitle with the
                 *        universal fields.
                 *
                 * The cue's @ref spans list is populated with a single
                 * unstyled @ref SubtitleSpan carrying @p text.
                 *
                 * @param start  Display-window start (media t=0 epoch).
                 * @param end    Display-window end.
                 * @param text   Displayed text.  Multi-line cues use
                 *               literal `\n`.
                 * @param anchor 9-position anchor; @c SubtitleAnchor::Default
                 *               when none was specified.
                 */
                Subtitle(TimeStamp start, TimeStamp end, String text,
                         SubtitleAnchor anchor = SubtitleAnchor::Default);

                /**
                 * @brief Constructs an unstyled subtitle with the full
                 *        non-style field set.
                 *
                 * Equivalent to the universal-fields constructor plus
                 * a region / speaker / metadata triple.  Spans are
                 * built from @p text exactly as in the simpler
                 * constructor.
                 */
                Subtitle(TimeStamp start, TimeStamp end, String text, SubtitleAnchor anchor, Rect2Di32 region,
                         String speaker, Metadata metadata);

                /**
                 * @brief Constructs a subtitle with explicit styled spans.
                 *
                 * Use this overload from format adapters that have
                 * parsed inline markup (SubRip / ASS / WebVTT) into
                 * structured runs.  The cue's @ref text is computed
                 * lazily from the concatenation of the spans' text.
                 */
                Subtitle(TimeStamp start, TimeStamp end, SubtitleSpan::List spans, SubtitleAnchor anchor,
                         Rect2Di32 region, String speaker, Metadata metadata);

                Subtitle(const Subtitle &);
                Subtitle(Subtitle &&) noexcept;
                ~Subtitle();
                Subtitle &operator=(const Subtitle &);
                Subtitle &operator=(Subtitle &&) noexcept;

                // -- Read-only accessors ----------------------------------

                /** @brief Display-window start (media t=0 epoch). */
                const TimeStamp &start() const;

                /** @brief Display-window end (media t=0 epoch). */
                const TimeStamp &end() const;

                /** @brief Display window length (@c end - @c start). */
                TimeStamp::Duration duration() const;

                /**
                 * @brief Cue text as a flat concatenation of all spans.
                 *
                 * The returned reference is cached inside the impl;
                 * it stays valid until the next mutation that touches
                 * @ref spans or @ref text on this handle (or any
                 * handle currently sharing the same impl).
                 */
                const String &text() const;

                /** @brief Styled spans that make up this cue. */
                const SubtitleSpan::List &spans() const;

                /** @brief 9-position anchor. */
                const SubtitleAnchor &anchor() const;

                /** @brief Pixel-space bounding-box hint; @c isValid() false when unset. */
                const Rect2Di32 &region() const;

                /** @brief Speaker / voice identifier; empty when absent. */
                const String &speaker() const;

                /** @brief Format-specific extension metadata. */
                const Metadata &metadata() const;

                // -- CoW mutators -----------------------------------------

                void setStart(const TimeStamp &v);
                void setEnd(const TimeStamp &v);

                /**
                 * @brief Replaces every span with a single unstyled
                 *        span carrying @p v.
                 *
                 * Use @ref setSpans when preserving per-run styling.
                 */
                void setText(const String &v);

                /** @brief Replaces the span list. */
                void setSpans(SubtitleSpan::List v);

                void setAnchor(const SubtitleAnchor &v);
                void setRegion(const Rect2Di32 &v);
                void setSpeaker(const String &v);
                void setMetadata(const Metadata &v);

                // -- Predicates -------------------------------------------

                /// @brief Returns @c true when this subtitle has no
                ///        content (empty text and zero-length window).
                bool isEmpty() const;

                /// @brief Returns @c true when @p t falls inside
                ///        [start, end).  Zero-length cues are inactive.
                bool isActiveAt(const TimeStamp &t) const;

                // -- Comparison + diagnostics -----------------------------

                bool operator==(const Subtitle &o) const;
                bool operator!=(const Subtitle &o) const { return !(*this == o); }

                /// @brief Structured JSON dump for inspection / tooling.
                JsonObject toJson() const;

                /// @brief Short human-readable summary.
                String toString() const;

        private:
                SharedPtr<SubtitleImpl> _d;
};

/** @brief Writes a @ref Subtitle to a @ref DataStream. */
DataStream &operator<<(DataStream &stream, const Subtitle &sub);

/** @brief Reads a @ref Subtitle from a @ref DataStream. */
DataStream &operator>>(DataStream &stream, Subtitle &sub);

// ============================================================================
// SubtitleList
// ============================================================================

struct SubtitleListImpl; // Pimpl — defined in subtitle.cpp.

/**
 * @brief An ordered collection of @ref Subtitle entries.
 * @ingroup proav
 *
 * Drop-in container for subtitle-file parser / emitter results and
 * for the timeline input to @ref Cea608Encoder.  Same value-type /
 * CoW semantics as @ref Subtitle — copy is O(1), shared storage
 * until a mutator detaches.
 *
 * @par Lookup helpers
 *
 * Point-in-time queries — "what subtitle (if any) is active at
 * @c t?", "what's the next subtitle after @c t?", "what subtitles
 * overlap @c [a, b)?" — are first-class.  When the list is in
 * canonical sorted-by-start order, the lookup helpers use binary
 * search; otherwise they fall back to linear scan.
 * @ref sortByStart sets the cache flag; mutators that could break
 * order reset it.
 *
 * @par Thread Safety
 *
 * Conditionally thread-safe (same contract as @ref Subtitle).
 *
 * @see Subtitle, SubRip, Cea608Encoder
 */
class SubtitleList {
        public:
                // -- Construction / destruction (out-of-line for pimpl) ---

                SubtitleList();
                explicit SubtitleList(List<Subtitle> entries);
                SubtitleList(const SubtitleList &);
                SubtitleList(SubtitleList &&) noexcept;
                ~SubtitleList();
                SubtitleList &operator=(const SubtitleList &);
                SubtitleList &operator=(SubtitleList &&) noexcept;

                // -- Size / access ----------------------------------------

                size_t size() const;
                bool   isEmpty() const;

                /** @brief Read-only indexed access. */
                const Subtitle &at(size_t i) const;

                /** @copydoc at */
                const Subtitle &operator[](size_t i) const;

                // -- Mutators (CoW) ---------------------------------------

                /** @brief Appends one entry; resets the sorted-cache
                 *         flag when order would no longer be ascending. */
                void append(const Subtitle &s);

                /** @brief Removes every entry. */
                void clear();

                /** @brief Sorts entries ascending by @c start (stable). */
                void sortByStart();

                /** @brief Reserves storage for at least @p n entries. */
                void reserve(size_t n);

                // -- Search helpers ---------------------------------------

                /**
                 * @brief Index of the first subtitle whose [start, end)
                 *        window contains @p t, or -1 when none.
                 *
                 * Binary search when sorted by start; linear scan
                 * otherwise.  Overlapping cues: returns the lowest
                 * matching index.
                 */
                int64_t findActiveAt(const TimeStamp &t) const;

                /**
                 * @brief Index of the first subtitle whose @c start is
                 *        @c >= @p t, or -1 when every subtitle starts
                 *        before @p t.
                 */
                int64_t findNextAfter(const TimeStamp &t) const;

                /**
                 * @brief Returns the subset whose [start, end) window
                 *        overlaps with [@p start, @p end).
                 */
                SubtitleList findInRange(const TimeStamp &start, const TimeStamp &end) const;

                /**
                 * @brief Returns a copy of the list with each cue's
                 *        @c start and @c end rounded to the nearest
                 *        frame boundary at the given @p frameRate.
                 *
                 * Subtitle authoring tools emit cue times at arbitrary
                 * millisecond precision, but any frame-based pipeline
                 * (TPG, ANC injection, caption renderer) ultimately
                 * has to decide which *frame* a cue is "on".  Snapping
                 * up front makes that decision explicit and reversible:
                 * the snapped @c start / @c end are the exact
                 * @ref TimeStamp values that the frame-N tick falls on,
                 * so downstream equality checks (e.g. "is this frame
                 * the cue's first frame?") just work.  Cues whose
                 * @c start rounds to the same frame as their @c end
                 * collapse to a zero-duration cue (kept in the list;
                 * the encoder treats it as a no-op).
                 *
                 * The original cues are not modified; this returns a
                 * fresh @ref SubtitleList (CoW-shared until the caller
                 * mutates it).
                 */
                SubtitleList snapToFrames(const FrameRate &frameRate) const;

                // -- Raw access -------------------------------------------

                /** @brief Read-only reference to the underlying list. */
                const List<Subtitle> &entries() const;

                // -- Comparison + diagnostics -----------------------------

                bool operator==(const SubtitleList &o) const;
                bool operator!=(const SubtitleList &o) const { return !(*this == o); }

                /** @brief Structured JSON dump. */
                JsonObject toJson() const;

                /** @brief Short human-readable summary. */
                String toString() const;

        private:
                SharedPtr<SubtitleListImpl> _d;
};

PROMEKI_NAMESPACE_END
