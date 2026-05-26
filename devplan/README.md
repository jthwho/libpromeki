# libpromeki Development Plan

This directory tracks pending work on the consolidated `promeki`
library and its companions (`promeki-tui`, `promeki-sdl`). Completed
work is **not** kept here — code and git history are the source of
truth. Each plan document is meant to read as "what's still open"
plus enough context for the open items to be intelligible.

## Layout

```
devplan/
├── README.md            (this file — index + current focus + dep graph)
├── fixme/               Tracked FIXME comments (one file per item)
├── core/                Core library work
│   ├── io.md            IODevice, File, FilePath, Dir (Phase 2 carry-ins)
│   ├── streams.md       TextStream type ops, ObjectBase saveState/loadState
│   ├── datastream-consolidation.md  DataStream / DataType / PROMEKI_DATATYPE refactor
│   ├── utilities.md     String / Variant / RegEx enhancements
│   ├── ownership.md     Heap-ownership Phase C migration backlog
│   ├── logger_ring_buffer.md  Crash-handler-readable retained log
│   └── systemcow-mediaio-allocator.md  MemfdRegion + SystemCow Buffer + MediaIOAllocator
├── network/             Network library work
│   ├── sockets.md       Phase 3A — complete; deferred items
│   ├── avoverip.md      Phase 3C — PtpClock + RTP follow-ups
│   ├── 2110.md          SMPTE ST 2110 conformance plan (-10/-20/-21/-30/-31/-40)
│   ├── srt.md           Phase 3D — SRT shipped; MediaIO backend + bonded listener deferred
│   ├── rtmp.md          Phase 3F/5 — RTMP / RTMPS publisher + subscriber (Phases 0-5 shipped; Phase 6 docs/CMake next)
│   └── tls.md           mbedTLS audit follow-ups — OCSP, 4.x upgrade triggers, deferred items
├── proav/               ProAV / MediaIO subsystem
│   ├── pipeline.md      MediaPipeline class follow-ups
│   ├── planner.md       MediaPipelinePlanner future work
│   ├── backends.md      Per-backend remaining work
│   ├── capabilities_audit.md  describe / proposeInput / proposeOutput audit
│   ├── optimization.md  Network transmit (sendmmsg, kernel pacing, TXTIME)
│   ├── timestamps.md    MediaTimeStamp / ClockDomain follow-ups
│   ├── framesync.md     COMPLETE; stub retained
│   ├── inspector-pcm-marker-decoder.md  COMPLETE; stub retained
│   ├── transcription.md TranscriptionEngine / WhisperCpp follow-ups (streaming, CUDA, diarization)
│   ├── dsp.md           Deferred audio DSP backends
│   ├── nvenc.md         NVENC / NVDEC follow-up work
│   ├── video-signal-carriers.md  VideoPortRef / SdiSignalConfig / HdmiSignalConfig / VideoReferenceConfig
│   ├── ntv2.md          AJA NTV2 SDI / HDMI MediaIO backend (build scaffolding shipped)
│   └── quicktime.md     QuickTime writer drain-at-close (deferred)
├── music/               Phase 6
│   ├── theory.md        Phase 6A/6B — unstarted
│   └── midi.md          Phase 6C/6D — unstarted
├── tui/                 Phase 5
│   └── widgets.md       TUI widgets to build
├── infra/               Cross-cutting infrastructure
│   ├── benchmarking.md  BenchmarkRunner / promeki-bench remaining suites
│   ├── promeki-test.md  Functional test runner (shipped 2026-05-05)
│   ├── audit.md         2026-04-25 audit findings register (90 open)
│   ├── qemu-cross-testing.md  qemu-user wiring for cross-build CI/CD (unstarted)
│   └── valgrind.md      COMPLETE; stub retained
└── demos/
    └── promeki-pipeline.md  Vue 3 / Vue Flow demo (all phases shipped)
```

## Current focus

1. **MediaPipeline polish** — `docs/mediapipeline.dox` authoring
   guide, `docs/mediaplay.dox` grammar reference. The functional test
   runner (`utils/promeki-test/`) is shipped and covers roundtrip,
   codec, audio, RTP, and FrameBridge suites; CI integration and the
   FrameBridge `acceptPending` bug remain open. See
   [proav/pipeline.md](proav/pipeline.md),
   [proav/backends.md](proav/backends.md), and
   [infra/promeki-test.md](infra/promeki-test.md).
2. **RTP follow-ups** — mid-stream descriptor discovery, RTP
   timestamp wrap, ST 2110-20 10/12-bit pgroup, L24, ST 2110-40,
   per-packet `SCM_TXTIME` deadlines via `RtpPacingMode::TxTime`.
   See [proav/backends.md](proav/backends.md) and
   [proav/optimization.md](proav/optimization.md).
3. **JPEG XS container + RTP** — QuickTime / ISO-BMFF `jxsm` sample
   entry (blocked on procuring ISO/IEC 21122-3:2024) and RFC 9134
   slice mode K=1. See [`fixme/`](fixme/).
