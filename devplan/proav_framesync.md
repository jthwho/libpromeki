# FrameSync, Clock rename, and FramePacer replacement

## Status: Phases 1–3 SHIPPED; Phase 4 (FramePacer removal) pending

Phases 1–3 landed together.  The library now has a standalone `Clock`
abstraction (`Clock`, `WallClock`, `SyntheticClock`, `SDLAudioClock`)
and a `FrameSync` object that resyncs a source media stream (video +
audio) to a destination clock's cadence via repeat/drop + audio
resampling.  `SDLPlayerTask` (new) is built on FrameSync and ships as
`createSDLPlayer()`; the legacy FramePacer-based implementation has
been renamed `SDLPlayerOldTask` / `createSDLPlayerOld()` and is
retained for side-by-side validation until FrameSync is proven in the
wild.  The `MediaConfig::SdlPlayerImpl` key selects between them —
`"framesync"` (default) or `"pacer"` (deprecated).

Phase 4 (retire `FramePacer`, delete `SDLPlayerOldTask`, migrate any
other pacer users) is the remaining work — see [Phase 4 —
FramePacer removal](#phase-4--framepacer-removal) below.

## Goals

- Standalone `Clock` class (not pacer-specific), carrying a `ClockDomain`,
  an actual-vs-nominal rate ratio, and its own reporting jitter so
  downstream code can filter it appropriately.
- A `SyntheticClock` in the library that advances only when asked — useful
  both as FrameSync's "source-only" driver and as a general-purpose tool
  (tests, offline conversion, frame-count-driven pipelines).
- `FrameSync` converts a variable-rate / differently-clocked source into
  one output frame per destination tick, with video repeated/dropped and
  audio resampled. Operates identically against any `Clock`; there is no
  separate "source-only" mode — you just hand it a `SyntheticClock`.
- SDL playback gets a new `SDLPlayer` class (separate from the existing
  `SDLPlayerTask`) built on FrameSync, so we can validate it side-by-side
  without destabilizing the existing path. Once FrameSync is proven,
  `FramePacer` and `SDLPlayerTask`'s pacer path can be deleted.

## Non-goals

- No new threading model. FrameSync blocks on `pullFrame()`; callers run
  that loop in whatever thread is appropriate.
- No codec knowledge inside FrameSync. It sees decoded `Frame`s only.

---

## Phase 1 — Clock (SHIPPED)

### New file `include/promeki/clock.h`

```cpp
// Asymmetric bound on (reportedTime - trueTime). minError is the most
// negative the clock can read relative to ground truth (early bias);
// maxError is the most positive (late bias). Both are non-strict
// bounds — a real clock may occasionally exceed them, but they
// represent the expected operating envelope.
struct ClockJitter {
        Duration minError;      // ≤ 0 for a clock that can read early
        Duration maxError;      // ≥ 0 for a clock that can read late

        Duration span() const { return maxError - minError; }
        bool     isSymmetric() const { return minError == -maxError; }
};

class Clock {
        public:
                virtual ~Clock() = default;
                virtual ClockDomain domain() const = 0;

                // Smallest meaningful time step (the quantization floor).
                virtual int64_t     resolutionNs() const = 0;

                // Expected asymmetric error envelope on nowNs() samples.
                // {0, 0} for a perfect synthetic clock; near-zero for
                // steady_clock; for an audio-buffer-derived clock the
                // envelope is mostly one-sided (clock reads stale
                // between callbacks), e.g. {0, bufferPeriod}. Used by
                // FrameSync to size its rate-estimate filter window.
                virtual ClockJitter jitter() const = 0;

                virtual int64_t     nowNs() const = 0;
                virtual void        sleepUntilNs(int64_t targetNs) = 0;

                // Ratio of actual tick rate to nominal. 1.0 for locked
                // clocks; audio clocks return measured drain / nominal,
                // giving FrameSync a drift-correction signal.
                virtual double      rateRatio() const { return 1.0; }
};
```

- `name()` removed; use `domain().name()` in logs.
- `WallClock` in the same header/cpp. Domain = `ClockDomain::SystemMonotonic`,
  `jitter()` ~= 1 ns, `rateRatio()` = 1.0.

### `SyntheticClock` (new, `include/promeki/syntheticclock.h`)

A frame-count-driven Clock. The reported time is literally
`currentFrame × framePeriod`; there is no wall-time component. Useful for
any pipeline where each output frame is exactly one `FrameRate` period
(file writers, tests, offline conversion).

```cpp
class SyntheticClock : public Clock {
        public:
                SyntheticClock();
                explicit SyntheticClock(const FrameRate &);

                // Optional — override the default Synthetic domain with
                // a caller-registered per-instance domain if cross-stream
                // correlation matters.
                void setDomain(const ClockDomain &);

                void setFrameRate(const FrameRate &);
                const FrameRate &frameRate() const;

                // Frame-count is the authoritative state. nowNs() is
                // derived from it.
                void    setCurrentFrame(int64_t frame);
                int64_t currentFrame() const;
                void    advance(int64_t frames = 1);   // convenience

                ClockDomain  domain() const override;
                int64_t      resolutionNs() const override;
                ClockJitter  jitter() const override;   // {0, 0}
                int64_t      nowNs() const override;    // frame × period
                void         sleepUntilNs(int64_t) override;  // no-op
                double       rateRatio() const override;  // 1.0
};
```

Semantics:
- `nowNs()` returns `currentFrame × frameDuration.nanoseconds()`.
- `setCurrentFrame(n)` sets the counter; subsequent `nowNs()` reflects it.
- `advance(n)` adds `n` to the counter (same effect as setCurrentFrame
  with the incremented value, just more ergonomic).
- `sleepUntilNs()` is a no-op — the counter is not touched. Time only
  moves when the caller advances the frame counter. FrameSync calls
  `advance(1)` after emitting each output frame, which is exactly "one
  output frame = one period."
- `jitter()` = {0, 0}, `rateRatio()` = 1.0, `resolutionNs()` = 1.
- `reset()` sets the counter to 0; `setCurrentFrame(N)` lets a caller
  phase-align to any point on the synthetic timeline.

### `SDLAudioPacerClock` → `SDLAudioClock`

- Registers a per-device domain at construction:
  `ClockDomain::registerDomain("sdl.audio:<deviceName>", desc, ClockEpoch::PerStream)`.
- `domain()` returns that registered domain.
- `rateRatio()` returns measured drain rate / nominal `bytesPerSec`,
  low-pass-filtered over ~1 s.
- `jitter()` reflects reality: on the order of the audio buffer period
  (SDL callback granularity) plus quantization to sample boundaries.
  FrameSync uses this to widen its rate-estimate filter window.

### `FramePacer`

- Change `setClock()` to take `Clock *`; drop `FramePacerClock` entirely
  (no alias). No callers outside the pacer use it yet.
- Internal `WallPacerClock` renamed / replaced by `WallClock`.
- Pacer stays for now; gets retired in Phase 4 once callers move to
  FrameSync.

---

## Phase 2 — FrameSync (SHIPPED)

### Header: `include/promeki/framesync.h`, source: `src/proav/framesync.cpp`

A single object with a single mode. The choice of `Clock` determines the
behaviour: `WallClock` = wall-paced; `SDLAudioClock` = audio-paced;
`SyntheticClock` = pristine frame-count output (the "source-only" use case).

### Public surface

```cpp
class FrameSync {
        public:
                struct PullResult {
                        Frame    frame;
                        int64_t  frameIndex = 0;
                        int64_t  framesRepeated = 0;   // since last pull
                        int64_t  framesDropped  = 0;
                        Duration error;                // wake-up error
                };

                FrameSync();
                explicit FrameSync(const String &name);
                ~FrameSync();

                void setTargetFrameRate(const FrameRate &);
                void setTargetAudioDesc(const AudioDesc &);
                void setClock(Clock *);          // required; not owned
                void setInputQueueCapacity(int);

                void reset();
                void reset(int64_t originNs);    // phase-aligned (clock domain)

                // Producer side (thread-safe).
                Error pushFrame(const Frame &);
                void  pushEndOfStream();

                // Consumer side. Blocks until the clock's next deadline;
                // emits exactly one Frame aligned to that tick.
                Result<PullResult> pullFrame();

                // Unblock a pullFrame() that is sleeping.
                void interrupt();

                // Stats.
                int64_t framesIn() const;
                int64_t framesOut() const;
                int64_t framesRepeated() const;
                int64_t framesDropped() const;
                Duration accumulatedError() const;
                double   currentResampleRatio() const;
};
```

### Internal model

- **Input queue**: `Queue<Frame>` with a bounded capacity. Overflow policy:
  drop-oldest by default (FrameSync is inherently lossy); configurable.
- **Timeline**: rational deadlines from an origin captured on the first
  pull, or set explicitly via `reset(originNs)`. Same approach as the
  pacer, lifted straight over.
- **Video selection per pull**:
  - Drain queue to the frame whose video `MediaTimeStamp` (or, absent
    timestamps, queue position × source nominal period) best matches this
    pull's deadline.
  - Drained-but-not-emitted frames count as drops; re-emissions of the
    last frame count as repeats.
  - Empty queue → repeat the last emitted frame, or an empty frame on the
    very first pull before any push.
- **Audio resampling**:
  - Accumulate input audio into a running buffer keyed on the audio
    `MediaTimeStamp` of the first-pushed frame.
  - Target sample count per pull = `targetAudioDesc.sampleRate × framePeriod`
    (fractional residue absorbed by the resampler).
  - Resample ratio = `sourceActualRate / destActualRate`:
    - `sourceActualRate` from audio `MediaTimeStamp` deltas, low-pass
      filtered with a window sized to `clock->jitter().span()` plus the
      source's own jitter. All inputs have real timestamps — MediaIO
      guarantees every essence carries a valid `MediaTimeStamp`.
    - `destActualRate = targetAudioDesc.sampleRate × clock->rateRatio()`.
  - With a `SyntheticClock`, `rateRatio()` = 1.0 and the ratio collapses
    to pure source-to-target SRC.
  - `AudioResampler` configured lazily on the first pushed audio (channel
    count known then).
- **Frame-count advance on SyntheticClock**:
  FrameSync treats SyntheticClock specially in one place only: after
  emitting each output frame, it calls `clock->advance(1)` if the clock
  is a SyntheticClock (dynamic_cast once on setClock, cache the pointer).
  This is what ties output frame count to clock time for the pristine
  "source-only" use case.
- **Output timestamps**:
  - `MediaTimeStamp` in `clock->domain()`, value = deadline.
  - For `SyntheticClock`, that's the Synthetic domain with monotone
    frame-count-derived values — exactly the semantics a file writer
    needs.
- **Debug**: periodic 1 Hz log with input/output actual rates, current
  resampler ratio, repeat/drop totals, accumulated error, clock name and
  jitter.

### Dependencies

- Uses existing `AudioResampler` (libsamplerate) unchanged.
- Uses `Frame`, `Audio`, `Image`, `MediaTimeStamp`, `ClockDomain`,
  `AudioDesc`, `FrameRate`.

---

## Phase 3 — SDLPlayer (SHIPPED)

Implemented as `SDLPlayerTask` / `createSDLPlayer()` in
`include/promeki/sdl/sdlplayer.h`; the legacy path is preserved as
`SDLPlayerOldTask` / `createSDLPlayerOld()` (deprecated) pending
Phase 4.

Rather than retrofit `SDLPlayerTask`, introduce a new `SDLPlayer` class
alongside it, built on FrameSync. This lets us validate the FrameSync path
end-to-end without touching working code.

### Surface (sketch)

```cpp
class SDLPlayer : public MediaIOTask {
        public:
                SDLPlayer(SDLVideoWidget *video, SDLAudioOutput *audio,
                          bool useAudioClock = true);
                ~SDLPlayer() override;
                // ... same externals as SDLPlayerTask ...
};

MediaIO *createSDLPlayer2(SDLVideoWidget *, SDLAudioOutput *,
                          bool useAudioClock = true,
                          ObjectBase *parent = nullptr);
```

### Wiring on open

```
_clock = useAudioClock && _audioOutput
           ? new SDLAudioClock(_audioOutput, bytesPerSec)
           : new WallClock();
_sync.setTargetFrameRate(_frameRate);
_sync.setTargetAudioDesc(_audioDesc);
_sync.setClock(_clock);
_sync.reset();
// Spawn strand-owned pull thread.
```

### Threading

- `executeCmd(Write)` on the MediaIO strand pushes into FrameSync — non-blocking.
- A new owned pull thread loops on `_sync.pullFrame()`:
  - Stashes the frame's image under `_pendingMutex`, posts `renderPending()`
    to the main thread (existing plumbing reused).
  - Pushes the frame's audio to `_audioOutput`.
  - Updates presentation stats.
- Close path: `pushEndOfStream()` → `interrupt()` → join pull thread →
  delete clock.

Once FrameSync is proven, `SDLPlayerOldTask` and its pacer path
are deleted, and the `SdlPlayerImpl` config key is retired.

---

## Phase 3.5 — MediaIOTask_FrameSync (SHIPPED)

`MediaIOTask_FrameSync` (`include/promeki/mediaiotask_framesync.h`,
`src/proav/mediaiotask_framesync.cpp`) wraps `FrameSync` as a
registered `MediaIO` backend (name `"FrameSync"`, `canInputAndOutput`
only).  Write side calls `FrameSync::pushFrame()`; read side calls
`FrameSync::pullFrame()`.  Default clock is a built-in `SyntheticClock`
so the backend runs non-blocking for offline / file pipelines;
`setClock(Clock *)` substitutes an external clock before `open()` for
real-time use.  `frameSync()` exposes the underlying `FrameSync`
instance for callers that construct the task directly via
`MediaIO::adoptTask()`.

Config keys: `OutputFrameRate` (invalid = inherit source),
`OutputAudioRate` (0 = inherit), `OutputAudioChannels` (0 = inherit),
`OutputAudioDataType` (Invalid = inherit), `InputQueueCapacity`
(default 8).  All four new `MediaConfig` IDs are declared in
`mediaconfig.h`.

Stats: `FramesPushed`, `FramesPulled`, `FramesRepeated`, `FramesDropped`.

Tests: 13 cases in `tests/mediaiotask_framesync.cpp`.

---

## Phase 4 — FramePacer removal (PENDING)

Remaining after this changeset:

1. ~~Land Clock + SyntheticClock + SDLAudioClock rename.~~ (shipped)
2. ~~Land FrameSync with unit tests.~~ (shipped — `tests/framesync.cpp`)
3. ~~Land the new SDLPlayer alongside the old one; manual smoke test
   both.~~ (shipped — `createSDLPlayer` / `createSDLPlayerOld`;
   `mediaplay` selects via `MediaConfig::SdlPlayerImpl`.)
4. ~~Land `MediaIOTask_FrameSync` for pipeline use.~~ (shipped — `tests/mediaiotask_framesync.cpp`)
5. Migrate any other pacer users (RTP sender) to FrameSync.
6. Delete `FramePacer`, `SDLPlayerOldTask`, `createSDLPlayerOld`, the
   `MediaConfig::SdlPlayerImpl` key, and the pacer-specific glue.

---

## Resolved decisions

- Input queue capacity default: 8 frames.
- All inputs have real `MediaTimeStamp`s (MediaIO invariant); no
  fallback-to-nominal logic in FrameSync.
- `jitter()` returns a `ClockJitter` struct with asymmetric
  `minError` / `maxError` Durations.
- `SyntheticClock` is frame-count authoritative: `setCurrentFrame()` /
  `advance()` are the only things that move time; `sleepUntilNs()` is a
  no-op. FrameSync advances the counter by 1 per emitted output frame.
