# AJA NTV2 MediaIO Backend

**Library:** `promeki` (gated by `PROMEKI_ENABLE_NTV2`)
**Status:** Build scaffolding landed 2026-05-16 (`thirdparty/libajantv2`
submodule pinned to `ntv2_18_0_0`, CMake flag wired, `NTV2` listed in
the feature string). No backend code yet — this document is the
implementation plan.
**Standards:** All work follows `CODING_STANDARDS.md`. Every new
class requires complete doctest coverage. See `devplan/README.md`
for the full requirements.
**Depends on:** [`devplan/proav/video-signal-carriers.md`](video-signal-carriers.md)
— the generic carrier types (`VideoPortRef`, `SdiSignalConfig`,
`HdmiSignalConfig`, `VideoReferenceConfig`) ship in their own
milestone first.  Phase 1 of this plan presumes those types and
their `MediaConfig` keys are already in the library.

## Goal

Wrap AJA's NTV2 SDI / HDMI capture and playout family (Kona,
Corvid, Io, T-Tap, KonaHDMI) behind libpromeki's generic `MediaIO`
interface so an AJA card looks like any other source / sink to a
`MediaPipeline`. The backend rides on the vendored open-source
`libajantv2` SDK already in `thirdparty/libajantv2`.

The MediaIO contract (always-async, port-group ticking, factory
registry, `proposeInput` / `proposeOutput`, allocator hook) is the
fixed surface — see `docs/mediaio.dox` and the NDI / V4L2 backends
for working precedent. This plan only covers what's NTV2-specific.

## Top-level architecture decision

**One `Ntv2MediaIO` per logical channel; a process-wide device
layer underneath.**

A *logical channel* in this design is a self-contained capture or
playout entity: one NTV2 frame-buffer (`NTV2Channel` resource on
the card), the set of physical SDI / HDMI ports assigned to it,
one optional audio system, and optional ANC extractor / inserter.
That's the unit a `MediaPipeline` consumes, and the only granularity
that lets independent pipelines (camera → recorder, return feed →
preview, fill / key pair, etc.) coexist on the same card without
sharing pipeline state.

Multiple physical SDI ports can attach to a single logical channel
when the signal is dual-link (3G dual, 4K30 dual) or quad-link
(4K Quad-Link Square Division or 2SI). 12G single-link uses one
port for a 4K signal. HDMI 2.0 8K may use multiple internal lanes
but is one physical connector. The logical channel hides all of
that from the pipeline.

Things that *aren't* per-channel — card acquisition, the OEM task
mode, signal routing arbitration, the device reference clock,
audio-system allocation across channels — live on a shared
`Ntv2Device` reference-counted by a process-wide
`Ntv2DeviceRegistry`. Same pattern as `NdiDiscovery` + `NdiLib`.

### Rejected alternative

*One `Ntv2MediaIO` per card, channels as port groups.* This would
collapse the card into a single MediaIO with N port groups (one per
channel). It conflicts with:

- `MediaIOPortGroup`'s contract — a group is "ports sharing a clock
  and rate, ticking together." NTV2 channels in `MultiFormatMode`
  run at independent formats and rates; that's not one group.
- The pipeline model — pipelines are wired to a `MediaIO`, not a
  port group, so any per-channel routing decision (different
  destinations, different cadences, different planner outcomes)
  forces a per-channel MediaIO anyway.
- Failure isolation — one channel hitting a signal loss or codec
  error shouldn't take the whole card's MediaIO with it.

## Class layout

```
promeki/
├── include/promeki/
│   ├── videoportref.h         # NEW — VideoPortRef + VideoConnectorKind (generic)
│   ├── sdisignalconfig.h      # NEW — SdiSignalConfig + SdiLinkStandard
│   │                          #   (SMPTE-named); generic, reusable
│   ├── hdmisignalconfig.h     # NEW — HdmiSignalConfig + HdmiSpecVersion
│   ├── videoreferenceconfig.h # NEW — VideoReferenceConfig +
│   │                          #   VideoReferenceSource + VideoReferenceRateFamily
│   ├── ntv2mediaio.h          # Ntv2MediaIO, Ntv2Factory
│   ├── ntv2device.h           # Ntv2Device, Ntv2DeviceRegistry
│   ├── ntv2format.h           # NTV2-specific mapping helpers
│   │                          #   (PixelFormat / FrameRate / VideoFormat ↔
│   │                          #    NTV2 enums; SdiLinkStandard ↔ AJA
│   │                          #    routing-preset table)
│   └── ntv2clock.h            # Ntv2DeviceClock (HW sample-counter clock)
└── src/proav/
    ├── videoportref.cpp
    ├── sdisignalconfig.cpp
    ├── hdmisignalconfig.cpp
    ├── videoreferenceconfig.cpp
    ├── ntv2mediaio.cpp
    ├── ntv2device.cpp
    ├── ntv2format.cpp
    └── ntv2clock.cpp
```

The four `*config*` / `videoportref` files are **generic** — no NTV2
dependency, no AJA SDK includes, no `#if PROMEKI_ENABLE_NTV2`
gating. They sit alongside `imagedesc.h` / `audiodesc.h` in the
flat header tree as the missing *carrier-level* descriptors that
complement the existing *content-level* types (`VideoFormat`,
`ImageDesc`, etc.). A future DeckLink, SDI-over-IP (ST 2110 / ST
2022-6), or third-party SDI backend reuses them unchanged.

