# MediaPipelinePlanner {#mediaplanner}

Automatic insertion of bridging stages (CSC, decoder, frame
sync, sample-rate converter, encoder) into a partial
`MediaPipelineConfig`.

`MediaPipelinePlanner` is the offline pass that consumes a
partial pipeline config — one in which some routes carry a format
gap between their source's output and their sink's accepted input
— and returns a new config in which every route is directly
format-compatible. The planner does this by walking the registered
`MediaIO::FormatDesc::bridge` callbacks and choosing the
cheapest applicable bridge for each gap.

## When the planner runs {#mediaplanner_when}

Call `MediaPipelinePlanner::plan` directly, or — more commonly —
use the convenience entry points:

- `MediaPipelineConfig::resolved` — returns the planned config.
- `MediaPipelineConfig::isResolved` — pre-flight check, no
  bridges instantiated.
- `MediaPipeline::build(cfg, autoplan = true)` — runs the planner
  under the hood before instantiation.
- `mediaplay` (the bundled CLI) runs the planner by default;
  pass `--no-autoplan` to opt out and require a strict,
  fully-resolved input config.

## Algorithm {#mediaplanner_algorithm}

For each route `A → B` in topological order:

1. Discover `A`'s produced `MediaDesc` through a four-strategy
   fallback chain:
   1. cached `MediaIO::mediaDesc` on an already-open stage,
   2. `MediaIO::describe`'s `preferredFormat`,
   3. the pre-open hint from `MediaIO::setExpectedDesc`,
   4. as a last resort, briefly `MediaIO::open` the source in
      `MediaIO::Source` mode to read its `mediaDesc`, then close.
2. Ask `B` via `MediaIO::proposeInput` what shape it actually
   wants. If the answer matches `A`'s produced desc, the route
   is direct — the planner emits it unchanged.
3. Otherwise, the planner walks every registered backend whose
   `MediaIO::FormatDesc::bridge` callback is non-null and asks
   each "can you convert from `A`'s desc to `B`'s preferred?"
   The cheapest acceptable bridge wins.
4. If no single bridge fits and both sides are compressed pixel
   formats, the planner tries the codec-transitive two-hop pattern
   `MediaIOTask_VideoDecoder → MediaIOTask_VideoEncoder`
   against an intermediate uncompressed shape.
5. Inserted stages are appended to the output config with stable
   generated names of the form `"<from>__bridge<N>__<to>"`,
   and the original route is rewritten as a chain through the
   inserted stages.

The pass is deterministic — given the same input config and the
same registered backend set, it always returns the same resolved
config. Re-planning a resolved config is a no-op.

## Cost scale {#mediaplanner_costs}

Bridge backends report a unitless integer cost via the `outCost`
out-parameter of `MediaIO::FormatDesc::BridgeFunc`. The
planner picks the cheapest applicable bridge. Costs follow this
fixed scale:

| Range         | Meaning                                     | Examples                                 |
| ------------- | ------------------------------------------- | ---------------------------------------- |
| 0             | identity / no-op                            | (the planner skips inserting a no-op)    |
| 1 – 10        | metadata-only transform                     | rename, re-tag                           |
| 10 – 100      | lossless precision-preserving conversion    | RGBA8 ↔ BGRA8, planar ↔ semi-planar same depth |
| 100 – 1000    | bounded-error lossy                         | YCbCr 4:2:2 → 4:2:0, dither, gamut clip  |
| 1000 – 10000  | heavily lossy                               | encode (any), 10-bit → 8-bit, downscale  |
| > 10000       | last resort / quality-destroying            | sample-rate halving without filter       |

## Tuning the planner — Policy {#mediaplanner_policy}

`MediaPipelinePlanner::Policy` lets callers steer the search:

- `Policy::quality`:
  - `Highest` — raw cost, lowest wins (default).
  - `Balanced` — small penalty for heavily-lossy bridges.
  - `Fastest` — large penalty for heavily-lossy bridges; prefers
    cheap CPU paths even at some quality cost.
  - `ZeroCopyOnly` — hard reject any single-hop bridge whose raw
    cost exceeds 100; planning fails when the gap requires a
    lossy bridge.
