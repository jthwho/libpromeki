/**
 * @file      mediapipelineconfig.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipelineconfig.h>

#include <promeki/file.h>
#include <promeki/iodevice.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/map.h>
#include <promeki/mediapipelineplanner.h>
#include <promeki/set.h>
#include <promeki/variantspec.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Stage helpers
// ============================================================================

bool MediaPipelineConfig::Stage::operator==(const Stage &other) const {
        return name == other.name
            && type == other.type
            && path == other.path
            && mode == other.mode
            && config == other.config
            && metadata == other.metadata;
}

// ============================================================================
// Mode name round-trip
// ============================================================================

String MediaPipelineConfig::modeName(MediaIO::Mode mode) {
        switch(mode) {
                case MediaIO::Source:    return String("Source");
                case MediaIO::Sink:      return String("Sink");
                case MediaIO::Transform: return String("Transform");
                case MediaIO::NotOpen:
                default:                 return String("NotOpen");
        }
}

MediaIO::Mode MediaPipelineConfig::modeFromName(const String &name, Error *err) {
        if(err) *err = Error::Ok;
        if(name == "Source")    return MediaIO::Source;
        if(name == "Sink")      return MediaIO::Sink;
        if(name == "Transform") return MediaIO::Transform;
        if(name == "NotOpen" || name.isEmpty()) return MediaIO::NotOpen;
        if(err) *err = Error::Invalid;
        return MediaIO::NotOpen;
}

// ============================================================================
// Container accessors
// ============================================================================

const MediaPipelineConfig::Stage *MediaPipelineConfig::findStage(const String &name) const {
        for(size_t i = 0; i < _stages.size(); ++i) {
                if(_stages[i].name == name) return &_stages[i];
        }
        return nullptr;
}

StringList MediaPipelineConfig::stageNames() const {
        StringList names;
        for(size_t i = 0; i < _stages.size(); ++i) names.pushToBack(_stages[i].name);
        return names;
}

void MediaPipelineConfig::addRoute(const String &from, const String &to) {
        Route r;
        r.from = from;
        r.to   = to;
        _routes.pushToBack(r);
}

bool MediaPipelineConfig::operator==(const MediaPipelineConfig &other) const {
        return _stages == other._stages
            && _routes == other._routes
            && _pipelineMetadata == other._pipelineMetadata
            && _frameCount == other._frameCount;
}

// ============================================================================
// JSON serialization
// ============================================================================

JsonObject MediaPipelineConfig::toJson() const {
        JsonObject j;
        if(!_pipelineMetadata.isEmpty()) j.set("metadata", _pipelineMetadata.toJson());
        // Only serialize frameCount when it is a concrete positive limit.
        // The default (unknown / infinite / empty) is "no cap" and round-
        // trips through the absence of the key.
        if(_frameCount.isFinite() && !_frameCount.isEmpty()) {
                j.set("frameCount", _frameCount.value());
        }

        JsonArray stageArr;
        for(size_t i = 0; i < _stages.size(); ++i) {
                const Stage &s = _stages[i];
                JsonObject sj;
                sj.set("name", s.name);
                if(!s.type.isEmpty()) sj.set("type", s.type);
                if(!s.path.isEmpty()) sj.set("path", s.path);
                sj.set("mode", modeName(s.mode));
                sj.set("config", s.config.toJson());
                if(!s.metadata.isEmpty()) sj.set("metadata", s.metadata.toJson());
                stageArr.add(sj);
        }
        j.set("stages", stageArr);

        JsonArray routeArr;
        for(size_t i = 0; i < _routes.size(); ++i) {
                const Route &r = _routes[i];
                JsonObject rj;
                rj.set("from", r.from);
                rj.set("to",   r.to);
                if(!r.fromTrack.isEmpty()) rj.set("fromTrack", r.fromTrack);
                if(!r.toTrack.isEmpty())   rj.set("toTrack",   r.toTrack);
                routeArr.add(rj);
        }
        j.set("routes", routeArr);

        return j;
}

MediaPipelineConfig MediaPipelineConfig::fromJson(const JsonObject &obj, Error *err) {
        MediaPipelineConfig cfg;
        bool good = true;

        if(obj.valueIsObject("metadata")) {
                Error mErr;
                cfg._pipelineMetadata = Metadata::fromJson(obj.getObject("metadata"), &mErr);
                if(mErr.isError()) {
                        promekiWarn("MediaPipelineConfig::fromJson: pipeline metadata invalid.");
                        good = false;
                }
        }

        if(obj.contains("frameCount")) {
                Error fcErr;
                int64_t fc = obj.getInt("frameCount", &fcErr);
                if(fcErr.isOk() && fc >= 0) {
                        cfg._frameCount = FrameCount(fc);
                } else {
                        promekiWarn("MediaPipelineConfig::fromJson: frameCount must be a non-negative integer.");
                        good = false;
                }
        }

        if(obj.valueIsArray("stages")) {
                JsonArray stages = obj.getArray("stages");
                for(int i = 0; i < stages.size(); ++i) {
                        if(!stages.valueIsObject(i)) {
                                promekiWarn("MediaPipelineConfig::fromJson: stages[%d] is not an object.", i);
                                good = false;
                                continue;
                        }
                        JsonObject sj = stages.getObject(i);
                        Stage s;
                        s.name = sj.getString("name");
                        s.type = sj.getString("type");
                        s.path = sj.getString("path");
                        const String modeStr = sj.getString("mode");
                        if(!modeStr.isEmpty()) {
                                Error mErr;
                                s.mode = modeFromName(modeStr, &mErr);
                                if(mErr.isError()) {
                                        promekiWarn("MediaPipelineConfig::fromJson: stage '%s' unknown mode '%s'.",
                                                    s.name.cstr(), modeStr.cstr());
                                        good = false;
                                }
                        }
                        if(sj.valueIsObject("config")) {
                                JsonObject cj = sj.getObject("config");
                                // Delegate per-entry coercion to VariantDatabase so
                                // spec-driven type restoration happens uniformly
                                // across every subclass.
                                cj.forEach([&s](const String &key, const Variant &val) {
                                        s.config.setFromJson(MediaConfig::ID(key), val);
                                });
                        }
                        if(sj.valueIsObject("metadata")) {
                                Error mErr;
                                s.metadata = Metadata::fromJson(sj.getObject("metadata"), &mErr);
                                if(mErr.isError()) good = false;
                        }
                        cfg._stages.pushToBack(std::move(s));
                }
        }

        if(obj.valueIsArray("routes")) {
                JsonArray routes = obj.getArray("routes");
                for(int i = 0; i < routes.size(); ++i) {
                        if(!routes.valueIsObject(i)) {
                                promekiWarn("MediaPipelineConfig::fromJson: routes[%d] is not an object.", i);
                                good = false;
                                continue;
                        }
                        JsonObject rj = routes.getObject(i);
                        Route r;
                        r.from      = rj.getString("from");
                        r.to        = rj.getString("to");
                        r.fromTrack = rj.getString("fromTrack");
                        r.toTrack   = rj.getString("toTrack");
                        cfg._routes.pushToBack(std::move(r));
                }
        }

        if(err) *err = good ? Error::Ok : Error::Invalid;
        return cfg;
}

// ============================================================================
// File I/O
// ============================================================================

Error MediaPipelineConfig::saveToFile(const FilePath &path, unsigned int indent) const {
        File f(path);
        Error err = f.open(IODevice::WriteOnly, File::Create | File::Truncate);
        if(err.isError()) {
                promekiErr("MediaPipelineConfig::saveToFile: open '%s' failed: %s",
                           path.toString().cstr(), err.desc().cstr());
                return err;
        }
        const String text = toJson().toString(indent);
        const int64_t expected = static_cast<int64_t>(text.size());
        int64_t n = f.write(text.cstr(), expected);
        f.close();
        if(n != expected) {
                promekiErr("MediaPipelineConfig::saveToFile: short write to '%s' (%lld of %zu bytes)",
                           path.toString().cstr(),
                           static_cast<long long>(n), text.size());
                return Error::IOError;
        }
        return Error::Ok;
}

MediaPipelineConfig MediaPipelineConfig::loadFromFile(const FilePath &path, Error *err) {
        File f(path);
        Error oerr = f.open(IODevice::ReadOnly);
        if(oerr.isError()) {
                promekiErr("MediaPipelineConfig::loadFromFile: open '%s' failed: %s",
                           path.toString().cstr(), oerr.desc().cstr());
                if(err) *err = oerr;
                return MediaPipelineConfig();
        }
        Result<int64_t> sz = f.size();
        int64_t fileSize = sz.second().isOk() ? sz.first() : 0;
        if(fileSize <= 0) fileSize = 16384;
        promeki::List<char> scratch;
        scratch.resize(static_cast<size_t>(fileSize));
        int64_t nread = f.read(scratch.data(), static_cast<int64_t>(scratch.size()));
        f.close();
        if(nread <= 0) {
                promekiErr("MediaPipelineConfig::loadFromFile: '%s' is empty or unreadable",
                           path.toString().cstr());
                if(err) *err = Error::IOError;
                return MediaPipelineConfig();
        }
        const String text(scratch.data(), static_cast<size_t>(nread));
        Error parseErr;
        JsonObject obj = JsonObject::parse(text, &parseErr);
        if(parseErr.isError()) {
                promekiErr("MediaPipelineConfig::loadFromFile: '%s' is not valid JSON",
                           path.toString().cstr());
                if(err) *err = parseErr;
                return MediaPipelineConfig();
        }
        return fromJson(obj, err);
}

// ============================================================================
// Validation
// ============================================================================

namespace {

// Validates every value in @p cfg against its registered VariantSpec.
// For a stage bound to a registered backend, the backend's spec map
// takes precedence (backends may narrow or widen the global spec).
// Falls back to the global MediaConfig registry for keys the backend
// does not list explicitly.  Unknown keys and out-of-spec values both
// emit a warning; the first out-of-spec value is reported as the
// structural error.
Error validateStageConfig(const MediaPipelineConfig::Stage &stage) {
        MediaConfig::SpecMap backendSpecs;
        if(!stage.type.isEmpty()) {
                backendSpecs = MediaIO::configSpecs(stage.type);
                const StringList unknown = MediaIO::unknownConfigKeys(stage.type, stage.config);
                for(size_t i = 0; i < unknown.size(); ++i) {
                        promekiWarn("MediaPipelineConfig: stage '%s' has unrecognized config key '%s'.",
                                    stage.name.cstr(), unknown[i].cstr());
                }
        }

        Error firstErr = Error::Ok;
        stage.config.forEach([&](MediaConfig::ID id, const Variant &val) {
                const VariantSpec *spec = nullptr;
                auto it = backendSpecs.find(id);
                if(it != backendSpecs.end()) spec = &it->second;
                else                          spec = MediaConfig::spec(id);
                if(spec == nullptr) return; // unknown — already warned above
                Error specErr;
                if(!spec->validate(val, &specErr)) {
                        promekiWarn("MediaPipelineConfig: stage '%s' key '%s' fails spec (%s).",
                                    stage.name.cstr(), id.name().cstr(),
                                    specErr.name().cstr());
                        if(firstErr.isOk()) firstErr = specErr;
                }
        });
        return firstErr;
}

// DFS with three-state colouring (white / gray / black) flags a cycle
// the moment a route lands on a gray stage.  Stages unreferenced by
// any route are simply visited-and-done.
enum class VisitColor { White, Gray, Black };

bool hasCycleDfs(const String &node,
                 const Map<String, promeki::List<String>> &adj,
                 Map<String, VisitColor> &color) {
        auto &c = color[node];
        c = VisitColor::Gray;
        auto it = adj.find(node);
        if(it != adj.end()) {
                const promeki::List<String> &edges = it->second;
                for(size_t i = 0; i < edges.size(); ++i) {
                        const String &next = edges[i];
                        auto nextIt = color.find(next);
                        if(nextIt == color.end()) continue;
                        VisitColor nc = nextIt->second;
                        if(nc == VisitColor::Gray) return true;
                        if(nc == VisitColor::White) {
                                if(hasCycleDfs(next, adj, color)) return true;
                        }
                }
        }
        color[node] = VisitColor::Black;
        return false;
}

} // namespace

// ============================================================================
// Planner-driven helpers
// ============================================================================
//
// Both helpers are thin wrappers around MediaPipelinePlanner.  They
// live here so callers can ask a config to resolve itself without
// having to know the planner type — same shape as JsonObject's static
// parse() factory wraps the underlying nlohmann::json call.

bool MediaPipelineConfig::isResolved(String *diagnostic) const {
        return MediaPipelinePlanner::isResolved(*this, diagnostic);
}

MediaPipelineConfig MediaPipelineConfig::resolved(Error *err,
                                                  String *diagnostic) const {
        MediaPipelineConfig out;
        Error perr = MediaPipelinePlanner::plan(*this, &out, {}, diagnostic);
        if(err != nullptr) *err = perr;
        if(perr.isError()) return MediaPipelineConfig();
        return out;
}

Error MediaPipelineConfig::validate() const {
        if(_stages.isEmpty()) {
                promekiWarn("MediaPipelineConfig::validate: pipeline has no stages.");
                return Error::Invalid;
        }

        // 1. Per-stage structural checks + unique names.
        Set<String> seenNames;
        for(size_t i = 0; i < _stages.size(); ++i) {
                const Stage &s = _stages[i];
                if(s.name.isEmpty()) {
                        promekiErr("MediaPipelineConfig::validate: stage #%zu has no name.", i);
                        return Error::InvalidArgument;
                }
                if(seenNames.contains(s.name)) {
                        promekiErr("MediaPipelineConfig::validate: duplicate stage name '%s'.",
                                   s.name.cstr());
                        return Error::InvalidArgument;
                }
                seenNames.insert(s.name);
                if(s.type.isEmpty() && s.path.isEmpty()) {
                        promekiErr("MediaPipelineConfig::validate: stage '%s' has neither type nor path.",
                                   s.name.cstr());
                        return Error::InvalidArgument;
                }
                if(s.mode != MediaIO::Source
                   && s.mode != MediaIO::Sink
                   && s.mode != MediaIO::Transform) {
                        promekiErr("MediaPipelineConfig::validate: stage '%s' has invalid mode.",
                                   s.name.cstr());
                        return Error::InvalidArgument;
                }
                Error cfgErr = validateStageConfig(s);
                if(cfgErr.isError()) return cfgErr;
        }

        // 2. Route structural checks + adjacency graph for cycle detection.
        Map<String, promeki::List<String>> adj;
        for(size_t i = 0; i < _stages.size(); ++i) {
                adj.insert(_stages[i].name, promeki::List<String>());
        }
        Set<String> stagesSeenInRoutes;
        for(size_t i = 0; i < _routes.size(); ++i) {
                const Route &r = _routes[i];
                if(r.from.isEmpty() || r.to.isEmpty()) {
                        promekiErr("MediaPipelineConfig::validate: route #%zu has empty endpoint.", i);
                        return Error::InvalidArgument;
                }
                if(!seenNames.contains(r.from)) {
                        promekiErr("MediaPipelineConfig::validate: route '%s→%s' from-stage not found.",
                                   r.from.cstr(), r.to.cstr());
                        return Error::InvalidArgument;
                }
                if(!seenNames.contains(r.to)) {
                        promekiErr("MediaPipelineConfig::validate: route '%s→%s' to-stage not found.",
                                   r.from.cstr(), r.to.cstr());
                        return Error::InvalidArgument;
                }
                if(r.from == r.to) {
                        promekiErr("MediaPipelineConfig::validate: route '%s→%s' is a self-loop.",
                                   r.from.cstr(), r.to.cstr());
                        return Error::InvalidArgument;
                }
                adj[r.from].pushToBack(r.to);
                stagesSeenInRoutes.insert(r.from);
                stagesSeenInRoutes.insert(r.to);
        }

        // 3. Cycle check via DFS coloring.
        Map<String, VisitColor> color;
        for(size_t i = 0; i < _stages.size(); ++i) {
                color.insert(_stages[i].name, VisitColor::White);
        }
        for(size_t i = 0; i < _stages.size(); ++i) {
                const String &n = _stages[i].name;
                if(color[n] == VisitColor::White) {
                        if(hasCycleDfs(n, adj, color)) {
                                promekiErr("MediaPipelineConfig::validate: route graph contains a cycle.");
                                return Error::Invalid;
                        }
                }
        }

        // 4. Orphan check — every stage must participate in a route
        //    unless the config is a single-stage self-test pipeline.
        if(_stages.size() > 1) {
                for(size_t i = 0; i < _stages.size(); ++i) {
                        const String &n = _stages[i].name;
                        if(!stagesSeenInRoutes.contains(n)) {
                                promekiErr("MediaPipelineConfig::validate: stage '%s' is not referenced by any route.",
                                           n.cstr());
                                return Error::InvalidArgument;
                        }
                }
        }

        return Error::Ok;
}

// ============================================================================
// describe()
// ============================================================================

StringList MediaPipelineConfig::describe() const {
        StringList out;
        if(!_pipelineMetadata.isEmpty()) {
                out.pushToBack("Pipeline metadata:");
                const StringList md = _pipelineMetadata.dump();
                for(size_t i = 0; i < md.size(); ++i) {
                        out.pushToBack(String("  ") + md[i]);
                }
        }
        if(_frameCount.isFinite() && !_frameCount.isEmpty()) {
                out.pushToBack(String("Frame count limit: ") + _frameCount.toString());
        }

        out.pushToBack(String("Stages (") + String::number(_stages.size()) + "):");
        for(size_t i = 0; i < _stages.size(); ++i) {
                const Stage &s = _stages[i];
                String hdr = "  ";
                hdr += s.name;
                hdr += " [";
                if(!s.type.isEmpty()) {
                        hdr += s.type;
                } else if(!s.path.isEmpty()) {
                        hdr += "file:";
                        hdr += s.path;
                } else {
                        hdr += "<unspecified>";
                }
                hdr += ", ";
                hdr += modeName(s.mode);
                hdr += "]";
                if(!s.type.isEmpty() && !s.path.isEmpty()) {
                        hdr += "  path=";
                        hdr += s.path;
                }
                out.pushToBack(hdr);

                s.config.forEach([&out](MediaConfig::ID id, const Variant &val) {
                        String line = "      ";
                        line += id.name();
                        line += " = ";
                        line += val.get<String>();
                        out.pushToBack(line);
                });

                if(!s.metadata.isEmpty()) {
                        out.pushToBack("      [metadata]");
                        const StringList md = s.metadata.dump();
                        for(size_t j = 0; j < md.size(); ++j) {
                                out.pushToBack(String("        ") + md[j]);
                        }
                }
        }

        out.pushToBack(String("Routes (") + String::number(_routes.size()) + "):");
        for(size_t i = 0; i < _routes.size(); ++i) {
                const Route &r = _routes[i];
                String line = "  ";
                line += r.from;
                if(!r.fromTrack.isEmpty()) {
                        line += ":";
                        line += r.fromTrack;
                }
                line += " -> ";
                line += r.to;
                if(!r.toTrack.isEmpty()) {
                        line += ":";
                        line += r.toTrack;
                }
                out.pushToBack(line);
        }
        return out;
}

// ============================================================================
// DataStream
// ============================================================================

DataStream &operator<<(DataStream &stream, const MediaPipelineConfig::Stage &s) {
        stream.writeTag(DataStream::TypeMediaPipelineStage);
        stream << s.name;
        stream << s.type;
        stream << s.path;
        stream << static_cast<int32_t>(s.mode);
        stream << s.config;
        stream << s.metadata;
        return stream;
}

DataStream &operator>>(DataStream &stream, MediaPipelineConfig::Stage &s) {
        if(!stream.readTag(DataStream::TypeMediaPipelineStage)) {
                s = MediaPipelineConfig::Stage();
                return stream;
        }
        int32_t m = 0;
        stream >> s.name;
        stream >> s.type;
        stream >> s.path;
        stream >> m;
        s.mode = static_cast<MediaIO::Mode>(m);
        stream >> s.config;
        stream >> s.metadata;
        return stream;
}

DataStream &operator<<(DataStream &stream, const MediaPipelineConfig::Route &r) {
        stream.writeTag(DataStream::TypeMediaPipelineRoute);
        stream << r.from;
        stream << r.to;
        stream << r.fromTrack;
        stream << r.toTrack;
        return stream;
}

DataStream &operator>>(DataStream &stream, MediaPipelineConfig::Route &r) {
        if(!stream.readTag(DataStream::TypeMediaPipelineRoute)) {
                r = MediaPipelineConfig::Route();
                return stream;
        }
        stream >> r.from;
        stream >> r.to;
        stream >> r.fromTrack;
        stream >> r.toTrack;
        return stream;
}

DataStream &operator<<(DataStream &stream, const MediaPipelineConfig &c) {
        stream.writeTag(DataStream::TypeMediaPipelineConfig);
        stream << c.pipelineMetadata();
        stream << c.stages();
        stream << c.routes();
        stream << c.frameCount();
        return stream;
}

DataStream &operator>>(DataStream &stream, MediaPipelineConfig &c) {
        if(!stream.readTag(DataStream::TypeMediaPipelineConfig)) {
                c = MediaPipelineConfig();
                return stream;
        }
        Metadata meta;
        MediaPipelineConfig::StageList stageList;
        MediaPipelineConfig::RouteList routeList;
        FrameCount frameCount;
        stream >> meta;
        stream >> stageList;
        stream >> routeList;
        stream >> frameCount;
        c.setPipelineMetadata(meta);
        c.stages() = std::move(stageList);
        c.routes() = std::move(routeList);
        c.setFrameCount(frameCount);
        return stream;
}

PROMEKI_NAMESPACE_END
