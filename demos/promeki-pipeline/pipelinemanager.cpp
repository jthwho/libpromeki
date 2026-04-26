/**
 * @file      pipelinemanager.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "pipelinemanager.h"

#include <utility>

#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/logger.h>
#include <promeki/objectbase.tpp>
#include <promeki/uuid.h>

using promeki::Error;
using promeki::JsonArray;
using promeki::JsonObject;
using promeki::List;
using promeki::Map;
using promeki::MediaPipeline;
using promeki::MediaPipelineConfig;
using promeki::MediaPipelinePlanner;
using promeki::ObjectBase;
using promeki::PipelineEvent;
using promeki::String;
using promeki::UUID;
using promeki::UniquePtr;

namespace promekipipeline {

// promekiInfo / promekiWarn / promekiErr / promekiDebug expand to refer to
// `Logger::` and `String::` unqualified, so pull both into our namespace
// once for every TU that uses them.
using promeki::Logger;

PipelineManager::PipelineManager(ObjectBase *parent) : ObjectBase(parent) {}

PipelineManager::~PipelineManager() {
        // Move every entry out of the map under the lock, then close
        // outside of it.  The closedSignal callbacks running during
        // close cascade fire on this loop and would otherwise re-enter
        // the manager's mutex through publish().
        List<Entry> doomed;
        {
                promeki::Mutex::Locker guard(_mutex);
                doomed.reserve(_order.size());
                for(const auto &id : _order) {
                        auto it = _entries.find(id);
                        if(it == _entries.end()) continue;
                        doomed.pushToBack(std::move(it->second));
                }
                _entries.clear();
                _order.clear();
                _subscribers.clear();
        }
        for(auto &e : doomed) {
                if(e.pipelineSubId >= 0 && e.pipeline.get() != nullptr) {
                        e.pipeline->unsubscribe(e.pipelineSubId);
                        e.pipelineSubId = -1;
                }
                if(e.pipeline.get() != nullptr) {
                        const MediaPipeline::State s = e.pipeline->state();
                        if(s != MediaPipeline::State::Empty &&
                           s != MediaPipeline::State::Closed) {
                                e.pipeline->close(true);
                        }
                }
        }
}

String PipelineManager::create(const String &name) {
        UniquePtr<MediaPipeline> pipeline = UniquePtr<MediaPipeline>::create(this);
        MediaPipeline *raw = pipeline.get();

        Entry entry;
        entry.id = generateId();
        entry.settings.setName(name);
        entry.pipeline = std::move(pipeline);

        // Subscribe before publishing the entry so we never miss an
        // event the pipeline emits during its first lifecycle call.
        const String capturedId = entry.id;
        entry.pipelineSubId = raw->subscribe(
                [this, capturedId](const PipelineEvent &ev) {
                        publish(capturedId, ev);
                });

        const String id = entry.id;
        {
                promeki::Mutex::Locker guard(_mutex);
                _order.pushToBack(id);
                _entries.insert(id, std::move(entry));
        }
        return id;
}

Error PipelineManager::remove(const String &id) {
        UniquePtr<MediaPipeline> doomed;
        int subId = -1;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _entries.find(id);
                if(it == _entries.end()) return Error::NotExist;
                Entry &e = it->second;
                subId = e.pipelineSubId;
                e.pipelineSubId = -1;
                doomed = std::move(e.pipeline);
                _entries.remove(id);
                for(size_t i = 0; i < _order.size(); ++i) {
                        if(_order[i] == id) {
                                _order.remove(i);
                                break;
                        }
                }
        }
        // Tear down outside the lock so closedSignal handlers and any
        // last-gasp publishes don't re-enter the manager's mutex.
        if(doomed.get() != nullptr) {
                if(subId >= 0) doomed->unsubscribe(subId);
                const MediaPipeline::State s = doomed->state();
                if(s != MediaPipeline::State::Empty &&
                   s != MediaPipeline::State::Closed) {
                        doomed->close(true);
                }
        }
        return Error::Ok;
}

Error PipelineManager::replaceConfig(const String &id,
                                     const MediaPipelineConfig &userConfig) {
        promeki::Mutex::Locker guard(_mutex);
        auto it = _entries.find(id);
        if(it == _entries.end()) return Error::NotExist;
        Entry &e = it->second;
        if(e.pipeline.get() == nullptr) return Error::Invalid;
        if(!stateAllowsConfigRewrite(e.pipeline->state())) return Error::Busy;
        e.userConfig = userConfig;
        e.resolvedConfig = MediaPipelineConfig();
        return Error::Ok;
}

Error PipelineManager::replaceSettings(const String &id,
                                       const PipelineSettings &settings) {
        // Apply the settings update under the lock, but push any live
        // tunable (today: the stats tick interval) into the running
        // pipeline outside the lock — setStatsInterval reaches into
        // the timer machinery on the pipeline's loop and we want to
        // avoid holding _mutex across that call.
        MediaPipeline *liveStatsTarget = nullptr;
        promeki::Duration newInterval;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _entries.find(id);
                if(it == _entries.end()) return Error::NotExist;
                Entry &e = it->second;
                const promeki::Duration prev = e.settings.statsInterval();
                e.settings = settings;
                if(e.pipeline.get() != nullptr &&
                   e.pipeline->state() == MediaPipeline::State::Running &&
                   prev != settings.statsInterval()) {
                        liveStatsTarget = e.pipeline.get();
                        newInterval = settings.statsInterval();
                }
        }
        if(liveStatsTarget != nullptr) liveStatsTarget->setStatsInterval(newInterval);
        return Error::Ok;
}

Error PipelineManager::build(const String &id) {
        // Run the planner under the lock (offline, no events) so the
        // resolved config and its corresponding live pipeline stay
        // consistent.  Then snapshot the pipeline pointer + resolved
        // config and call MediaPipeline::build outside the lock — the
        // build() call may publish PipelineEvents that loop back through
        // publish() and would otherwise deadlock on _mutex.
        MediaPipeline *raw = nullptr;
        MediaPipelineConfig toBuild;
        promeki::Duration statsInterval;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _entries.find(id);
                if(it == _entries.end()) return Error::NotExist;
                Entry &e = it->second;
                if(e.pipeline.get() == nullptr) return Error::Invalid;

                if(e.settings.autoplan()) {
                        MediaPipelineConfig resolved;
                        String diag;
                        const MediaPipelinePlanner::Policy policy = e.settings.plannerPolicy();
                        Error err = MediaPipelinePlanner::plan(e.userConfig,
                                                               &resolved,
                                                               policy,
                                                               &diag);
                        if(err.isError()) {
                                if(!diag.isEmpty()) {
                                        promekiWarn("Pipeline %s plan failed: %s",
                                                    id.cstr(), diag.cstr());
                                }
                                return err;
                        }
                        e.resolvedConfig = resolved;
                } else {
                        e.resolvedConfig = e.userConfig;
                }
                raw = e.pipeline.get();
                toBuild = e.resolvedConfig;
                statsInterval = e.settings.statsInterval();
        }
        raw->setStatsInterval(statsInterval);
        return raw->build(toBuild, /*autoplan=*/false);
}

