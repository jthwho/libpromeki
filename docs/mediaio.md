# Media I/O Subsystem {#mediaio}

Architecture and authoring guide for the MediaIO framework.

The MediaIO subsystem provides a uniform interface for reading and
writing media — files, image sequences, capture cards, decoders —
through a small set of cooperating classes. This page covers the
overall architecture, the threading model, the public API, and how
to write a new backend.

## Available backends {#mediaio_backends}

The library ships with the following `MediaIOTask` backends.
Each is registered via `PROMEKI_REGISTER_MEDIAIO` and can be
created by name through `MediaIO::create` or one of the
filename factories below.

| Backend            | Direction      | Purpose                                                                                                  |
|--------------------|----------------|----------------------------------------------------------------------------------------------------------|
| `TPG`              | source         | Synthetic test pattern generator — colour bars + audio (default `AudioPattern::AvSync`, which embeds LTC on the LTC channel + a click marker on the others) + timecode + a `ImageDataEncoder` data band stamped at the top of every frame. |
| `Inspector`        | sink           | Frame validator / monitor — decodes the TPG image data band, decodes audio LTC, reports A/V sync offset, tracks frame number / stream ID / TC continuity, and emits both per-frame events and a periodic summary log. See the [Inspector user guide](inspector.md). |
| `ImageFile`        | both           | Single image or numbered image sequence (DPX, Cineon, PNG, JPEG, JPEG XS, TGA, SGI, PNM, raw YUV). Compressed formats (JPEG / JPEG XS) deliver the bitstream as `Image::packet` on the loaded compressed Image. |
| `AudioFile`        | both           | Single audio file via libsndfile (WAV, FLAC, AIFF, ...).                                                 |
| `QuickTime`        | both           | QuickTime / ISO-BMFF reader and writer (fragmented or moov-at-end layout); H.264 / HEVC samples are delivered as Annex-B bytes on `Image::packet` for compressed tracks. |
| `Rtp`              | both           | RTP transport for video / audio / metadata streams (SDP-driven; SMPTE 2110 / AES67 friendly). Compressed payloads ride on `Image::packet` so the downstream decoder stage can consume them directly. |
| `V4L2`             | source         | Video4Linux2 capture device (webcams, capture cards exposing a V4L2 node).                               |
| `CSC`              | transform      | Colour-space / pixel-format converter stage (replaces the deprecated `Converter`).                       |
| `SRC`              | transform      | Stand-alone audio sample-rate converter stage.                                                           |
| `Burn`             | transform      | Burn-in overlay generator (text / timecode) for QA and review.                                           |
| `FrameSync`        | transform      | Frame-rate sync / re-clock stage.                                                                        |
| `VideoEncoder`     | transform      | Generic video-encoder stage that dispatches to the registered `VideoEncoder` backend for the configured `MediaConfig::VideoCodec`. Emits one compressed `Image` per encoded access unit with its `Image::packet` populated. |
| `VideoDecoder`     | transform      | Generic video-decoder stage — consumes the `Image::packet` from each compressed input `Image` and emits uncompressed `Image` frames on the other side. Auto-detects the codec from the packet's `MediaPacket::pixelDesc` when `MediaConfig::VideoCodec` is unset. |
| `RawBitstream`     | sink           | Writes every compressed `Image`'s attached `MediaPacket` payload verbatim to a file — the elementary-stream mirror of the encoder output. |

The TPG and Inspector are designed to be used together with no
extra configuration: a default-config TPG produces everything the
default-config Inspector knows how to verify (image data band,
BCD timecode, LTC audio, A/V sync), so a smoke-test pipeline is
just two factory calls plus a frame loop. See
[Inspector — Frame validation and monitoring](inspector.md) for
the full walk-through.

## Compressed bitstream flow {#mediaio_compressed}

