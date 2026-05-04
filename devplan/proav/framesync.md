# FrameSync, Clock, FramePacer replacement

**Status:** COMPLETE.

`Clock` (abstract base) plus `WallClock`, `SyntheticClock`, `MediaIOClock`,
and `SDLAudioClock` ship in `include/promeki/`; `FrameSync` and
`FrameSyncMediaIO` ship in `include/promeki/framesync.h` /
`framesyncmediaio.h`. `FramePacer` and `SDLPlayerOldTask` are deleted;
`SDLPlayerWidget` (built on `SDLPlayerTask` + `FrameSync`) is the
canonical SDL playback path. The `MediaConfig::SdlPlayerImpl` selector
key is gone.

The full design (Clock interface, SyntheticClock semantics, FrameSync
internals, the four-phase rollout plan) lives in git history (Phase 4p).
This document is retained as a stub; no new FrameSync work is planned
right now. Reopen if the API ever needs a follow-up.
