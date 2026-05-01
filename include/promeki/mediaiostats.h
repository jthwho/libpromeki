/**
 * @file      mediaiostats.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/framecount.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>
#include <promeki/variantdatabase.h>
#include <promeki/windowedstat.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Telemetry container shared by per-command and instance-wide reporting.
 * @ingroup mediaio_user
 *
 * Single @ref VariantDatabase type used in two contexts:
 *
 *  - As @ref MediaIOCommand::stats on every dispatched command, where
 *    backends and the framework populate the per-command keys
 *    (@ref ExecuteDuration, @ref QueueWaitDuration,
 *    @ref BytesProcessed, plus any backend-specific keys the command
 *    chose to surface).
 *  - As the instance-wide aggregate populated when a
 *    @c MediaIOCommandStats command resolves: backends fill in
 *    cumulative counters (@ref FramesDropped, @ref BytesPerSecond,
 *    etc.) into the @em same container, so a single accessor
 *    (@ref MediaIORequest::stats) covers both cases without forcing
 *    callers to think about which kind of report they are reading.
 *
 * Standard keys are declared as static members; backends are free to
 * add their own backend-specific keys for data not covered by the
 * standard set.  Has its own @ref StringRegistry so stats keys do
 * not collide with config or param keys.
 */
class MediaIOStats : public VariantDatabase<"MediaIOStats"> {
        public:
                using Base = VariantDatabase<"MediaIOStats">;
                using Base::Base;

                // ---- Per-command keys (populated by framework + backend on
                //      every command's MediaIOCommand::stats) ----

                /// @brief Duration — wall time the worker spent in @c executeCmd().
                PROMEKI_DECLARE_ID(ExecuteDuration,
                                   VariantSpec()
                                           .setType(Variant::TypeDuration)
                                           .setDefault(Duration())
                                           .setDescription("Wall time spent executing the command."));
                /// @brief Duration — wall time the command waited in the strand queue.
                PROMEKI_DECLARE_ID(QueueWaitDuration,
                                   VariantSpec()
                                           .setType(Variant::TypeDuration)
                                           .setDefault(Duration())
                                           .setDescription("Wall time spent in the strand queue."));
                /// @brief int64_t — payload bytes produced / consumed by this command.
                PROMEKI_DECLARE_ID(BytesProcessed,
                                   VariantSpec()
                                           .setType(Variant::TypeS64)
                                           .setDefault(int64_t(0))
                                           .setMin(int64_t(0))
                                           .setDescription("Payload bytes produced or consumed."));

                // ---- Instance-wide cumulative keys (populated when a
                //      MediaIOCommandStats command resolves) ----

