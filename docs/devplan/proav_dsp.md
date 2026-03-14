# ProAV DSP and Effects

**Phase:** 4C
**Dependencies:** Phase 4A (MediaNode), Phase 4B (audio node conventions)
**Library:** `promeki-proav`
**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

---

## AudioFilter

Base for audio filters. Derives from MediaNode.

**Files:**
- [ ] `include/promeki/audiofilter.h`
- [ ] `src/audiofilter.cpp`
- [ ] `tests/audiofilter.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one audio input, one audio output
- [ ] `enum FilterType { LowPass, HighPass, BandPass, BandStop, Notch, AllPass, LowShelf, HighShelf, Peaking }`
- [ ] `void setFilterType(FilterType type)`
- [ ] `FilterType filterType() const`
- [ ] `void setFrequency(double hz)` — center/cutoff frequency
- [ ] `double frequency() const`
- [ ] `void setQ(double q)` — quality factor / resonance
- [ ] `double q() const`
- [ ] `void setGainDb(double db)` — for shelf and peaking filters
- [ ] `double gainDb() const`
- [ ] Override `configure()`:
  - [ ] Compute biquad coefficients based on type, frequency, Q, gain, sample rate
  - [ ] Allocate per-channel state buffers
- [ ] Override `process()`:
  - [ ] Apply biquad filter to each channel
  - [ ] Direct Form II Transposed implementation (numerically stable)
- [ ] `void recalculate()` — recompute coefficients when parameters change at runtime
- [ ] Internal: biquad coefficient calculation (Robert Bristow-Johnson's Audio EQ Cookbook formulas)
- [ ] Doctest: LowPass at known frequency, verify frequency response (simple: sine above cutoff should be attenuated)

---

## AudioResampler

Sample rate conversion.

**Files:**
- [ ] `include/promeki/audioresampler.h`
- [ ] `src/audioresampler.cpp`
- [ ] `tests/audioresampler.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one audio input, one audio output
- [ ] `enum Quality { Fast, Medium, Best }`
- [ ] `void setOutputSampleRate(uint32_t sampleRate)`
- [ ] `uint32_t outputSampleRate() const`
- [ ] `void setQuality(Quality quality)` — controls filter length
- [ ] Override `configure()`:
  - [ ] Read input sample rate from input port AudioDesc
  - [ ] Compute resampling ratio
  - [ ] Design anti-aliasing filter based on quality setting
  - [ ] Set output port AudioDesc with new sample rate
- [ ] Override `process()`:
  - [ ] Pull input frame
  - [ ] Apply polyphase interpolation/decimation
  - [ ] Handle fractional ratios (e.g., 44100 -> 48000)
  - [ ] Push resampled frame to output
- [ ] Internal: polyphase filter bank
  - [ ] Precompute filter coefficients at configure time
  - [ ] Quality settings map to filter lengths (e.g., Fast=16, Medium=64, Best=256 taps)
- [ ] Handle arbitrary ratios (not just integer multiples)
- [ ] Preserve channel count
- [ ] Doctest: resample 44100->48000, verify output sample count, basic signal integrity

---

## AudioFormatConverter

Sample format conversion (int16 <-> float32, etc.).

**Files:**
- [ ] `include/promeki/audioformatconverter.h`
- [ ] `src/audioformatconverter.cpp`
- [ ] `tests/audioformatconverter.cpp`

**Implementation checklist:**
- [ ] Derive from `MediaNode`, use `PROMEKI_OBJECT`
- [ ] Constructor: one audio input, one audio output
- [ ] `void setOutputFormat(AudioDesc::SampleFormat format)`
- [ ] `AudioDesc::SampleFormat outputFormat() const`
- [ ] Supported format conversions:
  - [ ] int16 <-> int32
  - [ ] int16 <-> float32
  - [ ] int16 <-> float64
  - [ ] int32 <-> float32
  - [ ] int32 <-> float64
  - [ ] float32 <-> float64
  - [ ] int24 (packed) <-> int32
  - [ ] int24 (packed) <-> float32
- [ ] Override `configure()`:
  - [ ] Set output port AudioDesc with new format
  - [ ] Select appropriate conversion function
- [ ] Override `process()`:
  - [ ] Pull input frame
  - [ ] Convert sample format
  - [ ] Push converted frame to output
- [ ] Dithering:
  - [ ] `void setDitheringEnabled(bool enable)` — for int->int downconversion
  - [ ] `enum DitherType { NoDither, RPDF, TPDF, NoiseShaping }`
  - [ ] `void setDitherType(DitherType type)`
  - [ ] TPDF dithering for int16 output (standard practice)
- [ ] Clipping/saturation handling for float -> int conversion
- [ ] Preserve channel count and sample rate
- [ ] Doctest: convert float32->int16->float32 round-trip, verify values within dither noise floor
