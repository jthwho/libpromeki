/**
 * @file      mediaplay/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * General-purpose media pump built on the MediaIO framework.  The CLI
 * is a thin wrapper over `MediaConfig`: one `--in` picks the source
 * backend, an optional `--convert` stage inserts a
 * `MediaIOTask_Converter` between input and outputs, and any number
 * of `--out` flags register sink backends.  Each stage is configured
 * via repeatable `--ic` / `--cc` / `--oc Key:Value` options
 * whose values are type-coerced against the backend's default config
 * via the Variant type system — see `stage.cpp` for the dispatcher.
 *
 * This file is deliberately thin: it wires the CLI to the stage
 * builders, builds the SDL UI when a `SDL` output is requested,
 * constructs the Pipeline, runs the event loop, then cleans up.
 * Anything reusable lives in one of the split modules:
 *
 *   - `cli.{h,cpp}`       — Options, parser, help text, schema dump
 *   - `stage.{h,cpp}`     — StageSpec, value parser, stage builders
 *   - `pipeline.{h,cpp}`  — Pipeline + Sink
 */

#include <cstdio>

#include <promeki/application.h>
#include <promeki/audiodesc.h>
#include <promeki/benchmarkreporter.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/list.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/memspace.h>
#include <promeki/metadata.h>
#include <promeki/rect.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/thread.h>

#include <promeki/sdl/sdlapplication.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>

#include "cli.h"
#include "pipeline.h"
#include "stage.h"

using namespace promeki;
using namespace mediaplay;

namespace {

/**
 * @brief One row in the --stats output.
 *
 * Remembered per stage so the main-loop timer can walk the list on
 * every tick without having to re-discover the stages, and so the
 * BenchmarkReporters live long enough to accumulate between ticks.
 */
struct StatsSlot {
        MediaIO           *io       = nullptr;
        String             label;
        BenchmarkReporter *reporter = nullptr;
};

/**
 * @brief Flips EnableBenchmark on a stage and attaches a reporter.
 *
 * Safe to call immediately after a stage has been built and before
 * @c open() — the config key is read in @c resolveIdentifiersAndBenchmark
 * which runs at the top of @c open().  The reporter is heap-owned by
 * the caller and must outlive the MediaIO.
 *
 * @param io       The stage to enable benchmarking on.
 * @param label    Human label used for stats printing.
 * @param slots    Slot list to append to.
 */
void attachStatsReporter(MediaIO *io, const String &label,
                         List<StatsSlot> &slots) {
        if(io == nullptr) return;
        MediaIO::Config cfg = io->config();
        cfg.set(MediaConfig::EnableBenchmark, true);
        io->setConfig(cfg);
        auto *reporter = new BenchmarkReporter();
        io->setBenchmarkReporter(reporter);
        slots.pushToBack(StatsSlot{io, label, reporter});
}

/**
 * @brief Prints one line per stage with the current telemetry snapshot.
 *
 * Called from the main-loop --stats timer.  Delegates formatting to
 * @ref MediaIOStats::toString — everything here is just labeling and
 * I/O — so the rendering stays consistent with any other tool in the
 * library that logs stats.
 */
void printStats(const List<StatsSlot> &slots) {
        if(slots.isEmpty()) return;
        for(auto it = slots.begin(); it != slots.end(); ++it) {
                if(it->io == nullptr) continue;
                MediaIOStats s = it->io->stats();
                fprintf(stdout, "[stats] %-16s  %s\n",
                        it->label.cstr(),
                        s.toString().cstr());
        }
        fflush(stdout);
}

} // namespace