                /// @brief FrameCount — total frames dropped since open.
                PROMEKI_DECLARE_ID(FramesDropped, VariantSpec()
                                                          .setType(Variant::TypeFrameCount)
                                                          .setDefault(FrameCount(0))
                                                          .setDescription("Total frames dropped since open."));
                /// @brief FrameCount — total frames repeated due to underrun.
                PROMEKI_DECLARE_ID(FramesRepeated, VariantSpec()
                                                           .setType(Variant::TypeFrameCount)
                                                           .setDefault(FrameCount(0))
                                                           .setDescription("Total frames repeated due to underrun."));
                /// @brief FrameCount — total frames that arrived late.
                PROMEKI_DECLARE_ID(FramesLate, VariantSpec()
                                                       .setType(Variant::TypeFrameCount)
                                                       .setDefault(FrameCount(0))
                                                       .setDescription("Total frames that arrived late."));
                /// @brief int64_t — current depth of internal buffer.
                PROMEKI_DECLARE_ID(QueueDepth, VariantSpec()
                                                       .setType(Variant::TypeS64)
                                                       .setDefault(int64_t(0))
                                                       .setMin(int64_t(0))
                                                       .setDescription("Current depth of internal buffer."));
                /// @brief int64_t — capacity of internal buffer.
                PROMEKI_DECLARE_ID(QueueCapacity, VariantSpec()
                                                          .setType(Variant::TypeS64)
                                                          .setDefault(int64_t(0))
                                                          .setMin(int64_t(0))
                                                          .setDescription("Capacity of internal buffer."));
                /// @brief double — current data rate.
                PROMEKI_DECLARE_ID(BytesPerSecond, VariantSpec()
                                                           .setType(Variant::TypeDouble)
                                                           .setDefault(0.0)
                                                           .setMin(0.0)
                                                           .setDescription("Current data rate in bytes per second."));
                /// @brief double — current frame rate (frames per second).
                PROMEKI_DECLARE_ID(FramesPerSecond,
                                   VariantSpec()
                                           .setType(Variant::TypeDouble)
                                           .setDefault(0.0)
                                           .setMin(0.0)
                                           .setDescription("Current frame rate in frames per second."));
                /// @brief double — average end-to-end latency.
                PROMEKI_DECLARE_ID(AverageLatencyMs, VariantSpec()
                                                             .setType(Variant::TypeDouble)
                                                             .setDefault(0.0)
                                                             .setMin(0.0)
                                                             .setDescription("Average end-to-end latency in ms."));
                /// @brief double — peak observed latency.
                PROMEKI_DECLARE_ID(PeakLatencyMs, VariantSpec()
                                                          .setType(Variant::TypeDouble)
                                                          .setDefault(0.0)
                                                          .setMin(0.0)
                                                          .setDescription("Peak observed latency in ms."));
                /// @brief double — average per-frame processing time.
                PROMEKI_DECLARE_ID(AverageProcessingMs,
                                   VariantSpec()
                                           .setType(Variant::TypeDouble)
                                           .setDefault(0.0)
                                           .setMin(0.0)
                                           .setDescription("Average per-frame processing time in ms."));
                /// @brief double — peak per-frame processing time.
                PROMEKI_DECLARE_ID(PeakProcessingMs, VariantSpec()
                                                             .setType(Variant::TypeDouble)
                                                             .setDefault(0.0)
                                                             .setMin(0.0)
                                                             .setDescription("Peak per-frame processing time in ms."));
                /// @brief String — most recent error description.
                PROMEKI_DECLARE_ID(LastErrorMessage, VariantSpec()
                                                             .setType(Variant::TypeString)
                                                             .setDefault(String())
                                                             .setDescription("Most recent error description."));
                /// @brief int64_t — number of commands queued on the strand but not yet running.
                PROMEKI_DECLARE_ID(PendingOperations,
                                   VariantSpec()
                                           .setType(Variant::TypeS64)
                                           .setDefault(int64_t(0))
                                           .setMin(int64_t(0))
                                           .setDescription("Commands queued on the strand but not yet running."));

                /**
                 * @brief Spec-aware @ref VariantDatabase::setFromJson with WindowedStat detection.
                 *
                 * Per-command @ref WindowedStat entries
                 * (@c ReadExecuteDuration, @c WriteBytesProcessed,
                 * etc.) are dynamic — there is no spec registered for
                 * them, so the inherited @ref VariantDatabase::setFromJson
                 * cannot coerce the JSON-encoded string form back to
                 * its native @ref Variant type.  This shadow override
                 * recognises the canonical @c "cap=N:[...]"
                 * @ref WindowedStat form and reconstructs the typed
                 * value before storing it.  Every other key falls
                 * through to the base behaviour, including spec-driven
                 * coercion for the standard cumulative-aggregate keys.
                 */
                Error setFromJson(ID id, const Variant &val) {
                        if (val.type() == Variant::TypeString) {
                                const String s = val.get<String>();
                                if (s.startsWith("cap=")) {
                                        auto r = WindowedStat::fromString(s);
                                        if (error(r).isOk()) {
                                                Error err;
                                                set(id, Variant(value(r)), &err);
                                                return err;
                                        }
                                }
                        }
                        return Base::setFromJson(id, val);
                }

                /**
                 * @brief Renders the standard telemetry keys as a compact log line.
                 *
                 * Formats whichever standard keys are present into a
                 * single space-separated line suitable for dumping to
                 * a terminal or logger.  Keys are emitted in a fixed
                 * order so periodic telemetry output stays scannable.
                 * Cheap counters that are still zero (and the empty
                 * @c LastErrorMessage) are elided so the line stays
                 * quiet under normal operation.
                 *
                 * @return A single-line summary, or an empty String
                 *         if the container holds no standard keys.
                 */
                String toString() const;
};

PROMEKI_NAMESPACE_END
