# ProAV DSP and Effects (future)

**Phase:** 4C
**Dependencies:** `MediaIOTask_Converter` (see `proav_nodes.md`)
**Library:** `promeki`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

Audio DSP (filters, resampler, format converter) lands as MediaIO backends — either specialised `MediaIOTask_Converter` configurations or dedicated converter subclasses.

Nothing in this phase is actively in progress. It is listed here so the capability is not forgotten.

---

## Planned DSP MediaIO Backends

All implemented as `MediaIOTask` subclasses (or configurations of `MediaIOTask_Converter`). Each takes frames on `writeFrame()` and emits processed frames on `readFrame()` / `frameReadySignal`.

### AudioFilter backend

Biquad EQ filter (low/high-pass, band-pass/stop, notch, shelf, peaking) using Robert Bristow-Johnson's Audio EQ Cookbook formulas. Direct Form II Transposed. Stateful per-channel.

- Config keys: `ConfigFilterType`, `ConfigFrequency`, `ConfigQ`, `ConfigGainDb`
- `setParams` at runtime allows parameter changes without reconstruction
- Reuses `MediaIOTask_Converter` scaffolding for audio routing; may live as a dedicated subclass if it needs custom state layout

### AudioResampler backend

**Partially implemented (2026-04-12):** `AudioResampler` class exists in `include/promeki/audioresampler.h` / `src/proav/audioresampler.cpp`, backed by vendored libsamplerate (PROMEKI_ENABLE_SRC). Supports variable ratio, all five libsamplerate quality modes (SincBest → ZeroOrderHold via `SrcQuality` enum in `enums.h`), and end-of-input flush. Integrated into `AudioBuffer` for transparent push-time rate conversion and PI-controller clock-drift correction. V4L2 task uses this via `enableDriftCorrection()`.

Remaining work for the full MediaIO backend:
- `MediaIOTask` subclass wrapping `AudioResampler` for pipeline use (Config keys: `ConfigOutputSampleRate`, `ConfigQuality`)
- Preserves channel count; no channel-map conversion needed for this backend

### AudioFormatConverter backend

Sample format conversion (int16/int24/int32/float32/float64 interconversion), with TPDF dithering for int downconversion. Much of this functionality already exists via `Audio::convertTo()` — the backend is a thin MediaIO wrapper.

- Config keys: `ConfigOutputAudioDataType`, `ConfigDitherType`
- For the initial cut, just delegate to `Audio::convertTo()` inside a Converter configuration

---

## Why This Is Deferred

DSP is not on the critical path for the current user workload. `MediaIOTask_Converter` already proves out the "writeFrame → readFrame" contract for intra-frame transforms, so the backends above are straightforward to build on top of the Converter framework as soon as a real use case needs biquad filtering or sample-rate conversion inside a `MediaPipeline`. Until then, no implementation work happens here.
