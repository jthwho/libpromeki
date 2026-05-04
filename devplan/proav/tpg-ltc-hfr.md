# TPG: fix LTC generation when fps > 30

The TPG's LTC audio mode breaks at frame rates above 30 fps. LTC
itself runs to ~30 fps; for HFR captures (50 / 59.94 / 60 / 100 /
120 / 144 / 240 fps) the standard practice is to either send LTC
at half the video rate or use a dedicated HFR signalling form.

Need to confirm exact failure and pick a strategy:

- Drop to half-rate LTC (one TC per two video frames) — simplest
  but loses sub-frame accuracy.
- Encode at the nominal rate using a non-standard frame-count form
  that consumers may or may not parse.
- Switch to a different sample-domain framing (e.g. the TPG's
  `PcmMarker` BCD64 path) for HFR captures.

Related: [core/timecode-hfr-display.md](../core/timecode-hfr-display.md)
— `Timecode::toString` formatter also breaks for HFR rates.

## Tasks

- [ ] Reproduce the failure at 60 / 120 fps.
- [ ] Pick a strategy and document it in
  `docs/audiotestpattern.dox` (or wherever the AvSync mode is
  documented).
- [ ] Update `LtcEncoder` if needed; gate the TPG behaviour on
  `FrameRate::nominal() > 30`.
- [ ] Round-trip test through Inspector / `LtcDecoder` at HFR.