Naming: short prefix `Ntv2` (not `Aja` — AJA ships several SDK
families; NTV2 is the specific one). Matches the SDK's own naming
(`CNTV2Card`, `NTV2VideoFormat`, etc.).

### `Ntv2Device` / `Ntv2DeviceRegistry`

`Ntv2DeviceRegistry::instance()` returns the process singleton.
On `acquire(deviceSpec)` it either returns the existing `Ntv2Device`
(refcount bump) or constructs a new one (`CNTV2Card` open,
`AcquireStreamForApplication`, `SetTaskMode(NTV2_OEM_TASKS)`). On
the last `release()` the registry tears the card down.

`Ntv2Device` exposes:

- `CNTV2Card &card()` — the raw handle for backends that need to
  drive registers directly. Internal mutex guards `RouteSignal`,
  `EnableChannel`, register access patterns that race across
  channels.
- `NTV2DeviceID id() const`, `String displayName() const`,
  `String serial() const`.
- `Ntv2Capabilities caps()` — number of SDI ins/outs, HDMI ins/outs,
  audio systems, ANC extractor / inserter availability, supported
  pixel-formats and video formats. Computed once on acquire from
  `mDevice.features()`.
- `Error reserveChannel(int channelIndex, Ntv2MediaIO *owner)` /
  `releaseChannel(int channelIndex)` — track which logical channels
  own which framebuffer indices. Reject double-reservation.
- `Error reservePhysicalPorts(const Ntv2PortSpec &spec,
  Ntv2MediaIO *owner)` / `releasePhysicalPorts(...)` — track which
  SDI / HDMI connectors belong to which channel so dual-link /
  quad-link assignments don't overlap.
- `Error reserveAudioSystem(NTV2AudioSystem sys, Ntv2MediaIO *owner)`
  / `releaseAudioSystem(...)` — same idea for audio systems.
- `Error setReferenceClock(Ntv2ReferenceSource src,
  Ntv2ReferenceStandard std, Ntv2MediaIO *requester)` — the
  "device-wide knob with warn-on-conflict" path (see below).
- `Clock::Ptr sampleClock()` — the shared, lazily-constructed
  hardware sample-counter clock for this device (see "Device clock"
  below). All logical channels on the same card receive the same
  `Clock::Ptr` instance, so cross-channel timestamps share an epoch
  and a `==` test does the obvious thing.
- `signals`: `referenceChanged(src, std)` for channels that need to
  retune (typically none — the card itself handles it).

The registry is reference-counting + safe to call across threads.
Per-card device state mutations take the device's internal mutex;
the strand of one channel's MediaIO is its primary user, but the
device's destructor may run on whichever channel releases last.

### `Ntv2MediaIO`

`class Ntv2MediaIO : public DedicatedThreadMediaIO`. Each instance
represents one logical channel. Inherits the dedicated worker
thread for the MediaIO strand; spins additional internal threads
for the capture / playout side (see "Threading model" below).

`executeCmd(Open)` does:

1. Resolve `Ntv2DeviceIndex` → `Ntv2Device` via `Ntv2DeviceRegistry`.
2. Reserve the logical channel framebuffer (`Ntv2Channel`) per
   `MediaConfig::Ntv2Channel`.
3. Reserve the physical port set per `MediaConfig::Ntv2InputPorts`
   (source) or `Ntv2OutputPorts` (sink), with link mode from
   `Ntv2LinkMode`.
4. Reserve the audio system per `Ntv2AudioSystem` (or skip when
   `none`).
5. If anything is requested device-wide (reference source / standard),
   apply via `Ntv2Device::setReferenceClock`. Log a warning if other
   channels are already active and the new request differs from the
   current setting.
6. For sources: detect input signal, build `MediaDesc`, set up
   `AutoCirculateInitForInput`, optionally start the ANC extractor.
7. For sinks: validate that the offered `MediaDesc` is achievable on
   this hardware, set up `AutoCirculateInitForOutput`, optionally
   set up the ANC inserter.
8. Construct the port group + ports via the inherited
   `addPortGroup` / `addSource` / `addSink` helpers.
9. Spawn the capture or playout worker thread; AutoCirculate-start.
10. Set the port group's clock to a `Ntv2DeviceClock` derived from
    the hardware VBI.

`executeCmd(Close)` mirrors the unwind: AutoCirculate-stop, join
worker threads, release the ANC engine, release the audio system,
release the channel + ports, release the device handle (registry
refcount).

`cancelBlockingWork()` poisons a `std::atomic<bool>` polled by both
the capture / playout loop (which uses short `WaitForInputVerticalInterrupt`
timeouts) and the strand-side `_videoQueue.pop` so an in-flight
read can unwind through a parallel close request.

## Device clock (`Ntv2DeviceClock`)

AJA cards have an onboard sample counter, driven by the same
reference that clocks the SDI output and audio system. It is the
best timing reference the host can read against an AJA card:

- **Locked to the SDI reference** (genlock-respecting; if the card
  is locked to an external reference, so is the counter).
- **Sub-microsecond resolution** (1 / sampleRate seconds per tick;
  ~20.83 µs at 48 kHz).
