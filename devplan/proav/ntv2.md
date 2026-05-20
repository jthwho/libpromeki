# AJA NTV2 MediaIO Backend

**Library:** `promeki` (gated by `PROMEKI_ENABLE_NTV2`)
**Status:** **Phases 1–6 complete (2026-05-17), including the
Phase 6.4 HDR/colorimetry VPID work that was originally deferred.
Open-questions cleanup pass landed 2026-05-19: sink-side
`SdiLinkStandard::Auto` inference, driver-restart detection, and
several Doxygen / documentation fixes.**
Source-mode capture and sink-mode playout both ship for single-link
SDI and multi-link (QL_3G_SQD, QL_3G_2SI, SL_12G) topologies; the
capture and emit paths carry ancillary data through
`AUTOCIRCULATE_WITH_ANC` with the `ntv2anc.{h,cpp}` GUMP↔`AncPayload`
converters; per-frame PTS and CaptureTime ride on AutoCirculate's
`FRAME_STAMP`; multiple channels on the same card open concurrently
against a shared refcounted `Ntv2Device` with exclusive channel /
port / audio-system reservations, deadlock-free error rollbacks,
and the device-wide reference-clock conflict warning path; the
pixel-format mapping covers UYVY / YUY2 / 8-bit RGB family / V210 /
10-bit DPX (both endians) / 48-bit RGB; on-board CSC widgets bridge
framebuffer-vs-wire colour-family mismatches inside the routing
fabric and the negotiator honours an `Ntv2DisableOnBoardCsc`
opt-out; sinks fan one outbound signal across N port groups via the
`SdiOutputFanoutConfig` carrier and `SdiOutputFanout` config key;
the capture worker now polls `GetInputVideoFormat` every
`Ntv2SignalPollIntervalVbi` VBIs and emits
`errorOccurredSignal(Error::SignalLoss)` on present→absent
transitions; sink channels accept a caller-supplied `Clock::Ptr`
through `executeCmd(MediaIOCommandSetClock)` and wait on an
embedded `PacingGate` before each `AutoCirculateTransfer` so two
cards can be locked to a shared external reference without a
hardware genlock cable; `Ntv2DeviceClock::rateRatio()` returns a
long-window drift estimate of device-counter vs host wall time;
SMPTE ST 352 VPID byte-4 (transfer / colorimetry / luminance /
RGB range) is stamped on sink outputs from the framestore colour
model + per-frame metadata + configurable `Ntv2Vpid*Override` keys,
and the capture worker reads the incoming VPID at open + each
signal-poll cycle and stamps the decoded transfer / colorimetry /
range on every captured Frame's metadata.
The 2026-05-19 pass adds the `SdiWireFormat` enum + the
`sdiWireFormatFor(PixelFormat)` and
`inferSdiLinkStandard(VideoFormat, SdiWireFormat, cableCount)`
helpers in `sdiwireinference.h` for sink-side
`SdiLinkStandard::Auto` resolution (with on-board CSC overriding
the RGB framebuffer's natural wire payload to
`YCbCr_422_10` when CSC is enabled), and both worker loops now
poll `CNTV2Card::IsOpen()` every `Ntv2SignalPollIntervalVbi`
iterations to detect driver restart / hot-unplug, surfacing the
event via the new `StatsDeviceLost` counter +
`errorOccurredSignal(Error::DeviceError)`.  14 new doctest cases
in `tests/unit/sdiwireinference.cpp` cover `sdiBitsPerPixel(SdiWireFormat)`,
`sdiWireFormatFor(PixelFormat)`, and `inferSdiLinkStandard` boundary
cases; 1 new NTV2-specific case covers the `StatsDeviceLost` ID.
75 doctest cases cover the hardware-free units (format mappings,
32→64 bit clock-counter wrap extension, `mediaTimeStampFromSamples`
for FRAME_STAMP → device-clock-domain conversion, drift-aware
rateRatio convergence + VBI-mode 1.0 invariant, ANC encode/decode
round-trips including F2 + unregistered DID/SDID, reservation
atomicity + idempotency + busy conflicts, reference-clock
apply-new-on-conflict, single-link + quad-link Squares + quad-link
2SI + 12G + CSC-inserted + fanned-out crosspoint routing tables,
sink-mode `proposeInput` negotiation including the CSC opt-out
path, fanout-config string round-trip, URL parsing,
`Error::SignalLoss` identity, factory configSpecs for the new
Phase-6 keys, VPID transfer / colorimetry / luminance / RGB-range
mapping round-trips + SDR fall-through + out-of-range clamps).
Functional hardware tests remain TODO — the rig needs an AJA card
present.
**Standards:** All work follows `CODING_STANDARDS.md`. Every new
class requires complete doctest coverage. See `devplan/README.md`
for the full requirements.
**Depends on:** the generic carrier types
(`VideoPortRef`, `SdiSignalConfig`, `HdmiSignalConfig`,
`VideoReferenceConfig`) and their matching `MediaConfig` keys —
shipped 2026-05-16, devplan retired.

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

`raw()` reads `CNTV2Card::GetRawAudioTimer()` — the FPGA-resident
`kRegAud1Counter`, a single free-running 48 kHz counter that ticks
from FPGA load at power-up and is independent of any audio system
being reserved or capturing.  That's the same register the AJA
driver pre-extends and reports as `FRAME_STAMP::acAudioClockTimeStamp`
on every captured frame, which is what lets the capture loop bind a
per-frame PTS via `mediaTimeStampFromSamples` and land in the exact
same time base as `now()`.  The register is 32-bit and wraps every
~24.8 hours at 48 kHz; the clock keeps a 64-bit shadow that gets
advanced atomically on every read by comparing the new sample value
against the last-seen one and incrementing the high 32 bits on
detected wrap. `raw()` then converts to nanoseconds via the audio
sample rate.

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

### Fallback when no FPGA audio counter exists

The `kRegAud1Counter` register is part of the audio subsystem in
every shipping NTV2 FPGA, so in practice this fallback is dead
code on real hardware.  It exists as a safety net for hypothetical
future cards that ship without the audio subsystem entirely
(`Ntv2Capabilities::hasAudioCounter() == false`), in which case the
clock constructs in **VBI-fallback mode**:

