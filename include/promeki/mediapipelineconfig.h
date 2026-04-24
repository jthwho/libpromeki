/**
 * @file      mediapipelineconfig.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/datastream.h>
#include <promeki/error.h>
#include <promeki/filepath.h>
#include <promeki/framecount.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediaio.h>
#include <promeki/metadata.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/stringlist.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Declarative description of a complete media pipeline.
 * @ingroup pipeline
 *
 * A @ref MediaPipelineConfig is the shareable data object that
 * @ref MediaPipeline consumes at @c build() time.  It lists the
 * @ref MediaIO instances (stages) to instantiate, the
 * @c frameReady → @c writeFrame edges between them (routes), and
 * pipeline-wide @ref Metadata.
 *
 * Two serialization paths round-trip the same in-memory shape:
 *  - @ref toJson / @ref fromJson — stable, human-editable form used
 *    by preset files and GUI editors.
 *  - @c operator<< / @c operator>> on @ref DataStream — compact
 *    binary form suitable for IPC and saved state.
 *
 * @ref validate performs both structural checks (route endpoints
 * exist, no cycles, no orphans) and per-stage @ref VariantSpec
 * checks against the backend's registered config specs.
 */
class MediaPipelineConfig {
        PROMEKI_SHARED_FINAL(MediaPipelineConfig)
        public:
                /** @brief Shared pointer alias. */
                using Ptr = SharedPtr<MediaPipelineConfig>;

                /** @brief List of MediaPipelineConfig values. */
                using List = promeki::List<MediaPipelineConfig>;

                /** @brief List of shared MediaPipelineConfig pointers. */
                using PtrList = promeki::List<Ptr>;

                /**
                 * @brief Declarative description of one @ref MediaIO stage.
                 *
                 * A stage is either a registered backend (identified by
                 * @ref type — e.g. @c "TPG", @c "CSC") or an opaque
                 * filesystem path (identified by @ref path with @ref type
                 * empty) that is resolved at build time via
                 * @ref MediaIO::createForFileRead /
                 * @ref MediaIO::createForFileWrite.  When both are set
                 * @ref type wins and @ref path is passed through to the
                 * backend as @ref MediaConfig::Filename.
                 */
                struct Stage {
                        /** @brief Unique stage identifier used by routes and error messages. */
                        String          name;
                        /** @brief Registered @ref MediaIO backend name, or empty for a file stage. */
                        String          type;
                        /** @brief Filesystem path for file-based stages. */
                        String          path;
                        /** @brief Direction: @ref MediaIO::Source, @c Sink, or @c Transform. */
                        MediaIO::Mode   mode = MediaIO::NotOpen;
                        /** @brief Per-stage configuration. */
                        MediaConfig     config;
                        /** @brief Per-stage metadata overrides (empty == accept backend defaults). */
                        Metadata        metadata;

                        /** @brief True if the two Stage records have identical contents. */
                        bool operator==(const Stage &other) const;
                        bool operator!=(const Stage &other) const { return !(*this == other); }
                };

                /**
                 * @brief One directed frame-flow edge between two stages.
                 *
                 * At @c start() time the pipeline connects the @ref from
                 * stage's @c frameReadySignal to a handler that drains
                 * its ready queue into the @ref to stage's
                 * @c writeFrame.  Fan-out is implicit — a source may
                 * appear as @ref from in multiple routes; each destination
                 * back-pressures independently.
                 *
                 * @ref fromTrack and @ref toTrack are reserved for future
                 * multi-track stages and ignored today.
                 */
                struct Route {
                        /** @brief Source stage name. */
                        String from;
                        /** @brief Sink stage name. */
                        String to;
                        /** @brief Reserved — future sub-stream selector on the source side. */
                        String fromTrack;
                        /** @brief Reserved — future sub-stream selector on the sink side. */
                        String toTrack;

                        bool operator==(const Route &other) const {
                                return from == other.from
                                    && to == other.to
                                    && fromTrack == other.fromTrack
                                    && toTrack == other.toTrack;
                        }
                        bool operator!=(const Route &other) const { return !(*this == other); }
                };

                /** @brief Ordered list of stages. */
                using StageList = promeki::List<Stage>;

                /** @brief Ordered list of routes. */
                using RouteList = promeki::List<Route>;

                /** @brief Constructs an empty pipeline config. */
                MediaPipelineConfig() = default;

                // ------------------------------------------------------------
                // Stages
                // ------------------------------------------------------------

                /** @brief Returns all stages (read-only). */
                const StageList &stages() const { return _stages; }

                /** @brief Returns all stages (mutable). */
                StageList &stages() { return _stages; }

