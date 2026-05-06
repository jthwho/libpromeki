# MediaPipeline (MediaIO-based)

**Phase:** 4A
**Library:** `promeki`
**Standards:** All work follows `CODING_STANDARDS.md`. Every class
requires complete doctest coverage. See `devplan/README.md` for the
full requirements.

`MediaPipeline` is the data-driven pipeline builder that instantiates
and wires `MediaIO` instances from a declarative `MediaPipelineConfig`.
The class, the config object, JSON / DataStream round-trip, fan-out
dispatch, signal plumbing, lifecycle (`build` / `open` / `start` /
`stop` / `close`), the cascading async close path, planner integration
(see [planner.md](planner.md)), and `mediaplay --pipeline` /
`--save-pipeline` are all shipped. The `MemSpace::Stats` telemetry
is shipped and wired through `mediaplay --memstats`.

The full design notes (signal plumbing details, JSON schema example,
copy-on-write safety, the original "follow-on" sections about codec
config forwarding) live in git history and in `docs/mediaio.dox`.

## Remaining work

- [ ] **`docs/mediapipeline.dox`** — authoring guide, JSON schema,
  worked examples (multi-stage encode pipeline, fan-out, autoplan
  bridge transparency).
- [ ] **Fan-in / multi-input transforms** — current converter stages
  must have exactly one incoming and one outgoing route; dispatch
  policy (mix / interleave / select) on a multi-input consumer is
  deferred until a real use case lands.
- [ ] **`MemSpacePool`** — recycling `MemSpace` for fixed-size
  buffers, optional pre-allocation, LIFO recycle stack, pool hit/miss
  metrics. Optional follow-on; `MemSpace::Stats` already tells us
  whether this is worth building.
- [ ] **`mediaplay --plan --json <PATH>`** — write the resolved
  config out as JSON for editing then replay (see also
  [proav/backends.md](backends.md) → mediaplay section).
- [ ] **Variant JSON round-trip coverage** — verify every type a
  `MediaConfig` can carry (`PixelFormat`, `MediaDesc`, `FrameRate`,
  `Color`, `SocketAddress`, `UUID`, `UMID`, …) round-trips through
  `Variant → JSON → Variant`. See
  [core/utilities.md](../core/utilities.md) → Variant Enhancements.

## MediaIOPortConnection fixes (2026-05-05)

- **`Cancelled` → `upstreamDone`**: `Error::Cancelled` returned by a
  blocking-read backend's `cancelBlockingWork()` hook now routes to
  `upstreamDoneSignal` instead of `errorOccurredSignal`.  Prevents
  cascade stall when a DedicatedThreadMediaIO (RTP, FrameBridge) is
  closed mid-blocking-read.

- **Per-sink cap fires on limit-th write**: `sinkLimitReachedSignal`
  now fires as soon as `framesWritten >= frameLimit` after accepting a
  write, not on the (limit+1)-th submission.  Fixes pipelines where the
  source never produces a next frame to trigger the old post-write
  guard (e.g. a bounded RTP receiver whose last frame lands exactly on
  the limit).

## Functional test runner — SHIPPED

`utils/promeki-test/` (shipped 2026-05-05) replaces the legacy
`tests/func/roundtrip/` single-binary approach with a
self-registering, regex-filterable matrix.  Suites: roundtrip (file
I/O), codec (in-memory encode→decode), audio (AudioFile), RTP
(loopback), FrameBridge (single-process).  Per-test scratch folder,
`result.json` (with post-autoplan pipeline JSON), and run-level
`summary.json`.  See [infra/promeki-test.md](../infra/promeki-test.md)
for open items (CI wiring, NDI suite).
