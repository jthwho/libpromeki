# MediaIO Backends

**Phase:** 4B
**Dependencies:** MediaIO framework (complete), `proav_pipeline.md` (for `MediaPipeline` usage)
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

This document tracks the `MediaIOTask` backends that plug into the `MediaIO` framework. Each backend is a subclass of `MediaIOTask`, registered with `PROMEKI_REGISTER_MEDIAIO`, and self-describes through a `FormatDesc`. All media capabilities go here — there is no "MediaNode" layer; the legacy one has been removed.

---

## Completed MediaIO Framework and Backends

The framework itself (`MediaIO` controller, `MediaIOTask` interface, `Strand` per-instance executor, `MediaIOCommand` dispatch, three `VariantDatabase` types for Config/Stats/Params, factory + registration, cached state, EOF latching, step/prefetch/seek modes, track selection, mid-stream descriptor updates, `cancelPending()`, error/signal plumbing) is complete and documented in `docs/mediaio.dox`. The following backends are complete and tested:

- **MediaIOTask_TPG** — Test pattern generator (video + audio + timecode). Static pattern caching, motion, text burn-in with FiraCode default, AvSync mode (continuous LTC on the LTC channel + click marker on others), NTSC audio cadence via `FrameRate::samplesPerFrame()`. Image data encoder pass: stamps two 64-bit payloads (`(streamID << 32) | frameNumber` and `Timecode::toBcd64` BCD word) into the top of every generated frame via `ImageDataEncoder`. New config keys: `MediaConfig::StreamID`, `MediaConfig::TpgDataEncoderEnabled` (default true), `MediaConfig::TpgDataEncoderRepeatLines` (default 16). `ConfigVideoSize` / `ConfigFrameRate` / full config key set.
- **MediaIOTask_ImageFile** — DPX, Cineon, TGA, SGI, RGB, PNM/PPM/PGM, PNG, JPEG, JPEG XS, RawYUV. PNG backend uses libspng+zlib-ng with O_DIRECT save path. JPEG backend (libjpeg-turbo via `JpegImageCodec`) loads the bitstream compressed (zero decode), and saves pass-through bytes verbatim for already-JPEG inputs or routes uncompressed inputs through `Image::convert()`. JPEG XS backend (`ImageFileIO_JpegXS`) follows the same compressed-load / pass-through-save pattern with SVT header probing to determine the compressed `PixelFormat`. Default frame rate via `ConfigFrameRate`.
- **MediaIOTask_AudioFile** — WAV, BWF, AIFF, OGG via libsndfile. Frame chunking by samples/frame, seek delegation, step control, audiodesc from file on read / config on write.
- **MediaIOTask_QuickTime** — Classic + fragmented reader and writer for `.mov` / `.mp4`. ProRes + H.264 + HEVC + PCM + AAC read; ProRes + PCM write. udta metadata including ©-atoms, XMP BWF extension, UMID, write-time defaults. BufferPool + AudioBuffer FIFO utilities in place.
- **SDLPlayerTask** — Write-only display sink with audio-led pacing, fast mode, main-thread `renderPending()` dispatch. Injected via `MediaIO::adoptTask()`. Transparent compressed video decode (JPEG, JPEG XS, any registered `ImageCodec`) — decode runs on the strand worker thread via `Image::convert` to `RGBA8_sRGB`, no Converter stage needed in front.

**JpegXsImageCodec** is complete — JPEG XS (ISO/IEC 21122) encode/decode using vendored SVT-JPEG-XS, supporting planar YUV 4:2:2/4:2:0 at 8/10/12-bit (Rec.709 limited range) and planar RGB 4:4:4 at 8-bit (`RGB8_Planar_sRGB` via `COLOUR_FORMAT_PLANAR_YUV444_OR_RGB`). Interleaved `RGB8_sRGB` reaches the codec through the CSC fast path (`RGB8_sRGB` ↔ `RGB8_Planar_sRGB`) — the full encode/decode chain via `Image::convert()` is transparent to callers. CMake feature flag `PROMEKI_ENABLE_JPEGXS` (auto-detected on x86-64 with nasm/yasm). Config via `MediaConfig::JpegXsBpp` and `MediaConfig::JpegXsDecomposition`. Eight compressed `JPEG_XS_*` PixelFormat IDs plus `RGB8_Planar_sRGB` (new). Additional colour-space variants and the SVT packed-RGB validation bug workaround are tracked in `fixme.md`.

**Histogram** class added — lightweight sub-bucketed log2 histogram for hot-path instrumentation (latency/interval/size distributions). Used by `MediaIOTask_Rtp` for TX/RX timing stats.

**MediaIOTask_Inspector** is complete — MediaIO sink that decodes and validates a media stream frame by frame. Four checks (all default-configured): image data band decode (`ImageDataDecoder`, reports frame number + stream ID + picture TC), audio LTC decode (`LtcDecoder::decode(const Audio &, int channelIndex)`, reports TC + sync word position), A/V sync offset (instantaneous, in samples + fractional frames + natural-language direction), and continuity (frame number / stream ID / picture TC / LTC TC / sync offset jitter). Per-frame callback via `setEventCallback`. Thread-safe `InspectorSnapshot` accumulator. Multi-line periodic log with `"Frame N:"` prefix; discontinuities log immediately as warnings with `was/now/delta` detail. `InspectorEvent` / `InspectorSnapshot` / `InspectorDiscontinuity` data objects documented in `docs/dataobjects.dox`. QA user guide at `docs/inspector.dox`. Listed in the `docs/mediaio.dox` backend catalogue. Config keys: `InspectorDecodeImageData`, `InspectorDecodeLtc`, `InspectorCheckTcSync`, `InspectorCheckContinuity`, `InspectorDropFrames`, `InspectorImageDataRepeatLines`, `InspectorLtcChannel`, `InspectorSyncOffsetToleranceSamples`, `InspectorLogIntervalSec`. Tests in `tests/mediaiotask_inspector.cpp` (7 cases).

**ImageDataEncoder** is complete — fast VITC-style 64-bit payload stamper for raster images. Wire format: 4-bit sync + 64-bit payload + 8-bit CRC-8/AUTOSAR = 76-bit cell, MSB-first, one cell per pixel-aligned column; full format spec at `docs/imagedataencoder.dox`. Tests in `tests/imagedataencoder.cpp` (12 cases covering RGBA8, YUYV, planar 4:2:2, v210, NV12, rejection paths, and TPG end-to-end).

