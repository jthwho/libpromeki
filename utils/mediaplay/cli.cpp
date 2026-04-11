/**
 * @file      mediaplay/cli.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "cli.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include <promeki/cmdlineparser.h>
#include <promeki/error.h>
#include <promeki/filepath.h>
#include <promeki/mediaio.h>
#include <promeki/metadata.h>
#include <promeki/numname.h>
#include <promeki/textstream.h>
#include <promeki/variant.h>
#include <promeki/variantdatabase.h>
#include <promeki/variantspec.h>

using namespace promeki;

namespace mediaplay {

void printBackendConfigHelp() {
        TextStream ts(stdout);
        ts << endl << "Config per backend (set via --ic / --oc / --cc Key:Value," << endl
           << "  pass Key:list for enum values, Key:help for key details):" << endl;

        auto dumpBackend = [&](const String &name,
                               const String &caps,
                               const String &desc,
                               const MediaIO::Config::SpecMap &specs) {
                ts << endl << "  " << name << " [" << caps << "] — " << desc << endl;
                if(specs.isEmpty()) {
                        ts << "    (no config keys)" << endl;
                } else {
                        MediaIO::Config::writeSpecMapHelp(ts, specs);
                }
        };

        for(const auto &desc : MediaIO::registeredFormats()) {
                String caps;
                if(desc.canRead) caps += "R";
                if(desc.canWrite) caps += "W";
                if(desc.canReadWrite) caps += "RW";
                MediaIO::Config::SpecMap specs = desc.configSpecs
                        ? desc.configSpecs() : MediaIO::Config::SpecMap();
                dumpBackend(desc.name, caps, desc.description, specs);
        }

        // SDL is a mediaplay-local pseudo-backend.
        dumpBackend(String(kStageSdl), String("W"),
                    String(sdlDescription()), sdlConfigSpecs());
}

void usage() {
        fprintf(stderr,
                "Usage: mediaplay [OPTIONS] [INPUT [OUTPUT...]]\n"
                "\n"
                "Pumps media frames from one MediaIO input, through an optional\n"
                "Converter stage, out to one or more MediaIO sinks.  Every stage\n"
                "is configured via generic Key:Value options whose values are\n"
                "parsed against the backend's default config.  Pacing is\n"
                "implicit: if an SDL output is present it drives the pipeline\n"
                "at video rate (audio-led clock), otherwise frames flow as\n"
                "fast as the file sinks can consume them.\n"
                "\n"
                "Stages:\n"
                "  -i, --in <NAME|list>      Input backend name (default: TPG).\n"
                "  --ic <K:V>                Set one input stage config key.\n"
                "                            Repeatable.  Passing `K:list`\n"
                "                            lists valid values for Enum/PixelDesc\n"
                "                            keys.  Passing `K:help` shows the\n"
                "                            key's type, range, and description.\n"
                "  --im <K:V>                Set one input stage metadata key\n"
                "                            (Title, Artist, Copyright, ...).\n"
                "                            Repeatable.\n"
                "  -c, --convert             Insert a MediaIOTask_Converter\n"
                "                            between input and outputs.\n"
                "  --cc <K:V>                Set one Converter config key.\n"
                "                            Implies --convert.  Repeatable.\n"
                "  --cm <K:V>                Set one Converter metadata key.\n"
                "                            Implies --convert.  Repeatable.\n"
                "  -o, --out <NAME|PATH|list>\n"
                "                            Add an output stage — a backend\n"
                "                            name (SDL, QuickTime, ImageFile,\n"
                "                            ...) or a file path auto-detected\n"
                "                            via MediaIO::createForFileWrite.\n"
                "                            Repeatable (fan-out).\n"
                "  --oc <K:V>                Set one config key on the most\n"
                "                            recently declared -o.  Repeatable.\n"
                "  --om <K:V>                Set one metadata key on the most\n"
                "                            recently declared -o.  Overlays\n"
                "                            metadata inherited from upstream.\n"
                "                            Repeatable.\n"
                "\n"
                "SDL output (use -o SDL, configure via --oc):\n"
                "  --oc SdlPaced:false          Run the SDL sink as fast as\n"
                "                               possible (disables audio).\n"
                "  --oc SdlAudioEnabled:false   Open the SDL window but no audio.\n"
                "  --oc SdlWindowSize:1920x1080 Initial SDL window size.\n"
                "  --oc SdlWindowTitle:Preview  Set the SDL window title bar.\n"
                "\n"
                "Positional shortcuts:\n"
                "  mediaplay <input-file>            Read the file, play to SDL.\n"
                "  mediaplay <input> <output>...     Transcode input to every\n"
                "                                    output path listed.\n"
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
        // The parser is intentionally minimal: it collects --ic /
        // --im / --cc / --cm / --oc / --om strings verbatim into the
        // appropriate StageSpec lists so the real resolution (type
        // coercion, list handling, etc.) happens later against the
        // backend's default config.
        //
        // Stage scoping for the --*c / --*m flags is strictly
        // positional: each -i / -c / -o flips the "scope" that
        // subsequent --ic / --cc / --oc (and their --im / --cm /
        // --om metadata counterparts) attach to.
        CmdLineParser parser;
        bool helpRequested = false;

        opts.input.type = "TPG";          // default input

        parser.registerOptions({
                {'h', "help",
                 "Show help text and backend config schema",
                 CmdLineParser::OptionCallback([&]() {
                         helpRequested = true;
                         return 0;
                 })},

                // ---- Stages ----
                {'i', "in",
                 "Input MediaIO backend name (pass 'list' for the registry)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listMediaIOBackendsAndExit();
                         StageSpec sp;
                         classifyStageArg(s, sp);
                         opts.input = sp;
                         opts.lastScope = Options::ScopeInput;
                         opts.explicitIn = true;
                         return 0;
                 })},
                {0, "ic",
                 "Set input stage config (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.input.rawKeyValues.pushToBack(s);
                         return 0;
                 })},
                {0, "im",
                 "Set input stage metadata (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.input.rawMetaKeyValues.pushToBack(s);
                         return 0;
                 })},

                {'c', "convert",
                 "Insert a MediaIOTask_Converter between input and outputs",
                 CmdLineParser::OptionCallback([&]() {
                         opts.hasConverter = true;
                         opts.converter.type = "Converter";
                         opts.lastScope = Options::ScopeConverter;
                         return 0;
                 })},
                {0, "cc",
                 "Set Converter stage config (Key:Value), repeatable; implies --convert",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.hasConverter = true;
                         opts.converter.type = "Converter";
                         opts.converter.rawKeyValues.pushToBack(s);
                         return 0;
                 })},
                {0, "cm",
                 "Set Converter stage metadata (Key:Value), repeatable; implies --convert",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.hasConverter = true;
                         opts.converter.type = "Converter";
                         opts.converter.rawMetaKeyValues.pushToBack(s);
                         return 0;
                 })},

                {'o', "out",
                 "Add an output stage (backend name, 'SDL', 'list', or path)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listMediaIOBackendsAndExit();
                         StageSpec sp;
                         classifyStageArg(s, sp);
                         opts.outputs.pushToBack(sp);
                         opts.lastOutput  = opts.outputs.size() - 1;
                         opts.lastScope   = Options::ScopeOutput;
                         opts.explicitOut = true;
                         return 0;
                 })},
                {0, "oc",
                 "Set current output config (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(opts.outputs.isEmpty()) {
                                 fprintf(stderr,
                                         "Error: --oc must follow at least one -o/--out\n");
                                 return 1;
                         }
                         opts.outputs[opts.lastOutput].rawKeyValues.pushToBack(s);
                         return 0;
                 })},
                {0, "om",
                 "Set current output metadata (Key:Value), repeatable",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(opts.outputs.isEmpty()) {
                                 fprintf(stderr,
                                         "Error: --om must follow at least one -o/--out\n");
                                 return 1;
                         }
                         opts.outputs[opts.lastOutput].rawMetaKeyValues.pushToBack(s);
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
        });

        int r = parser.parseMain(argc, argv);
        if(helpRequested) {
                usage();
                std::exit(0);
        }
        if(r != 0) return false;

        // --- Positional arguments ---
        //
        // Positional handling depends on whether the user gave an
        // explicit --in.  Without --in the first positional becomes
        // the input when it looks like an existing readable file —
        // otherwise (sequence mask, non-existent path, etc.) it is
        // treated as an output target.  Once --in is set, every
        // positional is an output.
        //
        //   mediaplay foo.mov                 → read foo.mov, SDL out
        //   mediaplay out.dpx                 → TPG in, write out.dpx
        //   mediaplay out_####.dpx            → TPG in, sequence out
        //   mediaplay foo.mov out.mov         → transcode
        int positional = parser.argCount();
        int posStart = 0;
        if(positional > 0 && !opts.explicitIn) {
                const String &first = parser.arg(0);
                NumName maskProbe = NumName::fromMask(FilePath(first).fileName());
                bool looksLikeInput = false;
                if(!maskProbe.isValid()) {
                        // stat() — std::filesystem is acceptable here
                        // because mediaplay is a utility and already
                        // depends on libstdc++.
                        std::error_code ec;
                        looksLikeInput = std::filesystem::exists(first.cstr(), ec);
                }
                if(looksLikeInput) {
                        StageSpec sp;
                        classifyStageArg(first, sp);
                        opts.input = sp;
                        posStart = 1;
                }
        }
        for(int i = posStart; i < positional; ++i) {
                StageSpec sp;
                classifyStageArg(parser.arg(i), sp);
                opts.outputs.pushToBack(sp);
                opts.explicitOut = true;
        }

        return true;
}

} // namespace mediaplay
