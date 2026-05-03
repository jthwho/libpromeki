# MediaIO — Backend Author Guide {#mediaio_backend_guide}

How to write a new `MediaIO` subclass — picking a strategy class,
implementing `executeCmd`, declaring ports during open,
populating cached state, and registering the backend with the
factory system.

For an introduction to using the framework as a consumer, see the
[MediaIO User Guide](@ref mediaio_user_guide). The user guide
explains the always-async API, port connections, and signal
contracts; this guide assumes you know that material and focuses
on the implementer's surface.

## Class hierarchy {#mediaio_backend_hierarchy}

```
MediaIO                       (abstract — public API + cached state +
                               ports + signals; one virtual: submit())
└─ CommandMediaIO             (executeCmd virtuals + dispatch helper +
                               port-construction helpers)
   ├─ InlineMediaIO           (submit runs inline on the calling thread)
   ├─ SharedThreadMediaIO     (submit posts to a shared ThreadPool Strand)
   └─ DedicatedThreadMediaIO  (submit posts to an owned worker thread)
```

You inherit from one of the three concrete strategy classes —
never from `MediaIO` or `CommandMediaIO` directly. The strategy
class controls what thread your `executeCmd` runs on. The
`CommandMediaIO` layer routes commands by type to the right
overload; `MediaIO` owns the cached state and the public API.

### Picking a strategy {#mediaio_backend_picking_strategy}

| Strategy | Use when |
|----------|----------|
| `InlineMediaIO` | The backend is fast, deterministic, and does no I/O. Tests, in-memory transforms, generators driven entirely by config. `submit` runs on the caller's thread. |
| `SharedThreadMediaIO` | The backend does CPU-bound work that needs serialization but does not block on external resources. The compute backends (CSC, SRC, FrameSync, VideoEncoder, VideoDecoder, FrameBridge, NullPacing, Burn) all live here. Strand serializes commands per-instance; the shared pool keeps thread count bounded across the process. |
| `DedicatedThreadMediaIO` | The backend can block on syscalls (file I/O, sockets, capture devices, condvars). Owns its own worker thread so a slow backend cannot starve the shared pool. The I/O backends (ImageFile, AudioFile, QuickTime, V4L2, RTP, MjpegStream) live here. |

Default to `SharedThreadMediaIO` for compute and
`DedicatedThreadMediaIO` for blocking I/O. `InlineMediaIO` is
mostly for tests; production backends almost always want the
strand or the worker thread.

## The factory contract {#mediaio_backend_factory}

Every backend has a paired `MediaIOFactory` subclass that the
framework consults for identity, config metadata, and bridging.
The factory is registered via `PROMEKI_REGISTER_MEDIAIO_FACTORY`:

```cpp
class FooFactory : public MediaIOFactory {
public:
        FooFactory() = default;
        ~FooFactory() override = default;

        // ---- Identity ----
        String     name()        const override { return String("Foo"); }
        String     description() const override { return String("Foo backend"); }

        // ---- Discovery surfaces ----
        StringList extensions() const override { return { String("foo") }; }
        StringList schemes()    const override { return { String("foo") }; }

        // ---- Role flags ----
        bool canBeSource()    const override { return true;  }
        bool canBeSink()      const override { return false; }
        bool canBeTransform() const override { return false; }

        // ---- Configuration ----
        MediaConfig::SpecMap configSpecs() const override;

        // ---- Construction ----
        MediaIO *create(const MediaConfig &config,
                        ObjectBase *parent = nullptr) const override {
                return new FooMediaIO(config, parent);
        }

        // ---- Optional surfaces ----

        // Only needed if the backend should be discoverable by
        // URL.  Invoked by MediaIO::createFromUrl after dispatch
        // by scheme.
        Error urlToConfig(const Url &url,
                          MediaConfig *outConfig) const override;

        // Only needed for transforms that should be
        // planner-insertable as auto-bridges.  outCost is unitless
        // (lower = preferred).
        bool bridge(const MediaDesc &from, const MediaDesc &to,
                    MediaConfig *outConfig, int *outCost) const override;
};

PROMEKI_REGISTER_MEDIAIO_FACTORY(FooFactory)
```

