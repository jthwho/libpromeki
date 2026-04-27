/**
 * @file      audiochannelmap.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/audiochannelmap.h>

PROMEKI_NAMESPACE_BEGIN

namespace {
        // ---------------------------------------------------------------------
        // Static well-known layout table.  Built once at first call and
        // referenced for the lifetime of the process.
        // ---------------------------------------------------------------------
        const ::promeki::List<AudioChannelMap::WellKnownLayout> &knownLayoutsRef() {
                static const auto layouts = [] {
                        ::promeki::List<AudioChannelMap::WellKnownLayout> out;
                        const auto push = [&](const char *name, std::initializer_list<ChannelRole> rs) {
                                AudioChannelMap::WellKnownLayout entry;
                                entry.name = name;
                                entry.roles = ::promeki::List<ChannelRole>(rs);
                                out.pushToBack(std::move(entry));
                        };
                        push("Mono", {ChannelRole::Mono});
                        push("Stereo", {ChannelRole::FrontLeft, ChannelRole::FrontRight});
                        push("2.1", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::LFE});
                        push("3.0", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter});
                        push("Quad", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::BackLeft,
                                      ChannelRole::BackRight});
                        push("4.0", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::BackLeft,
                                     ChannelRole::BackRight});
                        push("5.0", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                                     ChannelRole::BackLeft, ChannelRole::BackRight});
                        push("5.1", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                                     ChannelRole::LFE, ChannelRole::BackLeft, ChannelRole::BackRight});
                        push("6.0", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                                     ChannelRole::SideLeft, ChannelRole::SideRight, ChannelRole::BackCenter});
                        push("6.1", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                                     ChannelRole::LFE, ChannelRole::BackCenter, ChannelRole::SideLeft,
                                     ChannelRole::SideRight});
                        push("7.0", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                                     ChannelRole::BackLeft, ChannelRole::BackRight, ChannelRole::SideLeft,
                                     ChannelRole::SideRight});
                        push("7.1", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                                     ChannelRole::LFE, ChannelRole::BackLeft, ChannelRole::BackRight,
                                     ChannelRole::SideLeft, ChannelRole::SideRight});
                        push("5.1.2", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                                       ChannelRole::LFE, ChannelRole::BackLeft, ChannelRole::BackRight,
                                       ChannelRole::TopFrontLeft, ChannelRole::TopFrontRight});
                        push("5.1.4", {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                                       ChannelRole::LFE, ChannelRole::BackLeft, ChannelRole::BackRight,
                                       ChannelRole::TopFrontLeft, ChannelRole::TopFrontRight, ChannelRole::TopBackLeft,
                                       ChannelRole::TopBackRight});
                        push("7.1.2",
                             {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                              ChannelRole::LFE, ChannelRole::BackLeft, ChannelRole::BackRight, ChannelRole::SideLeft,
                              ChannelRole::SideRight, ChannelRole::TopFrontLeft, ChannelRole::TopFrontRight});
                        push("7.1.4",
                             {ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                              ChannelRole::LFE, ChannelRole::BackLeft, ChannelRole::BackRight, ChannelRole::SideLeft,
                              ChannelRole::SideRight, ChannelRole::TopFrontLeft, ChannelRole::TopFrontRight,
                              ChannelRole::TopBackLeft, ChannelRole::TopBackRight});
                        push("FOA", {ChannelRole::AmbisonicW, ChannelRole::AmbisonicX, ChannelRole::AmbisonicY,
                                     ChannelRole::AmbisonicZ});
                        return out;
                }();
                return layouts;
        }

        // Look up a well-known layout by name.  Returns nullptr when not found.
        const AudioChannelMap::WellKnownLayout *findLayoutByName(const String &name) {
                for (const auto &layout : knownLayoutsRef()) {
                        if (layout.name == name) return &layout;
                }
                return nullptr;
        }
} // namespace

::promeki::List<AudioChannelMap::WellKnownLayout> AudioChannelMap::wellKnownLayouts() {
        return knownLayoutsRef();
}

AudioChannelMap AudioChannelMap::defaultForChannels(size_t channels) {
        return defaultForChannels(channels, AudioStreamDesc());
}

AudioChannelMap AudioChannelMap::defaultForChannels(size_t channels, const AudioStreamDesc &stream) {
        // Prefer a well-known role list when one matches the channel count.
        for (const auto &layout : knownLayoutsRef()) {
                if (layout.roles.size() == channels) {
                        return AudioChannelMap(stream, layout.roles);
                }
        }
        // No canonical layout — fill with Unused and let the caller populate roles.
        ::promeki::List<ChannelRole> roles;
        roles.reserve(channels);
        for (size_t i = 0; i < channels; ++i) roles.pushToBack(ChannelRole::Unused);
        return AudioChannelMap(stream, std::move(roles));
}

Result<AudioChannelMap> AudioChannelMap::fromString(const String &str) {
        const String s = str.trim();
        if (s.isEmpty()) return makeResult(AudioChannelMap());

        // First attempt: well-known shortcut, with optional "Stream:" prefix.
        // Splitting on the FIRST colon disambiguates shortcuts cleanly even
        // when a per-channel form like "Main:FrontLeft" would also parse:
        //   - "5.1"               -> layout "5.1", stream Undefined
        //   - "Main:5.1"          -> layout "5.1", stream Main
        //   - "Main:FrontLeft"    -> NOT a shortcut (no layout "FrontLeft");
        //                            falls through to per-channel parsing.
        if (s.find(',') == String::npos) {
                AudioStreamDesc shortcutStream;
                String          layoutName = s;
                size_t          colon = s.find(':');
                if (colon != String::npos) {
                        String streamPart = s.left(colon).trim();
                        layoutName = s.mid(colon + 1).trim();
                        shortcutStream = AudioStreamDesc(streamPart);
                }
                if (const auto *layout = findLayoutByName(layoutName)) {
                        return makeResult(AudioChannelMap(shortcutStream, layout->roles));
                }
        }

        // Per-channel comma list.  Each item is "Stream:Role" or "Role".
        EntryList  entries;
        StringList parts = s.split(",");
        entries.reserve(parts.size());
        for (const String &raw : parts) {
                const String    item = raw.trim();
                AudioStreamDesc itemStream;
                String          roleName;
                size_t          colon = item.find(':');
                if (colon != String::npos) {
                        itemStream = AudioStreamDesc(item.left(colon).trim());
                        roleName = item.mid(colon + 1).trim();
                } else {
                        roleName = item;
                }
                Result<int> r = Enum::valueOf(ChannelRole::Type, roleName);
                if (!isOk(r)) return makeError<AudioChannelMap>(Error::Invalid);
                entries.pushToBack(Entry(itemStream, ChannelRole(value(r))));
        }
        return makeResult(AudioChannelMap(std::move(entries)));
}

void AudioChannelMap::setRole(size_t index, const ChannelRole &role) {
        while (_entries.size() <= index) _entries.pushToBack(Entry(AudioStreamDesc(), ChannelRole::Unused));
        _entries[index] = Entry(_entries[index].first(), role);
}

void AudioChannelMap::setStream(size_t index, const AudioStreamDesc &stream) {
        while (_entries.size() <= index) _entries.pushToBack(Entry(AudioStreamDesc(), ChannelRole::Unused));
        _entries[index] = Entry(stream, _entries[index].second());
}

void AudioChannelMap::setEntry(size_t index, const AudioStreamDesc &stream, const ChannelRole &role) {
        while (_entries.size() <= index) _entries.pushToBack(Entry(AudioStreamDesc(), ChannelRole::Unused));
        _entries[index] = Entry(stream, role);
}

int AudioChannelMap::indexOf(const ChannelRole &role) const {
        for (size_t i = 0; i < _entries.size(); ++i) {
                if (_entries[i].second() == role) return static_cast<int>(i);
        }
        return -1;
}

int AudioChannelMap::indexOf(const AudioStreamDesc &stream, const ChannelRole &role) const {
        for (size_t i = 0; i < _entries.size(); ++i) {
                if (_entries[i].first() == stream && _entries[i].second() == role) return static_cast<int>(i);
        }
        return -1;
}

bool AudioChannelMap::isSingleStream() const {
        if (_entries.isEmpty()) return true;
        const AudioStreamDesc &first = _entries[0].first();
        for (size_t i = 1; i < _entries.size(); ++i) {
                if (_entries[i].first() != first) return false;
        }
        return true;
}

AudioStreamDesc AudioChannelMap::commonStream() const {
        if (_entries.isEmpty() || !isSingleStream()) return AudioStreamDesc();
        return _entries[0].first();
}

String AudioChannelMap::wellKnownName() const {
        if (_entries.isEmpty() || !isSingleStream()) return String();
        // Build the role list and look it up against the well-known table.
        ::promeki::List<ChannelRole> roles;
        roles.reserve(_entries.size());
        for (const auto &e : _entries) roles.pushToBack(e.second());
        for (const auto &layout : knownLayoutsRef()) {
                if (layout.roles == roles) {
                        AudioStreamDesc s = _entries[0].first();
                        if (s.isUndefined()) return layout.name;
                        return s.name() + String(":") + layout.name;
                }
        }
        return String();
}

String AudioChannelMap::toString() const {
        String wk = wellKnownName();
        if (!wk.isEmpty()) return wk;
        // Per-channel form: "[Stream:]Role,[Stream:]Role,..."
        String out;
        for (size_t i = 0; i < _entries.size(); ++i) {
                if (i > 0) out += ",";
                const Entry &e = _entries[i];
                if (e.first().isUndefined()) {
                        out += e.second().valueName();
                } else {
                        out += e.first().name();
                        out += ":";
                        out += e.second().valueName();
                }
        }
        return out;
}

PROMEKI_NAMESPACE_END
