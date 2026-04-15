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
 * fan-out list of pipeline sinks (`-d`); `converters` is the list
 * of intermediate stages (`-c NAME`), in pipeline order.  The
 * `-c` flag is repeatable so a pipeline can chain, e.g.,
 * @c "-c Converter -c VideoEncoder -c VideoDecoder".  Each
 * subsequent @c --cc / @c --cm attaches to the most recent @c -c.
 */
struct Options {
        StageSpec                       source;
        promeki::List<StageSpec>        converters;   ///< Intermediate stages, in order.
        promeki::List<StageSpec>        sinks;

        // State used while parsing --sc / --dc / --cc / --cm — the
        // parser needs to know which stage a stray `--*c` or `--*m`
        // attaches to.  `lastConverter` / `lastSink` index into
        // `converters` / `sinks` respectively.
        enum StageScope {
                ScopeSource = 0,
                ScopeConverter,
                ScopeSink
        };
        StageScope                      lastScope      = ScopeSource;
        size_t                          lastConverter  = 0;
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
