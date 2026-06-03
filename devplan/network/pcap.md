# Packet Capture (pcap / pcapng) — reader, demux, ST 2110-40 decode

**Phase:** 3C / 4 follow-up (offline ingest front-end)
**Dependencies:** Phase 3A (UdpSocket / SocketAddress / NetworkAddress),
Phase 3C (RtpSession / RtpPayload / SdpSession / RtpMediaIO,
`RtpPayloadAnc::unpackAncPackets`), Phase 4 (MediaIO, ClockDomain,
MediaTimeStamp), `IODevice` / `FileIODevice` / `BufferView` /
`Buffer`, the `anc*` / `st291packet` / `anctranslator` decode chain.
**Library:** `promeki` (feature flag `PROMEKI_ENABLE_NETWORK`)

**Standards:** All code must follow `CODING_STANDARDS.md`. Every class
requires complete doctest unit tests. See `README.md` for full
requirements.

---

## Goal

A library-native packet-capture **reader** that ingests `.pcap` and
`.pcapng` files, demultiplexes Ethernet / IPv4 / IPv6 / UDP, and feeds
the resulting UDP payloads into the existing RTP machinery — with the
first concrete deliverable being offline decode of **SMPTE ST 2110-40**
ancillary data. Flows are **labeled from an ingested SDP** so a
multi-essence 2110 capture is sorted into video / audio / ANC streams
by `(dst multicast group, dst port)` with payload type and SSRC
cross-checked.

A capture **writer** is in scope as a later phase (debug capture of a
live `RtpSession`), but the reader and the demux are the priority. The
read path is where the value is: everything *downstream* of the UDP
payload already exists in the library.

### Design rule — demux is a library type, not a util

The Ethernet / IPv4 / IPv6 / UDP demultiplexer must land as a
first-class library type, **not** inside the CLI. It is equally
applicable to the existing live raw-socket path (`rawsocket.cpp`) and
to any future offline source. The `promeki-pcap` util is a thin driver
over library types only. (Foundation-first; library-first.)

### Two clocks, kept distinct

- **Capture wall-clock** — the per-packet timestamp from the pcap /
  pcapng record. Surfaced as a `DateTime` (system-clock / Unix epoch),
  **not** `TimeStamp` — `TimeStamp` is steady-clock-relative and would
  be the wrong scale for an absolute arrival time. The INT64_MIN-Invalid
  validity sentinel is honored (e.g. a pcapng Simple Packet Block, which
  carries no timestamp, yields an invalid `DateTime`). pcapng carries this at the
  resolution declared by the interface (`if_tsresol`), commonly
  nanoseconds; classic pcap is microsecond unless the
  `LINKTYPE`/magic indicates nanosecond.
- **RTP media clock** — the 90 kHz (video/ANC) or sample-rate (audio)
  timestamp in the RTP header. Already handled by `RtpStreamClock` /
  `RtpMediaClock`. Do **not** conflate the two; the capture clock is
  arrival time, the media clock is presentation time.

---

## Current state

Nothing pcap-specific exists in the tree (`grep -ri pcap
include/ src/` finds only a FreeType autofit header by coincidence).
The decode chain *below* the UDP payload, however, is complete:

- `RtpPacket` (`rtppacket.h`) — a `BufferView` with RFC 3550 header
  accessors; the buffer is the source of truth, no parsed-out state.
- `RtpPayloadAnc::unpackAncPackets(buf, out, policy)` — reassembles
  ST 2110-40 / ST 291 ANC packets from an RTP payload batch.
- `RtpAncDepacketizerThread` / `RxAncFrame` — marker-bit reassembly to
  a per-stream ANC frame bundle.
- `ancpacket` / `st291packet` / `anctranslator` / the `anccodec_*`
  family — decode of the actual ANC payloads (AFD, ATC, CEA-708,
  HDR 2094-40, OP-47, ST 2020 audio metadata, VPID, …).
- `SdpSession`, `MediaDesc::fromSdp(const SdpSession&)`,
  `AncDesc::fromSdp(const SdpMediaDescription&)` — SDP parse and
  per-essence descriptor extraction already exist for the live path.

