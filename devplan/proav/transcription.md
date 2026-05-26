# TranscriptionEngine & WhisperCpp backend — open work

**Phase:** 4T (proav speech-to-text subsystem)
**Library:** `promeki`

This document tracks work on the `TranscriptionEngine` abstraction and its
first concrete backend (`WhisperCpp`).  The Phase 1 batch-only CPU implementation
shipped; the items below are known follow-ups.

## What shipped (Phase 1, 2026-05-25)

- `TranscriptionEngine` abstract base + backend registry
  (`include/promeki/transcriptionengine.h`).
- `Transcript` / `TranscriptWord` / `TranscriptList` value types with
  CoW semantics, DataStream serialization, and Variant integration
  (`include/promeki/transcript.h`).
- `SubtitleCueBuilder` — Transcript → Subtitle cue shaping policy
  (`include/promeki/subtitlecuebuilder.h`).
- `MediaConfig` keys: `TranscriptionChannelMode`, `TranscriptionChannelMap`,
  `TranscriptionChannelIndex`, `TranscriptionLanguage`,
  `TranscriptionSessionMode`, `TranscriptionStreamIndex`,
  `TranscriptionModelHint`, `TranscriptionDiarization`,
  `TranscriptionWordTimestamps`, `TranscriptionVad`,
  `TranscriptionEndpointSilence`; and `SubtitleCue*` shaping keys.
- `TranscriptionMode` / `TranscriptionChannelMode` enums in `enums.h`.
- `Metadata::Transcript` key in `metadata.h`.
- `WhisperCpp` backend via vendored `whisper.cpp` v1.8.4.  Batch mode
  only; CPU only; `PCMI_Float32LE` / `PCMI_S16LE` inputs.
- `Dir::models()` + `LibraryOptions::ModelsDir` convention.
- `promeki-fetch-model` CLI (SHA-256-verified HF model downloader).
- `docs/whisper.md` doxygen page.
- Unit tests: `subtitlecuebuilder.cpp`, `transcriptionengine.cpp`,
  `whispertranscriptionengine.cpp` (registration + gated e2e).

## Open

### Streaming / sliding-window mode

`TranscriptionMode::Streaming` is registered in the backend record but
rejected with `Error::NotSupported` at `create()` time.

- [ ] Walk the sliding-window pattern from `whisper.cpp`'s `examples/stream`.
- [ ] Overlap windows by N samples; emit partials (with
      `Transcript::setPartial(true)`) after each window.
- [ ] Finalise on endpoint-silence (`TranscriptionEndpointSilence` config key).
- [ ] Drop the `Error::NotSupported` rejection in `WhisperCpp::onConfigure`
      once the path is tested.

### CUDA (GPU) backend

- [ ] Pass `-DGGML_CUDA=ON` into the ExternalProject when
      `PROMEKI_ENABLE_CUDA=ON`.
- [ ] Flip `whisper_context_params::use_gpu = true` inside `ensureContext()`.
- [ ] Re-test on a CUDA host; add a gated unit test or e2e note.

### promeki-fetch-model SHA-256 catalog

Only the `tiny` entry has a verified SHA-256.  The others need to be
downloaded, verified, and filled in:

- [ ] `base`, `base.en`, `small`, `small.en`, `medium`, `medium.en`,
      `large-v3`, `large-v3-turbo` — populate `sha256Hex` field in
      `kCatalog[]` in `utils/promeki-fetch-model/main.cpp`.

### Speaker diarization

`TranscriptionDiarization` config key is declared; no backend implements it.

- [ ] Evaluate whisper.cpp's diarization shim or a stand-alone pyannote-
      onnx port once streaming mode is stable.

### Second transcription backend

No Vosk, sherpa-onnx, or cloud STT backend exists yet.  When adding one:

- [ ] Mirror the static-registrar pattern in `whispertranscriptionengine.cpp`.
- [ ] Declare `supportedInputs` / `supportedModes` explicitly in
      `BackendRecord`.
- [ ] Add a gated unit test (skip when the model / SDK is absent).

### Streaming HTTP download in promeki-fetch-model

Current path buffers the full model response in memory before writing to
disk.  Large models (`.large-v3`, ~3 GB) will OOM on low-memory hosts.

- [ ] Replace the buffered `HttpRequest::setBodySink`-free path with a
      streaming `BodySink` that writes chunks directly to disk as they
      arrive via the `IODevice` API.