- **Continuous** — no jumps on VBI; ticks monotonically between
  frames so sub-frame stamps are meaningful.
- **Shared across all channels on the same card** — every channel
  on the card sees the same counter, so cross-channel timestamps
  from one card carry the same epoch by construction.

Modelled as a `Clock` subclass (mirrors `NdiClock`):

```cpp
class Ntv2DeviceClock : public Clock {
        public:
                // One ClockDomain per device — names like
                // "Ntv2:0" / "Ntv2:serial:8675309" so multiple cards
                // in one box don't collide.
                static const ClockDomain &domainFor(const Ntv2Device &);

                Ntv2DeviceClock(Ntv2Device *device,
                                NTV2AudioSystem audioSys,   // -1 = VBI-only fallback
                                const FrameRate &rateHint);

                ~Ntv2DeviceClock() override;

                int64_t     resolutionNs() const override;   // 1/sampleRate ns, or one frame period for VBI fallback
                ClockJitter jitter() const override;         // ± half tick / ± half frame
                double      rateRatio() const override;      // 1.0 nominally; can be tuned by long-window drift estimate

        protected:
                Result<int64_t> raw() const override;
                Error           sleepUntilNs(int64_t targetNs) const override;
};
```

`raw()` reads `CNTV2Card::ReadAudioLastIn(audioSys)` (capture-side
counter) or `ReadAudioLastOut(audioSys)` (playout-side counter) —
whichever direction the device is primarily serving. The register
is 32-bit and wraps every ~24.8 hours at 48 kHz; the clock keeps a
64-bit shadow that gets advanced atomically on every read by
comparing the new sample value against the last-seen one and
incrementing the high 32 bits on detected wrap. `raw()` then
converts to nanoseconds via the audio sample rate.

`sleepUntilNs(target)` does a coarse sleep down to one VBI period
of the target, then short-polls the counter to land within one
sample.

### Per-device singleton

The clock is **owned by `Ntv2Device`**, not by `Ntv2MediaIO`. The
first channel to open on a device triggers
`Ntv2Device::sampleClock()` to construct the clock lazily; every
subsequent channel on that device gets the same `Clock::Ptr`. The
device tears the clock down when its own refcount reaches zero
(i.e. the last channel closed).

This means:
- `port_group_A->clock() == port_group_B->clock()` returns true
  when A and B are on the same card.
- Cross-channel `MediaTimeStamp` deltas on one card require no
  domain conversion.
- Cross-card stamps go through `ClockDomain` translation as usual
  (a different `Ntv2DeviceClock` per device, different domain).

### Fallback when no audio system is enabled

The sample counter requires an active audio system. If a logical
channel opens with `Ntv2AudioSystem=disabled` and no other channel
on the device has an audio system enabled either, the clock
constructs in **VBI-fallback mode**:

- `raw()` returns the host-side wall time at the most-recent
  `SubscribeInputVerticalEvent` wake (one tick per frame period).
- `resolutionNs()` reports one frame period.
- `jitter()` widens to ± half a frame period.

A warning logs once when this happens. The clock auto-promotes to
sample-counter mode if a later-opened channel turns on an audio
system, but only on the device-shared instance — once consumers
have latched onto the lower-resolution clock the resolution
upgrade is silent (their stamps just get more precise).

### Sink-side pacing

Sources always stamp frames against the device clock at capture
time. Sinks have two modes:

1. **Default** — the card paces itself off its own reference; the
   port group's clock is bound for read access but
   `executeCmd(SetClock)` returns `Error::NotSupported`. This is
   the common case.
2. **External pacing** (Phase 6) — the sink accepts an external
   `Clock::Ptr` via `setClock`; `executeCmd(Write)` blocks until
   the external clock advances to the next frame deadline before
   handing off to AutoCirculate. Useful when two cards must be
   synchronised to a third reference (PTP) without a hardware
   genlock cable.

## Threading model

Same shape as `NdiMediaIO` / `V4l2MediaIO`:

- One **strand thread** from `DedicatedThreadMediaIO` runs `executeCmd`
  for the channel's command queue.
- One **capture thread** (`ntv2/cap/<chN>`) per active source channel,
  loops on `AutoCirculateTransfer`, copies into a
  `UncompressedVideoPayload` / `PcmAudioPayload` pair, optionally
  reads ANC via `AncExtractGetField`, pushes onto a small queue
  (`VideoQueueDepth = 2`) drained by the strand in `executeCmd(Read)`.
- One **playout thread** (`ntv2/play/<chN>`) per active sink channel,
  pulls from a queue fed by `executeCmd(Write)` and submits via
  `AutoCirculateTransfer`.

Threads named via `Thread::setName` (per `devplan/proav/backend-thread-naming.md`).
`AcquireStreamForApplication` runs once on the device singleton, not
per channel — so a single process can drive every channel on the
card without fighting itself.

The AJA producer / consumer split in `ntv2capture.cpp` collapses to
one capture thread here, because the strand already plays the
consumer role on the read path.

## URL form

`ntv2://<device>/<channel>` where:

- `<device>` is a device index (`0`, `1`, …), a device-name shorthand
  recognised by `CNTV2DeviceScanner` (`kona5`, `corvid44`, …), or
  `serial:<serial>` to bind by physical board serial number.