Error PipelineManager::open(const String &id) {
        MediaPipeline *raw = nullptr;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _entries.find(id);
                if(it == _entries.end()) return Error::NotExist;
                raw = it->second.pipeline.get();
                if(raw == nullptr) return Error::Invalid;
        }
        return raw->open();
}

Error PipelineManager::start(const String &id) {
        MediaPipeline *raw = nullptr;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _entries.find(id);
                if(it == _entries.end()) return Error::NotExist;
                raw = it->second.pipeline.get();
                if(raw == nullptr) return Error::Invalid;
        }
        return raw->start();
}

Error PipelineManager::stop(const String &id) {
        MediaPipeline *raw = nullptr;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _entries.find(id);
                if(it == _entries.end()) return Error::NotExist;
                raw = it->second.pipeline.get();
                if(raw == nullptr) return Error::Invalid;
        }
        return raw->stop();
}

Error PipelineManager::close(const String &id, bool block) {
        // Snapshot the pipeline pointer under the lock so the close
        // cascade (which fires closedSignal on this loop) doesn't
        // re-enter the manager's mutex.
        MediaPipeline *raw = nullptr;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _entries.find(id);
                if(it == _entries.end()) return Error::NotExist;
                raw = it->second.pipeline.get();
                if(raw == nullptr) return Error::Invalid;
        }
        return raw->close(block);
}

