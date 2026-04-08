/**
 * @file      mediaplay/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * General-purpose media pump built on the MediaIO framework.  Reads
 * frames from any MediaIO source (selected via command-line options)
 * and delivers them to one or more MediaIO sinks: an SDL-backed
 * player for real-time display and audio, and/or a MediaIO writer
 * for file or image-sequence output.  The same frame is sent to
 * every enabled sink, making this the long-term host for playback,
 * capture, transcode and test-sequence generation.
 */

#include <csignal>
#include <cstdio>
#include <cstdlib>

#include <promeki/application.h>
#include <promeki/cmdlineparser.h>
#include <promeki/color.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/filepath.h>
#include <promeki/framerate.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_imagefile.h>
#include <promeki/mediaiotask_tpg.h>
#include <promeki/mediadesc.h>
#include <promeki/numname.h>
#include <promeki/objectbase.h>
#include <promeki/pixeldesc.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/frame.h>
#include <promeki/imgseq.h>
#include <promeki/enum.h>
#include <promeki/enums.h>

#include <promeki/sdl/sdlapplication.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>

using namespace promeki;

// --------------------------------------------------------------------
// "list" value support for enum-like options
// --------------------------------------------------------------------

/**
 * @brief Prints every registered value of an @ref Enum type to stdout
 *        and exits the process with status 0.
 *
 * Used by the @c --pattern=list, @c --audio-mode=list and
 * @c --burn-position=list convenience forms.  Called directly from the
 * CmdLineParser callback — whichever list option parses first wins and
 * the process terminates immediately, which matches the typical single-
 * list invocation pattern.
 *
 * @param label  Human-readable header (e.g. @c "Video patterns").
 * @param type   Registered @ref Enum type to enumerate.
 */
[[noreturn]] static void listEnumAndExit(const char *label, Enum::Type type) {
        fprintf(stdout, "%s:\n", label);
        Enum::ValueList values = Enum::values(type);
        for(size_t i = 0; i < values.size(); i++) {
                fprintf(stdout, "  %-16s (%d)\n",
                        values[i].first().cstr(), values[i].second());
        }
        std::exit(0);
}

/**
 * @brief Prints every registered @ref PixelDesc to stdout and exits.
 *
 * Used by the @c --pixel-format=list convenience form.  Output is one
 * line per registered format: @c "  Name   Description".
 */
[[noreturn]] static void listPixelFormatsAndExit() {
        fprintf(stdout, "Registered pixel formats:\n");
        PixelDesc::IDList ids = PixelDesc::registeredIDs();
        for(size_t i = 0; i < ids.size(); i++) {
                PixelDesc pd(ids[i]);
                fprintf(stdout, "  %-32s %s\n",
                        pd.name().cstr(), pd.desc().cstr());
        }
        std::exit(0);
}

/**
 * @brief Prints every registered MediaIO backend to stdout and exits.
 *
 * Used by the @c --type=list convenience form and by the legacy
 * @c --list-types flag.
 */
[[noreturn]] static void listMediaIOBackendsAndExit() {
        fprintf(stdout, "Registered MediaIO backends:\n");
        for(const auto &desc : MediaIO::registeredFormats()) {
                fprintf(stdout, "  %-20s %s\n",
                        desc.name.cstr(), desc.description.cstr());
        }
        std::exit(0);
}

// --------------------------------------------------------------------
// Options
// --------------------------------------------------------------------

struct Options {
        // Source selection
        String  type            = "TPG";
        String  file;

        // Frame rate / video layout (applies to TPG and anything that
        // accepts the corresponding config keys).
        String  rate            = "29.97";
        String  size            = "1280x720";
        String  pixelFormat     = "RGB8_sRGB";
        String  pattern         = "ColorBars";
        double  motion          = 0.0;
        bool    tpgVideo        = true;

        // TPG audio
        bool    tpgAudio        = true;
        String  audioMode       = "Tone";
        double  audioRate       = 48000.0;
        int     audioChannels   = 2;
        double  audioTone       = 1000.0;
        double  audioLevel      = -20.0;

        // TPG timecode
        bool    tpgTimecode     = true;
        String  tcStart         = "01:00:00:00";
        bool    tcDropFrame     = false;

        // TPG burn-in
        bool    burnEnabled     = false;
        String  burnFontPath;
        int     burnFontSize    = 36;
        String  burnText;
        String  burnPosition    = "BottomCenter";
        String  burnFgColor;    // empty = use VideoTestPattern default (white)
        String  burnBgColor;    // empty = use VideoTestPattern default (black)

        // Playback / display
        bool    fast            = false;
        bool    noAudio         = false;
        bool    noDisplay       = false;
        String  windowSize      = "1280x720";
        double  duration        = 0.0;
        int64_t frameCount      = 0;    // 0 = unlimited
        bool    verbose         = false;

        // File / sequence output
        String  output;                 // path, mask, or .imgseq sidecar
        int     seqHead         = 0;    // starting frame number for sequence writer
        bool    writeImgSeq     = false;
        String  imgSeqPath;             // explicit sidecar path (empty = auto-derive)
};

