# MediaPipeline Planner — Auto Stage Insertion

**Phase:** 4M
**Library:** `promeki`
**Status:** Phases 1–5 shipped (2026-04-18). The planner is wired
through `MediaPipeline::build` and is the default in `mediaplay`.

The shipped architecture (`MediaIODescription` introspection, the
`proposeInput` / `proposeOutput` negotiation API, `FormatDesc::bridge`
registration, the single-hop solver with codec-transitive two-hop
fallback, the four-strategy source-desc discovery chain, the
`InjectedStages` overload, and the planner's diagnostic format) is
documented end-to-end in `docs/mediaplanner.dox`. That doc — plus the
`docs/mediaio.dox` and `docs/mediapipeline.dox` companions — is the
canonical reference; this document tracks only follow-up work.

The original Phase 1–5 narrative, the alongside-renames sweep
(`Output → Source` / `Input → Sink` / `InputAndOutput → Transform`,
the `setMediaDesc → setExpectedDesc` migration, the deletion of
`MediaIOTask_Converter`), and the bridge / `proposeInput` /
`proposeOutput` coverage matrix all live in git history.

---

## Bug fixes (landed 2026-06-01)

### Unpinned `supportedInputsFor` returned the wrong format set

`VideoEncoder::supportedInputsFor(codecId, invalid_backend)` and the
`AudioEncoder` mirror were returning the union of every registered backend's
accepted formats.  `VideoEncoder::create` (and `AudioEncoder::create`) pick
the *highest-weight* backend, so the planner's proposeInput short-circuit
("no CSC needed") could take a format that only a lower-weight sibling
accepts and hand it to the chosen backend, which then rejected it with
`PixelFormatNotSupported` at encode time.  Fixed: unpinned now returns only
the highest-weight backend's inputs (the record list is kept
highest-weight-first by `registerBackend`; returning `list.front().supportedInputs`
is therefore sound and consistent with what `create` picks).

### `proposeInput` ignored `MediaConfig::CodecBackend` soft-pin

`VideoEncoderMediaIO::proposeInput` and `AudioEncoderMediaIO::proposeInput`
were resolving the codec from `MediaConfig::VideoCodec` / `AudioCodec` but
not folding in the `MediaConfig::CodecBackend` name before calling
`encoderSupportedInputs`.  If a caller pinned `CodecBackend = "x264"` in
config, proposeInput still used the unpinned (highest-weight) backend's
inputs for the CSC-skip test.  Fixed by the new `resolveEncoderBackend`
anonymous-namespace helper in each `.cpp` file, which applies the same
backend-resolution precedence as `create()`: explicit pin on the codec
wrapper wins, then `CodecBackend` string, then highest-weight default.

Both bugs were exposed by the `codec.h264.x264-nvidia` functional test
(x264 encode → NVDEC decode), added in the codec test cross-product
generalisation.

---

## Future work

### Generic Dijkstra bridge solver

The current solver covers single-hop and one hard-coded
codec-transitive two-hop pattern. Replace with a real Dijkstra over
`MediaDesc` states bounded by `Policy::maxBridgeDepth` so chains
like *FrameSync → CSC → VideoEncoder* (PAL 1080i source → 30p RGB
sink stored as H.264) plan automatically. Requires bridges to
expose "what intermediate would I produce given this input?" so the
search can enumerate frontier states without combinatorial blow-up.

### Per-backend `describe()` deferrals

`describe()` still uses defaults on three backends:

- **`V4L2`** — should populate `producibleFormats` from
  `v4l2QueryDevice()` (already exists for `mediaplay --probe`)
  without needing to open the device.
- **`Rtp`** — needs per-payload-type accept-shape rules (RFC 9134 /
  2435 / 4175 / L16 each have distinct constraints); currently the
  planner falls back to opening the SDP-driven session.
- **`ImageFile`** as a source — should probe the file header to
  populate `producibleFormats` and `containerMetadata` without
  paying a full open. Current fallback is an open/close cycle.

### Bridge solver memoisation

Dijkstra over `MediaDesc` may revisit identical sub-problems across
fan-out / multi-stage configs. Worth a process-wide cache once the
generic solver lands; not required for first-cut correctness.

### Audio multi-track routing

Current bridge inventory assumes a single audio track per
`MediaDesc`. Multi-track configs with different sample rates
(embedded LPCM at 48 kHz alongside Dolby E at 96 kHz) and per-track
channel re-mapping aren't expressible through the existing `Output*`
config keys. Extend `applyOutputOverrides` and the SRC bridge to
address tracks individually.

### GPU-resident bridges

When NVDEC decodes into GPU memory and the next stage is NVENC
encoding from GPU memory, the planner should recognise the
device-resident path and avoid inserting a host-round-trip CSC.
Requires `MediaDesc` to carry a memory-location field
(host / CUDA / Vulkan / Metal) and the bridge cost to penalise
host↔device hops. Revisit when measurable host↔device bandwidth
pain shows up in real workflows.

### Source-side `proposeOutput` polish

`proposeOutput` now accepts an optional `configDelta` out-parameter.
When a source backend populates it, the planner merges the delta onto
the source stage's config and skips the CSC/SRC bridge entirely (the
no-bridge path).  `TPG` is the first backend to implement this:
`TpgMediaIO::proposeOutput` returns `VideoPixelFormat` (and optionally
`VideoFormat` + audio keys) in the delta so the planner can re-
configure TPG to produce whatever a sink prefers without inserting a
CSC.  Two planner unit tests cover the paths
(`MediaPipelinePlanner_SourceRenegotiation_TpgAdoptsSinkPixelFormat`
and `..._FallsBackToCSCWhenNoDelta`).

Remaining open items:
- `V4L2` should populate the delta against its enumerated mode set.
  Other sources (file readers, RTP receivers) genuinely produce what
  their content dictates and correctly use the `NotSupported` default.

### `mediaplay --plan --json`

Today `--plan` prints the resolved config in `describe()` form.
A `--plan --json <PATH>` mode would let users save planner output as
a preset, edit by hand, then replay with `--pipeline <PATH>
--no-autoplan`. Mostly cosmetic — the underlying
`MediaPipelineConfig::saveToFile` already exists.

### `mediaprobe` standalone utility

`mediaplay --describe` covers per-stage introspection adequately.
A standalone `mediaprobe` would be cleaner for "what does this
file / device offer?" queries — drop on the backlog until there's
user demand.

---

## Open questions

- **`Variant<MediaDesc>` cycle.** Confirmed during Phase 2 to be a
  non-trivial refactor (forward-decl alone won't work because the
  X-macro template instantiation needs complete types). Phase 2 went
  with Option B (per-field `Output*` keys + the
  `applyOutputOverrides` base helper). If a future use case needs
  `MediaDesc` as a Variant value, the path is to extract a
  `MetadataOpaque` shim or restructure the `Variant` template
  instantiation site — both bigger lifts than the current setup
  justifies.
- **Compressed → compressed via more than 2 hops.** A future
  "transcode + colour-space" workflow (H.264 → ProRes422HQ where the
  encoder needs YUV422_10) is currently single-hop on the encoder
  bridge but the planner trusts NV12 as the universal intermediate.
  When we hit a codec pair where NV12 is wrong, the codec-transitive
  code needs upgrading or the generic Dijkstra needs to land.
