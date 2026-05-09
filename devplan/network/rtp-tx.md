# RTP TX Architecture Refactor

**Status:** Phases 1–5 all landed 2026-05-08:

  1. **Phase 1** — foundation (Queue bounding,
     `Frame::captureTime`, NTP arithmetic, RtpSession
     `setRtpAnchor` / `noteRtpEmission` / SR derivation).
  2. **Phase 2** — per-stream packetizer + TX threads,
     strand-as-router on `executeCmd(Write)`, sentinel-free
     shutdown via `Queue::cancelWaiters`.
  3. **Phase 3** — four follow-on chunks: (a)
     `sendPackets(RtpPacketBatch&)` collapse on `RtpSession`;
     (b) per-kind `Stream` subclass split (`VideoStream` /
     `DataStream` carrying codec-specific fields); (c) typed-
     list conversion (`_video` → `List<VideoStream> _videos`,
     `_data` → `List<DataStream> _datas`); (d) `WriterStream`
     / `ReaderStream` split — Stream now carries identity-only
     fields; per-kind writer types inherit from `WriterStream`
     (TX threads, TX stats, TX histograms); per-kind reader
     types inherit from `ReaderStream` (reassembly state, RX
     stats, RX histograms, reader-side `readerImageDesc` /
     `readerAudioDesc` / `fifo`).
  4. **Phase 4** — test catch-up; most items shipped with
     their introducing phase (see checkbox state below).
     `AudioPacketizerThread` / `AudioTxThread` direct unit
     cases remain deferred (the classes are nested inside
     `RtpMediaIO`); the rtp.* functional suite catches the
     same ~5/60 startup-loss the receive-side sync gate
     produces today and stays deferred until RX-side
     hardening lands.
  5. **Phase 5** — reader-side SR consumption: `RtcpPacket`
     parsers, `RtpStreamClock`, `RtpSession::ReceivedSr` +
     RTCP demux in `ReceiveThread`, `ReaderAggregator`
     wallclock-aligned drain, RX-side `Frame::captureTime`.
     25 new unit tests added across `rtcppacket.cpp`,
     `rtpsession.cpp`, and the new
     `rtpstreamclock.cpp`.

This devplan covers all five phases as one coherent refactor
across `RtpSession` and `RtpMediaIO`.
**Library:** `promeki` (network + proav)
**Standards:** `CODING_STANDARDS.md`; every new class requires
doctest unit tests; existing tests updated as APIs change.

Restructure the `RtpMediaIO` transmit path from "strand → per-stream
work-queue dispatch (mixed packetization + send)" to a clean two-stage
per-stream pipeline: **packetizer thread → TX thread**. The strand
becomes a pure router that pushes the active `Frame` (CoW handle —
refcount bump only) onto every active stream's bounded `PayloadQueue`
and returns immediately. Each stream's wire pacing strategy (kernel
`fq`, user-space sleep, future SCM_TXTIME) lives in the TX thread
alone. Audio TX no longer drains an `AudioBuffer` FIFO inside the TX
thread — the audio packetizer thread does that and hands AES67-sized
PCM payload `Buffer` chunks to the TX thread, which assembles them
into `RtpPacket`s at the cadence-driven emission instant (and
substitutes silence chunks if none are ready).

The data model is uniformly **multi-stream per kind**: `_videos`,
`_audios`, `_datas` are all `List<Stream>`-shaped, mirroring the
existing `_audios` list. Today's config layer populates one entry
per kind, but the routing, SDP, RTCP scheduler, and per-stream
threads scale to N video / N audio / N data streams without further
restructuring.

No cross-stream gating. Each stream emits independently as soon as
its packetizer produces packets. Receiver-side sync relies on RTCP
SR carrying capture-wallclock-anchored `(NTP, RTP_TS)` pairs — see
the **SR anchor** section below.

## Why

Current flaws:

- The strand blocks on video TX completion (per-frame `join`), so
  video packetization and wire pacing time both bleed into the
  upstream pipeline's frame cadence.
- Audio TX runs in its own thread but the packetization (FIFO drain
  + AES67 sizing + format conversion) happens in that thread. That
  conflates two concerns and complicates the pacing logic.
- A single `AudioBuffer` FIFO per stream serves as both the
  strand-to-TX handoff and the AES67 packet boundary buffer. That
  works but wires the strand directly to the AES67 packetiser's
  internal state.
- There is no clean place to swap pacing strategies per stream.
  `RtpPacingMode::TxTime` (deferred) needs per-packet `SCM_TXTIME`
  cmsg deadlines, which doesn't fit naturally alongside the audio
  FIFO logic.
- The audio TX path has no continuous-cadence guarantee: if the
  upstream source stalls, the wire goes silent for the duration of
  the stall, and the next packet's RTP timestamp lands at "old TS +
  one packet" rather than "old TS + N packets matching wallclock
  elapsed". AES67 / ST 2110-30 receivers see this as packet loss or
  declare a discontinuity. The new design must emit silence packets
  on the configured cadence whenever the packetizer can't deliver a
  list in time.
- SR `(NTP, RTP_TS)` pairs reflect strand / wire instants, not
  source-capture instants, so any pipeline delay between capture and
  the strand reaching `executeCmd(Write)` shows up as receiver-side
  A-V skew. Frames need to carry a first-class capture timestamp
  that the RTP TX path consults when seeding the SR anchor.
- `_video` and `_data` are single `Stream` slots while `_audios` is
  already a `List<AudioStream>`. That asymmetry forces a second
  refactor when multi-video / multi-data sessions land. Convert all
  three to lists in this pass.

The two-stage design separates **what to send** (packetization) from
**when to send it** (pacing), one queue between them, and one queue
between the strand and the packetizer. The strand becomes a router.
Every queue is bounded — backpressure naturally throttles the upstream
pipeline when any stream falls behind.

## Architecture

```
                 ┌──────────────────────────────┐
                 │  Strand (executeCmd(Write))  │  one Frame in
                 └──────────────────────────────┘
                         │   │   │
                  push   │   │   │   push
                  Frame  │   │   │   Frame
                  (CoW)  │   │   │   (CoW)
                  │      │   │   │
                  ▼      ▼   ▼   ▼
       ┌──────────────────────────────────────────┐
       │  Per-stream PayloadQueue<Frame>           │  one per stream
       │   bounded (depth ≈ 2)                     │  (M video, N audio,
       │                                           │   P data)
       └──────────────────────────────────────────┘
                  │      │       │
                  ▼      ▼       ▼
       ┌──────────────────────────────────────────┐
       │  Per-stream Packetizer thread             │  PAYLOAD BYTES ONLY
       │   audio: AudioBuffer FIFO drain →         │  (no RTP header set)
       │          AES67 Buffer chunks              │
       │   video: payload->pack() → RtpPacket      │
       │          list w/ payload-only contents    │
       │   data : JSON serialize → RtpPacket list  │
       └──────────────────────────────────────────┘
                  │              │
            audio │              │ video / data
            Buffer│              │ RtpPacketBatch
                  ▼              ▼
       ┌──────────────────────────────────────────┐
       │  Per-stream PacketQueue (bounded)         │
       │   audio: Queue<Buffer>                    │
       │   video / data: Queue<RtpPacketBatch>     │
       └──────────────────────────────────────────┘
                  │      │       │
                  ▼      ▼       ▼
       ┌──────────────────────────────────────────┐
       │  Per-stream TX thread                     │  FILLS FULL RTP HEADER
       │   audio: Cadence ticks; silence-fill if   │  (version/seq/SSRC/PT/
       │          PacketQueue empty                │   marker/RTP-TS)
       │   video: ts = cumulativeTicks(rate, idx); │
       │          KernelFq / Userspace / TxTime /  │
       │          None                             │
       │   data : ts = cumulativeTicks; burst send │
       └──────────────────────────────────────────┘
                  │      │       │
                  ▼      ▼       ▼
                              wire
```

**Per-stream queue depths and types:**

Both queue layers use the existing `promeki::Queue<T>` template,
extended in this refactor with optional bounding (see
`include/promeki/queue.h` work item under "Library additions"
below). `Queue<T>::setMaxSize(n)` (with `0` meaning unbounded —
the current behavior) makes `push` / `emplace` block on a "not
full" condition, and `cancelWaiters()` lets `cancelBlockingWork()`
unblock both producers and consumers without sentinel pushes.

- `PayloadQueue` — `Queue<Frame>`, depth 1 or 2 per stream.
  Strand pushes one Frame per frame interval (refcount bump);
  the packetizer drains. Bounded to 2 so the strand blocks if
  the packetizer falls behind, applying backpressure to the
  upstream pipeline. Larger depth reduces backpressure but masks
  pipeline pacing problems — keep small.
- `PacketQueue` (audio) — `Queue<Buffer>`, one PCM payload per
  AES67 packet. Bound at ≈ 1 second of packets (1000 chunks at
  1 ms cadence = 1 s headroom). Audio TX never blocks on an
  empty `PacketQueue` past one packet interval — see
  "AudioTxThread" silence-fill rules.
- `PacketQueue` (video / data) — `Queue<RtpPacketBatch>`. Bound
  at 2-3 frames worth for video; 8 batches for data.

**Threads per RtpMediaIO instance** (writer mode, M video / N
audio / P data streams active): `2(M+N+P) + 1` — one packetizer
+ one TX per active stream, plus the single RTCP scheduler. The
common case today (M=1, N=1, P=1) is 7 threads. Reader mode adds
a single per-RtpMediaIO RTCP-receive thread (Phase 5) but is
otherwise unchanged (per-session RTP receive thread + strand).