static void usage() {
        fprintf(stderr,
                "Usage: mediaplay [OPTIONS]\n"
                "\n"
                "Reads frames from a MediaIO source and plays them back through\n"
                "an SDL window (and audio device).  Mostly used to exercise the\n"
                "SDLPlayer MediaIO backend with the test pattern generator.\n"
                "\n"
                "Any option below whose value is an enum-like name accepts the\n"
                "special value 'list' to print every accepted choice and exit.\n"
                "For example:\n"
                "    mediaplay --pattern list\n"
                "    mediaplay --pixel-format list\n"
                "\n"
                "Source selection:\n"
                "  --type <NAME|list>       MediaIO backend name (default: TPG)\n"
                "  --file <PATH>            Read from file (infers type from extension)\n"
                "\n"
                "TPG video:\n"
                "  --rate <R>               Frame rate (default: 29.97)\n"
                "  --pattern <P|list>       Test pattern (default: ColorBars)\n"
                "  --size <WxH>             Frame size (default: 1280x720)\n"
                "  --pixel-format <F|list>  Pixel format name (default: RGB8_sRGB)\n"
                "  --motion <S>             Motion speed (default: 0.0)\n"
                "  --no-tpg-video           Disable TPG video generation\n"
                "\n"
                "TPG audio:\n"
                "  --audio-mode <M|list>    Tone | Silence | LTC | AvSync (default: Tone)\n"
                "  --audio-rate <R>         Sample rate in Hz (default: 48000)\n"
                "  --audio-channels <N>     Channel count (default: 2)\n"
                "  --audio-tone <HZ>        Tone frequency in Hz (default: 1000)\n"
                "  --audio-level <DBFS>     Tone level in dBFS (default: -20)\n"
                "  --no-tpg-audio           Disable TPG audio generation\n"
                "\n"
                "TPG timecode:\n"
                "  --no-tpg-tc              Disable TPG timecode generation\n"
                "  --tc-start <TC>          Starting timecode (default: 01:00:00:00)\n"
                "  --tc-df                  Use drop-frame timecode\n"
                "\n"
                "TPG burn-in:\n"
                "  --burn                   Enable text burn-in on the pattern.\n"
                "                           Includes the current timecode when\n"
                "                           --no-tpg-tc is not set.  Uses the\n"
                "                           library's bundled font unless\n"
                "                           --burn-font is also given.\n"
                "  --burn-font <PATH>       TrueType font file path\n"
                "                           (default: library's bundled font)\n"
                "  --burn-size <PX>         Burn font size in pixels (default: 36)\n"
                "  --burn-text <TEXT>       Static custom burn text below timecode\n"
                "  --burn-position <POS|list>\n"
                "                           TopLeft | TopCenter | TopRight |\n"
                "                           BottomLeft | BottomCenter | BottomRight |\n"
                "                           Center\n"
                "                           (default: BottomCenter)\n"
                "  --burn-fg <COLOR>        Burn text foreground color (default: white)\n"
                "  --burn-bg <COLOR>        Burn text background color (default: black)\n"
                "                           COLOR accepts hex (#ff8040), rgb(...),\n"
                "                           named (red/white/...), or model notation\n"
                "                           (e.g. sRGB(1,0,0,1)).\n"
                "\n"
                "Playback / display:\n"
                "  --fast                   Play as fast as possible (disables audio)\n"
                "  --no-audio               Do not create an audio output device\n"
                "  --no-display             Do not open an SDL window (headless)\n"
                "  --window-size <WxH>      Initial window size (default: 1280x720)\n"
                "  --duration <SEC>         Stop after N seconds\n"
                "  --frame-count <N>        Stop after N frames (0 = unlimited)\n"
                "  --verbose                Print periodic playback stats\n"
                "\n"
                "File / sequence output:\n"
                "  --output <PATH>          Write to PATH in addition to (or instead\n"
                "                           of) the SDL window.  Determines format by\n"
                "                           extension.  For image sequences, use a\n"
                "                           mask: e.g. out_####.dpx or out_%%04d.dpx.\n"
                "  --seq-head <N>           First frame number for sequence writers\n"
                "                           (default: 0).  Has no effect on single\n"
                "                           files.\n"
                "  --imgseq                 Also write a .imgseq JSON sidecar alongside\n"
                "                           the sequence (image-sequence outputs only).\n"
                "  --imgseq-file <PATH>     Explicit sidecar path; implies --imgseq.\n"
                "  --help                   Show this help text\n");
}

