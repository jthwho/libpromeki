# MediaPipelineStats: brittle unit-suffix matching

**File:** `src/proav/mediapipelinestats.cpp:53` (`unitFor()`)

**FIXME:** Stat-name-to-unit picking is done by suffix matching on
the stat ID string (e.g. names ending in `Duration`, `Ms`,
`Bytes*`).  Backend keys that don't follow the
`Foo*Duration` / `Foo*Ms` / `Bytes*` convention silently fall
through to `Count` and render without units.

The suffix table works for everything currently registered, but it
is implicit project policy rather than an enforced contract: a new
backend that registers a stat under, say, `WriteLatencyNs` would
get no unit information attached.

## Tasks

- [ ] Hang an explicit unit hint off the `StringRegistry::Item`
  (or on `VariantSpec`) for each stat ID at registration time.
- [ ] Replace the suffix matching in `unitFor()` with a lookup of
  that hint.
- [ ] Audit existing stat IDs and tag each with the right unit
  category (`Duration`, `DurationMs`, `Bytes`, `BytesPerSec`,
  `FramesPerSec`, `Count`).
- [ ] Track this against the per-`StringRegistry`-entry metadata
  refactor.
