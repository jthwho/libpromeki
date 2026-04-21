# MediaPipeline {#mediapipeline}

Data-driven builder and runtime for a DAG of `MediaIO` stages.

`MediaPipeline` stitches an arbitrary graph of `MediaIO`
backends into a running pipeline from a single declarative
configuration. The pipeline is the lowest-friction way to compose
a source, zero-or-more transform stages, and one-or-more sinks
without hand-rolling signal wiring for each tool.

## Overview {#mediapipeline_overview}

`MediaPipelineConfig` is a shareable data object describing:

- an ordered list of `MediaPipelineConfig::Stage` entries —
  one `MediaIO` instance each, keyed by a unique `name`
  string,
- an ordered list of `MediaPipelineConfig::Route` entries —
  directed frame-flow edges between stages,
- pipeline-wide `Metadata`.

The config round-trips through JSON (`MediaPipelineConfig::toJson` /
`MediaPipelineConfig::fromJson`) and `DataStream`. The JSON
form is the canonical preset file format for
`mediaplay` / GUI editors / scripted pipelines.

`MediaPipeline` consumes a `MediaPipelineConfig`, instantiates
each stage (via `MediaIO::create` or the
`MediaIO::createForFileRead` / `MediaIO::createForFileWrite`
factories for paths with no explicit backend), wires `frameReady` /
`frameWanted` / `writeError` signals according to the routes, and
drives the drain signal-driven on the owner's `EventLoop`.

## Lifecycle {#mediapipeline_lifecycle}

Explicit lifecycle calls advance the pipeline through a
deterministic `MediaPipeline::State` machine:

```
Empty ──build()──► Built ──open()──► Open ──start()──► Running
                                  │                     │
                                  │                     ├─stop()──► Stopped
                                  │                     │
                                  └──close()─────────── ▼
                                                      Closed
```

- `MediaPipeline::build` validates the config (structural +
  per-stage `VariantSpec`) and instantiates every stage.
- `MediaPipeline::open` opens each stage in reverse topological
  order so sinks are ready when sources produce the first frame.
  On any failure already-opened stages are closed.
- `MediaPipeline::start` connects `frameReady` / `frameWanted` /
  `writeError` signals and primes the drain with a single
  synchronous kick.
- `MediaPipeline::stop` cancels pending commands on every stage
  and disconnects the drain.
- `MediaPipeline::close` runs a graceful cascade. The true
  source stages (no upstream in the DAG) are closed first with
  `MediaIO::close(bool)` set to non-blocking. As each source's
  synthetic EOS reaches the drain loop and latches `upstreamDone`,
  every direct downstream consumer of that source is closed in turn
  — so intermediate stages only see `close()` after all their
  input frames have been written to them. When every stage has
  emitted its own `MediaIO::closedSignal` the pipeline emits
  `MediaPipeline::closedSignal` and transitions to
  `State::Closed`. Natural EOF (all sources exhaust their
  input) triggers the same cascade automatically, so consumers
  have a single completion signal to wait on regardless of what
  stopped the run.

`MediaPipeline::close` can be called blocking (waits until
the cascade completes, pumping the owning `EventLoop` when
invoked from it) or non-blocking (returns immediately, learns
completion via `closedSignal`). `MediaPipeline::finishedSignal`
is repurposed to fire alongside `closedSignal` at the end of the
cascade and carries a `clean` bool that summarises whether any
operational or close-time error was observed.

## JSON schema {#mediapipeline_json}

```json
{
  "metadata": { "Title": "TPG to MOV + display", "Author": "libpromeki" },
  "stages": [
    {
      "name": "source",
      "type": "TPG",
      "mode": "Source",
      "config": {
        "VideoEnabled":    true,
        "VideoPattern":    "ColorBars",
        "VideoSize":       "1920x1080",
        "VideoPixelDesc":  "YUV422_10_BT709_Limited",
        "AudioEnabled":    true,
        "AudioMode":       "Tone"
      }
    },
    {
      "name": "csc",
      "type": "CSC",
      "mode": "Transform",
      "config": { "OutputPixelDesc": "RGBA8_sRGB" }
    },
    {
      "name": "display",
      "type": "SDLPlayer",
      "mode": "Sink",
      "config": {}
    },
    {
      "name": "recorder",
      "type": "QuickTime",
      "mode": "Sink",
      "config": { "Filename": "/tmp/out.mov" }
    }
  ],
  "routes": [
    { "from": "source",   "to": "csc" },
    { "from": "csc",      "to": "display" },
    { "from": "csc",      "to": "recorder" }
  ]
}
```

