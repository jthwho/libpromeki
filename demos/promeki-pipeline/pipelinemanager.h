/**
 * @file      pipelinemanager.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>

#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mutex.h>
#include <promeki/objectbase.h>
#include <promeki/pipelineevent.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/uniqueptr.h>

#include "pipelinesettings.h"

namespace promekipipeline {

/**
 * @brief Owns the demo's collection of @c MediaPipeline instances.
 *
 * Each tracked entry pairs a @c MediaPipeline with the user-authored
 * @c MediaPipelineConfig (the graph the UI is editing), the planner's
 * resolved @c MediaPipelineConfig (the graph the UI displays — same
 * stages plus any auto-inserted bridges), and a @ref PipelineSettings
 * block (display name, planner policy, stats cadence).  A short id
 * generated at @ref create time keys the entry; subsequent lifecycle
 * calls (@ref build, @ref open, @ref start, @ref stop, @ref close)
 * route through that id.
 *
 * The manager fans every per-pipeline @c PipelineEvent stream into a
 * single subscription point.  Callers register a single
 * @ref EventCallback via @ref subscribe and receive events from every
 * tracked pipeline tagged with the originating id; the manager
 * installs a sub on each pipeline at @ref create time and tears it
 * down at @ref remove time, so subscribers do not have to track
 * pipeline churn manually.  Phase D's WebSocket broadcaster sits on
 * top of this fan-out.
 *
 * @par Threading
 * Construction captures the calling thread's @c EventLoop.  All
 * mutating methods acquire an internal mutex; the map and per-entry
 * bookkeeping (sub ids, subscriber list) are guarded.  Subscriber
 * callbacks fire on the subscriber's own @c EventLoop because the
 * underlying @c MediaPipeline::subscribe contract dispatches that way
 * — @ref PipelineManager simply re-publishes through the same
 * mechanism.
 */
class PipelineManager : public promeki::ObjectBase {
        PROMEKI_OBJECT(PipelineManager, ObjectBase)
        public:
                /** @brief Length in hex characters of the auto-generated pipeline id. */
                static constexpr int IdLength = 8;

                /**
                 * @brief Snapshot of one tracked pipeline.
                 *
                 * Held inside the manager's id → entry map.  The
                 * manager hands snapshots out by const reference under
                 * its mutex; callers must not stash the pointer past
                 * the next mutating call.
                 */
                struct Entry {
                        /** @brief Stable short id; assigned at @ref create time. */
                        promeki::String              id;

                        /** @brief Display name + planner policy + stats cadence. */
                        PipelineSettings             settings;

                        /** @brief Graph as authored / edited by the user. */
                        promeki::MediaPipelineConfig userConfig;

                        /**
                         * @brief Graph after the planner has resolved bridges.
                         *
                         * Equal to @ref userConfig when the entry's
                         * @ref PipelineSettings::autoplan is @c false.
                         * Populated at @ref build time.
                         */
                        promeki::MediaPipelineConfig resolvedConfig;

                        /** @brief Owning pointer to the live @c MediaPipeline. */
                        promeki::UniquePtr<promeki::MediaPipeline> pipeline;

                        /**
                         * @brief Sub id returned by @c MediaPipeline::subscribe.
                         *
                         * Held so @ref remove can deregister cleanly.
                         */
                        int                          pipelineSubId = -1;
                };

                /**
                 * @brief Callback signature for @ref subscribe.
                 *
                 * Invoked once per @c PipelineEvent from any tracked
                 * pipeline.  @p id identifies the originating pipeline,
                 * @p ev carries the unmodified payload from
                 * @c MediaPipeline::publish.
                 */
                using EventCallback = std::function<void(const promeki::String &id,
                                                         const promeki::PipelineEvent &ev)>;

                /**
                 * @brief Constructs a manager bound to the calling EventLoop.
                 * @param parent Optional ObjectBase parent.
                 */
                explicit PipelineManager(promeki::ObjectBase *parent = nullptr);

                /** @brief Destructor — closes and releases every tracked pipeline. */
                ~PipelineManager() override;

                // ------------------------------------------------------------
                // Pipeline lifecycle
                // ------------------------------------------------------------

                /**
                 * @brief Allocates a new pipeline and returns its short id.
                 *
                 * The new entry holds an empty @c MediaPipelineConfig,
                 * a default-constructed @ref PipelineSettings whose
                 * @ref PipelineSettings::name is @p name, and a freshly
                 * heap-allocated @c MediaPipeline parented to this
                 * manager.  A subscription is wired up so every event
                 * the new pipeline emits feeds the manager's fan-out.
                 *
                 * @param name Initial display name; passes through into
                 *             @ref PipelineSettings::name.
                 * @return The new pipeline's id.
                 */
                promeki::String create(const promeki::String &name);