- `<channel>` is the **logical channel** index (1-based, matches AJA's
  channel numbering). It identifies the framebuffer + AutoCirculate
  resource on the card.

The URL form intentionally does **not** name physical ports.
Physical port assignment is config-driven (`SdiInputSignal` /
`SdiOutputSignal` / `HdmiInputSignal` / `HdmiOutputSignal`),
because one logical channel can own anywhere from 1 to 4 SDI ports
depending on link standard, because the URL grammar shouldn't bake
link-mode semantics into the path, and because the same generic
config keys must work for non-NTV2 backends that also have SDI
ports.

Examples:

```
ntv2://0/1                     # logical channel 1 on device 0; ports from config
ntv2://kona5/2                 # logical channel 2 on the first Kona5
ntv2://serial:8675309/1        # bind by physical serial
ntv2:///1                      # device 0 implicit
```

CLI / JSON / Variant entry-points all accept the same config keys
as the URL host, per the existing MediaConfig pattern.

## Generic video-signal types (separate milestone)

The carrier-level types this backend consumes —
`VideoPortRef`, `SdiSignalConfig`, `HdmiSignalConfig`,
`VideoReferenceConfig`, plus the matching `MediaConfig` keys
(`SdiInputSignal`, `SdiOutputSignal`, `HdmiInputSignal`,
`HdmiOutputSignal`, `VideoReference`) — ship in their own
milestone: **[`video-signal-carriers.md`](video-signal-carriers.md)**.

They are not NTV2-specific (DeckLink, ST 2022-6, ST 2110-20, and
any future hardware-SDI / SDI-over-IP backend reuses them
unchanged), do not depend on the AJA SDK, and are independently
useful for tooling.  See that milestone for the full type
definitions, `PROMEKI_DATATYPE` ids, CoW `SharedPtr<Impl>`
conventions, and per-phase rollout.

Short summary of the surface this backend depends on:

- **`VideoPortRef`** — `(VideoConnectorKind, int index)` identifying
  one physical connector on the device.
- **`SdiLinkStandard`** — short-prefix enum: `SL_HD`, `SL_3GA`,
  `DL_HD`, `DL_3G`, `QL_3G_SQD`, `QL_3G_2SI`, `SL_6G`, `SL_12G`,
  `SL_24G`, etc.  `cablesFor(standard)` returns 1 / 2 / 4.
- **`SdiSignalConfig`** — `(SdiLinkStandard standard,
  List<VideoPortRef> ports)`.  The `MediaConfig::SdiInputSignal`
  / `SdiOutputSignal` keys carry one of these.
- **`HdmiSignalConfig`** — `(VideoPortRef port,
  HdmiSpecVersion versionHint)`.  Carried by the matching
  `HdmiInputSignal` / `HdmiOutputSignal` keys.
- **`VideoReferenceConfig`** — `(VideoReferenceSource source,
  VideoReferenceRateFamily family, VideoPortRef signalPort)`.
  Carried by the `VideoReference` key.  The rate-family enum
  captures the user's "1/1 vs 1/1.001 carrier" point as
  `Integer` vs `Fractional`.

## NTV2-specific types

After the generic carrier types absorb most of what would have
been NTV2-specific config, what's left is genuinely AJA-only and
stays in the `ntv2*` files.

### `Ntv2Capabilities`

Constructed once per `Ntv2Device::acquire`. Cached snapshot of
`CNTV2Card::features()` (number of SDI / HDMI inputs and outputs,
audio system count, ANC extractor / inserter presence, supported
NTV2 pixel formats, support flags like `HasBiDirectionalSDI` /
`CanDoMultiFormat` / `CanDoCustomAnc`). Plus derived booleans the
backend cares about:

- `bool supportsLinkStandard(SdiLinkStandard) const` — answers
  "can this card carry a Quad-Link 12G signal?" without forcing
  callers to know NTV2 device-feature enums.
- `bool hasAudioCounter() const` — drives `Ntv2DeviceClock`'s
  fall-through choice between sample-counter and VBI mode.

### `Ntv2DeviceClock`

(See "Device clock" section above — sample-counter `Clock`
subclass, per-device singleton.)

### What does **not** need an NTV2-specific type

- Port references → `VideoPortRef` (generic).
- Link standard / topology → `SdiLinkStandard` (generic).
- Reference clock source / family → `VideoReferenceConfig` (generic).
- HDMI port spec → `HdmiSignalConfig` (generic).
- The mapping from `SdiLinkStandard` → AJA routing presets lives
  in `ntv2format.cpp` as a private translation table.

## MediaConfig keys to add

Split into two groups: **generic video-I/O keys** (added with the
generic types, reusable by future backends) and **NTV2-specific
keys** (gated by the AJA SDK).

### Generic video-I/O keys (new group in `mediaconfig.h`)

```
SdiInputSignal       SdiSignalConfig         {}            Port + SMPTE link standard (sources).
SdiOutputSignal      SdiSignalConfig         {}            Port + SMPTE link standard (sinks).
HdmiInputSignal      HdmiSignalConfig        {}            HDMI port + version hint (sources).
HdmiOutputSignal     HdmiSignalConfig        {}            HDMI port + version hint (sinks).
VideoReference       VideoReferenceConfig    {FreeRun,Auto} Device-wide reference clock config.
```