Compressed essence rides through the pipeline as a compressed
`Image` (or `Audio`) carrying its encoded bytes on an
attached `MediaPacket` — `Image::packet` is the canonical
location for the bitstream, PTS / DTS, and codec-level flags
(keyframe, parameter-set, end-of-stream). There is no separate
per-Frame packet list: producer stages (VideoEncoder, container
demuxers, RTP readers, ImageFile loaders) attach the packet to
the Image they emit, and consumer stages (VideoDecoder,
RawBitstream, container muxers) read it off the compressed Image
they receive. Plane 0 and the packet's BufferView share the
same backing buffer when the producer was able to arrange zero
copy, so the extra wrapping is free on the hot path.

## Overview {#mediaio_overview}

MediaIO is split into two cooperating layers:

- **MediaIO** is the public-facing controller. It owns a command
  queue, manages caching, emits signals, and forwards I/O work to a
  backend. Users construct one of these (via factory methods),
  configure it, open it, and call `readFrame` / `writeFrame` /
  `seekToFrame`. `MediaIO` is an `ObjectBase` so it can emit
  signals; it is **not** intended to be subclassed.

- **MediaIOTask** is the backend interface. Concrete backends
  (test pattern generator, image file, audio file, video file,
  capture device, ...) inherit from `MediaIOTask` and override its
  private `executeCmd` virtuals. Tasks are never used directly;
  they are always driven by a MediaIO controller.

The two are kept honest through C++ friendship: `MediaIOTask`
declares its virtuals `private` and `friend`s `MediaIO`, so
only `MediaIO` can dispatch commands to a task. Backends override
private virtuals (which is legal in C++); external callers cannot.

## Architecture {#mediaio_architecture}

### Command pattern {#mediaio_command_pattern}

Every operation — open, close, read, write, seek, params — is
dispatched as a `MediaIOCommand` subclass. The hierarchy:

- `MediaIOCommand` (base — refcount, promise, type tag)
  - `MediaIOCommandOpen`
  - `MediaIOCommandClose`
  - `MediaIOCommandRead`
  - `MediaIOCommandWrite`
  - `MediaIOCommandSeek`
  - `MediaIOCommandParams`

Each command carries:

- **Inputs** (set by MediaIO) — the parameters of the operation
- **Outputs** (set by the task) — results to feed back into the
  MediaIO cache

For example, `MediaIOCommandOpen` has inputs like `mode` and
`config` and outputs like `mediaDesc`, `canSeek`, `frameCount`,
`defaultPrefetchDepth`, etc. The task fills these in during
`executeCmd`; MediaIO copies them into its cache when the call
returns.

Commands are owned via `SharedPtr<MediaIOCommand, false>` with
COW disabled — the polymorphic SharedPtr proxy path manages
lifetime. Use `PROMEKI_MEDIAIO_COMMAND()` to inject the required
boilerplate (the `type()` override and an asserting
`_promeki_clone()`).

### Strand-based serialization {#mediaio_strand}

`MediaIO` holds a per-instance `Strand` backed by a static shared
`ThreadPool`. All commands for a given `MediaIO` instance run
serially on the strand — no two commands for the same task ever
execute concurrently — but multiple MediaIO instances run
independently on the pool's threads.

Each command is a separate pool task, so threads are returned to
the pool between commands rather than held for the lifetime of
the MediaIO. See `Strand` for details.

### Lock-free data flow {#mediaio_data_flow}

MediaIO follows a strict rule: **the task never exposes state that
MediaIO needs to read concurrently**. All reportable values
(mediaDesc, frameRate, currentFrame, ...) live on MediaIO and are
updated by copying from command outputs *on the user thread*,
after the command's future has resolved. The worker thread only
writes to the command struct; the user thread only reads from the
cache. No mutexes guard the cache.

This works because of the future's happens-before guarantee:
everything the worker wrote to the command before fulfilling the
promise is visible on the user thread after `future.result()`
returns.

### Threading model {#mediaio_threading}

MediaIO is intended to be driven from **a single user thread**.
Public methods (`open`, `close`, `readFrame`, `writeFrame`,
`seekToFrame`, `setStep`, `setPrefetchDepth`, ...) are not
safe to call concurrently from multiple threads.