                /**
                 * @brief Closes and removes the pipeline identified by @p id.
                 *
                 * If the pipeline is still running it is closed
                 * synchronously before being released so subscribers do
                 * not leak past the entry.
                 *
                 * @param id The pipeline id.
                 * @return @c Error::Ok, or @c Error::NotExist when no
                 *         entry matches @p id.
                 */
                promeki::Error remove(const promeki::String &id);

                /**
                 * @brief Replaces the user-authored graph for @p id.
                 *
                 * Allowed only when the underlying pipeline state is in
                 * @c MediaPipeline::State::Empty,
                 * @c MediaPipeline::State::Built or
                 * @c MediaPipeline::State::Closed — the pipeline must
                 * be torn down before its config can be rewritten.
                 *
                 * @param id         The pipeline id.
                 * @param userConfig The new user graph.
                 * @return @c Error::Ok on success; @c Error::NotExist
                 *         when @p id is unknown; @c Error::Busy when
                 *         the pipeline is currently open / running /
                 *         stopped.
                 */
                promeki::Error replaceConfig(const promeki::String &id,
                                             const promeki::MediaPipelineConfig &userConfig);

                /**
                 * @brief Replaces the @ref PipelineSettings for @p id.
                 *
                 * If the new @ref PipelineSettings::statsInterval differs
                 * from the previous value and the pipeline is currently
                 * @c MediaPipeline::State::Running, the new interval is
                 * pushed into the live pipeline immediately.  Other
                 * fields take effect on the next @ref build.
                 *
                 * @param id       The pipeline id.
                 * @param settings The new settings block.
                 * @return @c Error::Ok or @c Error::NotExist.
                 */
                promeki::Error replaceSettings(const promeki::String &id,
                                               const PipelineSettings &settings);

                /**
                 * @brief Validates and instantiates the pipeline for @p id.
                 *
                 * When the entry's @ref PipelineSettings::autoplan is
                 * @c true the planner is run with the entry's
                 * @ref PipelineSettings::plannerPolicy and the resolved
                 * config is stored alongside the user config.  When
                 * @c autoplan is @c false the resolved config equals
                 * the user config and is passed through to
                 * @c MediaPipeline::build with @c autoplan=false.
                 *
                 * @param id The pipeline id.
                 * @return @c Error::Ok on success; @c Error::NotExist
                 *         when @p id is unknown; the planner / build
                 *         error otherwise.
                 */
                promeki::Error build(const promeki::String &id);

                /**
                 * @brief Opens every stage of the pipeline for @p id.
                 * @param id The pipeline id.
                 * @return @c Error::Ok, @c Error::NotExist, or the
                 *         error reported by @c MediaPipeline::open.
                 */
                promeki::Error open(const promeki::String &id);

                /**
                 * @brief Starts the drain for the pipeline for @p id.
                 * @param id The pipeline id.
                 * @return @c Error::Ok, @c Error::NotExist, or the
                 *         error reported by @c MediaPipeline::start.
                 */
                promeki::Error start(const promeki::String &id);

                /**
                 * @brief Stops the drain for the pipeline for @p id.
                 * @param id The pipeline id.
                 * @return @c Error::Ok, @c Error::NotExist, or the
                 *         error reported by @c MediaPipeline::stop.
                 */
                promeki::Error stop(const promeki::String &id);

                /**
                 * @brief Closes every stage of the pipeline for @p id.
                 *
                 * @param id    The pipeline id.
                 * @param block When @c true (default) blocks until the
                 *              cascade completes; when @c false returns
                 *              immediately and completion is observable
                 *              via @c MediaPipeline::closedSignal.
                 * @return @c Error::Ok, @c Error::NotExist, or the
                 *         error reported by @c MediaPipeline::close.
                 */
                promeki::Error close(const promeki::String &id, bool block = true);