**ImageDataDecoder** is complete — companion decoder for `ImageDataEncoder`. Converts a band of scan lines to RGBA8 via CSC, vertically averages the R channel, Otsu-thresholds, sync-driven sub-pixel bit-width detection, verifies CRC-8/AUTOSAR. Supports multi-line averaging (default) and single-middle-line modes. Tests in `tests/imagedatadecoder.cpp` (13 cases including NV12, multi-band, corruption recovery, bit-width reporting).

**LtcDecoder** format-agnostic overload — `LtcDecoder::decode(const Audio &, int channelIndex = 0)` now accepts any audio sample format via `AudioDesc::samplesToFloat`, picks the named channel, quantises to int8 for libvtc. Reusable scratch buffers as members (allocation-free steady state). New tests: `LtcDecoder_DecodeAudio_Float32Stereo`, `LtcDecoder_DecodeAudio_RejectsMismatchedSampleRate`, `LtcDecoder_DecodeAudio_RejectsBadChannelIndex`.

**CRC** utility template is complete — `CRC<T>` Rocksoft parameter model, table-driven, `uint8_t`/`uint16_t`/`uint32_t`/`uint64_t`; presets `Crc8Smbus`, `Crc8Autosar`, `Crc8Bluetooth`, `Crc16CcittFalse`, `Crc16Kermit`, `Crc32IsoHdlc`, `Crc32Bzip2` with named factories; validated against published catalogue check values. Tests in `tests/crc.cpp` (12 cases).

**CSC planar plane size fix** — `planarPlaneSize` in `src/core/pixelmemlayout.cpp` now uses ceiling division so sub-sampled chroma planes always have at least one row even when luma height is 1 or odd. Previously floor division produced 0 chroma rows for height=1 4:2:0, letting the CSC pipeline write past the end of the empty buffer. Regression tests added to `tests/pixelmemlayout.cpp`.

**Timecode BCD64 + mode handling** — `Timecode::toBcd64(TimecodePackFormat)` / `Timecode::fromBcd64(uint64_t, TimecodePackFormat, const Mode &)` added. New `TimecodePackFormat` enum in `enums.h` (`Vitc` / `Ltc`). `toString()` now returns `"--:--:--:--"` for invalid timecodes and plain digits for format-less timecodes (never fails); `fromString("")` and `fromString("--:--:--:--")` round-trip to a default-constructed Timecode with `Error::Ok`. Tests in `tests/timecode.cpp` (expanded, 29 new BCD64 / sentinel cases).

**Phase 4b additions (this session):**

**`EnumList`** — new `include/promeki/enumlist.h` + `src/core/enumlist.cpp`.  Ordered, duplicate-preserving list of `Enum` values sharing one runtime-chosen element type.  Full Variant integration (`TypeEnumList`), DataStream round-trip, `toString()` / `fromString()` comma-separated serialization, `uniqueSorted()` helper.  22 unit-test cases in `tests/enumlist.cpp`.

**`InterlaceMode` TypedEnum** — added to `enums.h` (Progressive / Interlaced / InterlacedEvenFirst / InterlacedOddFirst / Unknown).  `ImageDesc` drops its old `bool interlaced` field and adopts `InterlaceMode`.  DataStream serialization updated.  Tests in `tests/imagedesc.cpp` and `tests/datastream.cpp`.

**`AudioTestPattern` rewrite** — `setChannelModes()` now accepts an `EnumList<AudioPattern>` (one entry per channel).  Seven new patterns beyond the original Tone/Silence/LTC/AvSync:
- `SrcProbe` — 997 Hz reference sine (prime-relative, reveals SRC artifacts).
- `ChannelId` — channel-unique sine at `base + ch * step` Hz; configurable via `AudioChannelIdBaseFreq` / `AudioChannelIdStepFreq`.
- `WhiteNoise` — Gaussian white noise, cached pre-generated buffer + crossfade seam, fixed PRNG seed for reproducibility, per-channel offset for decorrelation, DC-removal pass.
- `PinkNoise` — Kellet IIR-filtered pink, same caching/seam/DC-removal design as white.
- `Chirp` — log-sweep from `chirpStartFreq` to `chirpEndFreq` over `chirpDurationSec`; incremental phase accumulator so the waveform is sample-exact continuous across both chunk boundaries and period wraps.
- `DualTone` — two simultaneous sines (SMPTE IMD-1 defaults: 60 Hz + 7 kHz, 4:1 ratio); per-tone phase accumulators persist across `create()` calls.
- `PcmMarker` — framing marker embedded in the sample domain: 16-sample alternating preamble, 8-sample start marker, 64-bit MSB-first payload (BCD64 timecode or monotonic counter), parity bit.

New static constants: `kSrcProbeFrequencyHz`, `kPcmMarkerPreambleSamples`, `kPcmMarkerStartSamples`, `kPcmMarkerPayloadBits`.  New static helper: `channelIdFrequency(ch, base, step)`.  All patterns covered in `tests/audiotestpattern.cpp` (19 test cases, including regression tests for chirp unit bug, noise cursor bug, noise seam crossfade, and DualTone phase-reset click).

**`MediaIO` unknown-config-key detection** — `MediaIO::unknownConfigKeys()` returns a `StringList` of keys present in a `MediaConfig` but not declared by the backend's `defaultConfig()` or the global common keys.  `MediaIO::validateConfigKeys()` applies a `ValidationMode::Lenient` (warn + return Ok) or `Strict` (warn + return `Error::InvalidArgument`) policy.  Tests: `MediaIO_UnknownConfigKeys_*` and `MediaIO_ValidateConfigKeys_*` in `tests/mediaio.cpp`.

**`MediaIO` Input/Output semantics clarified** — terminology corrected so `MediaIO::Output` means the backend *provides* frames to the caller (source) and `MediaIO::Input` means the backend *accepts* frames from the caller (sink).  All in-tree callers updated; the `docs/mediaio.dox` open-direction table updated accordingly.

**Inspector new-pattern coverage** — two new test cases in `tests/mediaiotask_inspector.cpp`: `Inspector pipeline carries a SrcProbe channel unharmed` and `Inspector pipeline carries a ChannelId channel map unharmed`, exercising the full TPG→Inspector pipeline with the new pattern types.

**Phase 4c additions:**

