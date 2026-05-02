/**
 * @file      mediapipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipeline.h>

#include <promeki/eventloop.h>
#include <promeki/logger.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiodescription.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaioportconnection.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediaiostatscollector.h>
#include <promeki/mediapipelineplanner.h>
#include <promeki/objectbase.tpp>
#include <promeki/set.h>
#include <promeki/thread.h>
#include <promeki/timerevent.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(MediaPipeline)

// ============================================================================
// Construction
// ============================================================================

MediaPipeline::MediaPipeline(ObjectBase *parent) : ObjectBase(parent) {}

MediaPipeline::~MediaPipeline() {
        // Best-effort tear down; errors are logged by close() itself.
        (void)close();
        removeLoggerTap();
        stopStatsTimer();
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

        promeki::Map<String, int>                   inDeg;
        promeki::Map<String, promeki::List<String>> adj;
        const MediaPipelineConfig::StageList       &stages = _config.stages();
        for (size_t i = 0; i < stages.size(); ++i) {
                inDeg.insert(stages[i].name, 0);
                adj.insert(stages[i].name, promeki::List<String>());
        }
        const MediaPipelineConfig::RouteList &routes = _config.routes();
        for (size_t i = 0; i < routes.size(); ++i) {
                adj[routes[i].from].pushToBack(routes[i].to);
                inDeg[routes[i].to] += 1;
        }

        // Ready queue seeded with every in-degree-zero node in declared
        // order, giving us deterministic output for equivalent graphs.
        promeki::List<String> ready;
        for (size_t i = 0; i < stages.size(); ++i) {
                if (inDeg[stages[i].name] == 0) ready.pushToBack(stages[i].name);
        }

        while (!ready.isEmpty()) {
                const String n = ready.front();
                ready.remove(static_cast<size_t>(0));
                order.pushToBack(n);
                const promeki::List<String> &nbrs = adj[n];
                for (size_t i = 0; i < nbrs.size(); ++i) {
                        const String &m = nbrs[i];
                        inDeg[m] -= 1;
                        if (inDeg[m] == 0) ready.pushToBack(m);
                }
        }

        if (order.size() != stages.size()) {
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
        if (!s.type.isEmpty()) {
                MediaConfig cfg = s.config;
                cfg.set(MediaConfig::Type, s.type);
                cfg.set(MediaConfig::Name, s.name);
                if (!s.path.isEmpty()) cfg.set(MediaConfig::Filename, s.path);
                io = MediaIO::create(cfg, this);
        } else if (!s.path.isEmpty()) {
                if (s.role == MediaPipelineConfig::StageRole::Source) {
                        io = MediaIO::createForFileRead(s.path, this);
                } else {
                        io = MediaIO::createForFileWrite(s.path, this);
                }
                if (io != nullptr) {
                        MediaConfig merged = io->config();
                        s.config.forEach([&merged](MediaConfig::ID id, const Variant &val) { merged.set(id, val); });
                        // Stage name always wins over anything the
                        // file-shortcut path or stage config supplied,
                        // so logs and stats key off the same identifier
                        // the pipeline graph uses.
                        merged.set(MediaConfig::Name, s.name);
                        io->setConfig(merged);
                }
        }
        if (io != nullptr && !s.metadata.isEmpty()) {
                (void)io->setPendingMetadata(s.metadata);
        }
        return io;
}

// ============================================================================
// Lifecycle
// ============================================================================

Error MediaPipeline::destroyStages() {
        // Tear down stats collectors before the MediaIOs they observe
        // disappear.  Collectors disconnect from
        // commandCompletedSignal in their setTarget(nullptr) path, so
        // doing this first guarantees no late notification fires
        // against a half-destroyed observer.
        clearStatsCollectors();

        // Tear down every connection first so their slot wiring is
        // disconnected before the underlying ports go away.  Each
        // connection was constructed with `this` as the ObjectBase
        // parent, so deleting them explicitly here clears them out of
        // the parent's child list before the pipeline itself is
        // destructed.
        for (auto it = _sources.begin(); it != _sources.end(); ++it) {
                MediaIOPortConnection *c = it->second.connection;
                if (c == nullptr) continue;
                c->stop();
                delete c;
                it->second.connection = nullptr;
        }
        for (auto it = _stages.begin(); it != _stages.end(); ++it) {
                MediaIO *io = it->second;
                if (io == nullptr) continue;
                if (io->isOpen()) {
                        Error err = io->close().wait();
                        if (err.isError()) {
                                promekiWarn("MediaPipeline: closing stage '%s' failed: %s", it->first.cstr(),
                                            err.desc().cstr());
                        }
                }
        }
        for (auto it = _stages.begin(); it != _stages.end(); ++it) {
                MediaIO *io = it->second;
                if (io == nullptr) continue;
                // Injected stages are caller-owned; the pipeline only
                // observes the pointer.  Leave them alive so mediaplay
                // and any future external-stage user can rebuild /
                // re-inject without paying a re-create cost.
                if (!_injected.contains(it->first)) {
                        delete io;
                }
        }
        _stages.clear();
        _sources.clear();
        _topoOrder.clear();
        _terminalSinksRemaining = 0;
        return Error::Ok;
}

namespace {

        // Picks the auto-name prefix for an injected MediaIO based on
        // its role.  Tries the cheap port-count accessors first
        // (post-open).  Falls back to describe() — the canonical
        // pre-open probe — when ports haven't been declared yet.
        // Returns the generic "stage" prefix when the role can't be
        // determined (e.g. an unregistered backend whose describe()
        // override doesn't fill the role flags).
        const char *rolePrefix(MediaIO *io) {
                bool src = io->isSource();
                bool snk = io->isSink();
                if (!src && !snk) {
                        MediaIODescription d;
                        if (io->describe(&d).isOk()) {
                                if (d.canBeTransform()) return "xfm";
                                src = src || d.canBeSource();
                                snk = snk || d.canBeSink();
                        }
                }
                if (src && snk) return "xfm";
                if (src) return "src";
                if (snk) return "sink";
                return "stage";
        }

} // namespace

Error MediaPipeline::injectStage(MediaIO *io) {
        if (_state != State::Empty && _state != State::Closed) {
                promekiErr("MediaPipeline::injectStage: pipeline is not in Empty/Closed state.");
                return Error::Busy;
        }
        if (io == nullptr) {
                promekiErr("MediaPipeline::injectStage: null MediaIO.");
                return Error::InvalidArgument;
        }

        // Resolve the stage name.  An IO that already carries a name
        // wins (with a numeric suffix when it collides with another
        // injected stage); an unnamed IO gets a role-based default.
        String chosen = io->name();
        if (chosen.isEmpty()) {
                const String prefix(rolePrefix(io));
                int          n = 1;
                do {
                        chosen = prefix + String::number(n++);
                } while (_injected.contains(chosen));
        } else if (_injected.contains(chosen)) {
                const String base = chosen;
                int          n = 2;
                do {
                        chosen = base + String::number(n++);
                } while (_injected.contains(chosen));
        }

        // Stamp the resolved name back onto the IO so io->name() and
        // the pipeline's stage key stay in sync (logs, stats, and any
        // future MediaIODescription snapshot all see the same name).
        io->setName(chosen);
        _injected.insert(chosen, io);
        return Error::Ok;
}

Error MediaPipeline::build(const MediaPipelineConfig &config, bool autoplan) {
        if (_state != State::Empty && _state != State::Closed) {
                promekiErr("MediaPipeline::build: pipeline is not in Empty/Closed state.");
                return Error::Busy;
        }

        // Resolve via the planner first when requested.  Failures
        // surface as the build's return value so callers don't have
        // to thread a separate plan() step.  The planner's
        // diagnostic is multi-line — splat each line so logs stay
        // grep-friendly when something goes wrong.  Injected stages
        // (SDL player, V4L2 device handles, ...) are passed through
        // so the planner can call describe() / proposeInput on the
        // live instance — without this it would fail to build a
        // stand-in from the registry and either error out or miss
        // the negotiation entirely.
        const MediaPipelineConfig *effectiveConfig = &config;
        MediaPipelineConfig        planned;
        if (autoplan) {
                String planDiag;
                Error  perr = MediaPipelinePlanner::plan(config, &planned, _injected, {}, &planDiag);
                if (perr.isError()) {
                        promekiErr("MediaPipeline::build: planner failed (%s)", perr.name().cstr());
                        if (!planDiag.isEmpty()) {
                                const StringList lines = planDiag.split(std::string("\n"));
                                for (size_t i = 0; i < lines.size(); ++i) {
                                        promekiErr("  %s", lines[i].cstr());
                                }
                        }
                        return perr;
                }
                effectiveConfig = &planned;
        }

        Error vErr = effectiveConfig->validate();
        if (vErr.isError()) return vErr;

        // Fan-in check — for this first implementation a stage may have
        // at most one incoming route.  Fan-out is unrestricted.
        promeki::Map<String, int> inCount;
        for (size_t i = 0; i < effectiveConfig->stages().size(); ++i) {
                inCount.insert(effectiveConfig->stages()[i].name, 0);
        }
        for (size_t i = 0; i < effectiveConfig->routes().size(); ++i) {
                inCount[effectiveConfig->routes()[i].to] += 1;
        }
        for (auto it = inCount.cbegin(); it != inCount.cend(); ++it) {
                if (it->second > 1) {
                        promekiErr("MediaPipeline::build: stage '%s' has %d incoming routes; "
                                   "fan-in is not supported in this implementation.",
                                   it->first.cstr(), it->second);
                        return Error::NotSupported;
                }
        }

        _config = *effectiveConfig;

        // Instantiate every stage.  Injected stages short-circuit the
        // backend factory so externally-owned MediaIOs (SDL, V4L2
        // device handles, etc.) participate in the drain without
        // having to be registered in MediaIOFactory::registeredFactories.
        for (size_t i = 0; i < _config.stages().size(); ++i) {
                const MediaPipelineConfig::Stage &s = _config.stages()[i];
                MediaIO                          *io = nullptr;
                auto                              injIt = _injected.find(s.name);
                if (injIt != _injected.end()) {
                        io = injIt->second;
                } else {
                        io = instantiateStage(s);
                }
                if (io == nullptr) {
                        promekiErr("MediaPipeline::build: stage '%s' instantiation failed.", s.name.cstr());
                        destroyStages();
                        return Error::OpenFailed;
                }
                _stages.insert(s.name, io);
                attachStatsCollector(s.name, io);
        }

        // Build per-source edge state from the routes.  The sink
        // pointer is resolved later in @ref start once @ref open has
        // created the port group and its sink ports — at build time
        // the stages have only been instantiated, so sink(0) returns
        // null.
        for (size_t i = 0; i < _config.routes().size(); ++i) {
                const MediaPipelineConfig::Route &r = _config.routes()[i];
                if (!_sources.contains(r.from)) {
                        SourceState ss;
                        ss.from = _stages[r.from];
                        _sources.insert(r.from, ss);
                }
                EdgeState es;
                es.toName = r.to;
                es.to = _stages[r.to];
                _sources[r.from].edges.pushToBack(es);
        }

        // Mark every edge whose target is not itself a producer (i.e. the
        // target has no outgoing routes) as a sink edge so the
        // pipeline-wide frame-count cap only applies to terminal sinks.
        // The second pass runs after the first because _sources is only
        // fully populated once every route has been visited.
        for (auto it = _sources.begin(); it != _sources.end(); ++it) {
                SourceState &ss = it->second;
                for (size_t i = 0; i < ss.edges.size(); ++i) {
                        EdgeState &es = ss.edges[i];
                        es.isSinkEdge = !_sources.contains(es.toName);
                }
        }

        // Cache the pipeline-wide frame-count cap (zero == no cap).
        // Unknown / infinite / empty FrameCount states all mean "unlimited"
        // and collapse to zero here so the start-time addSink pass uses
        // an empty FrameCount on every edge.
        if (_config.frameCount().isFinite() && !_config.frameCount().isEmpty()) {
                _frameCountLimit = _config.frameCount().value();
        } else {
                _frameCountLimit = 0;
        }

        Error tErr = topologicallySort(_topoOrder);
        if (tErr.isError()) {
                destroyStages();
                return tErr;
        }

        _state = State::Built;
        publishStateChanged();
        if (autoplan) {
                PipelineEvent ev;
                ev.setKind(PipelineEvent::Kind::PlanResolved);
                ev.setJsonPayload(_config.toJson());
                publish(ev);
        }
        return Error::Ok;
}

Error MediaPipeline::open() {
        if (_state != State::Built) {
                promekiErr("MediaPipeline::open: pipeline is not in Built state.");
                return Error::NotOpen;
        }

        // Open in forward topological order so each downstream stage
        // sees its upstream's freshly-resolved MediaDesc / AudioDesc /
        // Metadata.  For every stage that has an incoming route (i.e.
        // isn't a pure source), copy the upstream's live descriptors
        // onto it via setExpectedDesc / setExpectedAudioDesc /
        // setExpectedMetadata before opening — many backends
        // (QuickTime writer, ImageFile, AudioFile, CSC) rely on the
        // pre-open descriptor to configure themselves.  Pipeline-level
        // per-stage metadata from @ref MediaPipelineConfig::Stage::metadata
        // is merged on top of the inherited metadata so the user's
        // overrides always win over the upstream defaults.
        //
        // Build a quick from-lookup for the "who feeds me?" query; the
        // @c build step already rejects fan-in so every non-source
        // stage has exactly one upstream.
        promeki::Map<String, String> upstreamOf;
        for (size_t i = 0; i < _config.routes().size(); ++i) {
                const MediaPipelineConfig::Route &r = _config.routes()[i];
                upstreamOf.insert(r.to, r.from);
        }

        promeki::List<String> opened;
        _terminalSinksRemaining = 0;
        for (size_t i = 0; i < _topoOrder.size(); ++i) {
                const String                     &name = _topoOrder[i];
                const MediaPipelineConfig::Stage *spec = _config.findStage(name);
                if (spec == nullptr) continue; // cannot happen — build validated this
                MediaIO *io = _stages[name];

                auto upIt = upstreamOf.find(name);
                if (upIt != upstreamOf.end()) {
                        MediaIO *up = _stages[upIt->second];
                        if (up != nullptr && up->isOpen()) {
                                (void)io->setPendingMediaDesc(up->mediaDesc());
                                const AudioDesc &ad = up->audioDesc();
                                if (ad.isValid()) (void)io->setPendingAudioDesc(ad);
                                Metadata merged = up->metadata();
                                if (!spec->metadata.isEmpty()) {
                                        merged.merge(spec->metadata);
                                }
                                if (!merged.isEmpty()) (void)io->setPendingMetadata(merged);
                        } else if (!spec->metadata.isEmpty()) {
                                (void)io->setPendingMetadata(spec->metadata);
                        }
                } else if (!spec->metadata.isEmpty()) {
                        (void)io->setPendingMetadata(spec->metadata);
                }

                Error err = io->open().wait();
                if (err.isError()) {
                        promekiErr("MediaPipeline::open: stage '%s' open(role=%d) failed: %s", name.cstr(),
                                   static_cast<int>(spec->role), err.desc().cstr());
                        // Unwind the already-opened stages in reverse order.
                        for (size_t j = opened.size(); j-- > 0;) {
                                MediaIO *io2 = _stages[opened[j]];
                                if (io2 != nullptr) io2->close().wait();
                        }
                        return err;
                }
                opened.pushToBack(name);
                stageOpenedSignal.emit(name);
                publishStageState(name, String("Opened"));

                // Now that this stage's ports are live, wire the
                // pipeline's @ref MediaIOPortConnection graph: any
                // route whose @c to is this freshly-opened stage can
                // bind its sink onto the upstream connection, and any
                // route whose @c from is this stage can lazily create
                // its connection now that the source port exists.
                wireConnectionsForOpenedStage(name);
        }
        _state = State::Open;
        publishStateChanged();
        return Error::Ok;
}

void MediaPipeline::wireConnectionsForOpenedStage(const String &name) {
        MediaIO *io = _stages[name];
        if (io == nullptr) return;

        // Outgoing side: this stage is the @c from of any route
        // emanating from it.  Lazily create the connection on its
        // source port — the corresponding sinks bind in below as
        // each downstream stage opens.
        auto myIt = _sources.find(name);
        if (myIt != _sources.end() && myIt->second.connection == nullptr && io->sourceCount() > 0) {
                MediaIOPortConnection *conn = new MediaIOPortConnection(io->source(0), this);
                myIt->second.connection = conn;
                const String srcName = name;
                conn->upstreamDoneSignal.connect([this, srcName]() { onUpstreamDone(srcName); }, this);
                conn->errorOccurredSignal.connect([this, srcName](Error err) { onSourceConnectionError(srcName, err); },
                                                  this);
                conn->sinkErrorSignal.connect(
                        [this, srcName](MediaIOSink *snk, Error err) { onSinkConnectionError(srcName, snk, err); },
                        this);
                conn->sinkLimitReachedSignal.connect(
                        [this, srcName](MediaIOSink *snk) { onSinkLimitReached(srcName, snk); }, this);
        }

        // Incoming side: this stage is the @c to of zero or more
        // routes.  For each one, find the upstream's connection (must
        // exist by now — topo order opened the upstream first) and
        // attach this stage's sink port.  The frame-count cap is
        // honoured only on terminal sink edges.
        if (io->sinkCount() == 0) return;
        MediaIOSink                          *mySink = io->sink(0);
        const MediaPipelineConfig::RouteList &routes = _config.routes();
        for (size_t i = 0; i < routes.size(); ++i) {
                if (routes[i].to != name) continue;
                const String &fromName = routes[i].from;
                auto          srcIt = _sources.find(fromName);
                if (srcIt == _sources.end()) continue;
                SourceState &ss = srcIt->second;
                if (ss.connection == nullptr) continue;

                FrameCount limit;
                bool       isTerminal = false;
                for (size_t e = 0; e < ss.edges.size(); ++e) {
                        if (ss.edges[e].toName != name) continue;
                        ss.edges[e].toSink = mySink;
                        isTerminal = ss.edges[e].isSinkEdge;
                        break;
                }
                if (isTerminal && _frameCountLimit > 0) {
                        limit = FrameCount(_frameCountLimit);
                        _terminalSinksRemaining += 1;
                }
                Error addErr = ss.connection->addSink(mySink, limit);
                if (addErr.isError()) {
                        promekiWarn("MediaPipeline::open: addSink for "
                                    "edge '%s' -> '%s' failed: %s",
                                    fromName.cstr(), name.cstr(), addErr.desc().cstr());
                }
        }
}

Error MediaPipeline::start() {
        if (_state != State::Open) {
                promekiErr("MediaPipeline::start: pipeline is not in Open state.");
                return Error::NotOpen;
        }

        // Connections were created and wired during @ref open; here we
        // just announce the lifecycle transition for every stage and
        // arm the pumps.
        for (auto it = _stages.cbegin(); it != _stages.cend(); ++it) {
                stageStartedSignal.emit(it->first);
                publishStageState(it->first, String("Started"));
        }

        _state = State::Running;
        publishStateChanged();
        startStatsTimerIfNeeded();
        _cleanFinish = false;

        // Start every connection now that signals are wired and the
        // pipeline state has flipped to Running.  start() primes the
        // pump internally, so the first source read kicks prefetch.
        for (auto it = _sources.begin(); it != _sources.end(); ++it) {
                MediaIOPortConnection *c = it->second.connection;
                if (c == nullptr) continue;
                Error e = c->start();
                if (e.isError()) {
                        promekiWarn("MediaPipeline::start: connection for "
                                    "source '%s' failed to start: %s",
                                    it->first.cstr(), e.desc().cstr());
                }
        }

        return Error::Ok;
}

Error MediaPipeline::stop() {
        if (_state != State::Running) {
                promekiErr("MediaPipeline::stop: pipeline is not in Running state.");
                return Error::NotOpen;
        }

        // Tear down each connection's signal wiring before cancelling
        // pending work on its source — once the connection is stopped,
        // late frameReady / frameWanted emissions become harmless
        // no-ops.
        for (auto it = _sources.begin(); it != _sources.end(); ++it) {
                if (it->second.connection != nullptr) it->second.connection->stop();
        }

        for (auto it = _stages.begin(); it != _stages.end(); ++it) {
                MediaIO *io = it->second;
                if (io != nullptr) {
                        // cancelPending lives on the source port now.  Pure
                        // sinks have no in-flight reads to cancel; backends
                        // drain their pending writes via close()'s graceful
                        // path.
                        if (io->sourceCount() > 0) {
                                (void)io->source(0)->cancelPending();
                        }
                        stageStoppedSignal.emit(it->first);
                        publishStageState(it->first, String("Stopped"));
                }
        }

        _state = State::Stopped;
        publishStateChanged();
        stopStatsTimer();
        return Error::Ok;
}

Error MediaPipeline::close(bool block) {
        initiateClose(/*clean=*/true);
        if (!block) return Error::Ok;

        // Callers blocking on close() must not stall the cascade:
        // MediaIO stages emit closedSignal from their strand threads
        // and those signals are marshalled to this pipeline's owning
        // EventLoop.  When the caller runs on that same EventLoop we
        // have to pump events ourselves while we wait, otherwise the
        // posted callables just sit there and the cascade never
        // finishes.
        EventLoop *currentEL = EventLoop::current();
        EventLoop *ownerEL = eventLoop();
        while (_state != State::Closed) {
                if (currentEL != nullptr && currentEL == ownerEL) {
                        currentEL->processEvents(EventLoop::WaitForMore, 10);
                } else {
                        Thread::sleepMs(1);
                }
        }
        return _closeError;
}

