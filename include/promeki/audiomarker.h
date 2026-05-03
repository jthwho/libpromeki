/**
 * @file      audiomarker.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/datastream.h>
#include <promeki/enums.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/result.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief One sample-region annotation on an audio payload.
 * @ingroup proav
 *
 * @c AudioMarker locates a region of samples within a carrying
 * @ref PcmAudioPayload and classifies it with an @ref AudioMarkerType.
 * It is intentionally a small plain-value type: no shared pointer
 * variant, no internal allocation, copyable and comparable.  Markers
 * normally live inside an @ref AudioMarkerList stamped on a payload's
 * @ref Metadata::AudioMarkers — applications rarely construct one in
 * isolation.
 *
 * @par Region semantics
 *
 * @c offset is the zero-based sample index where the region starts;
 * @c length is the number of samples it covers.  Both values are in
 * the units of the payload's @ref AudioDesc::sampleCount, i.e. one
 * unit per @em frame regardless of channel count.  A region may
 * cover @c [offset, offset + length) sample indices; the half-open
 * interval mirrors the rest of the library (e.g.
 * @ref AudioPayload::sampleCount).
 *
 * A boundary-only marker (e.g. @ref AudioMarkerType::Discontinuity)
 * sets @c length == 0 — the marker locates a point on the timeline
 * without claiming any samples.
 *
 * @par Thread Safety
 *
 * Plain value type — copy and compare freely between threads.
 * Concurrent mutation of one instance must be externally
 * synchronized.
 */
class AudioMarker {
        public:
                /** @brief Default-constructs a zero-length @c Unknown marker at offset 0. */
                AudioMarker() = default;

                /** @brief Constructs a marker at @p offset of @p length samples, classified as @p type. */
                AudioMarker(int64_t offset, int64_t length, AudioMarkerType type)
                    : _offset(offset), _length(length), _type(type) {}

                /** @brief Returns the sample-offset of the region's first sample. */
                int64_t        offset() const { return _offset; }

                /** @brief Sets the region's first-sample offset. */
                void           setOffset(int64_t v) { _offset = v; }

                /** @brief Returns the number of samples the region covers (zero for boundary markers). */
                int64_t        length() const { return _length; }

                /** @brief Sets the region's sample length. */
                void           setLength(int64_t v) { _length = v; }

                /** @brief Returns the marker's classification. */
                AudioMarkerType type() const { return _type; }

                /** @brief Sets the marker's classification. */
                void            setType(AudioMarkerType v) { _type = v; }

                /**
                 * @brief Returns true if this marker would be considered
                 *        equal to @p o by every accessor.
                 *
                 * Comparison is field-wise — @c offset, @c length, and
                 * @c type all match.
                 */
                bool operator==(const AudioMarker &o) const {
                        return _offset == o._offset && _length == o._length && _type == o._type;
                }
                bool operator!=(const AudioMarker &o) const { return !(*this == o); }

                /**
                 * @brief Returns @c "Type@offset+length", e.g.
                 *        @c "SilenceFill\@128+256".
                 *
                 * Compact form preferred for log lines and the
                 * @ref AudioMarkerList round-trip.  A zero-length marker
                 * is rendered as @c "Type\@offset" (no @c +length suffix).
                 */
                String toString() const {
                        if (_length == 0) {
                                return _type.valueName() + "@" + String::number(_offset);
                        }
                        return _type.valueName() + "@" + String::number(_offset) + "+" + String::number(_length);
                }

                /**
                 * @brief Parses a marker from its @ref toString form.
                 *
                 * Accepts both forms: @c "Type\@offset+length" and
                 * @c "Type\@offset" (zero-length).  Whitespace is not
                 * permitted around the @c '@' or @c '+' delimiters.
                 *
                 * @param str The string to parse (e.g. @c "SilenceFill\@128+256").
                 * @return The parsed marker, or @c Error::Invalid when
                 *         @p str does not match the grammar or the type
                 *         name is not recognised.
                 */
                static Result<AudioMarker> fromString(const String &str) {
                        size_t at = str.find('@');
                        if (at == String::npos) return makeError<AudioMarker>(Error::Invalid);
                        String typeName = str.substr(0, at);
                        String tail     = str.substr(at + 1);
                        size_t plus     = tail.find('+');
                        String offsetStr;
                        String lengthStr;
                        if (plus == String::npos) {
                                offsetStr = tail;
                                lengthStr = String("0");
                        } else {
                                offsetStr = tail.substr(0, plus);
                                lengthStr = tail.substr(plus + 1);
                        }
                        Error   ce;
                        int64_t offset = offsetStr.to<int64_t>(&ce);
                        if (ce.isError()) return makeError<AudioMarker>(Error::Invalid);
                        int64_t length = lengthStr.to<int64_t>(&ce);
                        if (ce.isError()) return makeError<AudioMarker>(Error::Invalid);
                        AudioMarkerType t(typeName);
                        if (!t.hasListedValue()) return makeError<AudioMarker>(Error::Invalid);
                        return makeResult(AudioMarker(offset, length, t));
                }