- `raw()` returns the host-side wall time at the most-recent
  `SubscribeInputVerticalEvent` wake (one tick per frame period).
- `resolutionNs()` reports one frame period.
- `jitter()` widens to ± half a frame period.

A warning logs once when this happens.  Mode is latched at clock
construction — there is no runtime promotion path, since silently
upgrading resolution mid-stream would break the monotonic-clamp
contract for consumers already latched onto the lower-resolution
clock.

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

## Generic video-signal types (shipped 2026-05-16)

The carrier-level types this backend consumes —
`VideoPortRef`, `SdiSignalConfig`, `HdmiSignalConfig`,
`VideoReferenceConfig`, plus the matching `MediaConfig` keys
(`SdiInputSignal`, `SdiOutputSignal`, `HdmiInputSignal`,
`HdmiOutputSignal`, `VideoReference`) — landed as their own
changeset and the per-type devplan retired.

They are not NTV2-specific (DeckLink, ST 2022-6, ST 2110-20, and
any future hardware-SDI / SDI-over-IP backend reuses them
unchanged), do not depend on the AJA SDK, and are independently
useful for tooling.  See `include/promeki/videoportref.h`,
`sdisignalconfig.h`, `hdmisignalconfig.h`, and
`videoreferenceconfig.h` for the type definitions / `PROMEKI_DATATYPE`
ids / CoW `SharedPtr<Impl>` conventions.

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
                                          0 = disabled, 1..N = explicit).
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
Ntv2DisableOnBoardCsc    bool       false    Force a software CSC bridge on every RGB↔YCbCr
                                              boundary instead of using the card's on-board CSC
                                              widgets.
SdiOutputFanout          SdiOutputFanoutConfig {} Multi-destination SDI fanout — one outbound
                                              signal driven out N matching port groups.  Supersedes
                                              SdiOutputSignal when set.  String form
                                              "standard:p1+p2,p3+p4".
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

## Pixel-format mapping

| NTV2 frame-buffer format          | promeki `PixelFormat`             | Notes                              |
|-----------------------------------|-----------------------------------|------------------------------------|
| `NTV2_FBF_8BIT_YCBCR` (UYVY)      | `YUV8_422_UYVY_Rec709`            | Default capture/playout format     |
| `NTV2_FBF_8BIT_YCBCR_YUY2`        | `YUV8_422_Rec709`                 |                                    |
| `NTV2_FBF_10BIT_YCBCR` (V210)     | `YUV10_422_v210_Rec709`           | AJA-native 10-bit YCbCr packing    |
| `NTV2_FBF_24BIT_RGB`              | `RGB8_sRGB`                       |                                    |
| `NTV2_FBF_24BIT_BGR`              | `BGR8_sRGB`                       |                                    |
| `NTV2_FBF_ARGB`                   | `ARGB8_sRGB`                      |                                    |
| `NTV2_FBF_ABGR`                   | `ABGR8_sRGB`                      |                                    |
| `NTV2_FBF_RGBA`                   | `RGBA8_sRGB`                      |                                    |
| `NTV2_FBF_10BIT_DPX`              | `RGB10_DPX_sRGB`                  | DPX Method A (big-endian on wire)  |
| `NTV2_FBF_10BIT_DPX_LE`           | `RGB10_DPX_LE_sRGB`               | DPX little-endian variant          |
| `NTV2_FBF_48BIT_RGB`              | `RGB16_LE_sRGB`                   | 16 bits per channel, host order    |
| `NTV2_FBF_10BIT_YCBCRA`           | — (no alpha-capable YUV yet)      | Add when an RGBA-style YUV ships   |
| `NTV2_FBF_10BIT_RGB`              | — (RGB10A2 packing, byte-order TBD) | Wire when a card needs it        |
| `NTV2_FBF_10BIT_RGB_PACKED`       | — (3x10 packed RGB)               | Wire when a card needs it          |

Anything outside the table flows through the negotiator's
fallback path (see "On-board CSC negotiation" below): the
planner is either handed a same-family alternative or, when the
on-board CSC is in play, a cross-family target that the routing
fabric bridges in hardware.

### On-board CSC negotiation

AJA cards expose per-channel Colour-Space Converter widgets that
can bridge an RGB ↔ YCbCr mismatch between a framestore and the
wire inside the routing fabric — much cheaper than a host-side
CSC pass.  @ref Ntv2Routing::Config carries
`framebufferRgb`, `signalRgb`, and `allowOnBoardCsc` toggles; the
helper inserts one CSC per quadrant when the families differ and
`allowOnBoardCsc` is true.

The negotiator (`Ntv2MediaIO::proposeInput`) decides whether a
cross-family request needs CSC at all:

- Default (CSC enabled via @c Ntv2DisableOnBoardCsc=false): an
  upstream offering an RGB framestore for an SDI source is
  accepted as-is — the on-board CSC handles the wire-to-FB
  conversion at routing time.
- `Ntv2DisableOnBoardCsc=true`: the negotiator falls back to the
  wire's colour family so the routing path never picks up a CSC.
  Users with a tuned host CSC pipeline (GPU / SIMD) reach for
  this to keep the on-board CSCs reserved for routing they drive
  themselves.

Wire colour family is presently fixed at YCbCr (SDI is YCbCr
overwhelmingly).  Dual-link RGB and HDMI RGB paths will flip
`signalRgb` once those wire kinds ship.

## Output fanout (one signal → many SDI destinations)

The AJA crosspoint fabric lets one framestore-output crosspoint
drive multiple SDIOut-input crosspoints simultaneously, which is
the right primitive for monitor-out / redundancy / loop-thru use
cases.  Exposed as a new carrier type @ref SdiOutputFanoutConfig
and the @ref MediaConfig::SdiOutputFanout key:

