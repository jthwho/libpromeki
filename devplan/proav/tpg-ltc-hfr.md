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
  `PcmMarker` path) for HFR captures.

**Note (2026-05-03):** `PcmMarker` is now the TPG default on every
channel.  The new `AudioDataEncoder` / `AudioDataDecoder` wire format
carries a 48-bit frame counter — sufficient for any practical capture
duration at any frame rate — and the inspector decodes it per-channel
via `InspectorTest::AudioData`.  For HFR use cases this already
provides a better round-trip identifier than LTC; the remaining work
is deciding whether to keep `LTC` as an opt-in or deprecate it for
HFR configs.

**Update (2026-05-20):** the Timecode / ATC audit landed (see
`docs/timecode.md` and `docs/anc.md` § ATC carriage) and
addresses the underlying LTC at HFR problem.  In summary:

- `Timecode` now physical-frame at HFR (0..fps-1 at 60p, 0..119 at
  120p, with proper DF compensation at 59.94 / 119.88).
- `LtcEncoder` takes a `FrameRate` parameter and emits chunked
  per-video-frame audio so the codeword spans the correct number of
  video frames at HFR.
- libvtc packs / recovers the ST 12-3 sub-frame identifier bits in
  the LTC codeword automatically.

Audio LTC at HFR still runs at the super-frame rate (ST 12-1 §9.6
audio-bandwidth limit caps the bit rate at ~80×30 = 2.4 kbps), so
the per-physical-frame phase isn't recoverable from audio LTC.
Per-physical-frame timecode lives in ATC_HFRTC (SDID=0x61) instead;
see @ref anc_atc_carriage in the ANC docs.

The TPG-side decision (keep `LTC` as an opt-in vs deprecate for HFR)
is still open — the underlying library now works; the question is
about the TPG's UX policy.

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