void MediaPipeline::initiateClose(bool clean) {
        // Nothing to do: no stages alive, no cascade needed.
        if (_state == State::Empty || _state == State::Closed) {
                _state = State::Closed;
                return;
        }

        // Already underway — just downgrade the clean flag on
        // error-path re-entry so the eventual finishedSignal
        // reflects the error that re-triggered us.
        if (_closing) {
                if (!clean) _cleanFinish = false;
                return;
        }

        _closing = true;
        _closeError = Error::Ok;
        _cleanFinish = clean;

        // Build the "stages to wait for" set now so every stage we're
        // about to arm has already been registered.  We wire the
        // closedSignal first, then kick off the close, to avoid a
        // race where a very fast close emits before we connect.
        _stagesAwaitingClosed.clear();
        for (auto it = _stages.cbegin(); it != _stages.cend(); ++it) {
                const String &stageName = it->first;
                MediaIO      *io = it->second;
                if (io == nullptr) continue;
                _stagesAwaitingClosed.insert(stageName);
                io->closedSignal.connect([this, stageName](Error err) { onStageClosed(stageName, err); }, this);
        }

        if (_stagesAwaitingClosed.isEmpty()) {
                // Nothing to wait on — finalize synchronously.
                finalizeClose();
                return;
        }

        // Pick the trigger stages.  In Running state we only close the
        // true sources (stages with no upstream in the DAG); cascade
        // fires the rest as drainSource latches each upstreamDone.
        // In Open / Stopped there's no drain to propagate through, so
        // close every open stage directly in parallel.
        const bool cascade = (_state == State::Running);

        // Collect the set of stages that are downstream of some route —
        // anything NOT in that set is a true source.
        promeki::Set<String>                  hasUpstream;
        const MediaPipelineConfig::RouteList &routes = _config.routes();
        for (size_t i = 0; i < routes.size(); ++i) {
                hasUpstream.insert(routes[i].to);
        }

        // Classification pass — snapshot the work into local lists so
        // the mutation passes below can trigger a re-entrant
        // @ref finalizeClose (via @ref onStageClosed) without
        // invalidating the detection iterators.
        promeki::List<String> triggers;
        promeki::List<String> alreadyClosed;
        for (size_t i = 0; i < _topoOrder.size(); ++i) {
                const String &name = _topoOrder[i];
                auto          sit = _stages.find(name);
                if (sit == _stages.end() || sit->second == nullptr) continue;
                if (!sit->second->isOpen()) {
                        alreadyClosed.pushToBack(name);
                        continue;
                }
                if (!cascade || !hasUpstream.contains(name)) {
                        triggers.pushToBack(name);
                }
        }

        // Kick the live triggers first — they're the ones we genuinely
        // want to stay in the waiting set until their closedSignal
        // arrives.  We discard the request handle and wait for the
        // closedSignal cascade; the request would resolve to the same
        // error but the signal-driven path is what unwinds the
        // pipeline.  If close() short-circuits (NotOpen / already
        // closing) we treat the stage as already done so the cascade
        // can still finish.
        for (size_t i = 0; i < triggers.size(); ++i) {
                auto it = _stages.find(triggers[i]);
                if (it == _stages.end() || it->second == nullptr) continue;
                promekiDebug("MediaPipeline::initiateClose closing trigger '%s'", triggers[i].cstr());
                auto req = it->second->close();
                if (req.isReady()) {
                        Error err = req.wait();
                        if (err.isError()) onStageClosed(triggers[i], err);
                }
        }

        // Then drain the already-closed stages.  Doing this after
        // the live triggers guarantees that if every stage was
        // already closed going in, finalizeClose runs exactly once
        // from the final onStageClosed call.
        for (size_t i = 0; i < alreadyClosed.size(); ++i) {
                onStageClosed(alreadyClosed[i], Error::Ok);
        }

        // Arm the close watchdog.  If the cascade relies on frames
        // draining through a stuck stage (for example a FrameBridge
        // output blocked in @c waitForConsumer with no consumers),
        // the normal upstream-done propagation will never reach the
        // downstream trigger points.  After @ref _closeTimeoutMs we
        // escalate to @ref forceCloseRemaining to unblock everything
        // and let the cascade finish.  A zero timeout disables the
        // watchdog entirely — callers opting into an unbounded
        // graceful close.
        if (_closing && _closeTimeoutMs > 0 && _closeWatchdogTimerId < 0) {
                _closeWatchdogTimerId = startTimer(_closeTimeoutMs,
                                                   /*singleShot=*/true);
        }
}

