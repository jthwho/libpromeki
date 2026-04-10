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
- **SDLPlayerTask** — Write-only display sink with audio-led pacing, fast mode, main-thread `renderPending()` dispatch. Injected via `MediaIO::adoptTask()`. Transparent compressed video decode (JPEG, JPEG XS, any registered `ImageCodec`) — decode runs on the strand worker thread via `Image::convert` to `RGBA8_sRGB`, no Converter stage needed in front.

**JpegXsImageCodec** is complete — JPEG XS (ISO/IEC 21122) encode/decode using vendored SVT-JPEG-XS, supporting planar YUV 4:2:2/4:2:0 at 8/10/12-bit (Rec.709 limited range). CMake feature flag `PROMEKI_ENABLE_JPEGXS` (auto-detected on x86-64 with nasm/yasm). Config via `MediaConfig::JpegXsBpp` and `MediaConfig::JpegXsDecomposition`. Seven new `JPEG_XS_*` PixelDesc IDs. Packed RGB path and additional colour-space variants are tracked in `fixme.md`.

**Histogram** class added — lightweight sub-bucketed log2 histogram for hot-path instrumentation (latency/interval/size distributions). Used by `MediaIOTask_Rtp` for TX/RX timing stats.

All test coverage lives in `tests/mediaio.cpp`, `tests/quicktime.cpp`, `tests/mediaiotask_quicktime.cpp`, `tests/strand.cpp`, `tests/audiobuffer.cpp`, `tests/bufferpool.cpp`, `tests/histogram.cpp`, `tests/jpegxsimagecodec.cpp`, plus the per-backend format tests. See git history for the sprawling completed-work log — this document stays focused on what still needs to be built.

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

## MediaIOTask_Rtp — UNIFIED WRITER + READER

Unified RTP video + audio + metadata transceiver.  One MediaIO
instance carries one SDP session with up to three `m=` sections
(video / audio / data), each backed by its own `RtpSession`,
`RtpPayload`, and `UdpSocketTransport` so per-stream DSCP, SSRC,
and destination can be set independently.  This factoring mirrors
how SMPTE ST 2110 and AES67 deployments group streams and lets the
sink publish a single SDP file that a downstream receiver can
consume as one bundle.

Both Writer and Reader modes are now implemented.  The old devplan
split this into separate `MediaIOTask_RtpVideo` and
`MediaIOTask_RtpAudio` backends; that split has been merged into a
single unified task.

**Files (shipped):**
- `include/promeki/mediaiotask_rtp.h`
- `src/proav/mediaiotask_rtp.cpp`
- `tests/mediaiotask_rtp.cpp`

**Shipped in the writer-mode first pass:**

- Registered as `"Rtp"` with `canWrite = true` and `canRead = true` (`canReadWrite` is false).  `ReadWrite` mode is rejected at open time.
- Built on the completed `PacketTransport` / `UdpSocketTransport` stack: every stream owns its own transport so destinations, DSCPs, and bind ports stay independent.
- Supported payload classes (inferred from the media descriptor, no manual `RtpPayload` config key required):
  - Video MJPEG via `RtpPayloadJpeg` when the input `PixelDesc` is in the JPEG family.
  - Video RFC 4175 raw via `RtpPayloadRawVideo` for 8-bit interleaved uncompressed formats.  (Proper ST 2110-20 pgroup sizing for 10/12-bit YCbCr is deferred; the first pass supports RGB8/RGBA8/YUV422 8-bit.)
  - Audio L16 via `RtpPayloadL16` when the input is `PCMI_S16LE` or `PCMI_S16BE` (`S16LE` is byte-swapped into big-endian wire order inside the task).
  - Metadata JSON via `RtpPayloadJson` selected by `MediaConfig::DataRtpFormat = JsonMetadata`.
