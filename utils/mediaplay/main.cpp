/**
 * @file      mediaplay/main.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * General-purpose media pump built on the library's @ref MediaPipeline.
 * The CLI is a thin wrapper that builds a @ref MediaPipelineConfig
 * from user flags (or loads one from a JSON preset), instantiates the
 * pipeline, and pumps until the user hits the stop condition.
 *
 * Responsibilities split:
 *
 *   - `cli.{h,cpp}`   — Options, parser, help text, schema dump.
 *   - `stage.{h,cpp}` — StageSpec parsing + `resolveStagePlan` helper
 *                       that turns a CLI stage spec into a
 *                       `MediaPipelineConfig::Stage`.
 *   - this file       — wiring: pipeline config composition, SDL UI
 *                       injection, stats, lifecycle, `--save-pipeline`
 *                       / `--pipeline` short-circuits.
 */

#include <cstdio>

#include <promeki/application.h>
#include <promeki/audiodesc.h>
#include <promeki/benchmarkreporter.h>
#include <promeki/datetime.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/file.h>
#include <promeki/filepath.h>
#include <promeki/frame.h>
#include <promeki/framerate.h>
#include <promeki/iodevice.h>
#include <promeki/json.h>
#include <promeki/list.h>
#include <promeki/logger.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaio.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediapipelinestats.h>
#include <promeki/memspace.h>
#include <promeki/metadata.h>
#include <promeki/rect.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/thread.h>

#include <promeki/sdl/sdlapplication.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlplayerold.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>

#include "cli.h"
#include "stage.h"

using namespace promeki;
using namespace mediaplay;