So this plan adds a **container parser** plus an **L2/L3/L4 demux** and
the **flow-routing glue** that maps a demuxed UDP payload onto the
right existing depacketizer. It does not add a new ANC decoder.

---

## Architecture — three layers

```
  ┌─────────────────────────────────────────────────────────────┐
  │ promeki-pcap (util)        thin CLI: open → route → dump      │
  └─────────────────────────────────────────────────────────────┘
        ▲ (3) flow routing + SDP labeling → existing depacketizers
  ┌─────────────────────────────────────────────────────────────┐
  │ PcapFlowRouter / PcapSdpMap   (library)                       │
  │   key (dstIP, dstPort[, ssrc, pt]) → RtpPacket → unpackAnc…   │
  └─────────────────────────────────────────────────────────────┘
        ▲ (2) {src/dst addr, ports, BufferView udpPayload}
  ┌─────────────────────────────────────────────────────────────┐
  │ PacketDemux   (library, reusable by rawsocket.cpp too)        │
  │   Ethernet/VLAN → IPv4/IPv6 (+frag) → UDP                     │
  └─────────────────────────────────────────────────────────────┘
        ▲ (1) {TimeStamp captureTime, linkType, BufferView frame}
  ┌─────────────────────────────────────────────────────────────┐
  │ PcapReader   (library, over IODevice)                         │
  │   classic pcap + pcapng container framing, zero-copy          │
  └─────────────────────────────────────────────────────────────┘
```

### Zero-copy ingest

mmap (or whole-file read) the capture into a single `Buffer`; every
record/frame/payload handed upward is a `BufferView` into that one
backing buffer. No per-packet allocation. This matches the
BufferView-native design already committed to across the RTP stack and
matters because 2110 captures get large fast (a few seconds of 1080i
ANC + audio is already hundreds of MB).

---

## Phase P1 — container reader (`PcapReader`) — SHIPPED 2026-06-02

Landed: `enums_pcap.h` (`PcapLinkType` / `PcapFileFormat` /
`PcapByteOrder`), `pcapreader.h/cpp` (`PcapReader` + `PcapRecord`),
`Error::TruncatedData`, and `tests/unit/network/pcapreader.cpp`
(12 cases / 62 assertions, green). Capture time is surfaced as a
`DateTime` (Unix/system-clock), not `TimeStamp`. The whole capture is
read once into a single backing `Buffer`; `PcapRecord::frame` is a
zero-copy `BufferView` into it. Source must be sized + seekable
(file / in-memory buffer); the mmap and non-seekable/compressed
fallbacks remain the open decisions below.

**Required reading before executing:**
- libpcap `savefile` format (classic pcap): global header (magic
  `0xa1b2c3d4` µs / `0xa1b23c4d` ns, both byte orders), per-record
  header (ts_sec, ts_usec/nsec, incl_len, orig_len).
- pcapng (draft-tuexen-opsawg-pcapng / IETF): Section Header Block
  (SHB, `0x0A0D0D0A`, byte-order magic `0x1A2B3C4D`), Interface
  Description Block (IDB — `LinkType`, `SnapLen`, options incl.
  `if_tsresol`), Enhanced Packet Block (EPB — interface id, timestamp
  high/low, captured/original len, packet data, options). Simple
  Packet Block (SPB) as a fallback.
- `include/promeki/iodevice.h`, `fileiodevice.h`, `bufferview.h`,
  `buffer.h`, `timestamp.h` (validity sentinels).

**Scope:**
- `enums_pcap.h` — `TypedEnum<Self>` wrappers: `PcapLinkType`
  (EN10MB=1, RAW=101, LINUX_SLL=113, LINUX_SLL2=276, IPV4=228,
  IPV6=229, at minimum), `PcapFileFormat` (ClassicPcap / Pcapng),
  `PcapByteOrder`. Numeric values match the on-disk / IANA encodings;
  CamelCase identifiers, wire mapping in the reader. (Per the
  well-known-enums-in-`enums_<group>.h` convention.)
