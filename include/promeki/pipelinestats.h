/**
 * @file      pipelinestats.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantspec.h>
#include <promeki/framecount.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Telemetry that describes the pipeline layer itself.
 * @ingroup pipeline
 *
 * Per-stage statistics live in @ref MediaIOStats.  The roll-up
 * @em across those stages lives in @ref MediaPipelineStats::aggregate.
 * @ref PipelineStats is the third bucket — counters that describe the
 * drain loop that wires the stages together: how many frames the
 * pipeline fanned out, how many times a non-blocking write had to
 * stash a frame for retry, how many error events were surfaced, and
 * how many edges are currently paused on back-pressure.
 *
 * The class uses the same @ref VariantDatabase machinery as
 * @ref MediaIOStats so every counter round-trips through JSON,
 * DataStream, and TextStream for free, and new counters can be added
 * later without re-plumbing serialization.
 */
class PipelineStats : public VariantDatabase<"PipelineStats"> {
        public:
                /** @brief Base class alias. */
                using Base = VariantDatabase<"PipelineStats">;

                using Base::Base;

                /**
                 * @brief FrameCount — total frames pulled from sources and
                 *        fanned out to their outgoing edges since the
                 *        pipeline started draining.
                 */
                PROMEKI_DECLARE_ID(FramesProduced,
                        VariantSpec().setType(Variant::TypeFrameCount).setDefault(FrameCount(0))
                                .setDescription(
                                        "Total frames dispatched from sources to sinks."));

                /**
                 * @brief int64_t — total times a non-blocking
                 *        @ref MediaIO::writeFrame refused with
                 *        @c Error::TryAgain and the pipeline stashed
                 *        the frame for retry on the next drain.
                 */
                PROMEKI_DECLARE_ID(WriteRetries,
                        VariantSpec().setType(Variant::TypeS64).setDefault(int64_t(0))
                                .setMin(int64_t(0))
                                .setDescription(
                                        "Non-blocking writeFrame TryAgain events held for retry."));

                /**
                 * @brief int64_t — count of @c pipelineErrorSignal
                 *        emissions since the pipeline started.
                 */
                PROMEKI_DECLARE_ID(PipelineErrors,
                        VariantSpec().setType(Variant::TypeS64).setDefault(int64_t(0))
                                .setMin(int64_t(0))
                                .setDescription(
                                        "Number of errors surfaced via pipelineError."));

                /**
                 * @brief int64_t — current number of source stages that
                 *        have reported @c Error::EndOfFile.
                 */
                PROMEKI_DECLARE_ID(SourcesAtEof,
                        VariantSpec().setType(Variant::TypeS64).setDefault(int64_t(0))
                                .setMin(int64_t(0))
                                .setDescription(
                                        "Number of sources that have reached EOF."));

                /**
                 * @brief int64_t — current number of outgoing edges
                 *        that are holding a frame waiting for their
                 *        consumer to free capacity.
                 */
                PROMEKI_DECLARE_ID(PausedEdges,
                        VariantSpec().setType(Variant::TypeS64).setDefault(int64_t(0))
                                .setMin(int64_t(0))
                                .setDescription(
                                        "Number of edges currently paused on back-pressure."));

                /**
                 * @brief String — human-readable pipeline lifecycle
                 *        state (@c "Empty", @c "Built", @c "Open",
                 *        @c "Running", @c "Stopped", @c "Closed").
                 */
                PROMEKI_DECLARE_ID(State,
                        VariantSpec().setType(Variant::TypeString).setDefault(String())
                                .setDescription("Pipeline lifecycle state."));

                /**
                 * @brief int64_t — wall-clock uptime in milliseconds
                 *        since @ref MediaPipeline::start.
                 */
                PROMEKI_DECLARE_ID(UptimeMs,
                        VariantSpec().setType(Variant::TypeS64).setDefault(int64_t(0))
                                .setMin(int64_t(0))
                                .setDescription(
                                        "Wall-clock uptime since start, in milliseconds."));

                /**
                 * @brief Renders the pipeline-layer counters as a
                 *        compact single-line summary.
                 *
                 * Counters that are still at their default (zero /
                 * empty string) are elided so the line stays quiet
                 * under normal operation — mirroring the convention
                 * used by @ref MediaIOStats::toString.
                 */
                String toString() const;
};

PROMEKI_NAMESPACE_END
