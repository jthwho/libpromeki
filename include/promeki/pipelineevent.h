/**
 * @file      pipelineevent.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/metadata.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/variant.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Value-typed envelope for everything @ref MediaPipeline can
 *        announce to a subscriber.
 * @ingroup pipeline
 *
 * A @ref PipelineEvent unifies the existing per-signal stage / pipeline
 * notifications and the new stats tick into one stream that callers
 * (a demo UI, a REST relay, an in-process listener) can subscribe to
 * via @ref MediaPipeline::subscribe.  Each event carries:
 *  - a @ref Kind that describes what happened,
 *  - an optional @ref stageName (empty for pipeline-level events),
 *  - either a @ref Variant @ref payload (primitive payloads such as a
 *    state name or log message) or a @ref JsonObject @ref jsonPayload
 *    (richer payloads such as a @c MediaPipelineStats or
 *    @c MediaPipelineConfig snapshot — these types do not round-trip
 *    through @ref Variant directly so they are stored as already-
 *    serialized JSON),
 *  - free-form @ref metadata,
 *  - a @ref timestamp seeded by @ref MediaPipeline::publish when the
 *    event is dispatched.
 *
 * The class is a plain Shareable value (not an @ref ObjectBase): it
 * follows the project's data-object policy with @ref PROMEKI_SHARED_FINAL
 * so callers may share via @ref Ptr when convenient and copy by value
 * otherwise.
 *
 * @par Per-kind payload contract
 * - @ref Kind::StateChanged  — @ref payload holds a @c String naming
 *   the new @c MediaPipeline::State (e.g. @c "Running").
 * - @ref Kind::StageState    — @ref payload holds a @c String naming
 *   the stage transition (@c "Opened", @c "Started", @c "Stopped",
 *   @c "Closed"); @ref stageName identifies the stage.
 * - @ref Kind::StageError    — @ref payload holds a @c String message;
 *   @ref metadata carries the error name / code via @c "code".
 * - @ref Kind::StatsUpdated  — @ref jsonPayload holds the result of
 *   @c MediaPipelineStats::toJson; @ref payload is invalid.
 * - @ref Kind::PlanResolved  — @ref jsonPayload holds the result of
 *   @c MediaPipelineConfig::toJson; @ref payload is invalid.
 * - @ref Kind::Log           — @ref payload holds the log message
 *   @c String; @ref metadata carries @c "level", @c "source",
 *   @c "line", @c "threadName".
 *
 * @par Example
 * @code
 * PipelineEvent ev;
 * ev.setKind(PipelineEvent::Kind::StateChanged);
 * ev.setPayload(Variant(String("Running")));
 *
 * JsonObject json = ev.toJson();
 * Error err;
 * PipelineEvent round = PipelineEvent::fromJson(json, &err);
 * assert(err.isOk());
 * assert(round.kind() == PipelineEvent::Kind::StateChanged);
 * @endcode
 */
class PipelineEvent {
        PROMEKI_SHARED_FINAL(PipelineEvent)
        public:
                /** @brief Shared pointer alias. */
                using Ptr = SharedPtr<PipelineEvent>;

                /** @brief List of plain-value PipelineEvent records. */
                using List = promeki::List<PipelineEvent>;

                /** @brief List of shared PipelineEvent pointers. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Categorical tag identifying which kind of pipeline
                 *        notification this event represents.
                 */
                enum class Kind {
                        StateChanged,   ///< Pipeline-level state transition.
                        StageState,     ///< Per-stage open/start/stop/close transition.
                        StageError,     ///< Error reported by a stage (or pipeline).
                        StatsUpdated,   ///< Periodic stats snapshot.
                        PlanResolved,   ///< Resolved config after planner ran.
                        Log             ///< Log message captured via Logger listener.
                };

                /** @brief Default-constructs an empty event with @ref Kind::StateChanged. */
                PipelineEvent() = default;

                // ------------------------------------------------------------
                // Field accessors
                // ------------------------------------------------------------

                /** @brief Returns the event kind. */
                Kind kind() const { return _kind; }

                /** @brief Returns the originating stage, or an empty string for pipeline-level events. */
                const String &stageName() const { return _stageName; }

