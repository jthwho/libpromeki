/**
 * @file      audiochannelmap.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/pair.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/result.h>
#include <promeki/error.h>
#include <promeki/datastream.h>
#include <promeki/enums.h>
#include <promeki/audiostreamdesc.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-channel @c (AudioStreamDesc, ChannelRole) assignment.
 * @ingroup proav
 *
 * @c AudioChannelMap is an ordered list of channels, with each
 * channel carrying both a @ref ChannelRole (positional / functional
 * role) and an @ref AudioStreamDesc (the logical stream the channel
 * belongs to).  The pairing lets a single audio block carry
 * multiple program streams simultaneously — a 5.1 Main bed
 * alongside a Commentary mono channel, for instance — without
 * needing one buffer per stream.
 *
 * @par Construction
 *
 * Several constructors are offered for common shapes:
 *
 *  - From a @ref ChannelRole list (or @c std::initializer_list) —
 *    every channel takes the @ref AudioStreamDesc::Undefined stream.
 *  - From a @c (stream, roles) pair — every channel takes the
 *    same explicit stream.
 *  - From a list of fully-formed @ref Entry pairs — full per-channel
 *    control over both stream and role.
 *
 * Convenience factories @ref defaultForChannels seed the canonical
 * layouts (Mono / Stereo / 5.1 / 7.1 / 7.1.4 / FOA) for both the
 * @c Undefined-stream case and a caller-supplied stream.
 *
 * @par String form
 *
 * A map round-trips through a per-channel comma-separated string:
 *
 * @code
 * "Stereo"                              // single Undefined-stream well-known
 * "Main:5.1"                            // single explicit stream well-known
 * "FrontLeft,FrontRight"                // explicit per-channel, all Undefined
 * "Main:FrontLeft,Main:FrontRight,Commentary:Mono"  // mixed streams
 * "FrontLeft,Commentary:Mono"           // mixed streams (first Undefined)
 * @endcode
 *
 * Each per-channel item is either @c "StreamName:RoleName" or just
 * @c "RoleName" (in which case the stream is @c Undefined).
 * @ref toString prefers the well-known shortcut when the map matches
 * one (single stream throughout, role list matches a well-known
 * layout), falling back to the per-channel comma form otherwise.
 *
 * @par Equality
 *
 * Two maps compare equal when they are the same length and every
 * entry's stream and role match in the same order.
 *
 * @par Thread Safety
 *
 * Distinct instances are independent; concurrent mutation of one
 * instance must be externally synchronized.
 *
 * @see AudioStreamDesc, ChannelRole, AudioDesc
 */
class AudioChannelMap {
                PROMEKI_SHARED_FINAL(AudioChannelMap)
        public:
                /** @brief One entry: a @c (stream, role) pair for a single channel. */
                using Entry = Pair<AudioStreamDesc, ChannelRole>;

                /** @brief Per-channel entry list. */
                using EntryList = ::promeki::List<Entry>;

                /** @brief List of @ref AudioChannelMap values. */
                using List = ::promeki::List<AudioChannelMap>;

                /** @brief Shared-pointer alias for @ref AudioChannelMap. */
                using Ptr = SharedPtr<AudioChannelMap>;

                /** @brief List of shared @ref AudioChannelMap pointers. */
                using PtrList = ::promeki::List<Ptr>;

                /**
                 * @brief One entry in the well-known layout dictionary.
                 *
                 * @c name is the canonical layout name accepted by
                 * @ref fromString and emitted by @ref toString /
                 * @ref wellKnownName when this layout matches.
                 *
                 * @c roles is the canonical role list — well-known
                 * layouts are stream-agnostic; the stream is selected
                 * at instantiation time.
                 */
                struct WellKnownLayout {
                                String                       name;
                                ::promeki::List<ChannelRole> roles;
                };

                /** @brief Returns the dictionary of well-known layouts. */
                static ::promeki::List<WellKnownLayout> wellKnownLayouts();

                /**
                 * @brief Parses a string into an @c AudioChannelMap.
                 *
                 * Accepted forms:
                 *
                 *  - Well-known layout shortcut: @c "Stereo", @c "5.1",
                 *    @c "7.1.4", @c "FOA", … — every channel takes
                 *    @ref AudioStreamDesc::Undefined.
                 *  - Stream-prefixed well-known shortcut:
                 *    @c "Main:5.1", @c "Commentary:Mono", … — every
                 *    channel takes the named stream.
                 *  - Per-channel comma list: @c "FrontLeft,FrontRight"
                 *    or @c "Main:FrontLeft,Main:FrontRight,Commentary:Mono".
                 *    Each item is @c "StreamName:RoleName" or just
                 *    @c "RoleName" (Undefined stream).
                 *
                 * Whitespace around commas / colons is permitted.
                 *
                 * @param str The string to parse.
                 * @return The parsed map, or @c Error::Invalid if @p str
                 *         is unrecognized.
                 */
                static Result<AudioChannelMap> fromString(const String &str);

