# Fragmented MP4 reader: trex defaults emitted by writer but ignored by reader

**Files:** `src/proav/quicktime_reader.cpp` (`parseTraf` / `parseTrun`),
`src/proav/quicktime_writer.cpp` (`appendMvex`)

**FIXME:** The fragmented writer emits an `mvex` / `trex` box with
per-track default sample values, and our fragmented reader correctly
parses `tfhd` default overrides — but does not fall back to `trex`
defaults when `tfhd` does not supply them. Because our own writer
always writes `tfhd` overrides (via the Phase 6 audio trun
compression pass), our reader round-trips our own output correctly.
External fragmented MP4 files that rely purely on `trex` defaults
with a bare `tfhd` will read as zero-duration / zero-size samples.

## Tasks

- [ ] Parse `mvex` / `trex` during `parseMoov` and stash per-track
  defaults.
- [ ] In `parseTraf`, use the stashed `trex` defaults when the
  corresponding `tfhd` override flag is not set.
- [ ] Add a test with a synthesised fragmented MP4 that uses `trex`
  defaults without `tfhd` overrides, to verify the fallback path.