static bool parseOptions(int argc, char **argv, Options &opts) {
        CmdLineParser parser;
        bool helpRequested = false;
        parser.registerOptions({
                {'h', "help",
                 "Show this help text",
                 CmdLineParser::OptionCallback([&]() {
                         helpRequested = true;
                         return 0;
                 })},

                {0, "type",
                 "MediaIO backend name (pass 'list' to list available backends)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listMediaIOBackendsAndExit();
                         opts.type = s;
                         return 0;
                 })},
                {0, "file",
                 "Read from file",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.file = s;
                         return 0;
                 })},
                {0, "rate",
                 "Frame rate",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.rate = s;
                         return 0;
                 })},
                {0, "pattern",
                 "TPG pattern name (pass 'list' to list available patterns)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listEnumAndExit("Video patterns", VideoPattern::Type);
                         opts.pattern = s;
                         return 0;
                 })},
                {0, "size",
                 "Frame size as WxH",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.size = s;
                         return 0;
                 })},
                {0, "pixel-format",
                 "Pixel format name (pass 'list' to list available formats)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listPixelFormatsAndExit();
                         opts.pixelFormat = s;
                         return 0;
                 })},
                {0, "motion",
                 "TPG motion speed",
                 CmdLineParser::OptionDoubleCallback([&](double v) {
                         opts.motion = v;
                         return 0;
                 })},
                {0, "no-tpg-video",
                 "Disable TPG video",
                 CmdLineParser::OptionCallback([&]() {
                         opts.tpgVideo = false;
                         return 0;
                 })},

                {0, "audio-mode",
                 "TPG audio mode (pass 'list' to list available modes)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listEnumAndExit("TPG audio modes", AudioPattern::Type);
                         opts.audioMode = s;
                         return 0;
                 })},
                {0, "audio-rate",
                 "Audio sample rate (Hz)",
                 CmdLineParser::OptionDoubleCallback([&](double v) {
                         opts.audioRate = v;
                         return 0;
                 })},
                {0, "audio-channels",
                 "Audio channel count",
                 CmdLineParser::OptionIntCallback([&](int v) {
                         opts.audioChannels = v;
                         return 0;
                 })},
                {0, "audio-tone",
                 "Tone frequency in Hz",
                 CmdLineParser::OptionDoubleCallback([&](double v) {
                         opts.audioTone = v;
                         return 0;
                 })},
                {0, "audio-level",
                 "Tone level in dBFS",
                 CmdLineParser::OptionDoubleCallback([&](double v) {
                         opts.audioLevel = v;
                         return 0;
                 })},
                {0, "no-tpg-audio",
                 "Disable TPG audio",
                 CmdLineParser::OptionCallback([&]() {
                         opts.tpgAudio = false;
                         return 0;
                 })},

                {0, "no-tpg-tc",
                 "Disable TPG timecode",
                 CmdLineParser::OptionCallback([&]() {
                         opts.tpgTimecode = false;
                         return 0;
                 })},
                {0, "tc-start",
                 "Starting timecode",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.tcStart = s;
                         return 0;
                 })},
                {0, "tc-df",
                 "Drop-frame timecode",
                 CmdLineParser::OptionCallback([&]() {
                         opts.tcDropFrame = true;
                         return 0;
                 })},

                {0, "burn",
                 "Enable text burn-in on the pattern",
                 CmdLineParser::OptionCallback([&]() {
                         opts.burnEnabled = true;
                         return 0;
                 })},
                {0, "burn-font",
                 "TrueType font file for burn-in",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.burnFontPath = s;
                         opts.burnEnabled = true;
                         return 0;
                 })},
                {0, "burn-size",
                 "Burn font size in pixels",
                 CmdLineParser::OptionIntCallback([&](int v) {
                         opts.burnFontSize = v;
                         return 0;
                 })},
                {0, "burn-text",
                 "Static burn text",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.burnText = s;
                         opts.burnEnabled = true;
                         return 0;
                 })},
                {0, "burn-position",
                 "Burn position preset (pass 'list' to list available positions)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         if(s == "list") listEnumAndExit("Burn positions", BurnPosition::Type);
                         opts.burnPosition = s;
                         return 0;
                 })},
                {0, "burn-fg",
                 "Burn text foreground color",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.burnFgColor = s;
                         opts.burnEnabled = true;
                         return 0;
                 })},
                {0, "burn-bg",
                 "Burn text background color",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.burnBgColor = s;
                         opts.burnEnabled = true;
                         return 0;
                 })},

                {0, "fast",
                 "Play as fast as possible",
                 CmdLineParser::OptionCallback([&]() {
                         opts.fast = true;
                         return 0;
                 })},
                {0, "no-audio",
                 "Disable the audio output device",
                 CmdLineParser::OptionCallback([&]() {
                         opts.noAudio = true;
                         return 0;
                 })},
                {0, "no-display",
                 "Do not open an SDL window (headless)",
                 CmdLineParser::OptionCallback([&]() {
                         opts.noDisplay = true;
                         return 0;
                 })},
                {0, "window-size",
                 "Initial window size as WxH",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.windowSize = s;
                         return 0;
                 })},
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
                 "Print periodic playback stats",
                 CmdLineParser::OptionCallback([&]() {
                         opts.verbose = true;
                         return 0;
                 })},

                {0, "output",
                 "Write to PATH (file or sequence mask)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.output = s;
                         return 0;
                 })},
                {0, "seq-head",
                 "Starting frame number for a sequence writer",
                 CmdLineParser::OptionIntCallback([&](int v) {
                         opts.seqHead = v;
                         return 0;
                 })},
                {0, "imgseq",
                 "Write an .imgseq sidecar alongside the sequence",
                 CmdLineParser::OptionCallback([&]() {
                         opts.writeImgSeq = true;
                         return 0;
                 })},
                {0, "imgseq-file",
                 "Explicit path for the .imgseq sidecar",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.imgSeqPath = s;
                         opts.writeImgSeq = true;
                         return 0;
                 })},
        });

        int r = parser.parseMain(argc, argv);
        if(helpRequested) {
                usage();
                exit(0);
        }
        if(r != 0) return false;
        return true;
}

// --------------------------------------------------------------------
// Source construction
// --------------------------------------------------------------------

