/**
 * @file      mediaplay/cli.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "cli.h"
#include "helpformat.h"

#include <cstdio>
#include <cstdlib>

#include <promeki/ansistream.h>
#include <promeki/cmdlineparser.h>
#include <promeki/error.h>
#include <promeki/fileiodevice.h>
#include <promeki/list.h>
#include <promeki/string.h>

using namespace promeki;

namespace mediaplay {

        namespace {

                // A single flag entry (spec + description).  The renderer
                // pads `spec` to the widest spec in the section so the
                // description column lines up.
                struct HelpOption {
                                String spec;
                                String description;
                };

                // One section of the help output — heading, optional
                // free-text body under the heading, and zero or more
                // options.  Layout column widths are computed per-section
                // so a section of short specs (exit codes) lines up tight
                // while the long --src spec doesn't push everything else
                // to the right.
                struct HelpSection {
                                String           title;
                                String           prose;
                                List<HelpOption> options;
                };

                // Walks the section list and renders the full help doc
                // to @p out.  Word-wrapping uses the runtime terminal
                // width returned by @ref detectTerminalCols; if the
                // stream is color-enabled, section headings and option
                // specs pick up the accent colors from helpPalette().
                void renderHelp(AnsiStream &out, const String &title, const String &intro,
                                const List<HelpSection> &sections) {
                        const int          cols = detectTerminalCols();
                        const HelpPalette &palette = helpPalette();

                        out << title << '\n' << '\n';
                        writeWrapped(out, intro, 0, cols);
                        out << '\n';

                        for (const HelpSection &section : sections) {
                                out << '\n';
                                out.setForeground(palette.section);
                                out << section.title;
                                out.reset();
                                out << '\n';

                                if (!section.prose.isEmpty()) {
                                        out << "  ";
                                        writeWrapped(out, section.prose, 2, cols);
                                        out << '\n';
                                }

                                // Spec column width is computed locally
                                // to this section so short-spec sections
                                // don't inherit the longest option-spec
                                // width from the rest of the doc.
                                int specWidth = 0;
                                for (const HelpOption &opt : section.options) {
                                        int sw = static_cast<int>(opt.spec.size());
                                        if (sw > specWidth) specWidth = sw;
                                }

                                // Layout: "  <spec><pad>  <description>".
                                // The 4 = 2 left-margin + 2 gutter.
                                const int leftPad = 4 + specWidth;

                                for (const HelpOption &opt : section.options) {
                                        out << "  ";
                                        out.setForeground(palette.option);
                                        out << opt.spec;
                                        out.reset();
                                        int pad = specWidth - static_cast<int>(opt.spec.size()) + 2;
                                        for (int i = 0; i < pad; ++i) out << ' ';
                                        writeWrapped(out, opt.description, leftPad, cols);
                                        out << '\n';
                                }
                        }
                        out.flush();
                }

                // Builds the static help document.  Long descriptions are
                // single logical lines (no embedded \n) so the renderer
                // is free to wrap them to whatever width the terminal
                // happens to be; explicit \n is reserved for hard
                // paragraph boundaries inside one entry (e.g. when a
                // single option needs a follow-up caveat paragraph).
                List<HelpSection> buildHelpSections() {
                        List<HelpSection> sections;

                        HelpSection stages;
                        stages.title = "Stages:";
                        stages.options = {
                                {"-s, --src <NAME|list>", "Source backend name (default: TPG)."},
                                {"--sc <K:V>",
                                 "Set one source stage config key.  Repeatable.  Passing `K:list` lists "
                                 "valid values for Enum, EnumList, and PixelFormat keys.  Passing `K:help` "
                                 "shows the key's type, range, and description."},
                                {"--sm <K:V>",
                                 "Set one source stage metadata key (Title, Artist, Copyright, ...).  "
                                 "Repeatable."},
                                {"-c, --convert <NAME>",
                                 "Insert an intermediate MediaIO stage (backend name, e.g. CSC / "
                                 "VideoEncoder / VideoDecoder / FrameSync / SRC).  Repeatable — stages "
                                 "are chained in the order given."},
                                {"--cc <K:V>",
                                 "Set one config key on the most recently declared -c.  Repeatable."},
                                {"--cm <K:V>",
                                 "Set one metadata key on the most recently declared -c.  Repeatable."},
                                {"-d, --dst <NAME|PATH|list>",
                                 "Add a destination stage — a backend name (SDL, QuickTime, ImageFile, "
                                 "...) or a file path auto-detected via MediaIO::createForFileWrite.  "
                                 "Default destination is SDL.  Repeatable (fan-out)."},
                                {"--dc <K:V>",
                                 "Set one config key on the most recently declared -d.  Repeatable."},
                                {"--dm <K:V>",
                                 "Set one metadata key on the most recently declared -d.  Overlays "
                                 "metadata inherited from upstream.  Repeatable."},
                        };
                        sections.pushToBack(std::move(stages));

                        HelpSection playback;
                        playback.title = "Playback control:";
                        playback.options = {
                                {"--duration <SEC>",
                                 "Wall-clock runtime limit for the mediaplay process.  This is NOT a "
                                 "recording length — an unpaced file-only pipeline can easily write "
                                 "thousands of frames per second, so a 10s --duration may produce many "
                                 "more frames of content than you expect.  Use --frame-count when you "
                                 "want a specific number of output frames."},
                                {"--frame-count <N>",
                                 "Require the pipeline to deliver exactly N frames to each sink "
                                 "(0 = unlimited).  Runs through MediaPipelineConfig::frameCount — once "
                                 "a sink has received N frames at the next safe cut point (keyframe "
                                 "boundary for interframe codecs) the pipeline closes that sink and "
                                 "drops further source output on the floor.  Interframe streams may "
                                 "overshoot by up to one GOP so the sink ends on a complete sequence of "
                                 "GOPs."},
                                {"--verbose", "Print periodic progress stats."},
                                {"--stats",
                                 "Enable live MediaIO telemetry.  Prints a BytesPerSecond / "
                                 "FramesPerSecond / FramesDropped / QueueDepth summary for every stage "
                                 "(source, transform, sinks) once per second by default."},
                                {"--stats-interval <SEC>",
                                 "Override the --stats print interval in seconds (default 1.0)."},
                                {"--cpumon <SEC>",
                                 "Sample per-thread CPU usage every SEC seconds and log a one-line "
                                 "summary at Info level showing the top consumers, sorted descending.  "
                                 "Disabled by default."},
                                {"--elstats <SEC>",
                                 "Sample per-EventLoop activity every SEC seconds and log a one-line "
                                 "summary at Info level breaking each loop's wallclock into named "
                                 "buckets.  Disabled by default."},
                                {"--memstats",
                                 "Print MemSpace allocation statistics for every registered memory "
                                 "space on shutdown."},
                        };
                        sections.pushToBack(std::move(playback));

                        HelpSection preset;
                        preset.title = "Pipeline preset I/O:";
                        preset.options = {
                                {"--save-pipeline <PATH>",
                                 "Build the pipeline from the other flags, write it to PATH as a JSON "
                                 "preset, and exit without opening stages.  Pair with --pipeline <PATH> "
                                 "to re-run the same pipeline later."},
                                {"--pipeline <PATH>",
                                 "Load the pipeline from PATH (previously written by --save-pipeline) "
                                 "instead of building it from -s / -c / -d.  Other non-stage flags "
                                 "(--duration, --stats, --frame-count) still apply."},
                                {"--write-stats <PATH>",
                                 "Append a JSON-lines snapshot of the pipeline's stats to PATH once per "
                                 "--stats-interval, plus a final aggregate snapshot at shutdown.  "
                                 "Implicitly turns on the stats collector (default 1s interval)."},
                        };
                        sections.pushToBack(std::move(preset));

                        HelpSection planner;
                        planner.title = "Planner:";
                        planner.options = {
                                {"--no-autoplan",
                                 "Disable automatic bridge insertion (CSC, decoder, FrameSync, SRC, "
                                 "encoder) before pipeline build.  Default behaviour runs "
                                 "MediaPipelinePlanner; this flag forces a strict, fully-resolved input "
                                 "config."},
                                {"--plan",
                                 "Run the planner against the resolved CLI config, print the resulting "
                                 "stages and routes to stdout, and exit without opening anything."},
                                {"--describe",
                                 "Instantiate every stage, call MediaIO::describe on each, dump the "
                                 "summary, and exit."},
                        };
                        sections.pushToBack(std::move(planner));

                        HelpSection inspector;
                        inspector.title = "Inspector-driven pass/fail:";
                        inspector.prose = "Every Inspector sink is injected automatically so mediaplay can poll "
                                          "its snapshot at shutdown.  Any discontinuity reported by Inspector "
                                          "surfaces as exit code 21 below.";
                        sections.pushToBack(std::move(inspector));

                        HelpSection registry;
                        registry.title = "Registry enumeration:";
                        registry.options = {
                                {"--list-io",
                                 "Print every MediaIO backend on stdout and exit.  Columns: Name, Mode "
                                 "(I=input-only, O=output-only, I/O=both, T=transform), Description."},
                                {"--list-config <NAME>",
                                 "Print the named backend's config schema on stdout and exit.  Each key "
                                 "is shown with its type, range, default, and description."},
                                {"--list-codecs [video|audio|all]",
                                 "Print a tab-separated list of codec / backend pairs, one per line, "
                                 "and exit.  Columns: kind, codec, backend, enc, dec (yes / no).  "
                                 "Codecs with no registered backend are omitted entirely.  Default "
                                 "argument is 'all'.  Intended for scripts (see "
                                 "scripts/roundtrip-codecs.sh)."},
                        };
                        sections.pushToBack(std::move(registry));

                        HelpSection exits;
                        exits.title = "Exit codes (applicable to all runs):";
                        // Right-justify the exit code column manually
                        // (the spec column is left-aligned in render);
                        // a leading space gives "  0" / " 10" alignment.
                        exits.options = {
                                {" 0", "Success."},
                                {" 1", "Generic failure (CLI, argparse, setup)."},
                                {"10", "Pipeline build failed (planner could not resolve)."},
                                {"11", "Pipeline open failed."},
                                {"12", "Pipeline start failed."},
                                {"13", "Pipeline runtime error (pipelineErrorSignal fired)."},
                                {"21", "Inspector discontinuity detected."},
                        };
                        sections.pushToBack(std::move(exits));

                        HelpSection misc;
                        misc.title = "Misc:";
                        misc.options = {
                                {"-h, --help", "Show this help text."},
                        };
                        sections.pushToBack(std::move(misc));

                        return sections;
                }

        } // namespace

        void usage() {
                AnsiStream out(FileIODevice::stdoutDevice());
                out.setAnsiEnabled(helpUseColor());

                const String title = "Usage: mediaplay [OPTIONS]";
                const String intro =
                        "Pumps media frames from one MediaIO source, through zero or more intermediate "
                        "transform stages (CSC, SRC, VideoEncoder, etc.), out to one or more MediaIO "
                        "sinks.  Every stage is configured via generic Key:Value options whose values "
                        "are parsed against the backend's default config.  Pacing is implicit: if an "
                        "SDL destination is present it drives the pipeline at video rate (audio-led "
                        "clock), otherwise frames flow as fast as the file sinks can consume them.\n"
                        "\n"
                        "Use --list-io for the registry of available MediaIO backends and --list-config "
                        "<NAME> for one backend's config schema.";

                renderHelp(out, title, intro, buildHelpSections());
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
                        {'h', "help", "Show help text", CmdLineParser::OptionCallback([&]() {
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
                        {0, "cpumon", "Per-thread CPU usage report every <SEC> seconds",
                         CmdLineParser::OptionDoubleCallback([&](double v) {
                                 if (v <= 0.0) {
                                         fprintf(stderr, "Error: --cpumon must be > 0\n");
                                         return 1;
                                 }
                                 opts.cpuMonInterval = v;
                                 return 0;
                         })},
                        {0, "elstats", "Per-EventLoop activity report every <SEC> seconds",
                         CmdLineParser::OptionDoubleCallback([&](double v) {
                                 if (v <= 0.0) {
                                         fprintf(stderr, "Error: --elstats must be > 0\n");
                                         return 1;
                                 }
                                 opts.elStatsInterval = v;
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

                        {0, "list-io",
                         "Print every MediaIO backend (Name, Mode, Description) and exit",
                         CmdLineParser::OptionCallback([&]() {
                                 listMediaIOBackendsAndExit();
                                 return 0; // unreachable
                         })},
                        {0, "list-config",
                         "Print the named backend's config schema (--list-config <NAME>) and exit",
                         CmdLineParser::OptionStringCallback([&](const String &s) {
                                 if (s.isEmpty()) {
                                         fprintf(stderr, "Error: --list-config requires a MediaIO backend name\n");
                                         return 1;
                                 }
                                 listMediaIOConfigAndExit(s);
                                 return 0; // unreachable
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
