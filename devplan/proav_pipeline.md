# MediaPipeline (MediaIO-based)

**Phase:** 4A
**Dependencies:** MediaIO framework (`proav_nodes.md`), VariantDatabase, JsonObject
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document describes the **new** `MediaPipeline` class — a data-driven pipeline builder that instantiates and wires together `MediaIO` instances from a declarative configuration. It is unrelated to the legacy `MediaNode`/`MediaPipeline` classes (see "Deprecated: MediaNode/MediaPipeline legacy" at the bottom of this file).

---

## Overview

`MediaPipeline` takes a hierarchical configuration describing a full media pipeline — the set of `MediaIO` stages, each stage's `MediaIOConfig`, and how frames flow between stages — and builds a running pipeline out of `MediaIO` objects. The same configuration can be expressed as a live in-memory data object or as a JSON document; round-tripping either form produces the same pipeline.

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
- [ ] `MediaIOConfig config` — full `VariantDatabase` passed to `MediaIO::create`

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
        "ConfigVideoPixelDesc": "YUV422_10_BT709_Limited",
        "ConfigAudioEnable": true,
        "ConfigAudioMode": "Tone"
      }
    },
    {
      "name": "csc",
      "type": "Converter",
      "mode": "ReadWrite",
      "config": {
        "ConfigOutputPixelDesc": "RGBA8_sRGB"
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

**Note:** This replaces the deprecated `include/promeki/mediapipeline.h` / `src/proav/mediapipeline.cpp`. Those files must be deleted (along with all `MediaNode`/`MediaPipelineConfig` legacy files) as the final step of this phase, once the new class is built and `mediaplay` + tests are migrated. See "Deprecated legacy" below.

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

### Converter MediaIO Backend — REQUIRED

This pipeline class assumes a `"Converter"` backend exists (ReadWrite MediaIO that takes a frame on `writeFrame()` and produces a transformed frame on `readFrame()`). That backend is specified in `proav_nodes.md` under **MediaIOTask_Converter**. The pipeline cannot express pass-through CSC / codec transcode without it.

### RTP MediaIO Backends — REQUIRED for networked pipelines

See `proav_nodes.md` under **MediaIOTask_RtpVideo** and **MediaIOTask_RtpAudio**. Needed to replicate vidgen-style RTP streaming through the new pipeline. Until they exist, only file/container-based pipelines are expressible.

### JPEG ImageFile Backend — NICE-TO-HAVE

See `proav_nodes.md` under **MediaIOTask_ImageFile** (JPEG extension). Needed for JPEG still-image pipelines to work without transcoding round-trips.

### MediaIOConfig Variant type coverage

For JSON `fromJson` to work on every stage's config, every Variant type used in a `MediaIOConfig` must have `toJson()`/`fromJson()` wiring. Most existing primitives and `VariantDatabase` fields already do. New types (PixelDesc, MediaDesc, FrameRate, Color, etc.) may need Variant JSON conversion added — this work is tracked in `core_utilities.md` (Variant Enhancements) and should be completed alongside the pipeline.

---

## Deprecated: MediaNode / MediaPipeline Legacy

**Status: DEPRECATED but retained until migration is complete.**

The original `MediaNode`, `MediaPipeline`, `MediaNodeConfig`, `MediaPipelineConfig`, `MediaSink`, `MediaSource`, and all the concrete `*Node` classes (`TestPatternNode`, `JpegEncoderNode`, `RtpVideoSinkNode`, `RtpAudioSinkNode`, `FrameDemuxNode`, `TimecodeOverlayNode`) are deprecated. They will be removed once:

1. MediaIO backends exist for every capability they provided (see `proav_nodes.md`: Converter, RTP video, RTP audio, JPEG ImageFile extension)
2. The new `MediaPipeline` described above is built and tested
3. Utilities that depend on them are ported or deleted:
   - `utils/vidgen/` — will be removed (replaced by `mediaplay` with an appropriate pipeline config)
   - `utils/mediaplay/` — will be updated to build a `MediaPipeline` from a config instead of hand-plumbing MediaIOs
   - `utils/imgtest/`, `utils/testrender/` — audit and either port or remove
4. Tests that exercise the legacy classes are migrated to exercise the new pipeline

**Do not extend the deprecated classes.** Bug fixes in the deprecated path are acceptable only if they unblock migration; otherwise, the fix belongs in the new MediaIO backend or the new MediaPipeline.

**Files that will be removed:**
- `include/promeki/medianode.h`, `src/proav/medianode.cpp`
- `include/promeki/medianodeconfig.h`
- `include/promeki/mediapipeline.h`, `src/proav/mediapipeline.cpp` (old)
- `include/promeki/mediapipelineconfig.h`, `src/proav/mediapipelineconfig.cpp` (old)
- `include/promeki/mediasink.h`, `include/promeki/mediasource.h`
- All `*node.h` / `*node.cpp` pairs under `include/promeki/` and `src/proav/`
- `tests/medianode.cpp`, `tests/mediapipeline.cpp`, `tests/*node.cpp`

The final removal is a single large deletion commit once the migration is verified.

---

## Design Notes retained from legacy (still valid)

### Copy-on-write mutation safety

Multiple stages may hold references to the same `Buffer::Ptr` (shared via `SharedPtr` COW) when the pipeline fans out. Mutating consumers must call `ensureExclusive()` on `Image` and `Audio` before writing buffer data. `Image::ensureExclusive()` / `isExclusive()` exist; `Audio::ensureExclusive()` / `isExclusive()` still need to be added (tracked in the remaining-work list below). Buffer allocation (including COW clones) goes through the buffer's `MemSpace`, so pooled allocation is a MemSpace-level concern.

### Memory space statistics

MemSpace has no visibility into allocation behaviour. For pipeline debugging and performance analysis, per-space statistics and a pool implementation are still useful. This work is independent of the pipeline class itself and can land whenever needed — the pipeline will pick up pooled allocation automatically via the MemSpace layer.

---

## Remaining Work Checklist

- [ ] `Audio::ensureExclusive()` / `Audio::isExclusive()` — parity with `Image`
- [ ] `MemSpace::Stats` — per-space counters, `stats()` accessor, `resetStats()`, `peakCount`/`peakBytes`, COW detach tracking
- [ ] `MemSpacePool` — recycling `MemSpace` for fixed-size buffers, optional pre-allocation, LIFO recycle stack, pool hit/miss metrics
- [ ] `MediaPipelineConfig` data object (JSON + DataStream round-trip)
- [ ] `MediaPipeline` class (build/open/start/stop/close, signal plumbing, fan-out, validation)
- [ ] `docs/mediapipeline.dox` — authoring guide, JSON schema, worked examples
- [ ] Integration tests covering fan-out, converter pass-through, error propagation, cancellation, JSON round-trip, stats aggregation
- [ ] Migration of `mediaplay` to build its pipeline via `MediaPipeline::build()` from a config composed from CLI options
- [ ] Deletion of all deprecated `MediaNode`/`MediaPipeline` legacy files and their tests (final step)