void MediaPipeline::timerEvent(TimerEvent *e) {
        if (e == nullptr) {
                ObjectBase::timerEvent(e);
                return;
        }
        if (e->timerId() == _closeWatchdogTimerId) {
                _closeWatchdogTimerId = -1;
                forceCloseRemaining();
                return;
        }
        if (e->timerId() == _statsTimerId) {
                emitStatsSnapshot();
                return;
        }
        ObjectBase::timerEvent(e);
}

void MediaPipeline::forceCloseRemaining() {
        if (!_closing || _state == State::Closed) return;
        // Snapshot the outstanding set — close(false) on a stage can
        // re-enter via its closedSignal handler, mutating
        // _stagesAwaitingClosed while we iterate.
        promeki::List<String> remaining;
        for (auto it = _stagesAwaitingClosed.cbegin(); it != _stagesAwaitingClosed.cend(); ++it) {
                remaining.pushToBack(*it);
        }
        for (size_t i = 0; i < remaining.size(); ++i) {
                auto sit = _stages.find(remaining[i]);
                if (sit == _stages.end() || sit->second == nullptr) continue;
                MediaIO *io = sit->second;
                if (!io->isOpen()) continue;
                if (io->isClosing()) continue;
                promekiWarn("MediaPipeline: close watchdog escalating "
                            "stage '%s' to forced close",
                            remaining[i].cstr());
                // close() invokes the backend's
                // cancelBlockingWork first, so a stuck executeCmd
                // (e.g. FrameBridge waitForConsumer) unwinds with
                // Error::Cancelled and the strand can process the
                // submitted close.  The returned request resolves
                // asynchronously through closedSignal; we only
                // interpret a synchronous short-circuit.
                auto req = io->close();
                if (req.isReady()) {
                        Error err = req.wait();
                        if (err.isError()) onStageClosed(remaining[i], err);
                }
        }
}

