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

**Update (2026-05-20):** The Timecode / ATC audit (see
`docs/timecode.md`) wired up libvtc for all timecode formatting.
`libvtc` already handles 3-digit frame fields at HFR — e.g.
`"02:30:15:115"` at 120p (`VTC_FORMAT_120_30X4`).  `Timecode`
delegates `toString` / `fromString` to `vtc_timecode_to_string` /
`vtc_timecode_from_string_with_format`, so field-width is
automatically correct.  The underlying formatter gap is closed.

The remaining task is adding explicit round-trip test coverage
for each HFR rate so the fix doesn't regress.

## Tasks

- [x] Verify the failure mode at 60 / 100 / 120 / 144 / 240 fps. (resolved — libvtc handles field width)
- [x] Drive field width from `Timecode::Mode::fps()` so HFR rates
  use 3-digit `FFF` per SMPTE ST 12-1. (resolved — libvtc handles this automatically via `VTC_FORMAT_*`)
- [ ] Add `Timecode toString` test cases covering `NDF60`, `NDF120`, `NDF100`, `NDF120_24x5`
  to confirm 3-digit round-trip and prevent regression.