The macro registers a singleton instance with the global
`MediaIOFactory` registry at static-init time. From that point on,
`MediaIOFactory::findByName("Foo")` and the convenience helpers on
`MediaIO::create` / `createForFileRead` / `createFromUrl` will
find the backend.

What the framework expects from each virtual:

- `name()` / `description()` — identity. `name()` is the lookup
  key. `displayName()` defaults to `name()`; override it when you
  want a different label in UI.
- `extensions()` — file extensions the backend claims (no leading
  dot). Empty if it doesn't speak files.
- `schemes()` — URL schemes routed to this backend (lowercase,
  no trailing colon). Empty if it doesn't speak URLs.
- `canBeSource()` / `canBeSink()` / `canBeTransform()` — role
  flags consulted by the planner and by the user-facing
  `MediaIO::create*` helpers.
- `create(config, parent)` — construct a fresh, un-opened
  instance. Pass `parent` through to `MediaIO`'s constructor.
  Do not open here; `executeCmd(Open)` does that.
- `configSpecs()` — the `MediaConfig::SpecMap` mapping every
  `MediaConfig::ID` the backend understands to a `VariantSpec`
  that carries defaults, accepted types, ranges, and
  descriptions. Used by the planner and the validation helpers.
- `urlToConfig(url, outConfig)` — translate a parsed URL into a
  `MediaConfig`. Default returns `Error::NotSupported`.
- `bridge(from, to, outConfig, outCost)` — only override on
  transform backends. Returns `true` and populates the outputs
  when the transform can convert `from` to `to`.

Note: `defaultConfig`, `unknownConfigKeys`, and
`validateConfigKeys` are **static** helpers on `MediaIOFactory`
keyed by backend name — not virtuals you override. They derive
their answers from `configSpecs()` (defaults from each spec's
default value, "unknown" by diffing against the spec map). You
get them for free once you've populated `configSpecs()`.

## Implementing executeCmd {#mediaio_backend_executecmd}

`CommandMediaIO` exposes one virtual per command type. Override
the ones your backend supports; the defaults return sensible
errors for the rest:

```cpp
class FooMediaIO : public SharedThreadMediaIO {
        PROMEKI_OBJECT(FooMediaIO, SharedThreadMediaIO)
public:
        FooMediaIO(const MediaIO::Config &cfg, ObjectBase *parent = nullptr)
                : SharedThreadMediaIO(parent) {
                setConfig(cfg);
        }
        ~FooMediaIO() override = default;

protected:
        Error executeCmd(MediaIOCommandOpen   &cmd) override;
        Error executeCmd(MediaIOCommandClose  &cmd) override;
        Error executeCmd(MediaIOCommandRead   &cmd) override;
        Error executeCmd(MediaIOCommandStats  &cmd) override;
        // Skipped: executeCmd(Write/Seek/Params) defaults are fine
        // for a non-seekable read-only backend.
};
```

### The lifecycle of a command {#mediaio_backend_lifecycle}

The seven-step flow:

1. **Caller** invokes a public API on the MediaIO (`io->open()`,
   `source->readFrame()`, …).
2. **Public API** (on `MediaIO` / `MediaIOSource` / `MediaIOSink` /
   `MediaIOPortGroup`) builds the typed `MediaIOCommand`, wraps
   it in a `MediaIORequest`, and calls the protected `submit()`.
3. The strategy class's `submit()` records `QueueWaitDuration`
   and either short-circuits to `Error::Cancelled` (if the
   request was cancelled before dispatch) or calls `dispatch(cmd)`.
4. `CommandMediaIO::dispatch` does a type switch and routes to
   the matching `executeCmd(...)` overload.
5. **Your `executeCmd`** runs. Read the inputs, do the work,
   populate the outputs, return an `Error`.
6. The strategy writes your `Error` into `cmd->result` and calls
   `MediaIO::completeCommand(cmd)`.
7. `completeCommand` applies the output fields to the cached
   state, emits the relevant signals, and resolves the request's
   promise. Order is fixed: cache → signals → promise.

**Hard rule: backends never call `completeCommand`.** Backends
populate output fields and return an Error. The strategy class
handles the rest.

