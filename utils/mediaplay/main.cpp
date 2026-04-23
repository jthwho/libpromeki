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

#include <atomic>
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
#include <promeki/mediaiodescription.h>
#include <promeki/mediaiotask_inspector.h>
#include <promeki/url.h>
#include <promeki/mediapipeline.h>
#include <promeki/mediapipelineconfig.h>
#include <promeki/mediapipelineplanner.h>
#include <promeki/mediapipelinestats.h>
#include <promeki/memspace.h>
#include <promeki/metadata.h>
#include <promeki/objectbase.tpp>
#include <promeki/rect.h>
#include <promeki/size2d.h>
#include <promeki/string.h>
#include <promeki/thread.h>

#include <promeki/sdl/sdlsubsystem.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlplayer.h>
#include <promeki/sdl/sdlplayerwidget.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>

#include "cli.h"
#include "stage.h"

using namespace promeki;
using namespace mediaplay;

namespace {

// Documented in --help.  Stable, hand-picked so adding new Error
// codes can't renumber them.  Also used by scripts/roundtrip-codecs.sh.
constexpr int ExitOk                      = 0;
constexpr int ExitGeneric                 = 1;
constexpr int ExitPipelineBuild           = 10;
constexpr int ExitPipelineOpen            = 11;
constexpr int ExitPipelineStart           = 12;
constexpr int ExitPipelineRuntime         = 13;
constexpr int ExitInspectorDiscontinuity  = 21;

/**
 * @brief Builds a MediaIO around a freshly-constructed
 *        @ref MediaIOTask_Inspector so mediaplay can retain a typed
 *        handle for post-run snapshot polling.
 *
 * Mirrors the SDL injection path: the task can't stay anonymous because
 * the caller (main) needs to pull @c snapshot() off it after
 * @c app.exec() returns to compute the inspector pass/fail exit code.
 * The MediaIO takes ownership of the task via @ref MediaIO::adoptTask
 * and will delete it from its own destructor, so the caller only owns
 * the MediaIO pointer.
 *
 * @param stage The pipeline-config stage record to draw config /
 *              metadata from.  @c stage.type is expected to be
 *              @c "Inspector".
 * @param[out] taskOut Typed pointer to the adopted task, used later
 *              for @c snapshot() — MediaIO still owns it.
 * @return The new MediaIO (ready for @c pipeline.injectStage), or
 *         @c nullptr on adoption failure.
 */
MediaIO *createInspectorStage(const MediaPipelineConfig::Stage &stage,
                              MediaIOTask_Inspector **taskOut) {
        auto *task = new MediaIOTask_Inspector();
        auto *io = new MediaIO();
        MediaIO::Config cfg = MediaIO::defaultConfig(String("Inspector"));
        // Copy any per-stage config keys supplied via --dc onto the
        // backend default so --dc InspectorTests:Timestamp etc. still
        // works when mediaplay is the one constructing the task.
        const auto stageIds = stage.config.ids();
        for(size_t i = 0; i < stageIds.size(); ++i) {
                cfg.set(stageIds[i], stage.config.get(stageIds[i]));
        }
        io->setConfig(cfg);
        Error e = io->adoptTask(task);
        if(e.isError()) {
                fprintf(stderr, "Error: adoptTask(Inspector) failed: %s\n",
                        e.desc().cstr());
                delete io;
                delete task;
                return nullptr;
        }
        if(taskOut != nullptr) *taskOut = task;
        return io;
}

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
                // URL-first: if the arg parses as a URL with a
                // registered scheme, ask that backend to translate
                // the URL into a Config (same path createFromUrl
                // uses) so --probe works against pmfb://, pmdf:, etc.
                // without the caller having to convert back to a
                // fake filename.
                Error urlErr = Error::Ok;
                Url parsed = Url::fromString(opts.source.path, &urlErr);
                const MediaIO::FormatDesc *urlDesc = nullptr;
                if(urlErr.isOk() && parsed.isValid()) {
                        urlDesc = MediaIO::findFormatByScheme(parsed.scheme());
                }
                if(urlDesc != nullptr) {
                        probeName = urlDesc->name;
                        probeCfg = MediaIO::defaultConfig(probeName);
                        probeCfg.set(MediaConfig::Url, parsed);
                        if(urlDesc->urlToConfig) {
                                Error e = urlDesc->urlToConfig(parsed, &probeCfg);
                                if(e.isError()) {
                                        fprintf(stderr, "Error: '%s' rejected URL '%s'\n",
                                                urlDesc->name.cstr(),
                                                opts.source.path.cstr());
                                        return 1;
                                }
                        }
                        if(urlDesc->configSpecs && !parsed.query().isEmpty()) {
                                Error e = MediaIO::applyQueryToConfig(
                                        parsed, urlDesc->configSpecs(), &probeCfg);
                                if(e.isError()) {
                                        fprintf(stderr,
                                                "Error: query application failed for '%s'\n",
                                                opts.source.path.cstr());
                                        return 1;
                                }
                        }
                } else {
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
                }
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
        Error se = resolveStagePlan(opts.source, MediaIO::Source,
                                    String("source"),
                                    String("--sc[") + opts.source.type + "]",
                                    srcStage);
        if(se.isError()) return se;
        cfg.addStage(srcStage);
        String prevName = "source";

