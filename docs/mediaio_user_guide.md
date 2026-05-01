# MediaIO — User Guide {#mediaio_user_guide}

How to consume the `MediaIO` framework as an application author —
opening backends, reading and writing frames, wiring transfers,
handling cancellation, and observing state through signals.

For an introduction aimed at backend authors (writing a new
`MediaIO` subclass), see the
[MediaIO Backend Guide](mediaio_backend_guide.md).

## Concepts {#mediaio_user_concepts}

`MediaIO` is the abstract entry point for every media source, sink,
and transform — files, image sequences, capture cards, codecs,
and synthetic generators. Every public method is asynchronous;
every operation returns a [MediaIORequest](#mediaio_user_requests).
A backend's per-stream input/output points are exposed as
[ports](#mediaio_user_ports) (`MediaIOSource`, `MediaIOSink`)
grouped under [port groups](#mediaio_user_portgroups)
(`MediaIOPortGroup`). Transfers between MediaIOs are wired up with
[MediaIOPortConnection](#mediaio_user_connections).

The framework contracts you should care about as a user:

- Every public call returns immediately with a request handle.
- The cached state on `MediaIO` (`mediaDesc()`, `frameRate()`, …)
  is updated *before* signals fire and *before* request promises
  resolve, so a `.then()` callback always observes the post-update
  state.
- Cancellation is best effort. The framework guarantees pre-dispatch
  cancellation and supports interrupting blocking backend work; it
  cannot pre-empt a non-blocking `executeCmd` once it has started.

## Factory entry points {#mediaio_user_factories}

Three convenience helpers and one general factory:

```cpp
// 1. Construct from a config (most flexible).  Set
// MediaConfig::Type to the registered backend name.
MediaIO::Config cfg = MediaIOFactory::defaultConfig("TPG");
cfg.set(MediaConfig::VideoFormat, VideoFormat(VideoFormat::Smpte1080p30));
cfg.set(MediaConfig::VideoPixelFormat, PixelFormat(PixelFormat::RGBA8_sRGB));
MediaIO *io = MediaIO::create(cfg);

// 2. From a filename, for reading (extension or content probe).
MediaIO *io = MediaIO::createForFileRead("clip.mov");

// 3. From a filename, for writing (extension only).
MediaIO *io = MediaIO::createForFileWrite("output.dpx");

// 4. From a URL.  Looked up by scheme via
// MediaIOFactory::findByScheme.
MediaIO *io = MediaIO::createFromUrl("rtp://239.0.0.1:5000");
```

Backends are discovered through the `MediaIOFactory` registry.
`MediaIOFactory::registeredFactories()` returns every known
factory; `findByName` / `findByScheme` / `findForPath` are the
canonical lookups.

## Always-async API {#mediaio_user_requests}

`MediaIORequest` is the unified return type for every operation
that goes through the dispatch pipeline. It is a small handle to
shared state; copying or destroying it does not affect the
underlying request.

The four common consumption patterns:

```cpp
// (a) Block until the request resolves.
Error err = io->open().wait();
if (err.isError()) { ... }

// (b) Block with a timeout (milliseconds).  Returns Error::Timeout
//     if the deadline passes first.
Error err = io->source(0)->readFrame().wait(1000);

// (c) Attach a continuation that fires on the calling EventLoop
//     when the request resolves.  The callback is marshalled to
//     the EventLoop active at the time of the .then() registration,
//     so you observe the result on the thread you registered from.
io->open().then([](Error e) {
    if (e.isError()) { /* handle */ }
});

// (d) Recover the typed command for output payloads.  Use after
//     wait() returns Ok.
MediaIORequest req = io->source(0)->readFrame();
if (req.wait().isOk()) {
    const auto *cmd = req.commandAs<MediaIOCommandRead>();
    if (cmd != nullptr) {
        Frame::Ptr frame = cmd->frame;
        FrameNumber n    = cmd->currentFrame;
    }
}
```

For most consumers, the cached accessors on `MediaIO` /
`MediaIOPortGroup` (described below) are easier than reading typed
output fields directly — the framework copies the relevant outputs
into the cache as part of `completeCommand`.

## Lifecycle {#mediaio_user_lifecycle}

```cpp
// Pre-open setters (optional).  Nothing dispatches yet.
io->setExpectedDesc(myMediaDesc);
io->setExpectedMetadata(myMetadata);

// Open.  Blocks until the backend's open + port construction
// is finished (or the request is cancelled).
Error err = io->open().wait();
if (err.isError()) { return; }

// ... read or write frames via the backend's ports ...

// Close.  Blocks until the trailing EOS has propagated through
// the read cache and the closed signal has fired.
(void)io->close().wait();
delete io;
```

Async close: pass nothing to `close()` and use `.then()` for
notification, or attach to the `closed(Error)` signal:

```cpp
io->closedSignal.connect([](Error err) {
    if (err.isError()) { ... }
}, thisObject);
io->close();   // returns a request you can drop
```

While closing, new `readFrame` / `writeFrame` / `seekToFrame` /
`sendParams` calls return `Error::NotOpen` immediately. Reads
already in flight resolve normally, and exactly one synthetic
read result with `Error::EndOfFile` is appended to each source's
read cache so signal-driven consumers see one trailing
`frameReady` carrying EOS.

## Ports and port groups {#mediaio_user_ports}

A backend's open returns one or more [port groups](#mediaio_user_portgroups);
each group contains zero or more sources (`MediaIOSource`) and
zero or more sinks (`MediaIOSink`).

```cpp
io->open().wait();

// Most single-stream backends expose a single port group with one
// source (sources only) or one sink (sinks only).
MediaIOSource *src = io->source(0);
MediaIOSink   *snk = io->sink(0);

// Backends with multiple groups (e.g. a multi-track muxer) expose
// them via portGroup(i) and the group's source(i) / sink(i).
MediaIOPortGroup *grp = io->portGroup(0);
MediaIOSource    *vid = grp->source(0);
MediaIOSource    *aud = grp->source(1);
```

### Reading frames {#mediaio_user_read}

```cpp
MediaIORequest req = src->readFrame();
if (req.wait().isOk()) {
    const auto *cmd = req.commandAs<MediaIOCommandRead>();
    Frame::Ptr  frame = cmd->frame;
    // ... consume frame ...
}
```

Sources prefetch reads through `MediaIOReadCache` so steady-state
reads complete from cache without round-tripping to the backend.
Tune the prefetch depth via the source's accessors if a backend
has bursty latency; the default is whatever the backend declared
during `Open`.

The signal-driven equivalent:

```cpp
// frameReady fires whenever a result lands in the read cache.
// Signals declared with PROMEKI_SIGNAL are accessed via the
// `<name>Signal` member.
src->frameReadySignal.connect([src]() {
    MediaIORequest req = src->readFrame();
    req.then([](Error e) { /* consume cmd->frame in commandAs */ });
}, thisObject);
```

### Writing frames {#mediaio_user_write}

```cpp
io->setExpectedDesc(myDesc);
(void)io->open().wait();

MediaIORequest req = snk->writeFrame(frame);
(void)req.wait();
```

Async write errors surface through the `writeError(Error)` signal
on the sink (accessed as `snk->writeErrorSignal`). The sink's
`frameWanted` signal (accessed as `snk->frameWantedSignal`) fires
when the sink has capacity and is ready to consume more frames.

### Seeking {#mediaio_user_seek}

Seeking is a port-group operation since it changes the navigation
state of every port in the group:

```cpp
MediaIOPortGroup *grp = io->portGroup(0);
(void)grp->seekToFrame(FrameNumber(100)).wait();

// Explicit seek mode override.
(void)grp->seekToFrame(FrameNumber(100), MediaIO_SeekKeyframeBefore).wait();
```

`MediaIO_SeekDefault` resolves to whatever mode the backend
preferred at open time (exact for sample-accurate sources,
keyframe-before for compressed video, etc.).

`grp->setStep(2)` lets you skip every other frame, or
`grp->setStep(-1)` to play backwards. The argument is a plain
`int` — positive forwards, zero holds, negative reverses.

### Cached state and signals {#mediaio_user_cache}

`MediaIO` exposes cached descriptors that the framework keeps
fresh:

- `mediaDesc()`, `audioDesc()`, `metadata()`, `frameRate()`
- `defaultSeekMode()`, `isOpen()`, `isClosing()`

`MediaIOPortGroup`:

- `currentFrame()`, `frameCount()`, `canSeek()`
- `framesDropped()`, `framesRepeated()`, `framesLate()` (atomic
  counters)
- `frameRate()`

Signals declared with `PROMEKI_SIGNAL(name, ...)` are accessed
via the `nameSignal` member (e.g. `closedSignal.connect(...)`).
The signature columns below show the declared name and argument
types.

Per-MediaIO signals (on `MediaIO`):

- `closed(Error)` — async close completed
- `errorOccurred(Error)` — generic error
- `descriptorChanged()` — mid-stream MediaDesc update

Per-source signals (on `MediaIOSource`):

- `frameReady()` — a result landed in the read cache

Per-sink signals (on `MediaIOSink`):

- `frameWanted()` — sink has capacity, ready for more
- `writeError(Error)` — async write failed

Per-connection signals (on `MediaIOPortConnection` —
see [Wiring transfers](#mediaio_user_connections)):

- `upstreamDone()` — source reported end-of-stream
- `errorOccurred(Error)` — non-recoverable source-side error
- `sinkLimitReached(MediaIOSink *)` — sink hit its frame cap
- `sinkError(MediaIOSink *, Error)` — non-recoverable per-sink error
- `allSinksDone()` — every attached sink has stopped
- `stopped()` — connection's `stop()` ran

`MediaIOPortGroup` itself emits no signals — group-level state
(current frame, frame count, dropped / repeated counts) is read
from accessors on demand.

Read these and connect to them on the consumer thread. Cross-
thread connections are auto-marshalled by the signal/slot
system.

## Wiring transfers {#mediaio_user_connections}

`MediaIOPortConnection` glues a source to one or more sinks. The
common shapes:

```cpp
// 1:1 source → sink.  Convenience over the multi-sink form for
// the common standalone case (file-to-file copy, "save N frames"
// CLI helpers, test fixtures).
auto *conn = new MediaIOPortConnection(srcPort, sinkPort, parent);
(void)conn->start();

// 1:N fan-out.  Construct with the source, then attach each sink
// before calling start().  The sink set is frozen once start()
// has been called.
auto *conn = new MediaIOPortConnection(srcPort, parent);
(void)conn->addSink(sinkA);
(void)conn->addSink(sinkB);
(void)conn->addSink(sinkC);
(void)conn->start();
```

Per-sink frame caps go through the optional second argument to
`addSink`:

```cpp
auto *conn = new MediaIOPortConnection(srcPort, parent);
(void)conn->addSink(sinkA);                      // no cap
(void)conn->addSink(sinkB, FrameCount(120));     // cap at 120 frames
(void)conn->start();
```

The cap says "stop after N frames" and is honoured at the next
`Frame::isSafeCutPoint` on or after the limit-th write, so the
GOP / audio packet boundary containing the cap stays complete.
When the cap fires, that sink emits `sinkLimitReached` and stops
consuming; pumping continues for the remaining sinks. When every
sink has stopped (limit, error, or removed), the connection emits
`allSinksDone`. When the source itself reports trailing EOS, the
connection emits `upstreamDone` once. Empty / unknown
`FrameCount` (the default) disables the cap.

## Cancellation {#mediaio_user_cancel}

`MediaIORequest::cancel()` is best-effort with three states:

1. **Not yet dispatched** — the strategy class sees the
   cancellation flag set before calling the backend hook and
   short-circuits with `Error::Cancelled`. Reliable.
2. **In flight on a non-blocking backend** — cancellation is
   ignored; the operation runs to completion. The wrapping
   request still resolves with the real result, not Cancelled.
3. **In flight on a blocking backend** (e.g. file I/O, capture
   devices) — the framework signals the worker thread's
   blocking primitive (closing the fd, waking a condvar, setting
   a shutdown flag). The `executeCmd` call returns whichever
   error its interrupted syscall produced.

Use `cancel()` to abandon a request you no longer want; do not
treat it as a guaranteed pre-empt. To race-abort, hold the
request and call cancel from a different thread:

```cpp
MediaIORequest req = io->source(0)->readFrame();
// ... elsewhere ...
req.cancel();
(void)req.wait();   // resolves with whichever Error the strategy chose
```

## Parameterized commands {#mediaio_user_params}

For backend-specific RPCs (set device gain, query temperature,
trigger a one-shot, …), use `sendParams`. The `name` and the
`MediaIOParams` payload are entirely backend-defined:

```cpp
MediaIOParams params;
params.set(SomeBackend::ChannelID, 0);
MediaIORequest req = io->sendParams("SetGain", params);
if (req.wait().isOk()) {
    const auto *cmd = req.commandAs<MediaIOCommandParams>();
    double actual = cmd->params.getAs<double>(SomeBackend::ActualGainID, 0.0);
}
```

Backends that don't recognize `name` return `Error::NotSupported`.

## Stats {#mediaio_user_stats}

Two flavors of statistics:

- **Per-command** — every `MediaIORequest::stats()` carries
  `QueueWaitDuration` (submit→dispatch) and `ExecuteDuration`
  (the executeCmd call). Backends may add backend-specific keys.
- **Cumulative aggregate** — `io->stats()` returns a request
  whose `stats()` carries instance-wide counters (frames
  delivered, error counts, throughput, etc.). The stats command
  is urgent-flagged so polling does not block behind real I/O.

```cpp
MediaIORequest req = io->stats();
(void)req.wait();
MediaIOStats s = req.stats();
int64_t dropped  = s.getAs<int64_t>(MediaIOStats::FramesDropped, 0);
double  bps      = s.getAs<double>(MediaIOStats::BytesPerSecond, 0.0);
```

## Pipeline composition {#mediaio_user_pipeline}

For graphs of more than a couple of MediaIOs, see
[MediaPipeline](mediapipeline.md) — it wraps the MediaIO surface
with a planner that builds the right port connections, inserts
auto-bridges for format mismatches, and exposes a unified
lifecycle (`run`, `pause`, `close`). For one-off transfers,
`MediaIOPortConnection` is sufficient.

## Available backends {#mediaio_user_backends}

| Backend | Direction | Purpose |
|---------|-----------|---------|
| `TPG` | source | Synthetic test pattern (color bars + audio with embedded LTC + timecode + image data band). Defaults are tuned to feed the Inspector. |
| `Inspector` | sink | Frame validator/monitor — decodes the TPG image data band, tracks A/V sync, emits per-frame events and a periodic summary. See the [Inspector user guide](inspector.md). |
| `ImageFile` | both | Single image or numbered image sequence (DPX, Cineon, PNG, JPEG, JPEG XS, TGA, SGI, PNM, raw YUV). |
| `AudioFile` | both | libsndfile-backed audio file (WAV, FLAC, AIFF, …). |
| `QuickTime` | both | QuickTime/ISO-BMFF reader and writer. Compressed video samples ride as Annex-B bytes on the frame payload. |
| `Rtp` | both | RTP transport for video/audio/metadata streams (SDP-driven; SMPTE 2110 / AES67 friendly). |
| `MjpegStream` | sink | HTTP MJPEG multipart-streaming sink. |
| `V4L2` | source | Video4Linux2 capture device. |
| `CSC` | transform | Color-space / pixel-format converter. |
| `SRC` | transform | Audio sample-rate converter. |
| `Burn` | transform | Burn-in overlay generator (text/timecode) for QA. |
| `FrameSync` | transform | Frame-rate sync / re-clock. |
| `VideoEncoder` | transform | Generic video encoder (dispatches to the registered codec). |
| `VideoDecoder` | transform | Generic video decoder (codec auto-detected from the compressed payload). |
| `FrameBridge` | transform | Frame format adapter for stitching mismatched ports. |
| `RawBitstream` | sink | Writes the compressed payload of every frame verbatim — the elementary-stream mirror of the encoder output. |
| `NullPacing` | transform | Drop-everything pacing stage for soak testing. |
| `DebugMedia` | both | Diagnostic media file format for offline inspection. |

The TPG and Inspector are designed to be paired: a default-config
TPG produces everything a default-config Inspector knows how to
verify (image data band, BCD timecode, LTC audio, A/V sync), so
a smoke-test pipeline is two factory calls plus a connection.
