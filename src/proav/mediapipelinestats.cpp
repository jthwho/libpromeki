/**
 * @file      mediapipelinestats.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipelinestats.h>
#include <promeki/logger.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Per-stage accessors
// ============================================================================

void MediaPipelineStats::setStageStats(const String &stageName, const MediaIOStats &stats) {
        _perStage.insert(stageName, stats);
}

MediaIOStats MediaPipelineStats::stageStats(const String &stageName) const {
        auto it = _perStage.find(stageName);
        if (it == _perStage.end()) return MediaIOStats();
        return it->second;
}

void MediaPipelineStats::clear() {
        _perStage.clear();
        _aggregate = MediaIOStats();
        _pipeline = PipelineStats();
}

// ============================================================================
// Aggregate reduction
// ============================================================================

namespace {

        // Standard MediaIOStats keys are declared as `PROMEKI_DECLARE_ID`
        // members so they're constexpr IDs — we can sum, max, or average
        // them directly without any string lookups inside the hot loop.
        template <typename T> T sumAs(const MediaIOStats &a, MediaIOStats::ID id) {
                if (!a.contains(id)) return T(0);
                return a.get(id).get<T>();
        }

        // FrameCount-typed counters carry sentinel states (Unknown, Infinity)
        // that must not leak into the aggregate as negative int64_t via
        // `.value()`.  Stages reporting Unknown or Infinity are treated as
        // contributing zero to the roll-up — callers that care about those
        // states should inspect per-stage stats directly.
        FrameCount sumFrameCount(const MediaIOStats &a, MediaIOStats::ID id) {
                if (!a.contains(id)) return FrameCount(0);
                const FrameCount fc = a.get(id).get<FrameCount>();
                return fc.isFinite() ? fc : FrameCount(0);
        }

} // namespace

void MediaPipelineStats::recomputeAggregate() {
        FrameCount framesDropped = FrameCount(0);
        FrameCount framesRepeated = FrameCount(0);
        FrameCount framesLate = FrameCount(0);
        int64_t    queueDepth = 0;
        int64_t    queueCapacity = 0;
        int64_t    pendingOps = 0;
        double     bytesPerSec = 0.0;
        double     framesPerSec = 0.0;
        double     avgLatencySum = 0.0;
        int        avgLatencyCt = 0;
        double     avgProcSum = 0.0;
        int        avgProcCt = 0;
        double     peakLatency = 0.0;
        double     peakProc = 0.0;
        String     firstError;

        for (auto it = _perStage.cbegin(); it != _perStage.cend(); ++it) {
                const String       &name = it->first;
                const MediaIOStats &stats = it->second;

                framesDropped += sumFrameCount(stats, MediaIOStats::FramesDropped);
                framesRepeated += sumFrameCount(stats, MediaIOStats::FramesRepeated);
                framesLate += sumFrameCount(stats, MediaIOStats::FramesLate);
                queueDepth += sumAs<int64_t>(stats, MediaIOStats::QueueDepth);
                queueCapacity += sumAs<int64_t>(stats, MediaIOStats::QueueCapacity);
                pendingOps += sumAs<int64_t>(stats, MediaIOStats::PendingOperations);
                bytesPerSec += sumAs<double>(stats, MediaIOStats::BytesPerSecond);
                framesPerSec += sumAs<double>(stats, MediaIOStats::FramesPerSecond);

                const double latency = sumAs<double>(stats, MediaIOStats::AverageLatencyMs);
                if (latency > 0.0) {
                        avgLatencySum += latency;
                        ++avgLatencyCt;
                }
                const double proc = sumAs<double>(stats, MediaIOStats::AverageProcessingMs);
                if (proc > 0.0) {
                        avgProcSum += proc;
                        ++avgProcCt;
                }

                const double pLatency = sumAs<double>(stats, MediaIOStats::PeakLatencyMs);
                if (pLatency > peakLatency) peakLatency = pLatency;
                const double pProc = sumAs<double>(stats, MediaIOStats::PeakProcessingMs);
                if (pProc > peakProc) peakProc = pProc;

                if (firstError.isEmpty() && stats.contains(MediaIOStats::LastErrorMessage)) {
                        const String msg = stats.get(MediaIOStats::LastErrorMessage).get<String>();
                        if (!msg.isEmpty()) {
                                firstError = String("[") + name + "] " + msg;
                        }
                }
        }

        _aggregate = MediaIOStats();
        _aggregate.set(MediaIOStats::FramesDropped, framesDropped);
        _aggregate.set(MediaIOStats::FramesRepeated, framesRepeated);
        _aggregate.set(MediaIOStats::FramesLate, framesLate);
        _aggregate.set(MediaIOStats::QueueDepth, queueDepth);
        _aggregate.set(MediaIOStats::QueueCapacity, queueCapacity);
        _aggregate.set(MediaIOStats::PendingOperations, pendingOps);
        _aggregate.set(MediaIOStats::BytesPerSecond, bytesPerSec);
        _aggregate.set(MediaIOStats::FramesPerSecond, framesPerSec);
        _aggregate.set(MediaIOStats::PeakLatencyMs, peakLatency);
        _aggregate.set(MediaIOStats::PeakProcessingMs, peakProc);
        if (avgLatencyCt > 0) {
                _aggregate.set(MediaIOStats::AverageLatencyMs, avgLatencySum / avgLatencyCt);
        }
        if (avgProcCt > 0) {
                _aggregate.set(MediaIOStats::AverageProcessingMs, avgProcSum / avgProcCt);
        }
        if (!firstError.isEmpty()) {
                _aggregate.set(MediaIOStats::LastErrorMessage, firstError);
        }
}

// ============================================================================
// Rendering
// ============================================================================

StringList MediaPipelineStats::describe() const {
        StringList out;
        if (!_pipeline.isEmpty()) {
                String line = "pipeline: ";
                line += _pipeline.toString();
                out.pushToBack(line);
        }
        for (auto it = _perStage.cbegin(); it != _perStage.cend(); ++it) {
                String line = "  ";
                line += it->first;
                line += ": ";
                line += it->second.toString();
                out.pushToBack(line);
        }
        String agg = "aggregate: ";
        agg += _aggregate.toString();
        out.pushToBack(agg);
        return out;
}

// ============================================================================
// JSON
// ============================================================================

JsonObject MediaPipelineStats::toJson() const {
        JsonObject j;
        JsonObject perStageJson;
        for (auto it = _perStage.cbegin(); it != _perStage.cend(); ++it) {
                perStageJson.set(it->first, it->second.toJson());
        }
        j.set("perStage", perStageJson);
        j.set("aggregate", _aggregate.toJson());
        j.set("pipeline", _pipeline.toJson());
        return j;
}

MediaPipelineStats MediaPipelineStats::fromJson(const JsonObject &obj, Error *err) {
        MediaPipelineStats s;
        bool               good = true;

        auto applyEntry = [&good](auto &db, const String &context, const String &key, const Variant &val) {
                Error serr = db.setFromJson(typename std::decay_t<decltype(db)>::ID(key), val);
                if (serr.isError()) {
                        promekiWarn("MediaPipelineStats::fromJson: %s key '%s' rejected: %s", context.cstr(),
                                    key.cstr(), serr.desc().cstr());
                        good = false;
                }
        };

        if (obj.valueIsObject("perStage")) {
                const JsonObject perStageJson = obj.getObject("perStage");
                // forEach delivers (key, Variant); we ignore the Variant
                // form and re-fetch each value as a nested JsonObject via
                // getObject so we can rebuild the MediaIOStats via the
                // base's spec-aware fromJson.
                perStageJson.forEach([&](const String &name, const Variant &) {
                        if (!perStageJson.valueIsObject(name)) return;
                        JsonObject   nested = perStageJson.getObject(name);
                        MediaIOStats stats;
                        nested.forEach([&](const String &key, const Variant &val) {
                                applyEntry(stats, String("perStage[") + name + "]", key, val);
                        });
                        s._perStage.insert(name, stats);
                });
        }
        if (obj.valueIsObject("aggregate")) {
                JsonObject agg = obj.getObject("aggregate");
                agg.forEach([&](const String &key, const Variant &val) {
                        applyEntry(s._aggregate, String("aggregate"), key, val);
                });
        }
        if (obj.valueIsObject("pipeline")) {
                JsonObject pp = obj.getObject("pipeline");
                pp.forEach([&](const String &key, const Variant &val) {
                        applyEntry(s._pipeline, String("pipeline"), key, val);
                });
        }

        if (err) *err = good ? Error::Ok : Error::Invalid;
        return s;
}

// ============================================================================
// Equality
// ============================================================================

bool MediaPipelineStats::operator==(const MediaPipelineStats &other) const {
        if (_perStage.size() != other._perStage.size()) return false;
        for (auto it = _perStage.cbegin(); it != _perStage.cend(); ++it) {
                auto oit = other._perStage.find(it->first);
                if (oit == other._perStage.end()) return false;
                if (!(it->second == oit->second)) return false;
        }
        return _aggregate == other._aggregate && _pipeline == other._pipeline;
}

// ============================================================================
// DataStream
// ============================================================================

DataStream &operator<<(DataStream &stream, const MediaPipelineStats &s) {
        stream.writeTag(DataStream::TypeMediaPipelineStats);
        const MediaPipelineStats::PerStageMap &m = s.perStage();
        stream << static_cast<uint32_t>(m.size());
        for (auto it = m.cbegin(); it != m.cend(); ++it) {
                stream << it->first;
                stream << it->second;
        }
        stream << s.aggregate();
        stream << s.pipeline();
        return stream;
}

DataStream &operator>>(DataStream &stream, MediaPipelineStats &s) {
        if (!stream.readTag(DataStream::TypeMediaPipelineStats)) {
                s = MediaPipelineStats();
                return stream;
        }
        s.clear();
        uint32_t count = 0;
        stream >> count;
        for (uint32_t i = 0; i < count && stream.status() == DataStream::Ok; ++i) {
                String       name;
                MediaIOStats stats;
                stream >> name;
                stream >> stats;
                s.perStage().insert(name, std::move(stats));
        }
        MediaIOStats  agg;
        PipelineStats pp;
        stream >> agg;
        stream >> pp;
        s.aggregate() = std::move(agg);
        s.pipeline() = std::move(pp);
        return stream;
}

PROMEKI_NAMESPACE_END
