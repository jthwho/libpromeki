# BufferPool is available but not wired into any hot path

**Files:** `include/promeki/bufferpool.h`, `src/core/bufferpool.cpp`

**FIXME:** The `BufferPool` class was added during the Phase 6
efficiency batch as a reusable primitive for realtime reader/writer
hot paths, but it is intentionally not wired into
`QuickTimeReader::readSample()` (or anywhere else). For the current
workload (~30 allocations/sec for a single video reader) the
per-call allocation cost is well under 0.1% overhead, so wiring it
in blindly is premature optimization.

## Tasks

- [ ] Profile a realistic multi-stream / high-frame-rate workload
  and identify whether buffer allocation shows up in the hot path.
- [ ] If yes, wire `BufferPool` into `QuickTimeReader` as an optional
  opt-in.
- [ ] If no after multiple workloads, consider whether the class
  should be removed or kept as an available utility for other
  backends.