- `pcapreader.h/cpp` — `PcapReader` over an `IODevice`:
  - Auto-detects classic-pcap vs pcapng vs byte order from the leading
    magic. Both pcap timestamp resolutions (µs and ns).
  - pcapng: parse SHB + IDB(s) + EPB/SPB; honor per-interface
    `if_tsresol`; carry the interface's `LinkType` so a file mixing
    link types decodes correctly. Skip unknown block types by their
    `Total Length` field (forward-compatible). Tolerate multiple
    sections.
  - Iterator/pull API: `Result<PcapRecord> next()` returning
    `{ DateTime captureTime, PcapLinkType linkType, BufferView frame,
       uint32_t originalLength, bool snapTruncated }` — `frame` is a
    view into the backing `Buffer`, no copy.
  - Truncated-capture handling: a short final record returns a
    specific error code, not a hard failure (captures are often cut
    mid-write). Use a specific `Error` code, not a generic one.
- `bytereader`-style bounds checks: never read past `incl_len` /
  block length; reject `incl_len > orig_len`; cap snap length.

**Checklist:**
- [x] `enums_pcap.h` with `PcapLinkType` / `PcapFileFormat` / `PcapByteOrder`
- [x] `PcapReader` classic-pcap path (both byte orders, µs + ns)
- [x] `PcapReader` pcapng path (SHB / IDB / EPB / SPB, `if_tsresol`, multi-section)
- [x] Mixed-linktype-per-interface correctness (pcapng)
- [x] Zero-copy `BufferView` records over one backing `Buffer`
- [x] Truncated / malformed record → specific `Error` (`TruncatedData` / `CorruptData`), graceful stop
- [x] doctest: hand-built byte fixtures for every block type + both formats + both endians
- [ ] mmap-backed ingest (currently whole-file read into one `Buffer`)
- [ ] non-seekable / streamed source fallback (currently returns `NotSupported`)

---

## Phase P2 — link / network / transport demux (`PacketDemux`) — SHIPPED 2026-06-02

Landed: `packetdemux.h/cpp` (`PacketDemux`, `UdpDatagram`, `DemuxStatus`,
`DemuxResult`) and `tests/unit/network/packetdemux.cpp` (14 cases /
122 assertions, green). The demux is a standalone library type (reusable
by `rawsocket.cpp`), not a util-private helper. Output addresses are
`SocketAddress` (built from `Ipv4Address` / `Ipv6Address`); the
unfragmented UDP payload is a zero-copy `BufferView` into the capture,
while a reassembled datagram's payload views a private owned buffer.
Fragment reassembly is full (IPv4 + IPv6), bounded by count
(`MaxInFlight`, LRU eviction) rather than a wall clock, since offline
captures have no real-time aging signal — this resolves the "full vs
flag-only" open decision in favour of full. Snap-clipped datagrams and
incomplete fragments are surfaced (`Truncated` / `Fragment`), never
silently treated as whole.

**Required reading before executing:**
- Ethernet II + 802.1Q / 802.1ad VLAN tag stacking; Linux cooked
  capture SLL (113) and SLL2 (276) headers.
- IPv4 header (IHL, options, fragmentation: MF flag + frag offset),
  IPv6 base header + the handful of extension headers that can precede
  UDP (Hop-by-Hop, Routing, Fragment, Destination Options).
- UDP header; UDP-lite is out of scope.
- `socketaddress.h` (`SocketAddress::fromSockAddr` patterns),
  `networkaddress.h`, `ipv4address.h` / `ipv6address.h`.

