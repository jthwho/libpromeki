# Random Ideas That Need Exploration

1. We have a way to convert a byte count to human in String (e.g. 10000000 to 10 MB).  We also
   have other bits in the library that compute human strings for time (e.g. 1.67 ms).  We ought
   to just put all this metric (or 1024 based) logic all in one place.  The units are kind of
   arbitrary, we could keep those w/ the caller.  *Plan is to land this as a new `Units` class
   (its own header) rather than stacking more statics on `String`, keeping `String` focused.
   Candidates for the first cut: byte counts (metric + IEC), durations (ns / µs / ms / s / m /
   h), frequencies (Hz / kHz / MHz / GHz), and sample counts → time at a given sample rate.
   Everything should take an explicit `ByteCountStyle` / `DurationStyle` / etc. enum so the
   caller picks the unit family and the formatter picks the scale automatically.*

2. We should add a VideoFormat object to the class.  This should class should take care of
   handling SMPTE/well know format names.  It should be very accepting of names (e.g. 1080p50,
   1920x1080 50p, 4Kp60, etc) but provide toString() names that try to be SMPTE names first and
   then more generic names if the VideoFormat isn't a SMPTE rate.  We should add the ability to
   get the VideoFormat from MediaDesc (should take an arg w/ the image index, default to 0).
   *Plan: lives in core (not proav) since `MediaDesc` is core.  Must also be registered as a
   Variant type.  The `InterlaceMode` enum that now lives on `ImageDesc` should feed into the
   `p`/`i` suffix so `VideoFormat::toString()` picks `1080i50` vs `1080p50` automatically.*

3. We should probably rename mediaplay to mediaio.  We should also add full documentation in
   docs for it and a man page.  The utils doc should link to this one.

4. We should add full documentation in docs for promeki-bench and add a man page.  The utils
   doc should link to this one.

5. It'd be cool to have the mediaplay (or mediaio when we rename) pipeline draw a nice text
   diagram of the pipeline.  Something like `Source[TPG] → Converter[JPEG] → Sink[QuickTime]`
   rendered in a box-and-arrow ASCII style, with each stage's active config overrides listed
   underneath.  Useful for `--help` style introspection and for the periodic `--stats` header.

6. Individual MediaIO backends that spawn their own threads outside the shared `MediaIO::pool()`
   should name those threads from `name()`.  No backend currently does this; it's a small
   drive-by change per backend when someone gets annoyed enough at `top` / `htop` showing
   unnamed worker threads.  (Left over from the larger "MediaIO takes a Name config option"
   work that otherwise landed via `MediaConfig::Name` + `MediaConfig::Uuid`.)

7. We need to work on AudioDesc to make it first class like the other *Desc object.  We should
   be able to use it as if it's an enum in Variant.  *Today `AudioDesc` is a plain value class
   holding sample rate / channels / format — unlike `PixelDesc`, `ColorModel`, `PixelFormat`
   which are registry-backed `TypeRegistry` wrappers with stable IDs and a Variant type.  Plan:
   pick whichever pieces of AudioDesc identify a "named" format (e.g. `PCMI_Float32LE @ 48k @
   stereo`), promote those to registered IDs, and add `TypeAudioDesc` to the Variant X-macro so
   config keys can use `setType(Variant::TypeAudioDesc)` directly.*

8. Inspector should grow a `PcmMarker` audio-channel decoder — the sample-domain inverse of the
   TPG's `AudioPattern::PcmMarker` generator.  The generator framing is already defined in
   `AudioTestPattern::kPcmMarker*` constants (16-sample alternating preamble, 8-sample start
   marker of four highs + four lows, 64-bit MSB-first payload at ±0.8, trailing parity bit at
   ±0.6).  The decoder should hunt for the preamble, latch the start marker, decode the 64-bit
   payload, validate parity, and report the decoded value to the Inspector event alongside the
   existing LTC / picture-data results.  When the payload is a BCD64 timecode it should parse
   via `Timecode::fromBcd64`, otherwise expose the raw 64-bit counter.  This is the audio
   pipeline's analog of the picture `ImageDataDecoder` and gives us sample-exact round-trip
   verification (bit flips, dropped / duplicated / reordered chunks all become visible).

9. Inspector should grow a `WhiteNoise` / `PinkNoise` sanity probe.  Both generators produce
   bounded-amplitude broadband signals; a channel configured as `WhiteNoise` or `PinkNoise`
   should pass a simple spectral tilt check (≈0 dB/octave for white, ≈-3 dB/octave for pink)
   and peak-level check.  The check is coarse by design — the goal is "is this channel in the
   right ballpark" rather than "is this the exact same buffer the TPG generated".  Depends on
   item #12 (library FFT) landing first.

10. Inspector should grow a `Chirp` sweep tracker.  Given the sweep's configured
    `start / end / duration`, the Inspector can compute the instantaneous expected frequency
    for each sample and verify the decoded audio matches within a tolerance.  A working
    tracker also catches sample drops (the tracked instantaneous frequency jumps forward),
    duplicated chunks (frequency flat-lines), and SRC drift (frequency scales by the ratio).
    Bigger project than the noise probes; probably worth building a dedicated `ChirpDecoder`
    helper class rather than inlining the math in Inspector.  Depends on item #12.