                /**
                 * @brief Returns the primitive payload held in a Variant.
                 *
                 * Populated for @ref Kind::StateChanged, @ref Kind::StageState,
                 * @ref Kind::StageError and @ref Kind::Log.  Empty for
                 * @ref Kind::StatsUpdated and @ref Kind::PlanResolved — those
                 * kinds use @ref jsonPayload instead.
                 */
                const Variant &payload() const { return _payload; }

                /**
                 * @brief Returns the JSON-serialized payload.
                 *
                 * Populated for @ref Kind::StatsUpdated (a @c MediaPipelineStats
                 * snapshot) and @ref Kind::PlanResolved (a @c MediaPipelineConfig
                 * snapshot).  Empty for the primitive-payload kinds — those
                 * use @ref payload instead.
                 */
                const JsonObject &jsonPayload() const { return _jsonPayload; }

                /** @brief Returns the free-form metadata block (level, source, etc.). */
                const Metadata &metadata() const { return _metadata; }

                /** @brief Returns the dispatch timestamp seeded by @ref MediaPipeline::publish. */
                const TimeStamp &timestamp() const { return _ts; }

                // ------------------------------------------------------------
                // Field mutators
                // ------------------------------------------------------------

                /** @brief Replaces the kind. */
                void setKind(Kind k) { _kind = k; }

                /** @brief Replaces the stage name. */
                void setStageName(const String &s) { _stageName = s; }

                /** @brief Replaces the primitive payload. */
                void setPayload(const Variant &v) { _payload = v; }

                /** @brief Replaces the JSON-serialized payload. */
                void setJsonPayload(const JsonObject &j) { _jsonPayload = j; }

                /** @brief Replaces the metadata block. */
                void setMetadata(const Metadata &m) { _metadata = m; }

                /** @brief Replaces the dispatch timestamp. */
                void setTimestamp(const TimeStamp &t) { _ts = t; }

                // ------------------------------------------------------------
                // Serialization
                // ------------------------------------------------------------

                /**
                 * @brief Serializes the event to a stable JSON envelope.
                 *
                 * Shape:
                 * @code
                 * {
                 *   "kind": "StatsUpdated",
                 *   "stage": "tpg1",       // omitted when empty
                 *   "ts": 12345.678,       // monotonic seconds
                 *   "metadata": { ... },   // omitted when empty
                 *   "payload": ...         // primitive: scalar; complex: object
                 * }
                 * @endcode
                 * Primitive payloads are emitted as JSON scalars.  Complex
                 * payloads (StatsUpdated / PlanResolved) are emitted as
                 * the nested JSON object held by @ref jsonPayload.
                 */
                JsonObject toJson() const;

                /**
                 * @brief Reconstructs an event from a JSON envelope.
                 * @param obj JSON object as produced by @ref toJson.
                 * @param err Optional error output — set to
                 *            @c Error::ParseFailed when @c "kind" is
                 *            missing or unknown, @c Error::Invalid for
                 *            other malformed input.
                 * @return The decoded event.  On failure the returned
                 *         event holds defaults and @p err carries the
                 *         specific cause.
                 */
                static PipelineEvent fromJson(const JsonObject &obj, Error *err = nullptr);

                /**
                 * @brief Returns the stable string name of @p k.
                 *
                 * Round-trips through @ref kindFromString.  Unknown
                 * values yield @c "StateChanged" (the default).
                 */
                static String kindToString(Kind k);

                /**
                 * @brief Parses a string previously produced by @ref kindToString.
                 * @param s  The string to parse.
                 * @param ok Optional flag set to @c true when @p s named a
                 *           valid kind, @c false otherwise.
                 * @return The decoded kind, or @ref Kind::StateChanged on
                 *         unknown input.
                 */
                static Kind kindFromString(const String &s, bool *ok = nullptr);

        private:
                Kind            _kind = Kind::StateChanged;
                String          _stageName;
                Variant         _payload;
                JsonObject      _jsonPayload;
                Metadata        _metadata;
                TimeStamp       _ts;
};

PROMEKI_NAMESPACE_END