**Scope:**
- `packetdemux.h/cpp` — `PacketDemux`: given `(PcapLinkType, BufferView
  frame)` → `Result<UdpDatagram>` where `UdpDatagram` =
  `{ SocketAddress src, SocketAddress dst, BufferView payload,
     uint8_t ipProto, bool truncated }`.
  - EN10MB with 0/1/2 stacked VLAN tags; SLL / SLL2; RAW / IPV4 / IPV6
    link types (header-less L3).
  - IPv4 and IPv6, including extension-header walk to reach UDP.
  - **Fragment reassembly** behind a small reassembly cache keyed on
    `(src, dst, ipId, proto)` with a bounded LRU + timeout. Jumbo and
    fragmented ANC do occur; a non-reassembling demux silently drops
    them. If reassembly is disabled or a fragment train is incomplete,
    surface it (don't pretend the datagram is whole).
  - Non-UDP (TCP/ICMP/etc.) → a distinct "not UDP" result the caller
    can cheaply skip, not an error.
- Reuse this from `rawsocket.cpp` live capture in a follow-up (note it,
  don't necessarily wire it in this phase).

**Checklist:**
- [x] Ethernet II + single + double VLAN
- [x] SLL / SLL2 cooked headers
- [x] RAW / IPV4 / IPV6 header-less link types (+ NULL / LOOP via IP version nibble)
- [x] IPv4 parse + options (IHL) + fragment reassembly (bounded cache)
- [x] IPv6 parse + extension-header walk + fragment ext header
- [x] UDP extract → `UdpDatagram` with `SocketAddress` src/dst
- [x] Non-UDP fast-skip path (`DemuxStatus::NotUdp`)
- [x] doctest: per-encapsulation fixtures incl. in-order + out-of-order fragmented datagrams
- [ ] wire `PacketDemux` into `rawsocket.cpp` live capture (follow-up; demux is ready)

---

## Phase P3 — flow routing + SDP labeling (`PcapFlowRouter`) — SHIPPED 2026-06-02

Landed: `PcapFlowKind` (added to `enums_pcap.h`), `pcapsdpmap.h/cpp`
(`PcapSdpMap` + `PcapFlow`), `pcapflowrouter.h/cpp` (`PcapFlowRouter`,
`RoutedAncFrame`, `FlowStat`), and tests
`tests/unit/network/pcapsdpmap.cpp` (5 cases) +
`pcapflowrouter.cpp` (4 cases, incl. a full
ANC→pack→RTP→UDP→IP→Eth→pcap→router→unpack round-trip). The router
reuses the existing `RtpPayloadAnc::unpackAncPackets` and emits the
existing `RxAncFrame` bundle (wrapped with pcap context: src/dst
`SocketAddress`, SSRC, capture `DateTime`). SDP labeling is primary
(`PcapSdpMap` keyed on `(NetworkAddress, port)`, IPv4 + IPv6, TTL
suffix stripped, `smpte291` → ANC); auto-discovery `FlowStat`s are the
no-SDP fallback. PT-mismatch and SSRC/timestamp-change handling
implemented; audio/video dispatch seam present but not yet decoding.

**Required reading before executing:**
- `sdpsession.h`, `mediadesc.h` (`MediaDesc::fromSdp`), `ancdesc.h`
  (`AncDesc::fromSdp`), `audiodesc.h` / `imagedesc.h` `fromSdp`.
- `rtppacket.h`, `rtppayloadanc.h`
  (`RtpPayloadAnc::unpackAncPackets`), `rtpancdepacketizerthread.h`
  (`RxAncFrame`, `RtpAncDepacketizerContext`), `anctranslator.h`.
- RFC 4566 / 8866 SDP, ST 2110-10 §7 (the `c=`/`m=` multicast group +
  port that identify a flow), the `a=fmtp` DID/SDID list for -40.

**Scope:**
- `pcapsdpmap.h/cpp` — `PcapSdpMap`: ingest an `SdpSession` and build a
  routing table keyed on `(dst multicast IP, dst port)` → essence
  descriptor (`MediaDesc` + the per-essence `AncDesc` / `AudioDesc` /
  `ImageDesc` and expected payload type). This is the **SDP labeling**:
  the capture's flows are named and typed from the SDP rather than
  guessed.
- `pcapflowrouter.h/cpp` — `PcapFlowRouter`: drives
  `PcapReader` → `PacketDemux` → for each `UdpDatagram` whose
  `(dst, port)` matches a mapped flow, wrap `payload` as an `RtpPacket`,
  validate PT/SSRC against the SDP-derived expectation, and dispatch:
  - ANC flow → accumulate across marker bits, call
    `RtpPayloadAnc::unpackAncPackets`, emit `RxAncFrame` (the same
    bundle `RtpAncDepacketizerThread` produces — reuse, don't
    reimplement).
  - Audio / video flows → routed to their existing payload handlers in
    a later phase (out of scope for the -40 deliverable, but the
    routing key and dispatch seam must exist now so they slot in).
  - **SSRC change / sequence discontinuity** handling consistent with
    the live depacketizer's reset-epoch behavior.
- **Auto-discovery fallback** (no SDP): list observed
  `(dst, port, ssrc, pt)` flows with packet/byte counts so a user can
  decode a capture they have no SDP for. SDP labeling is the primary
  path; discovery is the fallback.

**Checklist:**
- [x] `PcapSdpMap` builds (dst,port)→essence table from `SdpSession`
- [x] `PcapFlowRouter` reader→demux→RtpPacket pipeline
- [x] PT validation against SDP expectation; SSRC-change reset
- [x] ANC flow → `unpackAncPackets` → `RxAncFrame` (reusing existing path)
- [x] Marker-bit reassembly + timestamp-change flush + EOF flush
- [x] Dispatch seam for audio/video flows (recognised + tallied, not yet decoded)
- [x] No-SDP auto-discovery flow listing (`FlowStat`)
- [x] doctest: synthetic 2110-40 capture fixture + SDP → decoded ANC (full round-trip)
- [ ] audio (-30/-31) and video (-20/-22) offline decode at the dispatch seam (follow-up)

---

## Phase P4 — `promeki-pcap` CLI — SHIPPED 2026-06-02

Landed: `utils/promeki-pcap/` (`main.cpp` + `CMakeLists.txt`), registered
in `utils/CMakeLists.txt` under `PROMEKI_ENABLE_NETWORK AND
PROMEKI_ENABLE_PROAV`. Thin `CmdLineParser`-driven front end over
`PcapReader` / `PacketDemux` / `PcapFlowRouter`. Subcommands `info`
(container summary), `flows` (auto-discovered or SDP-labelled flow
table), `anc` (per-frame ANC decode, text or `--json`). `--sdp` loads an
SDP via `SdpSession::fromFile`. When no SDP is available, a flow can be
designated ANC directly via the repeatable `--anc <host:port>` option
(backed by the library `PcapSdpMap::addAncFlow` / `PcapFlowRouter::addAncFlow`,
which append a manual `PcapFlowKind::Anc` flow that accepts any payload
type); `anc` requires `--sdp` or at least one `--anc`. Semantic decode is surfaced through a new library helper
`AncTranslator::describe(const AncPacket&)` (`parse` → `Variant::toString`;
unit-tested), so decoded ATC timecode / AFD / ST 2020 audio etc. render
inline. The `anc` subcommand also supports a `--type <name>` format
filter, an optional `/pt` pin on `--anc`, and full-nanosecond capture
timestamps. Smoke-tested against a synthetic capture: `info` / `flows` /
`flows --sdp` / `flows --anc` (kind labelled) / `anc` (via `--sdp` and via
`--anc`, with `--type` and `/pt`) text + JSON all verified; the full
valid-ANC decode is covered by the P3 router unit tests (both
SDP-labelled and manual-flow paths).

**Scope:** thin driver over the library types only (matches the
`promeki-2110-calc` / `promeki-fetch-model` util convention; lives in
`utils/promeki-pcap/`, registered in `utils/CMakeLists.txt`).

- `promeki-pcap info <file>` — container summary: format, sections,
  interfaces/link types, packet + byte counts, capture time span.
- `promeki-pcap flows <file>` — auto-discovered `(dst, port, ssrc, pt)`
  flow table.
- `promeki-pcap anc <file> --sdp <file>` — decode ST 2110-40 ANC and
  emit human-readable + JSON (via `anctranslator` + the JSON object
  family) per ANC frame: DID/SDID, line/horizontal offset, decoded
  payload (ATC timecode, AFD, CEA-708, etc.).
- No hardcoded paths; scratch under `/mnt/data/tmp/promeki/` if needed;
  use `Dir` statics.

**Checklist:**
- [x] `utils/promeki-pcap/` + CMake registration
- [x] `info` / `flows` / `anc` subcommands
- [x] JSON + text ANC output (DID/SDID/line/offset/format via `St291Packet::from` + `AncFormat::fromSt291DidSdid`)
- [x] semantic per-packet decode via `AncTranslator::describe` (new library one-call helper: `parse` → `Variant::toString`; renders ATC timecode, AFD, CEA-708 CDP, VPID, ST 2020 audio, etc.); shown inline (`=> ...`) in text and as `"decoded"` in JSON
- [x] `AncTranslator::describe` reports *why* a packet did not decode via an `Error*` out-param; the CLI shows `<undecoded: ...>` (text) / `"decodeError"` (JSON) so missing context is visible instead of silently dropped
- [x] generic parser-context injection: `--cfg <Key:Value>` sets any `AncTranslateConfig` key (parsed against its registered `VariantSpec`); e.g. `--cfg AtcParseRateHint:30` lets the ATC timecode parser run (verified end-to-end on a real 40_TC_Stream.pcap)
- [x] `--cfg list` enumerates every `AncTranslateConfig` key (type, default, description via `writeSpecMapHelp`) plus the allowed values for enum-typed keys (`Enum::values`)
- [ ] richer per-type detail — VPID field decode (currently 4-byte hex), CEA-608 parser (currently `Not Supported`), and fuller ATC userbits/flags; possibly a multiline `StringList describe()` on the ANC value types — follow-up
- [x] `--anc <host:port[/pt]>` manual ANC-flow designation with optional PT pin
- [x] `--type <name>` filter to dump only specific ANC formats (e.g. `Atc`, `Cea708`)
- [x] full-nanosecond capture timestamps in `info` and `anc` (floored, no `to_time_t` rounding); RTP timestamp shown decimal + hex
- [x] running frame id (`#N`) at the start of each ANC header line (and `"index"` in JSON), counting only emitted frames so `--type`-filtered output has no gaps — makes a specific frame easy to cite
- [x] inter-frame deltas on the ANC header line (per-SSRC): wall-clock gap since the previous frame **and** the RTP-timestamp advance translated to time via the 90 kHz ANC clock (`RtpPayloadAnc::ClockRate`), both pretty-printed with `Duration::toScaledString` (e.g. `+16.67 ms`); first frame shows `+n/a`.  Exposed as `captureDelta` / `rtpDelta` in JSON.  Lets the two clocks be timed up against each other at a glance
- [ ] `promeki-test` functional suite: known capture → expected ANC decode
      (needs a committed real-ANC `.pcap` fixture; router decode already
      unit-tested in-memory) — follow-up

---

## Phase P5 — writer (`PcapWriter`) — deferred

**Scope (future):** debug capture of a live `RtpSession` / raw-socket
flow to disk.

- `pcapwriter.h/cpp` — emit **classic pcap** first (simplest;
  µs or ns), pcapng later if interface metadata / per-packet options
  are wanted. Take `RtpPacket` / `BufferView` payloads and synthesize
  the UDP/IP/Ethernet headers (or write `LINKTYPE_RAW` L3 to skip
  Ethernet). Capture wall-clock from the system clock domain.
- Round-trip test: `PcapWriter` → `PcapReader` → identical records.

**Checklist:**
- [ ] `PcapWriter` classic-pcap output
- [ ] Header synthesis (Ethernet/IP/UDP or RAW L3)
- [ ] Round-trip doctest against `PcapReader`
- [ ] Optional: live `RtpSession` tap

---

## Open decisions

- ~~**Fragment reassembly scope in P2**~~ — RESOLVED: full bounded
  reassembly (IPv4 + IPv6), count-bounded LRU, no wall-clock aging in
  offline mode. Shipped in P2.
- **mmap vs streamed read** — mmap is the zero-copy default; a streamed
  fallback may be needed for non-seekable / compressed (`.pcapng.gz`,
  `.pcap.zst`) sources. Decompression is out of scope for v1 (user can
  pre-decompress); note it.
- **Where audio/video decode lands** — P3 builds the dispatch seam; the
  actual audio (-30/-31) and video (-20/-22) offline decode is a
  separate follow-up that reuses the same router.

---

## Non-goals (v1)

- Live capture from a NIC (that's `rawsocket.cpp`'s domain; the demux
  is shared, the capture source is not pcap).
- Compressed capture containers (`.gz` / `.zst`).
- TCP reassembly, UDP-lite, non-RTP UDP payload dissection beyond the
  flow table.
- Capture writer beyond classic pcap (P5).
