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
#include <promeki/metadata.h>
#include <promeki/textstream.h>
#include <promeki/variant.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantspec.h>

using namespace promeki;

namespace mediaplay {

void printBackendConfigHelp() {
        TextStream ts(stdout);
        ts << endl << "Config per backend (set via --sc / --dc / --cc Key:Value," << endl
           << "  pass Key:list for enum values, Key:help for key details):" << endl;

        // "Type" is implied by -i / -o / -c so it has no user-visible
        // role in the per-backend help — hide it from every backend.
        StringList hidden;
        hidden.pushToBack(String("Type"));

        auto dumpBackend = [&](const String &name,
                               const String &caps,
                               const String &desc,
                               const MediaIO::Config::SpecMap &specs) {
                // Emit the header line, then the spec block bracketed
                // by a dashed border that matches the widest physical
                // line the block printed.  Two backends can produce
                // very different widths — TPG is wide, AudioFile is
                // narrow — so the border is computed per block rather
                // than using a fixed column.
                String header = String("  ") + name + " [" + caps + "] — " + desc;
                ts << endl << header << endl;
                if(specs.isEmpty()) {
                        ts << "    (no config keys)" << endl;
                        return;
                }

                // Round-trip through a scratch buffer so we know the
                // actual block width before we write the top border.
                // The alternative — writing two passes directly to the
                // stream — would put the top border below the block
                // or force us to guess the width ahead of time.
                Buffer scratch;
                TextStream body(&scratch);
                int bodyWidth = MediaIO::Config::writeSpecMapHelp(body, specs, hidden);
                body.flush();

                int borderWidth = bodyWidth;
                int headerWidth = static_cast<int>(header.size());
                if(headerWidth > borderWidth) borderWidth = headerWidth;
                String border;
                for(int i = 0; i < borderWidth; ++i) border += '-';
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
        // converter (canInputAndOutput only) displays, so the badge
        // stays consistent regardless of how the backend decomposes
        // its modes.
        for(const auto &desc : MediaIO::registeredFormats()) {
                String caps;
                if(desc.canInput) caps += "I";
                if(desc.canOutput) caps += "O";
                if(desc.canInputAndOutput && caps.isEmpty()) caps = "IO";
                MediaIO::Config::SpecMap specs = desc.configSpecs
                        ? desc.configSpecs() : MediaIO::Config::SpecMap();
                dumpBackend(desc.name, caps, desc.description, specs);
        }

        // SDL is a mediaplay-local pseudo-backend — strictly a sink.
        dumpBackend(String(kStageSdl), String("I"),
                    String(sdlDescription()), sdlConfigSpecs());
}

void usage() {
        fprintf(stderr,
                "Usage: mediaplay [OPTIONS]\n"
                "\n"
                "Pumps media frames from one MediaIO source, through an optional\n"
                "Converter stage, out to one or more MediaIO sinks.  Every stage\n"
                "is configured via generic Key:Value options whose values are\n"
                "parsed against the backend's default config.  Pacing is\n"
                "implicit: if an SDL destination is present it drives the\n"
                "pipeline at video rate (audio-led clock), otherwise frames\n"
                "flow as fast as the file sinks can consume them.\n"
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
                "                            EnumList, and PixelDesc keys.\n"
                "                            Passing `K:help` shows the key's\n"
                "                            type, range, and description.\n"
                "  --sm <K:V>                Set one source stage metadata key\n"
                "                            (Title, Artist, Copyright, ...).\n"
                "                            Repeatable.\n"
                "  -c, --convert <NAME>      Insert an intermediate MediaIO\n"
                "                            stage (backend name, e.g.\n"
                "                            Converter / VideoEncoder /\n"
                "                            VideoDecoder).  Repeatable —\n"
                "                            stages are chained in the\n"
                "                            order given.\n"
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
                "  --frame-count <N>         Stop after N frames (0 = unlimited).\n"
                "                            Use this to bound the actual\n"
                "                            output length in frames.\n"
                "  --verbose                 Print periodic progress stats.\n"
                "  --stats                   Enable live MediaIO telemetry.\n"
                "                            Prints a BytesPerSecond /\n"
                "                            FramesPerSecond / FramesDropped /\n"
                "                            AverageLatencyMs summary for every\n"
                "                            stage (source, converter, sinks)\n"
                "                            once per second by default.  Auto-\n"
                "                            enables EnableBenchmark on every\n"
                "                            stage so latency keys populate.\n"
                "  --stats-interval <SEC>    Override the --stats print interval\n"
                "                            in seconds (default 1.0).\n"
                "  --memstats                Print MemSpace allocation\n"
                "                            statistics for every registered\n"
                "                            memory space on shutdown.\n"
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
        bool helpRequested = false;

        opts.source.type = "TPG";          // default source

        parser.registerOptions({
                {'h', "help",
                 "Show help text and backend config schema",
                 CmdLineParser::OptionCallback([&]() {
                         helpRequested = true;
                         return 0;
                 })},

                // ---- Stages ----
                {'s', "src",
                 "Source MediaIO backend name (pass 'list' for the registry)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listMediaIOBackendsAndExit();
                         StageSpec sp;
                         classifyStageArg(s, sp);
                         opts.source = sp;
                         opts.lastScope = Options::ScopeSource;
                         opts.explicitSrc = true;
                         return 0;
                 })},
                {0, "sc",
                 "Set source stage config (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.source.rawKeyValues.pushToBack(s);
                         return 0;
                 })},
                {0, "sm",
                 "Set source stage metadata (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.source.rawMetaKeyValues.pushToBack(s);
                         return 0;
                 })},

                {'c', "convert",
                 "Insert an intermediate MediaIO stage (backend name, e.g. "
                 "Converter / VideoEncoder / VideoDecoder).  Repeatable — "
                 "stages are chained in the order given.",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listMediaIOBackendsAndExit();
                         StageSpec sp;
                         // Intermediate stages must be registered
                         // MediaIO backends that support
                         // InputAndOutput; file paths are not a
                         // legal stage identifier here.  We still
                         // go through classifyStageArg so the
                         // diagnostic is uniform with -s / -d when
                         // the name is unknown.
                         classifyStageArg(s, sp);
                         opts.converters.pushToBack(sp);
                         opts.lastConverter = opts.converters.size() - 1;
                         opts.lastScope     = Options::ScopeConverter;
                         return 0;
                 })},
                {0, "cc",
                 "Set config on the most recently declared -c (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(opts.converters.isEmpty()) {
                                 fprintf(stderr,
                                         "Error: --cc must follow at least one -c/--convert\n");
                                 return 1;
                         }
                         opts.converters[opts.lastConverter]
                                 .rawKeyValues.pushToBack(s);
                         return 0;
                 })},
                {0, "cm",
                 "Set metadata on the most recently declared -c (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(opts.converters.isEmpty()) {
                                 fprintf(stderr,
                                         "Error: --cm must follow at least one -c/--convert\n");
                                 return 1;
                         }
                         opts.converters[opts.lastConverter]
                                 .rawMetaKeyValues.pushToBack(s);
                         return 0;
                 })},

                {'d', "dst",
                 "Add a destination stage (backend name, 'SDL', 'list', or path)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listMediaIOBackendsAndExit();
                         StageSpec sp;
                         classifyStageArg(s, sp);
                         opts.sinks.pushToBack(sp);
                         opts.lastSink   = opts.sinks.size() - 1;
                         opts.lastScope  = Options::ScopeSink;
                         opts.explicitDst = true;
                         return 0;
                 })},
                {0, "dc",
                 "Set current destination config (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(opts.sinks.isEmpty()) {
                                 fprintf(stderr,
                                         "Error: --dc must follow at least one -d/--dst\n");
                                 return 1;
                         }
                         opts.sinks[opts.lastSink].rawKeyValues.pushToBack(s);
                         return 0;
                 })},
                {0, "dm",
                 "Set current destination metadata (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(opts.sinks.isEmpty()) {
                                 fprintf(stderr,
                                         "Error: --dm must follow at least one -d/--dst\n");
                                 return 1;
                         }
                         opts.sinks[opts.lastSink].rawMetaKeyValues.pushToBack(s);
                         return 0;
                 })},

                // ---- Playback control (non-stage) ----
                {0, "duration",
                 "Stop after N seconds",
                 CmdLineParser::OptionDoubleCallback([&](double v) {
                         opts.duration = v;
                         return 0;
                 })},
                {0, "frame-count",
                 "Stop after N frames",
                 CmdLineParser::OptionIntCallback([&](int v) {
                         opts.frameCount = v;
                         return 0;
                 })},
                {0, "verbose",
                 "Print periodic progress stats",
                 CmdLineParser::OptionCallback([&]() {
                         opts.verbose = true;
                         return 0;
                 })},
                {0, "stats",
                 "Enable live MediaIO telemetry with per-stage summaries",
                 CmdLineParser::OptionCallback([&]() {
                         // Default 1.0s unless a prior
                         // --stats-interval already set a value.
                         if(opts.statsInterval <= 0.0) {
                                 opts.statsInterval = 1.0;
                         }
                         return 0;
                 })},
                {0, "stats-interval",
                 "Seconds between --stats prints",
                 CmdLineParser::OptionDoubleCallback([&](double v) {
                         if(v <= 0.0) {
                                 fprintf(stderr,
                                         "Error: --stats-interval must be > 0\n");
                                 return 1;
                         }
                         opts.statsInterval = v;
                         return 0;
                 })},
                {0, "memstats",
                 "Dump MemSpace stats for every registered memory space on shutdown",
                 CmdLineParser::OptionCallback([&]() {
                         opts.memStats = true;
                         return 0;
                 })},
                {0, "probe",
                 "Query and print the source device's supported formats, then exit",
                 CmdLineParser::OptionCallback([&]() {
                         opts.probe = true;
                         return 0;
                 })},
        });

        int r = parser.parseMain(argc, argv);
        if(helpRequested) {
                usage();
                std::exit(0);
        }
        if(r != 0) return false;

        if(parser.argCount() > 0) {
                fprintf(stderr,
                        "Error: unexpected positional argument '%s'.\n"
                        "Use -s/--src for the source and -d/--dst for destinations.\n",
                        parser.arg(0).cstr());
                return false;
        }

        return true;
}

} // namespace mediaplay
