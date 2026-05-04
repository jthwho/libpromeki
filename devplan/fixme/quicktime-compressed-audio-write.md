# QuickTimeWriter: compressed audio input path is missing

**File:** `src/proav/quicktime_writer.cpp` (`addAudioTrack`,
`writeSample` audio branch)

**FIXME:** The writer side treats audio samples as raw PCM only.
`addAudioTrack()` requires a PCM `AudioFormat`; it does not accept
an `AudioDesc` whose `AudioFormat` is a compressed entry (`Opus`,
`AAC`, …). The reader side handles compressed audio cleanly (after
the Phase 6 AudioDesc/Audio extensions), so this is an asymmetric
gap that blocks remux-style workflows (open a compressed source,
write it to a new container without transcoding).

## Tasks

- [ ] Update `addAudioTrack()` to accept an `AudioDesc` whose
  `AudioFormat::isCompressed()` is true.
- [ ] In `writeSample()` audio branch, when the track is compressed,
  write each sample as one variable-size entry (with its own
  duration / size / keyframe flags) rather than treating it as a
  constant-size PCM chunk.
- [ ] In `appendTrak()` stsd emission, when the track is compressed,
  emit the correct sample-entry form (ISO-BMFF `mp4a` / `Opus` /
  etc.) with any required extension atoms (`esds` for AAC, `dOps`
  for Opus, etc.) — at minimum a stub that carries the
  codec-specific config bytes supplied via the track metadata.
- [ ] Extend `QuickTimeMediaIO` writer path to accept compressed
  audio frames (currently refuses via the non-PCM guard in
  `setupWriterFromFrame`).
