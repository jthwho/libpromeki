# MediaPipeline (MediaIO-based)

**Phase:** 4A
**Dependencies:** MediaIO framework (`proav_nodes.md`), `MediaConfig` (consolidated catalog of well-known config keys, complete), VariantDatabase, JsonObject
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document describes the `MediaPipeline` class — a data-driven pipeline builder that instantiates and wires together `MediaIO` instances from a declarative configuration. (An older `MediaNode` / `MediaPipeline` layer existed in the tree; it has been deleted and this new class takes the name.)

**Foundation in place:** the `MediaConfig` consolidation (all well-known config keys live as `static inline const ID` members on a single class inheriting `VariantDatabase<MediaConfigTag>`) is the substrate this pipeline plugs into.  Every stage's `MediaConfig` round-trips to JSON via `VariantDatabase::toJson()` / `fromJson()` for free, and a key set on a `Converter` stage flows unchanged into the codec layer (e.g. `MediaConfig::JpegQuality` already wires from a `mediaplay --oc` flag through `MediaIOTask_ImageFile` → `ImageFileIO_JPEG` → `Image::convert` → `JpegImageCodec::configure` with no per-layer translation).  The follow-up below is the next consumer.

---

## Overview

`MediaPipeline` takes a hierarchical configuration describing a full media pipeline — the set of `MediaIO` stages, each stage's `MediaConfig`, and how frames flow between stages — and builds a running pipeline out of `MediaIO` objects. The same configuration can be expressed as a live in-memory data object or as a JSON document; round-tripping either form produces the same pipeline.

**Why this class exists:**
- All the real work already lives in `MediaIO` and its backends. The pipeline's only job is wiring.
- `mediaplay` and every future media tool should be driven by a config description rather than hard-coded MediaIO plumbing.
- A single JSON document can describe a complete end-to-end pipeline (e.g. TPG → CSC converter → RTP video sink + QuickTime file sink), which is the foundation for GUIs, presets, and automation.
- Keeps MediaIO itself simple: the controller/task split stays unchanged, and the pipeline is a thin composition layer on top.

---

## Design Goals

- **MediaIO is the atomic unit.** Every stage in a MediaPipeline is a `MediaIO` instance. No "node" wrapper type.
- **Declarative topology.** The pipeline config describes stages and routes; the builder resolves the DAG and runs it.
- **Fan-out and fan-in supported.** One source can feed multiple sinks; multiple sources can feed one converter/mixer (once converter stages support multi-input).
- **JSON symmetry.** `toJson()` / `fromJson()` are first-class, not afterthoughts. `Variant`-based configs make this free for leaf values.
- **Lifecycle mirrors MediaIO.** `open()`/`close()`/`start()`/`stop()` on the pipeline fans out to every owned MediaIO on its Strand.
- **Signal-driven frame movement.** Stages push frames via the `frameReady` signal; no dedicated pumper thread. The pipeline connects `frameReady` from each producer to `writeFrame` on each connected consumer.
- **Error propagation.** Any MediaIO error surfaces on the pipeline as a single aggregated signal, with the offending stage name attached.

---

## Configuration Data Model

### `MediaPipelineConfig`

New shareable data object that describes a complete pipeline. Round-trips through `DataStream` and `JsonObject`.

**Files:**
- [ ] `include/promeki/mediapipelineconfig.h`
- [ ] `src/proav/mediapipelineconfig.cpp`
- [ ] `tests/mediapipelineconfig.cpp`

**Shape:**
- [ ] `List<StageConfig> stages` — one entry per MediaIO instance
- [ ] `List<Route> routes` — explicit `from` → `to` edges referencing stages by name
- [ ] `Metadata metadata` — pipeline-wide metadata (name, author, description)