static MediaIO *buildTpgSource(const Options &opts) {
        auto rateResult = FrameRate::fromString(opts.rate);
        if(rateResult.second().isError()) {
                fprintf(stderr, "Error: invalid --rate '%s'\n", opts.rate.cstr());
                return nullptr;
        }
        FrameRate fps = rateResult.first();

        PixelDesc pd = PixelDesc::lookup(opts.pixelFormat);
        if(!pd.isValid()) {
                fprintf(stderr, "Error: unknown --pixel-format '%s'\n",
                        opts.pixelFormat.cstr());
                return nullptr;
        }

        auto sizeResult = Size2Du32::fromString(opts.size);
        if(sizeResult.second().isError() || !sizeResult.first().isValid()) {
                fprintf(stderr, "Error: invalid --size '%s' (expected WxH)\n",
                        opts.size.cstr());
                return nullptr;
        }
        Size2Du32 size = sizeResult.first();

        MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
        cfg.set(MediaIO::ConfigType, String("TPG"));
        cfg.set(MediaIOTask_TPG::ConfigFrameRate, fps);

        cfg.set(MediaIOTask_TPG::ConfigVideoEnabled, opts.tpgVideo);
        if(opts.tpgVideo) {
                // MediaIOTask_TPG's Variant::asEnum() resolves the String
                // unqualified-name form directly against VideoPattern::Type,
                // so storing the raw CLI string is enough.
                cfg.set(MediaIOTask_TPG::ConfigVideoPattern, opts.pattern);
                cfg.set(MediaIOTask_TPG::ConfigVideoSize, size);
                cfg.set(MediaIOTask_TPG::ConfigVideoPixelFormat, pd);
                cfg.set(MediaIOTask_TPG::ConfigVideoMotion, opts.motion);
        }

        cfg.set(MediaIOTask_TPG::ConfigVideoBurnEnabled, opts.burnEnabled);
        if(opts.burnEnabled) {
                // An empty burnFontPath is passed through verbatim —
                // MediaIOTask_TPG / VideoTestPattern / FastFont
                // substitute the library's bundled default internally.
                cfg.set(MediaIOTask_TPG::ConfigVideoBurnFontPath, opts.burnFontPath);
                cfg.set(MediaIOTask_TPG::ConfigVideoBurnFontSize, opts.burnFontSize);
                cfg.set(MediaIOTask_TPG::ConfigVideoBurnText, opts.burnText);
                cfg.set(MediaIOTask_TPG::ConfigVideoBurnPosition, opts.burnPosition);

                if(!opts.burnFgColor.isEmpty()) {
                        Color fg = Color::fromString(opts.burnFgColor);
                        if(!fg.isValid()) {
                                fprintf(stderr, "Error: invalid --burn-fg color '%s'\n",
                                        opts.burnFgColor.cstr());
                                return nullptr;
                        }
                        cfg.set(MediaIOTask_TPG::ConfigVideoBurnTextColor, fg);
                }
                if(!opts.burnBgColor.isEmpty()) {
                        Color bg = Color::fromString(opts.burnBgColor);
                        if(!bg.isValid()) {
                                fprintf(stderr, "Error: invalid --burn-bg color '%s'\n",
                                        opts.burnBgColor.cstr());
                                return nullptr;
                        }
                        cfg.set(MediaIOTask_TPG::ConfigVideoBurnBgColor, bg);
                }
        }

        bool audioEnabled = opts.tpgAudio && !opts.fast;
        cfg.set(MediaIOTask_TPG::ConfigAudioEnabled, audioEnabled);
        if(audioEnabled) {
                cfg.set(MediaIOTask_TPG::ConfigAudioMode, opts.audioMode);
                cfg.set(MediaIOTask_TPG::ConfigAudioRate, (float)opts.audioRate);
                cfg.set(MediaIOTask_TPG::ConfigAudioChannels, opts.audioChannels);
                cfg.set(MediaIOTask_TPG::ConfigAudioToneFrequency, opts.audioTone);
                cfg.set(MediaIOTask_TPG::ConfigAudioToneLevel, opts.audioLevel);
        }

        cfg.set(MediaIOTask_TPG::ConfigTimecodeEnabled, opts.tpgTimecode);
        if(opts.tpgTimecode) {
                cfg.set(MediaIOTask_TPG::ConfigTimecodeStart, opts.tcStart);
                cfg.set(MediaIOTask_TPG::ConfigTimecodeDropFrame, opts.tcDropFrame);
        }

        return MediaIO::create(cfg);
}

static MediaIO *buildSource(const Options &opts) {
        if(!opts.file.isEmpty()) {
                return MediaIO::createForFileRead(opts.file);
        }
        if(opts.type == "TPG") {
                return buildTpgSource(opts);
        }
        // Generic path: just create with a Type set in the config and
        // let the backend pick its own defaults.
        MediaIO::Config cfg = MediaIO::defaultConfig(opts.type);
        cfg.set(MediaIO::ConfigType, opts.type);
        return MediaIO::create(cfg);
}

// --------------------------------------------------------------------
// Sink construction
// --------------------------------------------------------------------
//
// A "sink" is anywhere mediaplay can deliver frames: the SDL player,
// a file, an image sequence, and eventually network/device backends.
// Every sink is a MediaIO writer.  The main loop pumps each read
// frame into every enabled sink, so adding a new sink type is just a
// matter of creating the MediaIO, wiring it with the source's
// descriptor, and pushing it onto the list.
struct Sink {
        MediaIO     *io    = nullptr;
        String       name;          // human-readable label for logging
        bool         paced = false; // true when the sink enforces real-time
};

