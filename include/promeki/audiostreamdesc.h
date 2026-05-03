/**
 * @file      audiostreamdesc.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/datastream.h>
#include <promeki/logger.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/stringregistry.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Compile-time tag for the @ref AudioStreamDesc registry.
 * @ingroup proav
 *
 * Picks out the per-process @ref StringRegistry that backs
 * @ref AudioStreamDesc identity.  Stream names like @c "Main",
 * @c "Commentary", @c "Music_FR" all live in this namespace and
 * never collide with other @ref StringRegistry consumers.
 */
using AudioStreamDescRegistry = StringRegistry<"AudioStreamDesc">;

/**
 * @brief Identifier for a logical audio stream within a channel map.
 * @ingroup proav
 *
 * Every entry in an @ref AudioChannelMap pairs a @ref ChannelRole
 * (positional / functional role) with an @c AudioStreamDesc (the
 * logical stream the channel belongs to).  This lets a single
 * audio block carry multiple program streams simultaneously — a
 * 5.1 Main stream alongside a Commentary mono stream, for
 * instance — without needing a separate buffer per stream.
 *
 * @par Identity
 *
 * An @c AudioStreamDesc is a thin wrapper around a @c uint64_t
 * identifier registered in the
 * @ref AudioStreamDescRegistry.  ID @c 0 is reserved as
 * @b Undefined — the default stream for code that does not need
 * to distinguish multiple streams (the dominant case).  Non-zero
 * IDs are assigned by the registry from the FNV-1a hash of the
 * stream's name, so a given name always resolves to the same ID
 * within a process and across runs of the same build.
 *
 * @par Built-in stream names
 *
 * The library pre-registers a small set of common stream names
 * at static-init time so application code can refer to them
 * symbolically:
 *
 *  - @c Main, @c Music, @c Dialog, @c Effects, @c Commentary,
 *    @c DescriptiveAudio, @c Background, @c Foreground,
 *    @c VoiceOver
 *
 * Applications register additional names ad-hoc by constructing
 * @c AudioStreamDesc("MyStreamName") — the registry adds the
 * name on first use.
 *
 * @par Name constraints
 *
 * Stream names must @b not contain the @c ':' or @c ',' characters,
 * since those are the delimiters used by
 * @ref AudioChannelMap::toString and @ref AudioChannelMap::fromString
 * (e.g. @c "Main:5.1" or @c "Main:FrontLeft,Commentary:Mono").
 * Constructing an @c AudioStreamDesc from a name containing either
 * character logs a warning via @c promekiWarn and yields the
 * @ref Undefined sentinel; the same restriction applies to
 * @ref lookup.
 *
 * @par Example
 * @code
 * AudioStreamDesc undef;                   // Undefined (id == 0)
 * AudioStreamDesc main("Main");            // built-in
 * AudioStreamDesc cust("MyExtra");         // app-registered
 * assert(main.name() == "Main");
 * assert(undef.isUndefined());
 * @endcode
 *
 * @par Thread Safety
 *
 * @c AudioStreamDesc instances are plain values — copy / compare
 * concurrently without synchronization.  The backing
 * @ref StringRegistry is internally synchronized for the
 * registration path.
 */
class AudioStreamDesc {
        public:
                /** @brief ID type — a registered name's @ref StringRegistry slot. */
                using ID = uint64_t;

                /**
                 * @brief Reserved ID for the @b Undefined stream.
                 *
                 * Default-constructed @c AudioStreamDesc has @c id() ==
                 * @c Undefined and @ref isUndefined returns @c true.
                 * The library guarantees @c 0 is never assigned to a
                 * registered name (FNV-1a's offset basis is non-zero,
                 * and the registry probes past collisions), so @c 0
                 * is permanently free.
                 */
                static constexpr ID Undefined = 0;

                /** @brief Default-constructs the @ref Undefined stream. */
                AudioStreamDesc() = default;

                /**
                 * @brief Constructs a stream descriptor from a registered ID.
                 *
                 * No validation is performed; @ref name returns an
                 * empty string and @ref isValid returns @c false when
                 * @p id has not been registered.
                 */
                explicit AudioStreamDesc(ID id) : _id(id) {}

                /**
                 * @brief Constructs a stream descriptor from a name,
                 *        registering the name on first use.
                 *
                 * Empty names produce the @ref Undefined stream.
                 * Names containing the reserved delimiters @c ':' or
                 * @c ',' are rejected with a warning and also produce
                 * the @ref Undefined sentinel; those characters are
                 * used by @ref AudioChannelMap::toString /
                 * @ref AudioChannelMap::fromString to separate stream
                 * names from role names and channels from each other,
                 * so allowing them here would corrupt the round-trip.
                 */
                explicit AudioStreamDesc(const String &name) {
                        if (name.isEmpty()) {
                                _id = Undefined;
                                return;
                        }
                        if (containsReservedDelimiter(name)) {
                                promekiWarn("AudioStreamDesc: name '%s' contains "
                                            "a reserved delimiter (':' or ','); "
                                            "rejected — using Undefined",
                                            name.cstr());
                                _id = Undefined;
                                return;
                        }
                        _id = AudioStreamDescRegistry::instance().findOrCreateProbe(name);
                }

                /** @brief Returns the registered ID. */
                ID id() const { return _id; }