Cross-thread notifications use the signal/slot system, which
automatically marshals signals to the receiver's `EventLoop`:

- `frameReady` — emitted whenever a `CmdRead` completes (success,
  EOF, or error). Consumers drain the ready queue with repeated
  `readFrame(frame, false)` calls; the queued result carries
  whatever error code the backend returned.
- `frameWanted` — emitted by the worker when a `CmdWrite` succeeds.
- `writeError` — emitted by the worker when an async `CmdWrite` fails
  (the only way to learn about non-blocking write errors).
- `errorOccurred` — generic error signal.

## User API {#mediaio_user_api}

### Creating an instance {#mediaio_create}

Three factory methods, depending on what you know:

```cpp
// 1. By type and config (most flexible).  TPG's default config
// already enables video, audio, and timecode, so the plain form
// is enough for a ready-to-use reference stream.
MediaIO::Config cfg = MediaIO::defaultConfig("TPG");
MediaIO *io = MediaIO::create(cfg);

// 2. From a filename, for reading (extension or content probe)
MediaIO *io = MediaIO::createForFileRead("clip.mov");

// 3. From a filename, for writing (extension only)
MediaIO *io = MediaIO::createForFileWrite("output.dpx");
```

### Open / close lifecycle {#mediaio_lifecycle}

```cpp
io->setExpectedDesc(myMediaDesc);  // optional, pre-open setters
io->setExpectedMetadata(myMetadata);
io->setVideoTracks({0});           // optional track selection

Error err = io->open(MediaIO::Source);
if(err.isError()) { ... }

// ... read/write frames ...

io->close();
delete io;
```

### Reading frames {#mediaio_read}

```cpp
Frame::Ptr frame;
Error err = io->readFrame(frame);          // blocking
Error err = io->readFrame(frame, false);   // non-blocking → TryAgain
if(io->frameAvailable()) { ... }           // poll for ready frame
```

MediaIO maintains a configurable read prefetch depth (set via
`setPrefetchDepth`, default per task) so that bulk reads can be
pipelined.

### Writing frames {#mediaio_write}

```cpp
io->setExpectedDesc(myDesc);
io->open(MediaIO::Sink);
io->writeFrame(frame);                     // blocking
io->writeFrame(frame, false);              // fire-and-forget
io->close();
```

Non-blocking writes report errors only via the `writeError` signal.

### Async close {#mediaio_async_close}

`close(bool)` defaults to blocking. Pass `false` to return
immediately and learn completion via the `closed` signal:

```cpp
io->closedSignal.connect([](Error err) {
        if(err.isError()) { ... }
}, thisObject);
io->close(false);   // returns Ok immediately
// ... pipeline keeps running; eventually closedSignal fires.
```

Behaviour during an async close:

- Any reads/writes submitted before the `close` call run to
  completion on the strand — blocking callers still unblock
  with the real result, prefetched reads land in the ready
  queue, and outgoing writes complete normally.
- New `readFrame` / `writeFrame` / `seekToFrame` /
  `sendParams` calls return `Error::NotOpen` (the
  `isClosing` flag gates them).
- Exactly one synthetic `readFrame` result with
  `Error::EndOfFile` is appended to the ready queue after the
  real trailing frames, so signal-driven consumers see one last
  `frameReady` carrying EOS. Cached descriptor accessors
  (`mediaDesc`, `frameRate`, ...) must not be read between
  the `close` call and the `closed` signal.

### Seeking {#mediaio_seek}

```cpp
io->seekToFrame(100);                              // SeekDefault
io->seekToFrame(100, MediaIO::SeekKeyframeBefore); // explicit mode
```

`SeekDefault` is resolved per-task: exact for sample-accurate
sources, keyframe-before for compressed video, etc. Seeks
automatically discard any prefetched reads from the old position.

### Parameterized commands {#mediaio_params}

