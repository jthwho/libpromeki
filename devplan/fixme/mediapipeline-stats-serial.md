# MediaPipeline stats collection serialises across stages

**File:** `src/proav/mediapipeline.cpp:1197`
(`MediaPipeline::buildStageStats()`)

**FIXME:** `buildStageStats()` issues `io->stats()` and immediately
`req.wait()`s on the returned `MediaIORequest`.  Each stats request
round-trips synchronously through the stage's strand, so an
N-stage pipeline pays N sequential strand turnarounds when
collecting cumulative stats.  For larger graphs this serialises
stats collection unnecessarily — a single stage stuck behind a
long-running command stalls every stage past it in the iteration
order.

## Tasks

- [ ] Issue all `io->stats()` requests up front in a first pass
  (no waiting), so each stage's strand starts work concurrently.
- [ ] Await the returned requests in a second pass and fold each
  result into the corresponding `MediaPipelineStageStats`.
- [ ] Add a benchmark / regression test for stats latency on a
  multi-stage pipeline so future refactors don't quietly
  re-serialise.
- [ ] Track this under the broader pipeline-stats latency pass.