void MediaPipeline::onStageClosed(const String &stageName, Error err) {
        if (err.isError() && _closeError.isOk()) _closeError = err;
        _stagesAwaitingClosed.remove(stageName);
        stageClosedSignal.emit(stageName);
        publishStageState(stageName, String("Closed"));
        if (_stagesAwaitingClosed.isEmpty() && _closing) {
                finalizeClose();
        }
}

void MediaPipeline::finalizeClose() {
        // Disarm the watchdog — a clean cascade has landed inside the
        // deadline.  Safe to call with an inactive id.
        if (_closeWatchdogTimerId >= 0) {
                stopTimer(_closeWatchdogTimerId);
                _closeWatchdogTimerId = -1;
        }

        // Drop to Closed and notify listeners before destroying stages
        // so slots that probe the pipeline see the terminal state.
        // Blocking callers in @ref close pump events (or poll) until
        // they observe @ref State::Closed, so flipping it here unblocks
        // them immediately.
        _state = State::Closed;
        publishStateChanged();
        stopStatsTimer();
        bool clean = _cleanFinish && _closeError.isOk();
        finishedSignal.emit(clean);
        closedSignal.emit(_closeError);

        destroyStages();

        _closing = false;
        _stagesAwaitingClosed.clear();
}

// ============================================================================
// Connection-driven handlers
// ============================================================================

