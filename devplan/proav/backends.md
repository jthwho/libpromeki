# MediaIO Backends

**Phase:** 4B
**Library:** `promeki`
**Standards:** All work follows `CODING_STANDARDS.md`. Every class
requires complete doctest coverage. See `devplan/README.md` for the
full requirements.

This document tracks the `MediaIO` backends that plug into the
`MediaIO` framework. Each backend is now an `XMediaIO` class derived
from `SharedThreadMediaIO` (or another concrete strategy in the
`MediaIO → CommandMediaIO → SharedThreadMediaIO` hierarchy added by
the MediaIO ports/strategies/factory refactor) and registered through
a `MediaIOFactory`. The legacy `MediaIOTask_*` naming is gone; older
docs may still mention the old names.

`docs/mediaio.dox` is the authoring guide for the framework. Earlier
phases of this document logged every backend's shipped feature set;
that history now lives in git. What remains here is the open work.

## Shipped backends (for navigation)

- **TpgMediaIO** — test pattern generator (video + audio + timecode).
- **ImageFileMediaIO** — DPX, Cineon, TGA, SGI, PNM, PNG, JPEG, JPEG XS,
  RawYUV. Includes the BWF sidecar audio path and `.imgseq` sequence
  index.
- **AudioFileMediaIO** — WAV, BWF, AIFF, FLAC, W64, RF64, OGG (libsndfile).
- **QuickTimeMediaIO** — Classic + fragmented `.mov` / `.mp4` reader and
  writer (ProRes, H.264, HEVC, AV1, JPEG, JPEG XS, PCM, AAC).
- **CscMediaIO** — uncompressed pixel-format conversion via `Image::convert`.
- **SrcMediaIO** — audio sample-format conversion via the
  `AudioFormat::convertTo` / direct-converter registry.
- **BurnMediaIO** — text overlay via `VideoTestPattern::applyBurn`.
- **FrameSyncMediaIO** — wraps `FrameSync` for cadence resync.
- **InspectorMediaIO** — QA sink: image-data band decode, per-channel
  `AudioDataDecoder` marker decode (`InspectorTest::AudioData`,
  default-on), audio LTC decode (opt-in), A/V sync offset, continuity
  checks, `--filter` queries.
- **VideoEncoderMediaIO** / **VideoDecoderMediaIO** — generic codec
  wrappers over the `VideoCodec` registry. JPEG, JPEG XS, NVENC,
  NVDEC backends are registered.
- **RawBitstreamMediaIO** — Annex-B / `.h264` / `.hevc` / `.bit`
  elementary-stream sink.
- **RtpMediaIO** — unified writer + reader for video / audio /
  metadata RTP, with SDP-driven auto-config.
- **NdiMediaIO** — NDI Advanced source + sink with sender-anchored
  audio timeline (`AudioMarker` / `pushSilence`) and `PacingGate` for
  external-clock pacing.
- **FrameBridgeMediaIO** — cross-process shared-memory frame transport.
- **DebugMediaMediaIO** (PMDF) — lossless debug container; companion
  CLI `pmdf-inspect`.
- **MjpegStreamMediaIO** — rate-limited MJPEG HTTP preview sink.
- **NullPacingMediaIO** — wall-clock-paced null sink.
- **V4L2MediaIO** — Linux V4L2 capture (with ALSA pairing).
- **SDLPlayerTask** / **SDLPlayerWidget** — SDL display sink + widget.
- **TPG, Inspector, Burn, RawBitstream, FrameBridge, DebugMedia,
  Mjpeg, NullPacing** all carry full describe / proposeInput /
  proposeOutput coverage per the planner contract (see
  `proav/planner.md`).

---

## CSC backend (CscMediaIO)

- [ ] First-class `OutputColorModel` knob (today the ColorModel rides
  inside the target `PixelFormat`).
- [ ] First-class `OutputSampleRate` knob (deferred to the audio
  resampler — see [proav/dsp.md](dsp.md)).

## Codec abstraction follow-ups

- [ ] **Generic codec config discovery.**
  `VideoEncoder::configure(const MediaConfig &)` is opt-in; each
  subclass knows which `MediaConfig::*` keys it cares about, but
  there's no programmatic way for callers (e.g. a `mediaplay --help`
  consumer or the pipeline editor frontend) to enumerate them. Add
  `defaultConfig()` / `configKeys()` to the codec base classes so
  schema can be rendered without hard-coding.
- [ ] **Stateful temporal codec drain hook.** Today the close path
  flushes; explore a separate `drain()` / `flush()` hook that lets
  the pipeline shut down with no in-flight loss when the encoder has
  internal buffering (this is the same gap as `proav/quicktime.md`'s
  HighQuality-preset drain-at-close issue).
- [ ] **`cancelPending()` hook** that explicitly clears the in-progress
  FIFO (today it clears on `close()`).
- [ ] **`FormatDesc::enumerate()` callback** so the factory can report
  supported conversions without opening a stage.

