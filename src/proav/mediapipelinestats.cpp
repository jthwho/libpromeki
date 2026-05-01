/**
 * @file      mediapipelinestats.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipelinestats.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/logger.h>
#include <promeki/units.h>
#include <promeki/variant.h>

#include <cmath>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Display-order list for command kinds: Read / Write first
        // because they dominate steady-state telemetry, then Open /
        // Close (lifecycle), then Seek / Params / Stats (control).
        // The underlying enum order is preserved (Open=0, Close=1,
        // ...) for stable serialization; this is purely a render
        // hint.
        constexpr MediaIOCommand::Kind kDisplayOrder[] = {
                MediaIOCommand::Read,   MediaIOCommand::Write, MediaIOCommand::Open, MediaIOCommand::Close,
                MediaIOCommand::Seek,   MediaIOCommand::Params, MediaIOCommand::Stats,
        };

        // Round-trips an enum tag as the canonical kindName() string.
        // Returns false when the JSON key does not map to any known
        // kind so callers can drop unknown sections rather than poison
        // the snapshot.
        bool kindFromName(const String &name, MediaIOCommand::Kind &out) {
                for (MediaIOCommand::Kind k : kDisplayOrder) {
                        if (name == MediaIOCommand::kindName(k)) {
                                out = k;
                                return true;
                        }
                }
                return false;
        }

        // Mapping from a stat ID name to the unit family used when
        // formatting both raw scalar values and WindowedStat
        // aggregates.  The collector pushes Duration as nanoseconds
        // and FrameCount as raw counter values, so the same picker
        // works for both the cumulative MediaIOStats Variants and
        // the per-window doubles.
        //
        // FIXME: name-suffix matching is brittle — backend keys named
        // outside the Foo*Duration / Foo*Ms / Bytes* convention will
        // fall through to Count and render without units.  Plan: hang
        // a unit hint off the StringRegistry::Item (or VariantSpec)
        // for each stat ID at registration time and pull it from
        // there.  Tracking this against the per-StringRegistry-entry
        // metadata refactor.
        enum class StatUnit {
                Duration,    ///< value is a duration in nanoseconds
                DurationMs,  ///< value is a duration in milliseconds
                Bytes,       ///< value is a byte count
                BytesPerSec, ///< value is bytes/second
                FramesPerSec,///< value is frames/second
                Count,       ///< value is a raw integer count
        };

        StatUnit unitFor(const String &statName) {
                if (statName.endsWith(String("Duration"))) return StatUnit::Duration;
                if (statName == "BytesProcessed") return StatUnit::Bytes;
                if (statName == "BytesPerSecond") return StatUnit::BytesPerSec;
                if (statName == "FramesPerSecond") return StatUnit::FramesPerSec;
                if (statName.endsWith(String("Ms"))) return StatUnit::DurationMs;
                return StatUnit::Count;
        }

        String formatScalar(StatUnit unit, double value) {
                if (!std::isfinite(value)) return String("-");
                switch (unit) {
                        case StatUnit::Duration:
                                return Units::fromDurationNs(value);
                        case StatUnit::DurationMs:
                                return Units::fromDurationMs(value);
                        case StatUnit::Bytes:
                                if (value < 0.0) value = 0.0;
                                return Units::fromByteCount(static_cast<uint64_t>(value));
                        case StatUnit::BytesPerSec:
                                if (value <= 0.0) return String("0 B/s");
                                return Units::fromBytesPerSec(value);
                        case StatUnit::FramesPerSec:
                                return Units::fromFramesPerSec(value);
                        case StatUnit::Count:
                                if (value < 0.0) {
                                        return String::number(static_cast<int64_t>(value));
                                }
                                if (std::floor(value) == value) {
                                        return Units::fromCount(static_cast<uint64_t>(value));
                                }
                                return String::format("{:.2f}", value);
                }
                return String::number(value);
        }

        // Renders a single scalar Variant payload (for cumulative
        // entries).  String / bool fall through to their raw form so
        // backend-specific keys with non-numeric payloads still
        // appear in the table.
        String formatVariant(StatUnit unit, const Variant &v) {
                switch (v.type()) {
                        case Variant::TypeString:
                                return v.get<String>();
                        case Variant::TypeBool:
                                return v.get<bool>() ? String("true") : String("false");
                        case Variant::TypeDuration: {
                                const Duration d = v.get<Duration>();
                                return Units::fromDurationNs(static_cast<double>(d.nanoseconds()));
                        }
                        case Variant::TypeFrameCount: {
                                const FrameCount fc = v.get<FrameCount>();
                                if (fc.isUnknown()) return String("?");
                                if (fc.isInfinite()) return String("inf");
                                return Units::fromCount(static_cast<uint64_t>(fc.value()));
                        }
                        default:
                                break;
                }
                Error  err;
                double d = v.get<double>(&err);
                if (err.isError()) return v.get<String>();
                return formatScalar(unit, d);
        }

} // namespace

// ============================================================================
// MediaPipelineStageStats
// ============================================================================

WindowedStatsBundle &MediaPipelineStageStats::windowedBundle(MediaIOCommand::Kind kind) {
        auto it = windowed.find(kind);
        if (it == windowed.end()) {
                windowed.insert(kind, WindowedStatsBundle());
                it = windowed.find(kind);
        }
        return it->second;
}

void MediaPipelineStageStats::clear() {
        cumulative = MediaIOStats();
        windowed.clear();
}

bool MediaPipelineStageStats::operator==(const MediaPipelineStageStats &other) const {
        if (name != other.name) return false;
        if (!(cumulative == other.cumulative)) return false;
        if (windowed.size() != other.windowed.size()) return false;
        for (auto it = windowed.cbegin(); it != windowed.cend(); ++it) {
                auto oit = other.windowed.find(it->first);
                if (oit == other.windowed.cend()) return false;
                if (!(it->second == oit->second)) return false;
        }
        return true;
}

JsonObject MediaPipelineStageStats::toJson() const {
        JsonObject sj;
        sj.set("name", name);
        sj.set("cumulative", cumulative.toJson());
        JsonObject windowedJson;
        for (auto it = windowed.cbegin(); it != windowed.cend(); ++it) {
                windowedJson.set(String(MediaIOCommand::kindName(it->first)), it->second.toJson());
        }
        sj.set("windowed", windowedJson);
        return sj;
}

MediaPipelineStageStats MediaPipelineStageStats::fromJson(const JsonObject &obj, Error *err) {
        MediaPipelineStageStats out;
        bool                    good = true;

        out.name = obj.getString("name");
        if (obj.valueIsObject("cumulative")) {
                JsonObject cj = obj.getObject("cumulative");
                cj.forEach([&](const String &key, const Variant &val) {
                        Error serr = out.cumulative.setFromJson(MediaIOStats::ID(key), val);
                        if (serr.isError()) {
                                promekiWarn("MediaPipelineStageStats::fromJson: cumulative key '%s' rejected: %s",
                                            key.cstr(), serr.desc().cstr());
                                good = false;
                        }
                });
        }
        if (obj.valueIsObject("windowed")) {
                JsonObject wj = obj.getObject("windowed");
                wj.forEach([&](const String &kindName, const Variant &) {
                        if (!wj.valueIsObject(kindName)) return;
                        MediaIOCommand::Kind kind;
                        if (!kindFromName(kindName, kind)) {
                                promekiWarn(
                                        "MediaPipelineStageStats::fromJson: unknown command kind '%s' in stage '%s'",
                                        kindName.cstr(), out.name.cstr());
                                good = false;
                                return;
                        }
                        Error               berr;
                        WindowedStatsBundle bundle = WindowedStatsBundle::fromJson(wj.getObject(kindName), &berr);
                        if (berr.isError()) good = false;
                        out.windowed.insert(kind, std::move(bundle));
                });
        }

        if (err) *err = good ? Error::Ok : Error::Invalid;
        return out;
}

DataStream &operator<<(DataStream &stream, const MediaPipelineStageStats &s) {
        stream << s.name;
        stream << s.cumulative;
        stream << static_cast<uint32_t>(s.windowed.size());
        for (auto it = s.windowed.cbegin(); it != s.windowed.cend(); ++it) {
                stream << static_cast<int32_t>(it->first);
                stream << it->second;
        }
        return stream;
}

DataStream &operator>>(DataStream &stream, MediaPipelineStageStats &s) {
        s.clear();
        stream >> s.name;
        stream >> s.cumulative;
        uint32_t windowedCount = 0;
        stream >> windowedCount;
        for (uint32_t i = 0; i < windowedCount && stream.status() == DataStream::Ok; ++i) {
                int32_t             kindRaw = 0;
                WindowedStatsBundle bundle;
                stream >> kindRaw;
                stream >> bundle;
                if (stream.status() == DataStream::Ok) {
                        s.windowed.insert(static_cast<MediaIOCommand::Kind>(kindRaw), std::move(bundle));
                }
        }
        return stream;
}

// ============================================================================
// Lookup
// ============================================================================

const MediaPipelineStageStats *MediaPipelineStats::findStage(const String &name) const {
        for (size_t i = 0; i < _stages.size(); ++i) {
                if (_stages[i].name == name) return &_stages[i];
        }
        return nullptr;
}

MediaPipelineStageStats *MediaPipelineStats::findStage(const String &name) {
        for (size_t i = 0; i < _stages.size(); ++i) {
                if (_stages[i].name == name) return &_stages[i];
        }
        return nullptr;
}

MediaPipelineStageStats MediaPipelineStats::stage(const String &name) const {
        const MediaPipelineStageStats *p = findStage(name);
        if (p == nullptr) return MediaPipelineStageStats();
        return *p;
}

StringList MediaPipelineStats::stageNames() const {
        StringList out;
        for (size_t i = 0; i < _stages.size(); ++i) out.pushToBack(_stages[i].name);
        return out;
}

// ============================================================================
// Rendering
// ============================================================================

StringList MediaPipelineStats::describe() const {
        // Build every row first so we can compute the column widths
        // for the prefix columns (stage, kind, statName) and emit
        // each row with consistent padding.  Numeric values are
        // routed through the per-stat unit picker so the rightmost
        // column carries human-friendly suffixes (KB, ms, fps, ...)
        // instead of raw integers.
        struct Row {
                        String stage;
                        String kind;
                        String statName;
                        String value;
        };

        const String kCumulativeKind = "Cumulative";

        promeki::List<Row> rows;
        for (size_t i = 0; i < _stages.size(); ++i) {
                const MediaPipelineStageStats &st = _stages[i];
                st.cumulative.forEach([&](MediaIOStats::ID id, const Variant &val) {
                        Row r;
                        r.stage = st.name;
                        r.kind = kCumulativeKind;
                        r.statName = id.name();
                        r.value = formatVariant(unitFor(r.statName), val);
                        rows.pushToBack(r);
                });
                // Walk windowed bundles in display order (Read / Write
                // first), skipping any kinds the stage didn't observe.
                for (MediaIOCommand::Kind k : kDisplayOrder) {
                        auto bit = st.windowed.find(k);
                        if (bit == st.windowed.cend()) continue;
                        const String kindName(MediaIOCommand::kindName(k));
                        bit->second.forEach([&](WindowedStatsBundle::ID id, const WindowedStat &ws) {
                                Row            r;
                                r.stage = st.name;
                                r.kind = kindName;
                                r.statName = id.name();
                                const StatUnit unit = unitFor(r.statName);
                                r.value = ws.toString([unit](double v) { return formatScalar(unit, v); });
                                rows.pushToBack(r);
                        });
                }
        }

        // Column widths: take the longest entry per column so the
        // dividers line up regardless of stat ID lengths.
        size_t stageW = 0, kindW = 0, statW = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
                if (rows[i].stage.size() > stageW) stageW = rows[i].stage.size();
                if (rows[i].kind.size() > kindW) kindW = rows[i].kind.size();
                if (rows[i].statName.size() > statW) statW = rows[i].statName.size();
        }

        StringList out;
        for (size_t i = 0; i < rows.size(); ++i) {
                out.pushToBack(String::format("{:<{}}  {:<{}}  {:<{}}  {}", rows[i].stage, stageW, rows[i].kind,
                                              kindW, rows[i].statName, statW, rows[i].value));
        }
        return out;
}

// ============================================================================
// JSON
// ============================================================================

JsonObject MediaPipelineStats::toJson() const {
        JsonObject root;
        JsonArray  arr;
        for (size_t i = 0; i < _stages.size(); ++i) {
                arr.add(_stages[i].toJson());
        }
        root.set("stages", arr);
        return root;
}

MediaPipelineStats MediaPipelineStats::fromJson(const JsonObject &obj, Error *err) {
        MediaPipelineStats out;
        bool               good = true;

        if (obj.valueIsArray("stages")) {
                JsonArray arr = obj.getArray("stages");
                for (int i = 0; i < arr.size(); ++i) {
                        if (!arr.valueIsObject(i)) continue;
                        Error                   sErr;
                        MediaPipelineStageStats st = MediaPipelineStageStats::fromJson(arr.getObject(i), &sErr);
                        if (sErr.isError()) good = false;
                        out._stages.pushToBack(std::move(st));
                }
        }

        if (err) *err = good ? Error::Ok : Error::Invalid;
        return out;
}

// ============================================================================
// Equality
// ============================================================================

bool MediaPipelineStats::operator==(const MediaPipelineStats &other) const {
        if (_stages.size() != other._stages.size()) return false;
        for (size_t i = 0; i < _stages.size(); ++i) {
                if (!(_stages[i] == other._stages[i])) return false;
        }
        return true;
}

// ============================================================================
// DataStream
// ============================================================================

DataStream &operator<<(DataStream &stream, const MediaPipelineStats &s) {
        stream.writeTag(DataStream::TypeMediaPipelineStats);
        const MediaPipelineStats::StageList &stages = s.stages();
        stream << static_cast<uint32_t>(stages.size());
        for (size_t i = 0; i < stages.size(); ++i) {
                stream << stages[i];
        }
        return stream;
}

DataStream &operator>>(DataStream &stream, MediaPipelineStats &s) {
        s.clear();
        if (!stream.readTag(DataStream::TypeMediaPipelineStats)) {
                return stream;
        }
        uint32_t stageCount = 0;
        stream >> stageCount;
        for (uint32_t i = 0; i < stageCount && stream.status() == DataStream::Ok; ++i) {
                MediaPipelineStageStats st;
                stream >> st;
                if (stream.status() == DataStream::Ok) {
                        s.stages().pushToBack(std::move(st));
                }
        }
        return stream;
}

PROMEKI_NAMESPACE_END
