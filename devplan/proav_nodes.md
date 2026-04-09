# MediaIO Backends

**Phase:** 4B
**Dependencies:** MediaIO framework (complete), `proav_pipeline.md` (for `MediaPipeline` usage)
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document tracks the `MediaIOTask` backends that plug into the `MediaIO` framework. Each backend is a subclass of `MediaIOTask`, registered with `PROMEKI_REGISTER_MEDIAIO`, and self-describes through a `FormatDesc`. All new media capabilities go here — there is no longer a separate "MediaNode" layer (see `proav_pipeline.md` for the history on the deprecated MediaNode/MediaPipeline).

---

## Completed MediaIO Framework and Backends

The framework itself (`MediaIO` controller, `MediaIOTask` interface, `Strand` per-instance executor, `MediaIOCommand` dispatch, three `VariantDatabase` types for Config/Stats/Params, factory + registration, cached state, EOF latching, step/prefetch/seek modes, track selection, mid-stream descriptor updates, `cancelPending()`, error/signal plumbing) is complete and documented in `docs/mediaio.dox`. The following backends are complete and tested:

- **MediaIOTask_TPG** — Test pattern generator (video + audio + timecode). Static pattern caching, motion, text burn-in with FiraCode default, AvSync mode, NTSC audio cadence via `FrameRate::samplesPerFrame()`. `ConfigVideoSize` / `ConfigFrameRate` / full config key set.
- **MediaIOTask_ImageFile** — DPX, Cineon, TGA, SGI, RGB, PNM/PPM/PGM, PNG, RawYUV. PNG backend uses libspng+zlib-ng with O_DIRECT save path. Default frame rate via `ConfigFrameRate`.
- **MediaIOTask_AudioFile** — WAV, BWF, AIFF, OGG via libsndfile. Frame chunking by samples/frame, seek delegation, step control, audiodesc from file on read / config on write.
- **MediaIOTask_QuickTime** — Classic + fragmented reader and writer for `.mov` / `.mp4`. ProRes + H.264 + HEVC + PCM + AAC read; ProRes + PCM write. udta metadata including ©-atoms, XMP BWF extension, UMID, write-time defaults. BufferPool + AudioBuffer FIFO utilities in place.
- **SDLPlayerTask** — Write-only display sink with audio-led pacing, fast mode, main-thread `renderPending()` dispatch. Injected via `MediaIO::adoptTask()`.

All test coverage lives in `tests/mediaio.cpp`, `tests/quicktime.cpp`, `tests/mediaiotask_quicktime.cpp`, `tests/strand.cpp`, `tests/audiobuffer.cpp`, `tests/bufferpool.cpp`, plus the per-backend format tests. See git history for the sprawling completed-work log — this document stays focused on what still needs to be built.

---

## MediaIOTask_Converter

Generic ReadWrite MediaIO that accepts a frame on `writeFrame()`, transforms it, and emits the result on `readFrame()`. Replaces the deprecated `JpegEncoderNode`, `ColorModelConvertNode`, `FrameDemuxNode`, and any other single-input / single-output transformation "node" in the legacy pipeline.

**Files (complete):**
- `include/promeki/mediaiotask_converter.h`
- `src/proav/mediaiotask_converter.cpp`
- `tests/mediaiotask_converter.cpp`

**Initial version (complete):**

- Registered as `"Converter"` with `canReadWrite = true`; Reader/Writer-only modes are rejected.
- Config keys: `ConfigOutputPixelDesc` (PixelDesc), `ConfigJpegQuality` (int), `ConfigJpegSubsampling` (Enum `ChromaSubsampling`), `ConfigOutputAudioDataType` (Enum `AudioDataType`; `Invalid` = pass-through), `ConfigCapacity` (int, default 4).
- Video transforms:
  - Uncompressed → uncompressed: `Image::convert()` (CSC framework, respects ColorModel baked into the target `PixelDesc`).
  - JPEG encode: `JpegImageCodec::encode()` when the target `PixelDesc` has `codecName() == "jpeg"`.
  - JPEG decode: `JpegImageCodec::decode()` when the source is a compressed JPEG.
  - Pass-through when no output pixel desc is set, or when source and target match.