**`StageConfig`** — describes one MediaIO instance:
- [ ] `String name` — unique identifier used by routes; also used in error messages
- [ ] `String type` — MediaIO format name (e.g. `"TPG"`, `"QuickTime"`, `"RtpVideo"`, `"ImageFile"`, `"Converter"`)
- [ ] `MediaIO::Mode mode` — Reader / Writer / ReadWrite
- [ ] `MediaConfig config` — full `VariantDatabase` passed to `MediaIO::create`

**`Route`** — describes one frame-flow edge:
- [ ] `String from` — source stage name
- [ ] `String to` — sink stage name
- [ ] `String fromTrack = ""` — optional source sub-stream selector (video/audio/timecode) for future multi-output stages
- [ ] `String toTrack = ""` — optional sink sub-stream selector

**Operations:**
- [ ] `Error validate() const` — checks that all route endpoints reference existing stage names, detects cycles, verifies every non-source stage has at least one incoming route, verifies every non-sink stage has at least one outgoing route
- [ ] `JsonObject toJson() const`
- [ ] `static Result<MediaPipelineConfig> fromJson(const JsonObject &obj)`
- [ ] `Error saveToFile(const FilePath &path) const` — writes JSON
- [ ] `static Result<MediaPipelineConfig> loadFromFile(const FilePath &path)` — reads JSON
- [ ] DataStream operators via the existing `VariantDatabase` framework
- [ ] Doctest: round-trip through JSON, round-trip through DataStream, validation errors, cycle detection

### JSON Representation

A pipeline that pulls the TPG, runs it through a colorspace converter, and fans the result into both an SDL player and a QuickTime writer:

```json
{
  "metadata": { "Name": "TPG to MOV + display", "Author": "libpromeki" },
  "stages": [
    {
      "name": "source",
      "type": "TPG",
      "mode": "Reader",
      "config": {
        "ConfigVideoEnable": true,
        "ConfigVideoPattern": "ColorBars",
        "ConfigVideoSize": "1920x1080",
        "ConfigVideoPixelFormat": "YUV422_10_BT709_Limited",
        "ConfigAudioEnable": true,
        "ConfigAudioMode": "Tone"
      }
    },
    {
      "name": "csc",
      "type": "Converter",
      "mode": "ReadWrite",
      "config": {
        "ConfigOutputPixelFormat": "RGBA8_sRGB"
      }
    },
    {
      "name": "display",
      "type": "SDLPlayer",
      "mode": "Writer",
      "config": {}
    },
    {
      "name": "recorder",
      "type": "QuickTime",
      "mode": "Writer",
      "config": { "ConfigFilename": "/tmp/out.mov" }
    }
  ],
  "routes": [
    { "from": "source",   "to": "csc" },
    { "from": "csc",      "to": "display" },
    { "from": "csc",      "to": "recorder" }
  ]
}
```

---

## MediaPipeline Class

**Files:**
- [ ] `include/promeki/mediapipeline.h`
- [ ] `src/proav/mediapipeline.cpp`
- [ ] `tests/mediapipeline.cpp`
- [ ] `docs/mediapipeline.dox` (authoring guide + JSON schema + worked examples)