                /**
                 * @brief UX-helper macro: drives @p id to @c State::Running.
                 *
                 * Inspects the entry's current @c MediaPipeline::State and
                 * chains the minimum lifecycle steps required to reach
                 * @c Running:
                 *
                 *  - @c Empty   → @ref build → @ref open → @ref start
                 *  - @c Built   → @ref open  → @ref start
                 *  - @c Open    → @ref start
                 *  - @c Running → no-op (returns @c Error::Ok)
                 *  - @c Stopped → @ref close → @ref build → @ref open → @ref start
                 *  - @c Closed  → @ref build → @ref open → @ref start
                 *
                 * The library's @c MediaPipeline::stop is documented as a
                 * one-shot transition into the terminal pre-close state,
                 * so re-running a stopped pipeline requires the
                 * @c close → @c build cascade above.  This convenience
                 * lets the demo's frontend offer a single @c [Start]
                 * button without forcing the user to learn that
                 * sequence.  Advanced callers can still drive the
                 * lifecycle one step at a time via the existing methods.
                 *
                 * @param id The pipeline id.
                 * @return @c Error::Ok when the pipeline reaches
                 *         @c Running; @c Error::NotExist when @p id is
                 *         unknown; otherwise the error from the first
                 *         step that failed (the pipeline is left in
                 *         whatever state that step produced).
                 */
                promeki::Error run(const promeki::String &id);

                // ------------------------------------------------------------
                // Introspection
                // ------------------------------------------------------------

                /** @brief Returns every tracked pipeline id, in insertion order. */
                promeki::List<promeki::String> ids() const;

                /**
                 * @brief Looks up an entry by id (read-only).
                 *
                 * The returned pointer is stable until the next
                 * @ref create / @ref remove call; it must not be
                 * dereferenced outside the manager's owning thread.
                 *
                 * @return Non-owning entry pointer, or @c nullptr.
                 */
                const Entry *find(const promeki::String &id) const;

                /** @brief Mutable variant of @ref find. */
                Entry *find(const promeki::String &id);

                /**
                 * @brief Produces a JSON snapshot of the entry for @p id.
                 *
                 * Shape (Phase D feeds this directly to HTTP):
                 * @code
                 * {
                 *   "id": "abc12345",
                 *   "name": "Untitled",
                 *   "state": "Empty",
                 *   "settings": { ... },
                 *   "userConfig": { ... },
                 *   "resolvedConfig": { ... }
                 * }
                 * @endcode
                 *
                 * Returns an empty @c JsonObject when @p id is unknown.
                 */
                promeki::JsonObject describe(const promeki::String &id) const;

                /**
                 * @brief Produces a JSON snapshot of every tracked pipeline.
                 *
                 * The returned object has shape
                 * @code
                 * { "pipelines": [<describe(id)>, ...] }
                 * @endcode
                 * Phase D will likely strip the wrapper and return the
                 * raw array; the wrapper here keeps the snapshot
                 * self-describing.
                 */
                promeki::JsonObject describeAll() const;

                // ------------------------------------------------------------
                // Event fan-out
                // ------------------------------------------------------------

                /**
                 * @brief Registers @p cb to receive every pipeline's events,
                 *        tagged with the originating pipeline id.
                 *
                 * The subscriber's @c EventLoop is captured at the call
                 * site (via @c MediaPipeline::subscribe inside
                 * @ref create), so callbacks always run on the
                 * subscriber's thread.  The callback fires once per
                 * event, even when the manager tracks many pipelines.
                 *
                 * @param cb Callback invoked with @c (pipelineId, event).
                 *           An empty callback is rejected and yields
                 *           @c -1.
                 * @return Non-negative subscription id usable with
                 *         @ref unsubscribe, or @c -1 when @p cb was
                 *         empty.
                 */
                int subscribe(EventCallback cb);

                /**
                 * @brief Removes a subscription previously installed via @ref subscribe.
                 *
                 * Unknown ids are silently ignored.
                 *
                 * @param id The subscription id returned by @ref subscribe.
                 */
                void unsubscribe(int id);

                /**
                 * @brief Translates a @c MediaPipeline::State to a stable string.
                 *
                 * Round-trips with the names used in the JSON snapshot.
                 */
                static promeki::String stateToString(promeki::MediaPipeline::State s);

        private:
                struct Subscriber {
                        int           id;
                        EventCallback fn;
                };

                /** @brief Generates a fresh short id (@c IdLength hex characters). */
                static promeki::String generateId();

                /** @brief Returns true when @p s permits a config rewrite. */
                static bool stateAllowsConfigRewrite(promeki::MediaPipeline::State s);

                /** @brief Snapshot the subscriber list under the lock and call them outside it. */
                void publish(const promeki::String &id, const promeki::PipelineEvent &ev);

                /** @brief Build the per-id describe() payload for an entry already located. */
                promeki::JsonObject describeEntry(const Entry &entry) const;

                mutable promeki::Mutex                            _mutex;
                promeki::Map<promeki::String, Entry>              _entries;
                promeki::List<promeki::String>                    _order;
                promeki::Map<int, Subscriber>                     _subscribers;
                int                                               _nextSubId = 0;
};

} // namespace promekipipeline
