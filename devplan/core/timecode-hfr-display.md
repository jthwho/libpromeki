# Timecode display for HFR

`Timecode` formatting (`toString` and the `std::formatter`
specialisations) breaks down at high frame rates (HFR > 30 fps).
Need to confirm exact symptom and fix.

Likely shape of the fix: the `:ff` field width assumes two digits,
which breaks for HFR rates where the frame count can exceed 99
(120, 144, 240, …). SMPTE ST 12-1 has accepted forms for HFR
(typically `HH:MM:SS:FFF`); the formatter should pick the right
field width from the timecode mode's nominal rate.

Related: [tpg-ltc-hfr.md](../proav/tpg-ltc-hfr.md) — TPG LTC
generation also breaks above 30 fps.

## Tasks

- [ ] Verify the failure mode at 60 / 100 / 120 / 144 / 240 fps.
- [ ] Drive field width from `Timecode::Mode::fps()` so HFR rates
  use 3-digit `FFF` per SMPTE ST 12-1.
- [ ] Test round-trip via `fromString` for each HFR rate.
