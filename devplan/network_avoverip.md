# AV-over-IP Building Blocks

**Phase:** 3C
**Dependencies:** Phase 3A (UdpSocket, AbstractSocket), Phase 3B (SslSocket for secure transports)
**Library:** `promeki`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

General-purpose primitives for building AV-over-IP implementations (ST 2110, AES67, custom protocols). Higher-level standard-specific implementations (NDI, Dante) will wrap vendor libraries through our API.

**Completed:** PrioritySocket, RtpSession (including `sendPacketsPaced()` for ST 2110-21 pacing), RtpPacket, RtpPayload (L24, L16, RawVideo, JPEG with RFC 2435 DQT/entropy parsing and 4:2:2 support), SdpSession (insertion-order-preserving attributes), MulticastManager.

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

### Deferred Items

Items deferred from completed classes, to be revisited when needed:

- **RtpSession**: Header extension support, RTCP (Sender/Receiver Reports, SDES), receive path, SSRC collision detection, `sendmmsg()` batch sending (see `proav_optimization.md`)
- **RtpPayloadJpeg**: `unpack()` reassembles raw fragments but does not reconstruct full JPEG headers (SOI/DQT/SOF0/DHT/SOS); no restart marker support