- @ref SdiOutputFanoutConfig pairs a single @ref SdiLinkStandard
  with @em multiple port groups.  Each group must have exactly
  @c cablesFor(standard) ports — single-link standards take
  1 port per group, dual-link 2, quad-link 4.  String form:
  `standard:g1ports,g2ports,...` with `+` between ports inside a
  group and `,` between groups.
- When @c SdiOutputFanout is set on the sink config, it
  supersedes @c SdiOutputSignal.  The first group is the primary
  destination; subsequent groups are mirrors.  The open path
  reserves @em every port across all groups, switches each one to
  transmit via @c SetSDITransmitEnable, and asks the routing
  helper for the fanned-out crosspoint list.
- @ref Ntv2Routing::Config now carries a @c mirrorPortStarts list
  — additional 1-based SDI port starts to replay the same routing
  pattern against, all sourced from the same framestore.  Source
  mode ignores @c mirrorPortStarts (mirroring an SDI input has no
  meaning).

Examples (string form passed in as @c SdiOutputFanout):

| String                                 | Effect                                                  |
|----------------------------------------|---------------------------------------------------------|
| `sl_3ga:sdi1`                          | Single-link, no fanout — equivalent to `SdiOutputSignal`. |
| `sl_hd:sdi1,sdi2,sdi3`                 | Single-link HD signal driven out SDI 1, 2, and 3.       |
| `dl_3g:sdi1+sdi2,sdi3+sdi4`            | Dual-link signal on (SDI 1, SDI 2) plus a mirror on (SDI 3, SDI 4). |
| `ql_3g_2si:sdi1+sdi2+sdi3+sdi4,sdi5+sdi6+sdi7+sdi8` | Quad-link 2SI signal across 8 SDI outputs (one source, two destination groups). |

Dual-link and SL_24G routing tables are still empty (the helper
returns no connections), so dual-link fanout will land when the
DL routing table itself does; the carrier type and config plumbing
are in place either way.

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

**Progress:** Phases 1 + 2 complete (2026-05-16, both same day);
Phase 3 (ANC capture + insertion) complete (2026-05-17);
Phase 4 (multi-channel concurrent) complete (2026-05-17);
Phase 5 (multi-link 4K Quad-Link + 12G) complete (2026-05-17);
Phase 6 (genlock + signal-loss detection + external pacing +
drift-aware clock + HDR / colorimetry VPID) complete (2026-05-17).

### Phase 1 — Device layer + device clock + single-channel SDI capture — **DONE 2026-05-16**

**What shipped:**

- `Ntv2Device`, `Ntv2DeviceRegistry`, `Ntv2Capabilities`.
- `Ntv2DeviceClock` — sample-counter clock with VBI-fallback path,
  per-device `ClockDomain`, and a 32→64 bit shadow-counter wrap
  extension serialised by an internal mutex.  Per-device singleton
  vended by `Ntv2Device::sampleClock()`; a static `createForTest`
  factory + `setCounterSourceForTest` injection seam lets the
  hardware-free unit tests drive the wrap arithmetic without an AJA
  card (chose the test-seam route from the Open-Questions list
  rather than an `Ntv2DeviceImpl` interface).  `noteVbi` takes a
  `TimeStamp` so callers don't manually extract ns at the boundary.
- `Ntv2MediaIO` source mode — single-link SDI, single channel, no
  ANC, optional audio system 1.  The clock is bound to the port
  group via `addPortGroup(name, clock)` on Open.  Audio system is
  reserved when requested for future audio-payload emission, but
  is **no longer load-bearing for clocking** — `Ntv2DeviceClock`
  reads the shared FPGA `kRegAud1Counter`, which ticks regardless
  of any audio system being captured.
- Pixel formats: UYVY, YUYV, RGB8, BGR8, ARGB, ABGR, RGBA (7 first-
  class mappings).  V210 / 10-bit RGB / 48-bit RGB land in Phase 5
  alongside the V210 first-class PixelFormat decision.
- `Ntv2Factory` registers backend via `PROMEKI_REGISTER_MEDIAIO_FACTORY`
  at TU load time; URL `ntv2://<device>/<channel>` parses to config
  (integer / name-shorthand / `serial:` host all supported);
  `enumerate()` walks `CNTV2DeviceScanner::GetNumDevices` and emits
  one URL per channel each card exposes; `--list-io` shows
  `Ntv2 I/O AJA NTV2 SDI / HDMI capture and playout via libajantv2`.
- Signal routing: per-channel single-link Connect
  (`NTV2_XptFrameBuffer{N}_Input ← NTV2_XptSDIIn{N}`) for source.
- Worker threads run as `Thread` subclasses (`CaptureWorker`),
  named `ntv2cap:<deviceIdx>:<channel>` so several capture workers
  across one or more cards stay distinguishable in `top -H` / logs.
- All atomics / threads / sleeps use the library's `Atomic<T>` /
  `BasicThread` / `BasicThread::sleep*` rather than `std::*` (per the
  prefer-own-classes feedback).

**Tests landed:**

- `tests/unit/ntv2format.cpp` — round-trip coverage for every
  PixelFormat the table maps, every supported broadcast video
  format (HD/SD progressive + interlaced), channel index ↔ enum,
  SDI/HDMI port → input source, reference-source mapping, link-
  standard cable-count check.
- `tests/unit/ntv2clock.cpp` — `createForTest` factory, 32→64 wrap
  arithmetic across a full counter rollover via an injected fake
  counter, counter-source failure → `Error::DeviceError`,
  `mediaTimeStampFromSamples` conversion across multiple sample
  rates and beyond the 32-bit range, VBI-fallback resolution /
  `noteVbi` advancement.

**Tests still to land (need hardware):**

- `tests/func/ntv2-capture-smoke/` — capture 60 frames from
  `ntv2://0/1`; validate frame count / no drops; assert port-group
  clock per-frame deltas land within `± resolutionNs() * 2` of the
  nominal frame period (proves sample counter actually drives the
  clock).

### Phase 2 — Single-channel SDI playout — **DONE 2026-05-16**

**What shipped:**

