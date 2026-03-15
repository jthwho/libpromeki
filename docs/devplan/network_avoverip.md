# AV-over-IP Building Blocks

**Phase:** 3C
**Dependencies:** Phase 3A (UdpSocket, AbstractSocket), Phase 3B (SslSocket for secure transports)
**Library:** `promeki-network`

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class requires complete doctest unit tests. See `README.md` for full requirements.

General-purpose primitives for building AV-over-IP implementations (ST 2110, AES67, custom protocols). Higher-level standard-specific implementations (NDI, Dante) will wrap vendor libraries through our API.

---

## PrioritySocket

Extends UdpSocket with DSCP/TOS QoS field setting. Essential for AV traffic prioritization.

**Files:**
- [ ] `include/promeki/network/prioritysocket.h`
- [ ] `src/net/prioritysocket.cpp`
- [ ] `tests/prioritysocket.cpp`

**Implementation checklist:**
- [ ] Derive from `UdpSocket`
- [ ] `enum Priority { BestEffort, Background, Video, Voice, NetworkControl }`
- [ ] `Error setPriority(Priority p)` — convenience that maps Priority to DSCP value, then calls `setHint(DSCP, value)`
- [ ] `Priority priority() const` — reverse-maps current DSCP hint value to Priority enum
- [ ] Direct DSCP access via inherited hint system: `setHint(DSCP, value)` / `hint(DSCP)`
- [ ] DSCP value mapping:
  - [ ] BestEffort = 0 (CS0)
  - [ ] Background = 8 (CS1)
  - [ ] Video = 34 (AF41) — standard for broadcast video
  - [ ] Voice = 46 (EF) — standard for real-time audio (AES67)
  - [ ] NetworkControl = 48 (CS6) — PTP traffic
- [ ] Platform guard: DSCP setting may require elevated permissions on some platforms
- [ ] Doctest: setPriority/getPriority, setDSCP/getDSCP, verify setsockopt is called

---

## PtpClock

IEEE 1588 PTP clock synchronization. Building block for AES67/ST 2110 sync.

**Files:**
- [ ] `include/promeki/network/ptpclock.h`
- [ ] `src/net/ptpclock.cpp`
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

## RtpSession

Real-time Transport Protocol (RFC 3550). Payload-type agnostic.

**Files:**
- [ ] `include/promeki/network/rtpsession.h`
- [ ] `src/net/rtpsession.cpp`
- [ ] `tests/rtpsession.cpp`

**Implementation checklist:**
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `Error start(const SocketAddress &localAddr)`
- [ ] `void stop()`
- [ ] `Error sendPacket(const Buffer &payload, uint32_t timestamp, uint8_t payloadType)`
- [ ] `Error sendPacket(const Buffer &payload, uint32_t timestamp, uint8_t payloadType, bool marker)`
- [ ] `uint32_t ssrc() const` — locally generated SSRC
- [ ] `void setSsrc(uint32_t)` — override auto-generated SSRC
- [ ] `uint16_t sequenceNumber() const`
- [ ] `uint32_t timestamp() const`
- [ ] `void setPayloadType(uint8_t pt)` — default payload type
- [ ] `void setClockRate(uint32_t hz)` — timestamp clock rate (e.g., 48000 for audio, 90000 for video)
- [ ] `PROMEKI_SIGNAL(packetReceived, Buffer payload, uint32_t timestamp, uint8_t payloadType, bool marker)`
- [ ] `PROMEKI_SIGNAL(ssrcCollision, uint32_t)` — SSRC conflict detected
- [ ] Internal: RTP header (RFC 3550)
  - [ ] Version (2), padding, extension, CSRC count, marker, payload type
  - [ ] Sequence number (auto-increment)
  - [ ] Timestamp
  - [ ] SSRC
  - [ ] Header extension support
- [ ] Uses UdpSocket (or PrioritySocket) internally
- [ ] Optional RTCP support (separate session or integrated):
  - [ ] Sender Report (SR), Receiver Report (RR)
  - [ ] SDES (source description)
- [ ] Doctest: send/receive packets via loopback, sequence number increment, header serialization/parsing

---

## RtpPayload

Base class for RTP payload type handlers.

**Files:**
- [ ] `include/promeki/network/rtppayload.h`
- [ ] `src/net/rtppayload.cpp`