- Config keys (all in `mediaconfig.h`):
  - Media descriptor keys are reused from the existing shared catalog: `VideoSize`, `VideoPixelFormat`, `AudioRate`, `AudioChannels`, `FrameRate`.
  - Transport-global: `RtpLocalAddress` (SocketAddress), `RtpSessionName`, `RtpSessionOrigin`, `RtpPacingMode` (Enum: `None`/`Userspace`/`KernelFq`/`TxTime`), `RtpMulticastTTL`, `RtpMulticastInterface`, `RtpSaveSdpPath`.
  - Per-stream (Video/Audio/Data prefixed): `RtpDestination` (SocketAddress; empty = stream disabled), `RtpPayloadType`, `RtpClockRate`, `RtpSsrc` (uint32), `RtpDscp`, plus video `RtpTargetBitrate`, audio `RtpPacketTimeUs`, and data `RtpFormat` (Enum: `JsonMetadata` / `St2110_40` placeholder).
- Pacing modes:
  - `None` — burst all packets (loopback / LAN only).
  - `Userspace` — `RtpSession::sendPacketsPaced()` sleeps between packets, spread across one frame interval.
  - `KernelFq` — `RtpSession::setPacingRate()` maps to `SO_MAX_PACING_RATE` via the `fq` qdisc.  The target rate is drawn from `VideoRtpTargetBitrate` if set, or computed from the descriptor (`width × height × bpp × fps` for uncompressed, 200 Mbps fallback for compressed, `sample_rate × channels × bytes_per_sample × 8` for audio).
  - `TxTime` — reserved for ST 2110-21-grade per-packet pacing via `SCM_TXTIME`; currently falls back to `KernelFq`.
- SDP export via two paths:
  - `MediaConfig::RtpSaveSdpPath` — when non-empty, the generated SDP is written to that file at open time.  Verified end-to-end: `mediaplay -i TPG -c --cc OutputPixelDesc:JPEG_YUV8_422_Rec709 -o Rtp --oc VideoRtpDestination:127.0.0.1:5004 --oc RtpSaveSdpPath:/tmp/stream.sdp` produces a valid `v=0` / `m=video` / `a=rtpmap:26 JPEG/90000` SDP.
  - `executeCmd(MediaIOCommandParams&)` with `name == "GetSdp"` — returns the SDP text under `result["Sdp"]` for callers that want the live session description without a file round-trip.
- Stats: `FramesSent`, `PacketsSent`, `BytesSent`, `FramesDropped` via `MediaIOCommandStats`.
- RTP timestamps: `frame_count × (clock_rate / frame_rate)` from a local steady clock.  PTP-locked timestamps are deferred until `PtpClock` lands (see `network_avoverip.md`).
- Tests (`tests/mediaiotask_rtp.cpp`): registry, default config, ReadWrite mode rejection, no-active-streams failure, video loopback with RTP header verification, audio PCMI_S16LE loopback with SSRC verification, metadata JSON loopback, SDP file export, SDP via GetSdp params command, plus new reader-mode tests: video reader loopback (MJPEG), audio reader loopback (L16), data reader loopback (JSON), combined A/V reader with frame aggregation, SDP-driven reader auto-config, JPEG XS writer+reader round-trip (when PROMEKI_ENABLE_JPEGXS is on).
- `mediaplay` integration: `stage.cpp` parses `SocketAddress` config values via `SocketAddress::fromString()` so `--oc VideoRtpDestination:239.0.0.1:5004` works straight from the CLI.
- A convenience script `scripts/rtp-rx-ffplay.sh` launches `ffplay` on an existing SDP file or synthesizes one from command-line parameters for MJPEG / raw / L16 / L24 streams.

**Shipped in the reader-mode second pass:**