// --------------------------------------------------------------------
// Pipeline — signal-driven source-to-sink pump
// --------------------------------------------------------------------
//
// The Pipeline owns the full media path: one source MediaIO and a
// list of sink MediaIOs.  It runs entirely off MediaIO's signals —
// @c frameReady from the source drains the read queue; any sink's
// @c writeError aborts the pipeline.  There is no dedicated pumper
// thread: all work happens on the main thread's EventLoop in
// response to signals that the MediaIO strand workers emit on their
// own threads and the signal/slot system marshals to us.
//
// Shutdown paths:
//  - Source reaches EOF → slot calls Application::quit(0).
//  - Source read error → log, quit(1).
//  - Any sink write error → log, quit(1).
//  - Frame count reached → quit(0).
//  - External quit (window close, Ctrl-C, duration timer) sets the
//    Application's shouldQuit flag; the next pump iteration observes
//    that the EventLoop is winding down and stops issuing reads.
class Pipeline : public ObjectBase {
        PROMEKI_OBJECT(Pipeline, ObjectBase)
        public:
                /// Maximum number of non-blocking writes that may be
                /// outstanding on any single sink before drain() stops
                /// pulling new frames from the source.  Keeps the sink
                /// strand queue short enough to bound memory when the
                /// source (e.g. DPX sequence) can produce frames much
                /// faster than a paced sink can consume them.  A few
                /// frames of headroom let the strand worker stay busy
                /// without starving when the sink is idle.
                static constexpr int MaxInflightPerSink = 4;

                Pipeline(MediaIO *source,
                         List<Sink> sinks,
                         int64_t frameCountLimit,
                         ObjectBase *parent = nullptr)
                        : ObjectBase(parent),
                          _source(source),
                          _sinks(std::move(sinks)),
                          _frameCountLimit(frameCountLimit),
                          _mainLoop(EventLoop::current())
                {
                        // One in-flight counter per sink; all mutations
                        // happen on the main thread (drain() increments;
                        // the posted frameWanted callable decrements), so
                        // plain ints are fine.
                        _inflight.resize(_sinks.size(), 0);

                        // React to every read completion (success, EOF, error).
                        ObjectBase::connect(&_source->frameReadySignal,
                                            &onFrameReadySlot);

                        // Hook each sink for cross-thread write
                        // notifications.  Both signals fire from the
                        // strand worker thread; we attach raw lambdas
                        // that stash state and post a parameter-free
                        // callable back to the main loop.
                        //
                        //  - writeErrorSignal: records the error and
                        //    triggers pipeline finalisation.
                        //  - frameWantedSignal: drops the sink's
                        //    in-flight count and reopens the drain, so
                        //    back-pressure throttles source reads to
                        //    the sink's consumption rate.
                        for(size_t i = 0; i < _sinks.size(); ++i) {
                                MediaIO *sinkIO = _sinks[i].io;
                                sinkIO->writeErrorSignal.connect(
                                        [this](Error err) {
                                                this->reportWriteError(err);
                                        },
                                        this);
                                sinkIO->frameWantedSignal.connect(
                                        [this, i]() {
                                                this->reportFrameWanted(i);
                                        },
                                        this);
                        }
                }

                /**
                 * @brief Kicks off the pipeline.
                 *
                 * Primes the source's prefetch queue by calling
                 * @c readFrame(..., false) once; subsequent work is
                 * driven entirely by @c frameReady signals.
                 */
                void start() {
                        drain();
                }

                uint64_t framesPumped() const { return _framesPumped; }

                /**
                 * @brief Exposes the owned sink list for shutdown
                 *        bookkeeping (cancel/close/delete).
                 */
                const List<Sink> &sinks() const { return _sinks; }
                List<Sink> &sinks() { return _sinks; }

                PROMEKI_SLOT(onFrameReady);
                PROMEKI_SLOT(onWriteErrorPosted);

        private:
                // Called from the strand worker thread when any sink
                // emits writeErrorSignal.  Stashes the error and posts
                // a callable that re-enters the pipeline on the main
                // thread to finalize shutdown.
                void reportWriteError(Error err) {
                        _writeErrorPending.setValue(true);
                        _writeError = err;
                        if(_mainLoop != nullptr) {
                                _mainLoop->postCallable([this]() {
                                        this->onWriteErrorPosted();
                                });
                        }
                }

                // Called from the strand worker thread when a sink
                // emits frameWantedSignal.  Posts a main-thread
                // callable that drops the in-flight count for that
                // sink and reopens the drain — this is the sole
                // back-pressure release path.
                void reportFrameWanted(size_t sinkIndex) {
                        if(_mainLoop != nullptr) {
                                _mainLoop->postCallable([this, sinkIndex]() {
                                        this->onFrameWantedPosted(sinkIndex);
                                });
                        }
                }

                // Main-thread half of the frameWanted path: decrement
                // the sink's in-flight counter and try to pump more
                // frames.  Safe to invoke after finish(): drain() is
                // a no-op once _finished is set.
                void onFrameWantedPosted(size_t sinkIndex) {
                        if(sinkIndex < _inflight.size() &&
                           _inflight[sinkIndex] > 0) {
                                _inflight[sinkIndex]--;
                        }
                        drain();
                }

