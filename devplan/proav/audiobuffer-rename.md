# Rename `AudioBuffer` to `AudioFifo` / `AudioQueue`

`AudioBuffer` is actually a thread-safe FIFO ring with optional
resampling, gain, remap, and metering — not a buffer in the
"contiguous container" sense. The name regularly confuses readers
who expect something `Buffer`-shaped.

Candidate names: `AudioFifo` (most literal) or `AudioQueue` (matches
existing `Queue<T>` naming conventions).

## Tasks

- [ ] Pick a name. `AudioFifo` is probably cleaner since `Queue`
  already names a generic primitive.
- [ ] Rename the class + header + source.
- [ ] Sweep call sites (NDI, FrameSync, V4L2, RTP audio path,
  AudioFile, SDLAudioOutput, tests).
- [ ] Update `MEMORY.md` notes that mention `AudioBuffer`.
- [ ] Update Doxygen group + the `docs/dataobjects.dox` table.