Error PipelineManager::run(const String &id) {
        // Pull the current state once under the lock; subsequent
        // lifecycle calls each take the lock themselves and re-check.
        // We don't hold the lock across the chain because each step
        // can publish PipelineEvents that loop back through publish().
        MediaPipeline::State state = MediaPipeline::State::Empty;
        {
                promeki::Mutex::Locker guard(_mutex);
                auto it = _entries.find(id);
                if(it == _entries.end()) return Error::NotExist;
                if(it->second.pipeline.get() == nullptr) return Error::Invalid;
                state = it->second.pipeline->state();
        }

        // Stopped is a terminal pre-close state — bring it back via
        // close → build → open → start.  Every other state has a direct
        // forward path to Running.
        if(state == MediaPipeline::State::Stopped) {
                Error err = close(id, /*block=*/true);
                if(err.isError()) return err;
                state = MediaPipeline::State::Closed;
        }

        if(state == MediaPipeline::State::Empty ||
           state == MediaPipeline::State::Closed) {
                Error err = build(id);
                if(err.isError()) return err;
                state = MediaPipeline::State::Built;
        }

        if(state == MediaPipeline::State::Built) {
                Error err = open(id);
                if(err.isError()) return err;
                state = MediaPipeline::State::Open;
        }

        if(state == MediaPipeline::State::Open) {
                Error err = start(id);
                if(err.isError()) return err;
                state = MediaPipeline::State::Running;
        }

        if(state == MediaPipeline::State::Running) return Error::Ok;
        return Error::Invalid;
}

List<String> PipelineManager::ids() const {
        promeki::Mutex::Locker guard(_mutex);
        return _order;
}

const PipelineManager::Entry *PipelineManager::find(const String &id) const {
        promeki::Mutex::Locker guard(_mutex);
        auto it = _entries.find(id);
        if(it == _entries.end()) return nullptr;
        return &it->second;
}

PipelineManager::Entry *PipelineManager::find(const String &id) {
        promeki::Mutex::Locker guard(_mutex);
        auto it = _entries.find(id);
        if(it == _entries.end()) return nullptr;
        return &it->second;
}

JsonObject PipelineManager::describe(const String &id) const {
        promeki::Mutex::Locker guard(_mutex);
        auto it = _entries.find(id);
        if(it == _entries.end()) return JsonObject();
        return describeEntry(it->second);
}

JsonObject PipelineManager::describeAll() const {
        JsonObject out;
        JsonArray arr;
        promeki::Mutex::Locker guard(_mutex);
        for(const auto &id : _order) {
                auto it = _entries.find(id);
                if(it == _entries.end()) continue;
                arr.add(describeEntry(it->second));
        }
        out.set("pipelines", arr);
        return out;
}

int PipelineManager::subscribe(EventCallback cb) {
        if(!cb) return -1;
        promeki::Mutex::Locker guard(_mutex);
        Subscriber s;
        s.id = _nextSubId++;
        s.fn = std::move(cb);
        const int id = s.id;
        _subscribers.insert(id, std::move(s));
        return id;
}

void PipelineManager::unsubscribe(int id) {
        promeki::Mutex::Locker guard(_mutex);
        _subscribers.remove(id);
}

String PipelineManager::stateToString(MediaPipeline::State s) {
        switch(s) {
                case MediaPipeline::State::Empty:   return "Empty";
                case MediaPipeline::State::Built:   return "Built";
                case MediaPipeline::State::Open:    return "Open";
                case MediaPipeline::State::Running: return "Running";
                case MediaPipeline::State::Stopped: return "Stopped";
                case MediaPipeline::State::Closed:  return "Closed";
        }
        return "Empty";
}

String PipelineManager::generateId() {
        const String full = UUID::generateV4().toString();
        // UUID strings are 36 chars; hex digits start at position 0.
        // Slice the first IdLength hex characters as a short id.  No
        // strict uniqueness guarantee but more than enough for a demo
        // that creates a handful of pipelines per session.
        return full.left(IdLength);
}

bool PipelineManager::stateAllowsConfigRewrite(MediaPipeline::State s) {
        return s == MediaPipeline::State::Empty ||
               s == MediaPipeline::State::Built ||
               s == MediaPipeline::State::Closed;
}

void PipelineManager::publish(const String &id, const PipelineEvent &ev) {
        // Snapshot under the lock, dispatch outside.  Subscribers may
        // legitimately install / remove subscriptions in their callback
        // (e.g. the future EventBroadcaster removing a closed WebSocket)
        // and would otherwise deadlock on _mutex.
        List<Subscriber> snapshot;
        {
                promeki::Mutex::Locker guard(_mutex);
                snapshot.reserve(_subscribers.size());
                for(const auto &kv : _subscribers) snapshot.pushToBack(kv.second);
        }
        for(const auto &s : snapshot) {
                if(s.fn) s.fn(id, ev);
        }
}

JsonObject PipelineManager::describeEntry(const Entry &e) const {
        JsonObject obj;
        obj.set("id", e.id);
        obj.set("name", e.settings.name());
        obj.set("state", e.pipeline.get() != nullptr
                ? stateToString(e.pipeline->state())
                : String("Empty"));
        obj.set("settings", e.settings.toJson());
        obj.set("userConfig", e.userConfig.toJson());
        obj.set("resolvedConfig", e.resolvedConfig.toJson());
        return obj;
}

} // namespace promekipipeline
