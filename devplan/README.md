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
│   ├── utilities.md     String / Variant / RegEx enhancements
│   ├── ownership.md     Heap-ownership Phase C migration backlog
│   └── logger_ring_buffer.md  Crash-handler-readable retained log
├── network/             Network library work
│   ├── sockets.md       Phase 3A — complete; deferred items
│   └── avoverip.md      Phase 3C — PtpClock + RTP follow-ups
├── proav/               ProAV / MediaIO subsystem
│   ├── pipeline.md      MediaPipeline class follow-ups
│   ├── planner.md       MediaPipelinePlanner future work
│   ├── backends.md      Per-backend remaining work
│   ├── capabilities_audit.md  describe / proposeInput / proposeOutput audit
│   ├── optimization.md  Network transmit (sendmmsg, kernel pacing, TXTIME)
│   ├── timestamps.md    MediaTimeStamp / ClockDomain follow-ups
│   ├── framesync.md     COMPLETE; stub retained
│   ├── dsp.md           Deferred audio DSP backends
│   ├── nvenc.md         NVENC / NVDEC follow-up work
│   └── quicktime.md     QuickTime writer drain-at-close (deferred)
├── music/               Phase 6
│   ├── theory.md        Phase 6A/6B — unstarted
│   └── midi.md          Phase 6C/6D — unstarted
├── tui/                 Phase 5
│   └── widgets.md       TUI widgets to build
├── infra/               Cross-cutting infrastructure
│   ├── benchmarking.md  BenchmarkRunner / promeki-bench remaining suites
│   ├── audit.md         2026-04-25 audit findings register (90 open)
│   └── valgrind.md      COMPLETE; stub retained
└── demos/
    └── promeki-pipeline.md  Vue 3 / Vue Flow demo (all phases shipped)
```

## Current focus

1. **MediaPipeline polish** — `docs/mediapipeline.dox` authoring
   guide, `docs/mediaplay.dox` grammar reference, and golden-data
   integration tests under `tests/func/mediaplay/`. See
   [proav/pipeline.md](proav/pipeline.md) and
   [proav/backends.md](proav/backends.md).
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

## Phase status (overview)

| Phase | Topic                                | Status                              |
| :---: | ------------------------------------ | ----------------------------------- |
| 1     | Core containers / concurrency        | COMPLETE                            |
| 2     | IO / filesystem / streams            | COMPLETE (carry-ins in `core/`)     |
| 3A    | Sockets                              | COMPLETE                            |
| 3B    | HTTP / WebSocket / TLS               | COMPLETE                            |
| 3C    | AV-over-IP (RTP / SDP / multicast)   | mostly complete; PtpClock pending   |
| 4     | ProAV — MediaIO framework + backends | framework + 18 backends shipped;<br>follow-ups in `proav/` |
| 4A    | MediaPipeline                        | shipped; docs + tests pending       |
| 4M    | MediaPipelinePlanner                 | shipped (single-hop + 2-hop codec)  |
| 5     | TUI widgets                          | unstarted                           |
| 6     | Music library                        | unstarted                           |
| 7     | Cross-cutting (Result, Variant, …)   | ongoing                             |

## Dependency graph (high level)

```
Phase 1 ──┬─► Phase 2 ──┬─► Phase 3A ─► Phase 3B
          │             │              └► Phase 3C ── PtpClock (open)
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