                /**
                 * @brief Returns the canonical layout for @p channels.
                 *
                 * Returns the first entry in @ref wellKnownLayouts whose
                 * role list has @p channels entries.  Several channel
                 * counts have more than one well-known layout — the
                 * registration order in the dictionary defines which
                 * layout @em wins:
                 *
                 *   - 1  → Mono
                 *   - 2  → Stereo
                 *   - 3  → 2.1            (before 3.0)
                 *   - 4  → Quad           (before 4.0, FOA)
                 *   - 5  → 5.0
                 *   - 6  → 5.1            (before 6.0)
                 *   - 7  → 6.1            (before 7.0)
                 *   - 8  → 7.1            (before 5.1.2)
                 *   - 10 → 5.1.4          (before 7.1.2)
                 *   - 12 → 7.1.4
                 *
                 * Counts not listed return @p channels Undefined-stream /
                 * @ref ChannelRole::Unused entries — the caller is
                 * expected to fill in roles explicitly.
                 *
                 * Callers that need a non-default layout (e.g. @c "3.0"
                 * for a 3-channel buffer or @c "5.1.2" for 8 channels)
                 * must build the map explicitly via @ref fromString or
                 * the @ref AudioChannelMap constructors rather than
                 * relying on this helper.
                 *
                 * Every entry in the result uses the
                 * @ref AudioStreamDesc::Undefined stream.
                 */
                static AudioChannelMap defaultForChannels(size_t channels);

                /**
                 * @brief Returns the canonical layout for @p channels in @p stream.
                 *
                 * Same as @ref defaultForChannels(size_t) but pins
                 * every entry to @p stream rather than the
                 * @c Undefined sentinel.  Useful when assembling a
                 * descriptor for a specific named program stream.
                 */
                static AudioChannelMap defaultForChannels(size_t channels, const AudioStreamDesc &stream);

                /** @brief Default-constructs an empty map (zero channels). */
                AudioChannelMap() = default;

                /**
                 * @brief Constructs a map from an explicit role list,
                 *        every channel in @ref AudioStreamDesc::Undefined.
                 */
                explicit AudioChannelMap(::promeki::List<ChannelRole> roles) {
                        _entries.reserve(roles.size());
                        for (const ChannelRole &r : roles) _entries.pushToBack(Entry(AudioStreamDesc(), r));
                }

                /**
                 * @brief Constructs a map from a brace-init list of roles,
                 *        every channel in @ref AudioStreamDesc::Undefined.
                 */
                AudioChannelMap(std::initializer_list<ChannelRole> roles) {
                        _entries.reserve(roles.size());
                        for (const ChannelRole &r : roles) _entries.pushToBack(Entry(AudioStreamDesc(), r));
                }

                /**
                 * @brief Constructs a single-stream map from a role list.
                 * @param stream The stream every channel belongs to.
                 * @param roles  Role list, one entry per channel.
                 */
                AudioChannelMap(const AudioStreamDesc &stream, ::promeki::List<ChannelRole> roles) {
                        _entries.reserve(roles.size());
                        for (const ChannelRole &r : roles) _entries.pushToBack(Entry(stream, r));
                }

                /**
                 * @brief Constructs a single-stream map from a brace-init role list.
                 */
                AudioChannelMap(const AudioStreamDesc &stream, std::initializer_list<ChannelRole> roles) {
                        _entries.reserve(roles.size());
                        for (const ChannelRole &r : roles) _entries.pushToBack(Entry(stream, r));
                }

                /**
                 * @brief Constructs a map from a fully-formed entry list.
                 *
                 * Use when the caller needs per-channel control over
                 * both the stream and the role — for example when
                 * mixing a 5.1 main stream with a mono commentary
                 * track in the same audio block.
                 */
                explicit AudioChannelMap(EntryList entries) : _entries(std::move(entries)) {}

                /** @brief Constructs a map from a brace-init list of @ref Entry pairs. */
                AudioChannelMap(std::initializer_list<Entry> entries) : _entries(entries) {}

                /** @brief Returns true if the map has at least one channel. */
                bool isValid() const { return !_entries.isEmpty(); }

                /** @brief Returns the number of channels. */
                size_t channels() const { return _entries.size(); }

                /** @brief Returns the per-channel @ref Entry list. */
                const EntryList &entries() const { return _entries; }

                /** @brief Returns the role at @p index, or @c Unused on out-of-range. */
                ChannelRole role(size_t index) const {
                        if (index >= _entries.size()) return ChannelRole::Unused;
                        return _entries[index].second();
                }

                /** @brief Returns the stream at @p index, or @c Undefined on out-of-range. */
                AudioStreamDesc stream(size_t index) const {
                        if (index >= _entries.size()) return AudioStreamDesc();
                        return _entries[index].first();
                }

                /** @brief Returns the @ref Entry at @p index, or @c (Undefined, Unused) on out-of-range. */
                Entry entry(size_t index) const {
                        if (index >= _entries.size()) return Entry(AudioStreamDesc(), ChannelRole::Unused);
                        return _entries[index];
                }