- Audio transform: `Audio::convertTo()` whenever `ConfigOutputAudioDataType` is set and differs from the input data type; otherwise pass-through.
- Output `MediaDesc` is computed from `pendingMediaDesc` at open so downstream consumers see the post-conversion descriptor before the first frame.
- Internal FIFO with configurable capacity. `executeCmd(Write)` returns `Error::TryAgain` when full; `executeCmd(Read)` returns `Error::TryAgain` when empty. Queue is drained on `close()`.
- Stats: `FramesConverted`, `BytesIn`, `BytesOut`, plus standard `QueueDepth` / `QueueCapacity`.
- Stateless (1 input → 1 output) for all current transforms.

**Remaining work (future):**
- [ ] `ConfigOutputColorModel` / `ConfigOutputSampleRate` as first-class knobs (today the ColorModel rides inside the target `PixelDesc`, sample-rate conversion is deferred to a future audio resampler).
- [ ] `ConfigCodec` / `ConfigCodecOptions` once there is a second `ImageCodec` registered alongside JPEG.
- [ ] Stateful temporal codecs (H.264/HEVC encode) — will need a `drain()` or `flush()` hook beyond the current close path.
- [ ] `cancelPending()` hook that explicitly clears the in-progress FIFO (today it clears on `close()`).
- [ ] Factory discovery of supported conversions via a `FormatDesc::enumerate()` callback.
- [ ] Additional coverage: cancel-while-converting, multi-frame round-trip with pixel-equality tolerance for CSC, QuickTime-sourced ProRes decode through the Converter.

---

## MediaIOTask_RtpVideo — NEW

Bidirectional RTP video transport. Reader mode receives an RTP video stream into frames; Writer mode transmits frames as RTP packets. Replaces the deprecated `RtpVideoSinkNode` and introduces the previously missing receive path.

**Files:**
- [ ] `include/promeki/mediaiotask_rtpvideo.h`
- [ ] `src/proav/mediaiotask_rtpvideo.cpp`
- [ ] `tests/mediaiotask_rtpvideo.cpp`

**Design:**

- [ ] Registered as `"RtpVideo"` with `canRead = true`, `canWrite = true`, `canReadWrite = false` (a single instance is a single direction)
- [ ] Builds on the existing `RtpSession` / `RtpPacket` / `RtpPayload` stack (already complete for the write path)
- [ ] Uses `PacketTransport` once that abstraction lands (see `proav_optimization.md`); until then, directly wraps `RtpSession` + `UdpSocket`
- [ ] Config keys:
  - [ ] `ConfigLocalAddress` / `ConfigLocalPort` — bind address (required)
  - [ ] `ConfigRemoteAddress` / `ConfigRemotePort` — send target (writer) or expected sender (reader, optional)
  - [ ] `ConfigMulticastGroup` — optional multicast group + TTL
  - [ ] `ConfigPayloadType` — RTP payload type (integer, 0-127)
  - [ ] `ConfigRtpPayload` — payload format name: `"RawVideo"` (ST 2110-20), `"Jpeg"` (RFC 2435), future `"H264"`, `"HEVC"`
  - [ ] `ConfigClockRate` — RTP clock rate in Hz (90000 for standard video)
  - [ ] `ConfigDscp` — DSCP marking for QoS
  - [ ] `ConfigPacingMode` — `"None"` / `"Userspace"` / `"KernelFq"` / `"TxTime"` — selects the pacing mechanism (see `proav_optimization.md`)
  - [ ] `ConfigTargetBitrate` — used to compute pacing rate
  - [ ] `ConfigSsrc` — optional fixed SSRC
- [ ] Reader mode:
  - [ ] Receives packets, reassembles fragmented frames, emits complete `Frame::Ptr` on CmdRead completion
  - [ ] Handles packet loss (gaps), timestamp wrap, out-of-order arrival within the reorder window
  - [ ] Discovers stream parameters from the first-received packets and exposes them via `MediaDesc` in `cmd.mediaDesc` (resolution, pixel format, frame rate) — for uncompressed the SDP configuration drives this; for MJPEG the payload headers carry enough
  - [ ] `cmd.canSeek = false`, `cmd.frameCount = MediaIO::FrameCountInfinite`
  - [ ] Supports mid-stream descriptor change via `cmd.mediaDescChanged`