                // Non-blocking drain: repeatedly read from the source
                // and hand the frame to every sink until we run out of
                // ready data, hit a terminal condition, or reach the
                // frame-count limit.  Also stops when any sink has
                // reached MaxInflightPerSink outstanding writes; the
                // next frameWanted callback will reopen the drain.
                void drain() {
                        if(_finished) return;
                        while(true) {
                                if(_frameCountLimit > 0 &&
                                   _framesPumped >=
                                       static_cast<uint64_t>(_frameCountLimit)) {
                                        finish(0);
                                        return;
                                }

                                // Back-pressure gate: if any sink's
                                // strand already has MaxInflightPerSink
                                // writes queued, stop reading from the
                                // source.  Without this, a fast source
                                // (e.g. DPX sequence at >100 fps) will
                                // pile frames into a paced sink's
                                // unbounded Strand queue and exhaust
                                // memory.  A sink draining one frame
                                // posts a frameWanted callable that
                                // drops its count and re-enters drain().
                                for(size_t i = 0; i < _sinks.size(); ++i) {
                                        if(_inflight[i] >= MaxInflightPerSink) {
                                                return;
                                        }
                                }

                                Frame::Ptr frame;
                                Error err = _source->readFrame(frame, false);
                                if(err == Error::TryAgain) {
                                        // Nothing ready yet — wait for the
                                        // next frameReady signal.
                                        return;
                                }
                                if(err == Error::EndOfFile) {
                                        fprintf(stdout, "Source reached EOF.\n");
                                        finish(0);
                                        return;
                                }
                                if(err.isError()) {
                                        if(err != Error::Cancelled) {
                                                fprintf(stderr,
                                                        "Source read error: %s\n",
                                                        err.name().cstr());
                                        }
                                        finish(1);
                                        return;
                                }

                                // Fan out to every sink.  Non-blocking
                                // writes always return TryAgain; errors
                                // surface later via writeErrorSignal and
                                // successful completions surface later
                                // via frameWantedSignal.  Increment the
                                // in-flight counter before dispatch so a
                                // fast worker that emits frameWanted on
                                // the same thread doesn't underflow us.
                                for(size_t i = 0; i < _sinks.size(); ++i) {
                                        _inflight[i]++;
                                        _sinks[i].io->writeFrame(frame, false);
                                }
                                _framesPumped++;
                        }
                }

                // Finalize the pipeline exactly once.  Further drain
                // calls after this become no-ops so a late frameReady
                // doesn't reopen the path.
                void finish(int rc) {
                        if(_finished) return;
                        _finished = true;
                        Application::quit(rc);
                }

                MediaIO                *_source = nullptr;
                List<Sink>              _sinks;
                // Parallel to _sinks: number of non-blocking writes
                // currently in flight (submitted but neither completed
                // nor errored) for each sink.  Only touched on the
                // main thread.
                List<int>               _inflight;
                int64_t                 _frameCountLimit = 0;
                uint64_t                _framesPumped = 0;
                bool                    _finished = false;
                EventLoop              *_mainLoop = nullptr;
                Atomic<bool>            _writeErrorPending{false};
                Error                   _writeError;
};

void Pipeline::onFrameReady() {
        drain();
}

void Pipeline::onWriteErrorPosted() {
        // The cross-thread post already bounced us onto the main
        // thread.  Read the stashed error, log it, and finalize.
        Error err = _writeError;
        if(err != Error::Cancelled) {
                fprintf(stderr, "Sink write error: %s\n", err.name().cstr());
        }
        finish(1);
}

// Build a file / sequence output sink.  Returns nullptr on failure.
// Sets the task's ConfigSequenceHead when the output looks like a
// sequence mask (contains # or %d), otherwise writes a single file
// per writeFrame() call.
static MediaIO *buildFileSink(const Options &opts,
                              const MediaDesc &srcDesc,
                              const AudioDesc &srcAudioDesc,
                              const Metadata &srcMetadata) {
        MediaIO *io = MediaIO::createForFileWrite(opts.output);
        if(io == nullptr) {
                fprintf(stderr,
                        "Error: no writable MediaIO backend for '%s'\n",
                        opts.output.cstr());
                return nullptr;
        }

        // For sequence output, inject the starting frame number.  For
        // single-file output the key is simply ignored by the task.
        MediaIO::Config cfg = io->config();
        cfg.set(MediaIOTask_ImageFile::ConfigSequenceHead, opts.seqHead);
        io->setConfig(cfg);

        io->setMediaDesc(srcDesc);
        if(srcAudioDesc.isValid()) {
                io->setAudioDesc(srcAudioDesc);
        }
        if(!srcMetadata.isEmpty()) {
                io->setMetadata(srcMetadata);
        }
        return io;
}

// Returns true when the --output path looks like an image-sequence
// mask (either hash-style or printf-style).  Single files and
// .imgseq sidecars return false.
static bool outputIsSequenceMask(const String &path) {
        FilePath fp(path);
        NumName n = NumName::fromMask(fp.fileName());
        return n.isValid();
}

// Derives a sidecar .imgseq path from a sequence mask.  Strips
// trailing separator characters (underscore, dot, dash, space) from
// the pattern's prefix and places "<stem>.imgseq" in the same
// directory.  If the prefix is empty, uses "sequence".
static FilePath deriveSidecarPath(const String &maskPath) {
        FilePath fp(maskPath);
        NumName n = NumName::fromMask(fp.fileName());
        String stem = n.prefix();
        while(!stem.isEmpty()) {
                char c = stem[stem.size() - 1];
                if(c != '_' && c != '.' && c != '-' && c != ' ') break;
                stem = stem.left(stem.size() - 1);
        }
        if(stem.isEmpty()) stem = "sequence";
        FilePath dir = fp.parent();
        if(dir.isEmpty()) dir = FilePath(".");
        return dir / (stem + ".imgseq");
}

