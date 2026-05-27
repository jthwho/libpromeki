/**
 * @file      subtitle.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <promeki/color.h>
#include <promeki/datastream.h>
#include <promeki/framerate.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/metadata.h>
#include <promeki/rect.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/textwrap.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Pimpl definitions
// ============================================================================

struct SubtitleSpanImpl {
                PROMEKI_SHARED_FINAL(SubtitleSpanImpl)

                /// @brief Span text (UTF-8).
                String text;
                /// @brief @c true when the span is rendered bold.
                bool bold = false;
                /// @brief @c true when the span is rendered italic.
                bool italic = false;
                /// @brief @c true when the span is rendered with an underline.
                bool underline = false;
                /// @brief Per-span foreground colour; default-invalid means "inherit".
                Color color;
                /// @brief Per-span background colour; default-invalid means "inherit".
                Color backgroundColor;
                /// @brief Per-span edge colour; default-invalid means "inherit".
                Color edgeColor;
                /// @brief Edge effect style (CEA-708 SetPenAttributes edge_type).
                SubtitleEdgeStyle edgeStyle = SubtitleEdgeStyle::None;
                /// @brief Font face tag (CEA-708 SetPenAttributes font_tag).
                SubtitleFontFace fontFace = SubtitleFontFace::Default;
                /// @brief Foreground opacity (CEA-708 SetPenColor fg_opacity).
                SubtitleOpacity foregroundOpacity = SubtitleOpacity::Solid;
                /// @brief Background opacity (CEA-708 SetPenColor bg_opacity).
                SubtitleOpacity backgroundOpacity = SubtitleOpacity::Solid;
                /// @brief Edge opacity (CEA-708 SetPenColor edge_opacity).
                SubtitleOpacity edgeOpacity = SubtitleOpacity::Solid;

                SubtitleSpanImpl() = default;
};

struct SubtitleImpl {
                PROMEKI_SHARED_FINAL(SubtitleImpl)

                /// @brief Display-window start (media t=0 epoch).
                TimeStamp start;
                /// @brief Display-window end.
                TimeStamp end;
                /// @brief Styled spans that make up this cue.
                SubtitleSpan::List spans;
                /// @brief Cached concatenation of every span's text.
                ///        Recomputed whenever @c spans changes.
                String flatText;
                /// @brief 9-position layout anchor.
                SubtitleAnchor anchor;
                /// @brief Per-cue display mode (Default = encoder picks).
                CaptionMode mode = CaptionMode::Default;
                /// @brief Roll-up row count (2..4) when @c mode is
                ///        @c RollUp.  Zero means "encoder default" —
                ///        the @c Cea608Encoder reads its own
                ///        @c Config::rollUpRows in that case.
                int rollUpRows = 0;
                /// @brief Optional pixel-space bounding-box hint.
                Rect2Di32 region;
                /// @brief Optional speaker / voice identifier.
                String speaker;
                /// @brief Interim-hypothesis marker for progressive
                ///        cue producers (live ASR, paint-on builds);
                ///        defaults to @c false (finalised cue).
                bool partial = false;
                /// @brief Format-specific extension metadata.
                Metadata metadata;

                SubtitleImpl() = default;

                /// @brief Rebuilds @c flatText from the current @c spans.
                void rebuildFlatText() {
                        flatText.clear();
                        for (size_t i = 0; i < spans.size(); ++i) flatText += spans[i].text();
                }
};

struct SubtitleListImpl {
                PROMEKI_SHARED_FINAL(SubtitleListImpl)
                List<Subtitle> entries;
                /// @brief Cached "is sorted by start?" flag.
                bool sortedByStart = true;
};

namespace {
        /// @brief Converts an absolute @ref TimeStamp value (epoch =
        ///        media t=0) into total milliseconds since epoch.
        int64_t timeStampToMs(const TimeStamp &ts) {
                return ts.milliseconds();
        }

        /// @brief Packs the boolean style flags into a single byte for
        ///        wire serialisation.  bit 0 = bold, bit 1 = italic,
        ///        bit 2 = underline.  Future bits (strikethrough,
        ///        small-caps, …) slot in here without forcing a wire
        ///        bump.
        uint8_t packStyleFlags(const SubtitleSpan &s) {
                uint8_t f = 0;
                if (s.bold()) f |= 0x01;
                if (s.italic()) f |= 0x02;
                if (s.underline()) f |= 0x04;
                return f;
        }

        // ====================================================================
        // Wrap helpers — used by @ref Subtitle::wrapped to lay out a cue's
        // styled spans across multiple rows.  Both the CEA-608 and CEA-708
        // encoders consume @ref Subtitle::wrapped so this is the single
        // source of truth for caption-grid layout.
        // ====================================================================

        /// @brief One word inside the cue's flat text plus the index
        ///        of the source @ref SubtitleSpan it lives in.
        ///
        /// Words are tokenised *per span* (not across the flat text)
        /// so a style boundary that falls between two characters
        /// without whitespace — e.g. @c <b>RED</b><i>BL</i> — produces
        /// two distinct words.  @c startPos is the codepoint offset
        /// of the word inside the cue's flat text and is used to
        /// detect whether adjacent words in the wrap result need an
        /// inter-word separator (true wrap boundary) or are visually
        /// fused (style change mid-word).
        struct StyledWord {
                        size_t spanIdx = 0;
                        String text;
                        size_t startPos = 0; ///< Absolute codepoint offset in @c cue.text().
        };

        /// @brief Splits @p cue 's spans into one @ref StyledWord per
        ///        whitespace-separated run *within each span*.
        List<StyledWord> tokenizeStyled(const Subtitle &cue) {
                List<StyledWord>          out;
                const SubtitleSpan::List &spans = cue.spans();
                size_t                    spanOffset = 0; // cumulative codepoint count
                for (size_t s = 0; s < spans.size(); ++s) {
                        const String &text = spans[s].text();
                        const size_t  n = text.length();
                        size_t        i = 0;
                        while (i < n) {
                                while (i < n && text.charAt(i).isSpace()) ++i;
                                if (i >= n) break;
                                const size_t wStart = i;
                                while (i < n && !text.charAt(i).isSpace()) ++i;
                                StyledWord w;
                                w.spanIdx = s;
                                w.text = text.substr(wStart, i - wStart);
                                w.startPos = spanOffset + wStart;
                                out.pushToBack(w);
                        }
                        spanOffset += n;
                }
                return out;
        }

        /// @brief Returns @c true when @p b begins at the codepoint
        ///        immediately after @p a in the cue's flat text — i.e.
        ///        no whitespace ran between them.
        bool wordsAreAdjacent(const StyledWord &a, const StyledWord &b) {
                return a.startPos + a.text.length() == b.startPos;
        }

        /// @brief Joins a half-open word range into a single
        ///        @ref SubtitleSpan list, fusing consecutive same-
        ///        span words and inserting single-space separators
        ///        between true wrap-boundary adjacencies only.
        SubtitleSpan::List rowSpansFromWords(const List<StyledWord>   &words,
                                              const SubtitleSpan::List &srcSpans, size_t wlo,
                                              size_t whi) {
                SubtitleSpan::List out;
                size_t             i = wlo;
                bool               firstRun = true;
                while (i < whi) {
                        const size_t runSpan = words[i].spanIdx;
                        String       runText;
                        size_t       j = i;
                        while (j < whi && words[j].spanIdx == runSpan) {
                                if (j > i) {
                                        if (!wordsAreAdjacent(words[j - 1], words[j])) {
                                                runText += " ";
                                        }
                                }
                                runText += words[j].text;
                                ++j;
                        }
                        if (!firstRun && i > wlo) {
                                if (!wordsAreAdjacent(words[i - 1], words[i])) {
                                        runText = String(" ") + runText;
                                }
                        }
                        firstRun = false;
                        if (runSpan < srcSpans.size()) {
                                const SubtitleSpan &src = srcSpans[runSpan];
                                SubtitleSpan        wrapped(runText, src.bold(), src.italic(),
                                                            src.underline(), src.color());
                                wrapped.setBackgroundColor(src.backgroundColor());
                                wrapped.setEdgeColor(src.edgeColor());
                                wrapped.setEdgeStyle(src.edgeStyle());
                                wrapped.setFontFace(src.fontFace());
                                wrapped.setForegroundOpacity(src.foregroundOpacity());
                                wrapped.setBackgroundOpacity(src.backgroundOpacity());
                                wrapped.setEdgeOpacity(src.edgeOpacity());
                                out.pushToBack(std::move(wrapped));
                        } else {
                                out.pushToBack(SubtitleSpan(runText));
                        }
                        i = j;
                }
                return out;
        }

        /// @brief Range of words belonging to one explicit
        ///        @c '\n'-delimited line of the cue's flat text.
        struct ExplicitRowRange {
                        size_t startWord = 0;
                        size_t endWord = 0;
                        size_t width = 0;
        };

        /// @brief Groups @p words by @c '\n'-delimited lines from the
        ///        cue's @p flat text.
        List<ExplicitRowRange> explicitRowRanges(const List<StyledWord> &words, const String &flat) {
                List<ExplicitRowRange> ranges;
                const size_t           n = flat.length();
                size_t                 wi = 0;
                size_t                 lineStart = 0;
                while (lineStart <= n) {
                        size_t lineEnd = lineStart;
                        while (lineEnd < n && flat.charAt(lineEnd) != '\n') ++lineEnd;
                        ExplicitRowRange r;
                        r.startWord = wi;
                        size_t rowChars = 0;
                        bool   firstWordInRow = true;
                        while (wi < words.size() && words[wi].startPos < lineEnd) {
                                if (!firstWordInRow) {
                                        if (!wordsAreAdjacent(words[wi - 1], words[wi])) ++rowChars;
                                }
                                rowChars += words[wi].text.length();
                                firstWordInRow = false;
                                ++wi;
                        }
                        r.endWord = wi;
                        r.width = rowChars;
                        if (r.endWord > r.startWord) ranges.pushToBack(r);
                        if (lineEnd >= n) break;
                        lineStart = lineEnd + 1;
                }
                return ranges;
        }

        /// @brief Builds the per-row layout for @p cue.  Returns one
        ///        @ref SubtitleSpan::List per physical row.  Tries
        ///        explicit @c '\n'-break layout first; falls back to
        ///        balanced minimax word-wrap when explicit breaks
        ///        don't fit @p maxCols / @p maxRows.
        List<SubtitleSpan::List> layoutRows(const Subtitle &cue, int maxCols, int maxRows) {
                List<SubtitleSpan::List> out;
                List<StyledWord>         words = tokenizeStyled(cue);
                if (words.isEmpty()) return out;
                const SubtitleSpan::List &srcSpans = cue.spans();

                // -- Phase 1: explicit-break attempt -------------------
                if (maxCols > 0) {
                        List<ExplicitRowRange> ranges = explicitRowRanges(words, cue.text());
                        const bool             countOk =
                                (maxRows <= 0) || (static_cast<int>(ranges.size()) <= maxRows);
                        bool widthOk = true;
                        for (size_t i = 0; i < ranges.size(); ++i) {
                                if (ranges[i].width > static_cast<size_t>(maxCols)) {
                                        widthOk = false;
                                        break;
                                }
                        }
                        if (!ranges.isEmpty() && widthOk && countOk) {
                                for (size_t i = 0; i < ranges.size(); ++i) {
                                        out.pushToBack(rowSpansFromWords(words, srcSpans,
                                                                          ranges[i].startWord,
                                                                          ranges[i].endWord));
                                }
                                return out;
                        }
                }

                // -- Phase 2: balanced re-flow over the whole cue ------
                List<size_t> widths;
                widths.reserve(words.size());
                for (size_t i = 0; i < words.size(); ++i) widths.pushToBack(words[i].text.length());

                TextWrap::Config cfg;
                cfg.maxCols = maxCols;
                cfg.maxRows = maxRows;
                cfg.policy = TextWrap::Policy::Balanced;
                List<size_t> breaks = TextWrap::rowBreaks(widths, cfg);
                for (size_t r = 0; r + 1 < breaks.size(); ++r) {
                        out.pushToBack(rowSpansFromWords(words, srcSpans, breaks[r], breaks[r + 1]));
                }
                return out;
        }
} // namespace

// ============================================================================
// SubtitleSpan — construction / special members
// ============================================================================

SubtitleSpan::SubtitleSpan() : _d(SharedPtr<SubtitleSpanImpl>::create()) {}

SubtitleSpan::SubtitleSpan(String text) : _d(SharedPtr<SubtitleSpanImpl>::create()) {
        _d.modify()->text = std::move(text);
}

SubtitleSpan::SubtitleSpan(String text, bool bold, bool italic, bool underline, Color color)
    : _d(SharedPtr<SubtitleSpanImpl>::create()) {
        auto *d = _d.modify();
        d->text = std::move(text);
        d->bold = bold;
        d->italic = italic;
        d->underline = underline;
        d->color = color;
}

SubtitleSpan::SubtitleSpan(const SubtitleSpan &) = default;
SubtitleSpan::SubtitleSpan(SubtitleSpan &&) noexcept = default;
SubtitleSpan::~SubtitleSpan() = default;
SubtitleSpan &SubtitleSpan::operator=(const SubtitleSpan &) = default;
SubtitleSpan &SubtitleSpan::operator=(SubtitleSpan &&) noexcept = default;

// ============================================================================
// SubtitleSpan — accessors / mutators
// ============================================================================

const String            &SubtitleSpan::text() const { return _d->text; }
bool                     SubtitleSpan::bold() const { return _d->bold; }
bool                     SubtitleSpan::italic() const { return _d->italic; }
bool                     SubtitleSpan::underline() const { return _d->underline; }
const Color             &SubtitleSpan::color() const { return _d->color; }
const Color             &SubtitleSpan::backgroundColor() const { return _d->backgroundColor; }
const Color             &SubtitleSpan::edgeColor() const { return _d->edgeColor; }
const SubtitleEdgeStyle &SubtitleSpan::edgeStyle() const { return _d->edgeStyle; }
const SubtitleFontFace  &SubtitleSpan::fontFace() const { return _d->fontFace; }
const SubtitleOpacity   &SubtitleSpan::foregroundOpacity() const { return _d->foregroundOpacity; }
const SubtitleOpacity   &SubtitleSpan::backgroundOpacity() const { return _d->backgroundOpacity; }
const SubtitleOpacity   &SubtitleSpan::edgeOpacity() const { return _d->edgeOpacity; }

bool SubtitleSpan::hasStyle() const {
        return _d->bold || _d->italic || _d->underline || _d->color.isValid()
                || _d->backgroundColor.isValid() || _d->edgeColor.isValid()
                || _d->edgeStyle != SubtitleEdgeStyle::None
                || _d->fontFace != SubtitleFontFace::Default
                || _d->foregroundOpacity != SubtitleOpacity::Solid
                || _d->backgroundOpacity != SubtitleOpacity::Solid
                || _d->edgeOpacity != SubtitleOpacity::Solid;
}
bool SubtitleSpan::isEmpty() const { return _d->text.isEmpty(); }

void SubtitleSpan::setText(String v) { _d.modify()->text = std::move(v); }
void SubtitleSpan::setBold(bool v) { _d.modify()->bold = v; }
void SubtitleSpan::setItalic(bool v) { _d.modify()->italic = v; }
void SubtitleSpan::setUnderline(bool v) { _d.modify()->underline = v; }
void SubtitleSpan::setColor(const Color &v) { _d.modify()->color = v; }
void SubtitleSpan::setBackgroundColor(const Color &v) { _d.modify()->backgroundColor = v; }
void SubtitleSpan::setEdgeColor(const Color &v) { _d.modify()->edgeColor = v; }
void SubtitleSpan::setEdgeStyle(const SubtitleEdgeStyle &v) { _d.modify()->edgeStyle = v; }
void SubtitleSpan::setFontFace(const SubtitleFontFace &v) { _d.modify()->fontFace = v; }
void SubtitleSpan::setForegroundOpacity(const SubtitleOpacity &v) { _d.modify()->foregroundOpacity = v; }
void SubtitleSpan::setBackgroundOpacity(const SubtitleOpacity &v) { _d.modify()->backgroundOpacity = v; }
void SubtitleSpan::setEdgeOpacity(const SubtitleOpacity &v) { _d.modify()->edgeOpacity = v; }

bool SubtitleSpan::operator==(const SubtitleSpan &o) const {
        return _d->bold == o._d->bold && _d->italic == o._d->italic && _d->underline == o._d->underline
                && _d->color == o._d->color && _d->backgroundColor == o._d->backgroundColor
                && _d->edgeColor == o._d->edgeColor
                && _d->edgeStyle.value() == o._d->edgeStyle.value()
                && _d->fontFace.value() == o._d->fontFace.value()
                && _d->foregroundOpacity.value() == o._d->foregroundOpacity.value()
                && _d->backgroundOpacity.value() == o._d->backgroundOpacity.value()
                && _d->edgeOpacity.value() == o._d->edgeOpacity.value() && _d->text == o._d->text;
}

// ============================================================================
// SubtitleSpan — diagnostics + DataStream
// ============================================================================

JsonObject SubtitleSpan::toJson() const {
        JsonObject obj;
        obj.set("text", _d->text);
        if (_d->bold) obj.set("bold", true);
        if (_d->italic) obj.set("italic", true);
        if (_d->underline) obj.set("underline", true);
        if (_d->color.isValid()) obj.set("color", _d->color.toString());
        if (_d->backgroundColor.isValid()) obj.set("backgroundColor", _d->backgroundColor.toString());
        if (_d->edgeColor.isValid()) obj.set("edgeColor", _d->edgeColor.toString());
        if (_d->edgeStyle != SubtitleEdgeStyle::None) {
                obj.set("edgeStyle", _d->edgeStyle.valueName());
        }
        if (_d->fontFace != SubtitleFontFace::Default) {
                obj.set("fontFace", _d->fontFace.valueName());
        }
        if (_d->foregroundOpacity != SubtitleOpacity::Solid) {
                obj.set("foregroundOpacity", _d->foregroundOpacity.valueName());
        }
        if (_d->backgroundOpacity != SubtitleOpacity::Solid) {
                obj.set("backgroundOpacity", _d->backgroundOpacity.valueName());
        }
        if (_d->edgeOpacity != SubtitleOpacity::Solid) {
                obj.set("edgeOpacity", _d->edgeOpacity.valueName());
        }
        return obj;
}

String SubtitleSpan::toString() const {
        String s = "SubtitleSpan(\"";
        constexpr size_t kMaxTextChars = 48;
        if (_d->text.size() <= kMaxTextChars) {
                s += _d->text;
        } else {
                s += _d->text.substr(0, kMaxTextChars);
                s += "...";
        }
        s += "\"";
        if (_d->bold) s += " B";
        if (_d->italic) s += " I";
        if (_d->underline) s += " U";
        if (_d->color.isValid()) {
                s += " color=";
                s += _d->color.toString();
        }
        s += ")";
        return s;
}

DataStream &operator<<(DataStream &stream, const SubtitleSpan &span) {
        // Wire layout:
        //   String  text
        //   uint8_t styleFlags          (bit 0 bold, 1 italic, 2 underline)
        //   Color   fgColor
        //   Color   backgroundColor
        //   Color   edgeColor
        //   int32_t edgeStyle           (SubtitleEdgeStyle value)
        //   int32_t fontFace            (SubtitleFontFace value)
        //   int32_t foregroundOpacity   (SubtitleOpacity value)
        //   int32_t backgroundOpacity
        //   int32_t edgeOpacity
        stream.beginFrame(DataTypeSubtitleSpan, 1);
        stream << span.text();
        stream << packStyleFlags(span);
        stream << span.color();
        stream << span.backgroundColor();
        stream << span.edgeColor();
        stream << static_cast<int32_t>(span.edgeStyle().value());
        stream << static_cast<int32_t>(span.fontFace().value());
        stream << static_cast<int32_t>(span.foregroundOpacity().value());
        stream << static_cast<int32_t>(span.backgroundOpacity().value());
        stream << static_cast<int32_t>(span.edgeOpacity().value());
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, SubtitleSpan &span) {
        if (!stream.readFrame(DataTypeSubtitleSpan)) {
                span = SubtitleSpan();
                return stream;
        }
        String  text;
        uint8_t flags = 0;
        Color   color;
        Color   bgColor;
        Color   edgeColor;
        int32_t edgeStyleVal = 0;
        int32_t fontFaceVal = 0;
        int32_t fgOpacityVal = 0;
        int32_t bgOpacityVal = 0;
        int32_t edgeOpacityVal = 0;
        stream >> text;
        stream >> flags;
        stream >> color;
        stream >> bgColor;
        stream >> edgeColor;
        stream >> edgeStyleVal;
        stream >> fontFaceVal;
        stream >> fgOpacityVal;
        stream >> bgOpacityVal;
        stream >> edgeOpacityVal;
        span = SubtitleSpan(std::move(text), (flags & 0x01) != 0, (flags & 0x02) != 0, (flags & 0x04) != 0,
                            color);
        span.setBackgroundColor(bgColor);
        span.setEdgeColor(edgeColor);
        span.setEdgeStyle(SubtitleEdgeStyle(edgeStyleVal));
        span.setFontFace(SubtitleFontFace(fontFaceVal));
        span.setForegroundOpacity(SubtitleOpacity(fgOpacityVal));
        span.setBackgroundOpacity(SubtitleOpacity(bgOpacityVal));
        span.setEdgeOpacity(SubtitleOpacity(edgeOpacityVal));
        return stream;
}

// ============================================================================
// Subtitle — construction / special members
// ============================================================================

Subtitle::Subtitle() : _d(SharedPtr<SubtitleImpl>::create()) {
        // Enforce the same "always at least one span" invariant that
        // the args-taking constructors and setSpans() maintain, so a
        // default Subtitle compares structurally equal to one built
        // through those paths from empty inputs (used by the DataStream
        // round-trip and by other generic value-shaped consumers).
        _d.modify()->spans.pushToBack(SubtitleSpan());
}

Subtitle::Subtitle(TimeStamp start, TimeStamp end, String text, SubtitleAnchor anchor)
    : _d(SharedPtr<SubtitleImpl>::create()) {
        auto *d = _d.modify();
        d->start = start;
        d->end = end;
        d->anchor = anchor;
        // Unstyled constructor: one span carrying the full text.  An
        // empty text still gets one empty span so spans().size() is a
        // stable 1, which keeps later code (renderers, encoders) from
        // special-casing the no-span case.
        d->spans.pushToBack(SubtitleSpan(std::move(text)));
        d->rebuildFlatText();
}

Subtitle::Subtitle(TimeStamp start, TimeStamp end, String text, SubtitleAnchor anchor, Rect2Di32 region, String speaker,
                   Metadata metadata)
    : _d(SharedPtr<SubtitleImpl>::create()) {
        auto *d = _d.modify();
        d->start = start;
        d->end = end;
        d->anchor = anchor;
        d->region = region;
        d->speaker = std::move(speaker);
        d->metadata = std::move(metadata);
        d->spans.pushToBack(SubtitleSpan(std::move(text)));
        d->rebuildFlatText();
}

Subtitle::Subtitle(TimeStamp start, TimeStamp end, SubtitleSpan::List spans, SubtitleAnchor anchor, Rect2Di32 region,
                   String speaker, Metadata metadata)
    : _d(SharedPtr<SubtitleImpl>::create()) {
        auto *d = _d.modify();
        d->start = start;
        d->end = end;
        d->anchor = anchor;
        d->region = region;
        d->speaker = std::move(speaker);
        d->metadata = std::move(metadata);
        d->spans = std::move(spans);
        if (d->spans.isEmpty()) d->spans.pushToBack(SubtitleSpan());
        d->rebuildFlatText();
}

Subtitle::Subtitle(const Subtitle &) = default;
Subtitle::Subtitle(Subtitle &&) noexcept = default;
Subtitle::~Subtitle() = default;
Subtitle &Subtitle::operator=(const Subtitle &) = default;
Subtitle &Subtitle::operator=(Subtitle &&) noexcept = default;

// ============================================================================
// Subtitle — accessors / mutators
// ============================================================================

const TimeStamp          &Subtitle::start() const { return _d->start; }
const TimeStamp          &Subtitle::end() const { return _d->end; }
TimeStamp::Duration       Subtitle::duration() const { return _d->end.value() - _d->start.value(); }
const String             &Subtitle::text() const { return _d->flatText; }
const SubtitleSpan::List &Subtitle::spans() const { return _d->spans; }
const SubtitleAnchor     &Subtitle::anchor() const { return _d->anchor; }
const CaptionMode        &Subtitle::mode() const { return _d->mode; }
int                       Subtitle::rollUpRows() const { return _d->rollUpRows; }
const Rect2Di32          &Subtitle::region() const { return _d->region; }
const String             &Subtitle::speaker() const { return _d->speaker; }
bool                      Subtitle::partial() const { return _d->partial; }
const Metadata           &Subtitle::metadata() const { return _d->metadata; }

void Subtitle::setStart(const TimeStamp &v) { _d.modify()->start = v; }
void Subtitle::setEnd(const TimeStamp &v) { _d.modify()->end = v; }

void Subtitle::setText(const String &v) {
        auto *d = _d.modify();
        d->spans.clear();
        d->spans.pushToBack(SubtitleSpan(v));
        d->rebuildFlatText();
}

void Subtitle::setSpans(SubtitleSpan::List v) {
        auto *d = _d.modify();
        d->spans = std::move(v);
        if (d->spans.isEmpty()) d->spans.pushToBack(SubtitleSpan());
        d->rebuildFlatText();
}

void Subtitle::setAnchor(const SubtitleAnchor &v) { _d.modify()->anchor = v; }
void Subtitle::setMode(const CaptionMode &v) { _d.modify()->mode = v; }
void Subtitle::setRollUpRows(int v) { _d.modify()->rollUpRows = v; }
void Subtitle::setRegion(const Rect2Di32 &v) { _d.modify()->region = v; }
void Subtitle::setSpeaker(const String &v) { _d.modify()->speaker = v; }
void Subtitle::setPartial(bool v) { _d.modify()->partial = v; }
void Subtitle::setMetadata(const Metadata &v) { _d.modify()->metadata = v; }

Subtitle Subtitle::wrapped(int maxCols, int maxRows) const {
        if (_d->flatText.isEmpty()) return *this;
        List<SubtitleSpan::List> rows = layoutRows(*this, maxCols, maxRows);
        if (rows.isEmpty()) return *this;
        SubtitleSpan::List flat;
        for (size_t r = 0; r < rows.size(); ++r) {
                if (r > 0) flat.pushToBack(SubtitleSpan("\n"));
                const SubtitleSpan::List &row = rows[r];
                for (size_t s = 0; s < row.size(); ++s) flat.pushToBack(row[s]);
        }
        Subtitle out = *this;
        out.setSpans(flat);
        return out;
}

bool Subtitle::isEmpty() const {
        return _d->flatText.isEmpty() && _d->start == TimeStamp() && _d->end == TimeStamp();
}

bool Subtitle::isActiveAt(const TimeStamp &t) const {
        return t.value() >= _d->start.value() && t.value() < _d->end.value();
}

bool Subtitle::operator==(const Subtitle &o) const {
        return _d->start == o._d->start && _d->end == o._d->end && _d->spans == o._d->spans
                && _d->anchor.value() == o._d->anchor.value() && _d->mode.value() == o._d->mode.value()
                && _d->rollUpRows == o._d->rollUpRows && _d->region == o._d->region
                && _d->speaker == o._d->speaker && _d->partial == o._d->partial
                && _d->metadata == o._d->metadata;
}

JsonObject Subtitle::toJson() const {
        JsonObject obj;
        obj.set("startMs", timeStampToMs(_d->start));
        obj.set("endMs", timeStampToMs(_d->end));
        obj.set("text", _d->flatText);
        bool anyStyledSpan = false;
        for (size_t i = 0; i < _d->spans.size(); ++i) {
                if (_d->spans[i].hasStyle()) {
                        anyStyledSpan = true;
                        break;
                }
        }
        // Only emit the spans array when there's something worth saying
        // beyond what @c text already carries.  Keeps the JSON dump
        // compact for the (very common) plain-text cue case.
        if (anyStyledSpan || _d->spans.size() > 1) {
                JsonArray spansArr;
                for (size_t i = 0; i < _d->spans.size(); ++i) spansArr.add(_d->spans[i].toJson());
                obj.set("spans", spansArr);
        }
        if (_d->anchor != SubtitleAnchor::Default) {
                obj.set("anchor", _d->anchor.valueName());
        }
        if (_d->mode != CaptionMode::Default) {
                obj.set("mode", _d->mode.valueName());
        }
        if (_d->rollUpRows != 0) {
                obj.set("rollUpRows", static_cast<int64_t>(_d->rollUpRows));
        }
        if (_d->region.isValid()) {
                JsonObject regionObj;
                regionObj.set("x", static_cast<int64_t>(_d->region.x()));
                regionObj.set("y", static_cast<int64_t>(_d->region.y()));
                regionObj.set("w", static_cast<int64_t>(_d->region.width()));
                regionObj.set("h", static_cast<int64_t>(_d->region.height()));
                obj.set("region", regionObj);
        }
        if (!_d->speaker.isEmpty()) obj.set("speaker", _d->speaker);
        // Only emit the partial flag when it's set — keeps the JSON
        // dump clean for the (vast majority) finalised-cue case.
        if (_d->partial) obj.set("partial", true);
        if (_d->metadata.size() > 0) {
                // Metadata exposes a toString() rendering; structured
                // JSON for Metadata is a separate cleanup (it's
                // VariantDatabase-backed so each value's renderer would
                // need to know its own JSON shape).
                obj.set("metadata", _d->metadata.toString());
        }
        return obj;
}

String Subtitle::toString() const {
        String s = "Subtitle([";
        s += String::number(timeStampToMs(_d->start));
        s += "ms..";
        s += String::number(timeStampToMs(_d->end));
        s += "ms] ";
        constexpr size_t kMaxTextChars = 64;
        if (_d->flatText.size() <= kMaxTextChars) {
                s += _d->flatText;
        } else {
                s += _d->flatText.substr(0, kMaxTextChars);
                s += "...";
        }
        if (_d->anchor != SubtitleAnchor::Default) {
                s += " anchor=";
                s += _d->anchor.valueName();
        }
        if (!_d->speaker.isEmpty()) {
                s += " speaker=";
                s += _d->speaker;
        }
        s += ")";
        return s;
}

// ============================================================================
// Subtitle — DataStream serialization
// ============================================================================
//
// Wire layout (each field tagged via its own DataStream operator):
//   TimeStamp start          (TypeTimeStamp)
//   TimeStamp end            (TypeTimeStamp)
//   int32_t   anchor.value() (TypeS32)
//   int32_t   mode.value()   (TypeS32 — CaptionMode)
//   int32_t   rollUpRows     (TypeS32 — 0 = encoder default)
//   Rect2Di32 region         (free-function operator<< template)
//   String    speaker        (TypeString)
//   bool      partial        (TypeBool — interim-hypothesis marker)
//   Metadata  metadata       (VariantDatabase template operator<<)
//   List<SubtitleSpan> spans (TypeList of TypeSubtitleSpan)
//
// The cached @ref Subtitle::text is reconstructed from spans on read,
// so it does not need its own wire field.

Error Subtitle::writeToStream(DataStream &s) const {
        s << start();
        s << end();
        s << static_cast<int32_t>(anchor().value());
        s << static_cast<int32_t>(mode().value());
        s << static_cast<int32_t>(rollUpRows());
        s << region();
        s << speaker();
        s << partial();
        s << metadata();
        s << spans();
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <>
Result<Subtitle> Subtitle::readFromStream<1>(DataStream &s) {
        TimeStamp          start;
        TimeStamp          end;
        int32_t            anchorValue = 0;
        int32_t            modeValue   = 0;
        int32_t            rollUpRows  = 0;
        Rect2Di32          region;
        String             speaker;
        bool               partial = false;
        Metadata           metadata;
        SubtitleSpan::List spans;
        s >> start >> end >> anchorValue >> modeValue >> rollUpRows >> region >> speaker >> partial >> metadata
                >> spans;
        if (s.status() != DataStream::Ok) return makeError<Subtitle>(s.toError());
        Subtitle sub(start, end, std::move(spans), SubtitleAnchor(anchorValue), region, std::move(speaker),
                     std::move(metadata));
        sub.setMode(CaptionMode(modeValue));
        sub.setRollUpRows(static_cast<int>(rollUpRows));
        sub.setPartial(partial);
        return makeResult(std::move(sub));
}

// ============================================================================
// SubtitleList — construction / special members
// ============================================================================

SubtitleList::SubtitleList() : _d(SharedPtr<SubtitleListImpl>::create()) {}

SubtitleList::SubtitleList(List<Subtitle> entries) : _d(SharedPtr<SubtitleListImpl>::create()) {
        auto *d = _d.modify();
        d->entries = std::move(entries);
        bool sorted = true;
        for (size_t i = 1; i < d->entries.size(); ++i) {
                if (d->entries[i].start().value() < d->entries[i - 1].start().value()) {
                        sorted = false;
                        break;
                }
        }
        d->sortedByStart = sorted;
}

SubtitleList::SubtitleList(const SubtitleList &) = default;
SubtitleList::SubtitleList(SubtitleList &&) noexcept = default;
SubtitleList::~SubtitleList() = default;
SubtitleList &SubtitleList::operator=(const SubtitleList &) = default;
SubtitleList &SubtitleList::operator=(SubtitleList &&) noexcept = default;

// ============================================================================
// SubtitleList — size / access
// ============================================================================

size_t SubtitleList::size() const { return _d->entries.size(); }
bool   SubtitleList::isEmpty() const { return _d->entries.isEmpty(); }
const Subtitle &SubtitleList::at(size_t i) const { return _d->entries[i]; }
const Subtitle &SubtitleList::operator[](size_t i) const { return _d->entries[i]; }
const List<Subtitle> &SubtitleList::entries() const { return _d->entries; }

// ============================================================================
// SubtitleList — mutators
// ============================================================================

void SubtitleList::append(const Subtitle &s) {
        auto *d = _d.modify();
        // Only invalidate the sorted-cache when the new entry would
        // violate ascending order.  Appending later cues to an
        // already-sorted list keeps the list searchable in O(log N).
        if (d->sortedByStart && !d->entries.isEmpty()
            && s.start().value() < d->entries[d->entries.size() - 1].start().value()) {
                d->sortedByStart = false;
        }
        d->entries.pushToBack(s);
}

void SubtitleList::clear() {
        auto *d = _d.modify();
        d->entries.clear();
        d->sortedByStart = true; // Empty is trivially sorted.
}

void SubtitleList::reserve(size_t n) { _d.modify()->entries.reserve(n); }

void SubtitleList::sortByStart() {
        auto *d = _d.modify();
        if (d->sortedByStart) return;
        List<Subtitle> tmp;
        tmp.reserve(d->entries.size());
        for (size_t i = 0; i < d->entries.size(); ++i) tmp.pushToBack(d->entries[i]);
        std::stable_sort(tmp.begin(), tmp.end(), [](const Subtitle &a, const Subtitle &b) {
                return a.start().value() < b.start().value();
        });
        d->entries.clear();
        for (auto &s : tmp) d->entries.pushToBack(std::move(s));
        d->sortedByStart = true;
}

// ============================================================================
// SubtitleList — search helpers
// ============================================================================

int64_t SubtitleList::findActiveAt(const TimeStamp &t) const {
        const auto &entries = _d->entries;
        if (entries.isEmpty()) return -1;

        if (_d->sortedByStart) {
                // Binary search for the first index whose @c start > @p t.
                // Anything at or before that index could still be
                // active — scan backwards for the leftmost match.
                size_t lo = 0;
                size_t hi = entries.size();
                while (lo < hi) {
                        size_t mid = lo + (hi - lo) / 2;
                        if (entries[mid].start().value() <= t.value()) {
                                lo = mid + 1;
                        } else {
                                hi = mid;
                        }
                }
                if (hi == 0) return -1;
                int64_t first = -1;
                for (int64_t i = static_cast<int64_t>(hi) - 1; i >= 0; --i) {
                        const Subtitle &s = entries[static_cast<size_t>(i)];
                        if (s.end().value() <= t.value()) continue;
                        if (s.start().value() <= t.value()) first = i;
                }
                return first;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].isActiveAt(t)) return static_cast<int64_t>(i);
        }
        return -1;
}

int64_t SubtitleList::findNextAfter(const TimeStamp &t) const {
        const auto &entries = _d->entries;
        if (entries.isEmpty()) return -1;

        if (_d->sortedByStart) {
                size_t lo = 0;
                size_t hi = entries.size();
                while (lo < hi) {
                        size_t mid = lo + (hi - lo) / 2;
                        if (entries[mid].start().value() < t.value()) {
                                lo = mid + 1;
                        } else {
                                hi = mid;
                        }
                }
                if (lo >= entries.size()) return -1;
                return static_cast<int64_t>(lo);
        }

        int64_t best = -1;
        for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].start().value() >= t.value()) {
                        if (best < 0
                            || entries[i].start().value() < entries[static_cast<size_t>(best)].start().value()) {
                                best = static_cast<int64_t>(i);
                        }
                }
        }
        return best;
}

SubtitleList SubtitleList::snapToFrames(const FrameRate &frameRate) const {
        SubtitleList out;
        if (!frameRate.isValid()) {
                // No rate to snap to — fall back to the input as-is.
                // Caller probably has a config error; we don't try to
                // diagnose that here.
                return *this;
        }
        // Per-frame tick at 1 GHz (ns) using @ref FrameRate::cumulativeTicks
        // for exact rational arithmetic on NTSC fractional rates.
        const int64_t nsPerSec = 1'000'000'000;
        const auto    snapToFrame = [&](const TimeStamp &ts) -> TimeStamp {
                const int64_t ns = ts.nanoseconds();
                // Convert ns → fractional frame index → nearest integer.
                // frame = ns * num / (nsPerSec * den), rounded.
                const int64_t num = static_cast<int64_t>(frameRate.numerator());
                const int64_t den = static_cast<int64_t>(frameRate.denominator());
                const int64_t denom = nsPerSec * den;
                int64_t       frame;
                if (ns >= 0) {
                        frame = (ns * num + denom / 2) / denom;
                } else {
                        frame = -((-ns * num + denom / 2) / denom);
                }
                // Convert back to ns via cumulativeTicks for exactness.
                int64_t       snappedNs;
                if (frame >= 0) {
                        snappedNs = frameRate.cumulativeTicks(nsPerSec, frame);
                } else {
                        // Negative frames: mirror the math by hand.
                        snappedNs = -frameRate.cumulativeTicks(nsPerSec, -frame);
                }
                using ClockDur = TimeStamp::Value::duration;
                return TimeStamp(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(
                        std::chrono::nanoseconds(snappedNs))));
        };
        out.reserve(_d->entries.size());
        for (size_t i = 0; i < _d->entries.size(); ++i) {
                const Subtitle &s = _d->entries[i];
                out.append(Subtitle(snapToFrame(s.start()), snapToFrame(s.end()), s.spans(), s.anchor(),
                                    s.region(), s.speaker(), s.metadata()));
        }
        return out;
}

SubtitleList SubtitleList::findInRange(const TimeStamp &start, const TimeStamp &end) const {
        SubtitleList out;
        const auto  &entries = _d->entries;
        // Overlap test: entry.start < end && entry.end > start.
        for (size_t i = 0; i < entries.size(); ++i) {
                const Subtitle &s = entries[i];
                if (s.start().value() < end.value() && s.end().value() > start.value()) out.append(s);
        }
        return out;
}

// ============================================================================
// SubtitleList — comparison / diagnostics
// ============================================================================

bool SubtitleList::operator==(const SubtitleList &o) const { return _d->entries == o._d->entries; }

JsonObject SubtitleList::toJson() const {
        JsonObject obj;
        obj.set("count", static_cast<int64_t>(_d->entries.size()));
        JsonArray arr;
        for (size_t i = 0; i < _d->entries.size(); ++i) arr.add(_d->entries[i].toJson());
        obj.set("entries", arr);
        return obj;
}

String SubtitleList::toString() const {
        String s = "SubtitleList(count=";
        s += String::number(static_cast<int>(_d->entries.size()));
        if (!_d->entries.isEmpty()) {
                s += ", span=";
                s += String::number(timeStampToMs(_d->entries[0].start()));
                s += "ms..";
                s += String::number(timeStampToMs(_d->entries[_d->entries.size() - 1].end()));
                s += "ms";
        }
        s += ")";
        return s;
}

PROMEKI_NAMESPACE_END
