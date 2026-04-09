# MediaIO Backends

**Phase:** 4B
**Dependencies:** MediaIO framework (complete), `proav_pipeline.md` (for `MediaPipeline` usage)
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document tracks the `MediaIOTask` backends that plug into the `MediaIO` framework. Each backend is a subclass of `MediaIOTask`, registered with `PROMEKI_REGISTER_MEDIAIO`, and self-describes through a `FormatDesc`. All media capabilities go here — there is no "MediaNode" layer; the legacy one has been removed.

---

## Completed MediaIO Framework and Backends

The framework itself (`MediaIO` controller, `MediaIOTask` interface, `Strand` per-instance executor, `MediaIOCommand` dispatch, three `VariantDatabase` types for Config/Stats/Params, factory + registration, cached state, EOF latching, step/prefetch/seek modes, track selection, mid-stream descriptor updates, `cancelPending()`, error/signal plumbing) is complete and documented in `docs/mediaio.dox`. The following backends are complete and tested:

- **MediaIOTask_TPG** — Test pattern generator (video + audio + timecode). Static pattern caching, motion, text burn-in with FiraCode default, AvSync mode, NTSC audio cadence via `FrameRate::samplesPerFrame()`. `ConfigVideoSize` / `ConfigFrameRate` / full config key set.
- **MediaIOTask_ImageFile** — DPX, Cineon, TGA, SGI, RGB, PNM/PPM/PGM, PNG, JPEG, RawYUV. PNG backend uses libspng+zlib-ng with O_DIRECT save path. JPEG backend (libjpeg-turbo via `JpegImageCodec`) loads the bitstream compressed (zero decode), and saves pass-through bytes verbatim for already-JPEG inputs or routes uncompressed inputs through `Image::convert()`. Default frame rate via `ConfigFrameRate`.
- **MediaIOTask_AudioFile** — WAV, BWF, AIFF, OGG via libsndfile. Frame chunking by samples/frame, seek delegation, step control, audiodesc from file on read / config on write.
- **MediaIOTask_QuickTime** — Classic + fragmented reader and writer for `.mov` / `.mp4`. ProRes + H.264 + HEVC + PCM + AAC read; ProRes + PCM write. udta metadata including ©-atoms, XMP BWF extension, UMID, write-time defaults. BufferPool + AudioBuffer FIFO utilities in place.
- **SDLPlayerTask** — Write-only display sink with audio-led pacing, fast mode, main-thread `renderPending()` dispatch. Injected via `MediaIO::adoptTask()`.

All test coverage lives in `tests/mediaio.cpp`, `tests/quicktime.cpp`, `tests/mediaiotask_quicktime.cpp`, `tests/strand.cpp`, `tests/audiobuffer.cpp`, `tests/bufferpool.cpp`, plus the per-backend format tests. See git history for the sprawling completed-work log — this document stays focused on what still needs to be built.

---

## MediaIOTask_Converter

Generic ReadWrite MediaIO that accepts a frame on `writeFrame()`, transforms it, and emits the result on `readFrame()`. Covers every single-input / single-output transform (CSC, codec encode/decode, audio sample-format conversion).

**Files (complete):**
- `include/promeki/mediaiotask_converter.h`
- `src/proav/mediaiotask_converter.cpp`
- `tests/mediaiotask_converter.cpp`

**Initial version (complete):**

- Registered as `"Converter"` with `canReadWrite = true`; Reader/Writer-only modes are rejected.
- Config keys: `ConfigOutputPixelDesc` (PixelDesc), `ConfigJpegQuality` (int), `ConfigJpegSubsampling` (Enum `ChromaSubsampling`), `ConfigOutputAudioDataType` (Enum `AudioDataType`; `Invalid` = pass-through), `ConfigCapacity` (int, default 4).
- Video transforms all go through a single `Image::convert()` call.  The converter parses its config keys at open time and forwards them as a `MediaConfig` to `Image::convert()`, which dispatches uncompressed↔uncompressed CSC, JPEG encode, JPEG decode, and JPEG↔JPEG transcode internally.  Pass-through when no output pixel desc is set, or when source and target match.
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

## Codec abstraction follow-ups

Tracking items that came out of the `Image::convert` / `ImageCodec::configure` refactor.  None are blocking, but they should be picked up the next time the codec layer grows a second user.

- [ ] **Promote the `CodecHandle` RAII guard.** Today it's a one-off file-local class inside `src/proav/image.cpp` that owns the raw `ImageCodec *` returned by `ImageCodec::createCodec()`.  When a second caller appears (e.g. a `MediaIOTask_VideoCodec` that needs to live across many frames, or a unit-test helper), promote it into a real type — either:
  - Hand the codec registry an `ImageCodec::Ptr` factory and give `ImageCodec` `PROMEKI_SHARED_FINAL`, so callers stop juggling raw pointers entirely; or
  - Lift the existing wrapper into `include/promeki/codec.h` as `ImageCodec::Owner` and reuse it from every call site.
- [ ] **Generic codec config discovery.** `ImageCodec::configure(const MediaConfig &)` is opt-in: each codec subclass knows which `MediaConfig::*` keys it cares about, but there's no way for callers (e.g. a future `mediaplay --help`) to enumerate them.  Add a `defaultConfig()` / `configKeys()` accessor to `ImageCodec` so backends and CLIs can render the per-codec key schema without hard-coding it.