**`MediaIOTask_ImageFile` sidecar audio** — automatic Broadcast WAV sidecar alongside image sequences.  On write: when audio is present in the pending `MediaDesc`, the backend creates a `<prefix>.wav` alongside the image files (e.g. `shot_####.dpx` → `shot.wav`), writes frame-aligned audio with silence padding on frames that carry no audio, and records the sidecar filename in the `.imgseq` JSON under `"audioFile"`.  On read: the backend probes for a sidecar `.wav` (resolution priority: `.imgseq` `"audioFile"` field → `SidecarAudioPath` config override → auto-derived name), opens it, and delivers frame-aligned audio chunks via `FrameRate::samplesPerFrame()`.  Seeking syncs both the image position and the audio file position via `AudioFile::seekToSample()`.  Three new `MediaConfig` keys: `SidecarAudioEnabled` (bool, default true — suppress sidecar on both read and write), `SidecarAudioPath` (String — override auto-derived path), `AudioSource` (Enum `AudioSourceHint` — `Sidecar`/`Embedded`, selects preferred source when both are available, falls back to the other).  `AudioSourceHint` TypedEnum added to `enums.h`.

**Auto-sidecar `.imgseq`** — `SaveImgSeqEnabled` (bool, default true) replaces the old "non-empty path = enable" pattern.  When true and `SaveImgSeqPath` is empty the sidecar filename is auto-derived from the sequence pattern prefix (e.g. `shot_####.dpx` → `shot.imgseq`).  Mask-based readers auto-discover the conventionally-named sidecar when `SaveImgSeqEnabled` is true.  `ImgSeq` gains an `"audioFile"` JSON field.

**Sequence writer output directory auto-creation** — if the target directory does not exist the writer calls `Dir::mkpath()` before attempting to write the first frame.

**`MediaIOTask_Inspector` improvements** — log lines simplified (removed `"Frame N:"` prefix from periodic report lines — the prefix was redundant with the report header and cluttered grep output).  `executeCmd(Close)` now emits a structured final-report block (total frames, per-check decode rates and pass/fail counts, A/V sync last offset, continuity discontinuity count).

All test coverage lives in `tests/mediaio.cpp`, `tests/quicktime.cpp`, `tests/mediaiotask_quicktime.cpp`, `tests/strand.cpp`, `tests/audiobuffer.cpp`, `tests/bufferpool.cpp`, `tests/histogram.cpp`, `tests/jpegxsimagecodec.cpp`, `tests/imagefileio_jpegxs.cpp`, `tests/crc.cpp`, `tests/imagedataencoder.cpp`, `tests/imagedatadecoder.cpp`, `tests/mediaiotask_inspector.cpp`, `tests/enumlist.cpp`, `tests/audiotestpattern.cpp`, plus the per-backend format tests. See git history for the sprawling completed-work log — this document stays focused on what still needs to be built.

**Phase 4e additions:**

**Four new purpose-built `MediaIOTask` backends** — each is a focused `InputAndOutput`-only processing stage with a FIFO output queue, no-drop write semantics, one-shot capacity warning, and a `pendingOutput()` override.  All four are registered via `PROMEKI_REGISTER_MEDIAIO`, carry a `defaultConfig()` lambda, and have comprehensive doctest unit-test suites.

- **`MediaIOTask_CSC`** (`include/promeki/mediaiotask_csc.h`, `src/proav/mediaiotask_csc.cpp`) — uncompressed pixel-format conversion via `Image::convert()`.  Config: `OutputPixelFormat` (pass-through when invalid or when source == target), `Capacity` (default 4).  Compressed sources/targets are rejected with `Error::NotSupported`.  Stats: `FramesConverted`, `QueueDepth`, `QueueCapacity`.  Output `MediaDesc` reflects the target `PixelFormat` at open time.  Tests: 11 cases in `tests/mediaiotask_csc.cpp`.

- **`MediaIOTask_SRC`** (`include/promeki/mediaiotask_src.h`, `src/proav/mediaiotask_src.cpp`) — audio sample-format conversion via `Audio::convertTo()`.  Config: `OutputAudioDataType` (pass-through when `Invalid`), `Capacity` (default 4).  Unknown audio data type string rejects at `open()`.  Video images pass through unchanged.  Stats: `FramesConverted`, `QueueDepth`, `QueueCapacity`.  Tests: 11 cases in `tests/mediaiotask_src.cpp`.

- **`MediaIOTask_Burn`** (`include/promeki/mediaiotask_burn.h`, `src/proav/mediaiotask_burn.cpp`) — text burn-in overlay via `VideoTestPattern::applyBurn()`.  Config: `VideoBurnEnabled` (default true), `VideoBurnFontPath`, `VideoBurnFontSize`, `VideoBurnText` (default `"{Timecode:smpte}"`), `VideoBurnPosition`, `VideoBurnTextColor`, `VideoBurnBgColor`, `VideoBurnDrawBg`, `Capacity` (default 4).  Non-paintable pixel formats pass through with a one-shot warning.  Stats: `FramesBurned`, `QueueDepth`, `QueueCapacity`.  Tests: 9 cases in `tests/mediaiotask_burn.cpp`.

- **`MediaIOTask_FrameSync`** (`include/promeki/mediaiotask_framesync.h`, `src/proav/mediaiotask_framesync.cpp`) — wraps `FrameSync` as a `MediaIO` backend.  Write side pushes source frames into `FrameSync`; read side calls `FrameSync::pullFrame()`.  Default clock is `SyntheticClock` (non-blocking, suitable for offline / file pipelines).  `setClock(Clock *)` substitutes an external clock for real-time playback; `frameSync()` exposes the underlying `FrameSync` for fine-tuning before `open()`.  Config: `OutputFrameRate` (invalid = inherit source), `OutputAudioRate` (0 = inherit), `OutputAudioChannels` (0 = inherit), `OutputAudioDataType` (Invalid = inherit), `InputQueueCapacity` (default 8).  `open()` fails when `pendingMediaDesc` has no valid frame rate.  Stats: `FramesPushed`, `FramesPulled`, plus `FramesRepeated` / `FramesDropped` from `FrameSync`.  Tests: 13 cases in `tests/mediaiotask_framesync.cpp`.

**New `MediaConfig` keys** (all in `mediaconfig.h`) — `OutputFrameRate` (FrameRate), `OutputAudioRate` (float), `OutputAudioChannels` (S32), `InputQueueCapacity` (S32) — added for the FrameSync backend.

