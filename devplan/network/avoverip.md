# AV-over-IP Building Blocks

**Phase:** 3C
**Dependencies:** Phase 3A (UdpSocket, AbstractSocket), Phase 3B (SslSocket for secure transports)
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

General-purpose primitives for building AV-over-IP implementations (ST 2110, AES67, custom protocols). Higher-level standard-specific implementations (NDI, Dante) will wrap vendor libraries through our API.

> The SMPTE ST 2110 conformance roadmap (system timing,
> uncompressed video, traffic shaping, PCM/AES3 audio, ANC, 2022-7
> redundancy, and the PacketScheduler / PtpClock / RtpMediaClock
> decoupling work) is tracked in [2110.md](2110.md). This document
> retains the general AV-over-IP building blocks.

**Completed:** PrioritySocket, RtpSession (including `sendPacketsPaced()` for ST 2110-21 pacing with a 100 µs sleep-skip threshold to avoid syscall overhead on high-packet-rate streams, plus `startReceiving()` / `stopReceiving()` receive loop with `PacketCallback`), RtpPacket, RtpPayload (L24, L16, RawVideo, JPEG with RFC 2435 DQT/entropy parsing and **dynamic 4:2:0/4:2:2 detection via SOF0 sampling factor** for the RTP Type byte, **RFC 2435 §3.1.5 W/8 + H/8 8-bit limit guard** — `pack()` emits `promekiWarnThrottled` when configured dimensions exceed 2040 px so UHD misconfigurations surface as a clear warning instead of silent SOF0 truncation at the receiver; callers should use JPEG XS (ST 2110-22) or uncompressed (ST 2110-20) for UHD, **JPEG XS via `RtpPayloadJpegXs` RFC 9134 codestream-mode pack/unpack**), RtpPayloadJson, SdpSession (insertion-order-preserving attributes, `fromFile()` / `toFile()`, structured `RtpMap` / `FmtpParameters` accessors, equality operators), MulticastManager, **MulticastReceiver** (standalone datagram receiver with owned worker thread, ASM/SSM group join, per-callback delivery).

**UdpSocket / UdpSocketTransport additions:** `setReceiveBufferSize(int bytes)` / `setSendBufferSize(int bytes)` (set `SO_RCVBUF` / `SO_SNDBUF`; pass 0 to leave kernel default). `UdpSocketTransport` exposes matching `setReceiveBufferSize` / `setSendBufferSize` setters and `receiveBufferSize()` / `sendBufferSize()` accessors; buffer sizing applied before `bind()` at `open()` time.

**RtpMediaIO socket-buffer sizing:** `MediaConfig::RtpRecvBufferBytes` / `RtpSendBufferBytes` config keys (default 8 MiB each) forwarded to `UdpSocketTransport` at stream-open time so high-bitrate uncompressed streams have headroom against kernel ring overflow.

---

## PtpClock

IEEE 1588 PTP clock synchronization. Building block for AES67/ST 2110 sync.

**Files:**
- [ ] `include/promeki/ptpclock.h`
- [ ] `src/network/ptpclock.cpp`
- [ ] `tests/ptpclock.cpp`

**Implementation checklist:**
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `enum Role { Slave, Master, Passive }`
- [ ] `enum Profile { Default, AES67, ST2110 }` — pre-configured PTP profiles
- [ ] `Error start(const String &interface, Profile profile = Default)`
- [ ] `void stop()`
- [ ] `bool isRunning() const`
- [ ] `Role role() const` — current BMCA-determined role
- [ ] `int64_t offset() const` — clock offset from master in nanoseconds
- [ ] `int64_t delay() const` — path delay in nanoseconds
- [ ] `SocketAddress masterAddress() const`
- [ ] `uint8_t grandmasterClockId() const` or `String grandmasterClockId()`
- [ ] `uint8_t domainNumber() const`, `setDomainNumber(uint8_t)`
- [ ] `bool isSynchronized() const` — true when offset is within acceptable range
- [ ] `PROMEKI_SIGNAL(synchronized)` — emitted when sync achieved
- [ ] `PROMEKI_SIGNAL(syncLost)` — emitted when sync lost
- [ ] `PROMEKI_SIGNAL(roleChanged, Role)`
- [ ] `PROMEKI_SIGNAL(offsetChanged, int64_t)`
- [ ] Internal: PTP message types
  - [ ] Sync, Follow_Up, Delay_Req, Delay_Resp
  - [ ] Announce (for BMCA)
- [ ] Uses UdpSocket on ports 319 (event) and 320 (general)
- [ ] Multicast groups: 224.0.1.129 (default), 224.0.1.130 (peer delay)
- [ ] Hardware timestamping support (SO_TIMESTAMPING) where available
- [ ] Profile-specific defaults:
  - [ ] AES67: domain 0, 1-second announce interval, 8 announce receipts
  - [ ] ST2110: domain 127 (common), DSCP 48 for PTP traffic
- [ ] Doctest: construction, start/stop, profile selection, message serialization/parsing (unit-level)

---

### RtpMediaIO SDP improvements (2026-05-05)

- **`a=framerate` round-trip**: `MediaDesc::toSdp()` now stamps
  `a=framerate=<rational>` on every video `m=` section; `fromSdp()`
  reads it back.  `RtpMediaIO` additionally stamps the rate in
  `buildSdp()` and reads it back via `applySdp()`.  Rational form
  (e.g. `60000/1001`) round-trips NTSC rates exactly.  Without this,
  RFC 2435 / RFC 4175 streams had no way to convey cadence in SDP,
  silently breaking per-frame audio aggregation math on non-29.97
  streams.

- **JPEG `x-dimensions` fmtp extension**: `ImageDesc::toSdp()` now
  emits `x-dimensions=W,H` in the JPEG `fmtp` so downstream planners
  have geometry before the first RTP packet.  `fromSdp()` parses the
  comma form and the `WxH` form.  Also fixes the previous regression
  where `colorimetry` / `RANGE` fmtp was silently dropped when
  `x-dimensions` was the only attribute.

- **`AudioFileFactory_LibSndFile`**: extension list built by probing
  `SFC_GET_FORMAT_MAJOR` at runtime so stripped libsndfile builds
  (without Vorbis/FLAC/Opus) report a clean lookup miss instead of
  failing at open.  `sf_format_check()` guard added to the write path
  for the same reason.

- **`AudioFormat` signed-type zero-preservation**: `integerToFloat` /
  `floatToInteger` for signed integer types now scale by
  `max(|Min|, Max)` so integer 0 maps to `0.0f` exactly.  The
  previous asymmetric linear mapping pushed silence to a tiny DC
  offset that derailed sync detectors expecting zero-mean silence
  between codewords.

### Deferred Items

Items deferred from completed classes, to be revisited when needed:

- **RtpSession**: Header extension support, RTCP (Sender/Receiver Reports, SDES), SSRC collision detection
- **RtpPayloadJpeg**: `unpack()` reassembles raw fragments but does not reconstruct full JPEG headers (SOI/DQT/SOF0/DHT/SOS); no restart marker support
- **RtpPayloadJpegXs**: Slice packetization mode (K=1) and interlaced framing are not yet implemented — only codestream mode (K=0) is wired up