---

## MediaIOTask_RtpVideo — NEW

Bidirectional RTP video transport. Reader mode receives an RTP video stream into frames; Writer mode transmits frames as RTP packets. This backend is new — the old library had a send-only `RtpVideoSinkNode` that was removed with the rest of the MediaNode layer; this reintroduces the send path in the MediaIO framework and adds the receive path that was previously missing.

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

Bidirectional RTP audio transport (AES67-compatible L16 / L24). New backend; same story as `MediaIOTask_RtpVideo` — the old send-only `RtpAudioSinkNode` was removed with the rest of the MediaNode layer and this reintroduces the send path and adds receive.

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

## MediaIOTask_ImageFile — JPEG Extension (COMPLETE)

JPEG read/write is wired into the existing `ImageFile` / `ImageFileIO` subsystem so the `"ImageFile"` MediaIO backend handles `.jpg` / `.jpeg` / `.jfif` automatically.

**Files (complete):**
- `src/proav/imagefileio_jpeg.cpp`
- `tests/imagefileio_jpeg.cpp`
- Extension map and `FF D8 FF` magic-byte probe in `src/proav/mediaiotask_imagefile.cpp`

**Design (as shipped):**

- Registered via `PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_JPEG)` under `ImageFile::JPEG`.  Extensions `jpg` / `jpeg` / `jfif` map to it through `MediaIOTask_ImageFile::extMap`.
- Magic-byte probe (`FF D8 FF`) wired into the existing `probeImageDevice()` path.
- **Load keeps the bitstream compressed.**  `ImageFileIO_JPEG::load()` slurps the file, probes the JPEG header via libjpeg to pick the best-matching `JPEG_*` `PixelDesc` (`JPEG_RGB8_sRGB`, `JPEG_YUV8_422_Rec709`, or `JPEG_YUV8_420_Rec709` based on colour space + SOF sampling factors), then wraps the file `Buffer::Ptr` as plane 0 of a compressed `Image` via `Image::fromBuffer()`.  Zero decode on the load path; downstream consumers call `Image::convert()` (which now dispatches to `JpegImageCodec::decode()` automatically) when they need uncompressed pixels.
- **Save has three paths** — all routed through the `Image::convert()` dispatcher that now handles codec encode/decode:
  - Already-compressed JPEG input → write the payload bytes verbatim (pass-through; zero re-encode).
  - Uncompressed input that the codec can encode directly → `Image::convert()` picks the right JPEG sub-format and hands the compressed bytes to the file writer.
  - Uncompressed input outside the codec's `encodeSources` → `Image::convert()` inserts a preparatory CSC to land on one of the canonical encode sources before calling the codec.
- The JPEG sub-format selected on save is chosen from the input `PixelDesc` family (RGB → `JPEG_RGB8_sRGB`, RGBA → `JPEG_RGBA8_sRGB`, YUV 4:2:2 → `JPEG_YUV8_422_Rec709`, YUV 4:2:0 → `JPEG_YUV8_420_Rec709`) to avoid an extra CSC hop.
- **PixelDesc `encodeSources` cleanup** was part of this work: `JPEG_RGB8_sRGB`, `JPEG_YUV8_422_Rec709`, and `JPEG_YUV8_420_Rec709` no longer falsely advertise cross-family sources (RGB in YUV, RGBA in RGB).  The codec tags its output based on the input component order, so each JPEG `PixelDesc` now only lists the inputs whose natural family matches its own — `Image::convert()` handles any cross-family CSC before the codec runs.

**Remaining work (future):**
- [ ] `MediaConfig::JpegQuality` / `MediaConfig::JpegSubsampling` / `MediaConfig::JpegProgressive` exposed as first-class `MediaIOTask_ImageFile` open-time config (today callers forward them through `ImageFile::save(config)` directly or via the Converter stage; the ImageFile backend itself does not advertise them in its `defaultConfig()`).
- [ ] EXIF / IPTC metadata parsing (initial: lossless pass-through of the raw JPEG bitstream, no tag-level round-trip).

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

Grammar built on `MediaConfig`: type-aware `Key:Value` parsing against each backend's `defaultConfig()`; `list` sentinel for any `Enum` or `PixelDesc` key; `--help` autogenerates the full backend schema from the live registry.  Positional shortcuts.  `createForFileRead`/`createForFileWrite` now seed the live config with full `defaultConfig()` + `ConfigType` + `ConfigFilename`.

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

## Known Issues / Open FIXMEs in Existing Backends

All tracked in `fixme.md`. Summary of the ones that belong to this document:
- QuickTime: little-endian float audio storage is lossy (promoted to s16); needs proper `lpcm` + `pcmC` extension atom
- QuickTime: `raw ` 24-bit RGB/BGR byte-order player disagreement
- QuickTime: compressed-audio pull-rate drift (single-packet-per-video-frame heuristic fails on variable-duration compressed audio)
- QuickTime: compressed-audio write path missing (blocks remux workflows)
- QuickTime: XMP parser only matches `bext:` prefix (blocked on core XML support)
- QuickTime: fragmented reader ignores `trex` default fallback (only handles `tfhd` overrides)
- QuickTime: `BufferPool` available but not wired into the hot path
