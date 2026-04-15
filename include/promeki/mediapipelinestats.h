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
#include <promeki/mediaio.h>
#include <promeki/pipelinestats.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Aggregated statistics for a running @ref MediaPipeline.
 * @ingroup pipeline
 *
 * Holds a @ref MediaIOStats snapshot for each stage, a rolled-up
 * per-stage @ref aggregate, and a @ref PipelineStats bucket for
 * counters that describe the drain layer itself (total fan-out,
 * write retries, pipeline errors, back-pressure).  The aggregate
 * reduces the per-stage numbers as follows:
 *
 * - Counters (@c FramesDropped / @c FramesRepeated / @c FramesLate /
 *   @c QueueDepth / @c QueueCapacity / @c PendingOperations) are summed.
 * - Throughput (@c BytesPerSecond / @c FramesPerSecond) is summed across
 *   stages — useful for total pipeline bandwidth.
 * - Average latency / processing times are averaged over the set of
 *   stages that reported a non-zero value.
 * - Peak latency / processing times use the maximum across stages.
 * - @c LastErrorMessage takes the first non-empty message, prefixed with
 *   its stage name.
 *
 * The snapshot round-trips through JSON (for logging / REST endpoints)
 * and @ref DataStream (for IPC).
 */
class MediaPipelineStats {
        PROMEKI_SHARED_FINAL(MediaPipelineStats)
        public:
                /** @brief Shared pointer alias. */
                using Ptr = SharedPtr<MediaPipelineStats>;

                /** @brief Map of stage name to its @ref MediaIOStats. */
                using PerStageMap = promeki::Map<String, MediaIOStats>;

                /** @brief List of value snapshots. */
                using List = promeki::List<MediaPipelineStats>;

                /** @brief List of shared snapshot pointers. */
                using PtrList = promeki::List<Ptr>;

                MediaPipelineStats() = default;

                // ------------------------------------------------------------
                // Per-stage accessors
                // ------------------------------------------------------------

                /** @brief Returns the per-stage stats map (read-only). */
                const PerStageMap &perStage() const { return _perStage; }

                /** @brief Returns the per-stage stats map (mutable). */
                PerStageMap &perStage() { return _perStage; }

                /**
                 * @brief Sets (or replaces) the stats record for @p stageName.
                 *
                 * Does not automatically refresh the aggregate; call
                 * @ref recomputeAggregate after a batch of updates.
                 */
                void setStageStats(const String &stageName, const MediaIOStats &stats);

                /**
                 * @brief Returns the stats record for @p stageName,
                 *        or an empty @ref MediaIOStats if absent.
                 */
                MediaIOStats stageStats(const String &stageName) const;

                /** @brief Returns true if @p stageName has a stored record. */
                bool containsStage(const String &stageName) const {
                        return _perStage.contains(stageName);
                }

                /** @brief Removes every per-stage record and clears the aggregate. */
                void clear();

                // ------------------------------------------------------------
                // Aggregate
                // ------------------------------------------------------------

                /** @brief Returns the rolled-up pipeline-wide stats (read-only). */
                const MediaIOStats &aggregate() const { return _aggregate; }

                /** @brief Returns the rolled-up stats (mutable). */
                MediaIOStats &aggregate() { return _aggregate; }

                /**
                 * @brief Rebuilds the @ref aggregate from the current per-stage records.
                 *
                 * See the class-level docs for the reduction rules.  Callers
                 * typically invoke this after @ref setStageStats loops or
                 * after loading via @ref fromJson.
                 */
                void recomputeAggregate();

                // ------------------------------------------------------------
                // Pipeline-layer counters
                // ------------------------------------------------------------

                /** @brief Returns the pipeline-layer counters (read-only). */
                const PipelineStats &pipeline() const { return _pipeline; }

                /** @brief Returns the pipeline-layer counters (mutable). */
                PipelineStats &pipeline() { return _pipeline; }

                /** @brief Replaces the pipeline-layer counters block. */
                void setPipeline(const PipelineStats &p) { _pipeline = p; }

                // ------------------------------------------------------------
                // Rendering / serialization
                // ------------------------------------------------------------

                /**
                 * @brief Produces a human-readable multi-line summary.
                 *
                 * Emits one line per stage (via @ref MediaIOStats::toString)
                 * followed by an @c "aggregate" line.
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

                /** @brief Equality compares every per-stage record and the aggregate. */
                bool operator==(const MediaPipelineStats &other) const;
                bool operator!=(const MediaPipelineStats &other) const { return !(*this == other); }

        private:
                PerStageMap   _perStage;
                MediaIOStats  _aggregate;
                PipelineStats _pipeline;
};

/** @brief Writes a MediaPipelineStats snapshot to a DataStream. */
DataStream &operator<<(DataStream &stream, const MediaPipelineStats &s);

/** @brief Reads a MediaPipelineStats snapshot from a DataStream. */
DataStream &operator>>(DataStream &stream, MediaPipelineStats &s);

PROMEKI_NAMESPACE_END