For backend-specific operations (set device gain, query temperature,
trigger one-shot, ...), use `sendParams`. The params and result
use the `MediaIOParams` `VariantDatabase` type (distinct from
`MediaConfig` and `MediaIOStats` so key names don't collide):

```cpp
// Backend defines its own IDs as static members:
// static const MediaIOParamsID ChannelID;     // "Channel"
// static const MediaIOParamsID ActualGainID;  // "ActualGain"

MediaIOParams params;
params.set(MediaIOTask_Foo::ChannelID, 0);
MediaIOParams result;
Error err = io->sendParams("SetGain", params, &result);
if(err.isOk()) {
        double actual = result.getAs<double>(MediaIOTask_Foo::ActualGainID);
}
```

The meaning of `name` and the layout of `params` / `result` are
entirely backend-defined.

## Authoring a backend {#mediaio_authoring}

### Setup {#mediaio_authoring_setup}

1. Create a header in `include/promeki/mediaiotask_<name>.h` that
   declares your subclass of `MediaIOTask`.
2. Create a source in `src/proav/mediaiotask_<name>.cpp`.
3. Add both to `CMakeLists.txt`.
4. Provide a static `formatDesc()` returning `MediaIO::FormatDesc`
   and register with `PROMEKI_REGISTER_MEDIAIO(YourClass)`.
5. Override the private `executeCmd` virtuals you need.

### Skeleton {#mediaio_authoring_template}

```cpp
class MediaIOTask_Foo : public MediaIOTask {
public:
    static const MediaIO::ConfigID ConfigSomeOption;

    static MediaIO::FormatDesc formatDesc();
    MediaIOTask_Foo() = default;
    ~MediaIOTask_Foo() override = default;

private:
    Error executeCmd(MediaIOCommandOpen &cmd) override;
    Error executeCmd(MediaIOCommandClose &cmd) override;
    Error executeCmd(MediaIOCommandRead &cmd) override;
    Error executeCmd(MediaIOCommandWrite &cmd) override;
    Error executeCmd(MediaIOCommandSeek &cmd) override;
    Error executeCmd(MediaIOCommandParams &cmd) override;

    // Backend-internal state — only touched from executeCmd, never
    // exposed to MediaIO directly.
    SomeFileHandle  _file;
    // ...
};
```

### Open / close contract {#mediaio_authoring_open}

In `executeCmd(MediaIOCommandOpen&)`, read the inputs and fill in
the outputs that describe what you have:

```cpp
Error MediaIOTask_Foo::executeCmd(MediaIOCommandOpen &cmd) {
    // ... open the resource using cmd.config and cmd.mode ...
    if(failed) return Error::OpenFailed;

    // Populate cache outputs
    cmd.mediaDesc           = builtMediaDesc;
    cmd.frameRate           = 24.0;
    cmd.canSeek             = true;
    cmd.frameCount          = totalFrames;
    cmd.defaultStep         = 1;
    cmd.defaultPrefetchDepth = 1;        // 1 for files, more for devices
    cmd.defaultSeekMode     = MediaIO_SeekExact;
    return Error::Ok;
}
```

**Open-failure cleanup contract**

If your `executeCmd(MediaIOCommandOpen&)` returns an error, `MediaIO`
will automatically dispatch `executeCmd(MediaIOCommandClose&)` on
the same instance to give you a chance to release any
partially-allocated resources. **Backends MUST tolerate Close from
a failed-open state** — typically by checking whether each resource
is valid before releasing it. The same Close handler runs after a
normal open, so write it once defensively.

### Reading {#mediaio_authoring_read}

```cpp
Error MediaIOTask_Foo::executeCmd(MediaIOCommandRead &cmd) {
    int step = cmd.step;          // honor the user's step value
    // ... read the next frame at the current position ...
    if(eof) return Error::EndOfFile;

    cmd.frame = builtFramePtr;    // set the output frame
    cmd.currentFrame = position;  // optional; MediaIO will cache it
    return Error::Ok;
}
```

Backends are responsible for stamping per-frame metadata they own:
`CaptureTime`, `PresentationTime`, `FrameRepeated`, `FrameDropped`,
`FrameLate`, `FrameKeyframe`, etc. MediaIO automatically stamps
`FrameNumber` on the returned frame, so you don't need to.

### Threading rules for backends {#mediaio_authoring_threading}

- Your `executeCmd` is called from a single strand worker thread.
  Two `executeCmd` calls on the same task **never** overlap.
- You may safely touch your own private members without
  synchronization (the strand serializes access).
- You may NOT touch `MediaIO` directly — there is no back-pointer.
  Communicate exclusively via command output fields.
- Backend internal threads (e.g. a capture callback thread) must
  coordinate with `executeCmd` themselves (typically by buffering
  into a `Queue` that `executeCmd(Read)` drains).

### Parameterized command dispatch {#mediaio_authoring_params}

```cpp
Error MediaIOTask_Foo::executeCmd(MediaIOCommandParams &cmd) {
    if(cmd.name == "SetGain") {
        double db = cmd.params.getAs<double>("Gain", 0.0);
        double actual = setHardwareGain(db);
        cmd.result.set("ActualGain", actual);
        return Error::Ok;
    }
    return Error::NotSupported;
}
```

## EOF semantics {#mediaio_eof}

When a read returns `Error::EndOfFile`, MediaIO latches the
stream as exhausted: subsequent `readFrame()` calls keep
returning `Error::EndOfFile` immediately (without issuing
further prefetch reads) until one of:

- `seekToFrame()` is called — the latch is cleared so the new
  position can be read
- `setStep()` is called with a new direction/speed — the latch
  is cleared (flipping to reverse past EOF is valid)
- `close()` is called — the latch is reset with the rest of the
  cache

Rationale: once the backend has said "done," spamming it with
additional reads is wasteful and may confuse stream-based
backends. The latch saves the repeated backend calls and makes
the EOF condition stable and observable from the user thread.

Users who want to detect EOF just check for `Error::EndOfFile`
from `readFrame()`.

## Mid-stream descriptor changes {#mediaio_descchange}

Some backends (VFR video files, segmented streams, format-changing
live captures) need to tell MediaIO "the MediaDesc has changed"
between frames. The flow:

1. In `executeCmd(MediaIOCommandRead &)`, the backend detects
   that this frame uses a new format.
2. It sets `cmd.mediaDescChanged = true` and fills
   `cmd.updatedMediaDesc` with the new descriptor.
3. It fills `cmd.frame` as usual and returns `Error::Ok`.

MediaIO then:

1. Copies `updatedMediaDesc` into its cache so
   `mediaDesc()` / `frameRate()` / `audioDesc()` return the
   new values.
2. Stamps `Metadata::MediaDescChanged = true` on the returned
   frame so per-frame consumers can tell which frame triggered
   the change.
3. Emits the `descriptorChanged` signal.

Listeners that wire up to `descriptorChanged` can react to the
change (e.g. reconfigure a downstream pipeline); per-frame
consumers can inspect `Metadata::MediaDescChanged` on the
frame.

## Backend statistics {#mediaio_stats}

MediaIO exposes a first-class stats query via `stats()`:

```cpp
MediaIOStats s = io->stats();
int64_t dropped  = s.getAs<int64_t>(MediaIOStats::FramesDropped, 0);
int64_t repeated = s.getAs<int64_t>(MediaIOStats::FramesRepeated, 0);
double  latency  = s.getAs<double>(MediaIOStats::AverageLatencyMs, 0.0);
```

`MediaIOStats` is its own `VariantDatabase` type (distinct from
`MediaConfig` and `MediaIOParams` so key names don't collide). It
defines standard key names as static members: `FramesDropped`,
`FramesRepeated`, `FramesLate`, `QueueDepth`, `QueueCapacity`,
`BytesPerSecond`, `AverageLatencyMs`, `PeakLatencyMs`, `LastErrorMessage`.
Cross-backend tooling (UIs, telemetry) can rely on these.
Backends are free to add their own backend-specific keys for data
not covered by the standard set.

Backends implement stats by overriding
`executeCmd(MediaIOCommandStats &)` and populating
`cmd.stats`. The default implementation returns an empty
`MediaIOStats` (i.e. a backend that doesn't override reports no
stats).

Because stats dispatch through the strand, a `stats()` call is
serialized with any pending reads/writes — the returned values
reflect the backend state as of the point where the stats
command was processed.

## Live capture pattern {#mediaio_capture}

Capture devices (video cards, cameras, network sources) produce
frames on their own clock and need to be decoupled from the
user's `readFrame()` calls. The recommended pattern is:

1. The task starts an internal callback or thread during
   `executeCmd(MediaIOCommandOpen &)` that feeds a bounded
   `Queue<Frame::Ptr>`.
2. The task's `executeCmd(MediaIOCommandRead &)` pops from the
   queue (blocking if empty), stamps per-frame metadata
   (`CaptureTime`, `FrameDropped`, etc.), and hands the frame
   back to MediaIO.
3. The task sets `cmd.defaultPrefetchDepth` to 2–4 during open
   so MediaIO keeps a small pipeline of in-flight reads for
   latency hiding.
4. The task tracks stats (dropped frames, queue depth, latency)
   and reports them via `executeCmd(MediaIOCommandStats &)`.

Sketch:

```cpp
class MediaIOTask_VideoDevice : public MediaIOTask {
    DeviceHandle       _device;
    Queue<Frame::Ptr>  _captureQueue;
    Atomic<int64_t>    _dropped{0};
    Atomic<int64_t>    _latePushes{0};

    // Device callback — runs on a device-owned thread.
    static void onDeviceFrame(Frame::Ptr frame, void *ctx) {
        auto *self = static_cast<MediaIOTask_VideoDevice *>(ctx);
        // If the queue is full, drop the oldest frame and count it.
        if(self->_captureQueue.size() >= 4) {
            Frame::Ptr drop;
            if(self->_captureQueue.popOrFail(drop)) self->_dropped.fetchAndAdd(1);
        }
        self->_captureQueue.push(frame);
    }

    Error executeCmd(MediaIOCommandOpen &cmd) override {
        _device = openDevice(cmd.config);
        if(!_device.isValid()) return Error::OpenFailed;
        _device.setCallback(onDeviceFrame, this);
        _device.start();
        cmd.canSeek              = false;
        cmd.frameCount           = MediaIO::FrameCountInfinite;
        cmd.defaultPrefetchDepth = 4;
        cmd.mediaDesc            = buildMediaDesc();
        return Error::Ok;
    }

    Error executeCmd(MediaIOCommandClose &cmd) override {
        if(_device.isValid()) {
            _device.stop();
            _device = DeviceHandle();
        }
        // Drain any queued frames left over.
        Frame::Ptr drop;
        while(_captureQueue.popOrFail(drop)) {}
        _dropped.setValue(0);
        return Error::Ok;
    }

    Error executeCmd(MediaIOCommandRead &cmd) override {
        // popWithTimeout returns the frame or TryAgain.  Blocking
        // here is fine — the strand holds a pool thread, but only
        // for this one frame duration.
        auto [frame, err] = _captureQueue.pop();
        if(err.isError()) return err;
        cmd.frame = frame;
        cmd.frame.modify()->metadata().set(Metadata::CaptureTime,
                MediaTimeStamp(TimeStamp::now(), ClockDomain::SystemMonotonic));
        return Error::Ok;
    }

    Error executeCmd(MediaIOCommandStats &cmd) override {
        cmd.stats.set(MediaIOStats::FramesDropped, _dropped.value());
        cmd.stats.set(MediaIOStats::QueueDepth,    (int64_t)_captureQueue.size());
        cmd.stats.set(MediaIOStats::QueueCapacity, (int64_t)4);
        return Error::Ok;
    }
};
```

Key points:

- The device callback thread is independent of the strand worker.
  `_captureQueue` is the thread-safe handoff.
- `executeCmd(Read)` blocks in the queue's `pop()` until a frame
  arrives. That ties up a pool thread for about one frame period,
  so for many simultaneous devices you may want to grow the pool
  (see [Thread pool sizing](#mediaio_pool)).
- Drops are counted at the callback (where the decision happens)
  and reported via stats. Per-frame drop counts can also be
  stamped on the frame via `Metadata::FrameDropped`.
- The task's Close handler stops the device AND drains any
  leftover queued frames so the next open starts clean. This
  same handler runs on failed open per the
  [Open / close contract](#mediaio_authoring_open).

## Per-frame metadata keys {#mediaio_metadata}

MediaIO and its backends conventionally stamp the following
`Metadata` keys on read frames (defined in `metadata.h`):

| Key | Type | Meaning | Set by |
|-----|------|---------|--------|
| `FrameNumber` | int64_t | Sequential frame number | MediaIO |
| `CaptureTime` | TimeStamp | Wall-clock at capture | Backend |
| `PresentationTime` | TimeStamp | When to display | Backend |
| `FrameRepeated` | int | Repeat count (underrun) | Backend |
| `FrameDropped` | int | Frames dropped just before | Backend |
| `FrameLate` | bool | Arrived late | Backend |
| `FrameKeyframe` | bool | Is an I-frame | Backend |
| `Timecode` | Timecode | SMPTE timecode | Backend |
| `MediaDescChanged` | bool | This frame triggered a descriptor change | MediaIO |

Absence of any of these means "no info" — consumers should treat
an absent `FrameRepeated` as zero, an absent `FrameKeyframe` as
false, etc.

## Clock integration {#mediaio_clock}

Every open `MediaIO` can supply a `Clock` that represents the stream's
timing source.  Call `createClock()` to obtain one:

```cpp
Clock::Ptr clock = Clock::Ptr::takeOwnership(io->createClock());
```

The method delegates first to the task's `createClock()` hook.
Backends with a hardware or device clock (capture card, audio output,
PTP source) override this and return a subclass tied to that source.
If the task returns `nullptr`, `MediaIO` falls back to a `MediaIOClock`
— a synthetic `Clock` that reads `currentFrame × framePeriod` and
propagates `Error::ObjectGone` when the `MediaIO` is destroyed.

The `MediaIOClock` fallback is appropriate for file-based or synthetic
sources where the authoritative time is the frame counter, not a wall
clock.  For real-time playback pipelines where A/V sync matters,
prefer the audio-device clock supplied by `SDLAudioOutput::createClock`.

### Custom task clock {#mediaio_clock_custom}

To supply a hardware clock from a backend, override `createClock` in
your `MediaIOTask` subclass:

```cpp
Clock *MyTask::createClock() {
        // Only valid after open.  Return nullptr if the device is not
        // yet open so MediaIO falls back to the MediaIOClock safely.
        if(!_deviceOpen) return nullptr;
        return new MyDeviceClock(_device);
}
```

The returned pointer is adopted by the caller into a `Clock::Ptr`.
`createClock()` is called after `open()` succeeds, so the device is
always ready.

## Thread pool sizing {#mediaio_pool}

All MediaIO instances share a single static `ThreadPool` exposed
via `MediaIO::pool()`. The default size is
`std::thread::hardware_concurrency()`. Backends that block in
`executeCmd` (e.g. live capture devices that wait for the next
frame) hold a pool thread for the duration of the read. If you
have many such backends active simultaneously, the pool can
starve.

Resize at startup if needed:

```cpp
MediaIO::pool().setThreadCount(16);
```

Cancellation, prefetch, and idle detection are all per-instance
via the per-MediaIO Strand, so resizing the pool is a global
concurrency tuning knob — not something application code typically
needs to do for correctness.
