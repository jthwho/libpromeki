# SdlSubsystem: 60 Hz wakeup prevents idle

**File:** `src/sdl/sdlsubsystem.cpp:60`

**FIXME:** `SdlSubsystem` starts a 16 ms (~60 Hz) periodic timer on
the main event loop that calls `SDL_PumpEvents()` so OS keyboard /
window / mouse events keep flowing even when the rest of the
application is quiescent.  This works, but it means the app never
goes idle — the main loop wakes 60 times a second forever, even
when the user isn't doing anything, costing battery and preventing
power-management heuristics from kicking in.

The wakeup chain we'd like (`watch → pipe → IoSource`) only fires
when something is *already* in SDL's queue, and only `SDL_PumpEvents`
moves OS events into that queue, hence the polling fallback.

## Tasks

- [ ] Spawn a dedicated SDL pump thread that blocks in
  `SDL_WaitEventTimeout()` (or the platform equivalent) and
  forwards an event into the main event loop only when an OS
  event actually arrives.
- [ ] Drop the 16 ms periodic timer in the idle case so the main
  loop can sleep when there is nothing to do.
- [ ] Verify on Linux / macOS / Windows that the threaded pump
  delivers keyboard / window / mouse events with the same latency
  as the current 60 Hz timer (or better).
- [ ] Make sure shutdown paths join the pump thread cleanly.
