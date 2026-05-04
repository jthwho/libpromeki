# ProAV DSP and Effects (future)

**Phase:** 4C
**Dependencies:** the CSC / SRC backends (see [backends.md](backends.md))
**Library:** `promeki`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `devplan/README.md` for full requirements.

Audio DSP (filters, resampler, format converter) will land as MediaIO backends — either specialised `CscMediaIO` / `SrcMediaIO` configurations or dedicated stage subclasses.

Nothing in this phase is actively in progress. It is listed here so the capability is not forgotten.

---

## Planned DSP MediaIO Backends

All implemented as `MediaIO` backends (typically derived from `SharedThreadMediaIO` like the existing `CscMediaIO` / `SrcMediaIO`). Each takes frames on `writeFrame()` and emits processed frames on `readFrame()` / `frameReadySignal`.

### AudioFilter backend

Biquad EQ filter (low/high-pass, band-pass/stop, notch, shelf, peaking) using Robert Bristow-Johnson's Audio EQ Cookbook formulas. Direct Form II Transposed. Stateful per-channel.

- Config keys: `ConfigFilterType`, `ConfigFrequency`, `ConfigQ`, `ConfigGainDb`
- `setParams` at runtime allows parameter changes without reconstruction
- May reuse `SrcMediaIO` scaffolding for audio routing; lives as a dedicated subclass if it needs custom state layout

### AudioResampler backend

**Partially implemented (2026-04-12):** `AudioResampler` class exists in `include/promeki/audioresampler.h` / `src/proav/audioresampler.cpp`, backed by vendored libsamplerate (PROMEKI_ENABLE_SRC). Supports variable ratio, all five libsamplerate quality modes (SincBest → ZeroOrderHold via `SrcQuality` enum in `enums.h`), and end-of-input flush. Integrated into `AudioBuffer` for transparent push-time rate conversion and PI-controller clock-drift correction. V4L2 task uses this via `enableDriftCorrection()`.

Remaining work for the full MediaIO backend:
- `MediaIO` backend wrapping `AudioResampler` for pipeline use (config keys: `OutputSampleRate`, `SrcQuality`)
- Preserves channel count; no channel-map conversion needed for this backend

### AudioFormatConverter backend

Sample format conversion (int16/int24/int32/float32/float64 interconversion), with TPDF dithering for int downconversion. Much of this functionality already exists via `AudioFormat::convertTo()` and the direct-converter registry — the backend would be a thin MediaIO wrapper that adds dither on integer downconversion.

- Config keys: `OutputAudioDataType`, `DitherType`
- For the initial cut, the existing `SrcMediaIO` covers the
  no-dither path; this backend would be the dithered variant.

---

## Why This Is Deferred

DSP is not on the critical path for the current user workload. The
existing `CscMediaIO` / `SrcMediaIO` backends already prove out the
"writeFrame → readFrame" contract for intra-frame transforms, so the
backends above are straightforward to build on top of that pattern as
soon as a real use case needs biquad filtering or sample-rate
conversion inside a `MediaPipeline`. Until then, no implementation
work happens here.
