# Library FFT

Add a real FFT to libpromeki so we can validate the audio TPG
patterns spectrally from inside the unit tests (rather than via an
external Python/scipy script).

At minimum we need:

- Forward real-to-complex FFT with power-of-two sizes.
- Matching inverse.
- Hann / Blackman-Harris windows.

The existing Highway-based code in `cscpipeline.cpp` is a reasonable
model for how to vendor or wrap a kernel. Candidates: kissfft (small,
self-contained, permissive) or pffft (faster, slightly larger).

Once the FFT is in place, add unit tests for every TPG pattern that
has a spectral property worth pinning:

- `WhiteNoise` — spectrum flat across octave bins within a few dB.
- `PinkNoise` — each octave ~3 dB below the next-lower octave (the
  Kellet filter ought to land within ~1 dB of the ideal slope).
- `Chirp` — instantaneous frequency tracked over a windowed STFT
  matches the closed-form sweep within tolerance at several sample
  points, including across a period wrap (regression-locks the chirp
  phase-continuity fix from Phase 4).
- `DualTone` — two dominant peaks at the configured `freq1` /
  `freq2`, amplitudes in the configured ratio, no unexpected
  third-order products.
- `SrcProbe` — a single ~997 Hz peak; trivial narrow-band FFT bin
  check.

This replaces an earlier plan to build a `scripts/` Python
audio-validation harness: in-process FFT tests are faster, avoid a
Python dependency, and don't require routing signal through an
intermediate WAV file.

## Blocks

The Inspector-side pattern decoders depend on this FFT landing
first:

- [proav/inspector-noise-spectral-check.md](../proav/inspector-noise-spectral-check.md)
- [proav/inspector-chirp-tracker.md](../proav/inspector-chirp-tracker.md)
- [proav/inspector-dualtone-imd.md](../proav/inspector-dualtone-imd.md)