`submit()` is not re-entrant from inside `executeCmd`. Calling it
on `this` from within your own hook would deadlock the strand or
serialize behind itself on the dedicated thread. Backends that
want to chain operations expose them at the public API level so
the caller composes via `MediaIORequest::then(...)`.

### Open and port construction {#mediaio_backend_open}

Open is the *only* place a backend may construct ports. Use the
`CommandMediaIO` helpers:

```cpp
Error FooMediaIO::executeCmd(MediaIOCommandOpen &cmd) {
        // Resolve config, open the resource.
        const String &filename = cmd.config.getAs<String>(MediaConfig::Filename, String());
        if (!_file.open(filename)) return Error::OpenFailed;

        // Build a port group — every port belongs to one.
        // Pass a Clock if the backend has a hardware/device timing
        // reference, otherwise the framework synthesizes a
        // MediaIOClock for you.
        MediaIOPortGroup *grp = addPortGroup("primary");

        // Add sources for each output stream.  desc is the
        // MediaDesc the source produces.
        addSource(grp, _videoDesc, "video");
        if (_haveAudio) addSource(grp, _audioDesc, "audio");

        // Backend-tuned per-open defaults.  These flow into the
        // cached state on completeCommand.
        cmd.defaultStep         = 1;
        cmd.defaultPrefetchDepth = 2;
        cmd.defaultSeekMode     = MediaIO_SeekKeyframeBefore;

        return Error::Ok;
}
```

The framework reads the cached `mediaDesc` / `audioDesc` /
`metadata` from the *first* source (or first sink for sink-only
backends) post-open, and the `frameRate` from the first port
group.

**Open-failure cleanup contract.** If your `executeCmd(Open)`
returns non-Ok, `CommandMediaIO::dispatch` automatically calls
`executeCmd(Close)` on the same instance to give you a chance to
release any partially-allocated resources. The same `Close`
handler also runs after a successful open, so write it once
defensively (check each handle for validity before releasing).

```cpp
Error FooMediaIO::executeCmd(MediaIOCommandClose &cmd) {
        if (_file.isOpen()) _file.close();
        // Ports are auto-destroyed via the ObjectBase parent/child
        // cascade rooted at this MediaIO.  Don't free them yourself.
        return Error::Ok;
}
```

### Reading frames {#mediaio_backend_read}

```cpp
Error FooMediaIO::executeCmd(MediaIOCommandRead &cmd) {
        // cmd.group is the port group whose source is reading.
        // cmd.step is the current step value (positive = forward,
        // negative = reverse, 0 = repeat).

        Frame::Ptr f;
        Error err = _file.readNext(&f);
        if (err == Error::EndOfFile) return Error::EndOfFile;
        if (err.isError()) return err;

        // Stamp per-frame metadata you own.  FrameNumber is
        // stamped automatically by the framework — you only need
        // to fill in the others.
        f.modify()->metadata().set(Metadata::CaptureTime, _file.captureTime());
        f.modify()->metadata().set(Metadata::FrameKeyframe, _file.isKeyframe());

        cmd.frame        = f;
        cmd.currentFrame = _file.position();
        return Error::Ok;
}
```

If your backend detects mid-stream descriptor changes (VFR,
segmented streams, format-changing live captures), set
`cmd.mediaDescChanged = true` and fill `cmd.updatedMediaDesc`
before returning. The framework will:

1. Update the cached `_mediaDesc` / `_audioDesc` / `_metadata` /
   `_frameRate`.
2. Stamp `Metadata::MediaDescChanged = true` on the returned
   frame.
3. Emit the `descriptorChanged` signal.

### Writing frames {#mediaio_backend_write}

```cpp
Error FooMediaIO::executeCmd(MediaIOCommandWrite &cmd) {
        Error err = _file.writeFrame(cmd.frame);
        if (err.isError()) return err;
        cmd.currentFrame = _file.position();
        cmd.frameCount   = _file.frameCount();
        return Error::Ok;
}
```

If a write carries a non-empty `Frame::configUpdate` delta (e.g.
encoder bitrate ramp), `dispatch` calls your `configChanged`
hook on the same thread, just before `executeCmd(Write)`. The
default is a no-op:

```cpp
void FooMediaIO::configChanged(const MediaConfig &delta) override;
```