void MediaPipeline::onUpstreamDone(const String &srcName) {
        promekiDebug("MediaPipeline::onUpstreamDone[%s] state=%d closing=%d", srcName.cstr(), (int)_state,
                     (int)_closing);
        if (_state != State::Running) return;
        auto it = _sources.find(srcName);
        if (it == _sources.end()) return;
        SourceState &ss = it->second;
        if (ss.upstreamDone) return;
        ss.upstreamDone = true;

        // Arm (or join) the pipeline-level close cascade BEFORE
        // touching downstream — the initiateClose call wires the
        // per-stage closedSignal listeners that finalize waits on,
        // so it has to run before any stage's finalize can emit.
        // initiateClose is idempotent on re-entry.
        initiateClose(/*clean=*/true);

        // Cascade: close every direct downstream consumer of this
        // source.  Graceful close lets their pending input writes
        // complete before they push their own synthetic EOS.
        for (size_t i = 0; i < ss.edges.size(); ++i) {
                MediaIO *to = ss.edges[i].to;
                if (to == nullptr) continue;
                if (!to->isOpen()) continue;
                if (to->isClosing()) continue;
                promekiDebug("MediaPipeline::onUpstreamDone[%s] "
                             "cascading close to '%s'",
                             srcName.cstr(), ss.edges[i].toName.cstr());
                // Fire and forget — the actual close completes
                // asynchronously and emits closedSignal.
                to->close();
        }
}