## RtpMediaIO follow-ups

- [ ] Mid-stream descriptor discovery: reader does not yet handle
  resolution / pixel-format changes mid-playback.
- [ ] Timestamp wrap handling in the reassembler (RTP timestamp
  wraps at 2³²).
- [ ] Proper ST 2110-20 pgroup sizing for 10/12-bit YCbCr in
  `RtpPayloadRawVideo`. Current implementation handles 8-bit
  interleaved formats only.
- [ ] **L24** audio (`RtpPayloadL24`). The payload class handles
  3-byte-packed BE samples; the task needs a pack-and-swap step (or a
  pre-stage that lands in 3-byte packed form) since
  `AudioFormat::PCMI_S24LE` stores in 4-byte int32 slots.
- [ ] **SMPTE ST 2110-40** Ancillary Data payload. The
  `MetadataRtpFormat::St2110_40` enum entry exists but the backend
  rejects it at configure time until RFC 8331 packet handling lands
  (ST 291 ANC parse, DID/SDID/DBN, field/line placement, BCH ECC).
- [ ] **`RtpPacingMode::TxTime`** wired through to per-packet
  `SCM_TXTIME` deadlines on `Datagram::txTimeNs`. The transport
  interface is ready (see [proav/optimization.md](optimization.md));
  the sender just needs to compute deadlines from the frame rate.
- [ ] PTP-locked RTP timestamps once `PtpClock` lands (see
  [network/avoverip.md](../network/avoverip.md)).
- [ ] Writer back-pressure: return `Error::TryAgain` when the
  underlying UDP send buffer is full instead of blocking.
  Not blocking at current rates, will matter for ST 2110 uncompressed.
- [ ] `RtpPayloadJpegXs` slice mode (K=1) and interlaced framing —
  currently only codestream mode (K=0) is implemented.
- [ ] APP marker parsing in MJPEG reader (APP2 ICC, APP14 Adobe,
  APP1 EXIF) so the receiver can recover Rec.709 vs Rec.601 / full
  vs limited range. See [fixme/rtp-jpeg-color-info.md](../fixme/rtp-jpeg-color-info.md).

## ImageFile backend

- [ ] `JpegQuality` / `JpegSubsampling` / `JpegProgressive` exposed as
  first-class config keys (today they ride through the codec
  configure() path).
- [ ] EXIF / IPTC parsing on read.
- [ ] `ImageFile::JpegXS` exposed in `defaultConfig()` so callers can
  probe JPEG XS config keys without knowing the codec.
- [ ] 10/12-bit planar RGB (`P_444_3x10_LE`, `RGB10_Planar_LE_sRGB`)
  for JPEG XS once SVT's packed-RGB validation is fixed
  (see [fixme/jpegxs-svt-packed-rgb.md](../fixme/jpegxs-svt-packed-rgb.md))
  or real 10/12-bit RGB workflows appear.

## QuickTime backend

See [proav/quicktime.md](quicktime.md) for the deferred
HighQuality-preset drain-at-close issue. Other open items are
recorded in [`fixme/`](../fixme/):

- [Little-endian float audio storage](../fixme/quicktime-lpcm-float.md)
  (lossy promotion to s16; needs `lpcm` + `pcmC`).
- [`raw ` 24-bit BGR / RGB byte-order](../fixme/quicktime-raw-byteorder.md)
  player disagreement.
- [Compressed-audio pull-rate drift](../fixme/quicktime-compressed-audio-drift.md)
  (single-packet-per-video-frame heuristic).
- [Compressed-audio write path missing](../fixme/quicktime-compressed-audio-write.md)
  (remux workflows blocked).
- [XMP parser matches only `bext:` prefix](../fixme/quicktime-xmp-bext.md)
  (blocked on core XML).
- [Fragmented reader ignores `trex` default fallback](../fixme/quicktime-trex-defaults.md).
- [`BufferPool` available but not wired into the hot path](../fixme/bufferpool-wiring.md)
  (premature; measure first).

---

## mediaplay CLI

The mediaplay utility is the main config-driven CLI. Most major work is
shipped (`-s` / `-d` source/sink flags, `--sc` / `--dc` per-stage
config, autoplan, `--describe`, `--plan`, `--stats`, `--memstats`,
`--pipeline <file>`, `--save-pipeline <file>`, NDI URLs via
`ndi://`, etc.).

### Remaining work

- [ ] Per-stage stats aggregation for `mediaplay --verbose` via
  `MediaPipeline::stats()`.
- [ ] Integration tests covering known CLI invocations against
  golden-data files (`tests/func/mediaplay/`).
- [ ] `docs/mediaplay.dox` — grammar reference with worked examples.
- [ ] `--plan --json <PATH>` to write the resolved config as JSON
  (the underlying `MediaPipelineConfig::saveToFile` already exists).

---

## Known issues

All tracked in [`fixme/`](../fixme/). The QuickTime, JPEG XS, and
RTP entries listed above are the ones that belong to this document.