`SdiInputSignal` and `HdmiInputSignal` are mutually exclusive on a
single MediaIO (the planner / factory validates exactly one is
set for source mode; same for sinks). A backend that does not
support HDMI returns `Error::NotSupported` if the HDMI key is set;
likewise SDI.

### NTV2-specific keys (in the existing per-backend block)

```
Ntv2DeviceIndex          int        0     Device index, or -1 to use Ntv2DeviceName.
Ntv2DeviceName           String     ""    Device name shorthand or "serial:NNN".
Ntv2Channel              int        1     Logical channel (1-based).
Ntv2AudioSystem          int        -1    Audio system index (-1 = auto-pair with channel,
                                          0..N = explicit, 9999 = disabled).
Ntv2WithAnc              bool       true  Enable ANC extractor (sources) / inserter (sinks).
Ntv2RetailServices       bool       false If true, leave AJA retail services enabled.
                                          Default is to switch to OEM tasks for the duration
                                          of the open.
Ntv2MultiFormatMode      bool       true     Allow channels on this card to run at
                                              independent formats.
Ntv2BufferLockMode       Enum       Auto     Page-lock host frame buffers for DMA
                                              throughput (Auto/On/Off).
Ntv2CaptureTimeoutMs     int        100      AutoCirculate poll timeout.
Ntv2VbiTimeoutMs         int        50       SubscribeInputVerticalEvent poll timeout
                                              used by cancelBlockingWork.
```

Existing keys honoured: `VideoSize`, `VideoPixelFormat`, `FrameRate`,
`AudioRate`, `AudioChannels`, `OpenMode`, `Name`, `Uuid`.

The `Ntv2DeviceIndex` / `Ntv2DeviceName` / `Ntv2Channel` triple
identifies *which logical channel on which AJA card*. Everything
else about "what signal flows through that channel" goes through
the generic keys above. So a single-link 1080p59.94 capture is:

```
Type            = "Ntv2"
Ntv2DeviceIndex = 0
Ntv2Channel     = 1
SdiInputSignal  = {standard: SL_3GA, ports: [sdi1]}
VideoReference  = {source: Genlock, family: Fractional}
VideoSize       = 1920x1080
VideoPixelFormat= YUV10_422_V210_Rec709
FrameRate       = 60000/1001
```

A 4K60 quad-link 2SI capture is the same except for two keys:

```
SdiInputSignal  = {standard: QL_3G_2SI,
                   ports: [sdi1, sdi2, sdi3, sdi4]}
VideoSize       = 3840x2160
```

## Pixel-format mapping (initial scope)

| NTV2 frame-buffer format       | promeki `PixelFormat`                  |
|--------------------------------|----------------------------------------|
| `NTV2_FBF_8BIT_YCBCR` (UYVY)   | `YUV8_422_UYVY_Rec709`                 |
| `NTV2_FBF_8BIT_YCBCR_YUY2`     | `YUV8_422_YUYV_Rec709`                 |
| `NTV2_FBF_10BIT_YCBCR` (V210)  | `YUV10_422_V210_Rec709` (new — see below) |
| `NTV2_FBF_24BIT_RGB`           | `RGB8_sRGB`                            |
| `NTV2_FBF_24BIT_BGR`           | `BGR8_sRGB`                            |
| `NTV2_FBF_ARGB`                | `ARGB8_sRGB`                           |
| `NTV2_FBF_ABGR`                | `ABGR8_sRGB`                           |
| `NTV2_FBF_RGBA`                | `RGBA8_sRGB`                           |
| `NTV2_FBF_10BIT_RGB`           | `RGB10_DPX_Be` (existing `I_3x10_DPX_B`) |
| `NTV2_FBF_48BIT_RGB`           | `RGB16_sRGB`                           |
| `NTV2_FBF_10BIT_YCBCRA`        | (Phase 5) — needs alpha-capable YUV    |
| `NTV2_FBF_10BIT_DPX`           | (Phase 5)                              |

Anything outside the table is rejected with a `proposeInput` /
`proposeOutput` answer that asks the planner to splice a CSC in
front of / behind the channel. The planner already knows how.

`YUV10_422_V210_Rec709` is a possible new addition to the PixelFormat
registry — V210 (32-bit-aligned 6-component 10-bit packing) is the
de-facto AJA / Blackmagic 10-bit YUV wire format. If the new
PixelFormat is too invasive for Phase 1 we'll temporarily land it as
an internal-only mapping that CSCs to/from `YUV10_422_SemiPlanar_LE_Rec709`
on the channel boundary and add the real PixelFormat in Phase 5
once we've decided how V210 fits into the well-known set.

## Video-format mapping

The NTV2 `NTV2VideoFormat` enum encodes raster + frame-rate + scan
mode together (`NTV2_FORMAT_1080p_5994_A`, `NTV2_FORMAT_4x1920x1080p_6000`,
etc.). `ntv2format.cpp` provides:

- `NTV2VideoFormat toNtv2VideoFormat(const ImageDesc &, const FrameRate &)`
- `Error fromNtv2VideoFormat(NTV2VideoFormat fmt, ImageDesc *imgOut,
                              FrameRate *rateOut, ScanMode *scanOut)`