void MediaPipeline::onSourceConnectionError(const String &srcName, Error err) {
        // NotOpen during a close cascade is expected plumbing; the
        // source has been torn down and any late frameReady that
        // raced the close emits readFrame == NotOpen.  Filter the
        // same way the original drainSource guarded against spurious
        // post-close kicks via @c upstreamDone.
        if (err == Error::TryAgain || err == Error::EndOfFile) return;
        if (err == Error::NotOpen && _closing) return;
        if (err != Error::Cancelled) {
                pipelineErrorSignal.emit(srcName, err);
                PipelineEvent ev;
                ev.setKind(PipelineEvent::Kind::StageError);
                ev.setStageName(srcName);
                ev.setPayload(Variant(err.desc()));
                Metadata m;
                m.set(Metadata::ID(String("code")), err.name());
                ev.setMetadata(m);
                publish(ev);
        }
        initiateClose(/*clean=*/false);
}

void MediaPipeline::onSinkConnectionError(const String &srcName, MediaIOSink *sink, Error err) {
        // TryAgain is back-pressure noise (filtered inside the
        // connection too); ignore it.  Writes during an already-
        // running close are expected to come back NotOpen once the
        // downstream stage has latched its own _closing flag — those
        // are cascade plumbing, not operational errors.
        if (err == Error::TryAgain) return;
        if (err == Error::NotOpen && _closing) return;

        // Map the sink back to its stage name for the public error
        // payload.  Falling back to the source name keeps the signal
        // useful when the lookup fails (e.g. a sink that wasn't
        // registered through the route table).
        String stageName = srcName;
        for (auto it = _sources.cbegin(); it != _sources.cend(); ++it) {
                const SourceState &ss = it->second;
                for (size_t i = 0; i < ss.edges.size(); ++i) {
                        if (ss.edges[i].toSink == sink) {
                                stageName = ss.edges[i].toName;
                                goto found;
                        }
                }
        }
found:
        if (err != Error::Cancelled) {
                pipelineErrorSignal.emit(stageName, err);
                PipelineEvent ev;
                ev.setKind(PipelineEvent::Kind::StageError);
                ev.setStageName(stageName);
                ev.setPayload(Variant(err.desc()));
                Metadata m;
                m.set(Metadata::ID(String("code")), err.name());
                ev.setMetadata(m);
                publish(ev);
        }
        initiateClose(/*clean=*/false);
}

