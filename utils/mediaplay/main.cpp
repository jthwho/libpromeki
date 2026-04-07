/**
 * @file      mediaplay/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Simple media player built on top of the MediaIO framework.  Reads
 * frames from any MediaIO source (selected via command-line options)
 * and writes them into an SDL-backed player MediaIO that displays the
 * video and plays the audio.  Primarily used to exercise the SDL
 * player backend with the test pattern generator.
 */

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <promeki/application.h>
#include <promeki/cmdlineparser.h>
#include <promeki/color.h>
#include <promeki/error.h>
#include <promeki/framerate.h>
#include <promeki/logger.h>
#include <promeki/mediaio.h>
#include <promeki/mediaiotask_tpg.h>
#include <promeki/mediadesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/frame.h>

#include <promeki/sdl/sdlapplication.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>

using namespace promeki;

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
        String  pattern         = "colorbars";
        double  motion          = 0.0;
        bool    tpgVideo        = true;

        // TPG audio
        bool    tpgAudio        = true;
        String  audioMode       = "tone";
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
        String  burnPosition    = "bottomcenter";
        String  burnFgColor;    // empty = use VideoTestPattern default (white)
        String  burnBgColor;    // empty = use VideoTestPattern default (black)

        // Playback
        bool    fast            = false;
        bool    noAudio         = false;
        String  windowSize      = "1280x720";
        double  duration        = 0.0;
        bool    verbose         = false;
        bool    listTypes       = false;
};