4. **Audit remediation R2–R6** — 90 open findings from the
   2026-04-25 audit. R1 is complete. See
   [infra/audit.md](infra/audit.md).
5. **Codec abstraction follow-ups** — generic `configKeys()`
   discovery, drain hook for stateful temporal codecs, GPU-resident
   bridges. See [proav/backends.md](proav/backends.md) and
   [proav/planner.md](proav/planner.md).
6. **ARM / cross-build robustness** — `PROMEKI_CONFIG_FILE` preset
   system, `cmake/configs/cross-aarch64-linux.cmake` + toolchain, and
   per-feature `#if PROMEKI_ENABLE_*` header guards shipped
   (2026-05-15). Next: qemu-user CI lane (`infra/qemu-cross-testing.md`).
8. **AJA NTV2 build scaffolding** — `thirdparty/libajantv2` submodule
   (ntv2_18_0_0), `PROMEKI_ENABLE_NTV2` CMake option, wired into
   `promeki` as PRIVATE link target (2026-05-16). MediaIO backend
   (`NTV2MediaIO`) lands in a follow-up changeset. See
   [proav/backends.md](proav/backends.md) and
   [proav/ancdata.md](proav/ancdata.md) Phase 5.
7. **DataStream / DataType consolidation** — Phases 1-3 complete
   (2026-05-16): `PROMEKI_DATATYPE` macro, `DataTypeID` enum, 47 types
   migrated, `enum DataStream::Type` and `Variant::TypeXxx` aliases
   removed. Phase 4 cleanup (SFINAE traits, `docs/dataobjects.dox`,
   `CODING_STANDARDS.md`) remains open.
   See [`core/datastream-consolidation.md`](core/datastream-consolidation.md).
9. **Validity sentinels on `TimeStamp` / `Duration` / `DateTime`**
   — SHIPPED 2026-05-17. `INT64_MIN` as `Invalid`; default-construct
   = invalid; `Duration::zero()` for explicit zero; arithmetic
   propagates invalid; `MediaTimeStamp::isValid()` requires both
   domain and inner `TimeStamp` to be valid; `MediaTimeStamp::nanoseconds()`
   convenience accessor added.  All consuming sites updated
   (`PacingGate`, `EventLoop`, `RtpSession`, all MediaIO backends).
   See [`proav/timestamps.md`](proav/timestamps.md).
10. **`AudioBuffer` MediaTimeStamp flow + `PcmAudioPayload` push/pop**
   — SHIPPED 2026-05-18. Anchor queue threads PTS through the ring
   FIFO; filter-delay correction back-adjusts resampled anchors;
   `resamplerSampleDelta()` exposes drift accounting;
   `push(PcmAudioPayload)` / `popPayload` / `popWaitPayload` /
   `nextSamplePts()` added; RTP audio packetizer migrated.
   See [`proav/timestamps.md`](proav/timestamps.md).
11. **`std::atomic` → `promeki::Atomic` migration + concurrency additions**
   — SHIPPED 2026-05-18. All `std::atomic<T>` / `std::memory_order_*`
   uses in our code migrated to `Atomic<T>` / `MemoryOrder`.  New
   additions to `atomic.h`: `MemoryOrder` enum + `toStdMemoryOrder()`,
   `atomicThreadFence()`, explicit-order overloads on all `Atomic<T>`
   methods, `compareExchangeWeak`, `AtomicRef<T>` (wraps
   `std::atomic_ref<T>`), `AtomicFlag` (wraps `std::atomic_flag`).
   New header `once.h`: `OnceFlag` + `callOnce` wrapping
   `std::once_flag` / `std::call_once`.  New in `uniqueptr.h`:
   `UniquePtr<T[]>` array specialization + `uniquePointerCast`.
   Tests: `once.cpp` (new), `atomic.cpp` extended, `uniqueptr.cpp`
   extended.  Audit finding #19 partial: `compareExchangeWeak` added;
   `requires` constraint on arithmetic ops remains open.
12. **Submodule auto-init system**
   — SHIPPED 2026-05-18. `cmake/PromekiSubmodules.cmake` maps each
   `thirdparty/` submodule to the CMake feature flag(s) that require
   it and runs `git submodule update --init --recursive` on first
   configure.  Mirror URL rewriting via `PROMEKI_MIRRORS_FILE` or
   well-known per-user / system config paths (shared search order with
   the companion script).  `scripts/mirror-thirdparty.py` handles
   GitLab project auto-create + `git push --mirror` for self-hosted
   mirrors; reads the same CMake-syntax config file.
   `cmake/mirrors.example.cmake` documents the config format.
   Replaces deleted `scripts/mirrors.conf` + `scripts/setup-mirrors.sh`.
