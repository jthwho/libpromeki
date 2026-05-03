/**
 * @file      mediapipelinestats.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiostats.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/windowedstatsbundle.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Per-stage telemetry record.
 * @ingroup pipeline
 *
 * Combines the cumulative @ref MediaIO::stats aggregate with the
 * per-@ref MediaIOCommand::Kind windowed breakdown collected by
 * @ref MediaIOStatsCollector so a single value fully describes a
 * stage's runtime state.  Round-trips through JSON (for logging /
 * REST endpoints) and @ref DataStream (for IPC).
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.
 */
class MediaPipelineStageStats {
        public:
                /** @brief Map of command kind to its windowed-stat bundle. */
                using WindowedMap = ::promeki::Map<MediaIOCommand::Kind, WindowedStatsBundle>;

                /** @brief List of plain-value stage records. */
                using List = ::promeki::List<MediaPipelineStageStats>;

                MediaPipelineStageStats() = default;

                /** @brief Stage identifier (matches @ref MediaPipeline::stage). */
                String name;

                /**
                 * @brief Cumulative aggregate from @ref MediaIO::stats.
                 *
                 * Populated with whatever the backend's
                 * @c executeCmd(MediaIOCommandStats &) and
                 * @c MediaIO::populateStandardStats produced at
                 * snapshot time.  Empty when the stage was not open.
                 */
                MediaIOStats cumulative;

                /**
                 * @brief Per-command-kind windowed breakdown.
                 *
                 * Keyed by @ref MediaIOCommand::Kind.  Each value is a
                 * @ref WindowedStatsBundle holding one
                 * @ref WindowedStat per stat ID the
                 * @ref MediaIOStatsCollector observed for that command
                 * kind (@c ExecuteDuration, @c QueueWaitDuration,
                 * @c BytesProcessed plus any backend-specific keys).
                 * Empty when no collector was attached or no commands
                 * of that kind have resolved yet.
                 */
                WindowedMap windowed;

                /**
                 * @brief Returns the windowed bundle for @p kind, creating
                 *        an empty one on first access.
                 *
                 * Convenience accessor — @c windowed[kind] works too,
                 * but this method's return type makes the intent
                 * explicit at the call site.
                 */
                WindowedStatsBundle &windowedBundle(MediaIOCommand::Kind kind);

                /** @brief True when no entries are present in either view. */
                bool isEmpty() const { return cumulative.size() == 0 && windowed.isEmpty(); }

                /** @brief Drops every cumulative entry and every windowed bundle. */
                void clear();

                /** @brief Equality compares name, cumulative, and every windowed bundle. */
                bool operator==(const MediaPipelineStageStats &other) const;
                bool operator!=(const MediaPipelineStageStats &other) const { return !(*this == other); }

                /** @brief Serializes the stage record to a @ref JsonObject. */
                JsonObject toJson() const;

                /**
                 * @brief Reconstructs a stage record from a @ref JsonObject.
                 * @param obj The JSON object produced by @ref toJson.
                 * @param err Optional error output.
                 */
                static MediaPipelineStageStats fromJson(const JsonObject &obj, Error *err = nullptr);
};

/** @brief Writes a MediaPipelineStageStats to a DataStream. */
DataStream &operator<<(DataStream &stream, const MediaPipelineStageStats &s);

/** @brief Reads a MediaPipelineStageStats from a DataStream. */
DataStream &operator>>(DataStream &stream, MediaPipelineStageStats &s);

/**
 * @brief Telemetry snapshot for a running @ref MediaPipeline.
 * @ingroup pipeline
 *
 * Holds an array of @ref MediaPipelineStageStats entries — one per
 * live stage in topological order — produced by
 * @ref MediaPipeline::stats.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used
 * concurrently; concurrent access to a single instance must be
 * externally synchronized.  @c MediaPipelineStats::Ptr uses an
 * atomic refcount and is safe to share across threads.
 */
class MediaPipelineStats {
                PROMEKI_SHARED_FINAL(MediaPipelineStats)
        public:
                /** @brief Shared pointer alias. */
                using Ptr = SharedPtr<MediaPipelineStats>;

                /** @brief List of value snapshots. */
                using List = ::promeki::List<MediaPipelineStats>;

                /** @brief List of shared snapshot pointers. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Ordered list of per-stage records. */
                using StageList = MediaPipelineStageStats::List;

                MediaPipelineStats() = default;

                // ------------------------------------------------------------
                // Stage accessors
                // ------------------------------------------------------------

                /** @brief Returns the per-stage list (read-only). */
                const StageList &stages() const { return _stages; }

                /** @brief Returns the per-stage list (mutable). */
                StageList &stages() { return _stages; }

                /** @brief Appends a stage record. */
                void addStage(const MediaPipelineStageStats &s) { _stages.pushToBack(s); }

                /** @brief Appends a stage record (move). */
                void addStage(MediaPipelineStageStats &&s) { _stages.pushToBack(std::move(s)); }

                /**
                 * @brief Returns the stage record for @p name by value.
                 *
                 * Friendlier than @ref findStage for callers that just
                 * want the data: returns a default-constructed
                 * @ref MediaPipelineStageStats when @p name is absent
                 * rather than forcing a null-pointer check.  Use
                 * @ref findStage when you need to mutate the entry in
                 * place or distinguish "not present" from "present but
                 * empty".
                 */
                MediaPipelineStageStats stage(const String &name) const;

                /**
                 * @brief Looks up a stage record by name.
                 * @return Pointer into @ref stages, or @c nullptr if
                 *         no entry has the given name.
                 */
                const MediaPipelineStageStats *findStage(const String &name) const;

                /** @brief Mutable @ref findStage. */
                MediaPipelineStageStats *findStage(const String &name);

                /** @brief True if a stage with @p name is present. */
                bool containsStage(const String &name) const { return findStage(name) != nullptr; }

                /** @brief Returns every stage name in declaration order. */
                StringList stageNames() const;

                /** @brief True when no stages have been recorded. */
                bool isEmpty() const { return _stages.isEmpty(); }

                /** @brief Drops every per-stage record. */
                void clear() { _stages.clear(); }

                // ------------------------------------------------------------
                // Rendering / serialization
                // ------------------------------------------------------------

                /**
                 * @brief Produces a human-readable, column-aligned table.
                 *
                 * One row per (stage, command kind, stat name, value)
                 * tuple — windowed stats render via
                 * @ref WindowedStat::toString with unit-aware formatters,
                 * cumulative stats render via the type-specific
                 * @ref Units helpers.
                 */
                StringList describe() const;

                /** @brief Serializes the snapshot to a @ref JsonObject. */
                JsonObject toJson() const;

                /**
                 * @brief Reconstructs a snapshot from a @ref JsonObject.
                 * @param obj The JSON object produced by @ref toJson.
                 * @param err Optional error output.
                 */
                static MediaPipelineStats fromJson(const JsonObject &obj, Error *err = nullptr);

                /** @brief Equality compares every per-stage record in order. */
                bool operator==(const MediaPipelineStats &other) const;
                bool operator!=(const MediaPipelineStats &other) const { return !(*this == other); }

        private:
                StageList _stages;
};

/** @brief Writes a MediaPipelineStats snapshot to a DataStream. */
DataStream &operator<<(DataStream &stream, const MediaPipelineStats &s);

/** @brief Reads a MediaPipelineStats snapshot from a DataStream. */
DataStream &operator>>(DataStream &stream, MediaPipelineStats &s);

PROMEKI_NAMESPACE_END