        private:
                int64_t         _offset = 0;
                int64_t         _length = 0;
                AudioMarkerType _type;
};

/**
 * @brief Ordered list of @ref AudioMarker annotations on a payload.
 * @ingroup proav
 *
 * @c AudioMarkerList is the value carried by
 * @ref Metadata::AudioMarkers — a producer-supplied annotation track
 * that locates noteworthy regions within an audio payload (silence
 * fills, concealed packet loss, glitches, discontinuities) and
 * classifies each with an @ref AudioMarkerType.
 *
 * Markers are stored in the order the producer added them.  The list
 * does not enforce non-overlap or sortedness — overlapping regions
 * are valid (e.g. a @c ConcealedLoss inside a larger
 * @c SilenceFill) and the producer is free to append in arrival
 * order.  Consumers that need a sorted view should sort a copy.
 *
 * @par String form
 *
 * The list round-trips through a comma-separated list of
 * @ref AudioMarker::toString forms:
 *
 * @code
 * "SilenceFill@128+256, Discontinuity@384"
 * @endcode
 *
 * The empty list serializes to the empty string.  Whitespace around
 * the commas is permitted on parse.
 *
 * @par Thread Safety
 *
 * Plain value type with @ref PROMEKI_SHARED_FINAL — copy / compare
 * freely between threads, take a @c ::Ptr if shared ownership is
 * desired.  Concurrent mutation of one instance must be externally
 * synchronized.
 */
class AudioMarkerList {
                PROMEKI_SHARED_FINAL(AudioMarkerList)
        public:
                /** @brief Single-marker entry alias. */
                using Entry = AudioMarker;

                /** @brief Per-entry list. */
                using EntryList = ::promeki::List<Entry>;

                /** @brief List of @c AudioMarkerList values. */
                using List = ::promeki::List<AudioMarkerList>;

                /** @brief Shared-pointer alias. */
                using Ptr = SharedPtr<AudioMarkerList>;

                /** @brief List of shared @c AudioMarkerList pointers. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Default-constructs an empty list. */
                AudioMarkerList() = default;

                /** @brief Constructs from an explicit entry list. */
                explicit AudioMarkerList(EntryList entries) : _entries(std::move(entries)) {}

                /** @brief Constructs from an initializer list of entries. */
                AudioMarkerList(std::initializer_list<Entry> il) : _entries(il) {}

                /** @brief Returns the underlying entry list. */
                const EntryList &entries() const { return _entries; }

                /** @brief Replaces all entries. */
                void             setEntries(EntryList entries) { _entries = std::move(entries); }

                /** @brief Appends a marker to the list. */
                void             append(const Entry &e) { _entries.pushToBack(e); }

                /** @brief Constructs and appends a marker in place. */
                void             append(int64_t offset, int64_t length, AudioMarkerType type) {
                        _entries.pushToBack(Entry(offset, length, type));
                }

                /** @brief Returns the number of markers in the list. */
                size_t           size() const { return _entries.size(); }

                /** @brief Returns @c true if the list contains no markers. */
                bool             isEmpty() const { return _entries.isEmpty(); }

                /** @brief Removes every marker from the list. */
                void             clear() { _entries.clear(); }

                /**
                 * @brief Total samples covered by markers of @p type.
                 *
                 * Sums the @ref AudioMarker::length of every entry
                 * whose @ref AudioMarker::type equals @p type.
                 * Overlapping regions are not deduplicated — a sample
                 * covered by two markers contributes twice.
                 */
                int64_t          totalLengthFor(AudioMarkerType type) const {
                        int64_t sum = 0;
                        for (const auto &e : _entries) {
                                if (e.type() == type) sum += e.length();
                        }
                        return sum;
                }

                /** @brief Number of markers whose @ref AudioMarker::type equals @p type. */
                size_t           countFor(AudioMarkerType type) const {
                        size_t n = 0;
                        for (const auto &e : _entries) {
                                if (e.type() == type) ++n;
                        }
                        return n;
                }