14. **Speech-to-text (TranscriptionEngine + WhisperCpp Phase 1)** —
   SHIPPED 2026-05-25. `TranscriptionEngine` abstract base + backend
   registry; `Transcript` / `TranscriptWord` / `TranscriptList` value
   types; `SubtitleCueBuilder` cue-shaping layer; `MediaConfig`
   Transcription* + SubtitleCue* keys; `TranscriptionMode` /
   `TranscriptionChannelMode` enums; `Metadata::Transcript` key;
   vendored `whisper.cpp` v1.8.4 `WhisperCpp` backend (CPU, batch
   only); `Dir::models()` + `LibraryOptions::ModelsDir` convention;
   `promeki-fetch-model` CLI (SHA-256-verified Hugging Face downloader);
   `docs/whisper.md`.  Streaming mode, CUDA backend, diarization, and
   HTTP-streaming downloads are deferred.
   See [proav/transcription.md](proav/transcription.md).
13. **`BasicThread` + `Thread` refactor** — SHIPPED 2026-05-18.
   `BasicThread` (Pimpl, move-only, no `ObjectBase`) owns OS thread,
   scheduling, affinity, naming, and static helpers (`sleepMs/Us/Ns`,
   `sleep(Duration)`, `yield`, `idealThreadCount`, `currentNativeId`,
   `setCurrentThreadName`, `priorityMin/Max`). `Thread` refactored to
   wrap a `BasicThread` member; `Thread::start()` now returns `Error`
   and bails cleanly on OS failure (no deadlock). All `std::thread`
   consumers converted: `ThreadPool` workers, `Logger` worker,
   `SignalHandler` watcher, `NdiDiscovery`, `NdiMediaIO` capture,
   `V4l2MediaIO` video/audio, `SdlPlayer` pull, `RtpChaosShim`
   endpoints — each with a unique OS-level name and a `nextInstanceId<Tag>()`-backed
   `instanceID()` accessor. `ObjectBase` now explicitly deletes
   copy/move; 9 derived classes shed redundant explicit deletes.
   `BasicThread::detach()` removed as unsafe under Pimpl.

## Phase status (overview)

| Phase | Topic                                | Status                              |
| :---: | ------------------------------------ | ----------------------------------- |
| 1     | Core containers / concurrency        | COMPLETE                            |
| 2     | IO / filesystem / streams            | COMPLETE (carry-ins in `core/`)     |
| 3A    | Sockets                              | COMPLETE                            |
| 3B    | HTTP / WebSocket / TLS               | COMPLETE                            |
| 3C    | AV-over-IP (RTP / SDP / multicast)   | mostly complete; PtpClock pending   |
| 3D    | SRT (Secure Reliable Transport)      | shipped; SrtMediaIO backend deferred |
| 4     | ProAV — MediaIO framework + backends | framework + 18 backends shipped;<br>NTV2 scaffolding landed; backend pending; follow-ups in `proav/` |
| 4A    | MediaPipeline                        | shipped; docs + tests pending       |
| 4M    | MediaPipelinePlanner                 | shipped (single-hop + 2-hop codec)  |
| 5     | TUI widgets                          | unstarted                           |
| 6     | Music library                        | unstarted                           |
| 7     | Cross-cutting (Result, Variant, …)   | ongoing                             |

## Dependency graph (high level)

```
Phase 1 ──┬─► Phase 2 ──┬─► Phase 3A ─► Phase 3B
          │             │              ├► Phase 3C ── PtpClock (open)
          │             │              └► Phase 3D (SRT) ── SrtMediaIO backend (open)
          │             │
          │             ├─► Phase 4 (MediaIO + backends + MediaPipeline)
          │             │       │
          │             │       ├─► proav/backends.md follow-ups
          │             │       ├─► proav/optimization.md (TXTIME, DPDK)
          │             │       └─► proav/quicktime.md (drain-at-close)
          │             │
          │             └─► Phase 7 (ObjectBase saveState/loadState)
          │
          ├─► Phase 5 (TUI widgets)         [independent]
          └─► Phase 6 (Music library)       [independent]

Phase 7 (cross-cutting) — ongoing
```

## Standards and testing

All work follows `CODING_STANDARDS.md` at the repo root. Every new
class requires complete doctest unit tests; every modification to an
existing class updates its tests. Test framework is doctest, vendored
under `thirdparty/doctest/`. Three test executables —
`unittest-promeki`, `unittest-tui`, `unittest-sdl` — built and run by
`build check`. The `scripts/precommit.sh` gate runs configure →
format-check → `-Werror` build → `check` → doxygen.

## Conventions

- Plan docs are pruned as work lands. The shape we want is: brief
  "what shipped" statement, then a checklist or bullet list of open
  items. Anything explanatory enough to call "design notes" should
  live in `docs/*.dox`, not here.
- Class names mentioned in plan docs reflect the **current**
  codebase. The MediaIO ports/strategies/factory refactor renamed
  `MediaIOTask_X` → `XMediaIO`; older docs caught in transitional
  states should be updated when revisited.
- When closing a plan doc, leave a one-paragraph stub pointing at
  `docs/` (or git history) instead of deleting it, so readers
  searching for the topic land on the canonical reference.