Apply the delta to your encoder/transcoder/... state.

### Seek {#mediaio_backend_seek}

```cpp
Error FooMediaIO::executeCmd(MediaIOCommandSeek &cmd) {
        // cmd.frameNumber is where to go.
        // cmd.mode is one of MediaIO_SeekExact / SeekNearestKeyframe /
        // SeekKeyframeBefore / SeekKeyframeAfter.  MediaIO_SeekDefault
        // has already been resolved to the backend's preferred mode
        // by the framework — you never see it here.
        return _file.seekTo(cmd.frameNumber, cmd.mode);
}
```

Seek and `setStep` automatically discard any prefetched reads
sitting in the read cache (the framework cancels the in-flight
queue and drops cached results), so the next read starts from the
new position.

### Parameterized commands {#mediaio_backend_params}

```cpp
Error FooMediaIO::executeCmd(MediaIOCommandParams &cmd) {
        if (cmd.name == "SetGain") {
                double db = cmd.params.getAs<double>(GainParamID, 0.0);
                double actual = _device.setGain(db);
                cmd.params.set(ActualGainParamID, actual);
                return Error::Ok;
        }
        return Error::NotSupported;
}
```

Define the param IDs as `static const MediaIOParamsID` members
on the backend's class so callers can reference them by symbol.

### Stats {#mediaio_backend_stats}

```cpp
Error FooMediaIO::executeCmd(MediaIOCommandStats &cmd) {
        cmd.stats.set(MediaIOStats::FramesDropped, _droppedCount.value());
        cmd.stats.set(MediaIOStats::QueueDepth,
                      static_cast<int64_t>(_queue.size()));
        // Framework will overlay standard rate-tracker keys on top
        // of whatever you write here in completeCommand.
        return Error::Ok;
}
```

`MediaIOStats` defines well-known cumulative-aggregate keys
(`FramesDropped`, `FramesRepeated`, `FramesLate`, `QueueDepth`,
`QueueCapacity`, `BytesPerSecond`, `AverageLatencyMs`,
`PeakLatencyMs`, `LastErrorMessage`). Cross-backend tooling
relies on these; backends are free to add their own keys.

## Cancellation contract {#mediaio_backend_cancel}