- `Ntv2MediaIO` sink mode — single-link SDI, single channel, no ANC.
  `openSink` / `closeSink` mirror the source-mode shape; audio
  system reservation in place (audio frame submission deferred to a
  follow-up, same as Phase 1 capture).
- `AutoCirculateInitForOutput` + per-channel `PlayoutWorker` (named
  `ntv2pb:<deviceIdx>:<channel>`) that drains a bounded
  `WriteQueueDepth=4` queue and submits via `AutoCirculateTransfer`.
  Pre-buffers 3 frames before calling `AutoCirculateStart` (mirrors
  the player demo's full-pipe start).  Strand-side
  `executeCmd(Write)` enqueues; on overflow the oldest frame is
  dropped + counted (`StatsFramesDroppedSink`) rather than blocking
  the strand worker.
- Sink-side `proposeInput`: pass-through when the offered
  PixelFormat already maps to an NTV2 frame-buffer format,
  otherwise picks a same-color-family fallback
  (`YUV8_422_UYVY_Rec709` for YCbCr, `RGB8_sRGB` for the rest) so
  the planner inserts a CSC bridge instead of failing open with
  `FormatMismatch`.  Audio-only descriptors pass through unchanged.
- `executeCmd(SetClock)` returns `Error::NotSupported` (external
  pacing is Phase 6).  The port group's clock is still the device
  sample clock so downstream consumers see meaningful times against
  the sink connector.
- Sink-side signal routing: per-channel single-link Connect
  (`NTV2_XptSDIOut{N}_Input ← NTV2_XptFrameBuffer{N}YUV`, or
  `…RGB` when the format's color model is non-YCbCr).
- Sets bi-directional SDI to transmit direction on cards with
  `HasBiDirectionalSDI` capability.
- Factory now advertises `canBeSink() == true`.

**Tests landed:**

- `tests/unit/ntv2mediaio.cpp` — factory advertises both source +
  sink, URL parsing across the three shapes (`ntv2://0/1`,
  `ntv2://kona5/2`, `ntv2:///3`), `proposeInput` passes every
  supported NTV2 frame-buffer format through unchanged, requests
  UYVY for unmapped YCbCr / RGB8 for unmapped non-YCbCr, leaves
  audio-only descriptors untouched, and rejects a null output
  pointer.

**Tests still to land (need hardware):**

- Functional playout test — feed TPG → `ntv2://0/2`, verify SDK
  reports the expected goodXfer count over a fixed wall window,
  hook up an SDI loop to capture and compare against a reference
  TPG render.

### Phase 3 — ANC capture + insertion — **DONE 2026-05-17**

**What shipped:**

- `ntv2anc.{h,cpp}` — pure conversion helpers between AJA's GUMP
  per-field byte buffers and libpromeki's `AncPayload` /
  `AncPacket(St291)` types.  `ntv2AncToPackets` parses F1 and F2
  separately so the F-bit (`AncMeta::St291::FieldB`) survives on
  the produced packets; `packetsToNtv2Anc` is the inverse, using
  `AJAAncillaryList::GetTransmitData` to fill the GUMP buffers.
  Unknown DID/SDID pairs round-trip as `AncFormat::Invalid` (wire
  fidelity preserved).
- `Ntv2MediaIO` capture path requests `AUTOCIRCULATE_WITH_ANC`
  when `Ntv2WithAnc=true` (the default) and the card exposes the
  custom-ANC engine; attaches resident 2 KB F1/F2 buffers to
  every `AutoCirculateTransfer` via `xfer.SetAncBuffers`; decodes
  to an `AncPayload` after each transfer; attaches the payload
  alongside the video on the produced `Frame`.  Frames are now
  assembled on the capture thread and queued whole so the strand's
  `executeCmd(Read)` is a pure drain.  Downgrades gracefully (warns
  once + continues video-only) on cards without `CanDoCustomAnc`.
- `Ntv2MediaIO` sink path symmetric: requests
  `AUTOCIRCULATE_WITH_ANC`; pulls every `AncPayload` off the
  incoming `Frame`; encodes via `packetsToNtv2Anc` into the
  resident F1/F2 buffers (using the cached interlaced
  `f2StartLine` derived from `NTV2SmpteLineNumber::GetLastLine`);
  attaches via `xfer.SetAncBuffers`.  Multi-payload frames merge
  before encode.  Out-of-range lines log a warning and the rest
  of the frame still ships (per the contract).
- `MediaIOStats::Ntv2AncPacketsReceived` and
  `Ntv2AncPacketsSent` telemetry IDs added so consumers can see
  ANC throughput per channel.

**Tests landed:**

- `tests/unit/ntv2anc.cpp` — four hardware-free round-trip cases:
  single CEA-708 packet through GUMP encode → decode with full
  wire-byte verification; mixed CEA-708 + ATC LTC + an
  unregistered DID/SDID packet across F1 and F2 on interlaced
  1080i59.94; empty payload produces a zeroed buffer that
  decodes to no packets; null-F1-buffer rejection.

**Tests still to land (need hardware):**

- `tests/func/ntv2-anc-roundtrip/` — TPG with CEA-708 captions →
  NTV2 sink → physical SDI loop → NTV2 source → Inspector AncData
  → byte-exact compare of the cc_data triples.  (Requires SDI
  loopback cabling on the test rig.)

### Phase 4 — Multi-channel concurrent — **DONE 2026-05-17**

**What shipped:**

- `Ntv2MediaIO` open paths (source + sink) refactored to release
  the device mutex before any `closeSource()` / `closeSink()`
  rollback.  `promeki::Mutex` wraps a non-recursive `std::mutex`,
  and the previous code held the device mutex across half the
  open routine while every error branch called the close path —
  which itself takes the mutex.  A second channel open hitting
  one of those error paths would have deadlocked the first
  channel's strand.  The fix is a tight `do { Mutex::Locker
  lk(...); ... } while (false);` scope plus a deferred
  `Error configErr` check so close runs after the lock releases.
- `Ntv2DeviceTestAccess` (declared as a friend struct since
  Phase 1, defined now) hardware-free seam: builds an
  `Ntv2Device` with a hand-crafted `Ntv2Capabilities` and a null
  `_card`, plus inspectors for the channel-owner, port-owner,
  audio-system-owner tables and the reference-clock state.
- `Ntv2Capabilities::createForTest` static factory hands the
  test seam a populated capability snapshot without touching a
  real `CNTV2Card`.
- `MultiFormatMode` continues to be applied on the first
  `Ntv2DeviceRegistry::acquire` call (`ntv2device.cpp` line 124,
  unchanged); the per-acquire `multiFormat` flag is honoured
  only on the first acquire (subsequent acquires of the same
  card see the existing entry).
- Audio-system arbitration confirmed: `Ntv2AudioSystem=-1`
  auto-pairs with the channel index (openSource line 274), `0`
  disables, explicit `1..N` either succeeds or — on conflict —
  warns and continues video-only on the channel without
  blocking the open (the channel-level reservation still
  succeeds, so the open as a whole proceeds).
- Device-wide reference conflict already implemented in
  `Ntv2Device::setReference`: warns when the requester changes
  under concurrent owners but applies the new request and
  updates `_refOwner`.  The prior owner doesn't retroactively
  fail.
- Card teardown on last release already in `Ntv2DeviceRegistry::release`:
  refcount transition to zero calls `Ntv2Device::shutdown` which
  drops the shared `Clock::Ptr`, releases the stream, and resets
  the `CNTV2Card` handle.

**Tests landed:**

- `tests/unit/ntv2device.cpp` (six cases, 66 assertions):
  - Channel reservation double-allocation → `Error::Busy`;
    idempotent same-owner re-reservation; release-then-reclaim;
    out-of-range channel index → `Error::InvalidArgument`.
  - Port reservation atomicity on conflict — partial requests
    leave no half-state behind, so the same owner can still
    claim the conflict-free ports afterwards.
  - `releasePortsOwnedBy` releases only the requester's ports;
    other owners' reservations untouched.
  - Audio-system double-allocation → `Error::Busy`; idempotent
    same-owner re-reservation; release-then-reclaim;
    out-of-range index rejection.
  - `setReference` apply-new-on-conflict: ownership transfers
    to the new requester, the raw NTV2ReferenceSource value
    updates, idempotent same-owner reissue is a no-op.
  - Two-owner concurrent reservation: channel + ports + audio
    system independently allocated on the same device, a
    one-owner close leaves the other untouched.

**Tests still to land (need hardware):**

- Two concurrent `mediaplay` captures from `ntv2://0/1` and
  `ntv2://0/2` on a real card, comparing per-channel frame
  counts.
- Device-wide reference change between two captures with
  different rate families; verify the warning fires and the
  card's reference register reflects the second request.

### Phase 5 — Multi-link 4K (Quad-Link + 12G) — **DONE 2026-05-17**

**What shipped:**

- `Ntv2Routing` (`ntv2routing.{h,cpp}`) — pure connection-list
  builder for single-link, Quad-link Squares, Quad-link 2SI, and
  12G single-link source + sink crosspoint topologies.  Inputs:
  `SdiLinkStandard`, 1-based framestore start, 1-based SDI port
  start, the card's `CanDo12gRouting` capability, the
  framestore RGB flag.  Outputs: `Ntv2Routing::ConnectionList`
  (one `(input, output)` crosspoint pair per `CNTV2Card::Connect`
  call).  Dual-link (`DL_HD`, `DL_3G`, `DL_3GB`) and `SL_24G`
  return an empty list — the open path translates that to
  `Error::NotSupported`.  12G single-link gracefully falls back
  to 2SI on cards lacking the dedicated 12G crosspoint
  (`CanDo12gRouting() == false`).
- `Ntv2Routing::needsTsi` / `needsSquares` helpers gate
  `CNTV2Card::SetTsiFrameEnable(true, ...)` and
  `Set4kSquaresEnable(true, ...)` so the framestore-grouping bit
  is programmed before any crosspoints are wired (the AJA helpers
  compute crosspoint ids assuming the grouping mode is already
  set).
- `Ntv2MediaIO::routeSdiInput` / `routeSdiOutput` rewritten to
  consume the resolved `SdiLinkStandard` from the caller's
  `SdiSignalConfig` (no more hardcoded single-link table).  The
  open paths pass `sdiSignal.standard()`, the channel start, the
  reserved SDI port start, and the framebuffer-RGB flag.
- `unittest-promeki` link line gained `${PROMEKI_NTV2_TARGET}`
  under `PROMEKI_ENABLE_NTV2` so the routing tests can anchor
  their expected crosspoint ids against AJA's own
  `GetFrameStoreInputXptFromChannel` / `GetTSIMuxInputXptFromChannel`
  / `GetInputSourceOutputXpt` / `GetSDIOutputInputXpt` helpers —
  the test verifies the routing module's idea of "which ids go
  together" matches the SDK's.

**Tests landed:**

- `tests/unit/ntv2routing.cpp` (12 cases, 35 assertions):
  single-link FB ← SDIIn pairing at default and offset channel
  starts; Auto-standard default to single-link; SL_12G follows
  the single-link path when `can12g=true` and the 2SI fallback
  when `can12g=false`; QL_3G_SQD yields 4 parallel pairs;
  QL_3G_2SI yields 8 TSI-mux + FB pairs and verifies the right
  mux/LinkA/B/SDI mapping per quadrant; sink-side single-link
  and QL_3G_2SI; dual-link and SL_24G return empty;
  `needsTsi` / `needsSquares` reflect the link standard.

**Follow-on landed same day (2026-05-17):**

- High-bit-depth pixel formats wired into the mapping —
  `YUV10_422_v210_Rec709` (V210), `RGB10_DPX_sRGB` /
  `RGB10_DPX_LE_sRGB` (DPX-A / DPX-LE), and `RGB16_LE_sRGB`
  (`NTV2_FBF_48BIT_RGB`) all round-trip through
  `Ntv2Format::toNtv2PixelFormat` / `fromNtv2PixelFormat`.  V210
  was always a first-class `PixelFormat::ID` in the registry; the
  Phase 5 plan called for a "promotion" that turned out to be a
  no-op — the lookup was already there, just unused.
- **On-board CSC routing** — `Ntv2Routing::Config` now carries
  `signalRgb` + `allowOnBoardCsc` toggles.  When the framestore
  and wire colour families differ and CSC is allowed, the helper
  inserts a per-channel (per-quadrant for QL) CSC widget in the
  crosspoint list: source path goes `SDIIn → CSC → FB`, sink path
  goes `FB → CSC → SDIOut`.  `Ntv2MediaIO`'s open paths
  populate the Config from the framestore's `colorModel().type()`
  and `Ntv2Capabilities::cscCount()`.
- **`MediaConfig::Ntv2DisableOnBoardCsc`** — bool, default false.
  When true, both the routing helper and `proposeInput` keep
  on-board CSCs out of the path; the negotiator falls back to
  the wire colour family so the planner inserts a software CSC
  (or asks upstream for native-wire-family producers).
- `Ntv2Capabilities` gained `cscCount()` so the negotiator /
  routing path knows how many CSC widgets the card has.
  Reported by `toString()` and surfaced in the test factory.

**Still deferred (smaller scope than originally planned):**

- Dual-link routing tables — none of the current cards under
  development use them; the helper returns an empty list and the
  open path surfaces `Error::NotSupported` cleanly so a future
  ask just adds the missing path.
- Dual-link RGB / HDMI RGB wires — `signalRgb` is wired through
  the Config struct but the open paths hard-code `false`.  Flip
  when HDMI sources / dual-link RGB SDI signal types land.
- `NTV2_FBF_10BIT_YCBCRA` (alpha-capable 10-bit YCbCr) and the
  RGB10-packed formats — no first-class `PixelFormat::ID` yet;
  add when a card actually needs them.

**Tests still to land (need hardware):**

- 4K60 capture via Quad-Link 2SI on a card with 4 SDI inputs;
  verify per-frame raster + correct field ordering.
- 4K30 capture via 12G single-link on a Kona 5 / Corvid 44
  (cards that expose `CanDo12gRouting`).
- 4K playout via QL_3G_SQD, verifying the four output ports all
  carry the expected quadrant data.

### Phase 6 — Genlock, external pacing, drift-aware clock — **DONE 2026-05-17**

**What shipped:**

- **`Error::SignalLoss`** added — a discrete code distinct from
  `NotReady` (which means "never came up") so callers can tell
  "input cable yanked mid-stream" apart from "no signal at open."
- **Capture-side signal-loss detection** — the capture worker
  counts VBI poll waits and consults
  `CNTV2Card::GetInputVideoFormat` every
  `MediaConfig::Ntv2SignalPollIntervalVbi` VBIs (default 15 ≈
  1 Hz at 60 fps).  On a present→absent transition it emits
  `MediaIO::errorOccurredSignal(Error::SignalLoss)` and logs a
  warning naming the affected port; on the inverse transition it
  logs an info line and increments the re-acquire counter.  New
  stats IDs `Ntv2SignalLoss` + `Ntv2SignalReacquired` expose the
  per-channel counters.  (The `InspectorDiscontinuity` event that
  the original devplan called out lives in the inspector test
  pipeline rather than at the MediaIO layer, so it doesn't apply
  here — `errorOccurredSignal` is the right surface for hardware
  signal-loss events.)
- **Genlock plumbing** — `VideoReferenceConfig` was already wired
  through `MediaConfig::VideoReference` from Phase 1; the open
  paths still call `_device->setReference(refCfg, this)` and the
  device-wide warn-on-conflict path from Phase 4 handles the
  multi-channel arbitration.  No new code was needed beyond
  documenting the existing behaviour.
- **Sink external pacing** — `executeCmd(MediaIOCommandSetClock)`
  no longer returns `NotSupported` in sink mode.  A non-null
  `cmd.clock` binds the gate via `PacingGate::setClock`, refreshes
  the period from `_frameRate`, and applies the configured skip /
  reanchor thresholds (`Ntv2PaceSkipThresholdMs` /
  `Ntv2PaceReanchorThresholdMs`).  The playout worker checks
  `_paceClockExternal` on every loop iteration and waits on the
  gate before each `AutoCirculateTransfer`; `Skip` verdicts drop
  the frame (counted via `StatsFramesDroppedSink`), `Reanchor`
  verdicts log a warning and proceed.  A null `cmd.clock` unbinds
  cleanly, restoring Phase-2 card-paced behaviour.  Source mode
  still returns `NotSupported` (the capture cadence is driven by
  the wire and can't be meaningfully replaced).  New stats IDs
  `Ntv2PacingTicksOnTime` / `…Late` / `…Skipped` /
  `Ntv2PacingReanchors` mirror the gate counters into MediaIOStats.
- **Drift-aware `Ntv2DeviceClock::rateRatio()`** — `raw()` now
  captures (counter ns, wall ns) pairs on every read.  After a
  5-second baseline window (`kRateBaselineMinWindowNs`) the
  estimator publishes a slowly-tracking LPF average of
  `counterDelta / wallDelta` so downstream consumers (audio
  resamplers, frame-syncs) can pull the true device-vs-host rate
  without measuring it themselves.  Clamped to [0.95, 1.05] to
  defend against a first-read transient.  Test seam
  `setWallTimeSourceForTest` lets unit tests synthesise wall +
  counter advance pairs without sleeping.  VBI-fallback mode
  always returns 1.0 (the "device" and host clocks are the same
  in that configuration).
- **New `MediaConfig` keys** — `Ntv2SignalPollIntervalVbi`,
  `Ntv2PaceSkipThresholdMs`, `Ntv2PaceReanchorThresholdMs`,
  `Ntv2VpidEnable`, `Ntv2VpidTransferOverride`,
  `Ntv2VpidColorimetryOverride`, `Ntv2VpidRangeOverride`, all
  wired into `Ntv2Factory::configSpecs` for tooling visibility.
- **HDR / colorimetry VPID signaling (Phase 6.4)** — landed in
  the same changeset as the rest of Phase 6.  Surface:
  - `ntv2vpid.{h,cpp}` — pure namespace-level helpers translating
    promeki's `TransferCharacteristics` / `ColorPrimaries` /
    `MatrixCoefficients` / `VideoRange` enums to and from AJA's
    `NTV2VPIDTransferCharacteristics` /
    `NTV2VPIDColorimetry` / `NTV2VPIDLuminance` /
    `NTV2VPIDRGBRange` (returned as `int` so non-NTV2 TUs don't
    drag AJA headers, same pattern as `ntv2format`).  PQ / HLG /
    Unspecified round-trip cleanly; every SDR variant collapses
    to `NTV2_VPID_TC_SDR_TV` per SMPTE ST 352; BT.709 / BT.2020
    round-trip through Rec709 / UHDTV; SMPTE2085 triggers the
    ICtCp luminance flag.
  - **Sink side (`applySinkVpid`)** — `openSink` resolves the
    four byte-4 fields with three-tier precedence: explicit
    `MediaConfig::Ntv2Vpid*Override` keys win, then per-frame
    H.273 derivation from the framestore `PixelFormat`'s
    `ColorModel::toH273()`, then the card's auto-derivation
    (override flag stays disabled).  Writes via
    `CNTV2Card::SetSDIOutVPIDTransferCharacteristics` /
    `Colorimetry` / `Luminance` / `RGBRange`.  Best-effort —
    individual setter failures warn but don't fail the open
    (cards lacking specific VPID overrides still benefit from
    the partial set).  Honours an `Ntv2VpidEnable=false` opt-out
    for legacy SDR pipelines that prefer the card's auto-derived
    VPID.
  - **Source side (`pollSourceVpid`)** — `openSource` reads the
    decoded byte-4 fields via `CNTV2Card::GetVPIDTransferCharacteristics`
    / `Colorimetry` / `RGBRange` at open time so the very first
    captured Frame already carries the wire's colour claim; the
    periodic signal-loss poll re-reads on its same cadence so
    mid-stream HDR transitions (producer flipping from SDR to PQ
    on the same physical port) reflect in subsequent captured
    frames.  When the wire claims PQ or HLG, the framestore's
    @ref PixelFormat is upgraded to the matching BT.2100 HDR
    variant (e.g. `YUV10_422_UYVY_LE_Rec709` →
    `YUV10_422_UYVY_LE_Rec2020_PQ`) so the colour-description
    claim travels with the image through every downstream
    consumer via the @ref ColorModel HDR entries — no per-frame
    metadata stamping required.  See the **Path C catalog
    extension** note below for the underlying core change.
  - **HDR static metadata (mastering display / MaxCLL / MaxFALL)**
    rides ANC-side through the existing
    `AncFormat::HdrStatic2086` codec registered in Phase 3 — no
    additional plumbing was needed; producers that put an
    `HdrStaticMetadata` packet on the Frame's ANC list get it
    inserted into the SDI VANC region for free.
  - **Path C catalog extension (core)** — `ColorModel` gained
    five HDR-aware entries (`Rec2020_PQ`, `Rec2020_HLG`,
    `DCI_P3_PQ`, `YCbCr_Rec2020_PQ`, `YCbCr_Rec2020_HLG`) with
    spec-correct SMPTE ST 2084 PQ and ITU-R BT.2100 HLG OETF /
    EOTF implementations and matching `toH273` codepoints
    (transfer = 16 / 18, primaries = 9 / 12, matrix = 0 / 9 as
    appropriate).  `PixelFormat` gained 17 HDR variants spanning
    the layouts that practically carry HDR — 10/12-bit YCbCr
    4:2:2 UYVY (SDI HDR), 10/12-bit YCbCr 4:2:0 planar and
    semi-planar (codec / P010 HDR outputs), 16-bit YCbCr 4:2:2
    semi-planar (NDI P216), 10-bit packed RGB10A2, 16-bit RGB,
    half-float linear BT.2020, and 16-bit DCI-P3 PQ.  This
    pivots the project's HDR signalling onto the @ref PixelFormat
    identity itself rather than parallel @ref Metadata keys
    (which remain only as a manual override path for the rare
    case of buffer-vs-claim mismatch).

**Tests landed:**

- `tests/unit/ntv2clock.cpp` — three new cases:
  rateRatio defaults to 1.0 before the baseline window
  stabilises; rateRatio tracks a synthesised 1.001 (1000 ppm)
  drift to within ±0.0005 after the LPF converges; rateRatio
  stays at exactly 1.0 in VBI-fallback mode.
- `tests/unit/ntv2mediaio.cpp` — two new cases: the factory
  configSpecs map exposes the three new Phase-6 keys with the
  documented defaults; `Error::SignalLoss` is a distinct
  registered error code with non-empty name + description.
- `tests/unit/ntv2vpid.cpp` — eight new cases covering the
  Ntv2Vpid mapping helpers: PQ / HLG / Unspecified round-trips
  exactly; every SDR transfer collapses to `NTV2_VPID_TC_SDR_TV`
  and the reverse picks BT709 as the canonical SDR claim;
  `Auto` resolves to Unspecified (the open path provides
  context); BT.709 / BT.2020 colorimetry round-trip; every other
  primary maps to `NTV2_VPID_Color_Unknown`; SMPTE2085 toggles
  the ICtCp luminance flag and every other matrix maps to
  YCbCr; Full / Limited / Unknown range mappings; out-of-range
  integers clamp to safe defaults without asserting.
- `tests/unit/colormodel.cpp` — three new cases covering the
  HDR catalog extension: `toH273` returns the correct H.273
  codepoints (PQ = 16, HLG = 18) for each HDR ColorModel; PQ
  and HLG OETF / EOTF round-trip mid-scale values to within
  1e-5; HDR YCbCr models inherit the BT.2020 NCL matrix from
  the SDR `YCbCr_Rec2020` variant unchanged.
- `tests/unit/pixelformat.cpp` — two new cases: every HDR
  PixelFormat resolves to the matching HDR `ColorModel`; the
  PixelFormat → ColorModel → toH273 chain yields the correct
  transfer codepoint on its own (no metadata stamping needed).

**Tests still to land (need hardware):**

- Loss + recovery: pull the SDI cable mid-capture; verify
  `errorOccurredSignal(Error::SignalLoss)` fires within
  `(Ntv2SignalPollIntervalVbi / frame_rate)` seconds and
  re-acquire fires when the cable is re-seated.
- External pacing: drive two NTV2 sinks against a shared
  `SyntheticClock` (or a third card's `Ntv2DeviceClock`) and
  verify the per-channel frame stamps stay coherent over a
  60-second run within the pacing gate's skip threshold.
- Drift sanity: capture for an hour and verify the published
  `rateRatio()` stays within 100 ppm of 1.0 on a healthy
  genlock — the host crystal vs FPGA crystal drift should be
  small enough that the LPF lands near the absolute floor.
- VPID loopback: play out an HDR signal with
  `Ntv2VpidTransferOverride=SMPTE2084` /
  `Ntv2VpidColorimetryOverride=BT2020`, capture on a second
  channel over an SDI cable, verify the captured Frame's
  `Metadata::VideoTransferCharacteristics` reports `SMPTE2084`
  and `VideoColorPrimaries` reports `BT2020`.  Also flip the
  override mid-stream and verify the next captured Frame after
  the signal-poll cadence picks up the new claim.

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

- **Mock layer for unit tests.** *(Resolved 2026-05-16.)*  Phase 1
  went with option (b)+(c): no `Ntv2DeviceImpl` indirection in
  production; the hardware-free units (format mappings, clock wrap)
  are tested directly, and `Ntv2DeviceClock` exposes a
  `createForTest` static factory plus a `setCounterSourceForTest`
  injection seam (`bool (*fn)(uint32_t *, void *)`) so the wrap-
  extension arithmetic can be exercised without an AJA card.
  Hardware-required tests live under `tests/func/` and are SKIPPED
  when no card is present.
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
- **Detection of card reboot / driver restart mid-open.**
  *(Resolved 2026-05-19.)*  Both worker loops (capture + playout)
  consult `CNTV2Card::IsOpen()` every `Ntv2SignalPollIntervalVbi`
  iterations.  On a present→absent transition the worker increments
  the new `StatsDeviceLost` counter, emits
  `errorOccurredSignal(Error::DeviceError)` exactly once, and
  exits the loop.  The MediaIO does **not** attempt automatic
  re-acquire — driver restarts re-enumerate device indices, so a
  reopen needs the caller to re-resolve the URL and rebuild the
  pipeline.  Fail-close is the simpler contract.
- **Which audio-system counter drives the device clock when both
  source and sink channels are open on the same card?**
  *(Resolved 2026-05-17.)* The clock reads `kRegAud1Counter` via
  `CNTV2Card::GetRawAudioTimer` — a single FPGA-resident
  free-running counter that is independent of any audio system's
  In/Out address.  There is no In/Out arbitration to make.  The
  same register is what AJA's driver pre-extends into
  `FRAME_STAMP::acAudioClockTimeStamp`, so the device clock's
  `now()` and per-frame PTS read by `mediaTimeStampFromSamples`
  share a time base exactly.
- **Sample-counter availability on T-Tap / playback-only cards.**
  *(Resolved 2026-05-17.)* `kRegAud1Counter` ticks from FPGA load
  at power-up and does not depend on capture or playout being
  active, so playback-only cards (T-Tap) read it just fine.
  `Ntv2Capabilities::hasAudioCounter` now gates only on whether
  the device has an audio subsystem at all (`_audioSystems > 0`),
  not on capture capability.  VBI fallback is therefore dead code
  on every shipping NTV2 card; the gate stays as a safety net for
  hypothetical future hardware without the subsystem.
- **`Ntv2DeviceClock` epoch on first open vs. after device close +
  reopen.** *(Resolved 2026-05-19.)*  Documented in
  `include/promeki/ntv2clock.h` under "Per-acquire epoch — stamps
  do not round-trip across close/reopen": the 64-bit shadow resets
  to zero on every `Ntv2DeviceRegistry::acquire`, so stamps are
  coherent across every channel on the same card for the lifetime
  of the acquisition but a process that closes + reopens the
  device starts fresh.  Callers that need to persist stamps across
  an acquire boundary snapshot the (host-wall, device-stamp) pair
  before close and re-anchor on reopen.  Picked monotonicity over
  cross-acquire round-trip — the alternative (anchor shadow to
  host wall time on first read) breaks the monotonic-clamp
  contract on any wall-time jump.
- **`HdmiInfoFrame` collision check.** *(Resolved 2026-05-19.)*
  Bidirectional Doxygen cross-references added to both
  `hdmiinfoframe.h` (typed ANC packet helper) and
  `hdmisignalconfig.h` (carrier-level descriptor), each with a
  `@note` explaining the names share a prefix but the types are
  unrelated, plus a `@see` linking to the sibling.
- **`SdiLinkStandard::Auto` semantics on sinks.**
  *(Resolved 2026-05-19.)*  Inferred at open time via the new
  library helper `inferSdiLinkStandard(VideoFormat, SdiWireFormat,
  cableCount)` (in `include/promeki/sdiwireinference.h`).  The
  sink open path classifies the framebuffer `PixelFormat` to an
  `SdiWireFormat` via `sdiWireFormatFor(pf)` — except when on-board
  CSC is enabled and the framebuffer is RGB, where the wire
  payload is overridden to `SdiWireFormat::YCbCr_422_10` (the
  family the CSC outputs).  The inference itself is generic
  SDI math (pixels-per-sec × bits-per-pixel × 18% overhead vs
  per-standard `sdiNominalDataRateGbps`); future DeckLink /
  ST 2110 / ST 2022-6 backends reuse it unchanged.  Quad-link
  disambiguation prefers `QL_3G_2SI` over `QL_3G_SQD`.  An
  inference miss surfaces `Error::InvalidArgument` and logs the
  raster / rate / wire-format / cable count for the operator.
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