                /** @brief Appends a stage. */
                void addStage(const Stage &s) { _stages.pushToBack(s); }

                /** @brief Appends a stage (move). */
                void addStage(Stage &&s) { _stages.pushToBack(std::move(s)); }

                /**
                 * @brief Looks up a stage by name.
                 * @param name The stage name.
                 * @return Pointer to the stage, or nullptr if no match.
                 */
                const Stage *findStage(const String &name) const;

                /** @brief Returns true if a stage with @p name exists. */
                bool hasStage(const String &name) const { return findStage(name) != nullptr; }

                /** @brief Returns every stage name, in declaration order. */
                StringList stageNames() const;

                // ------------------------------------------------------------
                // Routes
                // ------------------------------------------------------------

                /** @brief Returns all routes (read-only). */
                const RouteList &routes() const { return _routes; }

                /** @brief Returns all routes (mutable). */
                RouteList &routes() { return _routes; }

                /** @brief Appends a route. */
                void addRoute(const Route &r) { _routes.pushToBack(r); }

                /** @brief Appends a simple @p from → @p to route. */
                void addRoute(const String &from, const String &to);

                // ------------------------------------------------------------
                // Pipeline-wide metadata
                // ------------------------------------------------------------

                /** @brief Returns pipeline-wide metadata (read-only). */
                const Metadata &pipelineMetadata() const { return _pipelineMetadata; }

                /** @brief Returns pipeline-wide metadata (mutable). */
                Metadata &pipelineMetadata() { return _pipelineMetadata; }

                /** @brief Replaces the pipeline-wide metadata block. */
                void setPipelineMetadata(const Metadata &m) { _pipelineMetadata = m; }

                // ------------------------------------------------------------
                // Frame-count limit
                // ------------------------------------------------------------

                /**
                 * @brief Returns the pipeline-wide target frame count.
                 *
                 * A finite, non-empty @ref FrameCount tells @ref MediaPipeline
                 * to close each sink once it has received the requested
                 * number of frames; subsequent source reads are dropped
                 * on the floor.  Any non-finite value (the default
                 * @ref FrameCount::unknown, @ref FrameCount::empty, or
                 * @ref FrameCount::infinity) means "unlimited" — the
                 * pipeline runs until the sources naturally hit EOS.
                 *
                 * For interframe-coded streams (@ref VideoCodec::CodingTemporal)
                 * the cutoff respects GOP boundaries: once the target has
                 * been reached, the pipeline keeps writing until the next
                 * frame carries a @ref MediaPayload::Keyframe flag, then
                 * stops before writing that keyframe.  The sink therefore
                 * always receives a complete sequence of GOPs, even if
                 * that means overshooting the target by up to one GOP.
                 */
                const FrameCount &frameCount() const { return _frameCount; }

                /** @brief Sets the pipeline-wide target frame count. */
                void setFrameCount(const FrameCount &count) { _frameCount = count; }

                // ------------------------------------------------------------
                // Operations
                // ------------------------------------------------------------

                /**
                 * @brief Returns true when every route is already format-
                 *        compatible (no bridge insertion required).
                 *
                 * Thin wrapper around @ref MediaPipelinePlanner::isResolved.
                 * Useful as a pre-flight check before
                 * @ref MediaPipeline::build to decide whether the
                 * planner needs to run at all.
                 *
                 * @param diagnostic Optional output describing the
                 *                   first gapped route on a false return.
                 * @return @c true when no bridge is required.
                 */
                bool isResolved(String *diagnostic = nullptr) const;

                /**
                 * @brief Returns a fully-resolved copy of this config.
                 *
                 * Thin wrapper around @ref MediaPipelinePlanner::plan
                 * — splices in the bridging stages required to make
                 * every route directly format-compatible.  See
                 * @ref MediaPipelinePlanner for the algorithm and
                 * the @c Policy semantics.
                 *
                 * @param err Optional error output (set to
                 *            @ref Error::NotSupported when no bridge
                 *            chain exists for some route).
                 * @param diagnostic Optional human-readable error detail
                 *                   on failure.
                 * @return The resolved config on success, or an empty
                 *         config when planning failed.
                 */
                MediaPipelineConfig resolved(Error *err = nullptr,
                                             String *diagnostic = nullptr) const;