11. Inspector should grow a `DualTone` IMD measurement.  A DualTone channel carries two
    spectrally-isolated sines; the Inspector can FFT a chunk, confirm the two fundamentals
    are present at the expected frequencies, and compute the intermodulation distortion ratio
    from the third-order sideband products (f2±f1 at SMPTE IMD-1 ratios).  Real IMD analyzers
    are a rabbit hole — the first pass should just verify the two expected peaks are present
    and report their amplitudes.  Depends on item #12.

12. Add a real FFT to libpromeki so we can validate the audio TPG patterns spectrally from
    inside the unit tests (rather than via an external Python/scipy script).  At minimum we
    need a forward real-to-complex FFT with power-of-two sizes, a matching inverse, and
    Hann / Blackman-Harris windows; the existing `cscpipeline.cpp` Highway-based code is a
    reasonable model for how to vendor or wrap a kernel.  Candidates are kissfft (small,
    self-contained, permissive) or pffft (faster, slightly larger).  Once the FFT is in
    place, add unit tests for every TPG pattern that has a spectral property worth pinning:
    - `WhiteNoise` — spectrum is flat across octave bins within a few dB.
    - `PinkNoise` — each octave is ~3 dB below the next-lower octave (the Kellet filter
      ought to land within ~1 dB of the ideal slope).
    - `Chirp` — instantaneous frequency tracked over a windowed STFT matches the closed-
      form sweep within tolerance at several sample points, including across a period wrap
      (regression-locks the chirp phase-continuity fix from Phase 4).
    - `DualTone` — two dominant peaks at the configured `freq1` / `freq2`, amplitudes in
      the configured ratio, no unexpected third-order products.
    - `SrcProbe` — a single ~997 Hz peak; trivial to verify with a narrow-band FFT bin
      check.

    This replaces an earlier plan to build a `scripts/` Python audio-validation harness:
    in-process FFT tests are faster, avoid a Python dependency, and don't require us to
    route signal through an intermediate WAV file.  The Inspector-side pattern decoders
    (#9 / #10 / #11) depend on this FFT landing first.

13. Add an `AudioMeter` object to compute metering in a single pass over an `Audio` buffer
    (iterate once, compute everything we'd want to see).  The core metrics are obvious:
    - Per-channel peak (instantaneous plus an optional fall-back "peak hold" time)
    - Per-channel RMS over a configurable window
    - Per-channel clip detection (see below)
    - DC offset (running mean)

    Clip detection should be two-layered so it catches both "obviously broken" and "real-
    world PCM clipping":
    - **Over-full-scale**: any sample at or beyond ±1.0 (float) or the data type's full-
      scale value (integer).  This is the easy case — the signal has crossed a line it
      shouldn't.
    - **Plateau clipping**: N or more *consecutive* samples sitting at exactly the peak
      value, which is what a signal that's been hard-limited or analog-clipped actually
      looks like in the digital domain — the waveform doesn't overshoot, it plateaus at
      the limit for as long as it was trying to exceed it.  A single sample at ±full-
      scale is a normal signal peak; a run of them is a clip event.  Reasonable default
      for N is 2 or 3 samples; configurable so callers with dither-aware tooling can
      tighten or relax it.  The meter should report both the number of clip *events*
      (runs) and the length of the longest run.

    Stretch metrics once the basics are in place:
    - True Peak (oversampled peak detection — catches inter-sample peaks that exceed the
      sample-domain peak after D/A reconstruction; ITU-R BS.1770 calls for 4× oversampling)
    - LUFS / LKFS loudness (momentary / short-term / integrated, per EBU R128 / BS.1770)

    The class should be stateful so a caller can feed successive `Audio` chunks in and read
    out the accumulated numbers; a `reset()` clears the accumulators.  State is especially
    important for plateau clip detection — a clip run can span a chunk boundary, so the
    meter has to remember "the last sample of the previous chunk was at peak and we were
    N samples into a run" across chunks.  Fits alongside `BenchmarkReporter` /
    `MediaIOStats` in the "stateful telemetry primitives" layer.

14. Add graphical metering to the SDL viewer.  This should be its own `AudioMeterWidget` that
    takes an input from `AudioMeter` (item #13) and displays it.  We'd want to see clip
    marker, peak, and RMS.  It'd be nice to have LUFS/LKFS too, but that's something to
    explore later.  The widget should be configurable about scale (dBFS vs dB SPL offsets
    aren't our business, but dBFS range and reference gridlines are) and about layout
    (horizontal vs vertical meter strips), and should update at the SDL video frame rate
    rather than every audio chunk so a slow playback doesn't drown the GUI in redraw
    events.

15. Have the TPG encode the TPG config to JSON and stick that into the output frame's metadata.  We
    can use this to write the TPG details to the output file to make it easier to setup an inspector
    later down the road.

16. We should rename AudioBuffer to AudioFifo or AudioQueue

17. We need to investigate audio resamplers and build an AudioResample object.

18. Add a test in the Inspector to check that the number of audio samples per frame is correct.
    This gets tricky as some frame rates have a cadence to the audio and we can't assure we'll get
    in any particular phase.