void MediaPipeline::onSinkLimitReached(const String &srcName, MediaIOSink *sink) {
        // Mark the matching edge so future stats reflect the cap and
        // decrement the global counter.  The connection has already
        // dropped the sink from its dispatch list; once every capped
        // sink in the pipeline has fired the pipeline self-closes.
        auto it = _sources.find(srcName);
        if (it != _sources.end()) {
                SourceState &ss = it->second;
                for (size_t i = 0; i < ss.edges.size(); ++i) {
                        if (ss.edges[i].toSink == sink) {
                                if (!ss.edges[i].capReached) {
                                        ss.edges[i].capReached = true;
                                        if (_terminalSinksRemaining > 0) _terminalSinksRemaining -= 1;
                                }
                                break;
                        }
                }
        }
        if (_frameCountLimit > 0 && _terminalSinksRemaining == 0) {
                promekiDebug("MediaPipeline::onSinkLimitReached: all terminal "
                             "sinks have hit cap; initiating close.");
                initiateClose(/*clean=*/true);
        }
}


// ============================================================================
// Introspection
// ============================================================================

MediaIO *MediaPipeline::stage(const String &name) const {
        auto it = _stages.find(name);
        if (it == _stages.end()) return nullptr;
        return it->second;
}

StringList MediaPipeline::stageNames() const {
        StringList names;
        for (size_t i = 0; i < _topoOrder.size(); ++i) names.pushToBack(_topoOrder[i]);
        return names;
}