Three states (only state 3 is the backend's responsibility):

1. **Pre-dispatch** — strategy short-circuits to `Error::Cancelled`
   without calling `executeCmd`. Free.
2. **In flight on Inline / SharedThread** — late cancellation is
   ignored; `executeCmd` runs to completion. The wrapping request
   resolves with whatever the operation actually returned.
3. **In flight on DedicatedThread** — the framework can interrupt
   the worker thread's blocking primitive if the backend
   implements `cancelBlockingWork()`:

```cpp
class FooMediaIO : public DedicatedThreadMediaIO {
public:
        // ... constructors, executeCmd overrides ...

protected:
        // Called from the framework's cancellation path on a
        // *different* thread than the one running executeCmd.
        // Wake the syscall.
        void cancelBlockingWork() override {
                _device.signalShutdown();
                ::shutdown(_socket, SHUT_RDWR);
                _condvar.notify_all();
        }
};
```

Common interruption primitives:

- Closing a blocking fd to wake `read`/`accept`.
- Signaling a `std::condition_variable` to wake a wait loop.
- Setting an atomic flag that a syscall loop checks between
  iterations.

The interrupted `executeCmd` should return a sensible error
(`Error::Cancelled`, `Error::Interrupted`, or whatever the
syscall produced). The framework writes whichever error you
returned into `cmd->result`.

## Re-entrancy and thread safety {#mediaio_backend_thread_safety}

- Two `executeCmd` calls on the same instance never overlap. The
  strategy class serializes them.
- You may safely touch your own private members from `executeCmd`
  without locks.
- You may NOT call `submit()` (or any public API on `this`) from
  inside your own `executeCmd`. Doing so would deadlock the
  strand or serialize behind itself on the dedicated thread.
  Chain operations at the public API level so the caller composes
  via `MediaIORequest::then(...)`.
- Backend-internal threads (capture callback threads, network
  receive threads) coordinate with `executeCmd` themselves —
  typically by buffering into a `Queue<Frame::Ptr>` that
  `executeCmd(Read)` drains.

## Live capture pattern {#mediaio_backend_capture}

Capture devices produce frames on their own clock and need to be
decoupled from the user's `readFrame()` calls.

1. During `executeCmd(Open)`, start a callback or thread that
   feeds a bounded `Queue<Frame::Ptr>`.
2. `executeCmd(Read)` pops from the queue (blocking if empty),
   stamps per-frame metadata, returns the frame.
3. Set `cmd.defaultPrefetchDepth = 2..4` so MediaIO keeps a small
   pipeline of in-flight reads for latency hiding.
4. Track stats (drops, queue depth, latency) and report them via
   `executeCmd(Stats)`.

Backends that block in capture syscalls should derive from
`DedicatedThreadMediaIO` and implement `cancelBlockingWork()` to
wake the syscall on close.

## Auto-bridge transforms {#mediaio_backend_bridge}

If your backend is a transform that the planner should be allowed
to auto-insert (CSC, SRC, FrameSync, VideoEncoder, VideoDecoder,
FrameBridge), implement `MediaIOFactory::bridge`:

```cpp
bool FooFactory::bridge(const MediaDesc &from, const MediaDesc &to,
                        MediaIO::Config *cfg, double *cost) const {
        // Return true if this transform can convert `from` → `to`.
        // Populate *cfg with whatever config the transform needs to
        // do that conversion, and *cost with a relative weight
        // (lower = preferred).
        if (!canBridge(from, to)) return false;
        cfg->set(MediaConfig::OutputPixelFormat, to.pixelFormat());
        *cost = 10.0;
        return true;
}
```

The default implementation returns `false` (i.e. "I'm not a
planner-insertable bridge"). See @ref mediaplanner "MediaPipelinePlanner"
for how the planner uses these.

## Testing {#mediaio_backend_test}

Two header-only test helpers in `tests/unit/mediaio_test_helpers.h`:

- `promeki::tests::InlineTestMediaIO : InlineMediaIO` —
  callback-driven `executeCmd` overrides fronted by
  `std::function` hooks (`onOpen`, `onClose`, `onRead`, `onWrite`,
  `onSeek`, `onParams`, `onStats`). Defaults are sensible. Use
  this for canned-response unit tests.
- `promeki::tests::PausedTestMediaIO : CommandMediaIO` — manually
  pumped helper. `submit()` queues commands without dispatching;
  the test calls `processOne()` / `processAll()` to drain. Used
  for cancellation tests so pre-dispatch cancel is deterministic.

Most backend tests inherit from `InlineMediaIO` directly with
custom `executeCmd` overrides — see `tests/unit/mediaio_negotiation.cpp`
for an example. Wrap the inline subclass with a matching
`MediaIOFactory` subclass (also locally defined in the test file)
so the planner-side surface (`canBeSource`, `bridge`, etc.) can
be exercised.

## Files {#mediaio_backend_files}

- `include/promeki/mediaio.h` + `src/proav/mediaio.cpp` — abstract
  MediaIO + cached state + completeCommand
- `include/promeki/commandmediaio.h` + `src/proav/commandmediaio.cpp`
  — executeCmd virtuals + dispatch + port helpers
- `include/promeki/inlinemediaio.h` + .cpp — inline strategy
- `include/promeki/sharedthreadmediaio.h` + .cpp — strand strategy
- `include/promeki/dedicatedthreadmediaio.h` + .cpp — dedicated-thread
  strategy
- `include/promeki/mediaiofactory.h` + .cpp — abstract factory +
  registry
- `include/promeki/mediaiocommand.h` + .cpp — command hierarchy +
  the `PROMEKI_MEDIAIO_COMMAND` macro
- `include/promeki/mediaiorequest.h` + .cpp — promise/future-style
  request handle (`wait`, `wait(ms)`, `then`, `cancel`,
  `commandAs<T>`)
- `include/promeki/mediaio{port,source,sink,portgroup,portconnection}.h`
- `include/promeki/mediaioreadcache.h` — per-source read result
  cache
- `include/promeki/mediaiostats.h` — VariantDatabase for per-cmd
  + cumulative aggregate telemetry
