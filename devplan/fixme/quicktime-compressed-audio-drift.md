# QuickTimeMediaIO: compressed audio pull rate drifts

**File:** `src/proav/mediaiotask_quicktime.cpp`
(`executeCmd(MediaIOCommandRead)` audio branch)

**FIXME:** When reading a file with compressed audio (AAC-in-MP4,
Opus, etc.), `QuickTimeMediaIO` currently pulls exactly one audio
packet per video frame read. This is correct for the PCM path
(where `FrameRate::samplesPerFrame()` gives the exact PCM frame
count for the current video frame), but for variable-duration
compressed audio packets the strategy drifts. Example: AAC at 48 kHz
has 1024-sample packets (~21.3 ms); video at 30 fps has 33.3 ms per
frame → we fall behind by about half a packet per video frame. Over
a 1-minute file the audio would end ~15 seconds short of the video.

## Tasks

- [ ] Replace the "one packet per video frame" heuristic with a
  dts-walking strategy: compute the target end-of-frame time in the
  audio track's timescale, then read audio samples while their
  cumulative dts is below that target. Requires per-sample
  dts/duration access on the sample index — already available for
  non-compact paths.
- [ ] Update the AAC-in-MP4 round-trip test to verify A/V sync over
  a longer duration (e.g. 2+ seconds).