**Class responsibilities:**
- [ ] Derive from `ObjectBase`, `PROMEKI_OBJECT`
- [ ] Owns a `MediaPipelineConfig`
- [ ] Owns one `MediaIO *` per stage (map keyed by stage name)
- [ ] `Error build(const MediaPipelineConfig &config)` — validates config, instantiates each MediaIO via `MediaIO::create`, calls `adoptTask()` for backends that require external construction (SDLPlayer), stores stages in topological order
- [ ] `Error open(unsigned int timeoutMs = 0)` — opens every stage in topological order; on any failure closes all already-opened stages and returns the error
- [ ] `Error close(unsigned int timeoutMs = 0)` — closes every stage in reverse topological order
- [ ] `Error start(unsigned int timeoutMs = 0)` — begins frame movement; connects signals (see below) and kicks off source-stage reads
- [ ] `Error stop(unsigned int timeoutMs = 0)` — stops frame movement, cancels pending MediaIO commands
- [ ] `MediaIO *stage(const String &name) const` — direct access for tests or dynamic re-config
- [ ] `List<String> stageNames() const`
- [ ] `MediaPipelineConfig config() const`
- [ ] Accessors for pipeline-wide stats (aggregated from each stage's `MediaIOStats`)

**Signal plumbing:**
- [ ] For each route `from → to`: connect `from->frameReadySignal` to a slot that calls `to->writeFrame(frame, /*block=*/false)`; if `writeFrame` returns `Error::TryAgain`, emit a back-pressure signal and leave the frame pending on the producer until the consumer emits `frameWanted`
- [ ] Fan-out: one producer's `frameReadySignal` connects to N consumers; each consumer independently back-pressures
- [ ] Fan-in (future): one consumer accepts multiple producers; dispatch policy declared in the consumer's StageConfig (mix / interleave / select)
- [ ] Propagate `writeError` and other MediaIO error signals into a single `pipelineError(String stageName, Error err)` signal
- [ ] Emit `stageOpened(String)`, `stageClosed(String)`, `stageStarted(String)`, `stageStopped(String)` lifecycle signals for observers (mediaplay stats, TUI, etc.)

**Topology:**
- [ ] Build a DAG from the routes, topologically sort stages, reject cycles during `build()`
- [ ] Source stages (no incoming routes) are started last so that their first `frameReady` has somewhere to go
- [ ] Sink stages (no outgoing routes) are opened first so they are ready when the first frame arrives
- [ ] Validation: converter stages (ReadWrite) must have exactly one incoming and one outgoing route in the initial implementation; fan-in on converters is deferred

**Implementation checklist:**
- [ ] Build → open → start → stop → close lifecycle verified against multi-stage integration tests
- [ ] Fan-out pipeline test: TPG → { ImageFile seq, QuickTime writer } — both sinks receive all frames
- [ ] Fan-through test: TPG → Converter → ImageFile — converted pixels verified at sink
- [ ] Error propagation test: writer with invalid path emits `pipelineError` during `open()`, leaves pipeline in closed state
- [ ] Cancellation test: `stop()` while frames are flowing cancels pending MediaIO commands cleanly, no stuck futures
- [ ] JSON round-trip test: load a multi-stage pipeline from JSON, run it, verify frame count on sinks
- [ ] Stats aggregation test: run a short pipeline, verify per-stage and aggregated stats are populated

---

## Dependencies and Enabling Work

### Converter MediaIO Backend — COMPLETE

The pipeline leans on the `"Converter"` backend (ReadWrite MediaIO that takes a frame on `writeFrame()` and produces a transformed frame on `readFrame()`) for every CSC / codec / audio-format transform. See `proav_nodes.md` under **MediaIOTask_Converter**.

### RTP MediaIO Backend — COMPLETE

The unified `MediaIOTask_Rtp` supports both Writer and Reader modes (see `proav_nodes.md`).  Networked pipelines are now expressible: an RTP sink stage feeds packets to the network, and an RTP source stage reads them back via SDP-driven auto-config.

### MediaConfig Variant type coverage

For JSON `fromJson` to work on every stage's config, every Variant type used in a `MediaConfig` must have `toJson()`/`fromJson()` wiring. Most existing primitives and `VariantDatabase` fields already do. New types (PixelFormat, MediaDesc, FrameRate, Color, etc.) may need Variant JSON conversion added — this work is tracked in `core_utilities.md` (Variant Enhancements) and should be completed alongside the pipeline.

### Per-stage MediaConfig forwarding — FOLLOW-ON

**Status: foundation complete, pipeline work pending.**

The `MediaConfig` consolidation that landed alongside the codec abstraction means a single `MediaConfig` now spans every layer that touches a frame:

- A `MediaPipelineConfig::StageConfig` already carries a `MediaConfig` per stage (see [Configuration Data Model](#configuration-data-model)).
- `MediaIO::create(...)` consumes that `MediaConfig` directly — no per-backend translation step.
- Inside the converter / file backends, the same `MediaConfig` is forwarded to `Image::convert()` and `ImageFileIO::save()/load()` so codec knobs (`MediaConfig::JpegQuality`, `MediaConfig::JpegSubsampling`, future `MediaConfig::*Bitrate`, …) flow end-to-end without any key mapping.
- Doctest coverage in `tests/jpegimagecodec.cpp` (`JpegImageCodec_ConfigureFromMediaConfig`) and `tests/imagefileio_jpeg.cpp` (`ImageFileIO JPEG: save honours MediaConfig::JpegQuality`) locks the dispatch in.

This unblocks the following pipeline-level work and removes a lot of speculation from the original plan:

- [ ] `MediaPipelineConfig::StageConfig::config` is the single in-memory + JSON representation of every stage's settings — no per-backend `Map<String, Variant>` translation needed.
- [ ] `MediaPipeline::build()` can hand each stage's `MediaConfig` straight to `MediaIO::create()` once and never touch it again.
- [ ] CLI tooling (`mediaplay --pipeline`, future GUI editors) walks `MediaConfig` keys via `forEach()` to render schema; the keys autodiscover from `MediaConfig`'s static catalog plus per-backend `defaultConfig()` snapshots.
- [ ] When the `ImageCodec::configKeys()` accessor lands (see `proav_nodes.md` → "Codec abstraction follow-ups"), pipeline tooling can render per-codec key documentation alongside per-backend schema.

The interesting next consumer is `mediaplay --save-pipeline`: with `MediaConfig::toJson()` already in place, exporting a running pipeline to disk is "for each stage, dump its live `MediaConfig`" with no other plumbing.

---

## Design Notes

### Copy-on-write mutation safety

Multiple stages may hold references to the same `Buffer::Ptr` (shared via `SharedPtr` COW) when the pipeline fans out. Mutating consumers must call `ensureExclusive()` on `Image` and `Audio` before writing buffer data. `Image::ensureExclusive()` / `isExclusive()` exist; `Audio::ensureExclusive()` / `isExclusive()` still need to be added (tracked in the remaining-work list below). Buffer allocation (including COW clones) goes through the buffer's `MemSpace`, so pooled allocation is a MemSpace-level concern.

### Memory space statistics — COMPLETE

`MemSpace::Stats` has landed: every MemSpace instance carries a lock-free `Atomic<uint64_t>`-based Stats object tracking alloc / release / copy / fill counts and bytes, allocation failures, largest single allocation, live / peak outstanding allocations, and copy failures. A per-instance `Stats::Snapshot` POD is returned by `snapshot()` for reporting. `statsReport()` / `allStatsReport()` format the counters as a `StringList` (human-readable byte sizes via `String::fromByteCount`); `logStats()` / `logAllStats()` dump that report to the promeki log at Info level. `mediaplay --memstats` wires this into end-of-run diagnostics. A pool implementation remains optional future work.

---

## Remaining Work Checklist

- [ ] `Audio::ensureExclusive()` / `Audio::isExclusive()` — parity with `Image`
- [x] `MemSpace::Stats` — per-space atomic counters, `stats()` / `statsSnapshot()` / `resetStats()`, peak/live/max-alloc tracking, `statsReport()` / `logStats()` / `logAllStats()`, wired into `mediaplay --memstats`
- [ ] `MemSpacePool` — recycling `MemSpace` for fixed-size buffers, optional pre-allocation, LIFO recycle stack, pool hit/miss metrics
- [ ] `MediaPipelineConfig` data object (JSON + DataStream round-trip)
- [ ] `MediaPipeline` class (build/open/start/stop/close, signal plumbing, fan-out, validation)
- [ ] `docs/mediapipeline.dox` — authoring guide, JSON schema, worked examples
- [ ] Integration tests covering fan-out, converter pass-through, error propagation, cancellation, JSON round-trip, stats aggregation
- [ ] Migration of `mediaplay` to build its pipeline via `MediaPipeline::build()` from a config composed from CLI options
