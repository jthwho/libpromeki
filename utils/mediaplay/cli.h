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
 * Stages are stored in their own fields rather than merged into a
 * generic list so main() can quickly answer "is there a source?"
 * and "is there a converter?" without walking the list.  Multiple
 * outputs are explicitly supported (fan-out).
 */
struct Options {
        // Stages
        StageSpec                       input;
        bool                            hasConverter = false;
        StageSpec                       converter;
        promeki::List<StageSpec>        outputs;

        // State used while parsing --ic / --oc / --cc — the parser
        // needs to know which stage a stray `--*c` or `--*m` attaches
        // to.
        enum StageScope {
                ScopeInput = 0,
                ScopeConverter,
                ScopeOutput
        };
        StageScope                      lastScope   = ScopeInput;
        size_t                          lastOutput  = 0;

        // Framework-level (non-stage) flags
        //
        // Pacing policy is determined implicitly by the sink set: if
        // an SDL output is present, it paces the pipeline at video
        // rate via its audio-led clock; if no SDL output is present,
        // the pipeline runs as fast as the file sinks can consume.
        // There's no --fast / --no-display — users drop the SDL
        // sink by passing an explicit --out instead.
        bool                            noAudio     = false;
        bool                            explicitIn  = false;    ///< User passed --in at least once.
        bool                            explicitOut = false;    ///< User passed --out at least once.
        promeki::String                 windowSize  = "1280x720";
        double                          duration    = 0.0;
        int64_t                         frameCount  = 0;
        bool                            verbose     = false;
        bool                            memStats    = false;    ///< Dump MemSpace::Stats for every registered memory space on shutdown.
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
