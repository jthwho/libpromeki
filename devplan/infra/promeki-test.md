# promeki-test — Functional Test Runner

**Phase:** cross-cutting (development tool; CI integration future)
**Library:** n/a — standalone utility under `utils/promeki-test/`

`promeki-test` is the functional end-to-end test runner for libpromeki.
It replaces the legacy `tests/func/roundtrip/` single-binary approach
with a self-registering, regex-filterable case matrix, per-test scratch
folders, and JSON result artifacts — the same pattern `promeki-bench`
uses for microbenchmarks.

**Shipped (2026-05-05):**

- `TestParams` — `VariantDatabase<"FunctionalTest">` carrying per-test
  knobs (BaseFolder, TestFolder, LogFile, Verbose, Frames,
  PhaseTimeoutMs) plus per-suite extension keys declared via
  `PROMEKI_DECLARE_ID`.
- `TestContext` — per-invocation state object with first-call-wins
  result setters (`setPass` / `setFail` / `setSkip` / `setTimeout`),
  `setDetail` for numeric/string artifacts, and `setPipelineConfig`
  for the post-autoplan `MediaPipelineConfig` JSON snapshot.
- `TestRunner` / `TestCase` / `PROMEKI_REGISTER_FTEST` — process-wide
  registry, matching `BenchmarkRunner`'s static-init-safe design.
- `pipeline_common.{h,cpp}` — shared stage builders (`makeTpgStage`,
  `makeEncoderStage`, `makeDecoderStage`, `makeInspectorStage`) and
  single/dual pipeline drivers (`runPhase` / `runDualPhase`) with
  `PhaseOutcome` / `DualPhaseOutcome` / `DualPhaseSequence`.
- **Registered suites:**
  - `roundtrip.*` — file-based roundtrip (ImgSeq, QuickTime, PMDF);
    one case per (backend, extension, codec, pixel-format) from
    registry introspection. Replaces `tests/func/roundtrip/`.
  - `codec.*` — in-memory encode→decode with no file in between; one
    case per (codec, backend) pair.
  - `audio.*` — AudioFile roundtrip (WAV, BWF, AIFF, OGG).
  - `rtp.*` — RTP loopback (MJPEG, RFC 4175 raw, L16 audio).
  - `framebridge.*` — single-process FrameBridge probe (currently FAIL
    — surfaces library bug where `acceptPending` only runs inside
    `writeFrame`; see `fixme/` if a tracking entry exists).
- **CLI:** `-k/--regex`, `-b/--base`, `-n/--frames`, `-t/--timeout-ms`,
  `-v/--verbose`, `--log-console`, `-p/--param key=val`.
- **Artifacts per test:** `<TestFolder>/test.log`,
  `<TestFolder>/result.json` (status, message, durationMs, details,
  pipeline JSON), `<BaseFolder>/summary.json` (full matrix +
  pass/fail/skip/timeout totals).
- **CMake:** `PROMEKI_ENABLE_PROAV` gate in `utils/CMakeLists.txt`.
- **Library bug fixed along the way:** `MediaIO::createForFileRead` and
  `createForFileWrite` now always stamp `MediaConfig::OpenMode` in the
  file-extension dispatch path — the omission had silently turned every
  `MediaPipeline`-driven file write into a no-op.

## Remaining work

- [ ] **FrameBridge `acceptPending` bug** — the bridge's pending-accept
  loop only runs inside `writeFrame`, so the planner's brief-open probe
  (which reads the MediaDesc without writing a frame) races against the
  TX side and the RX open times out.  Fix the library, then re-enable
  the `framebridge.*` cases.
- [ ] **CI integration** — run `promeki-test -k '^roundtrip\.' -k
  '^codec\.' -k '^audio\.'` as part of the CI gate (the `check` target
  or a new `functest` target).  RTP and FrameBridge cases may need
  special consideration (network availability, the open bug above).
- [ ] **PMDF backend roundtrip cases** — PMDF is wired but may need
  `createForFileRead` / `createForFileWrite` verified in the runner.
- [ ] **NDI roundtrip case** — blocked on NDI SDK availability in the
  build environment.
- [ ] **`docs/promeki-test.dox`** — authoring guide: how to add a
  suite, the registration macro, TestContext API, per-suite parameter
  keys.
