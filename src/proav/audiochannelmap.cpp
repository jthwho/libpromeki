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
        const AudioChannelMap::WellKnownLayoutList &knownLayoutsRef() {
                static const auto layouts = [] {
                        AudioChannelMap::WellKnownLayoutList out;
                        const auto push = [&](const char *name, std::initializer_list<ChannelRole> rs) {
                                AudioChannelMap::WellKnownLayout entry;
                                entry.name = name;
                                entry.roles = AudioChannelMap::ChannelRoleList(rs);
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

AudioChannelMap::WellKnownLayoutList AudioChannelMap::wellKnownLayouts() {
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
        AudioChannelMap::ChannelRoleList roles;
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
        AudioChannelMap::ChannelRoleList roles;
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

// ============================================================================
// SMPTE ST 2110-30:2025 §6.2.2 channel-order bridge
// ============================================================================

namespace {
        // Canonical role sequence for a single ST 2110-30 Table 1
        // Channel Grouping Symbol.  Used by both the detector (looks
        // up by sequence) and the parser (looks up by symbol).
        struct St2110Grouping {
                        const char        *symbol;
                        size_t             channels;
                        // Pointer + length form: most groupings have
                        // a fixed role sequence (M / DM / ST / LtRt /
                        // 51 / 71 / SGRP).  The U-symbol family
                        // shares one all-Unused sequence so the
                        // detector can use a single Unused-run path.
                        const ChannelRole *roles;
        };

        static const ChannelRole kRolesMono[]     = {ChannelRole::Mono};
        static const ChannelRole kRolesDualMono[] = {ChannelRole::Mono, ChannelRole::Mono};
        static const ChannelRole kRolesStereo[]   = {ChannelRole::FrontLeft, ChannelRole::FrontRight};
        static const ChannelRole kRolesLtRt[]     = {ChannelRole::LeftTotal, ChannelRole::RightTotal};
        // 5.1 ordering matches the library's existing well-known
        // 5.1 layout (BackLeft / BackRight = ST 2110-30 Ls / Rs).
        static const ChannelRole kRoles51[] = {
                ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                ChannelRole::LFE,       ChannelRole::BackLeft,   ChannelRole::BackRight,
        };
        // 7.1 ordering is ST 2110-30's "side surrounds before back
        // surrounds" form: L, R, C, LFE, Lss(=SideL), Rss(=SideR),
        // Lrs(=BackL), Rrs(=BackR).  This deliberately differs from
        // the library's "7.1" well-known (which puts BackL / BackR
        // before SideL / SideR); detection of the library's 7.1
        // layout falls through to U08.
        static const ChannelRole kRoles71[] = {
                ChannelRole::FrontLeft, ChannelRole::FrontRight, ChannelRole::FrontCenter,
                ChannelRole::LFE,       ChannelRole::SideLeft,   ChannelRole::SideRight,
                ChannelRole::BackLeft,  ChannelRole::BackRight,
        };
        // SGRP: one SDI audio group (channels 1-4 unlabeled).
        // ST 2036-2 22.2 (`222`) is deferred — parse emits 24×
        // Unused; emit never produces it.
        static const ChannelRole kRolesSgrp[] = {
                ChannelRole::Unused, ChannelRole::Unused, ChannelRole::Unused, ChannelRole::Unused,
        };

        // Ordered by detection priority (longest, most specific
        // sequence first) so 51 wins over ST, ST wins over M, etc.
        // DM, SGRP, and 222 are intentionally omitted from the
        // detection table — the detector never produces them (see
        // header docs for the rationale).
        const St2110Grouping kDetectionTable[] = {
                {"71",   sizeof(kRoles71)   / sizeof(ChannelRole), kRoles71},
                {"51",   sizeof(kRoles51)   / sizeof(ChannelRole), kRoles51},
                {"LtRt", sizeof(kRolesLtRt) / sizeof(ChannelRole), kRolesLtRt},
                {"ST",   sizeof(kRolesStereo)/ sizeof(ChannelRole), kRolesStereo},
                {"M",    sizeof(kRolesMono) / sizeof(ChannelRole), kRolesMono},
        };

        // Returns true when the role sequence in @p entries starting
        // at @p offset matches @p grp 's canonical roles.
        bool matchesGrouping(const AudioChannelMap::EntryList &entries, size_t offset,
                             const St2110Grouping &grp) {
                if (offset + grp.channels > entries.size()) return false;
                for (size_t i = 0; i < grp.channels; ++i) {
                        if (entries[offset + i].second() != grp.roles[i]) return false;
                }
                return true;
        }

        // Formats an Unn symbol with a 2-digit count.  Caller
        // ensures count is in [1, 64].
        String formatUSymbol(size_t count) {
                String s = "U";
                if (count < 10) s += "0";
                s += String::number(static_cast<int>(count));
                return s;
        }
} // namespace

String AudioChannelMap::toSt2110ChannelOrder() const {
        if (_entries.isEmpty()) return String();
        String out = "(";
        size_t pos = 0;
        bool   first = true;
        while (pos < _entries.size()) {
                // Try the detection table first — longest match
                // wins (the table is already ordered that way).
                const St2110Grouping *match = nullptr;
                for (const St2110Grouping &grp : kDetectionTable) {
                        if (matchesGrouping(_entries, pos, grp)) {
                                match = &grp;
                                break;
                        }
                }
                if (match != nullptr) {
                        if (!first) out += ",";
                        out += match->symbol;
                        first = false;
                        pos += match->channels;
                        continue;
                }
                // No canonical grouping matches at this position —
                // accumulate consecutive unmatched channels into a
                // single U-group.  Cap at 64 per Table 1's U01..U64
                // range; longer runs emit as multiple U-groups.
                size_t runStart = pos;
                while (pos < _entries.size() && (pos - runStart) < 64) {
                        bool nextMatches = false;
                        for (const St2110Grouping &grp : kDetectionTable) {
                                if (matchesGrouping(_entries, pos, grp)) {
                                        nextMatches = true;
                                        break;
                                }
                        }
                        if (nextMatches) break;
                        ++pos;
                }
                if (!first) out += ",";
                out += formatUSymbol(pos - runStart);
                first = false;
        }
        out += ")";
        return out;
}

Result<AudioChannelMap> AudioChannelMap::fromSt2110ChannelOrder(const String &value) {
        String s = value.trim();
        // Strip the optional "SMPTE2110." convention prefix per RFC
        // 3190 syntax.  Other conventions are rejected — ST 2110-30
        // §6.2.2 says SMPTE2110 SHOULD be used and we don't speak
        // any of the legacy RFC 3190 grammars (DV / AIFF-C).
        if (s.startsWith("SMPTE2110.")) {
                s = s.mid(10).trim();
        } else if (s.startsWith("SMPTE2110")) {
                // Tolerate the dot-less form some senders emit.
                s = s.mid(9).trim();
        }
        if (s.length() < 2 || !s.startsWith("(") || !s.endsWith(")")) {
                return makeError<AudioChannelMap>(Error::Invalid);
        }
        // Strip the surrounding parens, then split on commas.
        s = s.mid(1, s.length() - 2).trim();
        if (s.isEmpty()) return makeResult(AudioChannelMap());

        // Pre-validate the body for malformed comma usage that
        // String::split silently swallows (it skips empty tokens).
        // Reject leading / trailing comma and consecutive commas so
        // "(M,)" / "(,ST)" / "(M,,ST)" all surface as invalid.
        if (s.startsWith(",") || s.endsWith(",") || s.contains(",,")) {
                return makeError<AudioChannelMap>(Error::Invalid);
        }

        EntryList entries;
        const StringList tokens = s.split(',');
        for (const String &rawToken : tokens) {
                const String token = rawToken.trim();
                if (token.isEmpty()) {
                        return makeError<AudioChannelMap>(Error::Invalid);
                }

                // Pick the canonical role sequence for the
                // symbol.  Fixed-shape symbols dispatch via the
                // table; U-symbols carry their channel count in
                // the symbol itself (U01..U64).
                const ChannelRole *roles = nullptr;
                size_t             count = 0;
                if (token == "M") {
                        roles = kRolesMono;  count = 1;
                } else if (token == "DM") {
                        roles = kRolesDualMono; count = 2;
                } else if (token == "ST") {
                        roles = kRolesStereo; count = 2;
                } else if (token == "LtRt") {
                        roles = kRolesLtRt; count = 2;
                } else if (token == "51") {
                        roles = kRoles51; count = 6;
                } else if (token == "71") {
                        roles = kRoles71; count = 8;
                } else if (token == "SGRP") {
                        roles = kRolesSgrp; count = 4;
                } else if (token == "222") {
                        // SMPTE ST 2036-2 22.2 mapping deferred —
                        // produce 24 Unused entries so the channel
                        // count survives the round-trip; the
                        // labelling layer can refine later.
                        count = 24;
                } else if (token.length() == 3 && token.startsWith("U")) {
                        // U01..U64 — parse the two-digit count.
                        Error pe;
                        const int n = token.mid(1).toInt(&pe);
                        if (pe.isError() || n < 1 || n > 64) {
                                return makeError<AudioChannelMap>(Error::Invalid);
                        }
                        count = static_cast<size_t>(n);
                } else {
                        return makeError<AudioChannelMap>(Error::Invalid);
                }

                entries.reserve(entries.size() + count);
                for (size_t i = 0; i < count; ++i) {
                        const ChannelRole r =
                                (roles != nullptr) ? roles[i] : ChannelRole::Unused;
                        entries.pushToBack(Entry(AudioStreamDesc(), r));
                }
        }
        return makeResult(AudioChannelMap(std::move(entries)));
}

PROMEKI_NAMESPACE_END