        for(size_t i = 0; i < opts.transforms.size(); ++i) {
                const String name = String("stage") +
                        String::number(static_cast<int64_t>(i));
                MediaPipelineConfig::Stage s;
                Error e = resolveStagePlan(opts.transforms[i],
                        MediaIO::Transform, name,
                        String("--cc[") + opts.transforms[i].type + "]",
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
                        MediaIO::Sink, name,
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
 * @brief Lazily constructs the shared SDL window / audio output for
 *        the first SDL stage encountered, reusing them for later SDL
 *        stages that share the same pipeline.  A fresh
 *        @ref SDLPlayerWidget is created per stage.
 */
struct SdlUi {
        SDLWindow       *window      = nullptr;
        SDLAudioOutput  *audioOutput = nullptr;
};

/**
 * @brief Builds an @ref SDLPlayerWidget-backed MediaIO for @p stage so
 *        it can be injected into the pipeline.
 *
 * SDL requires widget pointers that live on the main thread, so it is
 * the one backend @ref MediaPipeline cannot instantiate through the
 * normal factory — callers pre-build it and inject it.  The first SDL
 * stage lazily allocates the shared window / audio output in @p ui;
 * every subsequent SDL stage reuses them, matching the pre-pipeline
 * mediaplay behaviour.  The returned MediaIO is owned by the widget
 * (parented to @c ui.window), so the caller must NOT delete it —
 * window deletion at shutdown frees everything in one shot.
 */
MediaIO *createSdlStage(const MediaPipelineConfig::Stage &stage,
                        SdlUi &ui, bool enableBenchmark) {
        const String sdlTiming = stage.config.getAs<String>(
                MediaConfig::SdlTimingSource, String("audio"));
        const bool useAudioClock = (sdlTiming == String("audio"));
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
                ui.window->show();
                ui.audioOutput = new SDLAudioOutput(ui.window);
        }

        const Rect2Di32 fullRect(
                0, 0,
                static_cast<int>(sdlWinSize.width()),
                static_cast<int>(sdlWinSize.height()));

        auto *w = new SDLPlayerWidget(ui.audioOutput, useAudioClock,
                                      ui.window);
        w->setGeometry(fullRect);
        ui.window->resizedSignal.connect([w](Size2Di32 sz) {
                w->setGeometry(Rect2Di32(0, 0, sz.width(), sz.height()));
        });
        w->setFocus();
        SdlSubsystem::instance()->setFocusedWidget(w);
        MediaIO *player = w->mediaIO();
        if(player == nullptr) {
                fprintf(stderr, "Error: SDL player creation failed for stage '%s'\n",
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
                return ExitGeneric;
        }

        // --list-codecs short-circuit.  No Application / pipeline
        // state is needed — the codec registries are populated at
        // static-init time by the linked-in backend libraries, so we
        // can walk them and exit.  Documented in --help.
        if(opts.listCodecs != Options::ListCodecsNone) {
                CodecTsvKind kind = CodecTsvKind::All;
                if(opts.listCodecs == Options::ListCodecsVideo) {
                        kind = CodecTsvKind::Video;
                } else if(opts.listCodecs == Options::ListCodecsAudio) {
                        kind = CodecTsvKind::Audio;
                }
                printCodecsTsv(kind);
                return ExitOk;
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
                return ExitGeneric;
        }

        Application  app(argc, argv);
        SdlSubsystem sdl;

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
                        return ExitGeneric;
                }
        } else {
                Error berr = buildConfigFromOptions(opts, pipelineCfg);
                if(berr.isError()) return ExitGeneric;
        }

        if(!opts.savePipelinePath.isEmpty()) {
                Error serr = pipelineCfg.saveToFile(
                        FilePath(opts.savePipelinePath));
                if(serr.isError()) {
                        fprintf(stderr, "Error: saveToFile '%s' failed: %s\n",
                                opts.savePipelinePath.cstr(),
                                serr.desc().cstr());
                        return ExitGeneric;
                }
                fprintf(stdout, "Pipeline saved to %s\n",
                        opts.savePipelinePath.cstr());
                return ExitOk;
        }

        const bool statsEnabled = (opts.statsInterval > 0.0);

        // The --plan / --describe flags below need access to any
        // injected SDL stages so the planner can call describe() /
        // proposeInput on the live SDLPlayerTask instance instead of
        // failing to build a stand-in from the registry.  Build the
        // SDL UI now (lazily — only when the config actually mentions
        // an SDL stage) so both early-exit paths and the normal
        // build path see the same injected map.
        SdlUi ui;
        // Each injected SDL stage is owned by an SDLPlayerWidget
        // parented to ui.window, so deleting the window at shutdown
        // frees the whole chain.  We only need the name→MediaIO map
        // so the planner and --describe paths can reach the live
        // instances.
        promeki::Map<String, MediaIO *> injectedMap;
        // Inspector stages mediaplay injected itself — we keep typed
        // handles here so we can poll @c snapshot() after the run
        // completes and convert it to a pass/fail exit code.  Map
        // from the stage name (as declared in the pipeline config)
        // to the task pointer we adopted into the MediaIO.
        promeki::Map<String, MediaIOTask_Inspector *> inspectorTasks;
        for(size_t i = 0; i < pipelineCfg.stages().size(); ++i) {
                const MediaPipelineConfig::Stage &s = pipelineCfg.stages()[i];
                if(s.type == kStageSdl) {
                        MediaIO *player = createSdlStage(s, ui, statsEnabled);
                        if(player == nullptr) {
                                delete ui.audioOutput;
                                delete ui.window;
                                return ExitGeneric;
                        }
                        injectedMap.insert(s.name, player);
                } else if(s.type == String("Inspector")) {
                        // Always inject Inspector stages so main() can
                        // poll @c snapshot() after the run and surface
                        // any reported discontinuity as a dedicated exit
                        // code.  The pipeline-level @c --frame-count cap
                        // already guarantees the inspector receives the
                        // requested number of frames, so a separate
                        // "expected frames" flag is no longer needed.
                        MediaIOTask_Inspector *task = nullptr;
                        MediaIO *io = createInspectorStage(s, &task);
                        if(io == nullptr) {
                                delete ui.audioOutput;
                                delete ui.window;
                                return ExitGeneric;
                        }
                        if(statsEnabled) {
                                MediaIO::Config c = io->config();
                                c.set(MediaConfig::EnableBenchmark, true);
                                io->setConfig(c);
                        }
                        injectedMap.insert(s.name, io);
                        inspectorTasks.insert(s.name, task);
                }
        }

        // Cleanup helper for the --plan / --describe early-exit paths
        // — they don't transfer ownership to a MediaPipeline so they
        // have to tear down the injected window themselves.
        auto cleanupInjected = [&]() {
                injectedMap.clear();
                delete ui.audioOutput;
                ui.audioOutput = nullptr;
                delete ui.window;
                ui.window = nullptr;
        };

        // ---- --plan: run the planner and print the resolved config ----
        // Useful for "would this CLI invocation work?" pre-flight
        // checks without committing to actually opening anything.
        if(opts.planOnly) {
                MediaPipelineConfig planned;
                String diag;
                Error perr = MediaPipelinePlanner::plan(
                        pipelineCfg, &planned, injectedMap, {}, &diag);
                if(perr.isError()) {
                        fprintf(stderr, "Error: planning failed: %s\n",
                                perr.desc().cstr());
                        if(!diag.isEmpty()) {
                                const StringList lines =
                                        diag.split(std::string("\n"));
                                for(size_t i = 0; i < lines.size(); ++i) {
                                        fprintf(stderr, "  %s\n", lines[i].cstr());
                                }
                        }
                        cleanupInjected();
                        return ExitPipelineBuild;
                }
                fprintf(stdout, "Planner inserted %zu bridge stage(s).\n",
                        planned.stages().size() - pipelineCfg.stages().size());
                const StringList desc = planned.describe();
                for(size_t i = 0; i < desc.size(); ++i) {
                        fprintf(stdout, "%s\n", desc[i].cstr());
                }
                cleanupInjected();
                return 0;
        }

        // ---- --describe: dump describe() for every stage ----
        // Walks the input config (no planning) and prints each
        // stage's describe() summary.  Uses the injected SDL stage
        // if one exists for the name; otherwise instantiates from
        // the registry.
        if(opts.describeOnly) {
                int exitCode = 0;
                for(size_t i = 0; i < pipelineCfg.stages().size(); ++i) {
                        const MediaPipelineConfig::Stage &s = pipelineCfg.stages()[i];
                        MediaIO *io = nullptr;
                        bool ownsIo = false;
                        auto injIt = injectedMap.find(s.name);
                        if(injIt != injectedMap.end()) {
                                io = injIt->second;
                        } else {
                                MediaConfig mcfg = s.config;
                                if(!s.type.isEmpty()) mcfg.set(MediaConfig::Type, s.type);
                                if(!s.path.isEmpty()) mcfg.set(MediaConfig::Filename, s.path);
                                io = (!s.type.isEmpty())
                                        ? MediaIO::create(mcfg)
                                        : (s.mode == MediaIO::Source
                                                ? MediaIO::createForFileRead(s.path)
                                                : MediaIO::createForFileWrite(s.path));
                                ownsIo = (io != nullptr);
                        }
                        if(io == nullptr) {
                                fprintf(stderr, "[%s] failed to instantiate.\n",
                                        s.name.cstr());
                                exitCode = 1;
                                continue;
                        }
                        MediaIODescription d;
                        Error derr = io->describe(&d);
                        if(derr.isError()) {
                                fprintf(stderr, "[%s] describe() failed: %s\n",
                                        s.name.cstr(), derr.desc().cstr());
                                exitCode = 1;
                        }
                        const StringList lines = d.summary();
                        for(size_t j = 0; j < lines.size(); ++j) {
                                fprintf(stdout, "%s\n", lines[j].cstr());
                        }
                        fprintf(stdout, "\n");
                        if(ownsIo) {
                                if(io->isOpen()) (void)io->close();
                                delete io;
                        }
                }
                cleanupInjected();
                return exitCode;
        }

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
                        return ExitGeneric;
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

        // --- Frame-count cap ---
        // Push the CLI override onto the config last so it wins over
        // anything carried by a loaded preset — users running
        // `--pipeline foo.json --frame-count 60` expect the CLI value
        // to take priority.  Zero / unset means "unlimited" on both
        // sides, so skip the assignment in that case to preserve any
        // preset-defined cap.
        if(opts.frameCount > 0) {
                pipelineCfg.setFrameCount(FrameCount(opts.frameCount));
        }

        // --- Inject the SDL stages we built earlier into the pipeline ---
        // The actual UI / SDLPlayer instances are created above (so
        // --plan and --describe see them too); here we just hand
        // them to the pipeline by name.
        MediaPipeline pipeline;
        for(auto it = injectedMap.begin(); it != injectedMap.end(); ++it) {
                (void)pipeline.injectStage(it->first, it->second);
        }

        // --- Build + open + start ---
        // The planner runs by default (autoplan=true) — bridges
        // like CSC, decoder, FrameSync get spliced in automatically.
        // Disable with --no-autoplan when you want a strict
        // fully-resolved config (e.g. regression scripts).
        Error berr = pipeline.build(pipelineCfg, opts.autoplan);
        if(berr.isError()) {
                // The planner emits a multi-line diagnostic via
                // promekiErr inside MediaPipeline::build, so the
                // detailed "why" is already in the log by the time
                // we get here.  The fprintf below just gives the
                // user a single concise summary on stderr.
                fprintf(stderr, "Error: pipeline build failed: %s%s\n",
                        berr.desc().cstr(),
                        opts.autoplan ? " (planner enabled — see logs above for details)"
                                       : "");
                delete ui.audioOutput;
                delete ui.window;
                return ExitPipelineBuild;
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
                delete ui.audioOutput;
                delete ui.window;
                return ExitPipelineOpen;
        }

        // One-line description of the live pipeline for humans.
        {
                StringList lines = pipeline.describe();
                for(size_t i = 0; i < lines.size(); ++i) {
                        fprintf(stdout, "%s\n", lines[i].cstr());
                }
        }

        // Runtime-error latch.  @c pipelineErrorSignal fires from the
        // stage's owning thread (not the main EventLoop), so the flag
        // is an atomic that main() reads after @c app.exec() returns
        // to produce the final exit code.  We latch only the first
        // error to avoid overwriting a more interesting root cause
        // with a downstream fallout error.
        std::atomic<bool> runtimeErrorLatched{false};
        pipeline.pipelineErrorSignal.connect(
                [&runtimeErrorLatched](const String &stageName, Error err) {
                        fprintf(stderr, "Pipeline error at '%s': %s\n",
                                stageName.cstr(), err.desc().cstr());
                        bool expected = false;
                        runtimeErrorLatched.compare_exchange_strong(expected, true);
                }, &pipeline);
        // finishedSignal now fires at the END of the close cascade
        // (alongside closedSignal), so just use it for a diagnostic
        // line when the run wasn't clean — the actual quit happens
        // from closedSignal below.
        pipeline.finishedSignal.connect([&runtimeErrorLatched](bool clean) {
                if(!clean) {
                        fprintf(stderr, "Pipeline did not finish cleanly.\n");
                        bool expected = false;
                        runtimeErrorLatched.compare_exchange_strong(expected, true);
                }
        }, &pipeline);
        // Cascade finished — drive the actual EventLoop quit now that
        // every stage has drained and released its resources.  Pass
        // the close-cascade error through to the exit-code logic via
        // the same latch so the caller sees "pipeline runtime error"
        // rather than a generic failure.
        pipeline.closedSignal.connect([&runtimeErrorLatched](Error err) {
                if(err.isError()) {
                        bool expected = false;
                        runtimeErrorLatched.compare_exchange_strong(expected, true);
                }
                Application::quit(err.isOk() ? ExitOk : ExitPipelineRuntime);
        }, &pipeline);

        // Intercept Ctrl-C / signal-driven quit so the pipeline has a
        // chance to drain and close gracefully before the EventLoop
        // tears down.  The handler is invoked by
        // @ref Application::quit (from the signal-watcher thread on
        // POSIX); post the actual close onto the main EventLoop so
        // pipeline state is only mutated on its owning thread.
        EventLoop *mainEL = Application::mainEventLoop();
        Application::setQuitRequestHandler([&pipeline, mainEL](int /*code*/) -> bool {
                if(pipeline.state() == MediaPipeline::State::Closed
                   || pipeline.isClosing()) {
                        // Pipeline already closed or closing — let the
                        // default quit path run so the EventLoop exits.
                        return false;
                }
                // close(false) is idempotent on re-entry, so the
                // posted callable doesn't need to re-check state.
                auto kick = [&pipeline]() { (void)pipeline.close(false); };
                if(mainEL != nullptr) mainEL->postCallable(std::move(kick));
                else kick();
                return true;
        });

        if(ui.window != nullptr) {
                ui.window->closedSignal.connect([]() { Application::quit(0); });
        }
        if(opts.duration > 0.0 && mainEL != nullptr) {
                mainEL->startTimer(
                        static_cast<unsigned int>(opts.duration * 1000.0),
                        []() { Application::quit(0); },
                        true);
        }
        if(opts.verbose && mainEL != nullptr) {
                mainEL->startTimer(2000, [&pipeline]() {
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
                delete ui.audioOutput;
                delete ui.window;
                if(statsFile != nullptr) {
                        statsFile->close();
                        delete statsFile;
                }
                return ExitPipelineStart;
        }

        int rc = app.exec();

        // Drop the quit-request handler now that exec() has returned —
        // it captures @p pipeline by reference and the pipeline goes
        // out of scope at the end of main.  Any further Application::quit
        // calls (e.g. from a late cleanup path) fall through to the
        // default behaviour.
        Application::setQuitRequestHandler(nullptr);

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
        delete ui.audioOutput;
        delete ui.window;

        if(statsFile != nullptr) {
                statsFile->close();
                delete statsFile;
        }

        if(opts.memStats) MemSpace::logAllStats();

        // --- Final exit code resolution ---
        //
        // The ordering matters: a pipeline runtime error should win
        // over an Inspector-reported discontinuity, since a mid-run
        // failure is almost always the cause of any downstream
        // frame-continuity break.  A clean run with a discontinuity
        // still reports @ref ExitInspectorDiscontinuity so scripts
        // (scripts/roundtrip-codecs.sh and friends) can distinguish
        // "pipeline didn't make it" from "pipeline made it but the
        // frames don't hang together".
        int finalRc = rc;
        if(runtimeErrorLatched.load()) {
                finalRc = ExitPipelineRuntime;
        } else if(!inspectorTasks.isEmpty()) {
                int64_t totalDisc = 0;
                for(auto it = inspectorTasks.begin();
                    it != inspectorTasks.end(); ++it) {
                        InspectorSnapshot snap = it->second->snapshot();
                        totalDisc += snap.totalDiscontinuities;
                }
                if(totalDisc > 0) {
                        fprintf(stderr,
                                "Inspector reported %lld discontinuit%s\n",
                                (long long)totalDisc,
                                totalDisc == 1 ? "y" : "ies");
                        finalRc = ExitInspectorDiscontinuity;
                }
        }

        promekiLogSync();
        return finalRc;
}