// Writes a .imgseq sidecar describing a sequence that was just
// produced.  Snapshots range, format and rate from the actual frames
// written so downstream tools see an accurate descriptor.
static Error writeImgSeqSidecar(const FilePath &path,
                                const String &maskPath,
                                const MediaDesc &mediaDesc,
                                int seqHead,
                                int64_t frameCount) {
        NumName pattern = NumName::fromMask(FilePath(maskPath).fileName());
        if(!pattern.isValid()) return Error::Invalid;
        if(frameCount <= 0) {
                fprintf(stderr,
                        "Warning: skipping .imgseq sidecar — no frames written.\n");
                return Error::Ok;
        }

        ImgSeq seq;
        seq.setName(pattern);
        seq.setHead(static_cast<size_t>(seqHead));
        seq.setTail(static_cast<size_t>(seqHead + frameCount - 1));
        if(mediaDesc.frameRate().isValid()) {
                seq.setFrameRate(mediaDesc.frameRate());
        }
        if(!mediaDesc.imageList().isEmpty()) {
                const ImageDesc &id = mediaDesc.imageList()[0];
                seq.setVideoSize(id.size());
                seq.setPixelDesc(id.pixelDesc());
        }
        return seq.save(path);
}

// --------------------------------------------------------------------
// Main
// --------------------------------------------------------------------

