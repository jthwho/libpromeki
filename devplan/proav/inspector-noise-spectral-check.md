# Inspector: `WhiteNoise` / `PinkNoise` spectral sanity probe

`InspectorMediaIO` should grow a coarse spectral check for channels
configured as `WhiteNoise` or `PinkNoise`. Both generators produce
bounded-amplitude broadband signals; a check should pass:

- Spectral tilt (≈ 0 dB/octave for white, ≈ -3 dB/octave for pink).
- Peak-level bound.

The check is coarse by design — the goal is "is this channel in the
right ballpark?" rather than "is this the exact same buffer the TPG
generated".

**Depends on:** [core/fft.md](../core/fft.md) (library FFT) landing
first.

## Tasks

- [ ] Octave-bin power summation over a windowed STFT.
- [ ] Tolerance bands per pattern (white: ±N dB across octaves;
  pink: -3 dB ± M dB per octave).
- [ ] Report pass/fail in the Inspector event so test code and
  `--filter` queries can inspect it.