**Implementation checklist:**
- [ ] Abstract base class
- [ ] `virtual uint8_t payloadType() const = 0`
- [ ] `virtual uint32_t clockRate() const = 0`
- [ ] `virtual Buffer pack(const void *mediaData, size_t size) = 0` — media -> RTP payload(s)
- [ ] `virtual Buffer unpack(const Buffer &rtpPayload) = 0` — RTP payload -> media
- [ ] `virtual size_t maxPayloadSize() const` — default MTU-safe (1200 bytes)
- [ ] Concrete subclasses (implement as needed for AV-over-IP):
  - [ ] `RtpPayloadL24` — 24-bit linear audio (AES67 standard)
  - [ ] `RtpPayloadL16` — 16-bit linear audio
  - [ ] `RtpPayloadRawVideo` — RFC 4175 raw video (ST 2110-20)
- [ ] Doctest: pack/unpack round-trip for each concrete payload type

---

## SdpSession

SDP (Session Description Protocol, RFC 4566) parser/generator. Used by ST 2110 and AES67 for stream advertisement.

**Files:**
- [ ] `include/promeki/network/sdpsession.h`
- [ ] `src/net/sdpsession.cpp`
- [ ] `tests/sdpsession.cpp`

**Implementation checklist:**
- [ ] Data object with PROMEKI_SHARED_FINAL
- [ ] `static Result<SdpSession> fromString(const String &sdp)` — parse SDP text
- [ ] `String toString() const` — generate SDP text
- [ ] Session-level fields:
  - [ ] `String sessionName() const`, `setSessionName(const String &)` — s= line
  - [ ] `String origin() const`, `setOrigin(...)` — o= line (username, session-id, version, net-type, addr-type, address)
  - [ ] `String connectionAddress() const`, `setConnectionAddress(...)` — c= line
  - [ ] `uint64_t sessionId() const`
  - [ ] `uint64_t sessionVersion() const`
- [ ] `List<MediaDescription> mediaDescriptions() const`
- [ ] `void addMediaDescription(const MediaDescription &)`
- [ ] Nested `MediaDescription` class:
  - [ ] `String mediaType() const` — "audio", "video", "application"
  - [ ] `uint16_t port() const`
  - [ ] `String protocol() const` — "RTP/AVP", "RTP/SAVP"
  - [ ] `List<uint8_t> payloadTypes() const`
  - [ ] `HashMap<String, String> attributes() const` — a= lines
  - [ ] `String attribute(const String &name) const`
  - [ ] `void setAttribute(const String &name, const String &value)`
  - [ ] AES67/ST 2110 specific attributes: ptime, rtpmap, fmtp, source-filter, clock-domain
- [ ] `::Ptr`, `::List`, `::PtrList`
- [ ] Doctest: parse known SDP, generate and re-parse (round-trip), AES67 SDP example, ST 2110 SDP example

---

## MulticastManager

Manages multicast group membership for multi-stream scenarios.

**Files:**
- [ ] `include/promeki/network/multicastmanager.h`
- [ ] `src/net/multicastmanager.cpp`
- [ ] `tests/multicastmanager.cpp`

**Implementation checklist:**
- [ ] Derive from `ObjectBase`, use `PROMEKI_OBJECT`
- [ ] `Error joinGroup(const SocketAddress &group, UdpSocket *socket)`
- [ ] `Error joinGroup(const SocketAddress &group, UdpSocket *socket, const String &interface)`
- [ ] `Error leaveGroup(const SocketAddress &group, UdpSocket *socket)`
- [ ] `void leaveAllGroups()` — leave all managed groups
- [ ] `List<SocketAddress> activeGroups() const`
- [ ] `bool isMemberOf(const SocketAddress &group) const`
- [ ] Source-Specific Multicast (SSM) support:
  - [ ] `Error joinSourceGroup(const SocketAddress &group, const SocketAddress &source, UdpSocket *socket)` — `IP_ADD_SOURCE_MEMBERSHIP`
  - [ ] `Error leaveSourceGroup(const SocketAddress &group, const SocketAddress &source, UdpSocket *socket)`
- [ ] `void setDefaultInterface(const String &interface)` — used when not specified per-join
- [ ] `PROMEKI_SIGNAL(groupJoined, SocketAddress)`
- [ ] `PROMEKI_SIGNAL(groupLeft, SocketAddress)`
- [ ] Internal: tracks socket-to-group mappings, handles cleanup on socket destruction
- [ ] Doctest: join/leave groups, activeGroups tracking, SSM join (if testable on loopback)