Scan mode goes into `ImageDesc::scanMode` so the interlaced cases
(`NTV2_FORMAT_1080i_5994`, etc.) survive round-trip.

## ANC contract (Phase 5 of devplan/proav/ancdata.md)

The ANC ingest / emit contract is already documented in
`devplan/proav/ancdata.md` Phase 5 — this backend is the producer it
was waiting on. Summary of what `Ntv2MediaIO` must do (capture):

- Require `device->caps().hasCustomAncExtractor`. If absent and
  `Ntv2WithAnc=true`, fail Open with a clear error.
- For each captured frame: scan VANC (and HANC when requested) line
  ranges, build one `AncPayload` listing every ST 291 packet found.
  Each packet uses `transport = St291`, `data` set to the RFC 8331
  per-packet layout (DID, SDID, DataCount, packed UDW, checksum,
  padding), `meta` populated with `Line`, `HOffset`, `FieldB`,
  `CBit`, `StreamNum`.
- Resolve `AncFormat` via `AncFormat::fromSt291DidSdid(did, sdid)`.
  Unknown DID/SDID pairs are still carried as `AncFormat::Invalid`
  — wire fidelity preserved.
- Set `AncDesc::sourceRaster` and `scanMode` to match the paired
  `ImageDesc` so line numbers stay interpretable when the ANC
  payload travels alone.
- Stamp `payload.duration` to one frame period of the session frame
  rate.
- Attribute the ANC payload to the channel's video and audio stream
  indices via `AncDesc::pairedVideoStreamIndex` /
  `pairedAudioStreamIndex` (per the 2026-05-12 AncDesc addition).