namespace {

/**
 * @brief Handles mediaplay's legacy `--probe` short-circuit.
 *
 * Keeps the probe path byte-compatible with the pre-pipeline build:
 * resolves the source argument to a backend name, applies any
 * @c --sc overrides against the backend's @ref MediaConfig, calls
 * @ref MediaIO::queryDevice, and prints both the capability list and
 * the backend's device-info block.
 *
 * @param opts The parsed CLI options.
 * @return The exit code to pass back from @c main().
 */
int runProbe(const Options &opts) {
        String probeName = opts.source.type;
        MediaIO::Config probeCfg;
        if(probeName == kStageFile) {
                const MediaIO::FormatDesc *desc =
                        MediaIO::findFormatForPath(opts.source.path);
                if(desc == nullptr) {
                        fprintf(stderr, "Error: no backend recognises '%s'\n",
                                opts.source.path.cstr());
                        return 1;
                }
                probeName = desc->name;
                probeCfg = MediaIO::defaultConfig(probeName);
                probeCfg.set(MediaConfig::Filename, opts.source.path);
        } else {
                probeCfg = MediaIO::defaultConfig(probeName);
        }
        for(const auto &kv : opts.source.rawKeyValues) {
                size_t sep = kv.find(':');
                if(sep == String::npos) continue;
                String key = kv.left(sep);
                String val = kv.mid(sep + 1);
                MediaIO::ConfigID id(key);
                probeCfg.set(id, val);
        }
        auto caps = MediaIO::queryDevice(probeName, probeCfg);
        if(caps.isEmpty()) {
                fprintf(stderr, "No supported configurations reported by '%s'\n",
                        probeName.cstr());
                return 1;
        }
        fprintf(stdout, "Supported configurations for %s:\n", probeName.cstr());
        for(const auto &md : caps) {
                String line;
                if(!md.imageList().isEmpty()) {
                        const ImageDesc &id = md.imageList()[0];
                        line = String::sprintf("  %s  %s",
                                id.toString().cstr(),
                                md.frameRate().toString().cstr());
                } else {
                        line = String::sprintf("  (no video)  %s",
                                md.frameRate().toString().cstr());
                }
                fprintf(stdout, "%s\n", line.cstr());
        }
        MediaIO::printDeviceInfo(probeName, probeCfg);
        return 0;
}

/**
 * @brief Builds a @ref MediaPipelineConfig from CLI-parsed options.
 *
 * Stage names are assigned in pipeline order: @c "source",
 * @c "stage0", @c "stage1", ..., @c "sink0", @c "sink1", ... .  Each
 * pair of neighbouring stages in the declared order is connected by
 * one route, producing a simple linear chain with explicit fan-out
 * at the sink end.  Users who want a richer topology can author or
 * hand-edit the JSON preset.
 */
Error buildConfigFromOptions(const Options &opts, MediaPipelineConfig &out) {
        MediaPipelineConfig cfg;

        MediaPipelineConfig::Stage srcStage;
        Error se = resolveStagePlan(opts.source, MediaIO::Output,
                                    String("source"),
                                    String("--sc[") + opts.source.type + "]",
                                    srcStage);
        if(se.isError()) return se;
        cfg.addStage(srcStage);
        String prevName = "source";

        for(size_t i = 0; i < opts.converters.size(); ++i) {
                const String name = String("stage") +
                        String::number(static_cast<int64_t>(i));
                MediaPipelineConfig::Stage s;
                Error e = resolveStagePlan(opts.converters[i],
                        MediaIO::InputAndOutput, name,
                        String("--cc[") + opts.converters[i].type + "]",
                        s);
                if(e.isError()) return e;
                cfg.addStage(s);
                cfg.addRoute(prevName, name);
                prevName = name;
        }

        for(size_t i = 0; i < opts.sinks.size(); ++i) {
                const String name = String("sink") +
                        String::number(static_cast<int64_t>(i));
                MediaPipelineConfig::Stage s;
                Error e = resolveStagePlan(opts.sinks[i],
                        MediaIO::Input, name,
                        String("--dc[") + opts.sinks[i].type + "]",
                        s);
                if(e.isError()) return e;
                cfg.addStage(s);
                cfg.addRoute(prevName, name);
        }

        out = cfg;
        return Error::Ok;
}

/**
 * @brief Lazily constructs the shared SDL window / video widget /
 *        audio output for the first SDL stage encountered, reusing
 *        them for later SDL stages that share the same pipeline.
 */
struct SdlUi {
        SDLWindow       *window      = nullptr;
        SDLVideoWidget  *videoWidget = nullptr;
        SDLAudioOutput  *audioOutput = nullptr;
};

/**
 * @brief Builds an @ref SDLPlayer MediaIO for @p stage and attaches it
 *        to the pipeline via @ref MediaPipeline::injectStage.
 *
 * SDL requires widget pointers that live on the main thread, so it is
 * the one backend @ref MediaPipeline cannot instantiate through the
 * normal factory — callers pre-build it and inject it.  The first SDL
 * stage lazily allocates the shared window / audio output in @p ui;
 * every subsequent SDL stage reuses them, matching the pre-pipeline
 * mediaplay behaviour.
 */
MediaIO *createSdlStage(const MediaPipelineConfig::Stage &stage,
                        SdlUi &ui, bool enableBenchmark) {
        const String sdlTiming = stage.config.getAs<String>(
                MediaConfig::SdlTimingSource, String("audio"));
        const bool useAudioClock = (sdlTiming == String("audio"));
        const String sdlImpl = stage.config.getAs<String>(
                MediaConfig::SdlPlayerImpl, String("framesync"));
        const bool useFrameSync = (sdlImpl == String("framesync"));
        const Size2Du32 sdlWinSize = stage.config.getAs<Size2Du32>(
                MediaConfig::SdlWindowSize, Size2Du32(1280, 720));
        const String sdlWinTitle = stage.config.getAs<String>(
                MediaConfig::SdlWindowTitle, String("mediaplay"));

        if(ui.window == nullptr) {
                if(!sdlWinSize.isValid()) {
                        fprintf(stderr, "Error: invalid SDL WindowSize\n");
                        return nullptr;
                }
                ui.window = new SDLWindow(sdlWinTitle,
                                          static_cast<int>(sdlWinSize.width()),
                                          static_cast<int>(sdlWinSize.height()));
                ui.videoWidget = new SDLVideoWidget(ui.window);
                ui.videoWidget->setGeometry(
                        Rect2Di32(0, 0,
                                  static_cast<int>(sdlWinSize.width()),
                                  static_cast<int>(sdlWinSize.height())));
                SDLVideoWidget *vw = ui.videoWidget;
                ui.window->resizedSignal.connect([vw](Size2Di32 sz) {
                        vw->setGeometry(Rect2Di32(0, 0, sz.width(), sz.height()));
                });
                ui.window->show();
                ui.audioOutput = new SDLAudioOutput();
        }

        MediaIO *player = useFrameSync
                ? createSDLPlayer(ui.videoWidget, ui.audioOutput, useAudioClock)
                : createSDLPlayerOld(ui.videoWidget, ui.audioOutput, useAudioClock);
        if(player == nullptr) {
                fprintf(stderr, "Error: createSDLPlayer failed for stage '%s'\n",
                        stage.name.cstr());
                return nullptr;
        }
        if(enableBenchmark) {
                MediaIO::Config cfg = player->config();
                cfg.set(MediaConfig::EnableBenchmark, true);
                player->setConfig(cfg);
        }
        return player;
}

} // namespace

