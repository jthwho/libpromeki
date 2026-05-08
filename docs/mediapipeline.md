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
  "frameCount": 300,
  "stages": [
    {
      "name": "source",
      "type": "TPG",
      "mode": "Source",
      "config": {
        "VideoEnabled":    true,
        "VideoPattern":    "ColorBars",
        "VideoSize":       "1920x1080",
        "VideoPixelFormat":  "YUV422_10_BT709_Limited",
        "AudioEnabled":    true,
        "AudioMode":       "Tone"
      }
    },
    {
      "name": "csc",
      "type": "CSC",
      "mode": "Transform",
      "config": { "OutputPixelFormat": "RGBA8_sRGB" }
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

The top-level `frameCount` key is optional: when present and
positive, it caps the number of frames the pipeline will deliver
to each sink. The cap is enforced at runtime — once a sink has
received its target count the pipeline closes that sink cleanly
(its trailer / finalization still fires) and then cascades the
close to upstream stages; frames produced by a source after the
cap has been reached are silently dropped. For interframe-coded
stages (`VideoCodec::CodingTemporal`) the cut is deferred to the
next `Frame::isSafeCutPoint` — i.e. the next frame that begins a
new GOP — so a sink always ends on a sequence of complete GOPs,
even if that means writing up to one extra GOP beyond the target.
Omit the key (or set it to `0`) to let the pipeline run until
every source naturally hits EOS.

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
src.role = MediaPipelineConfig::StageRole::Source;
src.config.set(MediaConfig::VideoEnabled, true);
cfg.addStage(src);

MediaPipelineConfig::Stage csc;
csc.name = "csc";
csc.type = "CSC";
csc.role = MediaPipelineConfig::StageRole::Transform;
csc.config.set(MediaConfig::OutputPixelFormat,
               PixelFormat(PixelFormat::RGBA8_sRGB));
cfg.addStage(csc);

MediaPipelineConfig::Stage sink;
sink.name = "mov";
sink.path = "/tmp/out.mov";           // file sink, type auto-detected
sink.role = MediaPipelineConfig::StageRole::Sink;
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

## Transport controls {#mediapipeline_transport}

A built pipeline is classified as either a _playback_ or _capture_
pipeline via `MediaPipelineConfig::Kind` and exposes a transport
surface tailored to that classification on top of the lifecycle
state machine. Transport state lives orthogonally from the
pipeline lifecycle: `MediaPipeline::PlaybackState` and
`MediaPipeline::CaptureState` only matter while the pipeline is in
`MediaPipeline::State::Running`.

### Playback transport {#mediapipeline_transport_playback}

A playback pipeline must mark exactly one stage with
`MediaPipelineConfig::Stage::pacesPipeline = true` — the stage
whose `MediaIOPortGroup` clock paces output. The pipeline resolves
the pacing stage / port-group / clock at `open()` and routes
pause / resume / seek through it.

```cpp
MediaPipelineConfig cfg;
cfg.setKind(MediaPipelineConfig::Kind::Playback);

MediaPipelineConfig::Stage src;
src.name = "src";
src.type = "ImageFile";
src.path = "/path/to/sequence/####.dpx";
src.role = MediaPipelineConfig::StageRole::Source;
cfg.addStage(src);

MediaPipelineConfig::Stage out;
out.name  = "ndi";
out.type  = "NDI";
out.role  = MediaPipelineConfig::StageRole::Sink;
out.pacesPipeline = true;            // NDI sender's clock paces output
cfg.addStage(out);

cfg.addRoute("src", "ndi");
cfg.setStartPaused(true);            // park on first frame for a UI

MediaPipeline p;
p.build(cfg);
p.open();
p.start();                           // lands in PlaybackState::Paused

p.play();                            // -> Playing
p.pause();                           // -> Paused
p.togglePlayPause();                 // -> Playing
p.setRate(0.5);                      // slow motion
p.setRate(2.0);                      // fast forward
p.seek(FrameNumber(120));            // -> Seeking, then Paused
p.stepForward(1);                    // pause + seek(currentFrame()+1)
p.stepBackward(5);                   // pause + seek(currentFrame()-5)
```

`MediaPipeline::PlaybackState` follows the obvious states:

- `Idle` — pipeline is not Running.
- `Playing` — pacing clock running, frames flowing.
- `Paused` — pacing clock paused, output frame held.
- `Seeking` — published while a `seek()` is in flight.
- `Ended` — source EOS reached at current rate.

Transport transitions emit:

- `playbackStateChangedSignal(PlaybackState)` — typed signal.
- `rateChangedSignal(double)` — on every successful `setRate`.
- `positionChangedSignal(FrameNumber)` — after each successful seek.
- A `PipelineEvent::Kind::TransportStateChanged` envelope on the
  subscriber bus with `metadata["scope"]` set to `"playback"` or
  `"rate"` so demo UIs / network relays can render every transport
  transition without typed signal connections.

The implementation pauses the pacing-stage clock through the
existing `Clock::setPause` machinery; downstream pacing-aware
sinks (NDI/RTP senders) honour the pause natively. The synthetic
`MediaIOClock` and `SyntheticClock` used by file-driven playback
are pause-capable too via `ClockPauseMode::PausesRawKeepsRunning`,
so the pump idles cleanly while the clock's bookkeeping holds the
reported time steady. `MediaIOPortGroup::rate()` is a `double`
multiplier (1.0 = normal, 0.5 = half-speed, 2.0 = double-speed,
negative for reverse, 0.0 for hold). Backends consult
`MediaIOPortGroup::nextStep()` per read to translate fractional
rates into integer per-tick advances — slow motion repeats frames,
fast forward skips frames.

### Capture transport {#mediapipeline_transport_capture}

A capture pipeline marks at least one stage with
`MediaPipelineConfig::Stage::captureSink = true`. The pipeline
resolves the flagged stages at `open()` and gates frame delivery
to those sinks via the per-sink valve on
`MediaIOPortConnection`. Frames continue to flow through the rest
of the graph (preview / monitor sinks see them); only the capture
sinks are gated.

```cpp
MediaPipelineConfig cfg;
cfg.setKind(MediaPipelineConfig::Kind::Capture);

MediaPipelineConfig::Stage src;
src.name = "ingest";
src.type = "NDI";
src.role = MediaPipelineConfig::StageRole::Source;
cfg.addStage(src);

MediaPipelineConfig::Stage rec;
rec.name = "rec";
rec.type = "QuickTime";
rec.path = "/recordings/clip.mov";
rec.role = MediaPipelineConfig::StageRole::Sink;
rec.captureSink = true;
cfg.addStage(rec);

cfg.addRoute("ingest", "rec");

MediaPipeline p;
p.build(cfg);
p.open();
p.start();                                      // CaptureState::Idle

// Option A — manual record button:
p.startCapture();                               // -> Recording
p.pauseCapture();                               // -> Paused
p.resumeCapture();                              // -> Recording (next frame stamped ForceKeyframe)
p.stopCapture();                                // -> Idle

// Option B — metadata-triggered start:
p.setCaptureTrigger(String("Meta.FrameKeyframe == true"));
p.armCapture();                                 // -> Armed; recording starts on the first matching frame
```

`MediaPipeline::CaptureState`:

- `Idle` — capture sinks gated off, no trigger armed.
- `Armed` — trigger evaluated per frame; gate stays closed until
  it matches.
- `Recording` — gate open, frames flowing to the capture sinks.
- `Paused` — mid-recording pause; first post-resume frame carries
  `Metadata::ForceKeyframe` so a downstream encoder cuts a clean
  IDR.

Triggers implement `MediaPipelineTrigger`:

- `MediaPipelineFunctionTrigger` wraps any
  `std::function<bool(const Frame &)>` — most flexible for inline
  lambdas that need to inspect non-metadata payload fields.
- `MediaPipelineQueryTrigger` parses a `VariantQuery<Frame>`
  expression (`"Meta.Timecode >= '01:00:00:00'"`,
  `"Meta.FrameKeyframe == true"`, …) — round-trips through config
  files and is the idiomatic form for pure metadata predicates.

`MediaPipeline::setCaptureTrigger` is overloaded for callables,
query strings, and pre-built `MediaPipelineTrigger::UPtr`
instances. The trigger is evaluated once per frame on the
connection's pump thread; a match opens the capture sink gate and
stamps `Metadata::ForceKeyframe` on the matched frame so the
recording starts at a clean keyframe boundary.

Backends that need extra teardown / setup on capture pause / resume
(HW encoder low-power state, network heartbeat throttling, …) can
override `MediaIO::setIngestPaused(bool)` — the pipeline's
pause / resume calls fan it out to every flagged capture sink. The
default override is a no-op so existing backends require no change.

### Subscriber events {#mediapipeline_transport_events}

Every transport transition (playback or capture) is mirrored on
the `subscribe()` bus via `PipelineEvent::Kind::TransportStateChanged`.
The event's `payload` carries the new state name as a `String`;
the event's `metadata` carries `"scope"` set to `"playback"`,
`"capture"`, or `"rate"` so consumers can distinguish the source
without separate slot connections.
