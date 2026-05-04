# Benchmarking Infrastructure

**Phase:** cross-cutting (development tool; regression/CI later)
**Dependencies:** Phase 1 (ElapsedTimer, Duration, List, Map, Mutex), Phase 2 (File, JsonObject), Phase 4 (MediaIO framework) for the MediaIO hook work
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every new class requires complete doctest unit tests. See `README.md` for full requirements.

This document replaces the stub "Benchmark Infrastructure" section in `devplan/README.md` and the wishlist line in [proav/optimization.md](../proav/optimization.md) about a future `benchmark-promeki` target.

**Completed:** `BenchmarkRunner` core + `StatsAccumulator`, `promeki-bench` CLI driver with columnized output / build-type warnings / filtered count + duration estimate, CSC suite with programmatic pair generation, MediaIO stamp hooks (enqueue / dequeue / taskBegin / taskEnd) with sink-aware reporter submission, MediaIO identifier triple (`localId` / `Name` / `UUID`), three new `MediaConfig` keys (`Name`, `Uuid`, `EnableBenchmark`), **Part D** live telemetry (`RateTracker`, automatic `BytesPerSecond`/`FramesPerSecond`, base-class drop/repeat/late counters via `MediaIOTask::noteFrame*` helpers, latency keys derived from the attached reporter, `PendingOperations` from `Strand::pendingCount()`, `MediaIOStats::toString()` compact log-line renderer, urgent `stats()` dispatch so pollers don't block behind I/O queues), and `mediaplay --stats` / `--stats-interval` for live per-stage telemetry printing. **Image-data and inspector microbench suites** (`utils/promeki-bench/cases/imagedata.cpp` and `cases/inspector.cpp`) — encoder and decoder hot-path cases for RGBA8 / YUYV / planar 4:2:2 / v210 at 1920×1080 (cross-product configurable via `imagedata.format+=` / `imagedata.size+=`), plus a full TPG → Inspector pipeline case. `main.cpp` now silences info-level library logs at startup so bench output is clean.

**Remaining:** the non-CSC / non-imagedata microbench suites (network / codec / container / concurrency / variantdatabase / histogram), **Part E** MediaIO end-to-end bench cases (blocked on `MediaPipeline`), and CI regression integration.

---

## Overview

Three distinct things get conflated under the word "benchmarking". We want explicit homes for each:

1. **Microbenchmarks** — small, isolated, deterministic measurements of library primitives (CSC, codecs, container ops, socket batch send, strand dispatch). Ship as cases in a single `promeki-bench` driver, built against the library, produce JSON output.
2. **MediaIO end-to-end profiling** — per-frame stamp trace through a `MediaIO` instance (or a multi-stage `MediaPipeline`), aggregated into min/max/avg/stddev per step by `BenchmarkReporter`. Answers "which stage ate the time on this frame?" Wired into the `MediaIO` base class so every backend gets it for free.
3. **Live telemetry** — always-on rolling counters on `MediaIO` populating the standard `MediaIOStats` keys (`BytesPerSecond`, `FramesDropped`, etc.) for TUI meters, `mediaplay --stats`, and health checks. Cheap; runs whether or not profiling is enabled.

No vendored benchmark framework. Everything is library-native, reusing existing primitives: `ElapsedTimer`, `Duration`, `TimeStamp`, `Benchmark`, `BenchmarkReporter`, `JsonObject`, `Histogram`, `CmdLineParser`, `StringRegistry`.

`utils/cscbench/` was renamed to `utils/promeki-bench/` and is now the single binary that drives every microbench suite across the library. The CSC suite is shipped; new suites land as peer `cases/*.cpp` files alongside it.

---

## Part A: Microbenchmark Runner — SHIPPED

Library-native runner lives in `include/promeki/benchmarkrunner.h` + `src/core/benchmarkrunner.cpp`. Independent from the `Benchmark`/`BenchmarkReporter` per-frame trace classes (different purpose, different data model).

**Shipped:**

- `class BenchmarkRunner` — filter regex, min-time / warmup window, repeat count, verbose toggle. Process-wide case registry via `registerCase()` / `registeredCases()` / `formatRegisteredCases()`. `runAll()` / `runCase()` / `runCaseByName()`. `filteredCaseCount()` and `estimatedDurationMs()` for UI banners. `toJson()` / `writeJson()` / `loadBaseline()`. `formatTable()` / `formatComparison()` with auto-scaled units.
- `class BenchmarkCase` — suite / name / description metadata plus a `std::function<void(BenchmarkState &)>` body. `PROMEKI_REGISTER_BENCHMARK` static-init macro using `PROMEKI_UNIQUE_ID`, mirroring `PROMEKI_REGISTER_MEDIAIO`.
- `class BenchmarkState` — range-for (`for(auto _ : state)`) and imperative (`while(state.keepRunning())`) iteration. `pauseTiming()` / `resumeTiming()`, `setItemsProcessed()` / `setBytesProcessed()` / `setLabel()` / `setCounter()`. Tracks both `wallNs()` (calibrator uses this) and `effectiveNs()` (reported per-iter statistic). Ranged iterator closes the timer the moment the loop ends, so any teardown in the case body is excluded automatically.
- `class BenchmarkResult` — `suite` / `name` / `label` / `description` / `iterations` / `repeats` / `avgNsPerIter` / `minNsPerIter` / `maxNsPerIter` / `stddevNsPerIter` / `itemsPerSecond` / `bytesPerSecond` / custom counter map / success+error. Full `toJson()` / `fromJson()` round-trip.
- `class StatsAccumulator` (`include/promeki/statsaccumulator.h`) — extracted from `BenchmarkReporter`'s Welford-style math. Count, sum, sumSq, min, max, sample variance / stddev. Merge support so per-thread accumulators can combine without locks.
- **Columnized human-readable output** — `formatTable()` / `formatComparison()` / `formatRegisteredCases()` all use a shared internal `Column` / `renderTable()` helper with auto-scaled time units (ns/us/ms/s), throughput units (B/KB/MB/GB per sec), and items-per-sec with k/M/G suffixes. ASCII-only units so `String::size()` width math stays correct.
- **Calibrator uses wall time, not effective time** — pauseTiming-heavy cases previously spun the calibrator into runaway iteration growth because `effectiveNs()` can be ~0 for cases that exclude their entire body. The calibrator now sizes iterations against `BenchmarkState::wallNs()` so calibration always converges in bounded time regardless of how the case uses pauseTiming.
- Tests: `tests/statsaccumulator.cpp` (math + merge + numerical-edge clamp), `tests/benchmarkrunner.cpp` (range-for and keepRunning iteration, pause/resume, items/bytes counters, broken-case graceful failure, filter regex, filtered count scaling, duration estimate scaling, JSON round-trip, baseline comparison with signed delta percentage, table format content checks, static-init macro registration).

---

## Part B: `promeki-bench` Utility — FRAMEWORK SHIPPED, CSC ONLY

The driver lives under `utils/promeki-bench/` and is the single binary that drives every microbench suite. Currently registered: **CSC suite only** (programmatically generated from `PixelFormat::registeredIDs()`). Every other suite from the original plan is still pending — each lands as a new `cases/<suite>.cpp` with a `registerCases()` hook called from `main.cpp::registerAllSuites()`.

**Shipped:**

- `utils/promeki-bench/main.cpp` — generic driver. CLI options: `-h/--help`, `-l/--list`, `-f/--filter <regex>`, `-o/--output <path>`, `-b/--baseline <path>`, `-t/--min-time <ms>`, `-W/--warmup <ms>`, `-r/--repeats <N>`, `-q/--quiet`, `-p/--param key[=value|+=value]`. `--help` prints `CmdLineParser::generateUsage()` plus each suite's contributed parameter-reference block plus an examples section. No suite-specific flags at the top level.
- `utils/promeki-bench/benchparams.{h,cpp}` — generic `BenchParams` bag (Map of String → StringList) with set / append / typed getters. `parseArg()` accepts `key=value`, `key+=value`, and bare `key` forms. Suite-specific knobs travel through `-p` using namespaced keys (`csc.width`, `csc.src`, `csc.config.CscPath`, …); suites read them from the singleton at case-body runtime.
- `utils/promeki-bench/cases/cases.h` — per-suite registration + param-help hook declarations. Each case file adds one `registerXxxCases()` + one `xxxParamHelp()` to this header and `main.cpp::registerAllSuites()` calls them after `BenchParams` is populated.
- `utils/promeki-bench/cases/csc.cpp` — CSC suite. **Programmatic pair generation**: walks `PixelFormat::registeredIDs()`, filters out compressed formats, validates each candidate against `CSCPipeline`. Three modes: default (anchored on canonical formats — `csc.anchors` defaults to `RGBA8_sRGB` + `YUV8_422_Rec709`, yielding ~540 pairs), `csc.full=1` (full 18,225-pair matrix for regression sweeps), or explicit cross product via `csc.src+=` / `csc.dst+=`. Every case reads `csc.width` / `csc.height` / `csc.config.<KEY>` on invocation so BenchParams changes between runs are honored. Custom counters: `mpix_per_iter`, `stages`, `identity`, `fast_path`.
- **Banner with case count + estimated duration + build-type warning** — shows build sha, resolved build type for both the library (`BuildInfo::type`) and the bench binary (`#ifdef NDEBUG` compile-time check), min_time / warmup / repeats, filter if set, cases-after-filter vs total, and a wall-time estimate via `Duration::fromMilliseconds(runner.estimatedDurationMs()).toString()`. Warns loudly if either half was built without optimization — prevents silently collecting debug-build numbers that are ~18× off from release.
- **Baseline comparison** — `-b path.json` loads a prior run, matches cases by `suite.name`, prints a `Current` / `Baseline` / `Delta` table with signed percentage deltas, marks missing baseline cases as `(new)`.
- **CMake wiring** — `PROMEKI_BUILD_BENCHMARKS` option (default ON) on the top-level `CMakeLists.txt`. `utils/promeki-bench/CMakeLists.txt` builds the target. `utils/CMakeLists.txt` gates `add_subdirectory(promeki-bench)` on the option. Per-case `#if PROMEKI_ENABLE_PROAV && PROMEKI_ENABLE_CSC` guard inside `cases/csc.cpp` with stub `register*Cases()` in the `#else` so the file always links.
- **Docs updated** — `README.md` Included Utilities table swapped `cscbench` for `promeki-bench`. `docs/utils.dox` likewise.

**Remaining case suites** (each is an independent incremental commit; `utils/promeki-bench/cases/<file>.cpp` + header declaration + CMake source list):

- [x] `imagedata.cpp` — `ImageDataEncoder` + `ImageDataDecoder` hot-path cases for RGBA8 / YUYV / planar 4:2:2 / v210 at 1920×1080 (cross-product via `imagedata.format+=` / `imagedata.size+=`), plus a full TPG → Inspector pipeline case (`inspector.cpp`). Shipped alongside `MediaIOTask_Inspector`.
- [ ] `network.cpp` — `writeDatagrams` batch vs loop (the long-standing [proav/optimization.md](../proav/optimization.md) wishlist item), `UdpSocketTransport::sendPackets()` end-to-end, `RtpSession::sendPackets()` through `LoopbackTransport`.
- [ ] `codec.cpp` — `ImageCodec::encode`/`decode` for JPEG and JPEG XS across a small image matrix, `AudioCodec` when it lands.
- [ ] `container.cpp` — `List<int>` push/iterate, `Map<String, int>` insert/lookup, `HashMap` same. Skip `std::` comparison cases — "prefer own classes" makes the comparison uninformative.
- [ ] `concurrency.cpp` — `Strand::submit` dispatch, `ThreadPool` submit latency, `Queue<T>` SPSC round-trip.
- [ ] `variantdatabase.cpp` — `set` / `get` / `declareID` overhead; sanity check, expected cheap.
- [ ] `histogram.cpp` — `Histogram::record` overhead, sanity check on log2 bucket math.

**Remaining (docs):**

- [ ] `docs/benchmarking.dox` — authoring guide: how to add a case, the registration macro, the `BenchmarkState` API, how to pick item/byte counters, how to expose suite parameters via `-p`.

---

## Part C: MediaIO Stamp Hooks + Identifiers — SHIPPED

Every `MediaIO` instance now has a stable identity triple and opt-in per-frame benchmark stamping wired into the base class, so every backend gets per-stage profiling for free when enabled.

**Shipped — identifiers:**

- `int MediaIO::localId() const` — process-wide monotonic integer assigned in the base constructor from a `static std::atomic<int>` counter. Not user-settable. Stable for the lifetime of the instance.
- `const String &MediaIO::name() const` — human-readable stage name. Seeded in the constructor as `"media<localId>"`, overridable at open time via `MediaConfig::Name`. The base class resolves the default in `open()` and writes the effective value back into the live config so `io->config().getAs<String>(MediaConfig::Name)` always returns a non-empty string.
- `const UUID &MediaIO::uuid() const` — globally-unique instance identifier, seeded in the base constructor via `UUID::generate()` so it is valid even before `open()`. Overridable at open time via `MediaConfig::Uuid`. Native variant type `Variant::TypeUUID`; the existing Variant layer handles `UUID ↔ String` conversion so callers can set it either way.

**Shipped — new `MediaConfig` keys:**

- `MediaConfig::Name` — `Variant::TypeString`, default empty.
- `MediaConfig::Uuid` — `Variant::TypeUUID`, default invalid.
- `MediaConfig::EnableBenchmark` — `Variant::TypeBool`, default `false`.

**Shipped — stamp hooks:**

- `MediaIO` base owns four `Benchmark::Id` fields (`_idStampEnqueue`, `_idStampDequeue`, `_idStampTaskBegin`, `_idStampTaskEnd`) initialized from the resolved `name()` at open time (e.g. `"media3.enqueue"` / `"media3.taskEnd"`).
- Four hook points in the write / read dispatch path: **writer** gets all four (enqueue on the user thread before strand submit, dequeue / taskBegin / taskEnd inside the strand lambda bracketing `executeCmd()`). **Reader** gets three (no enqueue — reads are pulled, not pushed; the strand lambda allocates a local `Benchmark::Ptr`, stamps dequeue / taskBegin, runs `executeCmd`, stamps taskEnd, then attaches the benchmark to the produced frame via `setBenchmark()`).
- Zero-cost when off: every stamp site is gated by `if(_benchmarkEnabled && frame.isValid())`. Verified by a test that runs 10 reads with the flag off and asserts `frame->benchmark().isValid() == false` on every returned frame.
- `setBenchmarkReporter(BenchmarkReporter *)` + `benchmarkReporter()` accessor — caller-owned, attached externally. Not copied or deleted by `MediaIO`.
- `setBenchmarkIsSink(bool)` + `benchmarkIsSink()` accessor — defaults to `true` so a standalone MediaIO "just works". `MediaPipeline` will flip this to `false` on non-terminal stages. Sink stages submit `frame->benchmark()` to the attached reporter after the `taskEnd` stamp fires; non-sink stages stamp but don't submit.
- `MediaIO::ParamBenchmarkReport` and `MediaIO::ParamBenchmarkReset` — base-class params commands handled by `sendParams()` before the strand submit (no queuing behind real work). `BenchmarkReport` writes the reporter's `summaryReport()` into the result under the `ParamBenchmarkReport` key; `BenchmarkReset` clears the reporter's accumulators. Both return `Error::NotSupported` when no reporter is attached.
- Tests in `tests/mediaio_identifiers.cpp` (14 cases / 126 assertions): monotonic `localId`, distinct UUIDs on default construction, `name`/`uuid` defaults before open, `name`/`uuid` defaults survive open, explicit config overrides survive open and are written back into the live config, reader stamps frames when enabled, reader does not stamp when disabled, non-sink reader stamps without submitting, `BenchmarkReport` params command returns the summary string, `BenchmarkReport` returns `NotSupported` without a reporter, `BenchmarkReset` clears accumulated statistics.

**Backend-specific stamps (still optional, pending):**

Backends with interesting internal phases can add their own stamps using backend-scoped IDs on top of the four base-class stamps. None of these are required for the core infrastructure to work; the base-class stamps already cover "which stage is slow" for 90% of the debugging use case.

- [ ] `CscMediaIO` (and other transform backends) — `ConvertBegin` / `CscDone` / `CodecDone` / `ConvertEnd` to isolate CSC cost from codec cost.
- [ ] `RtpMediaIO` writer — `PacketizeBegin` / `PacketizeEnd` / `SendBegin` / `SendEnd` to isolate packetization cost from socket send cost.
- [ ] `QuickTimeMediaIO` reader — `DemuxBegin` / `DemuxEnd` / `DecodeBegin` / `DecodeEnd` to isolate demux from codec decode.

---

## Part D: Live Telemetry via `MediaIOStats` — SHIPPED

Always-on lightweight counters populating the standard `MediaIOStats` keys. Cheap enough to run unconditionally (one atomic pair per frame), and the happy path is fully automatic — backends get `BytesPerSecond` / `FramesPerSecond` for free without any migration. Only exception-path events (drops / repeats / late) require backend cooperation.

**Shipped — `RateTracker` helper (`include/promeki/ratetracker.h` + `src/core/ratetracker.cpp`):**

- Sliding 5 s window with lazy rotation. `record(int64_t bytes)` is lock-free (two atomic `fetch_add`s). `bytesPerSecond()` / `framesPerSecond()` / `reset()` take a small mutex and rotate the window when it ages past nominal length; the previous window's snapshot is held as a fallback so the rate never dips to zero mid-rotation.
- Tests in `tests/ratetracker.cpp` cover default state, record/query, reset, window rotation, and a small concurrent-record smoke test.

**Shipped — MediaIO base wiring:**

- `MediaIO` owns a `RateTracker _rateTracker` and three `Atomic<int64_t>` counters (`_framesDroppedTotal` / `_framesRepeatedTotal` / `_framesLateTotal`). Reset in `resolveIdentifiersAndBenchmark()` on every `open()` so reopened stages see fresh numbers.
- A new static `MediaIO::frameByteSize(const Frame::Ptr &)` helper sums every image-plane buffer plus every audio buffer's `size()` to feed the rate tracker.
- The writer and reader strand lambdas in `MediaIO::writeFrame` / `MediaIO::submitReadCommand` call `_rateTracker.record(frameByteSize(frame))` on the success path. Every backend (TPG, ImageFile, QuickTime, AudioFile, RTP, Converter, SDLPlayer) gets the rate for free with zero migration.
- Added `MediaIOStats::FramesPerSecond` next to `BytesPerSecond` (new standard key).
- A new private `MediaIO::populateStandardStats()` post-processes the backend's stats bag in `MediaIO::stats()`: overlays `BytesPerSecond` / `FramesPerSecond` from the tracker, copies `FramesDropped` / `FramesRepeated` / `FramesLate` from the atomics, and — when `_benchmarkEnabled && _benchmarkReporter != nullptr` — derives `AverageLatencyMs` / `PeakLatencyMs` by summing the consecutive `enqueue→dequeue` / `dequeue→taskBegin` / `taskBegin→taskEnd` step stats (readers skip the enqueue hop). `BenchmarkReporter` only tracks consecutive entry pairs, so direct `stepStats(enqueue, taskEnd)` would come back empty.

**Shipped — `MediaIOTask` protected helpers:**

- New `MediaIOTask::_owner` back-pointer, set to the parent MediaIO at each `_task = task` adoption site (`create`, `createForFileRead`, `createForFileWrite`, `adoptTask`).
- Protected `noteFrameDropped()` / `noteFrameRepeated()` / `noteFrameLate()` forwarders on `MediaIOTask` that increment the base class's atomics via the owner pointer. Null-owner guard keeps standalone tasks safe. Happy-path counting stays automatic — backends only call these for exception-path events.
- `MediaIOTask_Rtp` migrated: `_framesDropped` / `_readerFramesDropped` fields and their `StatsFramesDropped` stats key removed; both transport-drop and reader-queue-overflow sites now call `noteFrameDropped()`.
- `MediaIOTask_Converter` migrated: `_bytesIn` / `_bytesOut` fields and the `StatsBytesIn` / `StatsBytesOut` stats keys removed (superseded by the base `BytesPerSecond`). `FramesConverted` / `QueueDepth` / `QueueCapacity` stay as genuine backend state.
- `SDLPlayerTask` migrated: the internal `_framesDropped` atomic counter and its public `framesDropped()` accessor were removed; both drop sites (decode failure and pending-image replacement) now go through `noteFrameDropped()` so the standard `MediaIOStats::FramesDropped` key surfaces the value for free.

**Shipped — `MediaIOStats::PendingOperations` key and `toString()`:**

- `MediaIOStats::PendingOperations` — new standard `int64_t` key populated by `populateStandardStats()` from `Strand::pendingCount()`.  Gives telemetry callers backlog visibility without every backend having to track it.
- `MediaIOStats::toString()` — compact single-line renderer for standard keys.  Fixed key order (byte-rate, fps, drop, rep, late, lat, q, pend, err); counters at zero (`FramesRepeated`, `FramesLate`) and empty `LastErrorMessage` are elided to keep normal-operation lines quiet; `FramesDropped` always shown.
- `MediaIO::submitAndWait()` gained an `urgent` bool; `MediaIO::stats()` passes `urgent=true` so telemetry pollers don't block behind deep prefetch queues.
- `mediaplay::printStats()` refactored to call `MediaIOStats::toString()`, deleting the local `formatByteRate()` helper.

**Shipped — `mediaplay --stats`:**

- New `--stats` flag on `mediaplay` (no-arg, defaults to a 1 s print interval) plus `--stats-interval <SEC>` to override.
- When enabled, main.cpp flips `EnableBenchmark:true` on every stage (source, optional converter, every sink including SDL and file sinks) before `open()` and attaches a caller-owned `BenchmarkReporter` to each. A main-loop timer polls every stage's `stats()` and prints one compact line per stage via `MediaIOStats::toString()`.
- Reporter lifetimes match the stages; cleanup runs alongside the existing per-stage teardown in both the normal shutdown path and `cleanupAndFail`.

**Tests shipped — `tests/mediaio_stats.cpp` (12 cases):**

- Default-state assertion that every standard key is present and zero on a freshly-opened TPG reader.
- Read-path rate tracking check: custom `TelemetryTestTask` backend adopted via `adoptTask()` produces synthetic frames; after a short delay `BytesPerSecond` / `FramesPerSecond` are asserted positive.
- `noteFrameDropped` / `noteFrameRepeated` / `noteFrameLate` exercises via injection counters on `TelemetryTestTask`.
- Counters reset to zero across close/reopen.
- Latency keys populate when `EnableBenchmark=true` and a reporter is attached.
- Latency keys stay at 0.0 when no reporter is attached.
- `PendingOperations` key populated by base class from `Strand::pendingCount()` (non-negative, present in stats).
- `toString()` empty-instance edge case returns empty `String`.
- `toString()` standard-keys comprehensive: byte-rate, fps, drop, rep (non-zero), late elision (zero), lat, q, pend.
- `toString()` `LastErrorMessage` surfaced when non-empty.
- `toString()` drop=0 always shown; rep/late/err elided when zero/empty.

---

## Part E: MediaIO End-to-End Benchmark Cases — PENDING

`MediaPipeline::build()` is now shipped (see [proav/pipeline.md](../proav/pipeline.md)); this is the next item to land.

Once Part D lands (optional) and `MediaPipeline::build()` can assemble a running graph from a config, add these cases to `promeki-bench`:

- [ ] `utils/promeki-bench/cases/mediaio.cpp` — standalone MediaIO backends (no pipeline)
  - TPG → `/dev/null`: raw generator throughput per resolution / pixel format
  - ImageFile reader: frame decode rate per codec
  - QuickTime reader: demux + decode rate per codec
  - Converter: CSC / codec passthrough (overlaps with `csc.cpp` but measures the MediaIO wrapping cost)
- [ ] `utils/promeki-bench/cases/pipeline.cpp` — end-to-end `MediaPipeline` cases
  - TPG → Converter → QuickTime writer: full encode pipeline throughput
  - QuickTime reader → Converter → null sink: full decode pipeline throughput
  - TPG → RTP writer → LoopbackTransport → RTP reader → null sink: networked round-trip

Each case opens the pipeline with `EnableBenchmark=true`, attaches a `BenchmarkReporter`, runs N frames, reads the report, and reports per-stage durations as custom counters on the `BenchmarkResult`. The pipeline flips `setBenchmarkIsSink(false)` on all non-terminal stages during `build()` so a single reporter sees every frame exactly once.

---

## JSON Schema

Live shape produced by `BenchmarkRunner::toJson()`. Pipeline per-stage breakdowns (the `stages` array) are still pending — they land with Part E. Baseline comparison tolerates missing fields so v2 baselines round-trip against future v3 output.

```json
{
  "version": 2,
  "date": "2026-04-10T12:34:56Z",
  "build": "libpromeki@<sha>",
  "config": { "min_time_ms": 500, "warmup_ms": 100, "repeats": 3 },
  "cases": [
    {
      "suite": "csc",
      "name": "RGBA8_sRGB_to_YUV8_422_Rec709",
      "label": "1920x1080 RGBA8_sRGB -> YUV8_422_Rec709",
      "iterations": 240,
      "repeats": 3,
      "ns_per_iter": { "avg": 4201234, "min": 4123456, "max": 4321098, "stddev": 45678 },
      "items_per_sec": 238.0,
      "bytes_per_sec": 1976000000.0,
      "custom": {
        "mpix_per_iter": 2.0736,
        "stages": 2,
        "identity": 0,
        "fast_path": 1
      },
      "succeeded": true
    }
  ]
}
```

**Pipeline-case shape (pending Part E):**

```json
{
  "suite": "pipeline",
  "name": "tpg_converter_quicktime_1080p_prores",
  "iterations": 300,
  "ns_per_iter": { "avg": 8456789, "min": 8123456, "max": 8901234, "stddev": 123456 },
  "items_per_sec": 118.2,
  "custom": { "fps": 118.2 },
  "stages": [
    { "name": "media0", "uuid": "3f2a...",  "format": "TPG",       "avg_ns": 1234567, "min_ns": 1123456, "max_ns": 1456789 },
    { "name": "media1", "uuid": "8b1c...",  "format": "Converter", "avg_ns": 3456789, "min_ns": 3234567, "max_ns": 3678901 },
    { "name": "media2", "uuid": "d47e...",  "format": "QuickTime", "avg_ns": 3765432, "min_ns": 3543210, "max_ns": 4012345 }
  ]
}
```

**Remaining:**

- [ ] Pipeline cases populate the `stages` array by walking the attached `BenchmarkReporter::allStepStats()` (Part E).
- [ ] `formatComparison()` flags regressions above a configurable threshold and exits non-zero — gated on CI work, see Implementation Order item 9.

---

## Implementation Order

1. **`BenchmarkRunner` core + tests** — COMPLETE
2. **`promeki-bench` binary + CSC case migration** — COMPLETE (CSC suite registered via programmatic `PixelFormat::registeredIDs()` walk rather than a hand-curated pair list)
3. **Initial non-MediaIO case suites** — `network.cpp` is the highest-leverage next item (covers the [proav/optimization.md](../proav/optimization.md) wishlist). Then `concurrency.cpp`, `container.cpp`, `histogram.cpp`, `variantdatabase.cpp`, `codec.cpp`. Incremental; each suite is its own small commit.
4. **`MediaConfig::EnableBenchmark` + base-class stamp hooks (Part C)** — COMPLETE
5. **MediaIO identifiers (`localId` + `Name` + `UUID`)** — COMPLETE; the thread-naming half of the original idea — backends spawning threads outside the shared pool naming them via `name()` — is tracked in [proav/backend-thread-naming.md](../proav/backend-thread-naming.md).
6. **Live telemetry (Part D)** — COMPLETE. `RateTracker` helper, automatic happy-path recording in the MediaIO strand lambdas, `MediaIOTask::noteFrameDropped/Repeated/Late` exception-path helpers, `MediaIO::populateStandardStats` post-processing in `stats()`, RTP/Converter/SDLPlayer migrated, `tests/mediaio_stats.cpp` shipped, `mediaplay --stats` wired with auto-enabled `EnableBenchmark` and per-stage reporters.
7. **MediaIO end-to-end cases (Part E)** — blocked on `MediaPipeline::build()`. Add `cases/mediaio.cpp` first (single-stage), then `cases/pipeline.cpp` once the pipeline is usable.
8. **Baseline comparison regression threshold** — the `formatComparison()` delta table is already shipped; the remaining piece is a configurable threshold + non-zero exit code on regression, gated on CI needs.
9. **CI regression pipeline** — future phase, out of scope for this plan. When it lands it's a thin wrapper: run `promeki-bench -o current.json -b baselines/main.json`, fail on regressions above threshold.

---

## Design Constraints

- **Library-native.** No vendored benchmark framework. `BenchmarkRunner` reuses `ElapsedTimer`, `Duration`, `TimeStamp`, `JsonObject`, `Map`, `List`, `StringRegistry`, `Mutex`. Math duplicated from `BenchmarkReporter` moved to a shared `StatsAccumulator`.
- **Zero cost when off.** `MediaIO` stamp hooks are a one-branch check per stamp site. `RateTracker` (when it lands) runs unconditionally but is trivial (one timestamp + two atomic increments per frame).
- **Single source of truth for stats.** Standard `MediaIOStats` keys are populated by the base class, not the backends. Backends only report backend-specific state (queue depth, RTP histograms, codec internals).
- **Consistent naming.** Microbenchmark IDs are `"suite.name"`, MediaIO stamp IDs are `"<stagename>.{enqueue,dequeue,taskBegin,taskEnd}"`. The reporter's step-pair keys naturally become `<stagename>.enqueue → <stagename>.taskEnd` etc.
- **Sink submits, intermediates stamp.** Per-frame reporter submission happens exactly once per frame, at the terminal stage. Multi-stage graphs produce one aggregated report with all stages' stamps in order.
- **JSON schema versioned from day one.** Field is `version: 2` so we can evolve without breaking tooling.
- **No std::chrono leakage** in the runner's public API (`Duration` / `TimeStamp` everywhere).
- **Dev tool first.** Nothing in this plan blocks or runs during normal build/test. `promeki-bench` is opt-in, manually invoked, JSON output lands wherever the user points it.
- **Calibrator uses wall time.** A subtle bug in the early runner prototype calibrated iterations against `effectiveNs()`, which a `pauseTiming`-heavy case can drive to near-zero — the calibrator would then grow `iter` without bound. Calibration has to use real wall time. The per-iteration result statistics still use effective time so cases that legitimately exclude setup from measurement get accurate numbers.

---

## Non-Goals

- No microbenchmark-in-test wedging (`TEST_CASE` + timing). Benchmarks are their own target.
- No comparison against `std::` containers beyond sanity spot checks. The "prefer own classes" preference means we care about absolute perf, not relative-to-std perf.
- No flamegraphs, no sampling profiler integration. `Histogram`, `BenchmarkReporter`, and per-stage ns breakdowns are the output. External profilers (perf, VTune, Instruments) are still the right tool when someone needs to chase into kernel-level detail.
- No perf database, no web dashboard. JSON on disk is the artifact format.