                /**
                 * @brief Returns the registered name, or @c "Undefined"
                 *        for the default stream.
                 *
                 * Returns an empty @c String when @c id() is non-zero
                 * but unregistered.
                 */
                String name() const {
                        if (_id == Undefined) return String("Undefined");
                        return AudioStreamDescRegistry::instance().name(_id);
                }

                /** @brief Returns true if this stream is the @ref Undefined sentinel. */
                bool isUndefined() const { return _id == Undefined; }

                /** @brief Returns true if this stream has a registered name. */
                bool isValid() const {
                        if (_id == Undefined) return true; // Undefined is a valid value
                        return !AudioStreamDescRegistry::instance().name(_id).isEmpty();
                }

                /** @brief Equality. */
                bool operator==(const AudioStreamDesc &o) const { return _id == o._id; }
                bool operator!=(const AudioStreamDesc &o) const { return _id != o._id; }
                bool operator<(const AudioStreamDesc &o) const { return _id < o._id; }

                /** @brief Returns the stream's name as its string form. */
                String toString() const { return name(); }

                /**
                 * @brief Parses a stream descriptor from a string,
                 *        registering the name on first use.
                 *
                 * Behaves like the registering constructor but
                 * surfaces the rejected-name path as an explicit
                 * @c Error::Invalid Result so callers (Variant
                 * parsers, JSON / config loaders) can distinguish
                 * "the input was malformed" from "the input was
                 * Undefined".
                 *
                 * @param name The string to parse.
                 * @return The parsed descriptor on success; or
                 *         @c Error::Invalid when @p name contains
                 *         the reserved @c ':' or @c ',' delimiters.
                 *         An empty or @c "Undefined" input is
                 *         accepted and yields the @ref Undefined
                 *         sentinel.
                 */
                static Result<AudioStreamDesc> fromString(const String &name) {
                        if (name.isEmpty() || name == "Undefined") return makeResult(AudioStreamDesc());
                        if (containsReservedDelimiter(name)) return makeError<AudioStreamDesc>(Error::Invalid);
                        return makeResult(AudioStreamDesc(AudioStreamDescRegistry::instance().findOrCreateProbe(name)));
                }

                /**
                 * @brief Looks up a stream descriptor without creating one.
                 *
                 * Rejects names containing the reserved @c ':' or
                 * @c ',' delimiters with the same warning as the
                 * registering constructor.
                 *
                 * @param name Stream name to look up.
                 * @return The descriptor when registered; otherwise the
                 *         @ref Undefined sentinel.
                 */
                static AudioStreamDesc lookup(const String &name) {
                        if (name.isEmpty() || name == "Undefined") return AudioStreamDesc();
                        if (containsReservedDelimiter(name)) {
                                promekiWarn("AudioStreamDesc::lookup: name '%s' "
                                            "contains a reserved delimiter (':' "
                                            "or ','); rejected — returning Undefined",
                                            name.cstr());
                                return AudioStreamDesc();
                        }
                        ID id = AudioStreamDescRegistry::instance().findId(name);
                        if (id == AudioStreamDescRegistry::InvalidID) return AudioStreamDesc();
                        return AudioStreamDesc(id);
                }

        private:
                ID _id = Undefined;

                /// Returns true if @p name contains a reserved channel-map delimiter.
                static bool containsReservedDelimiter(const String &name) {
                        return name.find(':') != String::npos || name.find(',') != String::npos;
                }
};

/** @brief Writes an AudioStreamDesc as tag + registered name (length-prefixed). */
inline DataStream &operator<<(DataStream &stream, const AudioStreamDesc &desc) {
        stream.writeTag(DataStream::TypeAudioStreamDesc);
        stream << desc.name();
        return stream;
}

/** @brief Reads an AudioStreamDesc from tag + name. */
inline DataStream &operator>>(DataStream &stream, AudioStreamDesc &desc) {
        if (!stream.readTag(DataStream::TypeAudioStreamDesc)) {
                desc = AudioStreamDesc();
                return stream;
        }
        String name;
        stream >> name;
        if (stream.status() != DataStream::Ok) {
                desc = AudioStreamDesc();
                return stream;
        }
        if (name.isEmpty() || name == "Undefined") {
                desc = AudioStreamDesc();
        } else {
                // Re-register the name so cross-process round-trips are
                // self-consistent — the receiving side may not have seen
                // this name yet.  Names containing reserved delimiters
                // produce Undefined and a warning (handled by the ctor).
                desc = AudioStreamDesc(name);
        }
        return stream;
}

PROMEKI_NAMESPACE_END

PROMEKI_FORMAT_VIA_TOSTRING(promeki::AudioStreamDesc);

/**
 * @brief Hash specialization so @ref promeki::AudioStreamDesc can serve
 *        as a key in @c HashMap / @c HashSet (and any @c std::unordered_*).
 *
 * Hashes the registered ID directly; the @c AudioStreamDescRegistry
 * already disambiguates names via FNV-1a + linear probing, so the IDs
 * are well-distributed and a passthrough hash is appropriate.
 */
template <> struct std::hash<promeki::AudioStreamDesc> {
                size_t operator()(const promeki::AudioStreamDesc &v) const noexcept {
                        return std::hash<uint64_t>()(v.id());
                }
};