                /**
                 * @brief Sets the role at @p index, growing with @c (Undefined, Unused) fillers.
                 *
                 * Preserves the channel's existing stream when @p index
                 * is already populated.
                 */
                void setRole(size_t index, const ChannelRole &role);

                /**
                 * @brief Sets the stream at @p index, growing with @c (Undefined, Unused) fillers.
                 *
                 * Preserves the channel's existing role when @p index
                 * is already populated.
                 */
                void setStream(size_t index, const AudioStreamDesc &stream);

                /** @brief Sets both stream and role at @p index, growing as needed. */
                void setEntry(size_t index, const AudioStreamDesc &stream, const ChannelRole &role);

                /**
                 * @brief Returns the index of the first channel with @p role,
                 *        regardless of stream.
                 * @return Zero-based index, or @c -1 if @p role is not present.
                 */
                int indexOf(const ChannelRole &role) const;

                /**
                 * @brief Returns the index of the first channel matching both
                 *        @p stream and @p role.
                 */
                int indexOf(const AudioStreamDesc &stream, const ChannelRole &role) const;

                /** @brief Returns true if any channel carries @p role. */
                bool contains(const ChannelRole &role) const { return indexOf(role) >= 0; }

                /**
                 * @brief Returns true when every channel shares the same stream.
                 *
                 * An empty map returns @c true (vacuously single-stream)
                 * for symmetry with downstream callers iterating over
                 * the entry list.
                 */
                bool isSingleStream() const;

                /**
                 * @brief Returns the common stream when @ref isSingleStream
                 *        is @c true, otherwise the @c Undefined sentinel.
                 *
                 * Empty maps return @c Undefined.
                 */
                AudioStreamDesc commonStream() const;

                /**
                 * @brief Returns the well-known layout name when the map
                 *        is single-stream and matches a registered layout.
                 *
                 * The form is @c "LayoutName" for the @c Undefined
                 * stream and @c "StreamName:LayoutName" otherwise.
                 * Returns an empty @c String for mixed-stream maps or
                 * unmatched role lists.
                 */
                String wellKnownName() const;

                /** @brief Returns true if the map matches a well-known layout. */
                bool isWellKnown() const { return !wellKnownName().isEmpty(); }

                /**
                 * @brief Returns the string form of this map.
                 *
                 * Emits the well-known shortcut when @ref wellKnownName
                 * matches; otherwise emits the per-channel comma form.
                 * Either form round-trips through @ref fromString.
                 */
                String toString() const;

                /** @brief Equality: same length and identical (stream, role) pairs in order. */
                bool operator==(const AudioChannelMap &o) const { return _entries == o._entries; }
                bool operator!=(const AudioChannelMap &o) const { return !(*this == o); }

        private:
                EntryList _entries;
};

/** @brief Writes an AudioChannelMap as tag + count + (streamName, role) pairs. */
inline DataStream &operator<<(DataStream &stream, const AudioChannelMap &map) {
        stream.writeTag(DataStream::TypeAudioChannelMap);
        stream << static_cast<uint32_t>(map.channels());
        for (const auto &entry : map.entries()) {
                stream << entry.first().name();
                stream << static_cast<int32_t>(entry.second().value());
        }
        return stream;
}

/** @brief Reads an AudioChannelMap from its tagged wire format. */
inline DataStream &operator>>(DataStream &stream, AudioChannelMap &map) {
        if (!stream.readTag(DataStream::TypeAudioChannelMap)) {
                map = AudioChannelMap();
                return stream;
        }
        uint32_t count = 0;
        stream >> count;
        AudioChannelMap::EntryList entries;
        entries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
                String  streamName;
                int32_t roleValue = 0;
                stream >> streamName >> roleValue;
                AudioStreamDesc s = (streamName.isEmpty() || streamName == "Undefined") ? AudioStreamDesc()
                                                                                        : AudioStreamDesc(streamName);
                entries.pushToBack(AudioChannelMap::Entry(s, ChannelRole(roleValue)));
        }
        if (stream.status() != DataStream::Ok) {
                map = AudioChannelMap();
                return stream;
        }
        map = AudioChannelMap(std::move(entries));
        return stream;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::AudioChannelMap);

/**
 * @brief Hash specialization for @ref promeki::AudioChannelMap.
 *
 * Combines the @c (stream, role) hashes for every entry using the
 * boost-style mix.  Empty maps hash to @c 0 by construction so they
 * collide with each other, which is the natural behaviour for an
 * empty container.
 */
template <> struct std::hash<promeki::AudioChannelMap> {
                size_t operator()(const promeki::AudioChannelMap &v) const noexcept {
                        size_t h = 0;
                        for (const auto &entry : v.entries()) {
                                size_t s = std::hash<promeki::AudioStreamDesc>()(entry.first());
                                size_t r = std::hash<promeki::ChannelRole>()(entry.second());
                                size_t combined = s ^ (r + 0x9e3779b97f4a7c15ULL + (s << 6) + (s >> 2));
                                h = h ^ (combined + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
                        }
                        return h;
                }
};