                /**
                 * @brief Validates the config for use by @ref MediaPipeline::build.
                 *
                 * Checks performed:
                 *  - Every stage has a non-empty @ref Stage::name and the
                 *    names are unique.
                 *  - Every stage has a non-empty @ref Stage::type or
                 *    @ref Stage::path.
                 *  - Every stage has a valid @ref Stage::mode.
                 *  - Every route's @c from and @c to reference existing
                 *    stage names.
                 *  - The route graph is acyclic.
                 *  - Every stage participates in at least one route
                 *    (except single-stage configs).
                 *  - Each stage's @ref MediaConfig keys are recognized
                 *    (via @ref MediaIO::unknownConfigKeys) and each set
                 *    value passes its registered @ref VariantSpec.
                 *
                 * Findings are logged at warning level; the return value
                 * is @c Error::Invalid or @c Error::InvalidArgument on
                 * the first structural problem encountered, or
                 * @c Error::Ok if the config is well-formed.
                 *
                 * @return @c Error::Ok when the config is valid.
                 */
                Error validate() const;

                /**
                 * @brief Produces a human-readable multi-line description.
                 *
                 * Returns a @ref StringList suitable for logging or GUI
                 * display.  Each stage is rendered with its name, type,
                 * mode, and set config keys; each route is rendered as
                 * @c "from → to"; pipeline metadata appears at the top
                 * when present.
                 *
                 * @return The description lines.
                 */
                StringList describe() const;

                /**
                 * @brief Serializes the config to a @ref JsonObject.
                 *
                 * The returned object has the shape documented in
                 * @c docs/mediapipeline.dox: @c metadata, @c stages
                 * array of @c { name, type, path, mode, config,
                 * metadata } objects, and @c routes array of
                 * @c { from, to, fromTrack, toTrack } objects.
                 * @ref MediaConfig and @ref Metadata nested blocks
                 * round-trip via their inherited
                 * @ref VariantDatabase::toJson.
                 */
                JsonObject toJson() const;

                /**
                 * @brief Reconstructs a config from a @ref JsonObject.
                 * @param obj The JSON object produced by @ref toJson.
                 * @param err Optional error output — set to
                 *            @c Error::Invalid on malformed input.
                 * @return The reconstructed config.  On malformed input
                 *         the returned config is empty.
                 */
                static MediaPipelineConfig fromJson(const JsonObject &obj, Error *err = nullptr);

                /**
                 * @brief Writes the config to a JSON file.
                 * @param path  Target file path.
                 * @param indent Indent in spaces (2 by default for readability).
                 * @return @c Error::Ok on success, or a filesystem error.
                 */
                Error saveToFile(const FilePath &path, unsigned int indent = 2) const;

                /**
                 * @brief Reads a config from a JSON file.
                 * @param path The file path to read.
                 * @param err  Optional error output.
                 * @return The parsed config, or an empty one on failure.
                 */
                static MediaPipelineConfig loadFromFile(const FilePath &path, Error *err = nullptr);

                /** @brief True if both configs have identical contents. */
                bool operator==(const MediaPipelineConfig &other) const;
                bool operator!=(const MediaPipelineConfig &other) const { return !(*this == other); }

                /**
                 * @brief Renders a @ref MediaIO::Mode as a stable name.
                 *
                 * Used by JSON round-trip.  Unknown values produce
                 * @c "NotOpen".
                 */
                static String modeName(MediaIO::Mode mode);

                /**
                 * @brief Parses a mode name produced by @ref modeName.
                 * @param name The mode string.
                 * @param err  Optional error — @c Error::Invalid on unknown name.
                 * @return The parsed mode.
                 */
                static MediaIO::Mode modeFromName(const String &name, Error *err = nullptr);

        private:
                StageList       _stages;
                RouteList       _routes;
                Metadata        _pipelineMetadata;
                FrameCount      _frameCount;
};

// ============================================================================
// DataStream serialization
// ============================================================================
//
// Stage and Route are simple value types; their operators are declared
// here and defined in mediapipelineconfig.cpp alongside the rest of the
// serialization code.

/** @brief Writes a Stage to a DataStream. */
DataStream &operator<<(DataStream &stream, const MediaPipelineConfig::Stage &s);

/** @brief Reads a Stage from a DataStream. */
DataStream &operator>>(DataStream &stream, MediaPipelineConfig::Stage &s);

/** @brief Writes a Route to a DataStream. */
DataStream &operator<<(DataStream &stream, const MediaPipelineConfig::Route &r);

/** @brief Reads a Route from a DataStream. */
DataStream &operator>>(DataStream &stream, MediaPipelineConfig::Route &r);

/** @brief Writes a MediaPipelineConfig to a DataStream. */
DataStream &operator<<(DataStream &stream, const MediaPipelineConfig &c);

/** @brief Reads a MediaPipelineConfig from a DataStream. */
DataStream &operator>>(DataStream &stream, MediaPipelineConfig &c);

PROMEKI_NAMESPACE_END