## Frame routing on the strand

`RtpMediaIO::executeCmd(MediaIOCommandWrite&)` becomes:

1. On the first frame, seed the SR anchor on every active session
   from `frame.captureTime()` (see §SR anchor). "First frame" is
   detected by an `Atomic<bool> _anchorSeeded` on `RtpMediaIO`,
   flipped under a `compare_exchange` so a single seeding happens
   even if a future change ever takes the strand off the single-
   threaded model.
2. Run frame-level pacing gate (`paceVideoFrame`) so the strand
   has frame-rate cadence; drop on `Skip` verdict. Why this
   stays on the sink: synthetic sources (TPG, file-replay at
   "as fast as possible") have no upstream clock, and pacing
   modes that hand whole frames to the kernel in a burst
   (`KernelFq`, `None`) deliver no backpressure to the strand —
   the gate is the only thing keeping the source from
   firehosing. For paced-by-TX modes (`Userspace`) the gate is
   a near-no-op because the bounded `PayloadQueue` already
   blocks the strand at wire rate. For externally-clocked
   sources the gate's clock binding makes it a no-op too.
3. Push the whole `Frame` (CoW handle, refcount bump only) onto
   every active stream's `PayloadQueue` — `_videos[i]`,
   `_audios[j]`, `_datas[k]`. Each packetizer thread pulls its
   own essence (`frame.videoPayloads()`, `frame.audioPayloads()`,
   `frame.metadata()`) and ignores the rest. The Frame carries
   `captureTime`, frame number, metadata, and codec parameter
   sets along for the ride — no separate sideband tuples.
4. Update `_frameCount` / `_framesSent`.
5. Return.

`PayloadQueue` is `Queue<Frame>` per stream. Bounded (depth 2 by
default) — strand blocks if any queue is full, backpressuring
the upstream pipeline. The strand never blocks on actual wire
I/O and never `join`s on TX results.

## Per-stream packetizer threads

**Universal rule:** packetizers fill RTP **payload bytes only** —
no RTP header field is set by a packetizer. The TX thread fills
the full header (version, seq, SSRC, payload type, marker, RTP
timestamp) at emission time. Deferring the timestamp to the very
end is what makes the audio silence-fill rule possible without
two threads racing on a per-packet counter.

**Class shape:** three concrete classes
(`AudioPacketizerThread` / `VideoPacketizerThread` /
`DataPacketizerThread`) implementing a thin
`RtpPacketizerThreadBase` interface — they differ enough in
internal state (audio holds an `AudioBuffer`, video holds the
sprop cache + paramSets sideband, data holds a JSON string
buffer) that a single parameterised template would just push
the difference into specializations. TX threads follow the
same pattern (see §"Per-stream TX threads").

Each stream has its own packetizer class:

### `AudioPacketizerThread`

- Holds the `AudioBuffer` FIFO.
- Pop loop: pull next `Frame` off the audio `PayloadQueue`, locate
  this stream's `PcmAudioPayload` via `frame.audioPayloads()`.
- Push samples into the FIFO with format conversion (S16/S24/F32 etc.
  → PCMI_S16BE wire format).
- Drain whole AES67-aligned chunks of `packetBytes` each from the
  FIFO into `Buffer` payload-only slices. No RTP header, no
  RTP-TS — just the payload bytes for one AES67 packet.
- Push each `Buffer` to the audio `PacketQueue` (which is
  `Queue<Buffer>` for audio — one chunk per AES67 packet).
- Leftover-tail samples that don't fill a chunk survive in the
  FIFO into the next pop cycle.

### `VideoPacketizerThread`

- Pop loop: pull next `Frame` off the video `PayloadQueue`, locate
  this stream's `VideoPayload` via `frame.videoPayloads()`. Pull
  `Metadata::CodecParameterSets` off `frame.metadata()`.
- For H.264 / HEVC: feed `paramSets` and the bitstream through
  `injectParameterSets` (cache update + SDP `sprop-*` refresh +
  optional self-healing prepend on missing parameter sets). Same
  logic that lives in `sendVideo` today, just running on this
  thread.
- For uncompressed: optional `UncompressedVideoPayload::convert`
  to the wire pixel format when the input doesn't match RFC 4175's
  expected layout.
- Run `payload->pack(bytes, size)` to build the `RtpPacket::List`
  with payload-only contents (header bytes left zero — TX thread
  fills them).
- Build an `RtpPacketBatch` carrying:
  - the `RtpPacket::List`
  - `frameIndex` and `clockRate` so the TX thread can compute
    `cumulativeTicks(clockRate, frameIndex)` once and stamp it on
    every packet
  - `markerOnLast = true`
  - `rateCapBps` recomputed for VBR compressed video from the
    actual packed byte count, `0` for CBR / uncompressed
- Push the batch to the video `PacketQueue` (`Queue<RtpPacketBatch>`).

### `DataPacketizerThread`

- Pop loop: pull next `Frame` off the data `PayloadQueue`. Read
  `frame.metadata()`.
- Serialize via `Metadata::toJson().toString(0)`.
- Pack into an `RtpPacket::List` with payload-only contents.
- Build an `RtpPacketBatch` (`frameIndex`, `clockRate`,
  `markerOnLast = true`, `rateCapBps = 0`).
- Push the batch to the data `PacketQueue`.

