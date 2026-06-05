# QuickTime writer investigation

Started 2026-04-17 as "mplayer freezes on our `.mov` output". The
investigation surfaced one wontfix (mplayer-only kernel freeze),
several spec-cleanliness improvements (all landed), and one
substantive open issue (`VideoPreset:HighQuality` drain-at-close).
The full trace and the eight landed fixes live in git history; this
document keeps the open item plus pointers for reproduction.

## Open: HighQuality preset drain-at-close

### Symptom

`--cc VideoPreset:HighQuality` produces a tiny (~1830 B) `.mov`:
`is_avc=false`, `profile=unknown`, `nal_length_size=0`, mdat empty.
ffprobe sees zero video samples.

### Root cause

NVENC's `P6 + HIGH_QUALITY` preset enables B-frames and/or lookahead
by default. Two distinct issues compound:

- **B1. Intra-session drain is order-locked.** Even before close,
  `NvencVideoEncoder::Impl::receivePacket()` only drains slot-0.
  With lookahead, slot-0 stays `hasOutput=0` for many submits while
  later slots accumulate output. The drain should either recheck
  `hasOutput` against the actual head each call (some slot N<0's
  output now lives on slot N's buffer per NVENC), or adopt NVENC's
  reorder-aware completion model.
- **B2. Drain-at-close races sink close.** `VideoEncoderMediaIO
  ::executeCmd(Close)` empties NVENC into the stage's
  `_outputQueue`, then there's no one left to pull. Even with B1
  fixed, a `Pipeline::close()` that closes stages in parallel would
  still strand packets on close in any encoder with internal
  buffering.

### Proposed direction: MediaIO `shutdown()` / two-phase close

The right fix is a pipeline-level drain contract:

1. `MediaIO` grows a `shutdown()` command distinct from `close()` —
   "no more input is coming; drain any buffered output."
2. Pipeline teardown propagates `shutdown()` topologically from
   sources downward. A stage seeing `shutdown()` on its input
   flushes internal backend state into its output queue, stays
   alive so downstream can `read()`, and forwards `shutdown()` to
   its own downstream once its output queue is empty.
3. `close()` runs *after* `shutdown()` has propagated through the
   whole graph; every stage's output queue is already empty and
   `close()` just releases resources.

Deferred — this is a MediaIO-framework change, not a
QuickTime-writer change. Revisit when tackling it. In the meantime,
prefer `Balanced` (the default) for short captures; HQ works for
long captures where the relative size of the flushed-and-lost tail
is negligible (still drops the final ~lookaheadDepth frames).

### Reproduction

```bash
# Default (Balanced) — works:
mediaplay -s TPG --frame-count 30 \
    -c CSC --cc OutputPixelFormat:YUV8_420_SemiPlanar_Rec709 \
    -c VideoEncoder --cc VideoCodec:H264 \
    -d /mnt/data/tmp/promeki/balanced.mov

# HighQuality — reproduces the drain-at-close bug:
mediaplay -s TPG --frame-count 30 \
    -c CSC --cc OutputPixelFormat:YUV8_420_SemiPlanar_Rec709 \
    -c VideoEncoder --cc VideoCodec:H264 \
    --cc VideoPreset:HighQuality \
    -d /mnt/data/tmp/promeki/hq.mov
```

### Code pointers

- `src/proav/nvencvideoencoder.cpp` — `submit/flush` loop is where
  drain-order bug B1 lives.
- `src/proav/mediaiotask_videoencoder.cpp::executeCmd(Close)` —
  flush-and-strand bug B2 lives here.

## Landed capabilities (2026-06-04)

- **True MP4 brand mode.** `QuickTime::Profile{ProfileMov,ProfileMp4}`
  selected from the sink filename extension by `QuickTimeMediaIO`;
  `.mp4`/`.m4v`/`.m4a` now stamp an `isom`/`mp42` major brand,
  `.mov`/`.qt` keep `qt  `. H.264/HEVC `avc1`/`hvc1` entries are ISO-
  conformant in both.
- **All flavors of PCM.** The QuickTime `lpcm` v2 sound description
  (`formatSpecificFlags`) carries float / endianness / signedness /
  alignment, so LE/BE 16/24/32-bit, faithful float, and 24-in-32
  packings round-trip bit-exact. Planar maps to interleaved storage.
  The six classic FourCCs (sowt/twos/in24/in32/fl32/raw) remain for
  compatibility. Lossy Float32LE→S16LE promotion removed (was the
  `quicktime-lpcm-float` fixme).
- **SMPTE ST 436M VANC ancillary track.** `St436m` codec
  (`st436m.{h,cpp}`, 10-bit pass-through per ST 436M-2006 §6.2) +
  `TrackType::AncData` / `vanc` sample entry wired through writer/
  reader/MediaIO. One sample per frame; `AncPayload` in / out.
- **CEA-608 (`c608`) caption track.** `QtClosedCaption` codec
  (`qtclosedcaption.{h,cpp}`, `cdat`/`cdt2` atoms, byte-exact to
  ffmpeg's MOV muxer) + `TrackType::Caption` / `clcp` handler.
  `QuickTimeMediaIO` emits a player-renderable `c608` track from the
  frame's CEA-708 CDP ANC packets alongside the ST 436M track.
  Verified end-to-end: ffmpeg decodes our `c608` track byte-exact.
  There is **no standardized `c708` caption track** — Apple's QTFF
  defines only `c608`, and CEA-708 DTVCC already rides the ST 436M
  `vanc` track (full CDP). The consumer 708 path is in-stream A/53 SEI
  (a VideoEncoder feature), not a container track — see
  [`fixme/quicktime-cea708-caption-track.md`](../fixme/quicktime-cea708-caption-track.md).

## Other QuickTime open items

Tracked in [`fixme/`](../fixme/):

- [`raw ` 24-bit RGB/BGR byte-order](../fixme/quicktime-raw-byteorder.md)
  player disagreement.
- [Compressed-audio pull-rate drift](../fixme/quicktime-compressed-audio-drift.md)
  (one-packet-per-video-frame heuristic fails on variable-duration
  compressed audio).
- [Compressed-audio write path missing](../fixme/quicktime-compressed-audio-write.md)
  (blocks remux).
- [XMP parser only matches `bext:` prefix](../fixme/quicktime-xmp-bext.md)
  (blocked on core XML).
- [Fragmented reader ignores `trex` default fallback](../fixme/quicktime-trex-defaults.md)
  (only handles `tfhd` overrides).
- [`BufferPool` available but not wired into the hot path](../fixme/bufferpool-wiring.md)
  (premature optimization until a real workload demands it).
- [JPEG XS `jxsm` sample entry not implemented](../fixme/jpegxs-quicktime-container.md)
  (blocked on procuring ISO/IEC 21122-3:2024).
