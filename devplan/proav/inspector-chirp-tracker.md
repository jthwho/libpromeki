# Inspector: `Chirp` sweep tracker

`InspectorMediaIO` should grow a sweep tracker for channels carrying
TPG's `AudioPattern::Chirp`. Given the configured `start` / `end` /
`duration`, the tracker computes the instantaneous expected
frequency for each sample and verifies the decoded audio matches
within a tolerance.

A working tracker also catches:

- Sample drops — tracked instantaneous frequency jumps forward.
- Duplicated chunks — frequency flat-lines.
- SRC drift — frequency scales by the conversion ratio.

Probably worth building a dedicated `ChirpDecoder` helper class
rather than inlining the math in Inspector; bigger project than the
noise-tilt probes.

**Depends on:** [core/fft.md](../core/fft.md).

## Tasks

- [ ] Build `ChirpDecoder` helper (windowed STFT around the
  expected frequency; tolerance per bin).
- [ ] Wire into Inspector with config keys for tolerance and report
  cadence.
- [ ] Detect and report drop / duplicate / SRC-drift events
  separately from generic out-of-tolerance.
- [ ] Tests covering each failure mode against synthesised input.