**`VideoCodec::fromPixelFormat(const PixelFormat &)`** — new static method that scans the VideoCodec registry for the codec whose `compressedPixelFormats` list contains the given `PixelFormat`.  Returns an invalid codec if none claims it.  Used by the auto-detect path below.

**`MediaIOTask_VideoDecoder` auto-detect** — `MediaConfig::VideoCodec` is now optional.  When omitted, `open()` succeeds immediately and defers codec creation until the first `writeFrame()` call, where it inspects the incoming `MediaPacket::pixelFormat` and resolves the codec via `VideoCodec::fromPixelFormat()`.  The `executeCmd(MediaIOCommandOpen)` path resets all internal state regardless.  Existing explicit-codec behaviour is unchanged.

**`Frame::configUpdate` live reconfiguration** — `Frame` gained a `MediaConfig _configUpdate` member with `configUpdate()` accessors and `setConfigUpdate(MediaConfig)`.  `MediaIO::writeFrame()` now checks the frame's config update before executing the write command; if non-empty it calls `_task->configChanged(delta)`.  `MediaIOTask::configChanged(const MediaConfig &)` is a new virtual with a no-op default.  `MediaIOTask_VideoEncoder` and `MediaIOTask_VideoDecoder` both override it: each stores the running `_config`, merges the delta via `VariantDatabase::merge()`, and forwards the updated config to the underlying encoder/decoder via `configure()`.

---

## MediaIOTask_Converter

Generic ReadWrite MediaIO that accepts a frame on `writeFrame()`, transforms it, and emits the result on `readFrame()`. Covers every single-input / single-output transform (CSC, codec encode/decode, audio sample-format conversion).

**Files (complete):**
- `include/promeki/mediaiotask_converter.h`
- `src/proav/mediaiotask_converter.cpp`
- `tests/mediaiotask_converter.cpp`

**Initial version (complete):**

- Registered as `"Converter"` with `canInputAndOutput = true`; Input/Output-only modes are rejected.
- Config keys: `ConfigOutputPixelFormat` (PixelFormat), `ConfigJpegQuality` (int), `ConfigJpegSubsampling` (Enum `ChromaSubsampling`), `ConfigOutputAudioDataType` (Enum `AudioDataType`; `Invalid` = pass-through), `ConfigCapacity` (int, default 4).
- Video transforms all go through a single `Image::convert()` call.  The converter parses its config keys at open time and forwards them as a `MediaConfig` to `Image::convert()`, which dispatches uncompressed↔uncompressed CSC, JPEG encode, JPEG decode, and JPEG↔JPEG transcode internally.  Pass-through when no output pixel desc is set, or when source and target match.
- Audio transform: `Audio::convertTo()` whenever `ConfigOutputAudioDataType` is set and differs from the input data type; otherwise pass-through.
- Output `MediaDesc` is computed from `pendingMediaDesc` at open so downstream consumers see the post-conversion descriptor before the first frame.
- Internal FIFO with configurable capacity. `executeCmd(Read)` returns `Error::TryAgain` when empty. Queue is drained on `close()`.
- Stats: `FramesConverted`, `BytesIn`, `BytesOut`, plus standard `QueueDepth` / `QueueCapacity`.
- Stateless (1 input → 1 output) for all current transforms.

**Phase 4d additions:**

- **No-drop write semantics:** `executeCmd(Write)` no longer returns `Error::TryAgain` at capacity. Frames are always accepted; when the output FIFO exceeds the configured capacity a one-shot warning is logged (`_capacityWarned` flag suppresses repeats). Back-pressure is the caller's responsibility.
- **Write pipeline depth API:** `MediaIOCommandOpen::defaultWriteDepth` (int) added so backends can advertise their preferred write pipeline depth. `MediaIO::writeDepth()` returns the value captured at `open()`; `MediaIO::writesAccepted()` computes `writeDepth() - pendingWrites() - task->pendingOutput()`, clamped to zero. `MediaIOTask::pendingOutput()` virtual added (default 0); `MediaIOTask_Converter` overrides it to return its output FIFO depth.
- **Tests:** `WriteBeyondCapacity` renamed from `WriteBackPressure` and updated to verify all frames are readable. New `WritesAccepted` test exercises the full `writeDepth / writesAccepted / pendingOutput` contract.

**Remaining work (future):**
- [ ] `ConfigOutputColorModel` / `ConfigOutputSampleRate` as first-class knobs (today the ColorModel rides inside the target `PixelFormat`, sample-rate conversion is deferred to a future audio resampler).
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

- Registered as `"Rtp"` with `canInput = true` and `canOutput = true` (`canInputAndOutput` is false).  `InputAndOutput` mode is rejected at open time.
- Built on the completed `PacketTransport` / `UdpSocketTransport` stack: every stream owns its own transport so destinations, DSCPs, and bind ports stay independent.
- Supported payload classes (inferred from the media descriptor, no manual `RtpPayload` config key required):
  - Video MJPEG via `RtpPayloadJpeg` when the input `PixelFormat` is in the JPEG family.
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
  - `MediaConfig::RtpSaveSdpPath` — when non-empty, the generated SDP is written to that file at open time.  Verified end-to-end: `mediaplay -i TPG -c --cc OutputPixelFormat:JPEG_YUV8_422_Rec709 -o Rtp --oc VideoRtpDestination:127.0.0.1:5004 --oc RtpSaveSdpPath:/tmp/stream.sdp` produces a valid `v=0` / `m=video` / `a=rtpmap:26 JPEG/90000` SDP.
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
- **Load keeps the bitstream compressed.**  `ImageFileIO_JPEG::load()` slurps the file, probes the JPEG header via libjpeg to pick the best-matching `JPEG_*` `PixelFormat` (`JPEG_RGB8_sRGB`, `JPEG_YUV8_422_Rec709`, or `JPEG_YUV8_420_Rec709` based on colour space + SOF sampling factors), then wraps the file `Buffer::Ptr` as plane 0 of a compressed `Image` via `Image::fromBuffer()`.  Zero decode on the load path; downstream consumers call `Image::convert()` (which now dispatches to `JpegImageCodec::decode()` automatically) when they need uncompressed pixels.
- **Save has three paths** — all routed through the `Image::convert()` dispatcher that now handles codec encode/decode:
  - Already-compressed JPEG input → write the payload bytes verbatim (pass-through; zero re-encode).
  - Uncompressed input that the codec can encode directly → `Image::convert()` picks the right JPEG sub-format and hands the compressed bytes to the file writer.
  - Uncompressed input outside the codec's `encodeSources` → `Image::convert()` inserts a preparatory CSC to land on one of the canonical encode sources before calling the codec.
