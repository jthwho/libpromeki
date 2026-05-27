/**
 * @file      transcript.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <algorithm>
#include <promeki/datastream.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/transcript.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Pimpl definitions
// ============================================================================

struct TranscriptImpl {
                PROMEKI_SHARED_FINAL(TranscriptImpl)

                /// @brief Recognised words in emission order.
                TranscriptWord::List words;
                /// @brief Speaker / voice identifier; empty when absent.
                String speaker;
                /// @brief Detected BCP 47 tag; empty when absent.
                String language;
                /// @brief Utterance-level confidence in @c [0, 1].
                float confidence = 1.0f;
                /// @brief Interim-hypothesis marker; default false (finalised).
                bool partial = false;
                /// @brief Engine-specific extension metadata.
                Metadata metadata;
                /// @brief Cached concatenation of every word's text.
                String flatText;

                TranscriptImpl() = default;

                /// @brief Rebuilds @c flatText from the current @c words.
                ///
                /// Joins words with a single space, omitting the
                /// separator before words whose text already starts
                /// with whitespace and after empty entries.  Punctuation
                /// tokens (no leading space in the original word text)
                /// stay glued to the preceding word.
                void rebuildFlatText() {
                        flatText.clear();
                        for (size_t i = 0; i < words.size(); ++i) {
                                const String &t = words[i].text();
                                if (t.isEmpty()) continue;
                                if (!flatText.isEmpty()) {
                                        // Insert a space unless the word
                                        // already starts with whitespace or
                                        // is a punctuation-only token glued
                                        // to the previous word.
                                        char c0 = t[0];
                                        bool isPunct = (c0 == '.' || c0 == ',' || c0 == ';' || c0 == ':'
                                                        || c0 == '!' || c0 == '?');
                                        if (c0 != ' ' && c0 != '\t' && c0 != '\n' && !isPunct) {
                                                flatText += ' ';
                                        }
                                }
                                flatText += t;
                        }
                }
};

struct TranscriptListImpl {
                PROMEKI_SHARED_FINAL(TranscriptListImpl)

                List<Transcript> entries;
                /// @brief Cached ascending-by-start sort state.
                bool sortedByStart = true;

                TranscriptListImpl() = default;
};

// ============================================================================
// TranscriptWord — diagnostics + DataStream
// ============================================================================

namespace {

        // Local helper: ns → millisecond integer for JSON output.
        int64_t timeStampToMs(const TimeStamp &ts) {
                if (!ts.isValid()) return 0;
                return ts.nanoseconds() / 1'000'000;
        }

} // namespace

JsonObject TranscriptWord::toJson() const {
        JsonObject obj;
        obj.set("text", _text);
        obj.set("startMs", timeStampToMs(_start));
        obj.set("endMs", timeStampToMs(_end));
        if (_confidence < 1.0f) obj.set("confidence", static_cast<double>(_confidence));
        return obj;
}

String TranscriptWord::toString() const {
        String s = "TranscriptWord(\"";
        constexpr size_t kMaxTextChars = 32;
        if (_text.size() <= kMaxTextChars) {
                s += _text;
        } else {
                s += _text.substr(0, kMaxTextChars);
                s += "...";
        }
        s += "\" [";
        s += String::number(timeStampToMs(_start));
        s += "ms..";
        s += String::number(timeStampToMs(_end));
        s += "ms]";
        if (_confidence < 1.0f) {
                s += " conf=";
                s += String::number(_confidence);
        }
        s += ")";
        return s;
}

// Wire layout (each field tagged via its own DataStream operator):
//   String    text
//   TimeStamp start
//   TimeStamp end
//   float     confidence
Error TranscriptWord::writeToStream(DataStream &s) const {
        s << _text;
        s << _start;
        s << _end;
        s << _confidence;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <> Result<TranscriptWord> TranscriptWord::readFromStream<1>(DataStream &s) {
        TranscriptWord w;
        s >> w._text;
        s >> w._start;
        s >> w._end;
        s >> w._confidence;
        if (s.status() != DataStream::Ok) return makeError<TranscriptWord>(s.toError());
        return makeResult(std::move(w));
}

DataStream &operator<<(DataStream &stream, const TranscriptWord &word) {
        stream.beginFrame(DataTypeTranscriptWord, 1);
        word.writeToStream(stream);
        stream.endFrame();
        return stream;
}

DataStream &operator>>(DataStream &stream, TranscriptWord &word) {
        if (!stream.readFrame(DataTypeTranscriptWord)) {
                stream.setError(DataStream::ReadCorruptData,
                                "Expected DataTypeTranscriptWord tag");
                return stream;
        }
        auto r = TranscriptWord::readFromStream<1>(stream);
        if (error(r).isOk()) word = value(r);
        stream.endFrame();
        return stream;
}

// ============================================================================
// Transcript — construction / special members
// ============================================================================

Transcript::Transcript() : _d(SharedPtr<TranscriptImpl>::create()) {}

Transcript::Transcript(TranscriptWord::List words) : _d(SharedPtr<TranscriptImpl>::create()) {
        auto *d = _d.modify();
        d->words = std::move(words);
        d->rebuildFlatText();
}

Transcript::Transcript(TranscriptWord::List words, String speaker, String language, float confidence, bool partial)
    : _d(SharedPtr<TranscriptImpl>::create()) {
        auto *d = _d.modify();
        d->words = std::move(words);
        d->speaker = std::move(speaker);
        d->language = std::move(language);
        d->confidence = confidence;
        d->partial = partial;
        d->rebuildFlatText();
}

Transcript::Transcript(const Transcript &) = default;
Transcript::Transcript(Transcript &&) noexcept = default;
Transcript::~Transcript() = default;
Transcript &Transcript::operator=(const Transcript &) = default;
Transcript &Transcript::operator=(Transcript &&) noexcept = default;

// ============================================================================
// Transcript — accessors / mutators
// ============================================================================

const TranscriptWord::List &Transcript::words() const { return _d->words; }
const String               &Transcript::speaker() const { return _d->speaker; }
const String               &Transcript::language() const { return _d->language; }
float                       Transcript::confidence() const { return _d->confidence; }
bool                        Transcript::partial() const { return _d->partial; }
const Metadata             &Transcript::metadata() const { return _d->metadata; }
const String               &Transcript::text() const { return _d->flatText; }

TimeStamp Transcript::start() const {
        if (_d->words.isEmpty()) return TimeStamp();
        return _d->words[0].start();
}

TimeStamp Transcript::end() const {
        if (_d->words.isEmpty()) return TimeStamp();
        return _d->words[_d->words.size() - 1].end();
}

TimeStamp::Duration Transcript::duration() const {
        if (_d->words.isEmpty()) return TimeStamp::Duration::zero();
        return _d->words[_d->words.size() - 1].end().value() - _d->words[0].start().value();
}

bool Transcript::isEmpty() const { return _d->words.isEmpty(); }

void Transcript::setWords(TranscriptWord::List v) {
        auto *d = _d.modify();
        d->words = std::move(v);
        d->rebuildFlatText();
}

void Transcript::appendWord(TranscriptWord w) {
        auto *d = _d.modify();
        d->words.pushToBack(std::move(w));
        d->rebuildFlatText();
}

void Transcript::setSpeaker(String v) { _d.modify()->speaker = std::move(v); }
void Transcript::setLanguage(String v) { _d.modify()->language = std::move(v); }
void Transcript::setConfidence(float v) { _d.modify()->confidence = v; }
void Transcript::setPartial(bool v) { _d.modify()->partial = v; }
void Transcript::setMetadata(Metadata v) { _d.modify()->metadata = std::move(v); }

// ============================================================================
// Transcript — comparison / diagnostics
// ============================================================================

bool Transcript::operator==(const Transcript &o) const {
        return _d->words == o._d->words && _d->speaker == o._d->speaker && _d->language == o._d->language
                && _d->confidence == o._d->confidence && _d->partial == o._d->partial
                && _d->metadata == o._d->metadata;
}

JsonObject Transcript::toJson() const {
        JsonObject obj;
        obj.set("text", _d->flatText);
        obj.set("startMs", timeStampToMs(start()));
        obj.set("endMs", timeStampToMs(end()));
        if (_d->confidence < 1.0f) obj.set("confidence", static_cast<double>(_d->confidence));
        if (_d->partial) obj.set("partial", true);
        if (!_d->speaker.isEmpty()) obj.set("speaker", _d->speaker);
        if (!_d->language.isEmpty()) obj.set("language", _d->language);
        JsonArray wordsArr;
        for (size_t i = 0; i < _d->words.size(); ++i) wordsArr.add(_d->words[i].toJson());
        obj.set("words", wordsArr);
        if (_d->metadata.size() > 0) {
                // Same structured-JSON-for-Metadata caveat as Subtitle::toJson.
                obj.set("metadata", _d->metadata.toString());
        }
        return obj;
}

String Transcript::toString() const {
        String s = "Transcript([";
        s += String::number(timeStampToMs(start()));
        s += "ms..";
        s += String::number(timeStampToMs(end()));
        s += "ms] ";
        constexpr size_t kMaxTextChars = 64;
        if (_d->flatText.size() <= kMaxTextChars) {
                s += _d->flatText;
        } else {
                s += _d->flatText.substr(0, kMaxTextChars);
                s += "...";
        }
        if (_d->partial) s += " partial";
        if (!_d->speaker.isEmpty()) {
                s += " speaker=";
                s += _d->speaker;
        }
        if (!_d->language.isEmpty()) {
                s += " lang=";
                s += _d->language;
        }
        s += ")";
        return s;
}

// ============================================================================
// Transcript — DataStream serialization
// ============================================================================
//
// Wire layout (each field tagged via its own DataStream operator):
//   List<TranscriptWord> words
//   String               speaker
//   String               language
//   float                confidence
//   bool                 partial
//   Metadata             metadata
//
// The cached @ref Transcript::text is reconstructed from words on
// read, so it does not need its own wire field.

Error Transcript::writeToStream(DataStream &s) const {
        s << _d->words;
        s << _d->speaker;
        s << _d->language;
        s << _d->confidence;
        s << _d->partial;
        s << _d->metadata;
        return s.status() == DataStream::Ok ? Error::Ok : s.toError();
}

template <> Result<Transcript> Transcript::readFromStream<1>(DataStream &s) {
        TranscriptWord::List words;
        String               speaker;
        String               language;
        float                confidence = 1.0f;
        bool                 partial = false;
        Metadata             metadata;
        s >> words >> speaker >> language >> confidence >> partial >> metadata;
        if (s.status() != DataStream::Ok) return makeError<Transcript>(s.toError());
        return makeResult(
                Transcript(std::move(words), std::move(speaker), std::move(language), confidence, partial));
}

// ============================================================================
// TranscriptList — construction / special members
// ============================================================================

TranscriptList::TranscriptList() : _d(SharedPtr<TranscriptListImpl>::create()) {}

TranscriptList::TranscriptList(List<Transcript> entries) : _d(SharedPtr<TranscriptListImpl>::create()) {
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

TranscriptList::TranscriptList(const TranscriptList &) = default;
TranscriptList::TranscriptList(TranscriptList &&) noexcept = default;
TranscriptList::~TranscriptList() = default;
TranscriptList &TranscriptList::operator=(const TranscriptList &) = default;
TranscriptList &TranscriptList::operator=(TranscriptList &&) noexcept = default;

// ============================================================================
// TranscriptList — size / access
// ============================================================================

size_t                  TranscriptList::size() const { return _d->entries.size(); }
bool                    TranscriptList::isEmpty() const { return _d->entries.isEmpty(); }
const Transcript       &TranscriptList::at(size_t i) const { return _d->entries[i]; }
const Transcript       &TranscriptList::operator[](size_t i) const { return _d->entries[i]; }
const List<Transcript> &TranscriptList::entries() const { return _d->entries; }

// ============================================================================
// TranscriptList — mutators
// ============================================================================

void TranscriptList::append(const Transcript &t) {
        auto *d = _d.modify();
        if (d->sortedByStart && !d->entries.isEmpty()
            && t.start().value() < d->entries[d->entries.size() - 1].start().value()) {
                d->sortedByStart = false;
        }
        d->entries.pushToBack(t);
}

void TranscriptList::clear() {
        auto *d = _d.modify();
        d->entries.clear();
        d->sortedByStart = true; // Empty is trivially sorted.
}

void TranscriptList::reserve(size_t n) { _d.modify()->entries.reserve(n); }

void TranscriptList::sortByStart() {
        auto *d = _d.modify();
        if (d->sortedByStart) return;
        List<Transcript> tmp;
        tmp.reserve(d->entries.size());
        for (size_t i = 0; i < d->entries.size(); ++i) tmp.pushToBack(d->entries[i]);
        std::stable_sort(tmp.begin(), tmp.end(), [](const Transcript &a, const Transcript &b) {
                return a.start().value() < b.start().value();
        });
        d->entries.clear();
        for (auto &t : tmp) d->entries.pushToBack(std::move(t));
        d->sortedByStart = true;
}

// ============================================================================
// TranscriptList — search helpers
// ============================================================================

int64_t TranscriptList::findActiveAt(const TimeStamp &t) const {
        const auto &entries = _d->entries;
        if (entries.isEmpty()) return -1;

        if (_d->sortedByStart) {
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
                        const Transcript &tr = entries[static_cast<size_t>(i)];
                        if (tr.end().value() <= t.value()) continue;
                        if (tr.start().value() <= t.value()) first = i;
                }
                return first;
        }

        for (size_t i = 0; i < entries.size(); ++i) {
                const Transcript &tr = entries[i];
                if (tr.start().value() <= t.value() && t.value() < tr.end().value()) {
                        return static_cast<int64_t>(i);
                }
        }
        return -1;
}

int64_t TranscriptList::findNextAfter(const TimeStamp &t) const {
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

// ============================================================================
// TranscriptList — comparison / diagnostics
// ============================================================================

bool TranscriptList::operator==(const TranscriptList &o) const { return _d->entries == o._d->entries; }

JsonObject TranscriptList::toJson() const {
        JsonObject obj;
        obj.set("count", static_cast<int64_t>(_d->entries.size()));
        JsonArray arr;
        for (size_t i = 0; i < _d->entries.size(); ++i) arr.add(_d->entries[i].toJson());
        obj.set("entries", arr);
        return obj;
}

String TranscriptList::toString() const {
        String s = "TranscriptList(count=";
        s += String::number(_d->entries.size());
        s += ")";
        return s;
}

PROMEKI_NAMESPACE_END
