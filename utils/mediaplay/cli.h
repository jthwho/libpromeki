/**
 * @file      mediaplay/cli.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Top-level Options struct and command-line parser for mediaplay.
 * The parser is intentionally minimal: it collects raw Key:Value
 * strings verbatim and defers typed resolution to stage.cpp against
 * each backend's default config.
 */

#pragma once

#include <promeki/list.h>
#include <promeki/string.h>

#include "stage.h"

namespace mediaplay {

/**
 * @brief All options collected from the command line.
 *
 * `source` is the single pipeline source (`-s`); `sinks` is the
 * fan-out list of pipeline sinks (`-d`); `transforms` is the list
 * of intermediate stages (`-c NAME`), in pipeline order.  The
 * `-c` flag is repeatable so a pipeline can chain, e.g.,
 * @c "-c CSC -c VideoEncoder -c VideoDecoder".  Each subsequent
 * @c --cc / @c --cm attaches to the most recent @c -c.
 */
struct Options {
        StageSpec                       source;
        promeki::List<StageSpec>        transforms;   ///< Intermediate stages, in order.
        promeki::List<StageSpec>        sinks;

        // State used while parsing --sc / --dc / --cc / --cm — the
        // parser needs to know which stage a stray `--*c` or `--*m`
        // attaches to.  `lastTransform` / `lastSink` index into
        // `transforms` / `sinks` respectively.
        enum StageScope {
                ScopeSource = 0,
                ScopeTransform,
                ScopeSink
        };
        StageScope                      lastScope      = ScopeSource;
        size_t                          lastTransform  = 0;
        size_t                          lastSink       = 0;

        // Framework-level (non-stage) flags
        //
        // Pacing policy is determined implicitly by the sink set: if
        // an SDL sink is present, it paces the pipeline at video
        // rate via its audio-led clock; if no SDL sink is present,
        // the pipeline runs as fast as the file sinks can consume.
        // There's no --fast / --no-display — users drop the SDL
        // sink by passing an explicit --dst instead.
        bool                            noAudio     = false;
        bool                            explicitSrc = false;    ///< User passed --src at least once.
        bool                            explicitDst = false;    ///< User passed --dst at least once.
        promeki::String                 windowSize  = "1280x720";
        double                          duration    = 0.0;
        int64_t                         frameCount  = 0;
        bool                            verbose     = false;
        bool                            memStats    = false;    ///< Dump MemSpace::Stats for every registered memory space on shutdown.
        double                          statsInterval = 0.0;    ///< Seconds between live-telemetry prints (0 = off).
        bool                            probe       = false;    ///< Query and print device capabilities, then exit.

        /**
         * @brief When true (default), the pipeline config is run
         *        through @ref MediaPipelinePlanner before
         *        @ref MediaPipeline::build instantiates anything.
         *        Bridging stages (CSC, decoder, frame-rate sync,
         *        etc.) are spliced in automatically.  Disable with
         *        @c --no-autoplan to require a fully-resolved
         *        config — useful for regression scripts that want
         *        the planner out of the loop.
         */
        bool                            autoplan    = true;

        /**
         * @brief When true, run the planner against the input
         *        config, print the resolved config to stdout, and
         *        exit without touching the pipeline.  No frames flow.
         */
        bool                            planOnly    = false;

        /**
         * @brief When true, instantiate every stage declared in the
         *        config, call @ref MediaIO::describe on each,
         *        print the summary to stdout, and exit.  No frames
         *        flow.  Useful for "what does this source / sink
         *        look like?" introspection without standing up the
         *        whole pipeline.
         */
        bool                            describeOnly = false;

        /**
         * @brief When set, build the pipeline config from the CLI
         *        options, write it to this JSON path, and exit.  The
         *        pipeline itself is not opened or started.
         */
        promeki::String                 savePipelinePath;

        /**
         * @brief When set, load the pipeline config from this JSON
         *        path instead of building it from -s / -c / -d.  Any
         *        other stage-shaping flags on the CLI are ignored; the
         *        non-stage flags (@c --duration, @c --frame-count,
         *        @c --stats, ...) still apply.
         */
        promeki::String                 loadPipelinePath;

        /**
         * @brief When set, writes per-interval stats snapshots to
         *        this path as JSON-lines, plus a final aggregate
         *        snapshot at shutdown.  Setting this flag implicitly
         *        enables the stats collector (default interval: 1s).
         */
        promeki::String                 writeStatsPath;
};

/**
 * @brief Dumps a per-backend config schema to stdout.
 *
 * Walks the MediaIO registry, pulls each backend's defaultConfig,
 * and prints every key with its default value and Variant type.
 */
void printBackendConfigHelp();

/** @brief Prints usage to stderr followed by @c printBackendConfigHelp. */
void usage();

/**
 * @brief Parses @c argc/argv into @p opts.
 *
 * Returns false on parse failure.  A `--help` invocation prints
 * usage and exits the process without returning.
 */
bool parseOptions(int argc, char **argv, Options &opts);

} // namespace mediaplay