- The JPEG sub-format selected on save is chosen from the input `PixelFormat` family (RGB → `JPEG_RGB8_sRGB`, RGBA → `JPEG_RGBA8_sRGB`, YUV 4:2:2 → `JPEG_YUV8_422_Rec709`, YUV 4:2:0 → `JPEG_YUV8_420_Rec709`) to avoid an extra CSC hop.
- **PixelFormat `encodeSources` cleanup** was part of this work: `JPEG_RGB8_sRGB`, `JPEG_YUV8_422_Rec709`, and `JPEG_YUV8_420_Rec709` no longer falsely advertise cross-family sources (RGB in YUV, RGBA in RGB).  The codec tags its output based on the input component order, so each JPEG `PixelFormat` now only lists the inputs whose natural family matches its own — `Image::convert()` handles any cross-family CSC before the codec runs.

**Design additions (imgseq sidecar writing — shipped):**

`MediaIOTask_ImageFile` now writes an `.imgseq` JSON sidecar automatically when the writer closes a sequence.  Two new `MediaConfig` keys control the feature:

- `MediaConfig::SaveImgSeqPath` (String) — when non-empty, the backend writes an `.imgseq` JSON sidecar to this path on close.  A relative path is resolved against the image-sequence directory so the sidecar lands alongside the frames by default.
- `MediaConfig::SaveImgSeqPathMode` (Enum `ImgSeqPathMode`) — `Relative` (default) or `Absolute`; controls whether the `"dir"` field written into the sidecar is a relative path (from the sidecar's location to the image directory) or an absolute path.

The `ImgSeq` format gained a `"dir"` JSON field so the image directory can be expressed separately from the sidecar's own location.  `FilePath::relativeTo()` was added to support the relative-path computation.  The `--imgseq` / `--imgseq-file` CLI options and the `sidecar.{h,cpp}` files have been removed from `mediaplay`; the library handles sidecar writing entirely.

**Remaining work (future):**
- [ ] `MediaConfig::JpegQuality` / `MediaConfig::JpegSubsampling` / `MediaConfig::JpegProgressive` exposed as first-class `MediaIOTask_ImageFile` open-time config (today callers forward them through `ImageFile::save(config)` directly or via the Converter stage; the ImageFile backend itself does not advertise them in its `defaultConfig()`).
- [ ] EXIF / IPTC metadata parsing (initial: lossless pass-through of the raw JPEG bitstream, no tag-level round-trip).

---

## MediaIOTask_ImageFile — JPEG XS Extension (COMPLETE)

JPEG XS read/write is wired into the existing `ImageFile` / `ImageFileIO` subsystem so the `"ImageFile"` MediaIO backend handles `.jxs` files automatically.

**Files (complete):**
- `src/proav/imagefileio_jpegxs.cpp`
- `tests/imagefileio_jpegxs.cpp`
- Extension map (`.jxs`) and `FF 10` SOC magic-byte probe in `src/proav/mediaiotask_imagefile.cpp`

**Design (as shipped):**

- Registered via `PROMEKI_REGISTER_IMAGEFILEIO(ImageFileIO_JpegXS)` under `ImageFile::JpegXS`.  Extension `.jxs` maps to it through `MediaIOTask_ImageFile::extMap`.
- Magic-byte probe (`FF 10` — the JPEG XS SOC marker) wired into `probeImageDevice()`.
- **Load keeps the bitstream compressed.**  `ImageFileIO_JpegXS::load()` reads the file, probes the JPEG XS codestream header by calling `svt_jpeg_xs_decoder_init()` briefly to obtain `(width, height, bit_depth, colour_format)`, selects the appropriate compressed `PixelFormat` (`JPEG_XS_YUV8/10/12_422_Rec709`, `JPEG_XS_YUV8/10/12_420_Rec709`, or `JPEG_XS_RGB8_sRGB`), then wraps the file buffer as plane 0 of a compressed `Image` via `Image::fromBuffer()`.  Zero decode on the load path.
- **Save has three paths** — all routed through `Image::convert()`:
  - Already-compressed JPEG XS input → write payload bytes verbatim (pass-through; zero re-encode).
  - Compressed non-JPEG-XS input → rejected with `Error::NotSupported`.
  - Uncompressed input → target subtype chosen from input `PixelFormat` family to minimise CSC hops: planar YUV 4:2:2/4:2:0 map to their matching `JPEG_XS_YUV*` ID; `RGB8_sRGB` / `RGB8_Planar_sRGB` map to `JPEG_XS_RGB8_sRGB`; RGBA and other sRGB-family formats fall back to `JPEG_XS_RGB8_sRGB`; everything else falls back to `JPEG_XS_YUV8_422_Rec709`.  `Image::convert()` inserts any required CSC transparently.
- `MediaConfig::JpegXsBpp` and `MediaConfig::JpegXsDecomposition` flow through `Image::convert()` into `JpegXsImageCodec::configure()` unchanged.
- **RGB path** — `RGB8_sRGB` → `RGB8_Planar_sRGB` (CSC fast path: `FastPathRGB8toPlanarRGB8`) → `JpegXsImageCodec::encode()` (via `COLOUR_FORMAT_PLANAR_YUV444_OR_RGB`).  Decode reverses: codec → `RGB8_Planar_sRGB` → `RGB8_sRGB` (CSC fast path: `FastPathPlanarRGB8toRGB8`).  The CSC workaround is required because the SVT `send_picture` validation rejects packed-format buffers (bug tracked in `fixme.md`).

**New PixelMemLayout / PixelFormat added:**
- `PixelMemLayout::P_444_3x8` — 3-plane 8-bit 4:4:4, no subsampling.
- `PixelFormat::RGB8_Planar_sRGB` — 8-bit planar R/G/B (one byte per component per pixel, 3 equal-sized planes), sRGB full range.  Codec-internal intermediate; CSC fast paths to/from `RGB8_sRGB` and `RGBA8_sRGB`.

**Remaining work (future):**
- [ ] 10/12-bit planar RGB (`P_444_3x10_LE`, `RGB10_Planar_LE_sRGB`) once SVT packed validation is fixed or real 10/12-bit RGB workflows appear.
- [ ] `ImageFile::JpegXS` exposed in `MediaIOTask_ImageFile::defaultConfig()` so callers can probe JPEG XS config keys without knowing the codec.

---

## mediaplay — Generic Config-Driven CLI

**Files:** `utils/mediaplay/main.cpp`, `cli.{h,cpp}`, `stage.{h,cpp}`, `pipeline.{h,cpp}` (split from the original monolithic `main.cpp`; wired via `utils/mediaplay/CMakeLists.txt`)

**Shipped — CLI flag rename (this session):**

Source/destination flags renamed from `-i`/`-o` to `-s`/`-d` (and `--ic`/`--oc`/`--im`/`--om` to `--sc`/`--dc`/`--sm`/`--dm`) to align naming with the library's own `MediaIO::Output` (source) / `MediaIO::Input` (sink) semantics.  No backwards-compat aliases.  Help text updated throughout.

Three-column config schema layout in `--help` and `-s list` / `-d list` output: each config key rendered as `Key | Type | Description` in aligned columns, separated by dashed borders.

**Shipped — CLI rework (prior session):**

Short flag names: `-s/--src`, `-d/--dst`, `-c/--convert`, `--sc`, `--sm`, `--dc`, `--dm`, `--cc`, `--cm` (renamed across two sessions from original `--incfg`/`--outcfg`; no backwards compat aliases).

Removed flags: `--fast`, `--no-display`, `--no-audio`, `--window-size`.  SDL is now configured via `--dc Paced:false`, `--dc Audio:false`, `--dc WindowSize:1920x1080`, `--dc WindowTitle:Foo`.

Metadata schema support: `FormatDesc::defaultMetadata`, `MediaIO::defaultMetadata`, `applyStageMetadata`, `--sm`/`--dm`/`--cm` flags, and metadata dumps in `--help`.

SDL is a pseudo-backend (`kStageSdl`) that exposes a full schema via `sdlDefaultConfig()` / `sdlDefaultMetadata()` / `sdlDescription()` so it appears alongside real backends in `--help`, `-s list`, `-d list`.

`-h` as short alias for `--help`.

`--duration` fix: rewrote `Pipeline::drain()` into `drainSource()` + `drainConverter()` wired to each stage's `frameReadySignal` with non-blocking writes and per-stage back-pressure counters.  No more blocking `writeFrame(true)`/`readFrame(true)` on the main thread; `--duration` is now honoured even when a Converter is in the path.

Also fixed sort-in-place bug in the help dumper: `List::sort()` returns a copy, does not sort in place.

**Shipped — imgseq sidecar refactor (prior session):**

Removed `--imgseq` / `--imgseq-file` CLI options and deleted `sidecar.{h,cpp}`.  Sidecar writing is now a library-level concern: set `MediaConfig::SaveImgSeqPath` on the ImageFile writer stage instead.  The `ImgSeq` format gained a `"dir"` JSON field; `FilePath::relativeTo()` was added.  `ImgSeqPathMode` enum (Relative/Absolute) controls whether the sidecar's `"dir"` field is written as a relative or absolute path.

**Earlier shipped (previous session):**

Grammar built on `MediaConfig`: type-aware `Key:Value` parsing against each backend's `defaultConfig()`; `list` sentinel for any `Enum` or `PixelFormat` key; `--help` autogenerates the full backend schema from the live registry.  Positional shortcuts.  `createForFileRead`/`createForFileWrite` now seed the live config with full `defaultConfig()` + `ConfigType` + `ConfigFilename`.

**Verified end-to-end (current flags):**
- Plain `mediaplay` with no flags: TPG default config (video+audio+timecode all enabled) → SDL, no configuration needed.
- `-s TPG --sc VideoSize:64x48 --sc VideoPixelFormat:RGB8_sRGB --sc FrameRate:30` → 5 DPX files at the correct pixel layout.
- Adding `-c --cc OutputPixelFormat:RGBA8_sRGB` rewrites the sink mediaDesc; file size delta proves the CSC ran.
- Audio TPG → `--cc OutputAudioDataType:PCMI_S16LE` → WAV sink produces 16-bit PCM stereo 48 kHz.
- `-s TPG -c --cc OutputPixelFormat:RGBA8_sRGB -d /tmp/out.dpx --duration 2` honours `--duration`.
- Fan-out: `-d /tmp/a.dpx -d /tmp/b.dpx` produces two identical files from one source.
- `--sc VideoPattern:list` → the 12 registered TPG pattern names.
- `--sc VideoPixelFormat:list` → the 78 registered PixelFormats.
- `-s list` → the registered MediaIO backends with R/W capability flags.
- `--dm Title:"My Recording" Originator:foo` → metadata stamped into WAV/MOV containers.

**Still to do** (larger-scope work, depends on new framework classes):
- [ ] `--pipeline <path>` JSON ingest / `--save-pipeline <path>` JSON export — blocked on the new `MediaPipelineConfig` data object (phase 4A, see `proav_pipeline.md`).
- [ ] Per-stage stats aggregation for `--verbose` via the future `MediaPipeline::stats()`.
- [ ] Integration tests covering known CLI invocations against golden data files.
- [ ] `docs/mediaplay.dox` covering the grammar with worked examples.

---

---

## VideoCodec / AudioCodec registry, MediaPacket, and VideoEncoder / VideoDecoder framework (SHIPPED)

**Phase 4g additions (this session):**

**`VideoCodec` registry** — `include/promeki/videocodec.h` + `src/core/videocodec.cpp`.  TypeRegistry-pattern wrapper around an immutable `Data` record (name, description, FourCC list, compressed `PixelFormat` IDs, encoder + decoder factories).  Well-known IDs: `H264`, `HEVC`, `AV1`, `VP9`, `JPEG`, `JPEG_XS`, `ProRes_422_Proxy/LT/422/HQ`, `ProRes_4444/XQ`.  `lookup(name)` resolves by PascalCase string name (e.g. `"H264"`, `"HEVC"`, `"JPEG"`).  `registeredIDs()` enumerates every registered codec.  User-defined codecs can extend the registry via `registerType()` + `registerData()`.  `PROMEKI_REGISTER_VIDEO_ENCODER` / `PROMEKI_REGISTER_VIDEO_DECODER` macros wire factory functions into `VideoCodec::Data` at static-init time.  Tests in `tests/videocodec.cpp` (passthrough in-test codec covering push/pull plumbing, flush/EOS, `MediaConfig` forwarding) and `tests/videocodec_registry.cpp` (well-known ID resolution, lookup, factory wiring for JPEG/JPEG_XS, user-registered codecs).

**`AudioCodec` registry** — `include/promeki/audiocodec.h` + `src/core/audiocodec.cpp`.  Same TypeRegistry pattern.  Well-known IDs: `AAC`, `Opus`, `FLAC`, `MP3`, `PCMI_Float32LE/BE`, `PCMI_S8/U8`, `PCMI_S16LE/BE/U16LE/BE`, `PCMI_S24LE/BE/U24LE/BE`, `PCMI_S32LE/BE/U32LE/BE`.  No encoder/decoder backends yet — the registry and `AudioEncoder` / `AudioDecoder` forward declarations are in place for future backends.  Tests in `tests/audiocodec.cpp`.

**`MediaPacket` data object** — `include/promeki/mediapacket.h` + `src/proav/mediapacket.cpp`.  Shareable data object representing one compressed access unit.  Carries a `BufferView` onto the encoded bytes (supports shared-buffer / multi-NAL-unit slice views), a `PixelFormat` codec identifier, separate PTS + DTS `MediaTimeStamp` values, a `Duration`, a `Flag` bitmask (`Keyframe`, `EndOfStream`, `Discardable`, `Corrupt`, `ParameterSet`), and freeform `Metadata`.  Tests in `tests/mediapacket.cpp`.

**`MediaConfig::VideoCodec` + `MediaConfig::AudioCodec`** — `MediaConfig::CodecName` (String) retired; replaced by typed `MediaConfig::VideoCodec` (`TypeVideoCodec`) and `MediaConfig::AudioCodec` (`TypeAudioCodec`).  Codec string names switched to PascalCase throughout (`H264`, `HEVC`, `JPEG`, `JPEG_XS`, `AAC`, `Opus`, `PCMI_S16LE`, `PCMI_Float32LE`, etc.) to match the TypeRegistry name fields.  Codec config forwarding in `MediaIOTask_VideoEncoder`, `MediaIOTask_VideoDecoder`, and `mediaplay` updated accordingly.

**`JpegVideoEncoder` / `JpegVideoDecoder`** — `include/promeki/jpegvideocodec.h` + `src/proav/jpegvideocodec.cpp`.  Direct `VideoEncoder` / `VideoDecoder` implementations using libjpeg-turbo; the former `JpegImageCodec` adapter layer was deleted in Phase 4s (functionality folded directly into these classes).  Per-frame JPEG encode/decode with output FIFO queuing.  Registered against `VideoCodec::JPEG`.  Config keys: `JpegQuality`, `JpegSubsampling`, `OutputPixelFormat`, `Capacity`.  Coverage via `tests/videocodec_registry.cpp` and `tests/imagefileio_jpeg.cpp`.

**`JpegXsVideoEncoder` / `JpegXsVideoDecoder`** — `include/promeki/jpegxsvideocodec.h` + `src/proav/jpegxsvideocodec.cpp`.  Direct `VideoEncoder` / `VideoDecoder` implementations using vendored SVT-JPEG-XS; the former `JpegXsImageCodec` adapter layer was deleted in Phase 4s.  Registered against `VideoCodec::JPEG_XS`.  Config keys: `JpegXsBpp`, `JpegXsDecomposition`, `OutputPixelFormat`, `Capacity`.  Coverage via `tests/imagefileio_jpegxs.cpp`.

**`MediaIOTask_VideoEncoder`** — `include/promeki/mediaiotask_videoencoder.h` + `src/proav/mediaiotask_videoencoder.cpp`.  Generic `InputAndOutput` MediaIO backend that wraps any registered `VideoEncoder`.  Codec selected at open time from `MediaConfig::VideoCodec`; open fails with `NotSupported` when the codec has no registered encoder factory.  Config forwarded to the encoder via `VideoEncoder::configure()`.  Compressed frames emitted as `MediaPacket`-carrying `Frame` objects.  Tests in `tests/mediaiotask_videoencoder.cpp` (uses in-test Passthrough codec from `tests/videocodec.cpp`).  **Phase 4h fixes:** (1) `_pendingSrcFrames` FIFO added — mirrors the decoder fix: source Frames enqueued per `submitFrame`, dequeued per emitted `MediaPacket`, ensuring metadata and audio survive the NEED_MORE_INPUT buffering edge case.  (2) `MediaConfig::FrameRate` is now stamped from `cmd.pendingMediaDesc.frameRate()` at open time so the encoder's rate-control (VUI timing, HRD buffer sizing) uses the actual stream rate rather than the backend default.

**`MediaIOTask_VideoDecoder`** — `include/promeki/mediaiotask_videodecoder.h` + `src/proav/mediaiotask_videodecoder.cpp`.  Complementary read-write backend that accepts `MediaPacket` frames from an upstream encoder and decodes them back to uncompressed `Image` frames.  Tests in `tests/mediaiotask_videodecoder.cpp`.  **Phase 4h fix:** `_pendingSrcFrames` FIFO added — one source Frame is enqueued per `submitPacket` and dequeued per decoded image so audio and metadata are paired with the correct input across DPB reorder delay; the old single-`srcFrame` arg to `drainDecoderInto` caused off-by-N metadata on startup of every H.264/HEVC stream.

**`MediaIOTask_RawBitstream`** — `include/promeki/mediaiotask_rawbitstream.h` + `src/proav/mediaiotask_rawbitstream.cpp`.  Write-only MediaIO sink that appends each `MediaPacket` payload verbatim to a file (no container, no timestamps).  Registered with extensions `h264`/`h265`/`hevc`/`bit` so `mediaplay -d foo.h264` routes automatically.  Useful for capturing NVENC output as an Annex-B elementary stream for `ffplay`/`mpv` verification.  Config key: `MediaConfig::Filename`.  Stats: `StatsPacketsWritten`, `StatsBytesWritten`.  Test coverage pending (integration via NVENC path).

**CUDA utility** — `include/promeki/cuda.h` + `src/core/cuda.cpp`.  Thin wrapper for CUDA device enumeration and context management, compiled only when `PROMEKI_ENABLE_CUDA` is set.  Tests in `tests/cuda.cpp`.

**`NvencVideoEncoder` / `NvDecVideoDecoder`** — `include/promeki/nvencvideoencoder.h` + `include/promeki/nvdecvideodecoder.h` + matching `.cpp` files.  NVIDIA GPU-accelerated H.264 and HEVC encode/decode via the NVIDIA Video Codec SDK.  Compiled only when `PROMEKI_ENABLE_NVENC` / `PROMEKI_ENABLE_NVDEC` are set.  See `docs/nvenc.dox` for setup instructions.  Tests in `tests/nvencvideoencoder.cpp` and `tests/nvdecvideodecoder.cpp` (device-gated).

**`typeregistry_isolation` test** — `tests/typeregistry_isolation.cpp` pins that independently-compiled TypeRegistry instances (`VideoCodec`, `AudioCodec`, `PixelFormat`, `Enum`) share no accidental global state via their string-keyed lookup tables.

---

## Phase 4h additions

**`H264Bitstream` / `AvcDecoderConfig` / `HevcDecoderConfig`** — `include/promeki/h264bitstream.h` + `include/promeki/hevcbitstream.h` + `src/core/h264bitstream.cpp` + `src/core/hevcbitstream.cpp`.  Low-level NAL framing helpers shared by the QuickTime writer and any future RTP or elementary-stream path.  `H264Bitstream` provides `forEachAnnexBNal` / `forEachAvccNal` iterators, `annexBToAvcc` / `avccToAnnexB` converters, `annexBToAvccFiltered` (strips parameter-set NALs for avc1/hvc1 sample entries), and `wrapNalsAsAnnexB`.  `AvcDecoderConfig` and `HevcDecoderConfig` model the ISO/IEC 14496-15 `avcC` / `hvcC` configuration records with `fromAnnexB` (extract from first IDR), `serialize` / `parse` (raw payload round-trip), and `toAnnexB` (emit parameter sets as Annex-B for hardware decoders).  Tests in `tests/h264bitstream.cpp` (5 TEST_CASEs) and `tests/hevcbitstream.cpp` (3 TEST_CASEs).

**QuickTime H.264 / HEVC write + read** — the QuickTime writer now emits proper `avc1` / `hvc1` sample entries with embedded `avcC` / `hvcC` child boxes for H.264 and HEVC tracks.  On the first keyframe, `AvcDecoderConfig::fromAnnexB` / `HevcDecoderConfig::fromAnnexB` extract the parameter sets from the Annex-B access unit; the configuration record is serialized and stored in the track's `codecConfigBox`.  Each sample payload is rewritten from Annex-B to AVCC (length-prefixed) form via `H264Bitstream::annexBToAvccFiltered`, stripping SPS/PPS/VPS NALs that must live only in the sample description per ISO/IEC 14496-15.  `appendStsdBox` is now a single shared helper (eliminating duplicate inline copies in `appendTrak` and `appendInitTrak`).  The reader gained `parseVideoSampleEntry` which walks codec-specific child boxes (`avcC` / `hvcC`) after the fixed visual sample entry fields and stores the payload on `QuickTime::Track` via new `codecConfig()` / `codecConfigType()` accessors.  Tests in `tests/quicktime.cpp` (new `QuickTimeWriter: H.264 Annex-B input is stored as AVCC with avcC box` TEST_CASE with 4 subcases covering raw-byte avcC verification, reader PixelFormat resolution, configuration record extraction, and AVCC length-prefix verification).

**`Signal::connect(Function, ObjectBase*)` overload** — `include/promeki/signal.h` + `include/promeki/objectbase.h`.  Qt AutoConnection-style overload: when the signal emits on the owner's EventLoop thread the slot is called directly; from any other thread the call is marshalled via `ownerLoop->postCallable`.  Args are captured into `std::make_tuple` / `std::apply` so they must be copy-constructible.  Passes `nullptr` to the assertion guard.  Tests in `tests/thread.cpp` (4 new TEST_CASEs: same-thread direct dispatch, cross-thread marshal, concurrent-emitters serialization, null-owner assert).

**`Thread::id()`** — `include/promeki/thread.h` + `src/core/thread.cpp`.  Returns the `std::thread::id` captured at thread start or adoption, complementing `nativeId()` for cross-thread comparisons using `std::this_thread::get_id()`.

**`FrameSync` audio format conversion** — `src/proav/framesync.cpp`.  Non-native audio (e.g. `PCMI_S16LE` from the QuickTime PCM reader) is now converted to native float via `Audio::convertTo(AudioDesc::NativeType)` before being pushed into the resampler input FIFO.  Previously, the `isNative()` check in `produceAudio` silently discarded the buffer, causing silent audio in file-reading pipelines that use a non-native PCM format.  Also: `produceAudio` now allocates its output as `AudioDesc::NativeType` regardless of `_targetAudioDesc` and calls `out.zero()` instead of `std::memset`.

**CSC SIMD fast paths** — `src/proav/csc/fastpath-inl.h` + `src/proav/csc/fastpath.cpp`.  Added SIMD (Highway) kernels for NV12→RGBA8 and RGBA8→NV12 using reformulated BT.709 integer coefficients that fit in `i16` throughout; also added fast-path pairs for RGB8↔NV12, RGBA8↔NV21, and RGBA8↔semi-planar 4:2:2 (NV16).  Cross-validation test extended to include the new pairs and to sweep multiple widths (16/17/30/31/32/33/48/63) to exercise SIMD-loop boundaries, scalar-tail handler, and odd-width trailing-pixel path.

**`MediaIO` pipeline back-pressure fix** — `src/proav/mediaio.cpp`.  `frameWantedSignal` is now emitted after a successful read in addition to the existing `frameReadySignal` emit.  Without this, pipeline stages whose output FIFO was full when the previous write attempt fired could stall permanently: after all pending writes completed the write side had already fired its last `frameWanted` and only reads released remaining capacity, so the upstream was never re-kicked.

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
- JPEG XS: RGB encode path uses CSC workaround (`RGB8_sRGB` → `RGB8_Planar_sRGB` → SVT planar) due to SVT validation bug in `send_picture` for packed format; details and future direct path in `fixme.md`
- JPEG XS: additional matrix/range/colour-space variants (only Rec.709 limited-range and sRGB wired up)
- JPEG XS: QuickTime/ISO-BMFF `jxsm` sample entry not implemented — blocked on procuring ISO/IEC 21122-3:2024. See `fixme.md` for details.
- JPEG XS: RFC 9134 RTP slice packetization mode (K=1) + fmtp generation from SVT image config
