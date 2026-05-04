# Inspector: audio samples-per-frame check

Add a check in `InspectorMediaIO` that the number of audio samples
delivered per video frame is correct.

Trickier than it sounds: some frame rates have an audio cadence
pattern (e.g. 29.97 fps at 48 kHz: 1601 / 1602 / 1601 / 1602 / 1602
samples), and we can't assume any particular phase at the start of
inspection. The check has to learn the cadence over the first few
frames and then verify subsequent frames stay on it.

## Tasks

- [ ] First N frames: collect the per-frame sample counts.
- [ ] Match them against the closed-form cadence from
  `FrameRate::samplesPerFrame()` for the discovered phase.
- [ ] After phase lock, every subsequent frame must match the
  expected count for its slot in the pattern; report
  `InspectorEvent` discontinuities on mismatch.
- [ ] Tests: 30 / 29.97 / 25 / 50 / 59.94 / 60 fps at 48 / 96 kHz.