int main(int argc, char **argv) {
        Options opts;
        if(!parseOptions(argc, argv, opts)) {
                fprintf(stderr, "Use --help for usage information.\n");
                return 1;
        }

        SDLApplication app(argc, argv);

        // --- Build source ---
        MediaIO *source = buildSource(opts);
        if(source == nullptr) {
                fprintf(stderr, "Error: failed to build source MediaIO\n");
                return 1;
        }

        Error err = source->open(MediaIO::Reader);
        if(err.isError()) {
                fprintf(stderr, "Error: source open failed: %s\n",
                        err.name().cstr());
                delete source;
                return 1;
        }

        MediaDesc srcDesc = source->mediaDesc();
        AudioDesc srcAudioDesc = source->audioDesc();
        FrameRate srcRate = source->frameRate();
        fprintf(stdout, "Source: %s  %s @ %s fps\n",
                opts.file.isEmpty() ? opts.type.cstr() : opts.file.cstr(),
                srcDesc.imageList().isEmpty() ? "(no video)"
                        : srcDesc.imageList()[0].toString().cstr(),
                srcRate.toString().cstr());
        if(srcAudioDesc.isValid()) {
                fprintf(stdout, "  Audio: %s\n", srcAudioDesc.toString().cstr());
        }

        const Metadata srcMetadata = source->metadata();

        // --- Build sinks ---
        //
        // Sinks are kept in a generic list so the pumping loop below
        // doesn't need to know which backends are active.  Future
        // additions (network sinks, capture card out, etc.) only need
        // to push another Sink onto this list.
        List<Sink> sinks;

        // Common cleanup used on early-error exits below.
        auto cleanupAndFail = [&](int rc) {
                for(auto it = sinks.begin(); it != sinks.end(); ++it) {
                        if(it->io != nullptr) {
                                it->io->close();
                                delete it->io;
                        }
                }
                source->close();
                delete source;
                return rc;
        };

        // SDL UI objects live at this scope so we can delete them on
        // exit regardless of whether the player sink was created.
        SDLWindow       *window      = nullptr;
        SDLVideoWidget  *videoWidget = nullptr;
        SDLAudioOutput  *audioOutput = nullptr;

        // --- File / sequence sink ---
        //
        // Opened first because it is the simpler of the two and lets
        // us bail out before touching SDL at all if the caller
        // mistyped an output path.
        bool outputIsSeq = false;
        if(!opts.output.isEmpty()) {
                MediaIO *fileSink = buildFileSink(opts, srcDesc, srcAudioDesc,
                                                  srcMetadata);
                if(fileSink == nullptr) {
                        return cleanupAndFail(1);
                }
                err = fileSink->open(MediaIO::Writer);
                if(err.isError()) {
                        fprintf(stderr, "Error: output '%s' open failed: %s\n",
                                opts.output.cstr(), err.name().cstr());
                        delete fileSink;
                        return cleanupAndFail(1);
                }
                sinks.pushToBack(Sink{fileSink, String("file:") + opts.output, false});
                outputIsSeq = outputIsSequenceMask(opts.output);
                fprintf(stdout, "Output: %s%s\n",
                        opts.output.cstr(),
                        outputIsSeq ? "  (image sequence)" : "");
        }

        // --- SDL player sink ---
        if(!opts.noDisplay) {
                auto winSizeResult = Size2Du32::fromString(opts.windowSize);
                if(winSizeResult.second().isError() || !winSizeResult.first().isValid()) {
                        fprintf(stderr, "Error: invalid --window-size '%s' (expected WxH)\n",
                                opts.windowSize.cstr());
                        return cleanupAndFail(1);
                }
                Size2Du32 winSize = winSizeResult.first();
                window = new SDLWindow("mediaplay",
                                       (int)winSize.width(),
                                       (int)winSize.height());
                videoWidget = new SDLVideoWidget(window);
                videoWidget->setGeometry(Rect2Di32(0, 0,
                                                   (int)winSize.width(),
                                                   (int)winSize.height()));
                window->resizedSignal.connect([videoWidget](Size2Di32 sz) {
                        videoWidget->setGeometry(Rect2Di32(0, 0, sz.width(), sz.height()));
                });
                window->show();

                if(!opts.noAudio && !opts.fast && srcAudioDesc.isValid()) {
                        audioOutput = new SDLAudioOutput();
                }

                MediaIO *player = createSDLPlayer(videoWidget, audioOutput, !opts.fast);
                if(player == nullptr) {
                        fprintf(stderr, "Error: createSDLPlayer failed\n");
                        delete audioOutput; audioOutput = nullptr;
                        delete window; window = nullptr;
                        return cleanupAndFail(1);
                }
                player->setMediaDesc(srcDesc);
                if(srcAudioDesc.isValid()) {
                        player->setAudioDesc(srcAudioDesc);
                }
                err = player->open(MediaIO::Writer);
                if(err.isError()) {
                        fprintf(stderr, "Error: player open failed: %s\n",
                                err.name().cstr());
                        delete player;
                        delete audioOutput; audioOutput = nullptr;
                        delete window; window = nullptr;
                        return cleanupAndFail(1);
                }
                sinks.pushToBack(Sink{player, String("sdl"), !opts.fast});
        }

        if(sinks.isEmpty()) {
                fprintf(stderr,
                        "Error: no sinks configured.  Give --output and/or drop --no-display.\n");
                source->close();
                delete source;
                return 1;
        }

        // --- Signal-driven pipeline ---
        //
        // The Pipeline is a small ObjectBase that connects to the
        // source's frameReadySignal and each sink's writeErrorSignal.
        // Frame movement happens on the main thread's EventLoop: the
        // MediaIO strand worker emits frameReady after every read
        // completion, and the signal/slot system marshals that into a
        // slot call here.  No dedicated pumper thread, no manual
        // wake-up calls — the EventLoop's wake callback (set by
        // SDLApplication) handles SDL pump wake-up automatically.
        Pipeline pipeline(source, sinks, opts.frameCount);
        // Note: sinks list has been moved into the Pipeline; the
        // local 'sinks' variable is no longer the owning list.
        // We still need references to the raw pointers for cleanup
        // below, so walk the Pipeline's internal list instead via
        // the helper accessors we expose post-exec.
        pipeline.start();

        // --- Main-thread event loop, with optional timers ---
        SDLApplication *sdlApp = SDLApplication::instance();

        // Close the window → quit the app (only if the window exists).
        if(window != nullptr) {
                window->closedSignal.connect([sdlApp]() {
                        if(sdlApp != nullptr) sdlApp->quit(0);
                });
        }

        // Duration timeout.
        if(opts.duration > 0.0) {
                sdlApp->eventLoop().startTimer(
                        static_cast<unsigned int>(opts.duration * 1000.0),
                        [sdlApp]() { sdlApp->quit(0); },
                        true);
        }

        // Verbose stats (every 2 s).
        if(opts.verbose) {
                sdlApp->eventLoop().startTimer(2000, [&]() {
                        fprintf(stdout, "[mediaplay] pumped=%lu\n",
                                (unsigned long)pipeline.framesPumped());
                });
        }

        // Ctrl-C / SIGTERM cleanly stop the app.  The EventLoop wake
        // callback wired by SDLApplication takes care of pumping the
        // SDL event loop out of SDL_WaitEvent when quit is called.
        std::signal(SIGINT,  [](int) { Application::quit(0); });
        std::signal(SIGTERM, [](int) { Application::quit(0); });

        int rc = app.exec();

        // --- Shut down ---
        //
        // Cancel any in-flight strand work and close every MediaIO.
        // The Pipeline still holds the sink list, so we walk it via
        // the pipelineSinks() accessor to drive cleanup.
        source->cancelPending();
        for(auto it = pipeline.sinks().begin();
                 it != pipeline.sinks().end(); ++it) {
                it->io->cancelPending();
        }

        const uint64_t totalWritten = pipeline.framesPumped();

        for(auto it = pipeline.sinks().begin();
                 it != pipeline.sinks().end(); ++it) {
                it->io->close();
        }
        source->close();

        // --- Optional .imgseq sidecar ---
        //
        // Written after the sink is closed so we have the final frame
        // count, and so a downstream reader that re-opens the sidecar
        // sees the complete sequence on disk.
        if(opts.writeImgSeq) {
                if(opts.output.isEmpty()) {
                        fprintf(stderr,
                                "Warning: --imgseq requires --output; ignoring.\n");
                } else if(!outputIsSeq) {
                        fprintf(stderr,
                                "Warning: --imgseq ignored — output '%s' is not a sequence mask.\n",
                                opts.output.cstr());
                } else {
                        FilePath sidecar;
                        if(!opts.imgSeqPath.isEmpty()) {
                                sidecar = FilePath(opts.imgSeqPath);
                        } else {
                                sidecar = deriveSidecarPath(opts.output);
                        }
                        Error sErr = writeImgSeqSidecar(sidecar, opts.output,
                                                        srcDesc, opts.seqHead,
                                                        static_cast<int64_t>(totalWritten));
                        if(sErr.isError()) {
                                fprintf(stderr,
                                        "Warning: failed to write sidecar '%s': %s\n",
                                        sidecar.toString().cstr(), sErr.name().cstr());
                        } else {
                                fprintf(stdout, "Wrote sidecar: %s\n",
                                        sidecar.toString().cstr());
                        }
                }
        }

        for(auto it = pipeline.sinks().begin();
                 it != pipeline.sinks().end(); ++it) {
                delete it->io;
        }
        pipeline.sinks().clear();
        delete source;
        delete audioOutput;
        delete window;

        return rc;
}
