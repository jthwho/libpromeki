/**
 * @file      mediapipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediapipeline.h>

#include <promeki/eventloop.h>
#include <promeki/logger.h>
#include <promeki/mediapipelineplanner.h>
#include <promeki/objectbase.tpp>
#include <promeki/set.h>
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
                if (!s.path.isEmpty()) cfg.set(MediaConfig::Filename, s.path);
                io = MediaIO::create(cfg, this);
        } else if (!s.path.isEmpty()) {
                if (s.mode == MediaIO::Source) {
                        io = MediaIO::createForFileRead(s.path, this);
                } else {
                        io = MediaIO::createForFileWrite(s.path, this);
                }
                if (io != nullptr) {
                        MediaConfig merged = io->config();
                        s.config.forEach([&merged](MediaConfig::ID id, const Variant &val) { merged.set(id, val); });
                        io->setConfig(merged);
                }
        }
        if (io != nullptr && !s.metadata.isEmpty()) {
                (void)io->setExpectedMetadata(s.metadata);
        }
        return io;
}

// ============================================================================
// Lifecycle
// ============================================================================

Error MediaPipeline::destroyStages() {
        for (auto it = _stages.begin(); it != _stages.end(); ++it) {
                MediaIO *io = it->second;
                if (io == nullptr) continue;
                if (io->isOpen()) {
                        Error err = io->close();
                        if (err.isError()) {
                                promekiWarn("MediaPipeline: closing stage '%s' failed: %s", it->first.cstr(),
                                            err.desc().cstr());
                        }
                }
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
        return Error::Ok;
}

Error MediaPipeline::injectStage(const String &name, MediaIO *io) {
        if (_state != State::Empty && _state != State::Closed) {
                promekiErr("MediaPipeline::injectStage: pipeline is not in Empty/Closed state.");
                return Error::Busy;
        }
        if (io == nullptr) {
                promekiErr("MediaPipeline::injectStage: null MediaIO for stage '%s'.", name.cstr());
                return Error::InvalidArgument;
        }
        _injected.insert(name, io);
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
        // having to be registered in MediaIO::registeredFormats.
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
        }

        // Build per-source edge state from the routes.
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
        // target has no outgoing routes) as a sink edge so drainSource
        // knows which edges honour the frame-count cap.  The second pass
        // runs after the first because _sources is only fully populated
        // once every route has been visited.
        for (auto it = _sources.begin(); it != _sources.end(); ++it) {
                SourceState &ss = it->second;
                for (size_t i = 0; i < ss.edges.size(); ++i) {
                        EdgeState &es = ss.edges[i];
                        es.isSinkEdge = !_sources.contains(es.toName);
                }
        }

        // Cache the pipeline-wide frame-count cap (zero == no cap).
        // Unknown / infinite / empty FrameCount states all mean "unlimited"
        // and collapse to zero here so drainSource's hot-path check is a
        // single integer compare.
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
        for (size_t i = 0; i < _topoOrder.size(); ++i) {
                const String                     &name = _topoOrder[i];
                const MediaPipelineConfig::Stage *spec = _config.findStage(name);
                if (spec == nullptr) continue; // cannot happen — build validated this
                MediaIO *io = _stages[name];

                auto upIt = upstreamOf.find(name);
                if (upIt != upstreamOf.end()) {
                        MediaIO *up = _stages[upIt->second];
                        if (up != nullptr && up->isOpen()) {
                                (void)io->setExpectedDesc(up->mediaDesc());
                                const AudioDesc &ad = up->audioDesc();
                                if (ad.isValid()) (void)io->setExpectedAudioDesc(ad);
                                Metadata merged = up->metadata();
                                if (!spec->metadata.isEmpty()) {
                                        merged.merge(spec->metadata);
                                }
                                if (!merged.isEmpty()) (void)io->setExpectedMetadata(merged);
                        } else if (!spec->metadata.isEmpty()) {
                                (void)io->setExpectedMetadata(spec->metadata);
                        }
                } else if (!spec->metadata.isEmpty()) {
                        (void)io->setExpectedMetadata(spec->metadata);
                }

                Error err = io->open(spec->mode);
                if (err.isError()) {
                        promekiErr("MediaPipeline::open: stage '%s' open(%d) failed: %s", name.cstr(),
                                   static_cast<int>(spec->mode), err.desc().cstr());
                        // Unwind the already-opened stages in reverse order.
                        for (size_t j = opened.size(); j-- > 0;) {
                                MediaIO *io2 = _stages[opened[j]];
                                if (io2 != nullptr) (void)io2->close();
                        }
                        return err;
                }
                opened.pushToBack(name);
                stageOpenedSignal.emit(name);
                publishStageState(name, String("Opened"));
        }
        _state = State::Open;
        publishStateChanged();
        return Error::Ok;
}

Error MediaPipeline::start() {
        if (_state != State::Open) {
                promekiErr("MediaPipeline::start: pipeline is not in Open state.");
                return Error::NotOpen;
        }

        // Wire drain handlers for every source and edge.
        for (auto it = _sources.begin(); it != _sources.end(); ++it) {
                const String srcName = it->first;
                MediaIO     *srcIO = it->second.from;
                srcIO->frameReadySignal.connect([this, srcName]() { drainSource(srcName); }, this);
                srcIO->writeErrorSignal.connect([this, srcName](Error e) { onWriteError(srcName, e); }, this);

                // Each outgoing edge reopens the drain when the consumer
                // reports it can accept more.
                for (size_t i = 0; i < it->second.edges.size(); ++i) {
                        MediaIO *to = it->second.edges[i].to;
                        to->frameWantedSignal.connect([this, srcName]() { drainSource(srcName); }, this);
                }
                stageStartedSignal.emit(srcName);
                publishStageState(srcName, String("Started"));
        }

        // Connect writeError for every stage that isn't a pure source
        // (we already hooked sources above); pure sinks need it too.
        for (auto it = _stages.begin(); it != _stages.end(); ++it) {
                if (_sources.contains(it->first)) continue;
                const String stageName = it->first;
                MediaIO     *io = it->second;
                io->writeErrorSignal.connect([this, stageName](Error e) { onWriteError(stageName, e); }, this);
                stageStartedSignal.emit(stageName);
                publishStageState(stageName, String("Started"));
        }

        _state = State::Running;
        publishStateChanged();
        startStatsTimerIfNeeded();
        _cleanFinish = false;
        _framesProduced.setValue(0);
        _writeRetries.setValue(0);
        _pipelineErrors.setValue(0);
        _uptime.start();
        _uptimeStarted = true;

        // Prime the drain so the first readFrame kicks prefetch on every
        // source stage.  Subsequent iterations happen off frameReady.
        for (auto it = _sources.begin(); it != _sources.end(); ++it) {
                drainSource(it->first);
        }

        return Error::Ok;
}

Error MediaPipeline::stop() {
        if (_state != State::Running) {
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
        for (auto it = _stages.begin(); it != _stages.end(); ++it) {
                if (it->second != nullptr) {
                        (void)it->second->cancelPending();
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
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
        // arrives.  If close(false) comes back with an error we treat
        // the stage as already done so the cascade can still finish.
        for (size_t i = 0; i < triggers.size(); ++i) {
                auto it = _stages.find(triggers[i]);
                if (it == _stages.end() || it->second == nullptr) continue;
                promekiDebug("MediaPipeline::initiateClose closing trigger '%s'", triggers[i].cstr());
                Error err = it->second->close(false);
                if (err.isError()) onStageClosed(triggers[i], err);
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
                // close(false) invokes the backend's
                // cancelBlockingWork first, so a stuck executeCmd
                // (e.g. FrameBridge waitForConsumer) unwinds with
                // Error::Cancelled and the strand can process the
                // submitted close.
                Error err = io->close(false);
                if (err.isError()) onStageClosed(remaining[i], err);
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
// Drain
// ============================================================================

void MediaPipeline::drainSource(const String &srcName) {
        promekiDebug("MediaPipeline::drainSource[%s] ENTER state=%d closing=%d", srcName.cstr(), (int)_state,
                     (int)_closing);
        // Allow drain to continue during an async close so the synthetic
        // EOS pushed by each stage's finalize step can propagate through
        // the graph.  Only stop hard after the pipeline has fully
        // transitioned to Closed.
        if (_state != State::Running) return;
        auto it = _sources.find(srcName);
        if (it == _sources.end()) return;
        SourceState &ss = it->second;
        if (ss.from == nullptr) return;
        // Once this source has latched EOF (natural or synthetic) and
        // the cascade has fired, any further frameReady kicks are
        // spurious — e.g., the finalize task emits frameReady after
        // pushing the EOS, which the first drainSource call already
        // consumed.  A second invocation would try @c readFrame on a
        // stage whose @c _mode has flipped to @c NotOpen and surface
        // that as a spurious pipeline error.
        if (ss.upstreamDone) return;

        while (true) {
                // During close, skip the back-pressure gate so the
                // source's synthetic EOS can be read even if every
                // downstream's write queue is full — otherwise the
                // cascade deadlocks: source is closed with the EOS
                // waiting in its read queue, drainSource refuses to
                // read because downstream is saturated, the sink's
                // pull thread keeps consuming at nominal rate but
                // the resulting writesAccepted>0 windows only fire
                // frameWanted briefly (hard to synchronize with the
                // EOS arrival), and the close watchdog ends up
                // resolving the cascade instead of a clean finish.
                // Frames we can't write during close are dropped;
                // the stream is terminating anyway.
                if (!_closing) {
                        bool blocked = false;
                        for (size_t i = 0; i < ss.edges.size(); ++i) {
                                const EdgeState &e = ss.edges[i];
                                if (e.doneByLimit) continue;
                                if (e.to == nullptr) return;
                                if (e.to->writesAccepted() <= 0) {
                                        blocked = true;
                                        break;
                                }
                        }
                        if (blocked) return;
                }

                Frame::Ptr frame;
                Error      err = ss.from->readFrame(frame, false);
                if (err == Error::TryAgain) return;
                if (err == Error::EndOfFile) {
                        promekiDebug("MediaPipeline::drainSource[%s] EOF; "
                                     "cascading close to %zu downstream",
                                     srcName.cstr(), ss.edges.size());
                        ss.upstreamDone = true;
                        // Arm (or join) the pipeline-level close
                        // cascade BEFORE touching downstream — the
                        // initiateClose call wires the per-stage
                        // closedSignal listeners that finalize waits
                        // on, so it has to run before any stage's
                        // finalize can emit.  initiateClose is
                        // idempotent on re-entry, so it's cheap to
                        // call from either the natural-EOF trigger
                        // or the explicit @ref close trigger.
                        initiateClose(/*clean=*/true);
                        // Cascade: close every direct downstream
                        // consumer of this source.  Graceful close
                        // lets their pending input writes complete
                        // before they push their own synthetic EOS.
                        for (size_t i = 0; i < ss.edges.size(); ++i) {
                                MediaIO *to = ss.edges[i].to;
                                if (to == nullptr) continue;
                                if (!to->isOpen()) continue;
                                if (to->isClosing()) continue;
                                promekiDebug("MediaPipeline::drainSource[%s] "
                                             "cascading close to '%s'",
                                             srcName.cstr(), ss.edges[i].toName.cstr());
                                (void)to->close(false);
                        }
                        return;
                }
                if (err.isError()) {
                        if (err != Error::Cancelled) {
                                _pipelineErrors.fetchAndAdd(1);
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
                        return;
                }

                _framesProduced.fetchAndAdd(1);

                bool sinkReachedLimit = false;
                for (size_t i = 0; i < ss.edges.size(); ++i) {
                        EdgeState &e = ss.edges[i];
                        if (e.to == nullptr) continue;
                        // Previously-saturated edges stay dark: the sink
                        // has been handed its close() and any further
                        // write would race the backend's NotOpen latch.
                        if (e.doneByLimit) continue;

                        // Frame-count cutoff.  Honoured only on edges
                        // terminating at a true sink (no outgoing routes)
                        // and only when a per-pipeline cap is configured.
                        // The cap is crossed as soon as framesWritten
                        // reaches it; we still need to wait for the next
                        // @ref Frame::isSafeCutPoint to land so the GOP
                        // containing the cap stays complete.  Honouring
                        // the cap takes precedence over the normal write,
                        // so the sink never sees the cut frame.
                        //
                        // The sink itself isn't closed here — doing so
                        // would race the pipeline-level @ref initiateClose
                        // below (a fast sink can emit closedSignal before
                        // the pipeline's handler is wired).  Instead the
                        // EOF cascade triggered via initiateClose + each
                        // source's synthetic EOS closes every downstream,
                        // which is exactly the behaviour we want.
                        if (_frameCountLimit > 0 && e.isSinkEdge && e.framesWritten >= _frameCountLimit &&
                            frame.isValid() && frame->isSafeCutPoint()) {
                                if (!e.doneByLimit) {
                                        promekiDebug("MediaPipeline::drainSource[%s] "
                                                     "edge '%s' reached frame-count "
                                                     "cap (%lld).",
                                                     srcName.cstr(), e.toName.cstr(), (long long)_frameCountLimit);
                                        sinkReachedLimit = true;
                                }
                                e.doneByLimit = true;
                                continue;
                        }

                        Error werr = e.to->writeFrame(frame, false);
                        if (werr.isError()) {
                                // During close the back-pressure gate
                                // above is bypassed, so writeFrame may
                                // legitimately come back @c TryAgain
                                // (downstream is saturated).  Treat
                                // that as an expected drop — the goal
                                // of draining here is to reach the
                                // source's synthetic EOS, not to
                                // deliver every frame.
                                if (werr == Error::TryAgain) {
                                        if (_closing) continue;
                                        // writesAccepted said yes; the
                                        // backend said no.  That's a
                                        // contract violation worth
                                        // crashing on rather than
                                        // silently dropping a frame.
                                        _writeRetries.fetchAndAdd(1);
                                        PROMEKI_ASSERT(werr != Error::TryAgain);
                                }
                                // Writes during an already-running close
                                // are expected to come back NotOpen once
                                // the downstream stage has latched its
                                // own _closing flag.  Treat those as
                                // cascade plumbing, not an operational
                                // error.
                                if (werr != Error::NotOpen || !_closing) {
                                        onWriteError(e.toName, werr);
                                }
                                return;
                        }
                        // Only count edges that actually consume the
                        // frame for the cap — transforms along the way
                        // don't participate in the per-sink accounting.
                        if (e.isSinkEdge) e.framesWritten++;
                }

                // Once every sink edge has seen its cap, kick the
                // pipeline close so sources stop reading and any
                // frames already in flight get dropped on the floor.
                if (sinkReachedLimit && allSinkEdgesDoneByLimit()) {
                        promekiDebug("MediaPipeline::drainSource[%s] all sink "
                                     "edges saturated; initiating close.",
                                     srcName.cstr());
                        initiateClose(/*clean=*/true);
                        return;
                }
        }
}