- [ ] Writer mode:
  - [ ] Serializes each input frame into RTP packets via the appropriate `RtpPayload`
  - [ ] Uses `RtpSession::sendPackets()` / `sendPacketsPaced()` based on `ConfigPacingMode`
  - [ ] Emits `cmd.framesSent`, `cmd.bytesSent`, `cmd.packetsSent` via `MediaIOStats`
  - [ ] SDP description available via a parameterized command (`executeCmd(MediaIOCommandParams&)` with key `GetSdp`)
- [ ] PTP lock optional: if `PtpClock` exists and is set via `setParams`, RTP timestamps are derived from PTP; otherwise from a local steady clock

**Implementation checklist:**
- [ ] Factory + registration
- [ ] Sender path tested against loopback using an `MediaIOTask_RtpVideo` reader
- [ ] Receiver path handles packet reordering and fragment reassembly correctly
- [ ] Payload format dispatch (RawVideo vs JPEG) based on config
- [ ] Config error paths (invalid address, unsupported payload)
- [ ] Back-pressure: writer blocks / `TryAgain` when UDP send buffer is full
- [ ] Doctest: loopback round-trip for uncompressed, loopback round-trip for MJPEG, packet loss recovery, SDP export

---

## MediaIOTask_RtpAudio — NEW

Bidirectional RTP audio transport (AES67-compatible L16 / L24). Replaces the deprecated `RtpAudioSinkNode` and adds the receive path.

**Files:**
- [ ] `include/promeki/mediaiotask_rtpaudio.h`
- [ ] `src/proav/mediaiotask_rtpaudio.cpp`
- [ ] `tests/mediaiotask_rtpaudio.cpp`

**Design:**

- [ ] Registered as `"RtpAudio"`; single-direction per instance (`canRead = true`, `canWrite = true`, `canReadWrite = false`)
- [ ] Config keys mirror `"RtpVideo"` for network/transport options plus:
  - [ ] `ConfigSampleRate` — 48000 for AES67, custom for other profiles
  - [ ] `ConfigChannels`
  - [ ] `ConfigRtpPayload` — `"L16"` or `"L24"` (maps to the existing `RtpPayloadL16` / `RtpPayloadL24`)
  - [ ] `ConfigPacketTimeUs` — AES67 standard is 1000µs; configurable 125/250/333/1000 µs
- [ ] Reader mode:
  - [ ] Extracts PCM samples from RTP packets, emits whole-frame-sized chunks as `Audio` / `Frame::Ptr`
  - [ ] Late/missing packet handling via silence fill (logged in `MediaIOStats`)
- [ ] Writer mode:
  - [ ] Packs incoming `Audio` into AES67-sized packets and transmits
  - [ ] Pacing: for AES67 the packet interval is tight (1 ms typical); userspace pacing via `RtpSession::sendPacketsPaced()` is the default; kernel pacing is available via `PacketTransport` when it lands

**Implementation checklist:**
- [ ] Loopback round-trip test (TPG-generated audio through RtpAudio writer → RtpAudio reader → verify samples)
- [ ] Multiple packet-time settings
- [ ] Channel count handling (mono through 8-channel)
- [ ] Late-packet silence fill

---

## MediaIOTask_ImageFile — JPEG Extension (NEW)

Add JPEG read/write to the existing `ImageFile` / `ImageFileIO` subsystem so the `"ImageFile"` MediaIO backend picks it up automatically.

**Files:**
- [ ] `include/promeki/imagefileio_jpeg.h`
- [ ] `src/proav/imagefileio_jpeg.cpp`
- [ ] `tests/imagefileio_jpeg.cpp`

**Design:**

The existing `JpegImageCodec` already encodes and decodes JPEG frames to/from a `Buffer` (for RGB, RGBA, YUYV, UYVY, planar 4:2:2 and 4:2:0, NV12 formats). The `ImageFile` backend just needs to wrap that codec with the file I/O plumbing, plus a direct pass-through path for the case where the input frame is *already* a compressed JPEG (the payload can be written to disk verbatim without re-encoding).

