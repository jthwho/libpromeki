/**
 * @file      mediapipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipeline.h>

#include <promeki/logger.h>
#include <promeki/set.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

// ============================================================================
// Construction
// ============================================================================

MediaPipeline::MediaPipeline(ObjectBase *parent)
        : ObjectBase(parent)
{
}

MediaPipeline::~MediaPipeline() {
        // Best-effort tear down; errors are logged by close() itself.
        (void)close();
}

// ============================================================================
// Topological sort
// ============================================================================

namespace {

// Kahn's algorithm — returns @ref Error::Invalid if a cycle is detected.
// Drops orphan stages into the output in their declared order after the
// connected components so callers see a deterministic ordering even when
// the graph has disconnected islands (validation catches orphans first,
// so this only fires for the single-stage corner case).
} // namespace

Error MediaPipeline::topologicallySort(promeki::List<String> &order) const {
        order.clear();

        promeki::Map<String, int> inDeg;
        promeki::Map<String, promeki::List<String>> adj;
        const MediaPipelineConfig::StageList &stages = _config.stages();
        for(size_t i = 0; i < stages.size(); ++i) {
                inDeg.insert(stages[i].name, 0);
                adj.insert(stages[i].name, promeki::List<String>());
        }
        const MediaPipelineConfig::RouteList &routes = _config.routes();
        for(size_t i = 0; i < routes.size(); ++i) {
                adj[routes[i].from].pushToBack(routes[i].to);
                inDeg[routes[i].to] += 1;
        }

        // Ready queue seeded with every in-degree-zero node in declared
        // order, giving us deterministic output for equivalent graphs.
        promeki::List<String> ready;
        for(size_t i = 0; i < stages.size(); ++i) {
                if(inDeg[stages[i].name] == 0) ready.pushToBack(stages[i].name);
        }

        while(!ready.isEmpty()) {
                const String n = ready.front();
                ready.remove(static_cast<size_t>(0));
                order.pushToBack(n);
                const promeki::List<String> &nbrs = adj[n];
                for(size_t i = 0; i < nbrs.size(); ++i) {
                        const String &m = nbrs[i];
                        inDeg[m] -= 1;
                        if(inDeg[m] == 0) ready.pushToBack(m);
                }
        }

        if(order.size() != stages.size()) {
                promekiErr("MediaPipeline::topologicallySort: cycle detected.");
                return Error::Invalid;
        }
        return Error::Ok;
}

// ============================================================================
// Stage instantiation
// ============================================================================

MediaIO *MediaPipeline::instantiateStage(const MediaPipelineConfig::Stage &s) {
        MediaIO *io = nullptr;
        if(!s.type.isEmpty()) {
                MediaConfig cfg = s.config;
                cfg.set(MediaConfig::Type, s.type);
                if(!s.path.isEmpty()) cfg.set(MediaConfig::Filename, s.path);
                io = MediaIO::create(cfg, this);
        } else if(!s.path.isEmpty()) {
                if(s.mode == MediaIO::Output) {
                        io = MediaIO::createForFileRead(s.path, this);
                } else {
                        io = MediaIO::createForFileWrite(s.path, this);
                }
                if(io != nullptr) {
                        MediaConfig merged = io->config();
                        s.config.forEach([&merged](MediaConfig::ID id, const Variant &val) {
                                merged.set(id, val);
                        });
                        io->setConfig(merged);
                }
        }
        if(io != nullptr && !s.metadata.isEmpty()) {
                (void)io->setMetadata(s.metadata);
        }
        return io;
}

// ============================================================================
// Lifecycle
// ============================================================================

Error MediaPipeline::destroyStages() {
        for(auto it = _stages.begin(); it != _stages.end(); ++it) {
                MediaIO *io = it->second;
                if(io == nullptr) continue;
                if(io->isOpen()) {
                        Error err = io->close();
                        if(err.isError()) {
                                promekiWarn("MediaPipeline: closing stage '%s' failed: %s",
                                            it->first.cstr(), err.desc().cstr());
                        }
                }
                // Injected stages are caller-owned; the pipeline only
                // observes the pointer.  Leave them alive so mediaplay
                // and any future external-stage user can rebuild /
                // re-inject without paying a re-create cost.
                if(!_injected.contains(it->first)) {
                        delete io;
                }
        }
        _stages.clear();
        _sources.clear();
        _topoOrder.clear();
        return Error::Ok;
}

Error MediaPipeline::injectStage(const String &name, MediaIO *io) {
        if(_state != State::Empty && _state != State::Closed) {
                promekiErr("MediaPipeline::injectStage: pipeline is not in Empty/Closed state.");
                return Error::Busy;
        }
        if(io == nullptr) {
                promekiErr("MediaPipeline::injectStage: null MediaIO for stage '%s'.",
                           name.cstr());
                return Error::InvalidArgument;
        }
        _injected.insert(name, io);
        return Error::Ok;
}

Error MediaPipeline::build(const MediaPipelineConfig &config) {
        if(_state != State::Empty && _state != State::Closed) {
                promekiErr("MediaPipeline::build: pipeline is not in Empty/Closed state.");
                return Error::Busy;
        }

        Error vErr = config.validate();
        if(vErr.isError()) return vErr;

        // Fan-in check — for this first implementation a stage may have
        // at most one incoming route.  Fan-out is unrestricted.
        promeki::Map<String, int> inCount;
        for(size_t i = 0; i < config.stages().size(); ++i) {
                inCount.insert(config.stages()[i].name, 0);
        }
        for(size_t i = 0; i < config.routes().size(); ++i) {
                inCount[config.routes()[i].to] += 1;
        }
        for(auto it = inCount.cbegin(); it != inCount.cend(); ++it) {
                if(it->second > 1) {
                        promekiErr("MediaPipeline::build: stage '%s' has %d incoming routes; "
                                   "fan-in is not supported in this implementation.",
                                   it->first.cstr(), it->second);
                        return Error::NotSupported;
                }
        }

        _config = config;

        // Instantiate every stage.  Injected stages short-circuit the
        // backend factory so externally-owned MediaIOs (SDL, V4L2
        // device handles, etc.) participate in the drain without
        // having to be registered in MediaIO::registeredFormats.
        for(size_t i = 0; i < _config.stages().size(); ++i) {
                const MediaPipelineConfig::Stage &s = _config.stages()[i];
                MediaIO *io = nullptr;
                auto injIt = _injected.find(s.name);
                if(injIt != _injected.end()) {
                        io = injIt->second;
                } else {
                        io = instantiateStage(s);
                }
                if(io == nullptr) {
                        promekiErr("MediaPipeline::build: stage '%s' instantiation failed.",
                                   s.name.cstr());
                        destroyStages();
                        return Error::OpenFailed;
                }
                _stages.insert(s.name, io);
        }

        // Build per-source edge state from the routes.
        for(size_t i = 0; i < _config.routes().size(); ++i) {
                const MediaPipelineConfig::Route &r = _config.routes()[i];
                if(!_sources.contains(r.from)) {
                        SourceState ss;
                        ss.from = _stages[r.from];
                        _sources.insert(r.from, ss);
                }
                EdgeState es;
                es.toName = r.to;
                es.to     = _stages[r.to];
                _sources[r.from].edges.pushToBack(es);
        }

        Error tErr = topologicallySort(_topoOrder);
        if(tErr.isError()) {
                destroyStages();
                return tErr;
        }

        _state = State::Built;
        return Error::Ok;
}

Error MediaPipeline::open() {
        if(_state != State::Built) {
                promekiErr("MediaPipeline::open: pipeline is not in Built state.");
                return Error::NotOpen;
        }

        // Open in forward topological order so each downstream stage
        // sees its upstream's freshly-resolved MediaDesc / AudioDesc /
        // Metadata.  For every stage that has an incoming route (i.e.
        // isn't a pure source), copy the upstream's live descriptors
        // onto it with setMediaDesc / setAudioDesc / setMetadata before
        // opening — many backends (QuickTime writer, ImageFile,
        // AudioFile, Converter) rely on the pre-open descriptor to
        // configure themselves.  Pipeline-level per-stage metadata
        // from @ref MediaPipelineConfig::Stage::metadata is merged on
        // top of the inherited metadata so the user's overrides always
        // win over the upstream defaults.
        //
        // Build a quick from-lookup for the "who feeds me?" query; the
        // @c build step already rejects fan-in so every non-source
        // stage has exactly one upstream.
        promeki::Map<String, String> upstreamOf;
        for(size_t i = 0; i < _config.routes().size(); ++i) {
                const MediaPipelineConfig::Route &r = _config.routes()[i];
                upstreamOf.insert(r.to, r.from);
        }

        promeki::List<String> opened;
        for(size_t i = 0; i < _topoOrder.size(); ++i) {
                const String &name = _topoOrder[i];
                const MediaPipelineConfig::Stage *spec = _config.findStage(name);
                if(spec == nullptr) continue; // cannot happen — build validated this
                MediaIO *io = _stages[name];

                auto upIt = upstreamOf.find(name);
                if(upIt != upstreamOf.end()) {
                        MediaIO *up = _stages[upIt->second];
                        if(up != nullptr && up->isOpen()) {
                                (void)io->setMediaDesc(up->mediaDesc());
                                const AudioDesc &ad = up->audioDesc();
                                if(ad.isValid()) (void)io->setAudioDesc(ad);
                                Metadata merged = up->metadata();
                                if(!spec->metadata.isEmpty()) {
                                        merged.merge(spec->metadata);
                                }
                                if(!merged.isEmpty()) (void)io->setMetadata(merged);
                        } else if(!spec->metadata.isEmpty()) {
                                (void)io->setMetadata(spec->metadata);
                        }
                } else if(!spec->metadata.isEmpty()) {
                        (void)io->setMetadata(spec->metadata);
                }

                Error err = io->open(spec->mode);
                if(err.isError()) {
                        promekiErr("MediaPipeline::open: stage '%s' open(%d) failed: %s",
                                   name.cstr(),
                                   static_cast<int>(spec->mode),
                                   err.desc().cstr());
                        // Unwind the already-opened stages in reverse order.
                        for(size_t j = opened.size(); j-- > 0; ) {
                                MediaIO *io2 = _stages[opened[j]];
                                if(io2 != nullptr) (void)io2->close();
                        }
                        return err;
                }
                opened.pushToBack(name);
                stageOpenedSignal.emit(name);
        }
        _state = State::Open;
        return Error::Ok;
}

Error MediaPipeline::start() {
        if(_state != State::Open) {
                promekiErr("MediaPipeline::start: pipeline is not in Open state.");
                return Error::NotOpen;
        }

        // Wire drain handlers for every source and edge.
        for(auto it = _sources.begin(); it != _sources.end(); ++it) {
                const String srcName = it->first;
                MediaIO *srcIO = it->second.from;
                srcIO->frameReadySignal.connect(
                        [this, srcName]() { drainSource(srcName); }, this);
                srcIO->writeErrorSignal.connect(
                        [this, srcName](Error e) { onWriteError(srcName, e); }, this);

                // Each outgoing edge reopens the drain when the consumer
                // reports it can accept more.
                for(size_t i = 0; i < it->second.edges.size(); ++i) {
                        MediaIO *to = it->second.edges[i].to;
                        to->frameWantedSignal.connect(
                                [this, srcName]() { drainSource(srcName); }, this);
                }
                stageStartedSignal.emit(srcName);
        }

        // Connect writeError for every stage that isn't a pure source
        // (we already hooked sources above); pure sinks need it too.
        for(auto it = _stages.begin(); it != _stages.end(); ++it) {
                if(_sources.contains(it->first)) continue;
                const String stageName = it->first;
                MediaIO *io = it->second;
                io->writeErrorSignal.connect(
                        [this, stageName](Error e) { onWriteError(stageName, e); }, this);
                stageStartedSignal.emit(stageName);
        }

        _state = State::Running;
        _finished = false;
        _cleanFinish = false;
        _framesProduced.setValue(0);
        _writeRetries.setValue(0);
        _pipelineErrors.setValue(0);
        _uptime.start();
        _uptimeStarted = true;

        // Prime the drain so the first readFrame kicks prefetch on every
        // source stage.  Subsequent iterations happen off frameReady.
        for(auto it = _sources.begin(); it != _sources.end(); ++it) {
                drainSource(it->first);
        }

        return Error::Ok;
}

Error MediaPipeline::stop() {
        if(_state != State::Running) {
                promekiErr("MediaPipeline::stop: pipeline is not in Running state.");
                return Error::NotOpen;
        }

        // Signals are connected with @c this as the ObjectBase context;
        // the ObjectBase destructor disconnects automatically but for an
        // explicit stop we want to drop connections now.  The signal layer
        // does not expose bulk disconnect-by-owner at this time, so we
        // rely on each backend's cancelPending() to drain in-flight work
        // and simply let subsequent signal emissions be no-ops once
        // _state leaves Running.
        for(auto it = _stages.begin(); it != _stages.end(); ++it) {
                if(it->second != nullptr) {
                        (void)it->second->cancelPending();
                        stageStoppedSignal.emit(it->first);
                }
        }

        _state = State::Stopped;
        return Error::Ok;
}

Error MediaPipeline::close() {
        if(_state == State::Empty || _state == State::Closed) {
                _state = State::Closed;
                return Error::Ok;
        }
        if(_state == State::Running) {
                (void)stop();
        }

        // Close in topological order so sources flush before their
        // downstream sinks release resources they depend on.
        for(size_t i = 0; i < _topoOrder.size(); ++i) {
                const String &name = _topoOrder[i];
                auto it = _stages.find(name);
                if(it == _stages.end() || it->second == nullptr) continue;
                if(!it->second->isOpen()) continue;
                Error err = it->second->close();
                if(err.isError()) {
                        promekiWarn("MediaPipeline::close: stage '%s' close failed: %s",
                                    name.cstr(), err.desc().cstr());
                }
                stageClosedSignal.emit(name);
        }

        destroyStages();
        _state = State::Closed;
        return Error::Ok;
}

// ============================================================================
// Drain
// ============================================================================

void MediaPipeline::drainSource(const String &srcName) {
        if(_state != State::Running) return;
        auto it = _sources.find(srcName);
        if(it == _sources.end()) return;
        SourceState &ss = it->second;
        if(ss.from == nullptr) return;

        // Back-pressure: we gate on @ref MediaIO::writesAccepted for
        // every outgoing edge before pulling the next source frame.
        // Under the documented single-threaded driving contract, a
        // non-blocking @c writeFrame called after a positive
        // @c writesAccepted can only return @c Error::Ok — anything
        // else is a contract violation we want to hear about loudly,
        // not silently absorb.  Async write failures still arrive on
        // @c writeErrorSignal → @ref onWriteError.
        while(true) {
                for(size_t i = 0; i < ss.edges.size(); ++i) {
                        const EdgeState &e = ss.edges[i];
                        if(e.to == nullptr) return;
                        if(e.to->writesAccepted() <= 0) return;
                }

                Frame::Ptr frame;
                Error err = ss.from->readFrame(frame, false);
                if(err == Error::TryAgain) return;
                if(err == Error::EndOfFile) {
                        ss.upstreamDone = true;
                        checkDrained();
                        return;
                }
                if(err.isError()) {
                        if(err != Error::Cancelled) {
                                _pipelineErrors.fetchAndAdd(1);
                                pipelineErrorSignal.emit(srcName, err);
                        }
                        finish(false);
                        return;
                }

                _framesProduced.fetchAndAdd(1);

                for(size_t i = 0; i < ss.edges.size(); ++i) {
                        EdgeState &e = ss.edges[i];
                        if(e.to == nullptr) continue;
                        Error werr = e.to->writeFrame(frame, false);
                        if(werr.isError()) {
                                if(werr == Error::TryAgain) {
                                        // writesAccepted said yes; the
                                        // backend said no.  That's a
                                        // contract violation worth
                                        // crashing on rather than
                                        // silently dropping a frame.
                                        _writeRetries.fetchAndAdd(1);
                                        PROMEKI_ASSERT(werr != Error::TryAgain);
                                }
                                onWriteError(e.toName, werr);
                                return;
                        }
                }
        }
}

void MediaPipeline::onWriteError(const String &stageName, Error err) {
        if(err == Error::TryAgain) return;
        if(err != Error::Cancelled) {
                _pipelineErrors.fetchAndAdd(1);
                pipelineErrorSignal.emit(stageName, err);
        }
        finish(false);
}

void MediaPipeline::checkDrained() {
        // Every source must have hit EOF before we call the pipeline
        // done.  Sinks may still have in-flight writes of their own;
        // those are bounded by the strands' work queues and will
        // complete on their own — no per-edge hold to wait on now
        // that the writesAccepted gate guarantees every fan-out
        // succeeds synchronously.
        for(auto it = _sources.cbegin(); it != _sources.cend(); ++it) {
                if(!it->second.upstreamDone) return;
        }
        finish(true);
}

void MediaPipeline::finish(bool clean) {
        if(_finished) return;
        _finished     = true;
        _cleanFinish  = clean;
        finishedSignal.emit(clean);
}

// ============================================================================
// Introspection
// ============================================================================

MediaIO *MediaPipeline::stage(const String &name) const {
        auto it = _stages.find(name);
        if(it == _stages.end()) return nullptr;
        return it->second;
}

StringList MediaPipeline::stageNames() const {
        StringList names;
        for(size_t i = 0; i < _topoOrder.size(); ++i) names.pushToBack(_topoOrder[i]);
        return names;
}

StringList MediaPipeline::describe() const {
        StringList out = _config.describe();
        if(_stages.isEmpty()) return out;

        out.pushToBack("Live state:");
        for(size_t i = 0; i < _topoOrder.size(); ++i) {
                const String &name = _topoOrder[i];
                auto it = _stages.find(name);
                if(it == _stages.end() || it->second == nullptr) continue;
                MediaIO *io = it->second;
                String line = "  ";
                line += name;
                line += ": ";
                if(io->isOpen()) {
                        const MediaDesc &md = io->mediaDesc();
                        line += "fps=";
                        line += md.frameRate().toString();
                        line += " images=";
                        line += String::number(static_cast<uint64_t>(md.imageList().size()));
                        line += " audio=";
                        line += String::number(static_cast<uint64_t>(md.audioList().size()));
                } else {
                        line += "(not open)";
                }
                out.pushToBack(line);
        }
        return out;
}

MediaPipelineStats MediaPipeline::stats() {
        MediaPipelineStats out;
        for(auto it = _stages.begin(); it != _stages.end(); ++it) {
                if(it->second == nullptr) continue;
                if(!it->second->isOpen()) continue;
                out.setStageStats(it->first, it->second->stats());
        }
        out.recomputeAggregate();

        // Pipeline-layer counters.  Derived quantities (SourcesAtEof,
        // PausedEdges) are computed fresh on every call so callers get
        // a consistent view of the drain loop's current shape.
        PipelineStats &pp = out.pipeline();
        pp.set(PipelineStats::FramesProduced, _framesProduced.value());
        pp.set(PipelineStats::WriteRetries,   _writeRetries.value());
        pp.set(PipelineStats::PipelineErrors, _pipelineErrors.value());

        int64_t sourcesAtEof = 0;
        for(auto it = _sources.cbegin(); it != _sources.cend(); ++it) {
                if(it->second.upstreamDone) sourcesAtEof++;
        }
        pp.set(PipelineStats::SourcesAtEof, sourcesAtEof);
        // PausedEdges stays at its spec default (0) for now — no edge
        // can hold a frame under the current single-input drain model.
        // Reserved for the future fan-in extension that lifts the
        // current "one incoming route per stage" rule.
        pp.set(PipelineStats::PausedEdges, int64_t(0));

        const char *stateStr = "Empty";
        switch(_state) {
                case State::Empty:   stateStr = "Empty";   break;
                case State::Built:   stateStr = "Built";   break;
                case State::Open:    stateStr = "Open";    break;
                case State::Running: stateStr = "Running"; break;
                case State::Stopped: stateStr = "Stopped"; break;
                case State::Closed:  stateStr = "Closed";  break;
        }
        pp.set(PipelineStats::State, String(stateStr));

        if(_uptimeStarted) {
                pp.set(PipelineStats::UptimeMs, _uptime.elapsed());
        }
        return out;
}

PROMEKI_NAMESPACE_END
