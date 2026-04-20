# MediaIOTask Capabilities Compliance Audit

**Phase:** 4M follow-up
**Status:** Audit complete 2026-04-19; priorities 1–2 remediated (Phase 4n). Remaining gaps tracked below.
**Library:** `promeki`

Audit of every `MediaIOTask` subclass against the Phase 4M capabilities
contract (`describe()`, `proposeInput()`, `proposeOutput()` introduced in
`7210927`). Goal: every backend answers introspection + negotiation
queries correctly so `MediaPipelinePlanner` and the `mediaplay --describe`
path don't have to fall back to open/close probes or silently misroute
frames.

## Coverage matrix

| Task | Role | describe | proposeInput | proposeOutput | Status |
|------|------|----------|--------------|---------------|--------|
| tpg | source | ✅ | — (source only) | ✅ | OK |
| csc | transform | ✅ | ✅ | ✅ | OK |
| src | transform | ✅ (trivial) | ✅ | ✅ | OK |
| framesync | transform | ✅ (trivial) | ✅ | ✅ | OK |
| videodecoder | transform | ✅ | ✅ | ✅ | OK |
| videoencoder | transform | ✅ | ✅ | ✅ | OK |
| audiofile | source+sink | ❌ | ✅ | — | gap |
| imagefile | source+sink | ❌ | ✅ | — | gap |
| quicktime | source+sink | ❌ | ✅ | — | gap |
| burn | transform | ❌ | ✅ | ✅ | OK (Phase 4n) |
| rawbitstream | sink | ✅ | ✅ | — | OK (Phase 4n) |
| inspector | sink | ❌ | ❌ | — | acceptable default |
| framebridge | source+sink | ❌ | ❌ | — | acceptable default |
| v4l2 | source | ❌ | — | ❌ | deferred (TODO) |
| rtp | source+sink | ❌ | ❌ | — | deferred (TODO) |

## Findings

### 1. `mediaiotask_rawbitstream.cpp` — real bug *(priority 1)*

RawBitstream is a sink that only accepts compressed `Image` objects with
an attached `MediaPacket`. The default `proposeInput` says "accept
anything," so the planner routes an uncompressed source straight in and
the first `executeCmd(Write)` fails at runtime. `proposeInput` must reject
any offered `MediaDesc` whose first image is not compressed. `describe`
can advertise `acceptableFormats` as "every compressed PixelDesc" (from
the registered `VideoCodec` list) so the planner sees the real contract.

### 2. `mediaiotask_burn.cpp` — silent passthrough assumption *(priority 1)*

Burn requires `PixelDesc::hasPaintEngine() == true` but never advertises
that constraint. Today non-paintable input triggers a one-shot warning
and silent passthrough.

- `proposeInput`: reject / substitute when `hasPaintEngine()` is false.
- `proposeOutput`: return input unchanged (pure passthrough transform).
- `describe`: empty producible/acceptable lists are fine (transform is
  fully determined by input shape); one-line comment documenting the
  intentional passthrough is enough.

### 3. `mediaiotask_audiofile.cpp` — missing `describe` *(priority 2)*

Has a solid `preferredWriterDataType()` per-extension picker for
`proposeInput`, but never exposes the same info via
`describe().acceptableFormats()`. The planner's inspect/introspect path
can't see the sink's writable set without opening the file. Mirror the
extension-based logic into `describe()` when `canBeSink`.

### 4. `mediaiotask_imagefile.cpp` — missing `describe` *(priority 2)*

`preferredWriterPixelDesc` covers DPX/PNG/JPEG/TGA/SGI/PNM per-extension
bit-depth picks. `describe().acceptableFormats()` is not populated, so
introspection is blind for sink mode. Lift the per-extension table into
`describe()`.

### 5. `mediaiotask_quicktime.cpp` — missing `describe` *(priority 2)*

`isSupportedPixelDesc()` already enumerates the writer-supported set.
One `describe()` that pushes each into `acceptableFormats` is enough.

### 6. `mediaiotask_v4l2.cpp` — header explicitly defers; improvement possible

`v4l2QueryDevice()` already enumerates supported modes for the registered
`FormatDesc::queryDevice` callback. That same enumeration could populate
`producibleFormats` from `describe()` without the ALSA+V4L2 open cycle
every plan pass currently pays. Deferred but cheap to land.

### 7. `mediaiotask_rtp.cpp` — deferred per explicit comment

Each RFC payload type has its own shape rules (JPEG payload = 8-bit YUV422
only; raw payload = configurable subsampling/bit depth; L16 = 16-bit BE
PCM at fixed channel counts). Leaving `describe()` / `proposeInput()` out
until per-payload constraints are encoded is defensible.

### 8. `mediaiotask_framebridge.cpp` and `mediaiotask_inspector.cpp`

Default behaviour (accept anything, pass through) is correct for a
format-agnostic bridge and for a terminal sink. No change needed beyond a
one-line comment acknowledging the deliberate default.

### 9. `describe` triviality in `src` / `framesync`

Both return `Error::Ok` with no fields populated. OK for pure transforms
whose output shape is derived from `applyOutputOverrides(input, cfg)` at
plan time, but the planner does not learn "accepts any uncompressed
audio" / "accepts anything" — it has to probe via `proposeInput`.
Acceptable but slightly asymmetric vs CSC/VideoEncoder/VideoDecoder which
advertise concrete producible/acceptable lists. Not blocking.

## Remediation order

1. ~~`rawbitstream` — add `proposeInput` rejecting uncompressed + `describe` advertising compressed accept set.~~ **(done, Phase 4n)**
2. ~~`burn` — add `proposeInput` (paintable only) + `proposeOutput` (passthrough).~~ **(done, Phase 4n)**
3. `audiofile` / `imagefile` / `quicktime` — mirror existing picker logic into `describe().acceptableFormats()`.
4. `v4l2` — call `v4l2QueryDevice()` from `describe()` to cache producible formats.
5. Opportunistic cleanup on `src` / `framesync` / `inspector` / `framebridge` — explanatory comments only.

`rtp` stays on the separately-tracked follow-up until per-payload shape
rules are codified.
