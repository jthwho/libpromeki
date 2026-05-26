# Whisper transcription backend {#whisper}

How to enable and use the vendored whisper.cpp speech-to-text
backend that drives libpromeki's first concrete
@ref promeki::TranscriptionEngine implementation.

The backend wraps [ggml-org/whisper.cpp](https://github.com/ggml-org/whisper.cpp)
(pinned to v1.8.4) behind the generic `TranscriptionEngine`
interface, so application code never calls whisper directly — it
resolves a session via `TranscriptionEngine::create("WhisperCpp",
&config)` and the registry hands back a configured engine.

## 1. Build flag

The backend is built whenever `PROMEKI_ENABLE_WHISPER=ON` (the
default when `PROMEKI_ENABLE_PROAV=ON`):

```
cmake -B build -S . -DPROMEKI_ENABLE_WHISPER=ON
build
```

The flag pulls in `thirdparty/whisper.cpp` as an
`ExternalProject` and produces four static archives under the
shared third-party prefix:

- `libwhisper.a`   — the high-level Whisper API
- `libggml.a`      — GGML dispatcher
- `libggml-cpu.a`  — CPU backend (the only one enabled on this cut)
- `libggml-base.a` — shared graph / tensor core

All four are linked PRIVATE into `libpromeki.so`; the Whisper API
surface is an implementation detail of the backend and is not
re-exported.

System install (skip the vendored build):

```
cmake -B build -S . -DPROMEKI_USE_SYSTEM_WHISPER=ON
```

CUDA / Metal / Vulkan GPU backends inside whisper.cpp are
intentionally OFF on this first cut.  GPU support will land in a
follow-up gated on `PROMEKI_ENABLE_CUDA` (no separate
`PROMEKI_ENABLE_WHISPER_CUDA` flag).

## 2. Model files

Whisper does not ship with model weights — they are
multi-hundred-megabyte tensor files that must be staged outside
the library build.  libpromeki standardises a single location:

```
${PROMEKI_OPT_ModelsDir:-$XDG_DATA_HOME/promeki/models}/whisper/ggml-<name>.bin
```

@ref promeki::Dir::models returns the effective directory,
backed by @ref promeki::LibraryOptions::ModelsDir.  Per platform:

| Platform | Default                                                       |
|----------|----------------------------------------------------------------|
| Linux    | `$XDG_DATA_HOME/promeki/models`<br>`$HOME/.local/share/promeki/models` |
| macOS    | `$HOME/Library/Application Support/promeki/models`            |
| Windows  | `%LOCALAPPDATA%\promeki\models`                               |

Override at runtime with the env var:

```
export PROMEKI_OPT_ModelsDir=/var/lib/promeki/models
```

…or programmatically:

```cpp
LibraryOptions::instance().set(
    LibraryOptions::ModelsDir,
    String("/var/lib/promeki/models"));
```

## 3. Fetching a model

A vendored utility downloads the canonical Whisper GGML weights
from Hugging Face into the standard models directory:

```
build/bin/promeki-fetch-model --list
build/bin/promeki-fetch-model small
build/bin/promeki-fetch-model large-v3-q5_0 --dest /custom/dir
```

The utility knows the canonical
[`ggerganov/whisper.cpp`](https://huggingface.co/ggerganov/whisper.cpp/tree/main)
URLs for `tiny`, `base`, `small`, `medium`, `large-v3`,
`large-v3-turbo`, their `.en` (English-only) variants, and a few
`q5_0` quantizations.  Downloads stream over HTTPS with optional
SHA-256 verification — when a catalog entry carries a hash the
tool refuses to overwrite the destination on mismatch.  See
`promeki-fetch-model --help` for the full surface.

Manual download with `curl` / `wget` is fine too — drop
`ggml-<name>.bin` into `Dir::models()/whisper/` and the engine
picks it up automatically.

## 4. Engine session

The backend registers itself as `"WhisperCpp"` at process startup.
Resolve a session with the standard
@ref promeki::TranscriptionEngine::create entry point:

```cpp
#include <promeki/transcriptionengine.h>
#include <promeki/mediaconfig.h>

MediaConfig cfg;
cfg.set(MediaConfig::TranscriptionSessionMode,
        Variant(TranscriptionMode(TranscriptionMode::Batch)));
cfg.set(MediaConfig::TranscriptionLanguage, Variant(String("en")));
cfg.set(MediaConfig::TranscriptionWordTimestamps, Variant(true));

auto res = TranscriptionEngine::create("WhisperCpp", &cfg);
if (error(res).isError()) {
    promekiFatal("WhisperEngine create failed: %s",
                 error(res).name().cstr());
}
auto engine = std::move(value(res));

// Push every Frame carrying audio you want transcribed.
for (const Frame &f : sourceFrames) {
    engine->submitFrame(f);
}
engine->flush();

// Drain finalised cues.
while (Frame out = engine->receiveFrame(), out.isValid()) {
    Transcript t = out.metadata().get(Metadata::Transcript).getAs<Transcript>();
    promekiInfo("[%lld .. %lld] %s",
                t.start().nanoseconds() / 1'000'000LL,
                t.end().nanoseconds() / 1'000'000LL,
                t.text().cstr());
}
```

### Supported MediaConfig keys

| Key                              | Notes                                                                                    |
|----------------------------------|------------------------------------------------------------------------------------------|
| `TranscriptionSessionMode`       | Only `Batch` is accepted on this cut; `Streaming` returns `Error::NotSupported`.        |
| `TranscriptionStreamIndex`       | -1 (default) = first PCM audio payload on the source Frame.                              |
| `TranscriptionChannelMode`       | `ChannelMap` (default), `ChannelIndex`, or `DownmixAll`.                                 |
| `TranscriptionChannelMap`        | Consulted when mode is `ChannelMap`.  Empty = prefer `FrontCenter`, else downmix all.    |
| `TranscriptionChannelIndex`      | Consulted when mode is `ChannelIndex`.                                                   |
| `TranscriptionLanguage`          | BCP 47 tag (`"en"`, `"en-US"`, `"fr-CA"`).  Empty = whisper's auto-detect.               |
| `TranscriptionModelHint`         | Bare name (`"small"`), absolute path, or empty (= `"small"`, multilingual).             |
| `TranscriptionWordTimestamps`    | When true, each emitted Transcript carries one TranscriptWord per recognised word.       |

`TranscriptionDiarization`, `TranscriptionVad`, and
`TranscriptionEndpointSilence` are read-but-ignored on this cut
(whisper.cpp has no speaker diarization, and VAD / endpointing
only matter in streaming mode).

### Default model

When `TranscriptionModelHint` is empty the engine resolves to
`ggml-small.bin` — multilingual, ~470 MB on disk, real-time on a
modern desktop CPU.  The engine reports a clean
`Error::NotExist` (with the expected path in the message) if the
model file is absent, so an application can surface a "please
run promeki-fetch-model" message rather than crashing.

## 5. Limitations / roadmap

- **Batch only.**  Streaming mode (interim partials during
  `submitFrame`) requires careful sliding-window management with
  audio-context overlap.  It will land in a follow-up.
- **CPU only.**  GGML's CUDA / Metal / Vulkan backends are off
  in the vendored build.  GPU support will be wired behind the
  existing `PROMEKI_ENABLE_CUDA` flag.
- **No speaker diarization.**  Whisper.cpp doesn't ship a
  diarizer; we'll plumb that through a separate
  `SpeakerDiarizer` interface when it's needed.
- **In-memory model load.**  Whisper.cpp `mmap`s the model file
  internally, so peak RSS reflects model size + per-decode
  scratch (a few hundred MB on top for `small`).  Plan storage
  accordingly when staging large-v3 (~3 GB).
