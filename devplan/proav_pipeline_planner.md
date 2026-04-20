# MediaPipeline Planner — Auto Stage Insertion

**Phase:** 4M
**Status:** Phases 1–5 shipped (2026-04-18). Planner is wired through `MediaPipeline::build` and is the default in `mediaplay`. Remaining work tracked under [Future Work](#future-work) below.
**Library:** `promeki`

`MediaPipelinePlanner` consumes a partial `MediaPipelineConfig` describing only the user-meaningful stages, queries each stage about what it produces / accepts, and returns a fully-resolved `MediaPipelineConfig` with bridging stages (`CSC`, `VideoDecoder`, `VideoEncoder`, `FrameSync`, `SRC`) spliced in wherever a route's source MediaDesc isn't directly accepted by its sink. Companion introspection API (`MediaIO::describe()` returning `MediaIODescription`) is also used by the `mediaplay --describe` flag and intended for future GUI source pickers.

---

## Shipped Architecture

### Introspection — `MediaIODescription` + `MediaIO::describe()`

Shareable value object that captures everything you can know about a `MediaIO` without using it: backend identity, role flags (`canBeSource` / `canBeSink` / `canBeTransform`), `producibleFormats` / `acceptableFormats` / `preferredFormat`, `canSeek` / `frameCount` / `frameRate` / `containerMetadata`, plus `probeStatus` / `probeMessage`. JSON + DataStream round-trip; `summary()` for human display.

`MediaIO::describe()` is a synchronous wrapper that pre-fills identity / roles from `FormatDesc` plus cached state, then dispatches to `MediaIOTask::describe()` for backend-specific format-landscape population.

### Negotiation — `proposeInput()` / `proposeOutput()`

Two synchronous virtuals on `MediaIOTask` (with `MediaIO` thin wrappers). Three-way return contract per virtual:

- `Error::Ok` with `*preferred == offered` → accept as-is.
- `Error::Ok` with `*preferred != offered` → wants conversion to `*preferred`.
- `Error::NotSupported` → cannot consume / produce that shape at all.

Defaults: `proposeInput` accepts anything (passthrough); `proposeOutput` reports `NotSupported` (most sources produce what they produce).

### Bridge registration — `FormatDesc::bridge`

Optional callback per backend:

```cpp
using BridgeFunc = std::function<bool(const MediaDesc &from,
                                       const MediaDesc &to,
                                       MediaConfig *outConfig,
                                       int *outCost)>;
```

Returns `true` when the backend can convert `from → to`, populates the `MediaConfig` to instantiate the bridge stage, and reports a unitless quality cost. Cost bands (lower = higher quality):

| Range         | Meaning                                            |
| ------------- | -------------------------------------------------- |
| 0             | identity / no-op                                   |
| 1 – 10        | metadata-only transform                            |
| 10 – 100      | lossless precision-preserving conversion           |
| 100 – 1 000   | bounded-error lossy                                |
| 1 000 – 10 000| heavily lossy (any encode, deep bit-depth loss)    |
| > 10 000      | last resort                                        |

CSC additionally subtracts a "fast-path bonus" (–25, capped at floor 1) when `CSCRegistry::lookupFastPath` finds a registered SIMD kernel for the pair.

### Planner — `MediaPipelinePlanner::plan()`

Single-hop solver plus a codec-transitive two-hop fallback (VideoDecoder → VideoEncoder for compressed→compressed transitions through an NV12 intermediate). Discovers source MediaDescs via a four-strategy fallback chain (cached `mediaDesc()` → `describe().preferredFormat` → `expectedDesc()` → brief `open()/close()`). Two `plan()` overloads — the second takes an `InjectedStages` map so caller-built MediaIOs (SDL, future device handles) participate in negotiation without the planner trying to build registry stand-ins.

`Policy`: `Quality { Highest | Balanced | Fastest | ZeroCopyOnly }`, `maxBridgeDepth = 4`, `excludedBridges` (StringList).

`MediaPipelineConfig::isResolved()` / `resolved()` are thin wrappers. `MediaPipeline::build(config, autoplan = false)` runs the planner inline when requested and re-emits the planner's multi-line diagnostic through `promekiErr` on failure.

### Bridge / `proposeInput` / `proposeOutput` coverage

| Backend          | bridge | proposeInput              | proposeOutput / describe                                     |
|------------------|:------:|---------------------------|--------------------------------------------------------------|
| `CSC`            | ✓      | rejects compressed        | applies `applyOutputOverrides` from current config           |
| `FrameSync`      | ✓      | passthrough               | applies output rate / channel / format overrides             |
| `SRC`            | ✓      | rejects invalid audio     | applies `OutputAudioDataType` override                       |
| `VideoDecoder`   | ✓      | requires compressed input | reports configured uncompressed `OutputPixelDesc`            |
| `VideoEncoder`   | ✓      | requires uncompressed     | replaces pixel desc with the configured codec's compressed form |
| `TPG`            | —      | n/a (source)              | accepts any uncompressed shape; rejects compressed; describe advertises configured shape + `FrameCountInfinite` |
| `ImageFile` sink | —      | per-extension PixelDesc preference table (DPX, CIN, JPEG, PNG, TGA, SGI, PNM); preserves source bit depth | default                                                      |
| `AudioFile` sink | —      | per-extension DataType preference (WAV/BWF/AIFF/FLAC/W64/RF64 pass-through; OGG/Opus → Float32); preserves source bit depth | default                                                      |
| `QuickTime` sink | —      | curated FourCC whitelist (compressed: H.264 / HEVC / AV1 / ProRes / JPEG / JPEG XS; uncompressed: RGB8 / RGBA8 / YUYV / UYVY / v210 / I422 / I420 / NV12 / NV16); chroma + bit depth preserved | default                                                      |
| `SDLPlayer`      | —      | restricted to native `mapPixelDesc` set; prefers same colour family + bit depth | describe advertises full SDL-native acceptable list           |
| `RawBitstream`          | —      | rejects uncompressed input (`NotSupported`); passthrough on compressed | describes every registered compressed PixelDesc as acceptable |
| `Burn`                  | —      | rejects compressed and non-paintable input; substitutes same-family uncompressed via `defaultUncompressedPixelDesc` | passthrough (output shape == input shape) |
| `FrameBridge` / `Inspector` | — | default (passthrough) | default                                                      |

### `mediaplay` integration

- Autoplan is the default; `--no-autoplan` opts out.
- `--plan` runs the planner and prints the resolved config (stages + routes) without opening anything.
- `--describe` instantiates each declared stage, calls `MediaIO::describe`, and prints the `MediaIODescription::summary` lines.
- SDL UI + `SDLPlayer` injection is hoisted to the top of `main()` so `--plan` / `--describe` see the live SDL stage via the `InjectedStages` planner overload.
- Failures print the planner's full multi-line diagnostic (the gapped route, upstream produced desc, sink preferred desc, per-bridge trace with cost / decline / exclusion reason, codec-transitive applicability) one line per `promekiErr`.

### Refactors landed alongside

- `MediaIOMode` renamed library-wide: `Output → Source`, `Input → Sink`, `InputAndOutput → Transform`. `FormatDesc::canOutput / canInput / canInputAndOutput → canBeSource / canBeSink / canBeTransform`. Same for the `MediaIODirection` TypedEnum. No backwards-compatible aliases.
- `MediaIO::setMediaDesc → setExpectedDesc` (+ getter `expectedDesc()`). Same for `setAudioDesc`, `setMetadata`.
- `MediaIOTask_Converter` deleted; `feedback_csc_not_converter.md` updated to note actual removal.
- `MediaIOTask::applyOutputOverrides(input, config)` static helper that all transforms call to derive their produced `MediaDesc` from the standard `Output*` config keys.
- `MediaDesc::List` / `ImageDesc::List` / `AudioDesc::List` aliases plus `operator==` on `MediaDesc` and `ImageDesc` (foundation work flushed up by the planner's needs).
- `AudioDesc::setDataType` bug fix — was leaving the cached `_format` pointer stale; now refreshes via `lookupFormat`.
- DataStream type tag `TypeMediaIODescription = 0x42`.

### Test coverage

| File                                         | Cases | Assertions |
|----------------------------------------------|------:|-----------:|
| `tests/unit/mediaiodescription.cpp`          |    10 |         97 |
| `tests/unit/mediaio_negotiation.cpp`         |    19 |         83 |
| `tests/unit/mediaiotask_overrides.cpp`       |    15 |         35 |
| `tests/unit/mediaio_bridges.cpp`             |    23 |         83 |
| `tests/unit/mediapipelineplanner.cpp`        |    18 |         81 |

Full suite: 4209 tests, zero warnings (2026-04-19).

### Documentation

`docs/mediaplanner.dox` — algorithm, cost scale, `Policy` semantics, how to author a new bridge backend, diagnostic format, three worked examples (pixel-format gap, decoder, codec-transitive), known limits.

---

## Future Work

### Generic Dijkstra bridge solver

The current solver covers single-hop and one hard-coded codec-transitive two-hop pattern. A future revision will replace this with a real Dijkstra over `MediaDesc` states bounded by `Policy::maxBridgeDepth` so chains like *FrameSync → CSC → VideoEncoder* (e.g. PAL 1080i source → 30p RGB sink stored as H.264) plan automatically. Requires bridges to expose a "what intermediate would I produce given this input?" query so the search can enumerate frontier states without combinatorial blow-up.

### Per-backend `describe()` deferrals

`describe()` still uses defaults on three backends because doing them right requires non-trivial work outside the planner's scope:

- **`V4L2`** — should populate `producibleFormats` from `v4l2QueryDevice` (already exists for `mediaplay --probe`) without needing to actually open the device.
- **`Rtp`** — needs per-payload-type accept-shape rules (RFC 9134 / 2435 / 4175 / L16 each have distinct constraints); currently the planner falls back to opening the SDP-driven session.
- **`ImageFile`** as a *source* — should probe the file header to populate `producibleFormats` and `containerMetadata` without paying a full open. Current planner-fallback does an open/close cycle.

### Bridge solver memoisation

Dijkstra over `MediaDesc` may revisit identical sub-problems across fan-out / multi-stage configs. Worth a process-wide cache once the generic solver lands; not required for first-cut correctness.

### Audio multi-track routing

The current bridge inventory assumes a single audio track per `MediaDesc`. Multi-track configs with different sample rates per track (e.g. embedded LPCM at 48 kHz alongside Dolby E at 96 kHz) and per-track channel re-mapping aren't expressible through the existing `Output*` config keys. Need to extend `applyOutputOverrides` and the SRC bridge to address tracks individually.

### GPU-resident bridges

When NVDEC decodes into GPU memory and the next stage is NVENC encoding from GPU memory, the planner should recognise the device-resident path and avoid inserting a host-round-trip CSC. Requires `MediaDesc` to carry a "memory location" (host / CUDA / Vulkan / Metal) and the bridge cost to penalise host↔device hops. Revisit once we have measurable host↔device bandwidth pain in real workflows.

### Source-side `proposeOutput` polish

`TPG` already overrides `proposeOutput` to accept any uncompressed shape (the planner can re-configure TPG instead of inserting a CSC). `V4L2` should do the same against its enumerated mode set. Other sources (file readers, RTP receivers) genuinely produce what their content dictates and correctly use the `NotSupported` default.

### `mediaplay --plan` JSON output

Today `--plan` prints the resolved config in `describe()` form. A `--plan --json <PATH>` mode that writes the resolved config as JSON would let users save planner output as a preset, edit by hand, then replay with `--pipeline <PATH> --no-autoplan`. Mostly cosmetic — the underlying `MediaPipelineConfig::saveToFile` already exists.

### `mediaprobe` standalone utility

`mediaplay --describe` covers the per-stage introspection use case adequately. A standalone `mediaprobe` would be cleaner for "what does this file / device offer?" queries that don't need a full pipeline config — drop on the backlog until there's user demand.

---

## Open Questions

- **`Variant<MediaDesc>` cycle.** Confirmed during Phase 2 to be a non-trivial refactor (forward-decl alone won't work because the X-macro template instantiation needs complete types). Phase 2 went with Option B (per-field `Output*` keys + the `applyOutputOverrides` base helper). If a future use case needs `MediaDesc` as a Variant value, the path is to extract a `MetadataOpaque` shim or restructure the `Variant` template instantiation site — both bigger lifts than the current setup justifies.
- **Compressed → compressed via more than 2 hops.** A future "transcode + colour-space" workflow (H.264 → ProRes422HQ where the encoder needs YUV422_10) is currently single-hop on the encoder bridge but the planner trusts NV12 as the universal intermediate. Once we hit a codec pair where NV12 is wrong the codec-transitive code needs upgrading or the generic Dijkstra needs to land.