int main(int argc, char **argv) {
        Options opts;
        if(!parseOptions(argc, argv, opts)) {
                fprintf(stderr, "Use --help for usage information.\n");
                return 1;
        }

        // Bump the logger threshold to Info when the user wants any
        // live reporting — --stats and --verbose both route their
        // output through the logger, and silently eating it because
        // of an elevated default would be a lousy experience.
        const bool reportingEnabled = (opts.statsInterval > 0.0) || opts.verbose;
        if(reportingEnabled
           && Logger::defaultLogger().level() > Logger::LogLevel::Info) {
                Logger::defaultLogger().setLogLevel(Logger::LogLevel::Info);
        }

        if(!opts.savePipelinePath.isEmpty()
           && !opts.loadPipelinePath.isEmpty()) {
                fprintf(stderr,
                        "Error: --save-pipeline and --pipeline are mutually exclusive.\n");
                return 1;
        }

        SDLApplication app(argc, argv);

        // Default SDL sink when the user did not pass any -d and did
        // not ask to load a preset.  Saved presets carry their own
        // sink set, so never inject a default on the load path.
        if(opts.loadPipelinePath.isEmpty() && !opts.explicitDst) {
                StageSpec sdlSpec;
                sdlSpec.type = kStageSdl;
                opts.sinks.pushToBack(sdlSpec);
        }

        if(opts.probe) return runProbe(opts);

        // --- Resolve the MediaPipelineConfig ---
        MediaPipelineConfig pipelineCfg;
        if(!opts.loadPipelinePath.isEmpty()) {
                Error lerr;
                pipelineCfg = MediaPipelineConfig::loadFromFile(
                        FilePath(opts.loadPipelinePath), &lerr);
                if(lerr.isError()) {
                        fprintf(stderr, "Error: failed to load pipeline '%s': %s\n",
                                opts.loadPipelinePath.cstr(),
                                lerr.desc().cstr());
                        return 1;
                }
        } else {
                Error berr = buildConfigFromOptions(opts, pipelineCfg);
                if(berr.isError()) return 1;
        }

        if(!opts.savePipelinePath.isEmpty()) {
                Error serr = pipelineCfg.saveToFile(
                        FilePath(opts.savePipelinePath));
                if(serr.isError()) {
                        fprintf(stderr, "Error: saveToFile '%s' failed: %s\n",
                                opts.savePipelinePath.cstr(),
                                serr.desc().cstr());
                        return 1;
                }
                fprintf(stdout, "Pipeline saved to %s\n",
                        opts.savePipelinePath.cstr());
                return 0;
        }

        const bool statsEnabled = (opts.statsInterval > 0.0);

        // --- Stats writer (optional JSONL file) ---
        // Owned by main; lives for the duration of app.exec() + final
        // shutdown snapshot.  Opened before the stats thread starts so
        // periodic writes have a valid handle from the first tick.
        File *statsFile = nullptr;
        if(!opts.writeStatsPath.isEmpty()) {
                statsFile = new File(opts.writeStatsPath);
                Error oe = statsFile->open(IODevice::WriteOnly,
                                           File::Create | File::Truncate);
                if(oe.isError()) {
                        fprintf(stderr,
                                "Error: --write-stats '%s' open failed: %s\n",
                                opts.writeStatsPath.cstr(), oe.desc().cstr());
                        delete statsFile;
                        return 1;
                }
        }

        // Turn on EnableBenchmark for every non-injected stage before
        // build() so the MediaIO instances come up with per-frame
        // stamping on — the key is read in resolveIdentifiersAndBenchmark
        // at the top of open().
        if(statsEnabled) {
                MediaPipelineConfig::StageList &stages = pipelineCfg.stages();
                for(size_t i = 0; i < stages.size(); ++i) {
                        stages[i].config.set(MediaConfig::EnableBenchmark, true);
                }
        }

        // --- Pre-build SDL UI + inject SDL stages ---
        SdlUi ui;
        List<MediaIO *> injectedSdl;
        MediaPipeline pipeline;

        for(size_t i = 0; i < pipelineCfg.stages().size(); ++i) {
                const MediaPipelineConfig::Stage &s = pipelineCfg.stages()[i];
                if(s.type != kStageSdl) continue;
                MediaIO *player = createSdlStage(s, ui, statsEnabled);
                if(player == nullptr) {
                        for(auto *io : injectedSdl) delete io;
                        delete ui.audioOutput;
                        delete ui.window;
                        return 1;
                }
                (void)pipeline.injectStage(s.name, player);
                injectedSdl.pushToBack(player);
        }

        // --- Build + open + start ---
        Error berr = pipeline.build(pipelineCfg);
        if(berr.isError()) {
                fprintf(stderr, "Error: pipeline build failed: %s\n",
                        berr.desc().cstr());
                for(auto *io : injectedSdl) delete io;
                delete ui.audioOutput;
                delete ui.window;
                return 1;
        }

        // Attach BenchmarkReporters after build, before open, so the
        // stamps start accumulating from the very first frame.
        List<BenchmarkReporter *> reporters;
        if(statsEnabled) {
                StringList names = pipeline.stageNames();
                for(size_t i = 0; i < names.size(); ++i) {
                        MediaIO *io = pipeline.stage(names[i]);
                        if(io == nullptr) continue;
                        auto *rep = new BenchmarkReporter();
                        io->setBenchmarkReporter(rep);
                        reporters.pushToBack(rep);
                }
        }

        Error oerr = pipeline.open();
        if(oerr.isError()) {
                fprintf(stderr, "Error: pipeline open failed: %s\n",
                        oerr.desc().cstr());
                (void)pipeline.close();
                for(auto *r : reporters) delete r;
                for(auto *io : injectedSdl) delete io;
                delete ui.audioOutput;
                delete ui.window;
                return 1;
        }

        // One-line description of the live pipeline for humans.
        {
                StringList lines = pipeline.describe();
                for(size_t i = 0; i < lines.size(); ++i) {
                        fprintf(stdout, "%s\n", lines[i].cstr());
                }
        }

        // --- Frame-count limit wiring ---
        // The first stage in topological order is the source; counting
        // its successful frameReady signals is a close-enough proxy
        // for "frames pumped".  Uses a long-lived lambda-captured
        // counter so we don't need to heap-allocate state.
        int64_t framesCounted = 0;
        {
                StringList names = pipeline.stageNames();
                const int64_t limit = opts.frameCount;
                if(limit > 0 && !names.isEmpty()) {
                        MediaIO *sourceIO = pipeline.stage(names[0]);
                        if(sourceIO != nullptr) {
                                sourceIO->frameReadySignal.connect(
                                        [&framesCounted, limit]() {
                                                if(++framesCounted >= limit) {
                                                        Application::quit(0);
                                                }
                                        }, &pipeline);
                        }
                }
        }

        pipeline.pipelineErrorSignal.connect(
                [](const String &stageName, Error err) {
                        fprintf(stderr, "Pipeline error at '%s': %s\n",
                                stageName.cstr(), err.desc().cstr());
                }, &pipeline);
        pipeline.finishedSignal.connect([](bool clean) {
                Application::quit(clean ? 0 : 1);
        }, &pipeline);

        SDLApplication *sdlApp = SDLApplication::instance();
        if(ui.window != nullptr) {
                ui.window->closedSignal.connect([sdlApp]() {
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
                sdlApp->eventLoop().startTimer(2000, [&pipeline]() {
                        MediaPipelineStats s = pipeline.stats();
                        StringList lines = s.describe();
                        for(size_t i = 0; i < lines.size(); ++i) {
                                fprintf(stdout, "[mediaplay] %s\n",
                                        lines[i].cstr());
                        }
                });
        }

        // Shared stats-snapshot hook.  Called from the stats worker
        // thread on every tick and once more from the main thread at
        // shutdown after the worker has joined, so accesses to
        // @p statsFile never overlap across threads.
        auto emitStats = [&pipeline, statsFile](const char *label) {
                MediaPipelineStats s = pipeline.stats();
                StringList lines = s.describe();
                for(size_t i = 0; i < lines.size(); ++i) {
                        promekiInfo("[%s] %s", label, lines[i].cstr());
                }
                if(statsFile == nullptr) return;

                // One JSON object per line (JSONL).  We stamp a
                // timestamp and a "phase" tag so downstream tooling
                // can tell periodic snapshots from the final aggregate
                // without reparsing the interior.
                JsonObject obj = s.toJson();
                obj.set("timestamp", DateTime::now().toString());
                obj.set("phase", String(label));
                const String text = obj.toString() + "\n";
                (void)statsFile->write(text.cstr(),
                        static_cast<int64_t>(text.size()));
                statsFile->flush();
        };

        // The stats timer runs on its own worker thread so each
        // synchronous MediaIO::stats() round-trip doesn't park the
        // main-thread EventLoop that the SDL player relies on for
        // renderPending() dispatch.
        Thread statsThread;
        if(statsEnabled) {
                unsigned int intervalMs = static_cast<unsigned int>(
                        opts.statsInterval * 1000.0);
                if(intervalMs < 1) intervalMs = 1;
                statsThread.setName(String("mp-stats"));
                statsThread.start();
                statsThread.threadEventLoop()->startTimer(intervalMs,
                        [&emitStats]() { emitStats("stats"); });
        }

        Error serr = pipeline.start();
        if(serr.isError()) {
                fprintf(stderr, "Error: pipeline start failed: %s\n",
                        serr.desc().cstr());
                (void)pipeline.close();
                if(statsEnabled) {
                        statsThread.quit();
                        statsThread.wait();
                }
                for(auto *r : reporters) delete r;
                for(auto *io : injectedSdl) delete io;
                delete ui.audioOutput;
                delete ui.window;
                if(statsFile != nullptr) {
                        statsFile->close();
                        delete statsFile;
                }
                return 1;
        }

        int rc = app.exec();

        // Stop the stats thread first so it can't race the final
        // emitStats() call below — once we return from wait() the
        // stats file and the pipeline are ours alone.
        if(statsEnabled) {
                statsThread.quit();
                statsThread.wait();
        }

        // Final snapshot: stats collector was enabled for either
        // --stats or --write-stats, so emit one last record while the
        // pipeline is still in Running/Stopped state (counters are
        // still live and the aggregate reflects the run).  Tagged
        // "final" so JSONL consumers can distinguish it from the
        // periodic "stats" ticks.
        if(statsEnabled) emitStats("final");

        (void)pipeline.close();

        for(auto *r : reporters) delete r;
        for(auto *io : injectedSdl) delete io;
        delete ui.audioOutput;
        delete ui.window;

        if(statsFile != nullptr) {
                statsFile->close();
                delete statsFile;
        }

        if(opts.memStats) MemSpace::logAllStats();
        promekiLogSync();
        return rc;
}