                /**
                 * @brief Returns the comma-separated string round-trip form.
                 *
                 * Each entry is rendered via @ref AudioMarker::toString
                 * and joined with @c ", ".  The empty list yields an
                 * empty @c String.
                 */
                String toString() const {
                        if (_entries.isEmpty()) return String();
                        String out;
                        bool   first = true;
                        for (const auto &e : _entries) {
                                if (!first) out += ", ";
                                out += e.toString();
                                first = false;
                        }
                        return out;
                }

                /**
                 * @brief Parses a comma-separated marker list.
                 *
                 * Each item must match @ref AudioMarker::fromString.
                 * The empty input string parses to an empty list.
                 *
                 * @param str Comma-separated marker list (e.g.
                 *            @c "SilenceFill\@0+32, Discontinuity\@64").
                 * @return The parsed list, or @c Error::Invalid on the
                 *         first item that fails to parse.
                 */
                static Result<AudioMarkerList> fromString(const String &str) {
                        String trimmed = str.trim();
                        if (trimmed.isEmpty()) return makeResult(AudioMarkerList());
                        EntryList   entries;
                        StringList  parts = trimmed.split(",");
                        for (const auto &p : parts) {
                                String item = p.trim();
                                if (item.isEmpty()) continue;
                                auto r = AudioMarker::fromString(item);
                                if (error(r).isError()) return makeError<AudioMarkerList>(Error::Invalid);
                                entries.pushToBack(value(r));
                        }
                        return makeResult(AudioMarkerList(std::move(entries)));
                }

                /** @brief Equality: same length and identical entries in order. */
                bool operator==(const AudioMarkerList &o) const { return _entries == o._entries; }
                bool operator!=(const AudioMarkerList &o) const { return !(*this == o); }

        private:
                EntryList _entries;
};

/** @brief Writes an AudioMarkerList as tag + count + N (offset, length, type). */
inline DataStream &operator<<(DataStream &stream, const AudioMarkerList &list) {
        stream.writeTag(DataStream::TypeAudioMarkerList);
        stream << static_cast<uint32_t>(list.size());
        for (const auto &e : list.entries()) {
                stream << static_cast<int64_t>(e.offset());
                stream << static_cast<int64_t>(e.length());
                stream << static_cast<int32_t>(e.type().value());
        }
        return stream;
}

/** @brief Reads an AudioMarkerList from its tagged wire format. */
inline DataStream &operator>>(DataStream &stream, AudioMarkerList &list) {
        if (!stream.readTag(DataStream::TypeAudioMarkerList)) {
                list = AudioMarkerList();
                return stream;
        }
        uint32_t count = 0;
        stream >> count;
        AudioMarkerList::EntryList entries;
        entries.reserve(count);
        for (uint32_t i = 0; i < count && stream.status() == DataStream::Ok; ++i) {
                int64_t offset    = 0;
                int64_t length    = 0;
                int32_t typeValue = 0;
                stream >> offset >> length >> typeValue;
                entries.pushToBack(AudioMarker(offset, length, AudioMarkerType(typeValue)));
        }
        if (stream.status() != DataStream::Ok) {
                list = AudioMarkerList();
                return stream;
        }
        list = AudioMarkerList(std::move(entries));
        return stream;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::AudioMarker);
PROMEKI_FORMAT_VIA_TOSTRING(promeki::AudioMarkerList);

/**
 * @brief Hash specialization for @ref promeki::AudioMarker.
 *
 * Combines @c offset, @c length, and the @ref AudioMarkerType value
 * via the boost-style mix.  Markers are equal exactly when all
 * three fields match, so hashing all three keeps unequal markers
 * apart.
 */
template <> struct std::hash<promeki::AudioMarker> {
                size_t operator()(const promeki::AudioMarker &m) const noexcept {
                        size_t h = std::hash<int64_t>()(m.offset());
                        size_t l = std::hash<int64_t>()(m.length());
                        size_t t = std::hash<int32_t>()(m.type().value());
                        size_t combined = l ^ (t + 0x9e3779b97f4a7c15ULL + (l << 6) + (l >> 2));
                        return h ^ (combined + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
                }
};

/**
 * @brief Hash specialization for @ref promeki::AudioMarkerList.
 *
 * Combines per-entry hashes via the same boost-style mix as the
 * single-marker hash.  Empty lists hash to @c 0 by construction.
 */
template <> struct std::hash<promeki::AudioMarkerList> {
                size_t operator()(const promeki::AudioMarkerList &v) const noexcept {
                        size_t h = 0;
                        for (const auto &e : v.entries()) {
                                size_t s = std::hash<promeki::AudioMarker>()(e);
                                h        = h ^ (s + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
                        }
                        return h;
                }
};