int main(int argc, char **argv) {
        Options opts;
        if(!parseOptions(argc, argv, opts)) {
                fprintf(stderr, "Use --help for usage information.\n");
                return 1;
        }

        SDLApplication app(argc, argv);

        // --- Default output: SDL when no -o was given ---
        // If the user didn't pass any -o / --out, auto-add an SDL
        // sink so plain `mediaplay` just shows a window.  If they
        // passed any -o (file or otherwise), we respect their
        // explicit choice and do not inject the default SDL.
        if(!opts.explicitOut) {
                StageSpec sdlSpec;
                sdlSpec.type = kStageSdl;
                opts.outputs.pushToBack(sdlSpec);
        }

        // --- Build source ---
        MediaIO *source = buildSource(opts.input);
        if(source == nullptr) return 1;

        // Stats plumbing.  When --stats is on we attach a
        // BenchmarkReporter to every stage and flip EnableBenchmark
        // before open() so the latency keys populate.  The slot list
        // owns the reporters and drives the periodic print timer.
        List<StatsSlot> statsSlots;
        const bool statsEnabled = (opts.statsInterval > 0.0);
        if(statsEnabled) {
                attachStatsReporter(source, String("source"), statsSlots);
        }

        Error err = source->open(MediaIO::Reader);
        if(err.isError()) {
                fprintf(stderr, "Error: source open failed: %s\n", err.name().cstr());
                delete source;
                return 1;
        }

        MediaDesc srcDesc = source->mediaDesc();
        AudioDesc srcAudioDesc = source->audioDesc();
        FrameRate srcRate = source->frameRate();
        fprintf(stdout, "Source: %s  %s @ %s fps\n",
                opts.input.type == kStageFile ? opts.input.path.cstr()
                                              : opts.input.type.cstr(),
                srcDesc.imageList().isEmpty() ? "(no video)"
                        : srcDesc.imageList()[0].toString().cstr(),
                srcRate.toString().cstr());
        if(srcAudioDesc.isValid()) {
                fprintf(stdout, "  Audio: %s\n", srcAudioDesc.toString().cstr());
        }
        const Metadata srcMetadata = source->metadata();

        // --- Optional converter stage ---
        MediaIO *converter = nullptr;
        MediaDesc effectiveDesc = srcDesc;
        AudioDesc effectiveAudioDesc = srcAudioDesc;
        if(opts.hasConverter) {
                converter = buildConverter(opts.converter);
                if(converter == nullptr) {
                        source->close();
                        delete source;
                        return 1;
                }
                converter->setMediaDesc(srcDesc);
                if(srcAudioDesc.isValid()) converter->setAudioDesc(srcAudioDesc);
                if(!srcMetadata.isEmpty()) converter->setMetadata(srcMetadata);
                if(statsEnabled) {
                        attachStatsReporter(converter, String("converter"),
                                            statsSlots);
                }
                err = converter->open(MediaIO::ReadWrite);
                if(err.isError()) {
                        fprintf(stderr, "Error: Converter open failed: %s\n",
                                err.name().cstr());
                        delete converter;
                        source->close();
                        delete source;
                        return 1;
                }
                effectiveDesc = converter->mediaDesc();
                effectiveAudioDesc = converter->audioDesc();
                fprintf(stdout, "Converter: %zu key override(s)\n",
                        opts.converter.rawKeyValues.size());
                if(!effectiveDesc.imageList().isEmpty()) {
                        fprintf(stdout, "  Output video: %s\n",
                                effectiveDesc.imageList()[0].toString().cstr());
                }
                if(effectiveAudioDesc.isValid()) {
                        fprintf(stdout, "  Output audio: %s\n",
                                effectiveAudioDesc.toString().cstr());
                }
        }

        // --- Build sinks ---
        List<Sink> sinks;
        SDLWindow       *window      = nullptr;
        SDLVideoWidget  *videoWidget = nullptr;
        SDLAudioOutput  *audioOutput = nullptr;

        auto cleanupAndFail = [&](int rc) {
                for(auto it = sinks.begin(); it != sinks.end(); ++it) {
                        if(it->io != nullptr) {
                                it->io->close();
                                delete it->io;
                        }
                }
                if(converter != nullptr) {
                        converter->close();
                        delete converter;
                }
                source->close();
                delete source;
                delete audioOutput;
                delete window;
                for(auto it = statsSlots.begin();
                         it != statsSlots.end(); ++it) {
                        delete it->reporter;
                }
                statsSlots.clear();
                return rc;
        };

        for(size_t i = 0; i < opts.outputs.size(); ++i) {
                StageSpec spec = opts.outputs[i];

                if(spec.type == kStageSdl) {
                        // Apply --oc / --om overrides against the
                        // SDL schema so Paced / Audio / WindowSize /
                        // WindowTitle are resolved before we create
                        // widgets.  applyStageConfig uses
                        // sdlDefaultConfig() as the type schema for
                        // kStageSdl stages.
                        spec.config = sdlDefaultConfig();
                        Error ae = applyStageConfig(spec, String("--oc[SDL]"));
                        if(ae.isError()) return cleanupAndFail(1);
                        Error me = applyStageMetadata(spec, String("--om[SDL]"));
                        if(me.isError()) return cleanupAndFail(1);

                        const bool  sdlPaced  = spec.config.getAs<bool>(MediaIO::ConfigID("Paced"), true);
                        const bool  sdlAudio  = spec.config.getAs<bool>(MediaIO::ConfigID("Audio"), true);
                        const Size2Du32 sdlWinSize = spec.config.getAs<Size2Du32>(
                                MediaIO::ConfigID("WindowSize"), Size2Du32(1280, 720));
                        const String sdlWinTitle = spec.config.getAs<String>(
                                MediaIO::ConfigID("WindowTitle"), String("mediaplay"));

                        // One SDL window/widget per pipeline — if the
                        // user requested multiple SDL stages we only
                        // create the UI once and reuse the widgets.
                        // The first SDL stage's config wins for the
                        // window shape; later ones inherit it.
                        if(window == nullptr) {
                                if(!sdlWinSize.isValid()) {
                                        fprintf(stderr,
                                                "Error: invalid SDL WindowSize (expected WxH)\n");
                                        return cleanupAndFail(1);
                                }
                                window = new SDLWindow(sdlWinTitle,
                                                       (int)sdlWinSize.width(),
                                                       (int)sdlWinSize.height());
                                videoWidget = new SDLVideoWidget(window);
                                videoWidget->setGeometry(
                                        Rect2Di32(0, 0,
                                                  (int)sdlWinSize.width(),
                                                  (int)sdlWinSize.height()));
                                window->resizedSignal.connect(
                                        [videoWidget](Size2Di32 sz) {
                                                videoWidget->setGeometry(
                                                        Rect2Di32(0, 0,
                                                                  sz.width(),
                                                                  sz.height()));
                                        });
                                window->show();

                                // Audio is only opened when both the
                                // SDL stage asked for it (Audio:true)
                                // AND the pipeline is paced (audio-led
                                // pacing requires a working audio
                                // clock) AND the stream actually has
                                // an audio track.
                                if(sdlAudio && sdlPaced &&
                                   effectiveAudioDesc.isValid()) {
                                        audioOutput = new SDLAudioOutput();
                                }
                        }

                        MediaIO *player = createSDLPlayer(videoWidget,
                                                          audioOutput,
                                                          sdlPaced);
                        if(player == nullptr) {
                                fprintf(stderr, "Error: createSDLPlayer failed\n");
                                return cleanupAndFail(1);
                        }
                        player->setMediaDesc(effectiveDesc);
                        if(effectiveAudioDesc.isValid()) {
                                player->setAudioDesc(effectiveAudioDesc);
                        }
                        if(statsEnabled) {
                                attachStatsReporter(player, String("sink:sdl"),
                                                    statsSlots);
                        }
                        err = player->open(MediaIO::Writer);
                        if(err.isError()) {
                                fprintf(stderr, "Error: player open failed: %s\n",
                                        err.name().cstr());
                                delete player;
                                return cleanupAndFail(1);
                        }
                        sinks.pushToBack(Sink{player, String("sdl"),
                                              sdlPaced, false, String()});
                        continue;
                }

                String label;
                MediaIO *sinkIO = buildFileSink(spec, effectiveDesc,
                                                effectiveAudioDesc, srcMetadata,
                                                label);
                if(sinkIO == nullptr) return cleanupAndFail(1);
                if(statsEnabled) {
                        attachStatsReporter(sinkIO,
                                            String("sink:") + label,
                                            statsSlots);
                }
                err = sinkIO->open(MediaIO::Writer);
                if(err.isError()) {
                        fprintf(stderr, "Error: output '%s' open failed: %s\n",
                                label.cstr(), err.name().cstr());
                        delete sinkIO;
                        return cleanupAndFail(1);
                }
                bool isFile = (spec.type == kStageFile);
                sinks.pushToBack(Sink{sinkIO, label, false, isFile,
                                      isFile ? spec.path : String()});
                fprintf(stdout, "Output: %s\n", label.cstr());
        }

        if(sinks.isEmpty()) {
                fprintf(stderr,
                        "Error: no sinks configured.  Pass at least one -o/--out.\n");
                return cleanupAndFail(1);
        }

        // --- Signal-driven pipeline ---
        Pipeline pipeline(source, converter, sinks, opts.frameCount);
        pipeline.start();

        SDLApplication *sdlApp = SDLApplication::instance();
        if(window != nullptr) {
                window->closedSignal.connect([sdlApp]() {
                        if(sdlApp != nullptr) sdlApp->quit(0);
                });
        }
        if(opts.duration > 0.0) {
                sdlApp->eventLoop().startTimer(
                        static_cast<unsigned int>(opts.duration * 1000.0),
                        [sdlApp]() { sdlApp->quit(0); },
                        true);
        }
        if(opts.verbose) {
                sdlApp->eventLoop().startTimer(2000, [&]() {
                        fprintf(stdout, "[mediaplay] pumped=%lu\n",
                                (unsigned long)pipeline.framesPumped());
                });
        }
        // The stats timer runs on its own worker thread rather than
        // on the main EventLoop.  MediaIO::stats() is a synchronous
        // submitAndWait() round-trip through each stage's strand, so
        // polling from the main thread parks the main loop while
        // that round-trip completes.  The SDL player relies on
        // postCallable(renderPending) running on the main thread to
        // consume its staged image; if the main thread is blocked,
        // the SDL strand processes writeFrame() commands faster than
        // renderPending() can drain _pendingImage, and every frame
        // that arrives while _pendingImage is still set is counted
        // as dropped.  Driving the timer from a dedicated thread
        // keeps the main loop free and eliminates that race.
        Thread statsThread;
        if(statsEnabled) {
                unsigned int intervalMs = static_cast<unsigned int>(
                        opts.statsInterval * 1000.0);
                if(intervalMs < 1) intervalMs = 1;
                statsThread.setName(String("mp-stats"));
                statsThread.start();
                statsThread.threadEventLoop()->startTimer(intervalMs,
                        [&]() { printStats(statsSlots); });
        }
        int rc = app.exec();

        // --- Shut down ---
        // Stop the stats thread first so no in-flight printStats()
        // can race against the stages we are about to tear down.
        // quit() asks the worker's EventLoop to exit; wait() joins
        // the underlying std::thread.
        if(statsEnabled) {
                statsThread.quit();
                statsThread.wait();
        }
        source->cancelPending();
        if(!pipeline.finishedCleanly()) {
                if(converter != nullptr) converter->cancelPending();
                for(auto it = pipeline.sinks().begin();
                         it != pipeline.sinks().end(); ++it) {
                        it->io->cancelPending();
                }
        }

        for(auto it = pipeline.sinks().begin();
                 it != pipeline.sinks().end(); ++it) {
                it->io->close();
        }
        if(converter != nullptr) converter->close();
        source->close();

        for(auto it = pipeline.sinks().begin();
                 it != pipeline.sinks().end(); ++it) {
                delete it->io;
        }
        pipeline.sinks().clear();
        delete converter;
        delete source;
        delete audioOutput;
        delete window;

        // The reporters outlived every stage above — MediaIO only
        // observes the pointer, never owns it.  Delete them now that
        // the stages are gone.
        for(auto it = statsSlots.begin(); it != statsSlots.end(); ++it) {
                delete it->reporter;
        }
        statsSlots.clear();

        if(opts.memStats) {
                MemSpace::logAllStats();
        }

        return rc;
}
