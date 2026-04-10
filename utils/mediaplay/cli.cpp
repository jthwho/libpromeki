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
#include <promeki/variant.h>
#include <promeki/variantdatabase.h>

using namespace promeki;

namespace mediaplay {

namespace {

// Dumps every entry of a VariantDatabase<Tag> to stdout, sorted by
// key name, with Enums rendered as just their valueName() so the
// trailing [Enum] column isn't redundant.  Used for both the
// MediaConfig schema and the Metadata schema dumps.
template <typename Tag>
static void printDatabaseDump(const VariantDatabase<Tag> &db) {
        using DbID = typename VariantDatabase<Tag>::ID;
        List<DbID> ids = db.ids();
        StringList names;
        for(size_t i = 0; i < ids.size(); ++i) names.pushToBack(ids[i].name());
        // List::sort() returns a sorted copy rather than sorting in
        // place, so we have to assign the result back.
        names = names.sort();
        for(size_t i = 0; i < names.size(); ++i) {
                DbID id(names[i]);
                Variant v = db.get(id);
                String rendered;
                if(v.type() == Variant::TypeEnum) {
                        // Enum::toString() returns the fully
                        // qualified "TypeName::ValueName" form,
                        // which duplicates the type tag we already
                        // print in the trailing [Enum] column.
                        // valueName() is more readable.
                        Enum e = v.get<Enum>();
                        rendered = e.valueName();
                } else {
                        Error se;
                        rendered = v.get<String>(&se);
                        if(se.isError()) rendered = String("<") + v.typeName() + ">";
                }
                fprintf(stdout, "    %-24s = %-16s  [%s]\n",
                        names[i].cstr(), rendered.cstr(), v.typeName());
        }
}

} // namespace

void printBackendConfigHelp() {
        fprintf(stdout,
                "\nSchema per backend (set via --ic / --im / --oc / --om / --cc / --cm Key:Value):\n");
        auto dumpBackend = [&](const String &name,
                               const String &caps,
                               const String &desc,
                               const MediaIO::Config &cfg,
                               const Metadata &meta) {
                fprintf(stdout, "\n  %s [%s] — %s\n",
                        name.cstr(), caps.cstr(), desc.cstr());
                if(cfg.isEmpty()) {
                        fprintf(stdout, "    config: (none)\n");
                } else {
                        fprintf(stdout, "    config keys (--ic / --oc / --cc):\n");
                        printDatabaseDump(cfg);
                }
                if(meta.isEmpty()) {
                        fprintf(stdout, "    metadata: (none)\n");
                } else {
                        fprintf(stdout, "    metadata keys (--im / --om / --cm):\n");
                        printDatabaseDump(meta);
                }
        };

        for(const auto &desc : MediaIO::registeredFormats()) {
                String caps;
                if(desc.canRead) caps += "R";
                if(desc.canWrite) caps += "W";
                if(desc.canReadWrite) caps += "RW";
                dumpBackend(desc.name, caps, desc.description,
                            MediaIO::defaultConfig(desc.name),
                            MediaIO::defaultMetadata(desc.name));
        }

        // SDL is a mediaplay-local pseudo-backend (see stage.h) —
        // still advertise its schema alongside the registry so users
        // see a uniform picture.
        dumpBackend(String(kStageSdl),
                    String("W"),
                    String(sdlDescription()),
                    sdlDefaultConfig(),
                    sdlDefaultMetadata());
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
                "                            lists valid values when the\n"
                "                            key's type is an Enum or PixelDesc.\n"
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
