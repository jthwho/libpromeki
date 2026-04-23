# QuickTime Writer Investigation

Started 2026-04-17 as "mplayer freezes on our `.mov` output". The
investigation sprawled through the QuickTime container, the NVENC
bitstream, and eventually the pipeline drain-at-close contract. This
doc is the consolidated record of what was landed and what remains.

## Current status

- **mplayer freeze** — closed as "mplayer-only, wontfix" (see
  [Closed: mplayer freeze](#closed-mplayer-freeze)).
- **QuickTime spec-cleanliness wins** — landed
  (see [Landed fixes](#landed-fixes)).
- **`VideoPreset:HighQuality` broken output** — root cause identified;
  actual fix requires a MediaIO-level `shutdown()` / drain-before-close
  protocol, deferred (see
  [Deferred: HighQuality preset drain-at-close](#deferred-highquality-preset-drain-at-close)).

## Landed fixes

All of these are staged unstaged at the time of writing and should go in
the next checkpoint commit.

1. **ftyp major brand.** `iso5` for Fragmented layout, `qt  ` for
   Classic. `src/proav/quicktime_writer.cpp:273-294`.
2. **`sample_depends_on=2` on sync samples** (fMP4 `trun`).
   `kSampleFlagsSync` / `kSampleFlagsNonSync` constants +
   ffmpeg-style flag compression (`default_sample_flags` + `first_sample_flags`)
   in `writeFragment()`.
3. **`chan` atom in audio sample entry.** 24-byte full-box appended
   inside `appendStsdBox()` audio branch. Tags: Mono (`0x00640001`),
   Stereo (`0x00650002`), DiscreteInOrder + channel count otherwise.
4. **Per-fragment audio sample collapse.** Merges all contiguous audio
   chunks in a fragment into a single MP4 sample so audio `trun` is
   trivially uniform. Verified: audio trun is now 20 B with count=1 per
   fragment.
5. **Classic as default layout.** `src/proav/mediaiotask_quicktime.cpp:133`.
   Fragmented still available via `--cc QuickTimeLayout:Fragmented`.
6. **pic_timing SEI pinned on for H.264.** `nvencvideoencoder.cpp`
   session init always sets `enableTimeCode=1` /
   `outputPictureTimingSEI=1` for H.264; per-frame timing path widened
   so every H.264 frame populates `timeCode` (and
   `skipClockTimestampInsertion=1` when caller doesn't want a real
   timecode). Bonus: enabling `enableTimeCode` flips
   `nal_hrd_parameters_present_flag` to 1 in the emitted SPS, reaching
   parity with ffmpeg's `h264_nvenc` on both VUI fields.
7. **NVENC tuning default flipped to HQ.** `toNvencTuning()` fallback
   now returns `NV_ENC_TUNING_INFO_HIGH_QUALITY` for
   `VideoEncoderPreset::Default` / `Balanced`. No observed bitstream
   change (our code overwrites rc/gop/profile from MediaConfig anyway),
   but a more sensible default label for non-streaming capture use.
8. **mvhd/tkhd movie timescale — LCM of track timescales + round-to-nearest
   rescale.** Previously hardcoded `_movieTimescale=600` truncated 29.97 fps +
   48 kHz media by ~0.5 ms. Now `mvhd.timescale = LCM(30000, 48000) = 240000`
   and both `tkhd` durations exactly match their `mdhd` durations. Helpers
   `rescaleRound` and `lcmTimescale` in the anonymous namespace of
   `quicktime_writer.cpp`; applied in `writeMoov()` and `writeInitMoov()`.
   `_movieTimescale` member removed from the writer header.

## Closed: mplayer freeze

Cross-player test on every representative output:

| Player / path | Result |
|---|---|
| `ffplay` (sw) | plays |
| `vlc` | plays (startup audio glitch) |
| `mpv --hwdec=no` | plays |
| `mpv --hwdec=vdpau` | plays |
| `mpv --hwdec=nvdec` | plays |
| `mpv --hwdec=vaapi` | plays |
| `mplayer` (default vdpau) | hard freeze (kernel lockup) |

Only mplayer freezes, and on *any* H.264 NVENC output we produce, plus
on ffmpeg's fMP4 remux of the same elementary stream. ffplay runs the
software decoder on the exact same bytes without issue, and mpv runs the
same VDPAU path on the same hardware without issue. The freeze taking
down the graphics stack is itself a mplayer + NVIDIA-driver bug — a
userspace decoder should not be able to hang the kernel. **Not our bug
to fix.** The spec-cleanliness wins listed above stay on their own
merits.

## Closed: ffplay "flush burst on quit"

Briefly suspected a track-duration mismatch (ffplay kept its clock
running past EOF, frozen last frame, short audio burst on quit). Turned
out to be reproducible on ffmpeg's own classic-layout `.mov` output — it
is just ffplay's default EOF behaviour (doesn't auto-quit) plus SDL's
audio ring buffer flushing when the audio device closes. Not a file
issue.

Chasing this did surface the `_movieTimescale=600` truncation (fix #8
above), so the time wasn't wasted.

## Deferred: HighQuality preset drain-at-close

### Symptom

`--cc VideoPreset:HighQuality` produces a tiny (~1830 B) `.mov`:
`is_avc=false`, `profile=unknown`, `nal_length_size=0`, mdat empty.
ffprobe sees zero video samples.

### Root cause (traced 2026-04-17)

NVENC's `P6 + HIGH_QUALITY` preset enables B-frames and/or lookahead by
default. With 30 input frames and `VideoPreset:HighQuality`:

- Submits 0–15 return `NV_ENC_ERR_NEED_MORE_INPUT` while NVENC fills
  its lookahead buffer. No output is produced.
- Submits 16–28 return `NV_ENC_SUCCESS` — NVENC starts emitting output
  for the earlier frames.
- BUT our drain loop in `NvencVideoEncoder::Impl::receivePacket()`
  only drains if `_inFlight.front()->hasOutput` is true. Slot 0
  (submit 0) was marked `hasOutput=0` when it returned NEED_MORE_INPUT
  and is never re-marked during subsequent submits. So the per-submit
  drain never fires.
- The encoder task's `_outputQueue` therefore stays empty for the
  entire encode, and the downstream QuickTime task has nothing to read.
- Pipeline closes. `MediaIOTask_VideoEncoder::executeCmd(Close)` calls
  `flush()` (which marks every in-flight slot `hasOutput=true`) and
  `drainEncoderInto()`, successfully pulling all ~29 buffered packets
  into the task's own `_outputQueue`.
- But the sink's `Close` runs in parallel / immediately after, and
  nothing in the pipeline protocol pumps reads from the encoder task
  after its `Close` returns. The 29 packets are silently dropped.

`VideoPreset:Balanced` and the other presets work because every submit
returns `NV_ENC_SUCCESS` (1:1 latency) — packets flow out continuously
and the shutdown flush has nothing left to drain.

### Two issues, one visible symptom

- **B1. Intra-session drain is order-locked.** Even before close,
  `receivePacket()` only drains slot-0. With lookahead, slot-0 stays
  `hasOutput=0` for many submits. The drain should either recheck
  `hasOutput` against the actual head each call (some slot N<0's
  output is now on slot N's buffer from NVENC's POV, but the current
  code doesn't model that), or adopt NVENC's reorder-aware completion
  model.
- **B2. Drain-at-close races sink close.** Regardless of (B1), a
  clean close protocol needs the upstream task to fully flush into
  downstream before downstream is torn down. Today
  `MediaIOTask_VideoEncoder::executeCmd(Close)` empties the NVENC
  session into the *task's* `_outputQueue`, and then there's no one
  left to pull.

(B2) is the load-bearing one. Even if (B1) were perfect, a
`Pipeline::close()` that closes all stages in parallel would still
strand packets on close in any encoder with internal buffering.

### Proposed direction: MediaIO `shutdown()` / two-phase close

The right fix is a pipeline-level drain contract. Sketch:

1. `MediaIO` grows a `shutdown()` command distinct from `close()`.
   Meaning: "no more input is coming; drain any buffered output."
2. Pipeline teardown propagates `shutdown()` topologically from
   source(s) downward.  A stage seeing `shutdown()` on its input:
   - Flushes any internal backend state (encoder, muxer, …) into its
     output queue.
   - Stays alive so downstream can still `read()`.
   - Forwards `shutdown()` to its own downstream once its output queue
     drains to empty.
3. `close()` is called *after* `shutdown()` has propagated through the
   whole graph. At that point every stage's output queue is already
   empty and `close()` just releases resources.

Deferred — this is a MediaIO-framework change, not a QuickTime-writer
change. Revisit when tackling it.

### Workarounds until then

- Avoid `VideoPreset:HighQuality` on short captures. Use `Balanced`
  (the default) for anything expected to produce a file; HQ works for
  long captures where the relative size of the flushed-and-lost tail is
  negligible, but it will still drop the final ~lookaheadDepth frames.
- Long-term: don't work around it, do the `shutdown()` fix.

## Reproduction

All safe — tiny outputs, no desktop-session risk.

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

# Classic vs Fragmented layout (for QuickTime-writer diffing):
mediaplay -s TPG --frame-count 30 \
    -c CSC --cc OutputPixelFormat:YUV8_420_SemiPlanar_Rec709 \
    -c VideoEncoder --cc VideoCodec:H264 \
    --cc QuickTimeLayout:Fragmented \
    -d /mnt/data/tmp/promeki/frag.mov
```

`dump_stbl.py` in `/mnt/data/tmp/promeki/` parses mvhd / tkhd / mdhd /
stts / stsc / stsz / stco for an at-a-glance box summary — used to
verify fix #8.

## Code pointers

- `src/proav/quicktime_writer.cpp`
  - `:273-294` — ftyp emission.
  - anonymous namespace `rescaleRound` + `lcmTimescale` — movie
    timescale helpers for fix #8.
  - `writeMoov()` / `writeInitMoov()` — use the helpers for
    mvhd/tkhd duration.
  - `appendStsdBox()` audio branch — `chan` atom.
  - `writeFragment()` — `sample_depends_on` flag compression +
    per-fragment audio collapse.
- `src/proav/mediaiotask_quicktime.cpp:133` — default layout.
- `src/proav/nvencvideoencoder.cpp`
  - `toNvencTuning()` — HQ as default.
  - session init — unconditional H.264 pic_timing SEI.
  - submit/flush loop — the drain-order bug (B1) lives here.
- `src/proav/mediaiotask_videoencoder.cpp::executeCmd(Close)` — the
  flush-and-strand bug (B2) lives here.

## Reference: sample_flags bit layout (ISO/IEC 14496-12 §8.8.3.1)

32-bit big-endian word, bit 0 = MSB:

```
bits  0..3  reserved                (= 0)
bits  4..5  is_leading              (0=unknown, 1=has dep, 2=no dep, 3=has non-dep)
bits  6..7  sample_depends_on       (0=unknown, 1=depends, 2=no-depend, 3=reserved)
bits  8..9  sample_is_depended_on   (0=unknown, 1=yes, 2=no)
bits 10..11 sample_has_redundancy
bits 12..14 sample_padding_value
bit  15     sample_is_non_sync_sample  (0 = sync)
bits 16..31 sample_degradation_priority
```

Useful constants:
- sync-sample, no-deps:     `0x02000000`
- non-sync, has-deps:       `0x01010000`