- Reader mode (`MediaIOTask_Rtp` as a Reader).  Each configured stream opens its own `UdpSocketTransport` bound to the port in `*RtpDestination`, joins multicast groups automatically, and runs an `RtpSession` receive thread (`startReceiving()`) that delivers packets to per-stream reassemblers (`onVideoPacket`, `onAudioPacket`, `onDataPacket`).  Completed frames land in a bounded thread-safe `Queue<Frame::Ptr>` that `executeCmd(Read)` drains with a configurable timeout.
- SDP-driven reader auto-config: `MediaConfig::RtpSdp` (accepts a filesystem path as String, or a pre-parsed `SdpSession` Variant).  The reader calls `SdpSession::fromFile` / applies the parsed SDP via `applySdp()` to populate per-stream destinations, payload types, clock rates, and geometry from `m=` / `a=rtpmap` / `a=fmtp` lines.  Explicit per-stream config keys override SDP-discovered values.
- Reader frame aggregation: video stream is the frame clock.  When a complete video frame is reassembled (marker bit), `emitVideoFrame` drains one frame's worth of audio from the `AudioBuffer` FIFO and merges the latest metadata snapshot, pushing a single combined `Frame` downstream.  Audio that arrives ahead of video accumulates in the FIFO; late audio is waited for up to `audioTimeoutMs`.
- Per-stream TX worker threads (`SendThread`): video, audio, and data each get their own send thread so video pacing sleeps don't block audio's AES67 cadence.  Work items arrive via a `Queue<TxWorkItem>` with a result channel for synchronous caller wait.
- Timing instrumentation via `Histogram`: per-stream TX frame-interval, TX send-duration, RX packet-interval, RX frame-interval, RX frame-assemble-time — all in microseconds.  Surfaced as pretty-printed strings in `MediaIOStats`.
- New reader stats: `StatsFramesReceived`, `StatsPacketsReceived`, `StatsBytesReceived`, plus histogram stats (`StatsTxVideoFrameIntervalUs`, `StatsTxVideoSendDurationUs`, `StatsRxVideoPacketIntervalUs`, `StatsRxVideoFrameIntervalUs`, `StatsRxVideoFrameAssembleUs`).
- Compressed video pacing for writer: VBR streams without explicit `VideoRtpTargetBitrate` are paced per-frame — the rate cap is recomputed from each frame's actual byte count and set via `setPacingRate()` before dispatch.
- New config keys: `RtpSdp`, `RtpJitterMs`, `RtpMaxReadQueueDepth`.
- `SdpSession` content probe for reader auto-detection of `.sdp` files.
- New descriptors: `AudioDesc::fromSdp()`, `ImageDesc::fromSdp()`, `MediaDesc::fromSdp()` — derive audio/image/media descriptors from SDP media descriptions.

**Deferred for a follow-up pass:**

- [ ] Mid-stream descriptor discovery: reader mode does not yet handle resolution / pixel format changes mid-playback (`cmd.mediaDescChanged`).
- [ ] Timestamp wrap handling in the reader reassembler (RTP timestamp wraps at 2^32).
- [ ] Proper ST 2110-20 pgroup sizing for 10/12-bit YCbCr in `RtpPayloadRawVideo`.  The current implementation uses a simple `bitsPerPixel` model that works for 8-bit interleaved formats only.
- [ ] L24 audio support via `RtpPayloadL24`.  The existing payload class handles 3-byte-packed big-endian samples, but `AudioDesc::PCMI_S24LE` stores samples in 4-byte int32 slots — the task needs a pack-and-swap step (or a Converter stage that lands in 3-byte packed form).
- [ ] SMPTE ST 2110-40 Ancillary Data payload class for the metadata stream.  The `MetadataRtpFormat::St2110_40` enum entry exists but the backend rejects it at configure time until RFC 8331 packet handling is implemented (ST 291 ANC packet parsing, DID/SDID/DBN handling, field/line placement, BCH ECC).
- [ ] `SO_TXTIME` per-packet deadlines wired through `RtpPacingMode::TxTime` and `PacketTransport::setTxTime()`.  The transport interface is ready; the sender just needs to compute per-packet deadlines from the frame rate.
- [ ] PTP-locked RTP timestamps once `PtpClock` lands (see `network_avoverip.md`).
- [ ] Back-pressure: writer should return `Error::TryAgain` when the underlying UDP send buffer is full instead of blocking.  Not a problem at current rates but will matter for ST 2110 uncompressed.
- [ ] `RtpPayloadJpegXs` slice packetization mode (K=1) and interlaced framing — currently only codestream mode (K=0) is implemented.

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

**Design additions (imgseq sidecar writing — shipped):**

`MediaIOTask_ImageFile` now writes an `.imgseq` JSON sidecar automatically when the writer closes a sequence.  Two new `MediaConfig` keys control the feature:

- `MediaConfig::SaveImgSeqPath` (String) — when non-empty, the backend writes an `.imgseq` JSON sidecar to this path on close.  A relative path is resolved against the image-sequence directory so the sidecar lands alongside the frames by default.
- `MediaConfig::SaveImgSeqPathMode` (Enum `ImgSeqPathMode`) — `Relative` (default) or `Absolute`; controls whether the `"dir"` field written into the sidecar is a relative path (from the sidecar's location to the image directory) or an absolute path.

The `ImgSeq` format gained a `"dir"` JSON field so the image directory can be expressed separately from the sidecar's own location.  `FilePath::relativeTo()` was added to support the relative-path computation.  The `--imgseq` / `--imgseq-file` CLI options and the `sidecar.{h,cpp}` files have been removed from `mediaplay`; the library handles sidecar writing entirely.

**Remaining work (future):**
- [ ] `MediaConfig::JpegQuality` / `MediaConfig::JpegSubsampling` / `MediaConfig::JpegProgressive` exposed as first-class `MediaIOTask_ImageFile` open-time config (today callers forward them through `ImageFile::save(config)` directly or via the Converter stage; the ImageFile backend itself does not advertise them in its `defaultConfig()`).
- [ ] EXIF / IPTC metadata parsing (initial: lossless pass-through of the raw JPEG bitstream, no tag-level round-trip).

---

## mediaplay — Generic Config-Driven CLI

**Files:** `utils/mediaplay/main.cpp`, `cli.{h,cpp}`, `stage.{h,cpp}`, `pipeline.{h,cpp}` (split from the original monolithic `main.cpp`; wired via `utils/mediaplay/CMakeLists.txt`)

**Shipped — CLI rework (this session):**

Short flag names: `-i/--in`, `-o/--out`, `-c/--convert`, `--ic`, `--im`, `--oc`, `--om`, `--cc`, `--cm` (renamed from `--incfg`/`--outcfg`/etc; no backwards compat aliases).

Removed flags: `--fast`, `--no-display`, `--no-audio`, `--window-size`.  SDL is now configured via `-oc Paced:false`, `--oc Audio:false`, `--oc WindowSize:1920x1080`, `--oc WindowTitle:Foo`.

Metadata schema support: `FormatDesc::defaultMetadata`, `MediaIO::defaultMetadata`, `applyStageMetadata`, `--im`/`--om`/`--cm` flags, and metadata dumps in `--help`.

SDL is a pseudo-backend (`kStageSdl`) that exposes a full schema via `sdlDefaultConfig()` / `sdlDefaultMetadata()` / `sdlDescription()` so it appears alongside real backends in `--help`, `-i list`, `-o list`.

`-h` as short alias for `--help`.

`--duration` fix: rewrote `Pipeline::drain()` into `drainSource()` + `drainConverter()` wired to each stage's `frameReadySignal` with non-blocking writes and per-stage back-pressure counters.  No more blocking `writeFrame(true)`/`readFrame(true)` on the main thread; `--duration` is now honoured even when a Converter is in the path.

Also fixed sort-in-place bug in the help dumper: `List::sort()` returns a copy, does not sort in place.

**Shipped — imgseq sidecar refactor (this session):**

Removed `--imgseq` / `--imgseq-file` CLI options and deleted `sidecar.{h,cpp}`.  Sidecar writing is now a library-level concern: set `MediaConfig::SaveImgSeqPath` on the ImageFile writer stage instead.  The `ImgSeq` format gained a `"dir"` JSON field; `FilePath::relativeTo()` was added.  `ImgSeqPathMode` enum (Relative/Absolute) controls whether the sidecar's `"dir"` field is written as a relative or absolute path.

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
- JPEG XS: packed RGB encode path not implemented (`classifyInput` rejects `RGB8_sRGB`)
- JPEG XS: additional matrix/range/colour-space variants (only Rec.709 limited-range wired up)
- JPEG XS: QuickTime/ISO-BMFF `jxsm` sample entry not implemented
- JPEG XS: RFC 9134 RTP slice packetization mode (K=1) + fmtp generation from SVT image config