(Data is stable on the strand today, but a packetizer thread is
included for symmetry — JSON serialization of large metadata blobs
shouldn't run on the strand.)

## Per-stream TX threads

**Universal rule:** TX threads fill the full RTP header (version,
sequence number, SSRC, payload type, marker bit, RTP timestamp)
at emission time. Pacing strategy and silence-fill are local to
the TX thread.

`RtpSession::sendPackets` accepts an `RtpPacketBatch` (after the
Phase 3 cleanup) and stamps version/seq/SSRC/PT itself; the TX
thread is responsible for setting marker and RTP-TS on each
packet of the batch before handoff.

Each stream has its own TX thread:

### `VideoTxThread`

- Pop loop: dequeue an `RtpPacketBatch` from the video
  `PacketQueue`.
- Compute the frame's RTP timestamp:
  `ts = _frameRate.cumulativeTicks(batch.clockRate, batch.frameIndex)`.
- Stamp `ts` onto every packet in the batch; set `marker=1` on
  the last packet iff `batch.markerOnLast`.
- Apply per-frame rate cap if changed (`session->setPacingRate(...)`)
  for VBR compressed video.
- Dispatch via the configured `RtpPacingMode`:
  - `KernelFq` → `session->sendPackets(batch)` (kernel `fq` qdisc
    paces).
  - `Userspace` → `Cadence`-paced inner loop, one packet per
    deadline tick (`session->sendPacket(...)` in the loop, or a
    batched `sendPackets` overload that takes per-packet
    deadlines once SCM_TXTIME wiring lands).
  - `TxTime` → per-packet `Datagram::txTimeNs` deadlines (path
    stubbed now, implemented later).
  - `None` → burst (single `sendPackets` call).
- Call `session->noteRtpEmission(ts)` for RTCP SR.
- Update video stream's `packetsSent` / `bytesSent` /
  `senderOctets` (atomic counters — see Phase 3).

### `AudioTxThread`

- Owns the per-packet RTP-TS counter for this stream — RTP-TS
  is computed at emission time, not in the packetizer. Cadence
  and counter advance together: each emitted packet (real or
  silence) advances the counter by `packetSamples`.
- Pace at the configured AES67 packet time (`packetTimeUs`,
  typically 1 ms) using a `Cadence` helper (new — see "Library
  additions"). `Cadence::next()` returns deadlines anchored to
  the first emission instant; `Cadence::reanchor(now)` is used
  on long stalls (>N × packetTime, configurable) to avoid
  burst-catch-up on resume.
- Each tick:
  1. `tryPop` the audio `PacketQueue` (which is `Queue<Buffer>` —
     one PCM payload chunk per AES67 packet).
  2. If a chunk is ready, build an `RtpPacket` over it; if not,
     build an `RtpPacket` over a `PcmSilenceFiller`-provided
     silence buffer and bump `silencePacketsEmitted` /
     `silenceSamplesEmitted`.
  3. Stamp RTP-TS = current cursor on the packet.
  4. Send via `session->sendPackets` (which fills version /
     seq / SSRC / PT and sets marker=0 — PCM has no talkspurt
     model).
  5. Cursor += `packetSamples`. Call
     `session->noteRtpEmission(stampedRtpTs)`.
  6. Sleep until the next deadline.
- Update audio stream's `packetsSent` / `bytesSent` /
  `senderOctets`, plus the new `silencePacketsEmitted` /
  `silenceSamplesEmitted` counters that feed back through
  `MediaIOCommandStats` so the user can see how often the
  source stalled.

Silence packets count as real wire emissions for both
`noteRtpEmission` and `hasEmissionRecord` purposes — see
§"RtpSession changes" — so an audio session that only ever
emits silence still produces SRs.

The wire timeline is *always* contiguous in RTP-TS units at
exactly the configured packet cadence. The packetizer chooses
content (real samples or, by absence, silence); the TX thread
chooses *when* and *how many* and reports both.

### `DataTxThread`

- Pop loop: dequeue an `RtpPacketBatch`, compute
  `ts = cumulativeTicks(batch.clockRate, batch.frameIndex)`,
  stamp it on every packet, set marker on the last per
  `batch.markerOnLast`, send via `session->sendPackets`, no
  pacing required (data is small + low-rate).

## Backpressure and shutdown

The one behavior that's worth restating outside the queue-depth
table:

- **Backpressure path**: source pump → strand → `PayloadQueue`
  push (blocks if full) → packetizer drain → `PacketQueue` push
  (blocks if full) → TX thread drain. A slow wire propagates
  backwards through both queues to throttle the source pump
  *unless* the source is unclocked / synthetic, in which case
  the strand-side `paceVideoFrame` gate provides the cadence
  (see §"Frame routing on the strand" step 2).
- **Shutdown path**: no sentinels. `cancelBlockingWork()` calls
  `Queue::cancelWaiters()` on every PayloadQueue and PacketQueue,
  which wakes both producer and consumer waits with
  `Error::Cancelled`. Threads observe the cancel and exit. See
  §"Cleanup ordering" under Phase 2 for the join order.
- **Buffer-sharing safety**: `RtpPacket::createList` allocates one
  Buffer for N packets sharing distinct slices. The packetizer
  writes the payload bytes inside each slice; the TX thread later
  writes the RTP header bytes inside each slice. They write
  disjoint regions of the same Buffer. The queue handoff
  (mutex-protected) provides the happens-before relationship —
  no atomic ops on the buffer bytes themselves are needed.

## SR anchor (capture-wallclock based)

The SR's `(NTP, RTP_TS)` pair must reflect **source-capture
wallclock**, not wire-emission wallclock, so receivers can compute
capture-wallclock for any incoming RTP packet and align across
streams.

### Frame::captureTime — first-class timestamp on the Frame

Today `Frame` carries no frame-level wallclock; per-essence
`MediaTimeStamp::pts()` lives on each payload. This refactor adds:

- `Frame::captureTime() / setCaptureTime(MediaTimeStamp)` — a
  first-class `MediaTimeStamp` on the Frame, parallel to the
  existing per-payload pts. This is the moment the source captured
  this Frame as a whole (the analog of "shutter open" for a
  camera, "first sample of buffer" for an audio capture).
- `MediaIO` default-stamps any Frame that arrives at a write
  boundary without a `captureTime` already set, mirroring the
  pattern used today for per-payload pts (`mediatimestamp.h`
  already documents that MediaIO synthesizes a Synthetic-domain
  pts when the backend doesn't supply one). The default is
  `(TimeStamp::now(), ClockDomain::SystemMonotonic)`. Backends
  that own a hardware capture clock (V4L2, NDI, ST 2110 RX, PTP-
  locked sources) overwrite this with the hardware-derived value
  before pushing the Frame downstream.
- The pipeline preserves `captureTime` across CoW Frame copies
  (it's a single `MediaTimeStamp` member on `FrameData` —
  trivial). Backends that re-derive the Frame (e.g. CSC) inherit
  the upstream `captureTime` rather than restamping.

### RtpSession changes

- `RtpSession` re-gains
  `setRtpAnchor(NtpTime captureNtp, uint32_t rtpTs)` —
  establishes the (capture-NTP, RTP-TS) reference instant for
  this stream. Both anchor fields are stored unconditionally;
  there is no "anchor not set" path because every active session
  is anchored at openStream time (default: `NtpTime::now()` /
  `0`, refined by the first arriving Frame).
- `RtpSession::noteRtpEmission(uint32_t rtpTs)` records **only**
  the most-recent RTP timestamp that actually went on the wire.
  The current `_lastEmissionNtp = NtpTime::now()` capture is
  **deleted** — wallclock NTP at emission time is not what the SR
  should report and observing it disagrees with the capture-anchor
  design.
- Silence packets (emitted by `AudioTxThread` when its
  `PacketQueue` is empty) call `noteRtpEmission` exactly like
  real-content packets. They are real wire emissions, the SR's
  RTP-TS must reflect them, and `hasEmissionRecord()` must
  return true so the RTCP scheduler emits SRs for an audio
  session that's currently producing only silence.
- `RtpSession::emitRtcpSr` reports
  `(anchor.ntp + Duration::fromSeconds(double(last_rtp_ts -
    anchor.rtp_ts) / clockRate), last_rtp_ts)`. The subtraction
  is modular `uint32_t` so wraparound is handled naturally; the
  receiver uses the same arithmetic. NTP is the capture
  wallclock corresponding to the most-recently emitted RTP_TS —
  derived deterministically from the anchor + the stream's clock
  rate, never measured against the system clock per emission.
- `RtpSession::sendPackets` API is simplified to one overload:
  `Error sendPackets(RtpPacketBatch &batch)`. The session fills
  version / sequence number / SSRC / payload type on each packet;
  the caller (TX thread) has already filled marker and RTP-TS.
  The legacy `sendPackets(packets, ts, marker)`,
  `sendPackets(packets, ts, stride, ...)`, and
  `sendPacketsPaced(...)` overloads are deleted in Phase 3 —
  pacing now lives in the TX thread + `Cadence` and the strided
  variant has no callers after the refactor.
- `RtpSession::setRtpAnchor` and `noteRtpEmission` are
  **writer-only**. Reader mode never invokes them. Reader-side
  SR consumption is a separate API — `lastReceivedSr()` /
  `RtpStreamClock` (see Phase 5 — Reader-side SR consumption) —
  so the writer-side state and the reader-side state stay
  independent on `RtpSession`.

### Per-stream RTCP scheduling

The `RtcpScheduler` currently ticks at a single
`_rtcpIntervalMs` and emits SR + SDES on every active stream
simultaneously. RFC 3550 §6.3 wants per-stream randomized
intervals so a population of senders doesn't end up phase-
locked, and that matters for the multi-stream-per-session goal.
The scheduler computes a per-session next-emit deadline (base
interval × random factor in [0.5, 1.5]) and the wake interval
is `min(deadlines)`. Each emit reseeds that session's deadline.

### RtpMediaIO anchor seeding

- `executeCmd(Write)` consults `frame.captureTime()` on the very
  first frame. If `captureTime.domain()` is valid, convert its
  `TimeStamp` (steady_clock-based) to NTP using a single
  reference instant captured at seed time:
  ```
  TimeStamp steadyNow = TimeStamp::now();
  NtpTime   wallNow   = NtpTime::now();   // system_clock under the hood
  Duration  delta     = frame.captureTime().timeStamp() - steadyNow;
  NtpTime   captureNtp = wallNow + delta + frame.captureTime().offset();
  ```
  This pins the steady→wall mapping at one observed instant per
  open and applies it to the captureTime. Accuracy is bounded by
  the steady/system disagreement at that instant (~µs class on
  a normal Linux box). For PTP-locked sources whose
  `MediaTimeStamp` already lives in a PTP-anchored
  `ClockDomain`, a future overload of the conversion can read
  the domain's epoch directly instead of routing through
  steady/system; the data path is the same.
- If `captureTime` is invalid (default-construction never reached
  the MediaIO default-stamp hook), fall back to `NtpTime::now()`.
- The same `captureNtp` is seeded into every video / audio / data
  session with `rtpTs = 0` (the first-frame, first-sample point
  on each stream's clock). All streams share the wallclock
  anchor — that's what lets receivers align cross-stream sync
  purely from each stream's first SR.
- Per-frame anchor refinement (using each frame's own
  `captureTime` instead of just the first) is a future
  optimization — recorded in "Out of scope" — but the data path
  to do it is in place from day one because `Frame::captureTime`
  is plumbed through.

### SDP advertisement of the clock anchor

`buildSdp` should emit `a=ts-refclk:` and `a=mediaclk:` lines so
receivers can interpret the anchor:

- For SystemMonotonic / SystemWallclock anchors:
  `a=ts-refclk:localmac=<mac>` and `a=mediaclk:direct=0`. Tells
  receivers "the sender's wallclock is its own; align via the SR
  pair, not against a shared grandmaster."
- For PTP-locked anchors (future): `a=ts-refclk:ptp=IEEE1588-
  2008:<grandmaster-id>:<domain>` per ST 2110-10 / RFC 7273.
- Absent both, receivers fall back to "trust the SR pair" —
  what existing receivers do today. So it's safe to start with
  `localmac` / `direct=0` and refine when PTP lands.

For low-buffer receivers (ffplay with `-fflags nobuffer
-flags low_delay`) cross-stream buffering won't happen and audio
will appear before video by whatever the upstream encoder latency
is. That's a known limitation of those flags — well-buffered
receivers do the alignment correctly.

## Files

Tracking the actual layout that landed.  The original "or three
sibling files" naming question for the packetizer thread base
got resolved as **one base class header per role**
(`rtppacketizerthread.h`, `rtptxthread.h`) plus the three
concrete subclasses living as nested classes inside
`rtpmediaio.cpp` — they need access to RtpMediaIO state
(parameter-set cache, pacing mode, SDP refresh) so a sibling
file would have meant either friend declarations or extra
public surface area.

```
[Phase 1, landed]
include/promeki/queue.h                    optional bounding
                                              (setMaxSize / not-full
                                              cv / cancelWaiters)
include/promeki/frame.h                    captureTime() /
                                              setCaptureTime() on the
                                              CoW Data block
src/proav/mediaiosink.cpp                  default-stamp Frame
                                              captureTime in writeFrame
                                              when the inbound Frame
                                              has none
include/promeki/ntptime.h                  + Duration arithmetic
                                              (`operator+(Duration)`,
                                              `operator-(Duration)`)
                                              for the (steady, wall)
                                              MediaTimeStamp → NTP
                                              conversion
include/promeki/rtcppacket.h               existing RTCP packet
src/network/rtcppacket.cpp                    builders — buildSenderReport,
                                              buildSourceDescriptionCname,
                                              compound
include/promeki/rtpsession.h               setRtpAnchor / anchorNtp /
src/network/rtpsession.cpp                    anchorRtpTs;
                                              noteRtpEmission keeps only
                                              rtpTs (no _lastEmissionNtp);
                                              emitRtcpSr derives NTP
                                              from anchor; currentSrNtp
                                              pure-function preview;
                                              cname / hasEmissionRecord

[Phase 2, landed]
include/promeki/rtppacketbatch.h           plain value type — packets
                                              + frameIndex + clockRate
                                              + markerOnLast +
                                              rateCapBps + enqueuedAt
include/promeki/cadence.h                  deadline-anchored packet-
src/core/cadence.cpp                          pacing helper
include/promeki/pcmsilencefiller.h         reusable silence-payload
src/proav/pcmsilencefiller.cpp                builder
include/promeki/rtppacketizerthread.h      base class for the per-
src/network/rtppacketizerthread.cpp           stream packetizer threads
                                              + RtpFrameWork carrier
include/promeki/rtptxthread.h              thin base class for the per-
src/network/rtptxthread.cpp                   stream TX threads
include/promeki/rtpmediaio.h               Stream now carries
                                              RtpPacketizerThread* and
                                              RtpTxThread* (the three
                                              concrete subclasses are
                                              nested inside
                                              rtpmediaio.cpp);
                                              Atomic<bool> _anchorSeeded;
                                              AudioStream loses fifo /
                                              nextTimestamp from the
                                              writer path (reader path
                                              still uses fifo)
src/proav/rtpmediaio.cpp                   strand-as-router cut on
                                              executeCmd(Write); cancel-
                                              waiters-driven shutdown;
                                              VideoPacketizerThread,
                                              AudioPacketizerThread,
                                              DataPacketizerThread,
                                              VideoTxThread,
                                              AudioTxThread,
                                              DataTxThread nested classes;
                                              old SendThread / sendVideo /
                                              sendData deleted

[Phase 3 follow-ons, landed 2026-05-08]
include/promeki/rtpsession.h               sendPackets collapsed to a
src/network/rtpsession.cpp                    single RtpPacketBatch& overload;
                                              fillTransportHeader splits the
                                              transport-owned bits out of
                                              fillHeader; rateCapBps moved
                                              from VideoTxThread into the
                                              batch contract.
include/promeki/rtpmediaio.h               Per-kind Stream subclasses
src/proav/rtpmediaio.cpp                      (VideoStream / DataStream)
                                              alongside the existing
                                              AudioStream; cachedSps /
                                              cachedPps / cachedVps moved
                                              off Stream onto VideoStream.
include/promeki/rtpmediaio.h               _video → List<VideoStream>
src/proav/rtpmediaio.cpp                      _videos; _data →
                                              List<DataStream> _datas; all
                                              callers iterate or index [0];
                                              constructor no longer
                                              pre-populates — configure
                                              helpers push on first call.

include/promeki/rtpmediaio.h               WriterStream / ReaderStream
src/proav/rtpmediaio.cpp                      split — Stream now identity-
                                              only; per-kind writer types
                                              inherit from WriterStream;
                                              new VideoReaderStream /
                                              AudioReaderStream (owns
                                              fifo) / DataReaderStream
                                              inherit from ReaderStream.
                                              Six per-mode lists on
                                              RtpMediaIO; resetStream
                                              split into common / writer /
                                              reader helpers; openStream
                                              and openReaderStream take
                                              WriterStream& / ReaderStream&;
                                              imageDesc moved to
                                              VideoStream (writer) and
                                              readerImageDesc lives on
                                              VideoReaderStream / Audio
                                              ReaderStream as appropriate.

[Phase 5, landed 2026-05-08]
include/promeki/rtpstreamclock.h           receiver-side helper that
src/network/rtpstreamclock.cpp                maps RTP-TS ↔ wallclock NTP
                                              via the SR pair + clock
                                              rate; writer-side
                                              currentSrNtp / emitRtcpSr
                                              now delegate to the same
                                              math.
include/promeki/rtcppacket.h               parsers: parseHeader,
src/network/rtcppacket.cpp                    parseSenderReport,
                                              findSenderReports;
                                              Header / SenderReportInfo
                                              structs.
include/promeki/rtpsession.h               ReceivedSr struct +
src/network/rtpsession.cpp                    receivedSr() accessor;
                                              ReceiveThread demuxes
                                              RTCP via byte[1] in
                                              [200..223] and routes to
                                              the new handleRtcp helper.
include/promeki/rtpmediaio.h               Per-ReaderStream
src/proav/rtpmediaio.cpp                      RtpStreamClock cache,
                                              ReaderAggregator
                                              audioFifoFrontRtpTs
                                              cursor, refreshStreamClock
                                              + ntpToSteady helpers,
                                              wallclock-aligned drain
                                              and Frame::captureTime
                                              stamping in
                                              emitVideoFrame.
tests/unit/network/rtcppacket.cpp          10 new parser cases
tests/unit/network/rtpsession.cpp          4 new RTCP-demux /
                                              receivedSr cases
tests/unit/network/rtpstreamclock.cpp      11 new RtpStreamClock cases
                                              (round-trip, modular wrap,
                                              writer→SR→receiver
                                              < 1 sample error budget).
```

### Deferred TX-side polish

A few items in earlier sections of this devplan describe TX-side
work that did not end up landing in Phase 1 or Phase 2 — they are
not blockers for the smoke-test or for downstream RX work, so
they're parked here for follow-up rather than re-opening the
phases:

- **SDP `a=ts-refclk:localmac=...` and `a=mediaclk:direct=0`** —
  described in *§SDP advertisement of the clock anchor*.  The
  current `buildSdp` does not emit either line; receivers fall
  back to "trust the SR pair", which works against ffmpeg /
  ffplay / GStreamer today.  Fold into Phase 3 (the cleanup
  pass already touches the SDP builder for the `Stream`
  hierarchy split).
- **Per-stream randomized RTCP intervals** — described in
  *§Per-stream RTCP scheduling*.  The current `RtcpScheduler`
  still ticks at a single `_rtcpIntervalMs`.  Not visible to
  receivers in the single-stream-per-kind config we ship today;
  becomes interesting only once multi-stream config plumbing
  lands (out of scope here).
- **`MediaConfig::AudioRtpPrerollMs` knob on
  `AudioPacketizerThread`** — the value is read at
  configureAudioStream time and forwarded into the packetizer
  via `AudioStream::prerollSamples`, so the data path is in
  place; the packetizer holds off until the FIFO has
  accumulated `prerollSamples` worth before producing the
  first packet.  The Phase 3 stream-class split is the natural
  place to expose this through a typed accessor instead of a
  raw struct field.

## Library additions

Foundation work done as part of this refactor that lives outside
the RTP module. Each is independently testable and ships before the
RTP code that depends on it.

### `Queue<T>` optional bounding (`include/promeki/queue.h`)

- `void setMaxSize(size_t n)` — `0` (default) keeps the existing
  unbounded behavior; non-zero installs a "not full" wait
  condition that `push` / `emplace` block on when `size() >= n`.
- New blocking-push overloads accept a `timeoutMs` and return
  `Error::Timeout` / `Error::Cancelled` when applicable.
- `void cancelWaiters()` wakes both producers and consumers and
  causes outstanding waits to return `Error::Cancelled`. Used by
  `RtpMediaIO::cancelBlockingWork` so close paths don't have to
  push sentinels into every queue to unblock workers.
- All existing call sites continue to work: the size cap is opt-
  in, and the "not full" cv is only signalled when a max size
  has been set.

### `Frame::captureTime` (`include/promeki/frame.h`)

- New `MediaTimeStamp _captureTime` member on `FrameData`.
- Accessors `captureTime() const` / `setCaptureTime(MediaTimeStamp)`.
- `MediaIO`-level default stamping: a writer-side hook in
  `MediaIO::executeCmd(Write)` (or the equivalent base helper)
  fills `captureTime` with `MediaTimeStamp(TimeStamp::now(),
  ClockDomain::SystemMonotonic)` if the inbound Frame has none.
  Mirrors how MediaIO already synthesizes per-payload pts.
- CoW semantics already cover this — `_captureTime` is part of
  the shared `FrameData`, so copies share the value and any
  mutation goes through `_d.modify()`.

### `RtpPacketBatch` (`include/promeki/rtppacketbatch.h`)

```cpp
struct RtpPacketBatch {
        RtpPacket::List packets;          // payload bytes only; header zeroed
        FrameNumber     frameIndex;       // TX thread computes RTP-TS from this
        uint32_t        clockRate = 0;    //   + this via cumulativeTicks()
        bool            markerOnLast = true;  // false for non-AU-final batches
        uint64_t        rateCapBps   = 0; // 0 = no change; for VBR video
        TimeStamp       enqueuedAt;       // monotonic, for queue-latency histograms
};
```

Plain value type, no inheritance. Used for video and data
streams; audio uses `Queue<Buffer>` directly because each AES67
packet is one chunk and the cadence/RTP-TS counter lives in the
TX thread. Per-packet TXTIME deadlines (future SCM_TXTIME) live
on `Datagram::txTimeNs` inside the TX thread, not on the batch
— keeps the batch flat.

RTP-TS is intentionally **not** in the batch: the TX thread is
the single owner of RTP-TS so silence-fill / late-packet drop
logic is consistent with everything else on the wire.

### `Cadence` (`include/promeki/cadence.h`)

Deadline-anchored packet pacer used by `AudioTxThread` and
`VideoTxThread::Userspace`:

```cpp
class Cadence {
    public:
        explicit Cadence(const Duration &interval);
        void      anchor(const TimeStamp &t0);      // sets next() = t0
        TimeStamp next();                            // returns deadline, advances by interval
        void      reanchor(const TimeStamp &t);     // sets next() = t + interval (no burst)
        uint64_t  ticks() const;                     // packets emitted since anchor
        Duration  interval() const;
};
```

Anchored deadlines (`t0 + N × interval`) instead of accumulated
`sleep_for(interval)` so per-packet drift doesn't accumulate.

Semantics worth stating:

- After `anchor(t0)`, the first `next()` returns `t0` and the
  cursor advances by `interval`; the next call returns
  `t0 + interval`, etc.
- After `reanchor(t)` (long-stall recovery), the next `next()`
  returns `t + interval`. This deliberately skips one tick
  rather than emitting at `t` immediately, so a long stall
  doesn't produce a back-to-back burst with whatever fired
  just before it.
- `ticks()` is monotone across `reanchor` so per-stream stats
  reflect total emissions, not just emissions since the last
  anchor.

### `PcmSilenceFiller` (`include/promeki/pcmsilencefiller.h`)

Tiny helper that, given an `(AudioDesc, samplesPerPacket)`, builds
a reusable silence-payload `Buffer` once and returns it on demand.
Lives next to `AudioBuffer` since they share PCM layout
knowledge. `AudioTxThread` uses it; future audio output paths
that need cadence-fill can reuse it.

## Plan

### Phase 1 — Foundation

Lands the standalone library work plus the SR-anchor switchover.
The SR change *is* an intentional behavior change — RTCP SR's
NTP field becomes anchor-derived rather than measured at
emission time — but no thread / queue / packetizer architecture
moves yet, so blast radius is contained.

- [x] Add `Queue<T>::setMaxSize` / `cancelWaiters` / blocking-push
      overloads. Existing call sites unchanged. Unit tests cover
      bounded blocking, timeout, and cancel semantics.
- [x] Add `Frame::captureTime()` / `setCaptureTime()` and the
      `_captureTime` member on `FrameData`. Unit test: copy
      semantics, CoW detach on mutation.
- [x] Default-stamp `captureTime` in the `MediaIO` write path when
      a Frame arrives without one. Same pattern as the existing
      per-payload pts default-stamping.
- [x] Restore `RtpSession::setRtpAnchor(NtpTime, uint32_t)` and the
      anchor fields. **Delete** `_lastEmissionNtp`. `noteRtpEmission`
      keeps only the rtpTs. `emitRtcpSr` computes NTP from
      `anchor.ntp + (last_emit_rtp_ts - anchor.rtp_ts) / clockRate`,
      using modular `uint32_t` subtraction.
- [x] `RtpMediaIO` consults `frame.captureTime()` on the first
      `executeCmd(Write)`. If valid and convertible to NTP, use that;
      else fall back to `NtpTime::now()`. Seed every active session
      with the resulting `(captureNtp, 0)`.
- [x] Build smoke-test against ffplay + ffmpeg recv: confirm the SR
      mapping survives.

**Phase 1 status (2026-05-08):** Complete. `NtpTime` gained
`operator+(Duration)` / `operator-(Duration)` to support the
`(steady, wall)` reference-instant conversion. `RtpSession` exposes
`setRtpAnchor`, `anchorNtp`, `anchorRtpTs`, `currentSrNtp` (a pure-
function preview into the SR-derivation arithmetic), and a
loopback-transport SR-on-the-wire test pins the (NTP, RTP_TS)
bytes against the anchor formula.  Smoke-test:
`build/bin/mediaplay -s TPG -d Rtp --dc VideoRtpDestination:...`
plus headless `ffmpeg -i sdp -f null -` decodes 120 video frames +
audio cleanly across the wire.  Functional `promeki-test rtp.*`
suite is broken pre-existing on the receive side and stays broken
until the RX-side hardening lands (out of scope for Phase 1-4).

### Phase 2 — Per-stream queues and TX threads

- [x] Define `RtpPacketBatch` (see "Library additions") and unit-test
      it.
- [x] Add `Cadence` and unit-test it (anchor / next monotone /
      reanchor / drift behavior across simulated stalls).
- [x] Add `PcmSilenceFiller` and unit-test it (correct sample count,
      correct per-channel layout).
- [x] `PayloadQueue<T>` and `PacketQueue` are typedefs over
      `Queue<T>` with `setMaxSize` configured per stream.
      *(Realised: PayloadQueue is `Queue<RtpFrameWork>` inside
      `RtpPacketizerThread`; PacketQueue is
      `Queue<RtpPacketBatch>` for video/data and `Queue<Buffer>`
      for audio, owned by the TX-thread subclass.  No standalone
      typedefs ended up needed — the queues live as members of
      the threads that own them.)*
- [x] Add `RtpPacketizerThreadBase` and the three concrete subclasses.
      Move packetization logic (currently in `sendVideo` / `sendAudio`
      / `sendData`) into their respective packetizers.
- [x] Add `RtpTxThreadBase` and three subclasses. Move pacing /
      sending logic (currently in `sendVideo` / `sendAudio` /
      `sendData`) into their TX threads. `AudioTxThread` owns the
      cadence + silence-fill rule (publish silence stats back through
      the stream's stats counters).
      *(Silence stats counter publish is deferred to Phase 3
      where the per-stream stats split lands.  Cadence + silence-
      fill themselves are wired and exercised by the smoke-test.)*
- [x] `executeCmd(Write)` becomes a router: consult `captureTime`
      on first frame to seed the anchor, run the frame-rate
      pacing gate, push the whole `Frame` (CoW handle) onto every
      active stream's `PayloadQueue`, return.
- [x] Lifecycle: spawn / stop / join all threads in `openStream` /
      `resetAll`. `cancelBlockingWork` calls `Queue::cancelWaiters`
      on every queue so workers exit promptly without sentinels.
      Each thread gets a stable name via `Thread::setName` for
      `top` / `ps` / perf visibility:
      `RtpVidPkt/<i>`, `RtpVidTx/<i>`, `RtpAudPkt/<i>`,
      `RtpAudTx/<i>`, `RtpDatPkt/<i>`, `RtpDatTx/<i>`,
      `RtpRtcpSched`, `RtpRtcpRx` (Phase 5).

**Phase 2 status (2026-05-08):** Complete.  6 new nested
classes inside `RtpMediaIO` (3 packetizer + 3 TX threads), two
new free-standing base classes
(`RtpPacketizerThread`, `RtpTxThread`), strand-router cut on
`executeCmd(Write)`, sentinel-free shutdown via
`Queue::cancelWaiters`.  The strand-side per-frame interval
dropped from ~17 ms (blocked on TX completion) to <2.3 ms
(frame ferries refcount-bump only) measured against the
`mediaplay -s TPG -d Rtp` smoke test.  ffmpeg headless RX
decodes 119+ video frames + 374 KB audio per 2 s window for
both raw RGB and H.264, including `sprop-parameter-sets`
populated by the new `VideoPacketizerThread` (visible in the
`RtpVidPkt` thread name in log output).  AudioPacketizerThread
runs the FIFO drain on its own thread; AudioTxThread runs
Cadence-paced AES67 emission with `PcmSilenceFiller` covering
source stalls.  All 5200 unit tests still pass.

**Cleanup ordering** (must be spelled out in the implementation):

1. Strand stops accepting `executeCmd(Write)` and
   `cancelWaiters()` on every PayloadQueue.
2. For each stream, in parallel:
   a. Wait for the packetizer thread to exit.
   b. Packetizer's destructor `cancelWaiters()` on its
      PacketQueue.
   c. Wait for the TX thread to exit.
3. RTCP scheduler stops last (it reads
   `session.hasEmissionRecord()`).

### Phase 3 — Cleanup

- [x] Delete `SendThread` (replaced by per-stream packetizer + TX
      threads).  *(Done as part of Phase 2.)*
- [x] Delete the legacy `sendVideo` / `sendData` paths in
      `rtpmediaio.cpp` (logic moved into the new threads).
      *(Done as part of Phase 2.)*
- [x] Delete `RtpSession::sendPacketsPaced(...)` and the strided
      `sendPackets(packets, ts, stride, ...)` overload — neither
      has callers after pacing moves to the TX thread + `Cadence`.
      `VideoTxThread` now drives userspace pacing through a local
      `Cadence` instance anchored at the start of each frame; the
      strided-timestamp path was only used by the old audio TX
      and is unnecessary now that the audio TX stamps a single
      RTP-TS per AES67 packet.
- [x] Refactor `RtpSession::sendPackets` to a single
      `Error sendPackets(RtpPacketBatch &batch)` overload; the
      session fills version / seq / SSRC / PT (caller has filled
      marker + RTP-TS).  *(Landed 2026-05-08 as the first
      follow-on chunk; the session also applies
      `RtpPacketBatch::rateCapBps` before dispatch so the per-
      frame VBR rate cap moves out of `VideoTxThread` and into
      the batch contract.  All TX-thread call sites updated;
      `tests/unit/network/rtpsession.cpp` and the `LoopbackTransport`
      wire-level test rewritten against the new shape.)*
- [x] Split the `Stream` class hierarchy. The current `Stream`
      is doing writer + reader + codec-specific work all at once;
      separate it:
      - `Stream` (base): identity — transport, session, payload,
        payloadType, ssrc, dscp, mediaType, rtpmap, fmtp, active.
      - `WriterStream : Stream`: TX-side stats counters
        (`packetsSent`, `bytesSent`, `senderOctets`, plus
        per-thread histograms), all as `Atomic<int64_t>` /
        thread-safe forms so the strand-side `executeCmd(Stats)`
        can read while the TX thread writes.
      - `ReaderStream : Stream`: RX reassembly state
        (`reasmTimestamp`, `reasmPackets`, etc.) and RX stats.
      - `VideoStream : WriterStream`: parameter-set cache
        (`cachedSps` / `cachedPps` / `cachedVps`), wire pixel
        format, packetizer + TX thread pointers.
      - `AudioStream : WriterStream`: AES67 sizing
        (`packetSamples`, `packetBytes`, `packetTimeUs`,
        `prerollSamples`), the new silence stats fields,
        packetizer + TX thread pointers. **Removed** from
        `AudioStream`: `fifo` (moved to packetizer),
        `nextTimestamp` (moved to TX thread), `audioTx`
        (renamed `tx`).
      - `DataStream : WriterStream`: trivial today, symmetric.
      - Reader-side equivalents (`VideoReaderStream`, etc.)
        follow the same pattern; no virtual dispatch.

      *(Landed 2026-05-08 across two follow-on chunks.  Per-kind
      split first: `VideoStream` and `DataStream` joined the
      existing `AudioStream` as kind-specific subclasses, with
      `cachedSps` / `cachedPps` / `cachedVps` moved off the
      base into `VideoStream` so audio / data streams no
      longer carry a vestigial parameter-set cache.  The
      writer/reader split followed: `Stream` now carries
      identity-only fields (transport / session / payload /
      destination / rtpmap / fmtp / mediaType / active /
      clockDomain / ptpGrandmaster); `WriterStream` extends
      `Stream` with TX-thread pointers, TX stats counters
      (`packetsSent` / `bytesSent` / `senderOctets`), and TX
      histograms; `ReaderStream` extends `Stream` with the
      RX reassembly state, RX stats counters
      (`packetsReceived` / `bytesReceived` / `framesReceived`
      / `packetsLost`), the discovered descriptors
      (`readerImageDesc` / `readerAudioDesc`), and RX
      histograms.  `VideoStream` / `AudioStream` /
      `DataStream` inherit from `WriterStream`; new
      `VideoReaderStream` / `AudioReaderStream` (which owns
      the L16 RX `fifo`) / `DataReaderStream` inherit from
      `ReaderStream`.  `resetStream` split into
      `resetStreamCommon` (identity teardown) +
      `resetWriterStream` (stops packetizer + TX threads,
      wipes writer stats) + `resetReaderStream` (joins RX
      thread via session->stopReceiving, wipes reader stats /
      reasm state).  `openStream` and `openReaderStream`
      now take `WriterStream &` / `ReaderStream &` so the
      mode-bound work is type-checked at the call site.  The
      `imageDesc` field on `VideoStream` (writer-side source
      ImageDesc) replaces the previous overloaded use of
      `readerImageDesc` on Stream — `refreshSdpSprop` and
      `injectParameterSets` read from `imageDesc`,
      `emitVideoFrame` writes the discovered desc into
      `VideoReaderStream::readerImageDesc`.  All of
      `RtpMediaIO`'s callsites (configure*, applySdp, the
      RX callbacks, executeCmd(Stats), executeCmd(Close)
      histogram dump, the @ref RtcpScheduler's
      `emitForStream`, the strand-as-router `executeCmd(Write)`
      loop, the audio-FIFO setup in `executeCmd(Open)`)
      route through the per-mode list that matches the
      session's mode at open time.)*
- [x] Convert `_video` and `_data` from single `Stream` slots to
      `List<VideoStream>` / `List<DataStream>`, mirroring `_audios`.
      Today's config layer populates one entry per kind; the routing,
      SDP builder, RTCP scheduler, and per-stream threads iterate
      the lists. No multi-stream config plumbing yet — that lands
      separately — but the data model is ready for it.
      *(Landed 2026-05-08 as the third Phase 3 follow-on.
      `_video` → `List<VideoStream> _videos`, `_data` →
      `List<DataStream> _datas` (`_audios` was already a list).
      Constructor no longer pushes default entries — each
      `configure*Stream` helper pushes one entry per kind on
      first call (mirroring the existing audio pattern), so a
      close + reopen cycle starts from an empty list and rebuilds
      cleanly.  All ~180 access sites in `rtpmediaio.cpp`
      rewritten — `_video.foo` → `_videos[0].foo`, identity
      passes (`addStream(_video)`, `setupSession(_video)`,
      `refine(_video)`, `applyRate(_video, ...)`,
      `openStream(_video, ...)` etc.) replaced with `for (auto &v
      : _videos) ...` iteration loops; `RtcpScheduler::emitOnce`
      and `allStreamsHaveEmitted` likewise iterate.  TX-thread
      `run()` and packetizer `packetize()` each gate on
      `_videos.isEmpty()` / `_datas.isEmpty()` so a thread
      spawned before its stream is configured exits cleanly.
      ffmpeg headless RX decodes 60+ raw RGB frames + 375 KB
      audio per 2 s window unchanged from the pre-conversion
      smoke-test baseline.)*
- [x] All cumulative writer-side stats counters become
      `Atomic<int64_t>` (matches `Atomic<bool> _receiving` on
      `RtpSession`). Histograms updated by the TX thread are
      either snapshot-copied for the stats query or moved behind
      a single mutex — pick whichever is simpler at
      implementation time.
      *(`packetsSent` / `bytesSent` / `senderOctets` /
      `packetsReceived` / `bytesReceived` / `packetsLost` are
      now `Atomic<int64_t>` on `Stream`.  Histograms remain
      single-thread for now — no observed contention; left for
      a follow-up if it ever becomes one.)*
- [x] `MediaConfig::AudioRtpPrerollMs` (newly added) becomes a knob
      on the `AudioPacketizerThread` — wait for that many samples to
      buffer before producing the first packet list. Default 0.
      *(Plumbed via `AudioStream::prerollSamples` →
      `AudioPacketizerThread::onStart` resolves the FIFO reserve
      headroom; the `_prerollDone` gate inside the packetizer's
      `packetize` holds off downstream emission until the FIFO
      has accumulated `prerollSamples` worth.)*
- [x] Add stats fields:
      `silencePacketsEmitted`, `silenceSamplesEmitted`,
      `lateSamplesDropped` per `AudioStream`. Surface through
      `executeCmd(MediaIOCommandStats)`.
      *(`silencePacketsEmitted` and `silenceSamplesEmitted` land
      as `Atomic<int64_t>` on `AudioStream`; surfaced through
      `StatsAudioSilencePacketsEmitted` /
      `StatsAudioSilenceSamplesEmitted`.  `lateSamplesDropped`
      is deferred — the new design's bounded `PayloadQueue`
      already gives backpressure rather than late-drop, so the
      counter has no natural increment site today; revisit if a
      jitter-aware variant of `AudioPacketizerThread` lands
      that drops samples instead of blocking.)*

**Phase 3 partial completion (2026-05-08):** Atomic stats
counters, silence stats, the userspace-Cadence cut in
`VideoTxThread`, the deletion of `RtpSession::sendPacketsPaced`
and the strided `sendPackets` overload, and a sweep of stale
docstrings (Pacing table, Stream histogram comments, sendVideo
references) all landed in one chunk on top of Phase 2.  All
5200 unit tests pass; ffmpeg headless RX still decodes 119+
raw RGB / 110+ H.264 frames + ~370 KB audio per 2 s window.

**Four follow-on chunks landed 2026-05-08:**

  - **`RtpSession::sendPackets(RtpPacketBatch&)` collapse**
    landed: the only remaining `sendPackets` overload now
    takes a single `RtpPacketBatch &` parameter.  The TX
    thread stamps marker + RTP-TS on every packet before
    handoff; the session fills only the transport-owned
    fields (version / sequence number / SSRC / PT) via the
    new `fillTransportHeader` helper, plus the per-batch
    `rateCapBps` update via `setPacingRate` before dispatch.
    `VideoTxThread` builds single-packet sub-batches per
    cadence tick for userspace pacing, so per-packet pacing
    no longer relies on a session-level pacing helper.
    `tests/unit/network/rtpsession.cpp` and the loopback-
    transport wire-level test rewritten against the new
    shape — the test bytes still pin the
    `(NTP, RTP_TS)` SR pair against the anchor formula.
  - **Per-kind `Stream` class hierarchy** landed (partial
    Phase 3 #16): `VideoStream` and `DataStream` now exist
    as `Stream` subclasses next to the existing
    `AudioStream`.  Codec-specific state moved off `Stream`
    onto the per-kind types — the H.264 / HEVC parameter-
    set cache (`cachedSps` / `cachedPps` / `cachedVps`) now
    lives on `VideoStream` so audio and data streams no
    longer carry a vestigial parameter-set cache slot.
  - **Typed-list conversion** (Phase 3 #17): `_video` →
    `List<VideoStream> _videos`, `_data` →
    `List<DataStream> _datas` (matching the existing
    `_audios` shape).  All ~180 access sites rewritten —
    field accesses use `_videos[0].foo`; identity passes
    (`addStream`, `setupSession`, `refine`, `applyRate`,
    `openStream`, `openReaderStream`) iterate the list with
    `for (auto &v : _videos) ...`; the RTCP scheduler's
    `emitOnce` and `allStreamsHaveEmitted` likewise iterate.
    Constructor no longer pre-populates the lists — each
    `configure*Stream` helper pushes one entry on first
    call (mirroring the audio pattern), so a close + reopen
    cycle starts from an empty list and rebuilds cleanly.
    TX-thread `run()` and packetizer `packetize()` each
    gate on `_videos.isEmpty()` / `_datas.isEmpty()` so a
    thread spawned before its stream is configured exits
    cleanly.
  - **`WriterStream` / `ReaderStream` split** (Phase 3 #16
    deeper): `Stream` now carries identity-only fields
    (transport / session / payload / destination / rtpmap /
    fmtp / mediaType / active / clockDomain /
    ptpGrandmaster); `WriterStream` extends `Stream` with
    the TX-thread pointers, atomic TX stats counters, and TX
    histograms; `ReaderStream` extends `Stream` with the RX
    reassembly state, atomic RX stats counters, the
    discovered descriptors (`readerImageDesc` /
    `readerAudioDesc`), and RX histograms.  Per-kind
    classes inherit from the right side: `VideoStream` /
    `AudioStream` / `DataStream` from `WriterStream`; new
    `VideoReaderStream` / `AudioReaderStream` (owns the L16
    RX `fifo`) / `DataReaderStream` from `ReaderStream`.
    `RtpMediaIO` carries six per-mode lists; the
    `_readerMode` flag determines which set is populated.
    `resetStream` split into `resetStreamCommon` +
    `resetWriterStream` + `resetReaderStream`.  `openStream`
    and `openReaderStream` now take `WriterStream &` and
    `ReaderStream &` respectively so the mode-bound work is
    type-checked at the call site.  All RX callbacks
    (`onVideoPacket` / `onAudioPacket` / `onDataPacket` /
    `emitVideoFrame` / `emitDataMessage`) and writer-side
    helpers (RTCP scheduler, `executeCmd(Stats)`,
    `executeCmd(Close)` histogram dump,
    `executeCmd(Write)` strand router, audio-FIFO setup)
    route through the matching list.  Writer-side image
    descriptor moved to `VideoStream::imageDesc`
    (previously the overloaded `readerImageDesc` field on
    `Stream`); `refreshSdpSprop` and `injectParameterSets`
    read from `imageDesc`, `emitVideoFrame` writes the
    discovered desc into
    `VideoReaderStream::readerImageDesc`.

### Phase 4 — Test checklist

This is an index, not a separate landing — each test ships with
the phase that introduces the code it covers. Items are grouped
here so reviewers can see the verification surface in one place.

- [x] Unit: `Queue<T>` bounded blocking, timeout, cancel-unblocks-
      both-sides.  (`tests/unit/queue.cpp`, 13 new cases.)
- [x] Unit: `Frame::captureTime` getter/setter, CoW detach.
      (`tests/unit/frame.cpp`, 4 new cases.)  MediaIO default
      stamping is exercised end-to-end through the ffmpeg smoke
      test rather than via a dedicated unit case (would require
      a backend hook just for the test); the inline one-liner in
      `MediaIOSink::writeFrame` is small enough that the smoke
      test is the right level for it.
- [x] Unit: `RtpPacketBatch` shape; `PayloadQueue` / `PacketQueue`
      blocking semantics + cancel shutdown.  (Shape covered by
      `tests/unit/network/rtppacketbatch.cpp`, 4 cases; queue
      blocking/cancel covered by the bounded-`Queue<T>` cases
      above — `PayloadQueue` / `PacketQueue` are typed `Queue`
      members, so their semantics inherit directly.)
- [x] Unit: `NtpTime + Duration` round-trips with sub-µs
      precision — the building block the
      `(steadyNow, wallNow, delta)` formula uses.
      (`tests/unit/network/rtcppacket.cpp`, 5 new cases.)  A
      dedicated `MediaTimeStamp → NtpTime` integration test is
      not in place; the formula is exercised end-to-end in the
      ffmpeg smoke test against the SR's reported NTP.
- [x] Unit: `RtpSession::emitRtcpSr` carries
      `anchor.ntp + (last_rtp_ts - anchor.rtp_ts) / clockRate`
      exactly, including the `uint32_t` wraparound case.
      (`tests/unit/network/rtpsession.cpp`, including a wire-
      level loopback-transport test that pins the SR bytes.)
- [x] Unit: `RtpSession::emitRtcpSr` NTP is monotone non-decreasing
      across consecutive emissions for any monotone input rtpTs.
      (Same file, `currentSrNtp is monotone non-decreasing`
      subcase — 1000 ticks.)
- [x] Unit: `Cadence` returns monotone `next()`, `reanchor` jumps
      forward without burst, no per-packet drift over 10 k ticks.
      (`tests/unit/cadence.cpp`, 9 cases.)
- [x] Unit: `PcmSilenceFiller` produces correct sample / channel
      / endianness bytes for L16, L24, etc.
      (`tests/unit/pcmsilencefiller.cpp`, 11 cases covering
      signed/float = zero, unsigned BE/LE = midpoint.)
- [ ] Unit: `AudioPacketizerThread` produces sample-exact wire
      content from a synthetic input pattern.  (Deferred — the
      class is currently nested inside `RtpMediaIO`, so a
      dedicated test would either need the class promoted to a
      top-level type or a friend-class hook.  Phase 3 splits
      out per-kind stream classes and is a natural place to
      revisit this.)
- [ ] Unit: `AudioTxThread` emits silence packets at the correct
      cadence when its `PacketQueue` is empty, increments
      `silenceSamplesEmitted`, and the wire RTP-TS is contiguous
      across the gap.  (Deferred for the same reason; the
      `silenceSamplesEmitted` counter itself is a Phase 3 work
      item.)
- [ ] Functional (`utils/promeki-test/cases/rtp.cpp`): adapt the
      existing roundtrip suite to the new architecture once RX-side
      hardening lands. Expected to pass with no discontinuities
      after both halves are correct.
- [ ] Functional: AES67 listener (gst-launch `rtpL16depay` or
      equivalent) asserts continuous sample count across a forced
      source stall — regression test for the silence-fill rule.
- [ ] Functional: emitted SDP contains `a=ts-refclk:localmac=...`
      and `a=mediaclk:direct=0` for SystemMonotonic-anchored
      streams; emitted SDP is byte-identical across multiple
      open/close cycles for the same config (no spurious
      regeneration).  *(`a=ts-refclk` / `a=mediaclk` lines are
      not yet emitted — see "Deferred TX-side polish" below.)*

**TX smoke-test (manual / non-checklist):**
`mediaplay -s TPG -d Rtp --dc VideoRtpDestination:127.0.0.1:5004
--dc AudioRtpDestination:127.0.0.1:5006 --duration 3` paired with
headless `ffmpeg -i sdp -f null -` decodes 119+ raw RGB frames /
104+ H.264 frames + ~370 KB audio per 2 s capture window with no
errors and the expected `rtpmap` / `sprop-parameter-sets` /
clock-rate values in the SDP.  Reproducible via the helper at
`scripts/rtp-rx-ffplay.sh`.

### Phase 5 — Reader-side SR consumption (lip-sync)

Lip-sync is in scope for the reader, not just the writer. The
TX-side anchor work makes per-stream `(NTP, RTP_TS)` pairs
correct on the wire; this phase plumbs them through on the
receive side so a multi-stream RTP reader can align essences in
a common wallclock domain instead of relying on
"video-as-clock + drain-audio-FIFO" approximations.

- [x] **Reader-side RTCP parsing** — today the per-session receive
      thread only handles RTP. Add an RTCP demux at the receive
      socket (rtcp-mux means the same UDP port carries both;
      payload-type ≥ 200 → RTCP, otherwise RTP). Parse SR
      packets via `RtcpPacket` (extend it as needed — currently
      it only has builders, no parsers). On unknown packet types
      drop silently — RTCP is forward-compatible by design.
      *(Landed 2026-05-08.  `RtcpPacket` gained `parseHeader`,
      `parseSenderReport`, and `findSenderReports` plus a
      `Header` / `SenderReportInfo` struct surface;
      `RtpSession::ReceiveThread::run` now demuxes via
      @c byte[1] in [200..223] and routes RTCP datagrams to the
      new `handleRtcp` helper, which walks the compound for SRs
      via `findSenderReports` and silently drops everything else.)*
- [x] `RtpSession` reader-side state: `lastReceivedSr` =
      `(NtpTime ntp, uint32_t rtpTs, TimeStamp arrivedAtMono)`,
      protected by `_rtcpMutex`. Updated by the receive thread
      whenever a parsed RTCP packet contains an SR for this
      session's SSRC.
      *(Landed 2026-05-08 as `RtpSession::ReceivedSr`
      (`{ NtpTime ntp; uint32_t rtpTs; TimeStamp arrivedAt;
      bool valid; }`) on the public API and
      `_lastReceivedSr` private storage protected by the
      existing `_rtcpMutex`; multi-SR compounds keep the last
      entry, which is the sender's-own SR by RFC 3550 §6.4
      convention.)*
- [x] Add `RtpSession::receivedSr() const` returning the snapshot.
      Returns invalid until the first SR has arrived (some senders
      delay SRs, so receivers must tolerate "no SR yet").
      *(Landed 2026-05-08 alongside the RTCP demux above.  Returns
      a default-constructed `ReceivedSr` (`valid == false`) until
      the first SR is parsed.)*
- [x] Add a small `RtpStreamClock` helper (lives next to
      `RtpSession`) that, given a session's `lastReceivedSr` and
      `clockRate`, converts an arbitrary RTP-TS into an NTP
      wallclock instant via the same arithmetic the writer uses:
      `ntp = sr.ntp + (rtpTs - sr.rtpTs) / clockRate`. Modular
      `uint32_t` subtraction. Returns invalid if no SR is yet
      known.
      *(Landed 2026-05-08 as
      `include/promeki/rtpstreamclock.h` /
      `src/network/rtpstreamclock.cpp`.  Carries `srNtp` /
      `srRtpTs` / `clockRate` and exposes `toNtp(rtpTs)` plus
      the inverse `toRtpTs(ntp)` used by the wallclock-aligned
      audio drain.  `RtpSession::currentSrNtp` /
      `emitRtcpSr` now delegate the writer-side derivation to
      the same `RtpStreamClock::toNtp` arithmetic, so the
      sender and receiver run identical math against the
      `(NTP, RTP-TS)` anchor pair.)*
- [x] Replace the existing `ReaderAggregator`'s
      "video-frame-as-clock + drain-audio-FIFO" model with
      wallclock-aligned aggregation:
      - Each incoming RTP packet (video / audio / data) is
        labeled with its computed wallclock NTP via
        `RtpStreamClock`.
      - The aggregator emits a Frame when video for wallclock T
        arrives; it pulls the audio samples whose wallclock
        spans `[T, T+frame_duration)` and the latest data
        whose wallclock ≤ T.
      - When `RtpStreamClock` is invalid (no SR yet), fall
        back to today's behavior so the reader still produces
        output at startup before SRs arrive.
      *(Landed 2026-05-08.  Each `ReaderStream` carries its own
      cached `RtpStreamClock`, refreshed by `refreshStreamClock`
      whenever the underlying session reports a new SR
      (cheap `arrivedAt`-based change detection, called from
      every RX packet callback).  The aggregator gained an
      `audioFifoFrontRtpTs` cursor that tracks the audio-RTP-TS
      of the front sample currently in the FIFO; on push the
      cursor is seeded / realigned on a packet gap, on pop it
      advances by the popped count.  `emitVideoFrame` checks
      whether both video and audio streams have valid
      `RtpStreamClock`s — if so, it computes
      `T = videoClock.toNtp(frameRtpTimestamp)`, derives the
      target audio-RTP-TS as `audioClock.toRtpTs(T)`, and drops
      any FIFO samples whose RTP-TS precedes the target before
      popping `samplesPerFrame`.  The pre-SR fallback (the old
      "samplesPerFrame from videoFrameIndex" drain) is
      preserved for the startup window.)*
- [x] `Frame::captureTime` on the reader side: aggregator stamps
      it from the video packet's computed wallclock NTP so
      downstream consumers see the original sender's capture
      instant. Pipeline and downstream backends preserve it
      across CoW copies (already covered by Phase 1).
      *(Landed 2026-05-08.  `RtpMediaIO::ntpToSteady` converts
      the per-Frame wallclock NTP to a local `TimeStamp` via a
      `(steady, NTP)` reference instant pinned at open time.
      `emitVideoFrame` calls `frame.setCaptureTime(MediaTimeStamp(
      steady, ClockDomain::SystemMonotonic))` once both clocks
      are valid; the matching audio payload stamps its `pts` /
      Metadata::CaptureTime from the same value so a
      downstream backend sees a coherent A/V capture instant.
      Pre-SR fallback continues to use the existing
      `(rxFrameStartTime, v.clockDomain)` MediaTimeStamp so
      the captureTime field is always populated.)*
- [x] Unit: `RtpStreamClock` round-trips writer-side
      `(anchor, rtpTs)` to receiver-side `(SR, rtpTs)` with
      < 1 sample of error.
      *(Landed 2026-05-08 as
      `tests/unit/network/rtpstreamclock.cpp`, 11 cases
      covering default-construction validity, SR-anchor
      mapping, half-second / one-second offsets, the
      modular `uint32_t` wraparound case, and the
      writer-anchor → SR-pair → receiver-clock round-trip
      with a `< 1 sample` error budget.  Companion
      `RtcpPacket` parser cases land in
      `tests/unit/network/rtcppacket.cpp` (10 new cases) and
      RTCP-demux + `receivedSr` round-trip cases land in
      `tests/unit/network/rtpsession.cpp` (4 new subcases
      driven through `LoopbackTransport`).)*
- [ ] Functional: dual-stream roundtrip (video + audio with a
      simulated network delay between sources) shows zero A-V
      drift after one SR period.
      *(Deferred — the existing `promeki-test rtp.*` suite
      sees the pre-existing receive-side sync gate eat ~5 of
      60 frames at startup before the marker boundary lands,
      independent of Phase 5.  Once the RX-side hardening
      noted under "Out of scope" addresses that, the rtp.*
      suite can be extended with a wallclock-drift assertion
      that reads the per-Frame `captureTime` to confirm zero
      A-V drift after one SR period.)*

**Phase 5 status (2026-05-08):** Core landed.  The
implementation chunks are six new public surfaces:
`RtcpPacket::{parseHeader, parseSenderReport, findSenderReports,
Header, SenderReportInfo}`, `RtpStreamClock`,
`RtpSession::ReceivedSr` + `RtpSession::receivedSr`, and the
RTCP-demux path inside `RtpSession::ReceiveThread::run`.
The `ReaderAggregator` rewrite ties them together — each
`ReaderStream` carries its own cached `RtpStreamClock`, the
aggregator tracks an `audioFifoFrontRtpTs` cursor for FIFO
position → wallclock mapping, and `emitVideoFrame` drains
audio against the wallclock window when both clocks are valid.
RX-side `Frame::captureTime` is populated from the video
stream's wallclock NTP via a one-shot `(steady, NTP)` anchor
pinned at open time.  All 5233 unit tests pass (including 25
new Phase-5-specific cases); the smoke-test pipeline
`mediaplay -s TPG -d Rtp --dc VideoRtpDestination:...` paired
with headless `ffmpeg -i sdp -f null -` decodes 119+ raw RGB
frames + ~744 KB audio per 4 s window unchanged from the
Phase 2 baseline.

## Out of scope

- RX-side hardening (jitter buffer, audio FIFO sizing, sample-position
  tracking under partial-frame drops). Tracked separately under
  `proav/backends.md` once TX is solid.
- Per-packet `SCM_TXTIME` deadlines — deferred under
  [proav/optimization.md](../proav/optimization.md). The TX thread
  abstraction does make the eventual wiring trivial: a new pacing
  branch in the TX thread that stamps `Datagram::txTimeNs` per
  packet and uses `sendPackets` with the kernel TXTIME socket flag.
- DPDK transport — same.
- Multi-stream config plumbing — `_videos`, `_audios`, `_datas`
  are all `List<Stream>`-shaped after this refactor; configuration
  today populates one entry per kind. Adding the config keys to
  specify multiple `VideoRtpDestination` / `AudioRtpDestination` /
  `DataRtpDestination` entries is a config layer change only and
  orthogonal to this refactor.
- Per-frame SR anchor refinement — today the anchor is seeded
  once from the first Frame's `captureTime` and SRs are derived
  by extrapolation from the per-stream RTP-TS counter. A future
  optimization is to refresh the anchor from each Frame's
  `captureTime` (so SR NTP corrects for any drift between the
  capture clock and the RTP-TS clock without requiring PTP). The
  data path is in place — `Frame::captureTime` is plumbed through
  — but the refresh logic is left for a follow-up.

## Validation goal

The test command stays the same:

```
mediaplay -s TPG -c VideoEncoder --cc VideoCodec:H264 \
          -d Rtp --dc VideoRtpDestination:127.0.0.1:5004 \
          --dc RtpSaveSdpPath:/tmp/h264.sdp
```

Expected behavior after this refactor lands:

- ffplay started at any time relative to the sender comes up clean.
- A-V drift is bounded (RTCP SR maintains correspondence).
- No audio dropouts at IDR boundaries (the audio TX thread is fed
  by the audio packetizer, which is fed by the strand — strand
  stalls do propagate, but the queue depths absorb short stalls).
- Wire-side packet pacing is regular for both streams (kernel `fq`
  for video, AES67 cadence for audio).
- Multiple successive ffmpeg / ffplay runs against the same sender
  behave identically — no startup-state asymmetry.

The lingering "audio leads video at startup" symptom in low-buffer
ffplay mode is acknowledged but not chased on the TX side; it is
upstream encoder latency arriving at the receiver before the
matching video. Buffered receivers (default ffplay, GStreamer with
default `latency`, hardware ST 2110 receivers) align both streams
from the SR pair, and after Phase 5 our own RTP reader does the
same via `RtpStreamClock`-derived wallclock instants.
