# SDL player should honour the pacer's drop-frame suggestion

`SDLPlayerTask` / `SDLPlayerWidget` are now built on `FrameSync`,
which already produces per-pull `framesRepeated` / `framesDropped`
counters and the `Clock`-driven cadence. The SDL player should
actually act on those signals when it falls behind — drop a frame
on the SDL render side rather than rendering everything FrameSync
emitted (which compounds latency on a slow display).

Today the player paints whatever comes off `pullFrame()`. When
`PullResult::framesDropped > 0` (drift/late) the player should skip
the SDL paint for the dropped slot rather than catch up by painting
back-to-back.

## Tasks

- [ ] Inspect `FrameSync::PullResult` in the player loop.
- [ ] When `framesDropped > 0` or the wake-up `error` exceeds a
  threshold (~ half a frame period), skip the SDL paint and the
  audio push for the dropped slot.
- [ ] Surface a stat (`StatsRenderFramesDropped`) so the player's
  drop count is distinct from the FrameSync drop count.
- [ ] Tests via `SyntheticClock` that simulate slow paints.