static void usage() {
        fprintf(stderr,
                "Usage: mediaplay [OPTIONS]\n"
                "\n"
                "Reads frames from a MediaIO source and plays them back through\n"
                "an SDL window (and audio device).  Mostly used to exercise the\n"
                "SDLPlayer MediaIO backend with the test pattern generator.\n"
                "\n"
                "Source selection:\n"
                "  --type <NAME>            MediaIO backend name (default: TPG)\n"
                "  --file <PATH>            Read from file (infers type from extension)\n"
                "  --list-types             List registered MediaIO backends and exit\n"
                "\n"
                "TPG video:\n"
                "  --rate <R>               Frame rate (default: 29.97)\n"
                "  --pattern <P>            Test pattern (default: colorbars)\n"
                "  --size <WxH>             Frame size (default: 1280x720)\n"
                "  --pixel-format <F>       Pixel format name (default: RGB8_sRGB)\n"
                "  --motion <S>             Motion speed (default: 0.0)\n"
                "  --no-tpg-video           Disable TPG video generation\n"
                "\n"
                "TPG audio:\n"
                "  --audio-mode <M>         tone | silence | ltc (default: tone)\n"
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
                "                           bundled FiraCode font unless\n"
                "                           --burn-font is also given.\n"
                "  --burn-font <PATH>       TrueType font file path\n"
                "                           (default: etc/fonts/FiraCodeNerdFontMono-Regular.ttf)\n"
                "  --burn-size <PX>         Burn font size in pixels (default: 36)\n"
                "  --burn-text <TEXT>       Static custom burn text below timecode\n"
                "  --burn-position <POS>    topleft | topcenter | topright |\n"
                "                           bottomleft | bottomcenter | bottomright\n"
                "                           (default: bottomcenter)\n"
                "  --burn-fg <COLOR>        Burn text foreground color (default: white)\n"
                "  --burn-bg <COLOR>        Burn text background color (default: black)\n"
                "                           COLOR accepts hex (#ff8040), rgb(...),\n"
                "                           named (red/white/...), or model notation\n"
                "                           (e.g. sRGB(1,0,0,1)).\n"
                "\n"
                "Playback:\n"
                "  --fast                   Play as fast as possible (disables audio)\n"
                "  --no-audio               Do not create an audio output device\n"
                "  --window-size <WxH>      Initial window size (default: 1280x720)\n"
                "  --duration <SEC>         Stop after N seconds\n"
                "  --verbose                Print periodic playback stats\n"
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
                 "MediaIO backend name",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.type = s;
                         return 0;
                 })},
                {0, "file",
                 "Read from file",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.file = s;
                         return 0;
                 })},
                {0, "list-types",
                 "List registered MediaIO backends and exit",
                 CmdLineParser::OptionCallback([&]() {
                         opts.listTypes = true;
                         return 0;
                 })},

                {0, "rate",
                 "Frame rate",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
                         opts.rate = s;
                         return 0;
                 })},
                {0, "pattern",
                 "TPG pattern name",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
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
                 "Pixel format name",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
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
                 "TPG audio mode (tone, silence, ltc)",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
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
                 "Burn position preset",
                 CmdLineParser::OptionStringCallback([&](const String &s) {
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
                {0, "verbose",
                 "Print periodic playback stats",
                 CmdLineParser::OptionCallback([&]() {
                         opts.verbose = true;
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
                cfg.set(MediaIOTask_TPG::ConfigVideoPattern, opts.pattern);
                cfg.set(MediaIOTask_TPG::ConfigVideoSize, size);
                cfg.set(MediaIOTask_TPG::ConfigVideoPixelFormat, pd);
                cfg.set(MediaIOTask_TPG::ConfigVideoMotion, opts.motion);
        }

        cfg.set(MediaIOTask_TPG::ConfigVideoBurnEnabled, opts.burnEnabled);
        if(opts.burnEnabled) {
                String fontPath = opts.burnFontPath;
                if(fontPath.isEmpty()) {
                        // Fall back to the bundled FiraCode font shipped
                        // in etc/fonts.  Matches the behavior of vidgen,
                        // imgtest, and testrender.
                        fontPath = String(PROMEKI_SOURCE_DIR)
                                 + "/etc/fonts/FiraCodeNerdFontMono-Regular.ttf";
                }
                cfg.set(MediaIOTask_TPG::ConfigVideoBurnFontPath, fontPath);
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
// Main
// --------------------------------------------------------------------

int main(int argc, char **argv) {
        Options opts;
        if(!parseOptions(argc, argv, opts)) {
                fprintf(stderr, "Use --help for usage information.\n");
                return 1;
        }

        SDLApplication app(argc, argv);

        if(opts.listTypes) {
                fprintf(stdout, "Registered MediaIO backends:\n");
                for(const auto &desc : MediaIO::registeredFormats()) {
                        fprintf(stdout, "  %-20s %s\n",
                                desc.name.cstr(), desc.description.cstr());
                }
                return 0;
        }

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

        // --- Build SDL UI ---
        auto winSizeResult = Size2Du32::fromString(opts.windowSize);
        if(winSizeResult.second().isError() || !winSizeResult.first().isValid()) {
                fprintf(stderr, "Error: invalid --window-size '%s' (expected WxH)\n",
                        opts.windowSize.cstr());
                source->close();
                delete source;
                return 1;
        }
        Size2Du32 winSize = winSizeResult.first();
        SDLWindow *window = new SDLWindow("mediaplay",
                                          (int)winSize.width(),
                                          (int)winSize.height());
        SDLVideoWidget *videoWidget = new SDLVideoWidget(window);
        videoWidget->setGeometry(Rect2Di32(0, 0,
                                           (int)winSize.width(),
                                           (int)winSize.height()));
        window->resizedSignal.connect([videoWidget](Size2Di32 sz) {
                videoWidget->setGeometry(Rect2Di32(0, 0, sz.width(), sz.height()));
        });
        window->show();

        SDLAudioOutput *audioOutput = nullptr;
        if(!opts.noAudio && !opts.fast && srcAudioDesc.isValid()) {
                audioOutput = new SDLAudioOutput();
        }

        // --- Build player ---
        MediaIO *player = createSDLPlayer(videoWidget, audioOutput, !opts.fast);
        if(player == nullptr) {
                fprintf(stderr, "Error: createSDLPlayer failed\n");
                source->close();
                delete source;
                delete audioOutput;
                delete window;
                return 1;
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
                source->close();
                delete source;
                delete audioOutput;
                delete window;
                return 1;
        }

        // --- Run pumping thread ---
        //
        // Pumping happens on a worker thread so blocking calls (prefetch,
        // audio-led pacing) don't stall the SDL main loop.  The main
        // thread runs SDLApplication::exec() for event dispatch and
        // (via the render callback posted by the player task) repaint.
        std::atomic<bool> running{true};
        std::atomic<uint64_t> framesPumped{0};
        std::thread pumper([&]() {
                while(running.load()) {
                        Frame::Ptr frame;
                        Error rerr = source->readFrame(frame);
                        if(rerr.isError()) {
                                if(rerr == Error::EndOfFile) {
                                        fprintf(stdout, "Source reached EOF.\n");
                                } else if(rerr != Error::Cancelled) {
                                        fprintf(stderr,
                                                "Source read error: %s\n",
                                                rerr.name().cstr());
                                }
                                running.store(false);
                                break;
                        }
                        Error werr = player->writeFrame(frame);
                        if(werr.isError() && werr != Error::Cancelled) {
                                fprintf(stderr,
                                        "Player write error: %s\n",
                                        werr.name().cstr());
                                running.store(false);
                                break;
                        }
                        framesPumped.fetch_add(1);
                }
                // EOF or error — wake the main thread so exec() can exit.
                Application::quit(0);
        });

        // --- Main-thread event loop, with optional timers ---
        SDLApplication *sdlApp = SDLApplication::instance();

        // Close the window → quit the app.
        window->closedSignal.connect([sdlApp]() {
                if(sdlApp != nullptr) sdlApp->quit(0);
        });

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
                                (unsigned long)framesPumped.load());
                });
        }

        // Ctrl-C cleanly stops the app.
        std::signal(SIGINT, [](int) { Application::quit(0); });
        std::signal(SIGTERM, [](int) { Application::quit(0); });

        int rc = app.exec();

        // --- Shut down ---
        running.store(false);
        source->cancelPending();
        player->cancelPending();
        if(pumper.joinable()) pumper.join();

        player->close();
        source->close();

        delete player;
        delete source;
        delete audioOutput;
        delete window;

        return rc;
}