Emit side (sink): inverse — accept the listed packets, inject them
at the requested line numbers via `AncInsertSetField`, recompute
checksums where missing, warn (don't error) on out-of-range lines.

The libajantv2 SDK side: `AJAAncillaryList` + the
`AncExtractInit/Start/Stop/GetField` and `AncInsertInit/SetField`
register-level APIs. The capture loop hands the extracted ANC
buffer to a thin `ntv2AncToPackets` converter that emits an
`AncPacket::List`; the inverse on emit.

## Phase plan

Each phase ends in a working, testable, committable slice. Cut
points chosen so we can validate against real hardware as we go.

### Phase 1 — Device layer + device clock + single-channel SDI capture

**Prerequisite:** the generic carrier types and `MediaConfig`
keys from [`video-signal-carriers.md`](video-signal-carriers.md)
must be in the library first.  This phase consumes them; it does
not introduce them.

- `Ntv2Device`, `Ntv2DeviceRegistry`, `Ntv2Capabilities`.
- `Ntv2DeviceClock` — sample-counter clock backing both source
  timestamping and (Phase 2+) sink pacing. Per-device singleton
  vended by `Ntv2Device::sampleClock()`. Includes 32→64 bit wrap
  extension, VBI fallback path, and per-device `ClockDomain`
  registration.
- `Ntv2MediaIO` source mode only, single-link SDI, single channel,
  no ANC, optional audio system 1. The clock is bound to the port
  group via `addPortGroup(name, clock)` on Open.
- Pixel formats: UYVY, V210 (internal — temporary CSC bridge if
  needed), RGB8, BGR8.
- `Ntv2Factory` registers backend; URL `ntv2://<device>/<channel>`
  parses to config; `enumerate()` calls
  `CNTV2DeviceScanner::GetNumDevices` and emits canonical names.
- Doctest coverage: format-mapping helpers (no hardware),
  `Ntv2PortSpec` round-trips, `Ntv2DeviceRegistry` refcount logic,
  `Ntv2DeviceClock` 32→64 bit wrap extension (driven by an injected
  fake-counter source so the test runs without hardware). The
  device-impl mock story (see Open Questions) decides whether the
  clock test uses an `Ntv2DeviceImpl` interface or a friend-class
  test seam.
- Functional test: `tests/func/ntv2-capture-smoke/` — skip when no
  AJA card present; on real hardware it captures 60 frames from
  `ntv2://0/1`, validates frame count / no drops, and asserts the
  port-group clock's per-frame deltas land within
  `± resolutionNs() * 2` of the nominal frame period (i.e. proves
  the sample counter actually drives the clock).

### Phase 2 — Single-channel SDI playout

- `Ntv2MediaIO` sink mode, single-link SDI, single channel, no ANC.
- AutoCirculate output path, audio system playback.
- `proposeInput` matches the supported pixel-format table; otherwise
  asks for the closest acceptable shape so the planner splices a
  CSC.
- Sink clock — default mode (card paces itself);
  `executeCmd(SetClock)` returns `Error::NotSupported` for now.
  External-pacing support is Phase 6.
- The port group's clock is the device sample clock so downstream
  consumers can still read meaningful times against the sink — even
  without external pacing, the playout-side counter is the
  authoritative wall time at the SDI connector.
- Doctest: `proposeInput` covers each NTV2 frame-buffer format and
  a few unsupported ones.

### Phase 3 — ANC capture + insertion

- `ntv2AncToPackets` / `packetsToNtv2Anc` converters.
- Source: optional ANC extractor wired into the capture loop,
  builds `AncPayload` per the Phase 5 contract above.
- Sink: optional ANC inserter consumes the frame's `AncPayloads`,
  injects per-line.
- Functional test: `tests/func/ntv2-anc-roundtrip/` — TPG with
  CEA-708 captions → NTV2 sink → physical SDI loop → NTV2 source →
  Inspector AncData → byte-exact compare of the cc_data triples.
  (Requires SDI loopback cabling on the test rig.)

### Phase 4 — Multi-channel concurrent

- N independent `Ntv2MediaIO`s on one card open at once.
- Validate `MultiFormatMode` (each channel free to run its own
  format).
- Device-wide setting conflicts: when channel A sets
  `Ntv2ReferenceStandard=Hz_1_1` and channel B opens with
  `Hz_1_1001`, log a warning naming both channels but apply B's
  value (the user-explicit setting wins; the prior channel doesn't
  fail). Test covers the warning path.
- Audio-system arbitration: `Ntv2AudioSystem=-1` auto-pairs with
  the channel index; explicit values reject double-allocation.
- Card teardown happens on the last release.
- Functional test: capture from two channels concurrently to two
  files, compare per-channel frame counts.

### Phase 5 — Multi-link 4K (Quad-Link + 12G)

- `Ntv2PortSpec` with 4 ports + `QuadLinkSquareDivision` /
  `QuadLink2SI`; `Ntv2Device::reservePhysicalPorts` enforces no
  overlap.
- Signal routing builds the SDR / 2SI routing tree across the four
  framestores under one logical channel.
- 12G single-link as the simpler analogue: one port, one channel,
  but routing differs from 3G single-link.
- Pixel-format additions if needed: `YUV10_422_V210_Rec709`
  promoted to a first-class PixelFormat with full Variant /
  DataStream registration, replacing the Phase-1 internal-only
  bridge.
- Functional test: 4K60 capture via Quad-Link 2SI on hardware that
  has the four required SDI inputs.

### Phase 6 — Genlock, external pacing, drift-aware clock

- Genlock support: `Ntv2ReferenceSource=Genlock` (or `External`) on
  the device, with signal-loss detection on the reference input.
  Loss surfaces as `MediaIO::errorOccurred(Error::SignalLoss)` and
  an `InspectorDiscontinuity` event so downstream stages can react.
- Sink external pacing: `executeCmd(SetClock)` on sinks accepts a
  caller-supplied `Clock::Ptr`; `executeCmd(Write)` blocks on a
  `PacingGate` against that clock before submitting to
  AutoCirculate. Enables locking two AJA cards (different
  `Ntv2DeviceClock` domains) to a shared external reference such
  as PTP or another card's sample clock.
- Drift-aware `rateRatio()`: long-window estimate of the device
  sample clock vs. wall clock so downstream drift correction (audio
  resamplers, video frame-syncs) can pull the true rate from
  `clock->rateRatio()` instead of measuring it themselves.
- HDR / colorimetry signaling — HDR Static Metadata on SDI rides
  ANC-side, but the device may need to be told to allow the VPID
  bits that advertise it.

## Cross-cutting / library follow-ups likely surfaced

- **New `PixelFormat::YUV10_422_V210_Rec709`** (Phase 5) — sets a
  precedent for AJA / BMD's 10-bit packed wire format and
  potentially a new CSC bridge.
- **`MediaConfig::Ntv2*` family** — adds the SDK-specific config
  group alongside `Ndi*` / `V4l2*`.
- **`MediaIOFactory::queryDevice`** for NTV2 — should return the
  available video format list per requested channel (mirrors the
  V4L2 implementation which enumerates device modes).
- **`MediaIOFactory::printDeviceInfo`** for NTV2 — `mediaplay --probe`
  output for AJA devices: list channels, current input signals,
  link capabilities, ANC engine count.
- **`MediaIOAllocator::makePinnedHostAllocator`-style placement** —
  NTV2 DMA benefits from page-locked host buffers exactly like NDI.
  Reuse `NdiMediaIO::makePinnedHostAllocator` directly (move the
  helper to a shared `MediaIOAllocator` static if a second backend
  wants it).

## Open questions

- **Mock layer for unit tests.** `CNTV2Card` is hard to mock — most
  methods are non-virtual and the SDK assumes a real driver. Options:
  (a) inject an `Ntv2DeviceImpl` interface that production wraps
  `CNTV2Card` and tests fake out; (b) only test format / port /
  registry logic without touching `Ntv2Device`; (c) add a small
  doctest gate for hardware-required tests, default-skipped.
  Probably some combination. Decide before Phase 1 code lands.
- **OEM task mode for non-acquired channels.** When the user runs
  `Ntv2RetailServices=true` we keep the retail tasks running; the
  AJA OEM task switch on `AcquireStreamForApplication` may still
  fight us. Confirm the right call sequence on a system with the
  AJA control panel installed.
- **HDMI input on KonaHDMI.** Single-card KonaHDMI exposes 4 HDMI
  inputs and the SDK auto-maps SDI channel numbers to HDMI inputs
  (see `ntv2capture.cpp` line 86). We model HDMI ports explicitly
  in `Ntv2PortSpec` so the mapping is the user's choice, not the
  SDK's surprise.
- **HDMI 2.0 8K** — single-connector multi-lane. May or may not need
  multi-physical-port modelling; revisit when we have an 8K HDMI
  card to test against.
- **`AJAAncillaryList`** vs hand-rolled byte parsing. The SDK ships
  helpers that decode the captured ANC blob into per-packet
  structures. Probably worth using rather than reimplementing the
  decode side; the emit side may be cleaner hand-rolled because we
  control the on-wire layout exactly.
- **GPUDirect RDMA path.** `AJANTV2_DISABLE_RDMA=ON` at build time,
  so this is off the table for now. When CUDA-resident frame
  buffers become a thing (see `devplan/core/systemcow-mediaio-allocator.md`),
  revisit and re-enable RDMA so frames go straight from the AJA DMA
  engine into device memory.
- **Detection of card reboot / driver restart mid-open.** AJA's
  driver can be unloaded out from under us. Need to decide whether
  to surface that as `errorOccurred(DeviceError)` and fail-close, or
  attempt automatic recovery on the next VBI.
- **Which audio-system counter drives the device clock when both
  source and sink channels are open on the same card?** Capture-
  channels naturally read `ReadAudioLastIn` and playout-channels
  naturally read `ReadAudioLastOut`. The two run off the same
  reference so their rates match, but their epochs differ by the
  output FIFO depth. Pick one canonically and document; probably
  `In` when any source channel is open, `Out` otherwise.
- **Sample-counter availability on T-Tap / playback-only cards.**
  Capture-side `ReadAudioLastIn` may not be meaningful on a card
  with no input. Validate the capability via
  `Ntv2Capabilities::hasAudioCounter` (computed at device acquire
  time) and pick the right register or fall back to VBI mode
  accordingly.
- **`Ntv2DeviceClock` epoch on first open vs. after device close +
  reopen.** The sample counter is free-running on the card across
  process restarts; our 64-bit shadow starts at zero on every
  device acquire. The reported clock therefore has a per-acquire
  epoch, which is correct for in-pipeline timing but means saved
  timestamps don't round-trip across a close. Document; the
  alternative (anchoring to wall time on first read) loses the
  monotonicity guarantee on long-running processes.
- **`HdmiInfoFrame` collision check.** There's already a class
  named `HdmiInfoFrame` (`include/promeki/hdmiinfoframe.h`) in the
  ANC stack — that's a *typed packet helper*, unrelated to the new
  carrier-config `HdmiSignalConfig`. Names don't collide, but a
  reader skimming the file list might assume kinship. Worth a
  short Doxygen "see also" both ways once the new types land.
- **`SdiLinkStandard::Auto` semantics on sinks.** On a source,
  `Auto` means "detect what's arriving." On a sink, there is
  nothing to detect — the standard is forced by the chosen
  `VideoFormat` + cable count. Either reject `Auto` on sinks at
  Open, or infer the standard from the offered `MediaDesc` and
  log what we picked. Probably the latter (matches the planner's
  proposeOutput flow).
- **Embedding ST 2110 / ST 2022-6 in the same generic types
  later.** Network SDI carriers don't have a physical
  `VideoPortRef`, but they do still pick an SMPTE link standard
  (the originating SDI signal's standard, embedded in the IP
  stream). Probably grows a sibling config — `SmpteIpSignalConfig`
  with an SDP-file or RTP-stream reference instead of a
  `VideoPortRef` — when we get there. Worth keeping
  `SdiSignalConfig` focused on physical SDI to leave room.

## Validation checklist (running, per phase)

- `build` clean, warnings-free.
- `build check` passes (unit tests).
- `tests/func/ntv2-*` passes on a rig with an AJA card; documented
  as `SKIP` when no card is present.
- `mediaplay --probe ntv2://0/1` lists the device + current input
  signal.
- `mediaplay ntv2://0/1 -o /tmp/cap.mov -d 5s` captures 5 seconds
  from channel 1 to QuickTime.
- `mediaplay /path/to/clip.mov -o ntv2://0/2` plays a file out
  channel 2.
- Multi-channel: two `mediaplay` instances driving `ntv2://0/1` and
  `ntv2://0/2` simultaneously without interference.

## References

- `docs/mediaio.dox` — MediaIO framework authoring guide.
- `devplan/proav/backends.md` — registered backend status board.
- `devplan/proav/ancdata.md` Phase 5 — the SDI ANC ingest / emit
  contract this backend must satisfy.
- `devplan/proav/backend-thread-naming.md` — name spawned worker
  threads from the stage's `name()`.
- `thirdparty/libajantv2/demos/ntv2capture/` — single-channel SDI
  capture reference impl (AJA).
- `thirdparty/libajantv2/demos/ntv2player/` — single-channel SDI
  playout reference impl (AJA).
- `thirdparty/libajantv2/demos/ntv2qtmultiinput/` — multi-channel
  Qt example (uses `NTV2FrameGrabber` per channel).
- `thirdparty/libajantv2/demos/ntv2llburn/` — low-latency capture
  + burn-in + emit; reference for the loop-through pipeline shape.
- `include/promeki/ndimediaio.h` + `src/proav/ndimediaio.cpp` —
  closest comparable backend (dedicated worker, internal capture
  thread, source / sink in one class, pinned-host allocator).
- `include/promeki/v4l2mediaio.h` + `src/proav/v4l2mediaio.cpp` —
  the other comparable; useful for capture-thread pattern and
  device hot-unplug handling.