StringList MediaPipeline::describe() const {
        StringList out = _config.describe();
        if (_stages.isEmpty()) return out;

        out.pushToBack("Live state:");
        for (size_t i = 0; i < _topoOrder.size(); ++i) {
                const String &name = _topoOrder[i];
                auto          it = _stages.find(name);
                if (it == _stages.end() || it->second == nullptr) continue;
                MediaIO *io = it->second;
                String   line = "  ";
                line += name;
                line += ": ";
                if (io->isOpen()) {
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

// ============================================================================
// PipelineEvent dispatch
// ============================================================================

namespace {

        const char *stateName(MediaPipeline::State s) {
                switch (s) {
                        case MediaPipeline::State::Empty: return "Empty";
                        case MediaPipeline::State::Built: return "Built";
                        case MediaPipeline::State::Open: return "Open";
                        case MediaPipeline::State::Running: return "Running";
                        case MediaPipeline::State::Stopped: return "Stopped";
                        case MediaPipeline::State::Closed: return "Closed";
                }
                return "Empty";
        }

        const char *logLevelName(Logger::LogLevel level) {
                switch (level) {
                        case Logger::Force: return "Force";
                        case Logger::Debug: return "Debug";
                        case Logger::Info: return "Info";
                        case Logger::Warn: return "Warn";
                        case Logger::Err: return "Err";
                }
                return "Info";
        }

} // namespace

int MediaPipeline::subscribe(EventCallback cb) {
        if (!cb) {
                promekiErr("MediaPipeline::subscribe: empty callback rejected.");
                return -1;
        }
        EventLoop *loop = EventLoop::current();
        if (loop == nullptr) {
                promekiErr("MediaPipeline::subscribe: no EventLoop on calling thread.");
                return -1;
        }

        bool firstSubscriber = false;
        int  id = -1;
        {
                Mutex::Locker lock(_subsMutex);
                id = _nextSubId++;
                Subscriber s;
                s.id = id;
                s.fn = std::move(cb);
                s.loop = loop;
                _subscribers.insert(id, s);
                firstSubscriber = (_subscribers.size() == 1);
        }
        if (firstSubscriber) {
                installLoggerTap();
        }
        return id;
}

void MediaPipeline::unsubscribe(int id) {
        bool lastSubscriber = false;
        {
                Mutex::Locker lock(_subsMutex);
                auto          it = _subscribers.find(id);
                if (it == _subscribers.end()) return;
                _subscribers.remove(it);
                lastSubscriber = _subscribers.isEmpty();
        }
        if (lastSubscriber) {
                removeLoggerTap();
        }
}

void MediaPipeline::publish(PipelineEvent ev) {
        if (ev.timestamp().nanoseconds() == 0) {
                ev.setTimestamp(TimeStamp::now());
        }

        promeki::List<Subscriber> snapshot;
        {
                Mutex::Locker lock(_subsMutex);
                if (_subscribers.isEmpty()) return;
                for (auto it = _subscribers.cbegin(); it != _subscribers.cend(); ++it) {
                        snapshot.pushToBack(it->second);
                }
        }

        for (size_t i = 0; i < snapshot.size(); ++i) {
                Subscriber &s = snapshot[i];
                if (s.loop == nullptr || !s.fn) continue;
                EventCallback fn = s.fn;
                PipelineEvent copy = ev;
                s.loop->postCallable([fn = std::move(fn), copy]() mutable { fn(copy); });
        }
}

void MediaPipeline::publishStateChanged() {
        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::StateChanged);
        ev.setPayload(Variant(String(stateName(_state))));
        publish(ev);
}

void MediaPipeline::publishStageState(const String &stageName, const String &transition) {
        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::StageState);
        ev.setStageName(stageName);
        ev.setPayload(Variant(transition));
        publish(ev);
}

void MediaPipeline::installLoggerTap() {
        if (_loggerTap != 0) return;
        EventLoop *ownerLoop = eventLoop();
        if (ownerLoop == nullptr) return;
        _loggerTap = Logger::defaultLogger().installListener(
                [this, ownerLoop](const Logger::LogEntry &entry, const String &threadName) {
                        PipelineEvent ev;
                        ev.setKind(PipelineEvent::Kind::Log);
                        ev.setPayload(Variant(entry.msg));
                        Metadata m;
                        m.set(Metadata::ID(String("level")), String(logLevelName(entry.level)));
                        if (entry.file != nullptr) {
                                m.set(Metadata::ID(String("source")), String(entry.file));
                        }
                        m.set(Metadata::ID(String("line")), static_cast<int64_t>(entry.line));
                        if (!threadName.isEmpty()) {
                                m.set(Metadata::ID(String("threadName")), threadName);
                        }
                        ev.setMetadata(m);
                        ownerLoop->postCallable([this, ev]() { publish(ev); });
                },
                0);
}

void MediaPipeline::removeLoggerTap() {
        if (_loggerTap == 0) return;
        Logger::ListenerHandle h = _loggerTap;
        _loggerTap = 0;
        Logger::defaultLogger().removeListener(h);
}

void MediaPipeline::setStatsInterval(Duration interval) {
        _statsInterval = interval;
        stopStatsTimer();
        if (_state == State::Running) startStatsTimerIfNeeded();
}

void MediaPipeline::startStatsTimerIfNeeded() {
        if (_statsTimerId >= 0) return;
        if (_statsInterval.nanoseconds() <= 0) return;
        if (_state != State::Running) return;
        int64_t ms = _statsInterval.milliseconds();
        if (ms <= 0) ms = 1;
        _statsTimerId = startTimer(static_cast<unsigned int>(ms),
                                   /*singleShot=*/false);
}

void MediaPipeline::stopStatsTimer() {
        if (_statsTimerId < 0) return;
        stopTimer(_statsTimerId);
        _statsTimerId = -1;
}

void MediaPipeline::emitStatsSnapshot() {
        MediaPipelineStats snap = stats();
        statsUpdatedSignal.emit(snap);
        PipelineEvent ev;
        ev.setKind(PipelineEvent::Kind::StatsUpdated);
        ev.setJsonPayload(snap.toJson());
        publish(ev);
}

void MediaPipeline::attachStatsCollector(const String &name, MediaIO *io) {
        if (io == nullptr) return;
        if (_statsCollectors.contains(name)) return;
        const int windowSize = _config.statsWindowSize();
        if (windowSize <= 0) return;
        auto *collector = new MediaIOStatsCollector(io, this);
        collector->setWindowSize(windowSize);
        _statsCollectors.insert(name, collector);
}

void MediaPipeline::clearStatsCollectors() {
        // Cross-thread Signal::connect tracks the receiver via an
        // ObjectBasePtr and auto-registers a disconnect cleanup, so
        // any commandCompletedSignal callable queued before this
        // delete becomes a no-op once the collector finishes
        // destruction (its _pointerMap nulls every captured tracker
        // in runCleanup).  No deferred-delete dance needed — straight
        // delete is now safe.
        for (auto it = _statsCollectors.begin(); it != _statsCollectors.end(); ++it) {
                delete it->second;
        }
        _statsCollectors.clear();
}

MediaPipelineStageStats MediaPipeline::buildStageStats(const String &name, MediaIO *io) {
        MediaPipelineStageStats out;
        out.name = name;
        if (io == nullptr) return out;

        // FIXME: each MediaIO::stats() round-trips synchronously
        // through that stage's strand, so an N-stage pipeline pays N
        // sequential strand turnarounds.  For larger graphs this
        // serialises stats collection unnecessarily.  Plan: issue all
        // io->stats() requests up front, then await them in a second
        // pass so the strand work overlaps.  Tracking under a future
        // pipeline-stats latency pass.
        if (io->isOpen()) {
                MediaIORequest req = io->stats();
                req.wait();
                out.cumulative = req.stats();
        }

        // Fold the per-(kind, stat) windows captured by the
        // collector into the stage's windowed map.  Each
        // collector window key is (kind, stat-id); we group by
        // kind into a per-kind WindowedStatsBundle so callers see
        // a clean breakdown by command kind.
        auto cit = _statsCollectors.find(name);
        if (cit != _statsCollectors.end() && cit->second != nullptr) {
                const auto &windows = cit->second->windows();
                for (auto wit = windows.cbegin(); wit != windows.cend(); ++wit) {
                        out.windowedBundle(wit->first.kind).set(wit->first.id, wit->second);
                }
        }

        return out;
}

MediaPipelineStats MediaPipeline::stats() {
        MediaPipelineStats out;
        // Walk in topological order so consumers see sources before
        // their downstream stages — matches describe() ordering.
        for (size_t i = 0; i < _topoOrder.size(); ++i) {
                const String &name = _topoOrder[i];
                auto          sit = _stages.find(name);
                if (sit == _stages.end()) continue;
                out.addStage(buildStageStats(name, sit->second));
        }
        // Stages that aren't in the topo order (e.g. injected stages
        // that weren't part of the config graph) still deserve a
        // record — emit them after the topo set.
        for (auto it = _stages.cbegin(); it != _stages.cend(); ++it) {
                if (out.containsStage(it->first)) continue;
                out.addStage(buildStageStats(it->first, it->second));
        }
        return out;
}

MediaPipelineStageStats MediaPipeline::stageStats(const String &name) {
        auto it = _stages.find(name);
        if (it == _stages.end()) return MediaPipelineStageStats();
        return buildStageStats(name, it->second);
}

PROMEKI_NAMESPACE_END