Keys in the stage-level `config` are `MediaConfig` IDs and
round-trip via `VariantDatabase::toJson` /
`VariantDatabase::fromJson`, so every spec documented in
`include/promeki/mediaconfig.h` is expressible. The `mode`
string uses `MediaPipelineConfig::modeName` — one of
`"Source"`, `"Sink"`, `"Transform"`, or `"NotOpen"`.

## Topology rules {#mediapipeline_topology}

- Fan-out is unrestricted — any stage may appear as the `from` of
  multiple routes and each destination back-pressures independently
  via `MediaIO::writesAccepted` and `frameWanted`.
- Fan-in is rejected at `MediaPipeline::build` time in the
  initial implementation — every stage has at most one incoming
  route. A future converter / mixer extension will lift this.
- Cycles are rejected by `MediaPipelineConfig::validate`.
- Orphan stages (not referenced by any route in a multi-stage
  config) are rejected by validation.

## Statistics {#mediapipeline_stats}

`MediaPipeline::stats` walks every live stage, calls
`MediaIO::stats`, and rolls the results up into a
`MediaPipelineStats` snapshot. The snapshot carries three
buckets:

- `perStage` — a `MediaIOStats` record per stage, keyed by the
  stage name.
- `aggregate` — a single `MediaIOStats` with summed counters,
  summed throughput, averaged latencies, and max peaks across
  every stage. Reduction rules are documented on
  `MediaPipelineStats`.
- `pipeline` — a `PipelineStats` record describing the drain
  layer itself: `PipelineStats::FramesProduced` (frames pulled
  from sources and fanned out), `PipelineStats::WriteRetries`
  (non-blocking `writeFrame` TryAgain events held for retry),
  `PipelineStats::PipelineErrors`,
  `PipelineStats::SourcesAtEof` / `PipelineStats::PausedEdges`
  (live drain shape), `PipelineStats::State` (lifecycle name),
  and `PipelineStats::UptimeMs` (ms since `MediaPipeline::start`).

The whole snapshot round-trips through JSON and DataStream, so it
can be logged, persisted, or sent over IPC without per-bucket
plumbing.

## mediaplay CLI integration {#mediapipeline_mediaplay}

The bundled `mediaplay` utility exposes the pipeline through
three preset / telemetry flags:

- `--save-pipeline <PATH>` — build a `MediaPipelineConfig`
  from the other flags (`-s` / `-c` / `-d`, etc.), write it as
  a JSON preset, and exit without opening any stage.
- `--pipeline <PATH>` — load a previously-written preset and
  run it. Other non-stage flags (`--duration`, `--frame-count`,
  `--stats`, ...) still apply. Stages whose `type` is `"SDL"`
  are recognised and the SDL UI is built and injected via
  `MediaPipeline::injectStage` before `MediaPipeline::build`
  runs.
- `--write-stats <PATH>` — append one JSON object per
  `--stats-interval` to PATH (JSON-lines), plus a final aggregate
  snapshot at shutdown. Each line is a `MediaPipelineStats`
  `toJson` with two extra fields: `timestamp` (ISO-style
  wall-clock at write time) and `phase` (`"stats"` for periodic
  ticks, `"final"` for the shutdown record). Setting
  `--write-stats` also turns on the stats collector when no
  `--stats-interval` was given (default 1 s).

## Code walk-through {#mediapipeline_example}

Build and run a TPG → CSC → QuickTime pipeline from an
in-memory config:

```cpp
MediaPipelineConfig cfg;

MediaPipelineConfig::Stage src;
src.name = "src";
src.type = "TPG";
src.mode = MediaIO::Source;
src.config.set(MediaConfig::VideoEnabled, true);
cfg.addStage(src);

MediaPipelineConfig::Stage csc;
csc.name = "csc";
csc.type = "CSC";
csc.mode = MediaIO::Transform;
csc.config.set(MediaConfig::OutputPixelDesc,
               PixelDesc(PixelDesc::RGBA8_sRGB));
cfg.addStage(csc);

MediaPipelineConfig::Stage sink;
sink.name = "mov";
sink.path = "/tmp/out.mov";           // file sink, type auto-detected
sink.mode = MediaIO::Sink;
cfg.addStage(sink);

cfg.addRoute("src", "csc");
cfg.addRoute("csc", "mov");

// Persist for later re-use.
cfg.saveToFile("/tmp/tpg-to-mov.json");

// Run.
Application app(argc, argv);
MediaPipeline pipeline;
pipeline.build(cfg);
pipeline.open();
pipeline.start();

// Quit once the graceful close cascade has run to completion.
// Natural EOF auto-initiates the cascade; Ctrl-C can be wired to
// pipeline.close(false) via Application::setQuitRequestHandler.
pipeline.closedSignal.connect(
    [](Error err) { Application::quit(err.isOk() ? 0 : 1); }, &pipeline);

return app.exec();
```
