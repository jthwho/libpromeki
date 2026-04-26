/**
 * @file      pipelinesettings.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/json.h>
#include <promeki/mediapipelineplanner.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

namespace promekipipeline {

        // Pull RefCount into our namespace so the PROMEKI_SHARED_FINAL macro
        // (which references the unqualified type name) compiles when used on
        // classes that live outside the promeki namespace.
        using promeki::RefCount;

        /**
 * @brief Per-pipeline demo settings (display name, planner policy,
 *        stats cadence) distinct from the @c MediaPipelineConfig graph.
 *
 * @c MediaPipelineConfig describes the graph (stages and routes); a
 * @c PipelineSettings sits beside it and captures the demo-specific
 * knobs that the @c PipelineManager applies when it builds, opens,
 * and observes the corresponding @c MediaPipeline.  This class is a
 * plain Shareable value (no @c ObjectBase, follows the data-object
 * policy with @c PROMEKI_SHARED_FINAL) so the manager can hand
 * snapshots out by value or share them via @c Ptr without juggling
 * lifetimes.
 *
 * The settings round-trip through JSON so the future Phase D
 * @c PUT @c /api/pipelines/{id}/settings endpoint can replace the
 * full block in a single request.
 */
        class PipelineSettings {
                        PROMEKI_SHARED_FINAL(PipelineSettings)
                public:
                        /** @brief Default display name for new pipelines. */
                        static const promeki::String DefaultName;

                        /** @brief Default stats tick interval (1 second). */
                        static const promeki::Duration DefaultStatsInterval;

                        /** @brief Default planner quality bias. */
                        static constexpr promeki::MediaPipelinePlanner::Quality DefaultQuality =
                                promeki::MediaPipelinePlanner::Quality::Highest;

                        /** @brief Default planner bridge-depth ceiling. */
                        static constexpr int DefaultMaxBridgeDepth = 4;

                        /** @brief Default autoplan setting (planner runs at build()). */
                        static constexpr bool DefaultAutoplan = true;

                        /** @brief Shared pointer alias. */
                        using Ptr = promeki::SharedPtr<PipelineSettings>;

                        /** @brief List alias for plain-value lists. */
                        using List = promeki::List<PipelineSettings>;

                        /** @brief List alias for shared-pointer lists. */
                        using PtrList = promeki::List<Ptr>;

                        /** @brief Default-constructs settings with the documented defaults. */
                        PipelineSettings();

                        /** @brief Returns the user-visible display name. */
                        const promeki::String &name() const { return _name; }

                        /** @brief Replaces the user-visible display name. */
                        void setName(const promeki::String &n) { _name = n; }

                        /**
                 * @brief Returns the periodic stats tick interval.
                 *
                 * A zero @c Duration disables the stats tick (and the
                 * matching @c PipelineEvent::Kind::StatsUpdated stream).
                 */
                        const promeki::Duration &statsInterval() const { return _statsInterval; }

                        /** @brief Replaces the stats tick interval. */
                        void setStatsInterval(const promeki::Duration &d) { _statsInterval = d; }

                        /** @brief Returns the planner quality bias. */
                        promeki::MediaPipelinePlanner::Quality quality() const { return _quality; }

                        /** @brief Replaces the planner quality bias. */
                        void setQuality(promeki::MediaPipelinePlanner::Quality q) { _quality = q; }

                        /** @brief Returns the planner bridge-depth ceiling. */
                        int maxBridgeDepth() const { return _maxBridgeDepth; }

                        /** @brief Replaces the planner bridge-depth ceiling. */
                        void setMaxBridgeDepth(int d) { _maxBridgeDepth = d; }

                        /** @brief Returns backend type names the planner must not use as bridges. */
                        const promeki::StringList &excludedBridges() const { return _excludedBridges; }

                        /** @brief Replaces the planner's excluded-bridge list. */
                        void setExcludedBridges(const promeki::StringList &list) { _excludedBridges = list; }

                        /** @brief Returns whether @c PipelineManager::build runs the planner. */
                        bool autoplan() const { return _autoplan; }

                        /** @brief Replaces the autoplan flag. */
                        void setAutoplan(bool yes) { _autoplan = yes; }

                        /**
                 * @brief Materializes the planner @c Policy described by this
                 *        settings block.
                 *
                 * Convenience for @c PipelineManager::build so callers do not
                 * have to copy fields one at a time.
                 */
                        promeki::MediaPipelinePlanner::Policy plannerPolicy() const;

                        /**
                 * @brief Serializes the settings to a stable JSON shape.
                 *
                 * The shape is @code
                 * {
                 *   "name": "Untitled",
                 *   "statsIntervalMs": 1000,
                 *   "quality": "Highest",
                 *   "maxBridgeDepth": 4,
                 *   "excludedBridges": [...],
                 *   "autoplan": true
                 * }
                 * @endcode
                 */
                        promeki::JsonObject toJson() const;

                        /**
                 * @brief Reconstructs a settings block from a JSON object.
                 *
                 * Missing keys take their default values; unknown keys are
                 * ignored.  Returns @c Error::Invalid via @p err when
                 * @c quality holds an unrecognised value.
                 *
                 * @param obj The JSON object as produced by @ref toJson.
                 * @param err Optional error output.
                 * @return The decoded settings.
                 */
                        static PipelineSettings fromJson(const promeki::JsonObject &obj, promeki::Error *err = nullptr);

                        /**
                 * @brief Returns the canonical name of @p q.
                 *
                 * Round-trips through @ref qualityFromString.  Unknown
                 * inputs yield @c "Highest".
                 */
                        static promeki::String qualityToString(promeki::MediaPipelinePlanner::Quality q);

                        /**
                 * @brief Parses a string previously produced by @ref qualityToString.
                 * @param s  The string to parse.
                 * @param ok Optional flag set to @c true when @p s named a
                 *           valid quality, @c false otherwise.
                 * @return The decoded quality, or @c Quality::Highest on
                 *         unknown input.
                 */
                        static promeki::MediaPipelinePlanner::Quality qualityFromString(const promeki::String &s,
                                                                                        bool *ok = nullptr);

                private:
                        promeki::String                        _name;
                        promeki::Duration                      _statsInterval;
                        promeki::MediaPipelinePlanner::Quality _quality;
                        int                                    _maxBridgeDepth;
                        promeki::StringList                    _excludedBridges;
                        bool                                   _autoplan;
        };

} // namespace promekipipeline
