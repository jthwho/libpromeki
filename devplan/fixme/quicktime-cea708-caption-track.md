# QuickTime: CEA-708 carriage notes + remaining caption knobs

The CEA-608 (`c608`) closed-caption track landed 2026-06-04 (byte-exact
to ffmpeg, verified end-to-end), and the read-side merge of a `c608`
track into the ANC model landed the same day.

## No `c708` caption track — by design

There is **no standardized `c708` caption track** in QuickTime/ISO-BMFF.
Apple's QTFF defines only the CEA-608 (`c608`) closed-caption media
(its "subtitle" media is the separate `tx3g` timed-text format, not
captions). ffmpeg has no `c708` mux/demux either. So a `c708` track is
not a real target. CEA-708 DTVCC is already carried two standard ways:

- **ST 436M `vanc` track** — full CEA-708 CDP (cc_type 2/3 DTVCC
  included), losslessly. **Done** (Phase 3).
- **In-stream A/53 SEI** — 708 `cc_data` embedded in the H.264/HEVC
  bitstream as ATSC A/53 user-data SEI (the consumer/web MP4 path). A
  **VideoEncoder** feature (SEI insertion), not a container track —
  track separately if/when wanted.

## Done: caption read-back as ANC (`QuickTimeCaptionReadPolicy`)

`QuickTimeMediaIO` now reconstructs an `AncPayload` from a `c608` track
on read, governed by `MediaConfig::QuickTimeCaptionReadPolicy`
(`enums_mediaio.h`):

- `Auto` (default): surface the `vanc` ANC; if a `c608` track exists and
  the ANC carries no CEA-608/708 packet, inject the `c608` captions as a
  reconstructed CEA-708 CDP ANC packet (no duplication).
- `VancOnly`: ignore the `c608` track.
- `CaptionTrackOnly`: the `c608` track is authoritative — inject it and
  strip any caption packets that came from `vanc`.

## Remaining (deferred)

- [ ] **Write-side carriage config.** Today the writer always emits both
  a `vanc` track and a `c608` caption track when caption ANC is present.
  A `MediaConfig::QuickTimeAncCarriage` (`Vanc` / `Captions` / `Both`)
  would let writers choose. Deferred — the read-side policy was the
  immediate need.