- [ ] Registered via `PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_JPEG)`; file extensions `jpg` / `jpeg`
- [ ] Magic-byte probe (`FF D8 FF`) for content detection — plugs into the existing `ImageFileIO::probe()` hook
- [ ] Supported read output formats: whichever `JpegImageCodec::decode()` supports (RGB8, RGBA8, YUYV, UYVY, planar 4:2:2, planar 4:2:0, NV12)
- [ ] Supported write input formats:
  - [ ] Any uncompressed pixel format that `JpegImageCodec::encode()` accepts → encode as JPEG on write
  - [ ] An already-compressed `EncodedDesc`-tagged frame carrying a JPEG payload → write the payload directly (no re-encode)
- [ ] Config keys (added to the existing ImageFile config):
  - [ ] `ConfigJpegQuality` — 1-100, default 85
  - [ ] `ConfigJpegSubsampling` — `"4:4:4"`, `"4:2:2"`, `"4:2:0"` (or auto from input pixel format)
  - [ ] `ConfigJpegProgressive` — bool
- [ ] Audio / metadata: reuse existing `Frame` support so EXIF / IPTC can round-trip later (initial: lossless pass-through of raw metadata block, full parse deferred)

**Implementation checklist:**
- [ ] JpegImageCodec pass-through encode path — hook up in the new `ImageFileIO_JPEG::save()`
- [ ] Pass-through (no re-encode) path when the incoming Frame is already compressed JPEG
- [ ] Probe + extension registration
- [ ] Round-trip test: RGB8 → save → load → RGB8 pixel tolerance
- [ ] Pass-through test: read a JPEG, write it, verify bytes match (or at least decode to identical pixels)
- [ ] Quality / subsampling config tests
- [ ] Integration via MediaIO: `MediaIO::createForFileRead("foo.jpg")` returns an ImageFile MediaIO that delegates to `ImageFileIO_JPEG`

---

## mediaplay — Generic Config-Driven CLI

**Files:** `utils/mediaplay/main.cpp`, `cli.{h,cpp}`, `stage.{h,cpp}`, `pipeline.{h,cpp}`, `sidecar.{h,cpp}` (split from the original monolithic `main.cpp`; wired via `utils/mediaplay/CMakeLists.txt`)

**Shipped — CLI rework (this session):**

Short flag names: `-i/--in`, `-o/--out`, `-c/--convert`, `--ic`, `--im`, `--oc`, `--om`, `--cc`, `--cm` (renamed from `--incfg`/`--outcfg`/etc; no backwards compat aliases).

Removed flags: `--fast`, `--no-display`, `--no-audio`, `--window-size`.  SDL is now configured via `-oc Paced:false`, `--oc Audio:false`, `--oc WindowSize:1920x1080`, `--oc WindowTitle:Foo`.

Metadata schema support: `FormatDesc::defaultMetadata`, `MediaIO::defaultMetadata`, `applyStageMetadata`, `--im`/`--om`/`--cm` flags, and metadata dumps in `--help`.

SDL is a pseudo-backend (`kStageSdl`) that exposes a full schema via `sdlDefaultConfig()` / `sdlDefaultMetadata()` / `sdlDescription()` so it appears alongside real backends in `--help`, `-i list`, `-o list`.

`-h` as short alias for `--help`.

`--duration` fix: rewrote `Pipeline::drain()` into `drainSource()` + `drainConverter()` wired to each stage's `frameReadySignal` with non-blocking writes and per-stage back-pressure counters.  No more blocking `writeFrame(true)`/`readFrame(true)` on the main thread; `--duration` is now honoured even when a Converter is in the path.

Also fixed sort-in-place bug in the help dumper: `List::sort()` returns a copy, does not sort in place.

**Earlier shipped (previous session):**

Grammar built on `MediaIOConfig`: type-aware `Key:Value` parsing against each backend's `defaultConfig()`; `list` sentinel for any `Enum` or `PixelDesc` key; `--help` autogenerates the full backend schema from the live registry.  Positional shortcuts.  `createForFileRead`/`createForFileWrite` now seed the live config with full `defaultConfig()` + `ConfigType` + `ConfigFilename`.

