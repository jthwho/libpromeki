# Inspector: `DualTone` IMD measurement

`InspectorMediaIO` should grow an intermodulation-distortion check
for channels carrying TPG's `AudioPattern::DualTone`. The channel
carries two spectrally-isolated sines; the Inspector can FFT a
chunk, confirm the two fundamentals are present at the expected
frequencies, and compute the IMD ratio from the third-order
sideband products (f2 ± f1 at SMPTE IMD-1 ratios).

Real IMD analysers are a rabbit hole — first pass should just verify
the two expected peaks are present and report their amplitudes;
proper third-order analysis comes later.

**Depends on:** [core/fft.md](../core/fft.md).

## Tasks

- [ ] FFT chunk, locate peaks at the configured `freq1` / `freq2`,
  assert amplitudes are within a tolerance of the configured ratio.
- [ ] Report peak amplitudes and the (best-effort) third-order
  sideband levels in the Inspector event.
- [ ] Stretch: full SMPTE IMD-1 calculation once the basics work.