- `Policy::maxBridgeDepth` — caps the number of bridges per
  route (default 4).
- `Policy::excludedBridges` — backend type names the planner
  is forbidden from using. Use this to force a particular path:
  e.g. `{"VideoEncoder"}` blocks any transcode insertion.

## Authoring a new bridge backend {#mediaplanner_authoring}

Any `MediaIOTask` backend can become a planner-insertable bridge
by setting `MediaIO::FormatDesc::bridge` in its `formatDesc`
registration. The callback signature is:

```cpp
bool bridge(const MediaDesc &from,
            const MediaDesc &to,
            MediaIO::Config *outConfig,
            int *outCost);
```

Return `true` only when the bridge is applicable to the `from` /
`to` pair. When returning true:

- Populate `outConfig` with the `MediaConfig` that the
  planner should hand to `MediaIO::create` when instantiating
  this bridge stage. Always start from
  `MediaIO::defaultConfig(name)` so spec defaults flow through.
- Populate `outCost` using the bands above. Higher numbers mean
  lower quality — the planner picks the smallest.

The transform's own `MediaIOTask::proposeInput` should accept
the `from` shape (otherwise the bridge will be inserted but fail
at open time), and its `MediaIOTask::proposeOutput` should
return the `to` shape via `MediaIOTask::applyOutputOverrides`
so the planner can compute downstream descs. Following this
pattern keeps every transform symmetrically usable from either the
planner or hand-authored configs.

## Diagnostic output {#mediaplanner_diagnostics}

On failure, `MediaPipelinePlanner::plan` writes a multi-line
diagnostic to its `diagnostic` out-parameter explaining:

- The route that could not be resolved.
- The upstream produced `MediaDesc`.
- The sink's preferred `MediaDesc`.
- Each registered bridge's verdict — accepted (with raw and
  adjusted costs), declined, excluded by policy, or rejected
  by `ZeroCopyOnly`.
- Whether the codec-transitive two-hop fallback was applicable.

`MediaPipeline::build` re-emits each line through `promekiErr`
so logs stay grep-friendly. In `mediaplay` the planner runs by
default — failures point straight at the gap without requiring the
user to read planner code.

## Worked examples {#mediaplanner_examples}

### Source produces RGBA8, sink wants NV12 {#mediaplanner_ex_pixel_gap}

A typical `TPG → QuickTime mp4` pipeline. TPG generates
RGBA8_sRGB by default; the H.264 encoder upstream of QuickTime
needs NV12. The planner inserts a single `CSC` stage with
`OutputPixelFormat` set to `YUV8_420_SemiPlanar_Rec709`.

```
// Before planning:
//   src (TPG) ──> sink (QuickTime, expects NV12)
//
// After planning:
//   src ──> src__bridge0__sink (CSC) ──> sink
```

### Compressed source → uncompressed sink {#mediaplanner_ex_decoder}

H.264 file → SDL display. The planner inserts a
`VideoDecoder` configured with `VideoCodec=H264` and
`OutputPixelFormat` set to whatever the SDL widget reports as its
preferred input via `MediaIO::proposeInput`.

### Compressed → different compressed codec {#mediaplanner_ex_codec_transitive}

H.264 source → HEVC sink. No single bridge fits, so the
codec-transitive fallback synthesises an intermediate
uncompressed shape (NV12 by convention) and chains
`VideoDecoder → VideoEncoder`. The planner emits two new
stages and rewrites the route accordingly.

## Known limits {#mediaplanner_limits}

- Multi-hop chains beyond the codec-transitive special case are
  out of scope for v1. A future revision will replace this with
  a generic Dijkstra search bounded by
  `Policy::maxBridgeDepth`.
- The planner currently reads source MediaDescs by opening them
  when no `describe` / `expectedDesc` data is available. This
  has side effects for live capture sources (RTP, V4L2) — they
  bind sockets / lock device handles. Backends that implement
  `MediaIOTask::describe` avoid the cost.
- Per-sink `MediaIOTask::proposeInput` overrides are not yet
  implemented for every sink. Sinks that fall back to the
  accept-anything default cause the planner to leave compatible-
  looking gaps unbridged. Add a `proposeInput` override to any
  sink that has well-known input constraints.
