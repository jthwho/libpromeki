/**
 * @file      subtitle.cpp
 * @copyright Howard Logic. All rights reserved.
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
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Pimpl definitions
// ============================================================================

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
                /// @brief Optional pixel-space bounding-box hint.
                Rect2Di32 region;
                /// @brief Optional speaker / voice identifier.
                String speaker;
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
                return std::chrono::duration_cast<std::chrono::milliseconds>(ts.value().time_since_epoch()).count();
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
} // namespace

// ============================================================================
// SubtitleSpan — diagnostics + DataStream
// ============================================================================

JsonObject SubtitleSpan::toJson() const {
        JsonObject obj;
        obj.set("text", _text);
        if (_bold) obj.set("bold", true);
        if (_italic) obj.set("italic", true);
        if (_underline) obj.set("underline", true);
        if (_color.isValid()) obj.set("color", _color.toString());
        return obj;
}

String SubtitleSpan::toString() const {
        String s = "SubtitleSpan(\"";
        constexpr size_t kMaxTextChars = 48;
        if (_text.size() <= kMaxTextChars) {
                s += _text;
        } else {
                s += _text.substr(0, kMaxTextChars);
                s += "...";
        }
        s += "\"";
        if (_bold) s += " B";
        if (_italic) s += " I";
        if (_underline) s += " U";
        if (_color.isValid()) {
                s += " color=";
                s += _color.toString();
        }
        s += ")";
        return s;
}

DataStream &operator<<(DataStream &stream, const SubtitleSpan &span) {
        stream.writeTag(DataStream::TypeSubtitleSpan);
        stream << span.text();
        stream << packStyleFlags(span);
        stream << span.color();
        return stream;
}

DataStream &operator>>(DataStream &stream, SubtitleSpan &span) {
        if (!stream.readTag(DataStream::TypeSubtitleSpan)) {
                span = SubtitleSpan();
                return stream;
        }
        String  text;
        uint8_t flags = 0;
        Color   color;
        stream >> text;
        stream >> flags;
        stream >> color;
        span = SubtitleSpan(std::move(text), (flags & 0x01) != 0, (flags & 0x02) != 0, (flags & 0x04) != 0,
                            color);
        return stream;
}

// ============================================================================
// Subtitle — construction / special members
// ============================================================================

Subtitle::Subtitle() : _d(SharedPtr<SubtitleImpl>::create()) {}

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
const Rect2Di32          &Subtitle::region() const { return _d->region; }
const String             &Subtitle::speaker() const { return _d->speaker; }
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
void Subtitle::setRegion(const Rect2Di32 &v) { _d.modify()->region = v; }
void Subtitle::setSpeaker(const String &v) { _d.modify()->speaker = v; }
void Subtitle::setMetadata(const Metadata &v) { _d.modify()->metadata = v; }

bool Subtitle::isEmpty() const {
        return _d->flatText.isEmpty() && _d->start == TimeStamp() && _d->end == TimeStamp();
}

bool Subtitle::isActiveAt(const TimeStamp &t) const {
        return t.value() >= _d->start.value() && t.value() < _d->end.value();
}

bool Subtitle::operator==(const Subtitle &o) const {
        return _d->start == o._d->start && _d->end == o._d->end && _d->spans == o._d->spans
                && _d->anchor.value() == o._d->anchor.value() && _d->region == o._d->region
                && _d->speaker == o._d->speaker && _d->metadata == o._d->metadata;
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
        if (_d->anchor.value() != SubtitleAnchor::Default.value()) {
                obj.set("anchor", _d->anchor.valueName());
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
        if (_d->anchor.value() != SubtitleAnchor::Default.value()) {
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
// Subtitle — DataStream operators
// ============================================================================

void writeSubtitleData(DataStream &stream, const Subtitle &sub) {
        // Wire layout (each field tagged via its own DataStream operator):
        //   TimeStamp start          (TypeTimeStamp)
        //   TimeStamp end            (TypeTimeStamp)
        //   int32_t   anchor.value() (TypeS32)
        //   Rect2Di32 region         (free-function operator<< template)
        //   String    speaker        (TypeString)
        //   Metadata  metadata       (VariantDatabase template operator<<)
        //   List<SubtitleSpan> spans (TypeList of TypeSubtitleSpan)
        //
        // The cached @ref Subtitle::text is reconstructed from spans on
        // read, so it does not need its own wire field.
        stream << sub.start();
        stream << sub.end();
        stream << static_cast<int32_t>(sub.anchor().value());
        stream << sub.region();
        stream << sub.speaker();
        stream << sub.metadata();
        stream << sub.spans();
}

Subtitle readSubtitleData(DataStream &stream) {
        TimeStamp          start;
        TimeStamp          end;
        int32_t            anchorValue = 0;
        Rect2Di32          region;
        String             speaker;
        Metadata           metadata;
        SubtitleSpan::List spans;
        stream >> start;
        stream >> end;
        stream >> anchorValue;
        stream >> region;
        stream >> speaker;
        stream >> metadata;
        stream >> spans;
        return Subtitle(start, end, std::move(spans), SubtitleAnchor(anchorValue), region, std::move(speaker),
                        std::move(metadata));
}

DataStream &operator<<(DataStream &stream, const Subtitle &sub) {
        stream.writeTag(DataStream::TypeSubtitle);
        writeSubtitleData(stream, sub);
        return stream;
}

DataStream &operator>>(DataStream &stream, Subtitle &sub) {
        if (!stream.readTag(DataStream::TypeSubtitle)) {
                sub = Subtitle();
                return stream;
        }
        sub = readSubtitleData(stream);
        return stream;
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
        std::vector<Subtitle> tmp;
        tmp.reserve(d->entries.size());
        for (size_t i = 0; i < d->entries.size(); ++i) tmp.push_back(d->entries[i]);
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
                const int64_t ns =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(ts.value().time_since_epoch())
                                .count();
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