**Verified end-to-end:**
- Plain `mediaplay` with no flags: TPG default config (video+audio+timecode all enabled) → SDL, no configuration needed.
- `-i TPG --ic VideoSize:64x48 --ic VideoPixelDesc:RGB8_sRGB --ic FrameRate:30` → 5 DPX files at the correct pixel layout.
- Adding `-c --cc OutputPixelDesc:RGBA8_sRGB` rewrites the sink mediaDesc; file size delta proves the CSC ran.
- Audio TPG → `--cc OutputAudioDataType:PCMI_S16LE` → WAV sink produces 16-bit PCM stereo 48 kHz.
- `-i TPG -c --cc OutputPixelDesc:RGBA8_sRGB -o /tmp/out.dpx --duration 2` honours `--duration` (the drain bug was the previous blocker).
- Fan-out: `-o /tmp/a.dpx -o /tmp/b.dpx` produces two identical files from one source.
- `--ic VideoPattern:list` → the 12 registered TPG pattern names.
- `--ic VideoPixelDesc:list` → the 78 registered PixelDescs.
- `-i list` → the registered MediaIO backends with R/W capability flags.
- `--om Title:"My Recording" Originator:foo` → metadata stamped into WAV/MOV containers.

**Still to do** (larger-scope work, depends on new framework classes):
- [ ] `--pipeline <path>` JSON ingest / `--save-pipeline <path>` JSON export — blocked on the new `MediaPipelineConfig` data object (phase 4A, see `proav_pipeline.md`).
- [ ] Per-stage stats aggregation for `--verbose` via the future `MediaPipeline::stats()`.
- [ ] Integration tests covering known CLI invocations against golden data files.
- [ ] `docs/mediaplay.dox` covering the grammar with worked examples.

---

## Deprecated MediaNode-based Concrete Nodes

The following classes live in the codebase but are **deprecated**. They will be deleted in the final migration sweep (see `proav_pipeline.md`). No new development should target them; bug fixes are acceptable only when they unblock migration.

- `TestPatternNode` — replaced by `MediaIOTask_TPG` (already complete)
- `JpegEncoderNode` — replaced by `MediaIOTask_Converter` with JPEG codec config
- `FrameDemuxNode` — replaced by the pipeline's native fan-out / routing
- `TimecodeOverlayNode` — will become a `MediaIOTask_Converter` with text-burn config, or a dedicated text-overlay converter backend
- `RtpVideoSinkNode` / `RtpAudioSinkNode` — replaced by `MediaIOTask_RtpVideo` / `MediaIOTask_RtpAudio`
- `AudioSourceNode` / `AudioSinkNode` — replaced by `MediaIOTask_AudioFile` (already complete)
- `ImageSourceNode` / `ImageSinkNode` — replaced by `MediaIOTask_ImageFile` (already complete, JPEG extension pending)
- `AudioMixerNode` — will become a `MediaIOTask_Converter` (fan-in) once converter supports multi-input
- `AudioGainNode` — will become a simple converter backend
- `ColorModelConvertNode` — replaced by `MediaIOTask_Converter` with ColorModel config
- `FrameSyncNode` — audio/video sync via timecode; will become a specialised converter backend

---

## Known Issues / Open FIXMEs in Existing Backends

All tracked in `fixme.md`. Summary of the ones that belong to this document:
- QuickTime: little-endian float audio storage is lossy (promoted to s16); needs proper `lpcm` + `pcmC` extension atom
- QuickTime: `raw ` 24-bit RGB/BGR byte-order player disagreement
- QuickTime: compressed-audio pull-rate drift (single-packet-per-video-frame heuristic fails on variable-duration compressed audio)
- QuickTime: compressed-audio write path missing (blocks remux workflows)
- QuickTime: XMP parser only matches `bext:` prefix (blocked on core XML support)
- QuickTime: fragmented reader ignores `trex` default fallback (only handles `tfhd` overrides)
- QuickTime: `BufferPool` available but not wired into the hot path