bool MediaPipeline::allSinkEdgesDoneByLimit() const {
        bool seenSinkEdge = false;
        for (auto it = _sources.cbegin(); it != _sources.cend(); ++it) {
                const SourceState &ss = it->second;
                for (size_t i = 0; i < ss.edges.size(); ++i) {
                        const EdgeState &e = ss.edges[i];
                        if (!e.isSinkEdge) continue;
                        seenSinkEdge = true;
                        if (!e.doneByLimit) return false;
                }
        }
        // An all-done verdict on a pipeline with no sink edges would
        // short-circuit the close path on pure-transform graphs; make
        // the empty case a no-op instead.
        return seenSinkEdge;
}

void MediaPipeline::onWriteError(const String &stageName, Error err) {
        if (err == Error::TryAgain) return;
        if (err != Error::Cancelled) {
                _pipelineErrors.fetchAndAdd(1);
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

MediaPipelineStats MediaPipeline::stats() {
        MediaPipelineStats out;
        for (auto it = _stages.begin(); it != _stages.end(); ++it) {
                if (it->second == nullptr) continue;
                if (!it->second->isOpen()) continue;
                out.setStageStats(it->first, it->second->stats());
        }
        out.recomputeAggregate();

        // Pipeline-layer counters.  Derived quantities (SourcesAtEof,
        // PausedEdges) are computed fresh on every call so callers get
        // a consistent view of the drain loop's current shape.
        PipelineStats &pp = out.pipeline();
        pp.set(PipelineStats::FramesProduced, FrameCount(_framesProduced.value()));
        pp.set(PipelineStats::WriteRetries, _writeRetries.value());
        pp.set(PipelineStats::PipelineErrors, _pipelineErrors.value());

        int64_t sourcesAtEof = 0;
        for (auto it = _sources.cbegin(); it != _sources.cend(); ++it) {
                if (it->second.upstreamDone) sourcesAtEof++;
        }
        pp.set(PipelineStats::SourcesAtEof, sourcesAtEof);
        // PausedEdges stays at its spec default (0) for now — no edge
        // can hold a frame under the current single-input drain model.
        // Reserved for the future fan-in extension that lifts the
        // current "one incoming route per stage" rule.
        pp.set(PipelineStats::PausedEdges, int64_t(0));

        pp.set(PipelineStats::State, String(stateName(_state)));

        if (_uptimeStarted) {
                pp.set(PipelineStats::UptimeMs, _uptime.elapsed());
        }
        return out;
}

PROMEKI_NAMESPACE_END
