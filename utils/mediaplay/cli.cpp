/**
 * @file      mediaplay/cli.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "cli.h"

#include <cstdio>
#include <cstdlib>

#include <promeki/buffer.h>
#include <promeki/cmdlineparser.h>
#include <promeki/error.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiofactory.h>
#include <promeki/metadata.h>
#include <promeki/textstream.h>
#include <promeki/variant.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantspec.h>

using namespace promeki;

namespace mediaplay {

        void printBackendConfigHelp() {
                TextStream ts(stdout);
                ts << endl
                   << "Config per backend (set via --sc / --dc / --cc Key:Value," << endl
                   << "  pass Key:list for enum values, Key:help for key details):" << endl;

                // "Type" is implied by -i / -o / -c so it has no user-visible
                // role in the per-backend help — hide it from every backend.
                StringList hidden;
                hidden.pushToBack(String("Type"));

                auto dumpBackend = [&](const String &name, const String &caps, const String &desc,
                                       const MediaIO::Config::SpecMap &specs) {
                        // Emit the header line, then the spec block bracketed
                        // by a dashed border that matches the widest physical
                        // line the block printed.  Two backends can produce
                        // very different widths — TPG is wide, AudioFile is
                        // narrow — so the border is computed per block rather
                        // than using a fixed column.
                        String header = String("  ") + name + " [" + caps + "] — " + desc;
                        ts << endl << header << endl;
                        if (specs.isEmpty()) {
                                ts << "    (no config keys)" << endl;
                                return;
                        }

                        // Round-trip through a scratch buffer so we know the
                        // actual block width before we write the top border.
                        // The alternative — writing two passes directly to the
                        // stream — would put the top border below the block
                        // or force us to guess the width ahead of time.
                        Buffer     scratch;
                        TextStream body(&scratch);
                        int        bodyWidth = MediaIO::Config::writeSpecMapHelp(body, specs, hidden);
                        body.flush();

                        int borderWidth = bodyWidth;
                        int headerWidth = static_cast<int>(header.size());
                        if (headerWidth > borderWidth) borderWidth = headerWidth;
                        String border;
                        for (int i = 0; i < borderWidth; ++i) border += '-';
                        ts << border << endl;
                        ts << String(static_cast<const char *>(scratch.data()), scratch.size());
                        ts << border << endl;
                };

                // Badge letters match the MediaIO's own perspective: a
                // source (e.g. TPG) provides frames so it carries `[O]` for
                // Output; a sink (e.g. a file writer) accepts frames so it
                // carries `[I]` for Input.  The mediaplay CLI is named from
                // the pipeline's perspective — `-s` takes a source backend
                // (`[O]`) and `-d` takes a destination sink (`[I]`) — so the
                // badge mirrors the backend's own role, not the CLI slot it
                // plugs into.
                //
                // Order is Input-first so a backend that supports both (but
                // not simultaneously) reads as `IO` — the same string a pure
                // transform (canBeTransform only) displays, so the badge
                // stays consistent regardless of how the backend decomposes
                // its modes.
                for (const MediaIOFactory *desc : MediaIOFactory::registeredFactories()) {
                        if (desc == nullptr) continue;
                        String caps;
                        if (desc->canBeSink()) caps += "I";
                        if (desc->canBeSource()) caps += "O";
                        if (desc->canBeTransform() && caps.isEmpty()) caps = "IO";
                        dumpBackend(desc->name(), caps, desc->description(), desc->configSpecs());
                }

                // SDL is a mediaplay-local pseudo-backend — strictly a sink.
                dumpBackend(String(kStageSdl), String("I"), String(sdlDescription()), sdlConfigSpecs());
        }

        void usage() {
                fprintf(stderr, "Usage: mediaplay [OPTIONS]\n"
                                "\n"
                                "Pumps media frames from one MediaIO source, through zero or more\n"
                                "intermediate transform stages (CSC, SRC, VideoEncoder, etc.), out\n"
                                "to one or more MediaIO sinks.  Every stage is configured via\n"
                                "generic Key:Value options whose values are parsed against the\n"
                                "backend's default config.  Pacing is implicit: if an SDL\n"
                                "destination is present it drives the pipeline at video rate\n"
                                "(audio-led clock), otherwise frames flow as fast as the file\n"
                                "sinks can consume them.\n"
                                "\n"
                                "Backend badges in the schema below show which direction each\n"
                                "backend supports:\n"
                                "  [O]  = Output — backend provides frames; use with -s\n"
                                "  [I]  = Input  — backend accepts frames; use with -d\n"
                                "  [IO] = both\n"
                                "\n"
                                "Stages:\n"
                                "  -s, --src <NAME|list>     Source backend name (default: TPG).\n"
                                "  --sc <K:V>                Set one source stage config key.\n"
                                "                            Repeatable.  Passing `K:list`\n"
                                "                            lists valid values for Enum,\n"
                                "                            EnumList, and PixelFormat keys.\n"
                                "                            Passing `K:help` shows the key's\n"
                                "                            type, range, and description.\n"
                                "  --sm <K:V>                Set one source stage metadata key\n"
                                "                            (Title, Artist, Copyright, ...).\n"
                                "                            Repeatable.\n"
                                "  -c, --convert <NAME>      Insert an intermediate MediaIO\n"
                                "                            stage (backend name, e.g.\n"
                                "                            CSC / VideoEncoder /\n"
                                "                            VideoDecoder / FrameSync /\n"
                                "                            SRC).  Repeatable — stages\n"
                                "                            are chained in the order\n"
                                "                            given.\n"
                                "  --cc <K:V>                Set one config key on the\n"
                                "                            most recently declared -c.\n"
                                "                            Repeatable.\n"
                                "  --cm <K:V>                Set one metadata key on the\n"
                                "                            most recently declared -c.\n"
                                "                            Repeatable.\n"
                                "  -d, --dst <NAME|PATH|list>\n"
                                "                            Add a destination stage — a backend\n"
                                "                            name (SDL, QuickTime, ImageFile,\n"
                                "                            ...) or a file path auto-detected\n"
                                "                            via MediaIO::createForFileWrite.\n"
                                "                            Default destination is SDL.\n"
                                "                            Repeatable (fan-out).\n"
                                "  --dc <K:V>                Set one config key on the most\n"
                                "                            recently declared -d.  Repeatable.\n"
                                "  --dm <K:V>                Set one metadata key on the most\n"
                                "                            recently declared -d.  Overlays\n"
                                "                            metadata inherited from upstream.\n"
                                "                            Repeatable.\n"
                                "\n"
                                "Playback control:\n"
                                "  --duration <SEC>          Wall-clock runtime limit for\n"
                                "                            the mediaplay process.  This\n"
                                "                            is NOT a recording length — an\n"
                                "                            unpaced file-only pipeline can\n"
                                "                            easily write thousands of\n"
                                "                            frames per second, so a 10s\n"
                                "                            --duration may produce many\n"
                                "                            more frames of content than\n"
                                "                            you expect.  Use --frame-count\n"
                                "                            when you want a specific\n"
                                "                            number of output frames.\n"
                                "  --frame-count <N>         Require the pipeline to deliver\n"
                                "                            exactly N frames to each sink\n"
                                "                            (0 = unlimited).  Runs through\n"
                                "                            MediaPipelineConfig::frameCount\n"
                                "                            — once a sink has received N\n"
                                "                            frames at the next safe cut\n"
                                "                            point (keyframe boundary for\n"
                                "                            interframe codecs) the pipeline\n"
                                "                            closes that sink and drops\n"
                                "                            further source output on the\n"
                                "                            floor.  Interframe streams may\n"
                                "                            overshoot by up to one GOP so\n"
                                "                            the sink ends on a complete\n"
                                "                            sequence of GOPs.\n"
                                "  --verbose                 Print periodic progress stats.\n"
                                "  --stats                   Enable live MediaIO telemetry.\n"
                                "                            Prints a BytesPerSecond /\n"
                                "                            FramesPerSecond / FramesDropped /\n"
                                "                            QueueDepth summary for every stage\n"
                                "                            (source, transform, sinks) once per\n"
                                "                            second by default.\n"
                                "  --stats-interval <SEC>    Override the --stats print interval\n"
                                "                            in seconds (default 1.0).\n"
                                "  --memstats                Print MemSpace allocation\n"
                                "                            statistics for every registered\n"
                                "                            memory space on shutdown.\n"
                                "\n"
                                "Pipeline preset I/O:\n"
                                "  --save-pipeline <PATH>    Build the pipeline from the other flags,\n"
                                "                            write it to PATH as a JSON preset, and\n"
                                "                            exit without opening stages.  Pair with\n"
                                "                            --pipeline <PATH> to re-run the same\n"
                                "                            pipeline later.\n"
                                "  --pipeline <PATH>         Load the pipeline from PATH (previously\n"
                                "                            written by --save-pipeline) instead of\n"
                                "                            building it from -s / -c / -d.  Other\n"
                                "                            non-stage flags (--duration, --stats,\n"
                                "                            --frame-count) still apply.\n"
                                "  --write-stats <PATH>      Append a JSON-lines snapshot of the\n"
                                "                            pipeline's stats to PATH once per\n"
                                "                            --stats-interval, plus a final\n"
                                "                            aggregate snapshot at shutdown.\n"
                                "                            Implicitly turns on the stats\n"
                                "                            collector (default 1s interval).\n"
                                "\n"
                                "Planner:\n"
                                "  --no-autoplan             Disable automatic bridge insertion\n"
                                "                            (CSC, decoder, FrameSync, SRC, encoder)\n"
                                "                            before pipeline build.  Default behaviour\n"
                                "                            runs MediaPipelinePlanner; this flag forces\n"
                                "                            a strict, fully-resolved input config.\n"
                                "  --plan                    Run the planner against the resolved CLI\n"
                                "                            config, print the resulting stages and\n"
                                "                            routes to stdout, and exit without opening\n"
                                "                            anything.\n"
                                "  --describe                Instantiate every stage, call\n"
                                "                            MediaIO::describe on each, dump the\n"
                                "                            summary, and exit.\n"
                                "\n"
                                "Inspector-driven pass/fail:\n"
                                "  Every Inspector sink is injected automatically so mediaplay\n"
                                "  can poll its snapshot at shutdown.  Any discontinuity reported\n"
                                "  by Inspector surfaces as exit code 21 below.\n"
                                "\n"
                                "Codec enumeration:\n"
                                "  --list-codecs [video|audio|all]\n"
                                "                            Print a tab-separated list of codec /\n"
                                "                            backend pairs, one per line, and exit.\n"
                                "                            Columns: kind, codec, backend, enc, dec\n"
                                "                            (yes / no).  Codecs with no registered\n"
                                "                            backend are omitted entirely.  Default\n"
                                "                            argument is 'all'.  Intended for scripts\n"
                                "                            (see scripts/roundtrip-codecs.sh).\n"
                                "\n"
                                "Exit codes (applicable to all runs):\n"
                                "   0  Success.\n"
                                "   1  Generic failure (CLI, argparse, setup).\n"
                                "  10  Pipeline build failed (planner could not resolve).\n"
                                "  11  Pipeline open failed.\n"
                                "  12  Pipeline start failed.\n"
                                "  13  Pipeline runtime error (pipelineErrorSignal fired).\n"
                                "  21  Inspector discontinuity detected.\n"
                                "\n"
                                "Misc:\n"
                                "  -h, --help                Show this help text and the schema.\n");
                printBackendConfigHelp();
        }

        bool parseOptions(int argc, char **argv, Options &opts) {
                // The parser is intentionally minimal: it collects --sc /
                // --sm / --cc / --cm / --dc / --dm strings verbatim into the
                // appropriate StageSpec lists so the real resolution (type
                // coercion, list handling, etc.) happens later against the
                // backend's default config.
                //
                // Stage scoping for the --*c / --*m flags is strictly
                // positional: each -s / -c / -d flips the "scope" that
                // subsequent --sc / --cc / --dc (and their --sm / --cm /
                // --dm metadata counterparts) attach to.
                CmdLineParser parser;
                bool          helpRequested = false;

                opts.source.type = "TPG"; // default source

                parser.registerOptions({
                        {'h', "help", "Show help text and backend config schema", CmdLineParser::OptionCallback([&]() {
                                 helpRequested = true;
                                 return 0;
                         })},

                        // ---- Stages ----
                        {'s', "src", "Source MediaIO backend name (pass 'list' for the registry)",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (s == "list") listMediaIOBackendsAndExit();
                                 StageSpec sp;
                                 classifyStageArg(s, sp);
                                 opts.source = sp;
                                 opts.lastScope = Options::ScopeSource;
                                 opts.explicitSrc = true;
                                 return 0;
                         })},
                        {0, "sc", "Set source stage config (Key:Value), repeatable",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 opts.source.rawKeyValues.pushToBack(s);
                                 return 0;
                         })},
                        {0, "sm", "Set source stage metadata (Key:Value), repeatable",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 opts.source.rawMetaKeyValues.pushToBack(s);
                                 return 0;
                         })},

                        {'c', "convert",
                         "Insert an intermediate MediaIO stage (backend name, e.g. "
                         "CSC / VideoEncoder / VideoDecoder / FrameSync / SRC).  "
                         "Repeatable — stages are chained in the order given.",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (s == "list") listMediaIOBackendsAndExit();
                                 StageSpec sp;
                                 // Intermediate stages must be registered
                                 // MediaIO backends that support
                                 // InputAndOutput; file paths are not a
                                 // legal stage identifier here.  We still
                                 // go through classifyStageArg so the
                                 // diagnostic is uniform with -s / -d when
                                 // the name is unknown.
                                 classifyStageArg(s, sp);
                                 opts.transforms.pushToBack(sp);
                                 opts.lastTransform = opts.transforms.size() - 1;
                                 opts.lastScope = Options::ScopeTransform;
                                 return 0;
                         })},
                        {0, "cc", "Set config on the most recently declared -c (Key:Value), repeatable",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (opts.transforms.isEmpty()) {
                                         fprintf(stderr, "Error: --cc must follow at least one -c/--convert\n");
                                         return 1;
                                 }
                                 opts.transforms[opts.lastTransform].rawKeyValues.pushToBack(s);
                                 return 0;
                         })},
                        {0, "cm", "Set metadata on the most recently declared -c (Key:Value), repeatable",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (opts.transforms.isEmpty()) {
                                         fprintf(stderr, "Error: --cm must follow at least one -c/--convert\n");
                                         return 1;
                                 }
                                 opts.transforms[opts.lastTransform].rawMetaKeyValues.pushToBack(s);
                                 return 0;
                         })},

                        {'d', "dst", "Add a destination stage (backend name, 'SDL', 'list', or path)",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (s == "list") listMediaIOBackendsAndExit();
                                 StageSpec sp;
                                 classifyStageArg(s, sp);
                                 opts.sinks.pushToBack(sp);
                                 opts.lastSink = opts.sinks.size() - 1;
                                 opts.lastScope = Options::ScopeSink;
                                 opts.explicitDst = true;
                                 return 0;
                         })},
                        {0, "dc", "Set current destination config (Key:Value), repeatable",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (opts.sinks.isEmpty()) {
                                         fprintf(stderr, "Error: --dc must follow at least one -d/--dst\n");
                                         return 1;
                                 }
                                 opts.sinks[opts.lastSink].rawKeyValues.pushToBack(s);
                                 return 0;
                         })},
                        {0, "dm", "Set current destination metadata (Key:Value), repeatable",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (opts.sinks.isEmpty()) {
                                         fprintf(stderr, "Error: --dm must follow at least one -d/--dst\n");
                                         return 1;
                                 }
                                 opts.sinks[opts.lastSink].rawMetaKeyValues.pushToBack(s);
                                 return 0;
                         })},

                        // ---- Playback control (non-stage) ----
                        {0, "duration", "Stop after N seconds", CmdLineParser::OptionDoubleCallback([&](double v) {
                                 opts.duration = v;
                                 return 0;
                         })},
                        {0, "frame-count", "Deliver exactly N frames to each sink (0 = unlimited)",
                         CmdLineParser::OptionIntCallback([&](int v) {
                                 if (v < 0) {
                                         fprintf(stderr, "Error: --frame-count must be >= 0\n");
                                         return 1;
                                 }
                                 opts.frameCount = v;
                                 return 0;
                         })},
                        {0, "verbose", "Print periodic progress stats", CmdLineParser::OptionCallback([&]() {
                                 opts.verbose = true;
                                 return 0;
                         })},
                        {0, "stats", "Enable live MediaIO telemetry with per-stage summaries",
                         CmdLineParser::OptionCallback([&]() {
                                 // Default 1.0s unless a prior
                                 // --stats-interval already set a value.
                                 if (opts.statsInterval <= 0.0) {
                                         opts.statsInterval = 1.0;
                                 }
                                 return 0;
                         })},
                        {0, "stats-interval", "Seconds between --stats prints",
                         CmdLineParser::OptionDoubleCallback([&](double v) {
                                 if (v <= 0.0) {
                                         fprintf(stderr, "Error: --stats-interval must be > 0\n");
                                         return 1;
                                 }
                                 opts.statsInterval = v;
                                 return 0;
                         })},
                        {0, "memstats", "Dump MemSpace stats for every registered memory space on shutdown",
                         CmdLineParser::OptionCallback([&]() {
                                 opts.memStats = true;
                                 return 0;
                         })},
                        {0, "probe", "Query and print the source device's supported formats, then exit",
                         CmdLineParser::OptionCallback([&]() {
                                 opts.probe = true;
                                 return 0;
                         })},
                        {0, "no-autoplan",
                         "Disable automatic bridge insertion (CSC, decoder, frame sync, etc.) "
                         "before pipeline build.  Default behaviour runs MediaPipelinePlanner "
                         "automatically; this flag forces a fully-resolved input config.",
                         CmdLineParser::OptionCallback([&]() {
                                 opts.autoplan = false;
                                 return 0;
                         })},
                        {0, "plan",
                         "Run the planner on the resolved config, print the result to stdout, "
                         "and exit without opening anything",
                         CmdLineParser::OptionCallback([&]() {
                                 opts.planOnly = true;
                                 return 0;
                         })},
                        {0, "describe",
                         "Instantiate every stage, call MediaIO::describe on each, print the "
                         "summary, and exit",
                         CmdLineParser::OptionCallback([&]() {
                                 opts.describeOnly = true;
                                 return 0;
                         })},

                        // ---- Pipeline preset I/O ----
                        {0, "save-pipeline", "Build the pipeline from -s/-c/-d, write to PATH as JSON, and exit",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 opts.savePipelinePath = s;
                                 return 0;
                         })},
                        {0, "pipeline", "Load the pipeline from a saved JSON preset instead of -s/-c/-d",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 opts.loadPipelinePath = s;
                                 return 0;
                         })},

                        {0, "write-stats", "Write per-interval and final stats snapshots to PATH as JSON lines",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 opts.writeStatsPath = s;
                                 // Turn on the stats collector when the user
                                 // wants a file but didn't explicitly set an
                                 // interval — a silent file is the wrong
                                 // default.
                                 if (opts.statsInterval <= 0.0) {
                                         opts.statsInterval = 1.0;
                                 }
                                 return 0;
                         })},

                        {0, "list-codecs", "Print codec/backend list (video|audio|all) as TSV and exit",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (s == "video") {
                                         opts.listCodecs = Options::ListCodecsVideo;
                                 } else if (s == "audio") {
                                         opts.listCodecs = Options::ListCodecsAudio;
                                 } else if (s == "all" || s.isEmpty()) {
                                         opts.listCodecs = Options::ListCodecsAll;
                                 } else {
                                         fprintf(stderr,
                                                 "Error: --list-codecs must be 'video', "
                                                 "'audio', or 'all' (got '%s')\n",
                                                 s.cstr());
                                         return 1;
                                 }
                                 return 0;
                         })},

                });

                int r = parser.parseMain(argc, argv);
                if (helpRequested) {
                        usage();
                        std::exit(0);
                }
                if (r != 0) return false;

                if (parser.argCount() > 0) {
                        fprintf(stderr,
                                "Error: unexpected positional argument '%s'.\n"
                                "Use -s/--src for the source and -d/--dst for destinations.\n",
                                parser.arg(0).cstr());
                        return false;
                }

                return true;
        }

} // namespace mediaplay
