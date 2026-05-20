# Ancillary-Data Framework — SMPTE Conformance Audit

**Library:** `promeki` (proav + network)
**Companion docs:** `devplan/proav/ancdata.md` (the originating stack),
`docs/anc.md` (authored 2026-05-19, references this audit).
**Started:** 2026-05-19
**Status:** **F1–F10 landed.** All audit findings have a final
disposition; the fundamental-layer (Phase A + B + C) wire bugs are
fixed, every codec deep-audit (D4c HDR-static, D5c HDR-dynamic
2094-40, D9 OP-47 SDP, D8 ST 2020-2 Method A, D6 VPID) has been
byte-for-byte verified, and the registry / docs work has landed.
F10 Method B (ST 2020-3) and the OP-47 Multipacket variant
(RDD 8 §6) remain out of scope at this layer pending real
production demand.
All fundamental-layer standards are in hand; the [NEEDS-STD] markers
from the first pass have been resolved.  F1 (comment / doc /
reserved-input tightening), F2 (CEA-708 CDP wire-format correctness),
F3 (RFC 8331 / ST 2110-40 correctness), F4 (ST 2016-3 AFD/Bar Data
+ ST 2016-4 Pan-Scan correction), F5 (ATC value type + HFR
alternation), F6 (Type-1 packet support + reserved validation), F7
(checksum policy wiring), and F8 (registry / category housekeeping)
are all committed (2026-05-19).  F9 item 1 (A8/E3) landed the five
ST 291 framing fields as direct `AncPacket` accessors; F9 item 2
(E4 — indexed registry views) and item 3 (E7 — `docs/anc.md`) also
landed in the same F6–F9 commit.  F10 codec deep audits: D4c
(HDR-static SEI) and D5c (HDR-dynamic 2094-40 KLV) verified
byte-for-byte on 2026-05-20; D8 (ST 2020 Dolby audio metadata),
D9 (RDD 8 OP-47 SDP), and D6 (VPID) codecs landed 2026-05-20.

This is a *standards audit* of the existing ancillary-data stack — the
generic `AncFormat` / `AncTransport` / `AncPacket` carrier and the
`St291Packet` / `RtpPayloadAnc` ST 291 + RFC 8331 layer that every
codec sits on. We walk each fundamental piece against the relevant
SMPTE document, then each codec against its application document,
and log every divergence (real bug, latent gap, missing feature,
doc-only slip). The goal at the end of the audit is to **be
confident the fundamental layer is bit-exact correct and complete**
before any new codec work lands on top of it.

Each finding has a severity tag:

- **bug** — a real, demonstrable wire-format error or contract break.
- **latent** — incorrect for a case the library does not currently
  exercise (e.g. Type-1 ST 291 packets, composite SDI) but would
  silently produce wrong bytes the moment that case appears.
- **gap** — missing feature or coverage; not a wire violation but
  limits round-trip fidelity.
- **perf** — hot-path inefficiency worth a profile-driven look.
- **doc** — wire layer is correct, header / inline docs say
  something inaccurate or misleading.
- **policy** — behaviour is a deliberate choice but a defensible
  alternative exists worth confirming.

## Standards used in this audit

All present in `~/docs/smpte` as of 2026-05-19:

| Doc | Coverage |
|---|---|
| ST 291-1:2011 | ANC packet & space formatting (foundation) |
| RP 291-2:2013 | DID/SDID assignments + ANC space use |
| ST 12-1:2014  | Time-and-control code |
| ST 12-2:2014  | ATC — Transmission of timecode in ANC space |
| ST 12-3:2016  | ATC for HFR (>30 fps progressive) |
| ST 334-1:2015 | CEA-608 carriage in HD VANC |
| ST 334-2:2015 | Caption Distribution Packet (CDP) for CEA-708 |
| ST 352:2013   | Video Payload Identifier (VPID) |
| ST 2010:2008  | SCTE-104 messages in ANC |
| ST 2016-1:2009 | AFD code definitions and Bar Data |
| ST 2016-3:2009 | VANC mapping of AFD and Bar Data |
| ST 2016-4:2014 | VANC mapping of Pan-Scan info |
| ST 2020-1:2014 | Dolby audio metadata in ANC (family) |
| ST 2031:2015  | Carriage of DVB / SCTE VBI data |
| ST 2086:2014  | Mastering Display Color Volume metadata (referenced by 2108-1) |
| ST 2094-* (1, 10, 20, 30, 40, 60) | Dynamic HDR metadata families |
| ST 2106:2016  | Closed-caption flag (CCF) ANC packet |
| ST 2108-1:2018 | HDR/WCG SEI-style metadata ANC packet |
| ST 2108-2:2019 | HDR/WCG KLV-style metadata ANC packet |
| ST 2110-10/-20/-21/-40 | Pro-media over managed IP networks |
| RFC 8331      | RTP payload for SMPTE ST 291 ANC |
| RDD 8:2008    | OP-47 / Subtitling Distribution Packet (SDP) |

## Status at a glance

| Phase | What | Status |
|------:|------|--------|
| A | Audit `AncFormat` / `AncPacket` / `AncMeta` / `AncDesc` / `AncPayload` against ST 291-1 + RP 291-2 | **Catalogued** |
| B | Audit `St291Packet` build/parse path against ST 291-1 §5–§9 | **Catalogued** |
| C | Audit `RtpPayloadAnc` against RFC 8331 + ST 2110-40 | **Catalogued** |
| D | Audit per-codec wire formats: ATC, AFD, CEA-708 CDP, HDR static, HDR dynamic, VPID, SCTE-104, ST 2020 audio metadata | **Catalogued** (HDR dynamic KLV verified at the SDID + Packet-Count level; full KLV byte audit deferred) |
| E | Cross-cutting concerns (checksum policy wiring, protected-codes guarding, hot-path metadata access) | **Catalogued** |
| F | Fix plan: order, dependencies, test additions | **F1 + F2 + F3 + F4 + F5 + F6 + F7 + F8 + F9 + F10 landed** (F1–F8: 2026-05-19; F9 closed in same pass; F10/D4c + F10/D5c verified 2026-05-20; F10/D9 OP-47 SDP + F10/D8 ST 2020 codecs + F10/D6 VPID codec landed 2026-05-20) |
| G | Codec-level deep audits (per-spec byte position checklist for ST 2108-2 KLV multi-packet path, RDD 8, ST 2020 sub-flavours) | **D4c + D5c + D9 (OP-47 SDP) + D8 (ST 2020-2 Method A) + D6 (VPID) verified (2026-05-20)**; ST 2020-3 Method B + OP-47 Multipacket remain pending pending production demand |

# Phase A — Fundamental layer findings

### A1. `St291Packet` cannot represent **Type-1** ST 291 packets.
**Severity:** *latent*

ST 291-1 §5.1 + RFC 8331 §2.2 (explicit note: "in a Type 1 ANC
data packet, this word will actually carry the data block number
(DBN)") define two packet shapes:

- **Type 1** (DID b7 = 1, DID ≥ 0x80): single-word DID, then a
  **Data Block Number (DBN)** word, then DC, UDW…, CS.
- **Type 2** (DID b7 = 0, DID ≤ 0x7F): DID + **SDID** + DC + UDW… + CS.

`St291Packet`:

- `buildRaw(did, sdid, …)` always writes a Type-2 layout — there
  is no way to specify a DBN.
- `sdid()` reads word index 1 unconditionally; for a Type-1 packet
  this returns the **DBN byte mis-labelled as SDID**.
- `isType1()` returns the high bit of DID but no API path acts on
  it.

Every well-known `AncFormat` currently registered is Type 2 (DID
values 0x41, 0x43, 0x44, 0x45, 0x60, 0x61). DID 0x80 (Packet
Marked for Deletion, ST 291-1 §6.3) is the only common Type-1.
Wire bytes are wrong the moment we register a Type-1 format or a
user does.

### A2. Protected codes are not enforced on the pass-through parity path.
**Severity:** *bug*

ST 291-1 §9.1: 10-bit word values `0x000`, `0x001`, `0x002`,
`0x003`, `0x3FC`, `0x3FD`, `0x3FE`, `0x3FF` shall not appear inside
any ANC packet payload. The parity-computing path
(`makeWord(in)` with bits 8–9 == 0) is safe by construction.

The **pass-through path** (`makeWord(in)` with bits 8–9 non-zero)
honours caller-supplied 10-bit words verbatim and does not reject
values in the protected ranges.

### A3. ADF (Ancillary Data Flag) handling is implicit but undocumented.
**Severity:** *doc*

ST 291-1 §5.2 defines the ADF as `000h 3FFh 3FFh` (component) or
`3FCh` (composite). RFC 8331 §1 explicitly says "the Ancillary
Data Flag (ADF) word is not specifically carried in this RTP
payload". The library deliberately stores packets in post-ADF
form (DID is byte 0) which is correct for both ST 291 in-memory
and RFC 8331. The contract just isn't called out anywhere.

### A4. Composite ancillary packets are not modelled.
**Severity:** *gap*

ST 291-1 §5.3 / §7.2 defines composite-domain ANC packets with
ADF 3FCh. No modern equipment in our pipeline ingests composite
SDI. Document the decision deliberately rather than leaving it
silent.

### A5. Packet-for-deletion (DID=0x80) is not modelled.
**Severity:** *gap* (low priority)

ST 291-1 §6.3 / §7.3 defines in-band packet deletion. No backend
the library is paired with performs this. Acknowledged gap.

### A6. DC is not validated against 255-word cap.
**Severity:** *latent bug*

ST 291-1 §6.5: DataCount range is 0..255. `St291Packet::build()`
uses `udw.size()` directly with no bounds check; a caller passing
300 UDWs silently emits DC=0x2C, leaving the buffer internally
inconsistent.

### A7. Format registry: SDID 0x00 should not appear in SDP emission.
**Severity:** *bug*

ST 291-1 §6.2 says SDID 00h is reserved.
RFC 8331 §3.1 actually contradicts ST 291 here: it explicitly
says "Type 1 ANC data packets (which do not have SDIDs defined)
SHALL be labelled with SDID=0x00." So 0x00 has *two* meanings in
SDP fmtp depending on whether the underlying packet is Type-1 or
not.

The library uses SDID=0x00 internally as a **wildcard sentinel**
on `Smpte2020Audio` (which spans SDIDs 0x01–0x09 under DID 0x45).
The SDP generator emits `DID_SDID={0x45,0x00}` — which a
conforming receiver will read as either "Type-1 packet with DID
0x45" (RFC 8331) or "reserved" (ST 291), neither of which is
what the library intends.

**Fix shape:** add a `st291ConcreteSdids()` accessor (defaults to
`{st291Sdid}` for non-wildcard formats; an explicit list of
0x01-0x09 for SMPTE 2020). Have the SDP generator iterate the
concrete-SDID list.

### A8. ~~`AncPacket` metadata access on the RTP hot path is a hash lookup.~~ **Partially resolved (F9.1, 2026-05-19).**
**Severity:** *perf*

`RtpPayloadAnc::packAncFrame` reads 5 framing fields per packet
from `Metadata`. At ~30 packets per HD-60 frame that is ~9000
hashtable lookups per second; 4K production graphs double it.
Profile-driven decision; convert to direct `Impl` fields if
warranted.

**Resolution:** the five fields (Line, HOffset, FieldB, CBit,
StreamNum) are now direct accessors on `AncPacket` (landed
2026-05-19 as part of the F9 hot-path pass); the `AncMeta::St291`
Metadata namespace is removed.  RTP pack / unpack now reads these
fields with O(1) struct access.  Remaining: a profile run on a
4K60 stream to quantify the gain and confirm E4 (indexed category
views) is the next worthy target.

### A9. `AncCategory` lacks Subtitles / Klv / Sei / Vbi.
**Severity:** *gap*

Categories we want soon based on the standards now in hand:

- **Subtitles** — RDD 8 / OP-47 carries subtitling distinct from
  Captions.
- **Klv** — ST 2108-2 / MISB 0601 / generic ST 336 KLV.
- **Sei** — H.264/HEVC SEI distinguished from Captions/HDR.
- **Vbi** — ST 2031 VBI carriage.

### A10. `AncFormat` registry is missing formats verifiable from the new standards.
**Severity:** *gap*

Verified additions based on the now-present standards (DID / SDID
in hex):

| Format | DID | SDID | Doc | Notes |
|---|---|---|---|---|
| OP-47 SDP (Subtitling) | 0x43 | 0x02 | RDD 8 §6 | Multi-packet subtitling distribution |
| OP-47 multipack header | 0x43 | 0x01 | RDD 8 | Companion of SDP |
| ST 2106 CCF (caption flag) | 0x41 | 0x14 | ST 2106 §5 | Need to verify from ST 2106 PDF, present |
| Pan-Scan VANC | 0x41 | 0x06 | ST 2016-4 | **Currently mis-registered as `BarData` (see D2a)** |
| ST 2031 SDID-1 (NTSC) | 0x60 | 0x01 | ST 2031 | Line-21 / VBI carriage |
| HdrDynamic2094-10 SEI in ST 2108-1 | 0x41 | 0x0C | ST 2108-1 §5.3.4 | Same SDID as HdrStatic; Frame Type 2 |

The Pan-Scan registration mistake is structural and lands as a
codec finding (D2a).

### A11. Doxygen on `St291Packet::checksum*()` describes the wrong bit layout.
**Severity:** *doc*

`include/promeki/st291packet.h:45-51` says the checksum word has
"bit 8 = even parity over bits 0-7". ST 291-1 §6.7 (and RFC 8331
§2.2): the checksum is a **9-bit value** (bits 0–8) plus bit 9 =
NOT bit 8 — no independent parity. Implementation is correct;
header doc isn't.

# Phase B — ST 291 packet layer

### B1. `udw()` accessor cannot signal truncation.
**Severity:** *gap*

`St291Packet::from()` only validates `data.size() >= 5`, not that
the buffer matches the declared DC. `udw()` returns a shorter list
than DC implies when the wire is truncated, with no error signal.

### B2. CRC is not validated on parse / capture.
**Severity:** *bug* (RFC 8331 §7 explicitly recommends it)

`AncChecksumPolicy` is defined in `enums.h` (`PreserveOrRecompute`,
`AlwaysRecompute`, `StrictValidate`) but no code consults it.
RFC 8331 §7 says receivers SHOULD check Data_Count and
Checksum_Word. The library currently does neither.

### B3. `buildRaw` does not validate DID/SDID against reserved ranges.
**Severity:** *latent bug*

ST 291-1 §6.1 / §6.2 / Figure 4: DIDs 0x00–0x03 are reserved,
SDID 0x00 is reserved, etc. Today every registered AncFormat is
in range; user-defined formats can pass anything.

### B4. `St291Packet` lacks raw 10-bit-word access.
**Severity:** *gap*

`udw()` strips parity. For byte-exact replay verification we want
`udwRaw()` returning full 10-bit words.

### B5. ANC builder helpers don't expose the C-bit / Y-vs-C placement.
**Severity:** *latent gap*

ST 12-2 §8.2.1 (HD ATC must live in Y), ST 2016-3 §5 ("when
carried in a high definition signal, they shall be carried in the
Y stream"), ST 2108-1 §5 — all spec the data stream selection.
Library codecs hardcode `cBit=false` (Y stream). When a future
codec targets C, the bit must be plumbed via
`AncTranslateConfig::St291BuildCBit`.

# Phase C — RFC 8331 / ST 2110-40 carriage

### C1. F-bit value 0b01 receiver behaviour.
**Severity:** *bug* — RFC 8331 §2.1 violation

RFC 8331 §2.1: "The value 0b01 is not valid. Receivers SHOULD
ignore an ANC data packet with an F field value of 0b01 and
SHOULD process any other ANC data packets with valid F field
values that are present in the RTP payload."

`unpackAncPackets` reads F unconditionally and stamps it on every
record. A datagram with F=0b01 is decoded, not ignored.

**Fix shape:** add a `FieldIndication::Invalid = 0x1` enumerator
and `Reserved = 0x01` discriminator; reject (return without
appending records) when F=0b01.

### C2. ANC_Count = 0 keep-alive packets are dropped.
**Severity:** *bug* — RFC 8331 §2.1 / ST 2110-40 §5.5 violation

RFC 8331 §2.1: "ANC_Count of 0 indicates that there are no ANC
data packets in the payload (for example, an RTP packet that
carries no actual ANC data packets even though its marker bit
indicates the last ANC data RTP packet in a field/frame). If the
ANC_Count is 0, the Length will also be 0."

ST 2110-40 §5.5: "Senders shall send at least one RTP packet
corresponding to each video field, frame, or segment. In the
event that no ANC packets are transmitted in relation to a frame
(or interlaced field or PsF segment) of video, an RTP packet with
an ANC_Count value of zero and a Marker bit set shall be
transmitted."

Library `validate()` returns `DropSilently` for ANC_Count=0; the
packer's `packAncFrame()` returns an empty list for empty input.
**Two bugs in opposite directions:**

- Receive side: we drop the receiver's keep-alive marker so the
  consumer never sees end-of-frame for empty ANC frames.
- Send side: we don't emit the ST 2110-40 §5.5 mandatory keep-alive
  for empty frames.

**Fix shape:** rework `validate()` to accept ANC_Count=0 with
Length=0; rework `packAncFrame()` to emit a single keep-alive
RTP packet (ANC_Count=0, Length=0, Marker=1, F per session) when
the input list is empty.

### C3. ~~Greedy-fit planner caps at 254 records.~~ **Re-verified during F1 — code is correct.**
**Severity:** ~~bug~~ — **withdrawn**

The audit's original finding misread the increment order in
`planRtpPackets`. Trace: when `cur.count == 255`, the check
`cur.count >= 0xFFu` fires *before* adding the next record, so
the flush emits a 255-record plan and the next record starts a
fresh plan. Max-per-RTP is 255, matching RFC 8331 §2.1.

No code change in F1.

### C4. Length=0 / Word_align contract when ANC_Count=0.
**Severity:** *latent bug* (only matters once C2 is fixed)

RFC 8331 §2.2: "Word align SHALL NOT be used if there are zero
ANC data packets being carried in the RTP packet." Plus
ANC_Count=0 ⇒ Length=0. The packer must take the keep-alive
shape exactly.

### C5. Trailing-pad overrun is silently accepted on receive.
**Severity:** *latent bug*

`rtppayloadanc.cpp:330-338`: when the padded record extends past
the declared Length, the depacker keeps the record and treats the
overshoot as padding. RFC 8331 §2.2 + §2.1 imply the declared
Length already covers all word-align padding; an overshoot is a
malformed payload. Should warn / reject under StrictValidate.

### C6. RTP marker bit and payload-type are not validated on receive.
**Severity:** *gap*

Defence-in-depth; RTP demux should already filter SSRC + PT.

### C7. Line_Number / Horizontal_Offset sentinel values are not modelled.
**Severity:** *bug* (interop)

RFC 8331 §2.2:

| Line_Number | Meaning |
|---|---|
| 0x7FF | Without specific line location |
| 0x7FE | "On any line in the range from the second line after the line specified for switching to the last line before active video, inclusive" |
| 0x7FD | Line number larger than 11 bits |

| Horizontal_Offset | Meaning |
|---|---|
| 0xFFF | Without specific horizontal location |
| 0xFFE | Within HANC space |
| 0xFFD | Within active-video region (SAV-EAV) |
| 0xFFC | Larger than 12 bits |

Library only knows `UnspecifiedHOffset = 0xFFF`. The default
value of `AncMeta::St291::Line` is `0` — which is **a valid line
number** on some interfaces, not a "no specific line" sentinel.
Per ST 2110-40 §5.2.2 the special Line_Number value 0x7FE is the
recommended default for ANC inserted without exact line knowledge
("two lines after the switching point" — RP 168). Our default
should be `0x7FE`, not `0`.

**Fix shape:** add the sentinels as constants on `St291Packet`
(or `AncMeta::St291::Line`) and change the metadata default to
0x7FE.

### C8. SDP `m=` line uses the wrong media name.
**Severity:** *bug*

RFC 8331 §3.1: `m=video <port> RTP/AVP <pt>` with
`a=rtpmap:<pt> smpte291/<rate>`. The library's `AncDesc::toSdp`
docstring says `m=application` (and presumably the implementation
matches the doc). RFC 8331 §4 example is `m=video 30000 RTP/AVP
112`.

### C9. SDP fmtp parameters under ST 2110-40 are missing.
**Severity:** *gap* — ST 2110-40 §7 non-compliance

ST 2110-40 §7 mandates several fmtp parameters that the library's
SDP emit doesn't include:

- **SSN** — `ST2110-40:2018` or `ST2110-40:2023` (signals the
  standard version).
- **TM** — `CTM` (Compatible Transmission Model) or `LLTM`
  (Low-Latency Transmission Model). Receivers default to CTM
  when absent.
- **exactframerate** — required so ANC timestamps can be aligned
  to video frame timing.
- **TROFF** — required when sender uses a non-default TR offset.
- **VPID_Code** (from RFC 8331 §3.1) — single integer; the byte
  1 of the VPID per ST 352.
- **DID_SDID** — already emitted, but see A7 for wildcard SDID
  handling and the Type-1 SDID=0x00 collision.

### C10. ST 2110-40 §5.2.1 forbids audio + EDH packets on this transport.
**Severity:** *gap*

"While embedded audio packets (including audio control packets)
are valid SMPTE ST 291-1 ANC packets, they should not be
transmitted using this method. Similarly, EDH packets should not
be transported using this mechanism."

Library does no filtering. The egress path should drop
`AncCategory::AudioMetadata` and EDH-flavoured packets (or at
minimum log once) when destined for `AncTransport::St291` over
RTP.

### C11. Sender-side ordering by SDI location.
**Severity:** *gap*

ST 2110-40 §5.2.2: "Senders which signal proposed SDI location
information shall ensure that the proposed locations of each ANC
data packet are in increasing order as the ANC data packets within
each frame are transmitted."

`packAncFrame()` walks `packets` in input order without sorting
by `(Line_Number, Horizontal_Offset)`. When SDI location is
signalled exactly (not 0x7FE / 0xFFF), this is a §5.2.2
violation.

### C12. F-bit asymmetry across pack/unpack.
**Severity:** *policy*

Packer infers `InterlacedField2` only when *every* St291 packet in
the datagram has `FieldB=true`; otherwise `Progressive`. Depacker
stamps the same FieldB on *every* record from the payload's F.
Mixed-field datagrams (technically permissible) silently lose
per-record field info on round trip. RFC 8331 doesn't forbid this
asymmetry but it is non-obvious.

### C13. ESN (Extended Sequence Number) is hard-coded to zero.
**Severity:** *gap* (deferred in `ancdata.md` Phase 1.5)

RFC 8331 §2.1: ESN is the high 16 bits of a 32-bit packet
sequence; receivers may use it to detect reorder across the 16-bit
RTP sequence wrap. Not yet wired; flagged deferral.

### C14. RTP clock rate.
**Severity:** *latent* (correct for ST 2110-40, soft for general RFC 8331)

ST 2110-40 §5.3: "The RTP Clock rate shall be 90 kHz." Library
hardcodes 90000. ✓ Correct for ST 2110-40.

RFC 8331 §3.1 (general profile): "When an ANC data RTP stream is
to be associated with an RTP video stream, the RTP timestamp
rates SHOULD be the same to ensure that ANC data packets can be
associated with the appropriate frame or field. Otherwise, a 90
kHz rate SHOULD be used."

So in a non-2110 deployment the library is slightly conservative;
not a bug. Document the 90 kHz default and the ST 2110-40 hard
constraint.

# Phase D — Per-codec findings

### D1. ATC (anccodec_atc.cpp).

**D1a. [doc]** File header line 13 says "Type-1 ST 291 packet" —
should say **Type 2** per ST 12-2 §5.

**D1b. [bug, soft]** Parser default rate hint is 30 fps when
`AtcParseRateHint` is absent. ATC bytes alone cannot disambiguate
24/25/30-NDF; only the DF bit narrows 30 → 29.97-DF (ST 12-2
§6.1). A real 24p ATC packet decodes as 30 fps NDF. Caller must
supply the rate from the paired video desc; "30 fps default" is
silently misleading.

**D1c. [gap]** Binary groups (8 × 4-bit) per ST 12-2 §6.2 / Table
6 are dropped on parse and zero-filled on build. `Timecode` value
type has no slot for them.

**D1d. [gap]** Color-frame bit (UDW 3 b7), polarity (UDW 7 b7),
BGF0 / BGF1 / BGF2 (UDW 11 b7, UDW 15 b6/b7) — all per ST 12-1 §9
— are zero-filled on emit and discarded on parse. ST 12-2 §6.1.4
permits zero for HD-progressive but they carry data for SD /
interlaced / analog round trips.

**D1e. [gap]** DBB2 (VITC line select b0–b4, duplication flag b5,
validity bit b6, process bit b7) per ST 12-2 §6.2.2 is not
preserved.

**D1f. [bug]** ST 12-3:2016 HFR alternation. ST 12-3 §6 requires
that at progressive frame rates greater than 30 fps, ATC_VITC1
(field-mark=0) and ATC_VITC2 (field-mark=1) alternate every
frame to label first-vs-second-frame of a frame-pair. The
library's `buildAtcVitc1St291` always builds with DBB1=0x01 and
never alternates. ST 12-2 §7.2 + §9.2 / ST 12-3 §6.

### D2. AFD (anccodec_afd.cpp).

**D2a. [bug]** `AncFormat::BarData` (DID=0x41, SDID=0x06) is
**incorrectly registered**. ST 2016-3 §4 says **one combined
packet** carries both AFD and Bar Data (DID=0x41, SDID=0x05,
DC=8, 8 UDWs total). There is no separate "Bar Data" ANC packet
in ST 2016-3. SDID=0x06 is registered to **ST 2016-4 Pan-Scan
data**, per the road map in ST 2016-3 Annex C.

`ancformat.cpp:73-83`: the `BarData` Data record's `.desc` is
literally `"SMPTE 2016-3 Bar Data"` — that text claims a
provenance that the cited standard does not provide.

**Fix shape:** rename `AncFormat::BarData` → `AncFormat::PanScan`,
update the description to "SMPTE 2016-4 Pan-Scan Information", and
fold bar-data handling into the AFD codec (it shares the packet).

**D2b. [gap]** Bar data (UDW 4: flags, UDW 5–6: value 1, UDW 7–8:
value 2) is zero-filled on build and ignored on parse. Per ST
2016-3 §4.1 / Table 1 these are valid emit fields when the
producer has letterbox / pillarbox metadata.

**D2c. [verified]** AFD UDW 1 bit layout (ST 2016-3 Table 1):
- b6–b3 = a3..a0 (AFD code)
- b2    = AR (aspect-ratio flag)
- b7, b1, b0 = '0'

Library codec layout matches. ✓

**D2d. [verified]** DC = 8 fixed. ✓

### D3. CEA-708 CDP (anccodec_cea708.cpp + cea708cdp.cpp).

**D3a. [bug, CRITICAL]** Time-code section byte order is **REVERSED**.

ST 334-2:2015 §5.3 Table 4 mandates 5 bytes after the section ID
in **H-M-S-F order**:
- byte 0: time_code_section_id = 0x71
- byte 1: '11' (2 bits) | tc_10hrs (2 bits) | tc_1hrs (4 bits)
- byte 2: '1' (1 bit) | tc_10min (3 bits) | tc_1min (4 bits)
- byte 3: tc_field_flag (1 bit) | tc_10sec (3 bits) | tc_1sec (4 bits)
- byte 4: drop_frame_flag (1 bit) | '0' (1 bit) | tc_10fr (2 bits) | tc_1fr (4 bits)

`cea708cdp.cpp:97-101` writes in **F-S-M-H order**:
```cpp
out[pos + 1] = bcdHigh(timeCode.frame(), 0x03);   // ← spec says HOURS
out[pos + 2] = bcdHigh(timeCode.sec(), 0x07);     // ← spec says MINUTES
out[pos + 3] = bcdHigh(timeCode.min(), 0x07);     // ← spec says SECONDS
out[pos + 4] = bcdHigh(timeCode.hour(), 0x03);    // ← spec says FRAMES
```

The accompanying comment cites "§5.1.6.1" — a section number
that doesn't exist in ST 334-2:2015. The internal round-trip
works because `fromBuffer()` reads the same wrong order, so the
bug only manifests against conforming third-party decoders.

A timecode `01:02:03:04` emits as `04:03:02:01` on the wire.

**D3b. [bug]** Reserved bits in the time-code section are written
as zero, not the spec-required '1's:

- Hours byte (byte 1) bits 7-6 must be **'11'** per ST 334-2 Table
  4. Library writes '00'.
- Minutes byte (byte 2) bit 7 must be **'1'**. Library writes '0'.
- Frames byte (byte 4) bit 6 is `zero` ('0'). Library writes '0' ✓.

Conforming receivers may reject the packet on Reserved-bit
validation.

**D3c. [bug]** `drop_frame_flag` (byte 4 bit 7) is hardcoded to 0
even when the input `Timecode` is in DF30 mode. Bundle the fix
with D3a.

**D3d. [bug]** `tc_field_flag` (byte 3 bit 7) is always written as
0. ST 334-2 §5.3 specifies it for interlaced pictures and HFR
(>=50 Hz) frame-pair labelling. Acceptable for progressive HD
but incorrect for legacy / HFR sources.

**D3e. [bug]** On parse (`cea708cdp.cpp:205`) the timecode mode is
hardcoded to `NDF30` regardless of the CDP's `frameRateCode`
field and `drop_frame_flag`. ST 334-2 Table 3 defines 9 frame
rates including 23.976, 24, 25, 29.97, 30, 50, 59.94, 60. The
library should resolve `frameRateCode` (+ drop_frame_flag) to a
`Timecode::Mode` per Table 3.

**D3f. [gap]** `ccsvcinfo_section` (ID 0x73) per ST 334-2 §5.5
is not parsed or built — preserved as opaque bytes. Round-trip
byte-exact, but the library cannot construct or read the caption
service descriptor.

**D3g. [verified]** Other CDP layout elements match the spec:
- `cdp_identifier = 0x9669` ✓
- `cdp_frame_rate` high nibble + reserved '1111' low nibble ✓
- Flags byte bit layout (time_code_present at b7, …, Reserved '1'
  at b0) ✓
- `ccdata_section`: marker '111' + cc_count, then per-triple
  marker '11111' + cc_valid + cc_type ✓
- Footer sequence-counter mirror of header counter ✓
- `packet_checksum` makes the mod-256 sum zero ✓
- Section IDs 0x71 / 0x72 / 0x73 / 0x74 ✓

### D4. HDR Static (anccodec_hdrstatic_st291.cpp).

**D4a. [verified]** DID=0x41, SDID=0x0C per ST 2108-1 §5.1. ✓

**D4b. [verified]** Packet is Type 2; DC range 0x02–0xFF; UDW
carries one or more HDR/WCG Metadata Frames per ST 2108-1 §5.3.
Library matches.

**D4c. [verified — F10, 2026-05-20]** Full byte-position audit of
`anccodec_hdrstatic_st291.cpp` against ST 2108-1 §5.3.2 / §5.3.3.
§5.3.4 / §5.3.5 (Dynamic Metadata Types 1 and 5) deferred — they
belong to separate codecs.

Findings:

- **Frame Type 0 (Static Metadata Type 1 — Mastering Display)** ✓
  Frame Type=`0x00`, Length=`0x1A` (26), Data Byte 1=`0x89` (SEI
  payloadType=137), Data Byte 2=`0x18` (SEI payloadSize=24).
  24-byte body matches H.265 `mastering_display_colour_volume()`:
  three (x, y) primary pairs as u(16) big-endian × 50000, white
  point (x, y) as u(16) big-endian × 50000, then
  `max_display_mastering_luminance` and
  `min_display_mastering_luminance` each as u(32) big-endian in
  units of `0.0001 cd/m²`.

- **Frame Type 1 (Static Metadata Type 2 — Content Light Level)** ✓
  Frame Type=`0x01`, Length=`0x06`, Data Byte 1=`0x90` (SEI
  payloadType=144), Data Byte 2=`0x04` (SEI payloadSize=4). 4-byte
  body matches H.265 `content_light_level_info()` (MaxCLL,
  MaxFALL as u(16) big-endian in cd/m²).

- **Primary ordering (c=0 R, c=1 G, c=2 B) — conformant.**
  H.265's GBR convention is non-normative ("It is suggested
  that c=0 corresponds to the green primary…"); the underlying
  SMPTE ST 2086 defines no canonical ordering. CTA-861.3-A §3.2.1
  Table 5 ("All possible mappings of the chromaticity of Red,
  Green and Blue color primaries to indices 0, 1 and 2 are
  allowed and shall be supported by the sink") explicitly hands
  ordering-discovery to the sink (red = largest x, green = largest
  y, blue = the remaining index). The library's RGB-on-wire choice
  is consistent across codecs and conformant to ST 2108-1, ST 2086,
  H.265, and CTA-861.3. ATSC A/341's normative GBR mandate applies
  only to Dynamic Metadata Type 1 (Frame Type 2), which this codec
  does not emit; that ordering will be enforced inside the future
  `HdrDynamic2094_10` codec.

- **Forward-tolerance on Frame Length** (intentional Postel's-law
  choice, now documented in the codec header). Parser rejects
  `Length < spec_min` (26 / 6) with `Error::CorruptData` but
  accepts `> spec_min`, decoding only the first 24 / 4 SEI bytes
  and skipping the rest. Locks in future spec extensions without
  breaking captures.

- **Duplicate Frame Type tolerance** (intentional, now documented).
  §5.3.2 / §5.3.3 say "no more than one Frame Type 0 (resp. 1)
  per video frame"; a non-conformant sender emitting two is
  decoded last-wins, no error surfaced.

- **`readU32Be` style** — each byte is now `static_cast<uint32_t>`
  *before* the shift. Under C++20 the previous form (`int`-promoted
  shift cast afterwards) was well-defined (result fits in
  `unsigned int`) but the explicit cast makes the intent obvious.

- **§5.3.4 (Frame Type 2 = ATSC A/341 ST 2094-10) and §5.3.5
  (Frame Type 6 = ETSI TS 103 433-1 SL-HDR1) [deferred]** — out of
  scope for the `HdrStatic2086` codec. Frame Type 2 belongs to
  the future `HdrDynamic2094_10` codec (name-only registry entry
  as of F8); Frame Type 6 has no library well-known yet. The
  byte-position audits for those frame types will land with the
  respective codecs. The current parser correctly walks past
  unknown Frame Types, so a mixed-type UDW that contains both
  static and dynamic frames still decodes the static portion.

Test additions (`tests/unit/anccodec_hdrstatic_st291.cpp`,
+7 cases / +43 assertions on top of the prior 8 cases / 65
assertions):

- ST 2108-1 §5.3.2 MD frame byte exactness — locks down the full
  28-UDW MD frame for the BT.2020 + 1000 cd/m² + MaxCLL 1000 /
  MaxFALL 400 sample.
- ST 2108-1 §5.3.3 CLL frame byte exactness — same for the CLL
  frame.
- Parse from spec-exact wire bytes — hand-built BT.709 + 4000
  cd/m² UDW; confirms decoded values match.
- Forward-tolerant parse on oversized MD Frame Length (28) —
  decodes the first 24 SEI body bytes; skips the extras.
- Rejects sub-minimum MD Frame Length (16) with CorruptData.
- Rejects wrong SEI payloadType / payloadSize on MD with
  CorruptData.
- Duplicate Type-0 frame — last-wins tolerance verified.

Verification: `unittest-promeki` passes (HdrStatic_St291 subset:
15 cases / 108 assertions). No new warnings.

### D5. HDR Dynamic 2094-40 KLV (anccodec_hdrdynamic2094_40_st291.cpp).

**D5a. [verified]** DID=0x41, SDID=0x0D per ST 2108-2 §5.1. ✓

(Note: ST 2108-1 §5.3 also reserves Frame Type 5 inside SDID=0x0C
for ST 2094-40 dynamic metadata. Two valid carriages exist. The
library picked the ST 2108-2 KLV carriage — fine, and consistent
with the audit's recommendation.)

**D5b. [verified]** Packet Count in UDW 1 (1-indexed, increments
per packet of a multi-packet message) — comment-level
verification only; full byte audit deferred to Phase G.

**D5c. [verified — F10, 2026-05-20]** Full byte-position audit of
`anccodec_hdrdynamic2094_40_st291.cpp` against ST 2108-2:2019 §5
and ST 2094-2:2017 §6.1, Tables 10 and 11.

Findings:

- **§5.1 Packet header** ✓ — DID=`0x41`, SDID=`0x0D`, Type-2.
- **§5.3 UDW format** ✓:
  - UDW[0] = Packet Count, 1-based; first packet = `0x01`.
  - UDW[1..n] = Metadata Message (all or portion).
  - Multi-packet concatenation: Packet Count increments
    `0x01, 0x02, ...` across packets that share one Message.
  - Parser sorts segments by Packet Count, validates the
    `[1, 2, ..., N]` sequence with no gaps, then concatenates
    Message-bytes.
- **§5.4.1 Message Length** ✓ — 16-bit big-endian (upper 8 in
  first data word, lower 8 in second), excludes the Length field
  itself.
- **§5.4.2 Frame structure** ✓ — Key (16-byte UL) + Length (BER)
  + Value, repeated. Parser walks all Frame Keys; only App 4
  Set frames are surfaced into `HdrDynamic2094_40`. Other
  Frame Keys (Mastering Display §5.4.2.2, Maximum Light Level
  §5.4.2.3, DMCVT App 1 §5.4.2.4) are skipped — this codec
  is scoped to HDR10+ (App 4 / §5.4.2.5).
- **ST 2094-2 Table 10 (App 4 Set Key)** ✓ — verified
  byte-for-byte: `06.0E.2B.34.02.53.01.01.05.31.02.04.00.00.00.00`.
- **ST 2094-2 §6.1 BER Length** ✓ — codec always emits the
  4-byte long form (`0x83` + 3 length bytes) per §6.1
  "the set length field shall be 4 bytes long".
- **ST 2094-2 Table 3 + Table 11 Local Tags** ✓ — every tag
  emitted/parsed (24 in total) matches its spec value:
  `0x3601` ApplicationIdentifier, `0x3602` ApplicationVersion,
  `0x3604` TimeIntervalStart, `0x3605` TimeIntervalDuration,
  `0x3606` UpperLeftCorner, `0x3607` LowerRightCorner,
  `0x3608` WindowNumber, `0x360B` TargetedSystemDisplayMaxLum,
  `0x3630`–`0x3635` window-only ellipse items, `0x3636`/`0x3637`
  TargetedSystemDisplayActualPeakLuminance + Rows,
  `0x3638`/`0x3639` MasteringDisplayActualPeakLuminance + Rows,
  `0x363A` MaximumSceneColorComponentLevels (MaxSCL), `0x363B`
  AverageMaxRGB, `0x363C`/`0x363D` DistributionMaxRGB
  Percentages/Percentiles, `0x363E` FractionBrightPixels,
  `0x363F` KneePoint, `0x3640` BezierCurveAnchors, `0x3641`
  ColorSaturationWeight.
- **Rational denominators** ✓ — verified against Table 11:
  TargetedSystemDisplayMaxLum = 100, MaxSCL = 100000,
  AverageMaxRGB = 100000, DistributionMaxRGBPercentiles = 100000,
  FractionBrightPixels = 1000, KneePoint = 4095,
  BezierCurveAnchors = 1023, ColorSaturationWeight = 8.
- **Required item presence** ✓ — ApplicationIdentifier (Req),
  ApplicationVersionNumber (Req), and the Table 11 Req items
  (MaxSCL, AverageMaxRGB, Distribution*, FractionBrightPixels)
  are unconditionally emitted on every App 4 Set.

Minor findings (documented in the codec header; not bugs):

- **Window 0 ProcessingWindow sentinel** — Window 0 needs
  the full-image rectangle, but the value type carries no
  image dimensions. Codec emits `(0,0)` /
  `(0xFFFF, 0xFFFF)` until a config-driven dimension hint
  lands. Already documented in the codec header.
- **TimeInterval defaults** — Start=0, Duration=1 emitted
  for per-frame metadata. Already documented.
- **Non-App-4 set partial-write window** — if a malformed
  non-App-4 Set in the same Message places writable global
  tags (e.g. `ApplicationVersionNumber`,
  `TargetedSystemDisplayMaxLum`, peak-luminance grids) before
  the `ApplicationIdentifier` tag, the parser writes those
  global fields then bails when it discovers the wrong
  Identifier, leaving a transient mutation. In practice the
  library's builder and well-formed ST 2094-2 senders emit
  `ApplicationIdentifier` first, so this is a theoretical
  ordering concern rather than a wire bug. Documented in the
  codec header.
- **`readU32Be` style** — each byte is now
  `static_cast<uint32_t>` *before* the shift (parallel cleanup
  to the D4c codec).

Test additions (`tests/unit/anccodec_hdrdynamic2094_40_st291.cpp`,
+10 cases / +23 assertions on top of the prior 12 cases / 158
assertions):

- Packet Count byte = `0x01` on the first/only packet.
- Message Length = u16 BE, excludes itself.
- Frame Key is the ST 2094-2 Table 10 App 4 UL, byte-for-byte.
- Set Length is the 4-byte BER long form (`0x83` + 3 length
  bytes); decoded length matches remaining bytes.
- ApplicationIdentifier tag (`0x3601`) carries value 4.
- WindowNumber tag (`0x3608`) carries 0 on the only window.
- TargetedSystemDisplayMaxLum tag (`0x360B`) Rational with
  Den=100 and the expected numerator.
- AverageMaxRGB tag (`0x363B`) Rational with Den=100000.
- Multi-packet — Packet Count is sequential `1..N` across the
  emitted packets.
- Parser skips an unrecognized non-App-4 Frame Key inserted
  before the App 4 Set in the Message (Postel-tolerance).

Verification: `unittest-promeki` passes (HdrDynamic2094_40_St291
subset: 22 cases / 181 assertions). No new warnings.

### D6. VPID (codec landed).

**[verified — F10/D6, 2026-05-20]** Thin AncCodec wrapper landed
at `src/proav/anccodec_vpid.cpp`.  The parser / builder delegate
to `SdiVpid::fromSt291Packet` / `SdiVpid::toSt291Packet` so the
byte-level wire enforcement (DID 0x41 / SDID 0x01 / DC=4) is
shared with direct callers of those helpers.  The codec
registers all three dispatch hooks
(`PROMEKI_REGISTER_ANC_PARSER` + `_BUILDER` + `_SYNC_POLICY`)
under `AncTransport::St291`, honours
`AncTranslateConfig::St291BuildLine` (default
`UnspecifiedLine` = 0x7FE) and `St291FieldB`, and rejects
non-`SdiVpid` Variants on the build path with
`Error::InvalidArgument`.  Frame-sync policy: VPID is a
steady-state link descriptor with no per-frame sequence state,
so Play and Repeat pass through and Drop discards (mirrors the
AFD policy shape).  11 codec cases / 33 assertions added.
Callers wanting the ST 352:2013 §6.2 recommended VANC line can
pre-fill the cfg via `SdiVpid::recommendedAncLine(fmt, field)`.

### D7. SCTE-104 (registered but no codec yet).

**[gap]** ST 2010:2008 is on hand. Codec is the next obvious
extension. No audit beyond "registry entry correct".

### D8. ST 2020 Dolby audio metadata.

**[verified — F10/D8, 2026-05-20]** ST 2020-2 Method A codec
landed.  New `AncSt2020Audio` value type +
`anccodec_st2020audio.cpp` round-trip ST 2020-1 §7 / ST 2020-2 §5
byte layout: DID 0x45 fixed, SDID 0x01..0x09 channel-pair
association (ST 2020-1 §7.1 Table 1), Payload Descriptor §5.4
Table 1 (COMPATIBILITY=0, Reserved=0, VERSION=01b, DOUBLE_PKT,
SECOND_PKT, DUPLICATE_PKT), DC1 = MDF + 1 single packet, 2-packet
split for 254 < MDF ≤ 508, Y-stream emission per §8.  Multi-parser
reassembles the §5.3 split pair, validates the §5.4.3 DOUBLE /
SECOND bit sequence, rejects mismatched-SDID pairs and three-plus
packet groups.  Wildcard-SDID SDP emission (A7) had already landed
in F3 (concrete-SDID expansion 0x01-0x09 instead of the (DID, 0x00)
collision).  Deferred: ST 2020-3 Method B (Dolby E specific) +
deeper §5 metadata-frame parsing (sync segments / data segments /
end-of-frame sync) — those will land with the codecs that actually
need them.  19 codec + 5 value-type test cases, 116 assertions.

### D9. RDD 8 OP-47 (codec landed; multipacket pending).

**[verified — F10/D9, 2026-05-20]** OP-47 SDP codec landed.  New
`AncOp47Sdp` value type + `anccodec_op47sdp.cpp` round-trip the
full RDD 8 §5 SDP layout: DID 0x43 / SDID 0x02, IDENTIFIER 0x51
0x15, LENGTH = full UDW byte count, FORMAT CODE 0x02 (WST
teletext), 5 Structure A descriptors (line + reserved + field
one), up to 5 Structure B 45-byte payloads (run-in / framing /
MRAG / subtitling), FOOTER ID 0x74, 16-bit BE Footer Sequence
Counter (§5.2), SDP CHECKSUM (§5.3 — sum mod 256 = 0).  Parser
rejects: wrong identifiers, wrong format code, LENGTH/DC
mismatch, wrong footer ID, corrupted checksum, non-zero
descriptor following zero (§5.4.2 prefix-rule violation).
Builder rejects >5 packets.  Y-stream emission per §3 (cites
SMPTE 344M).  Frame-sync policy: only Play passes through —
Drop and Repeat both drop, because repeating would re-issue an
identical FSC and confuse downstream loss detection.  RDD 8 §6
Multipacket carriage (DID 0x43 / SDID 0x01) remains a registry-
only entry (`AncFormat::Op47Multipack`); the codec is a sibling
that will land alongside an actual multipacket-emitting backend.
20 codec + 5 value-type test cases, 121 assertions.

### D10. ST 2106 CCF — Closed Caption Flag.

**[gap]** ST 2106:2016 (present) defines a 1-byte ANC packet at
DID=0x41 / SDID=0x14 that indicates the presence of closed
captions. Not in the registry. Useful for caption-on / caption-off
signalling.

### D11. ST 2031 line-21 / VBI carriage.

**[gap]** ST 2031:2015 (present) defines a VANC mapping of VBI
data including line-21 captions. Not in the registry. Lower
priority than CDP since ST 334-2 supersedes it for new content.

# Phase E — Cross-cutting findings

### E1. `AncTranslateConfig::St291BuildCBit` is missing.
**Severity:** *gap* (see B5)

### E2. `AncChecksumPolicy` is defined but not wired.
**Severity:** *bug* (see B2)

### E3. `Metadata` lookups on the ANC hot path.
**Severity:** *perf* (see A8)

### E4. `registeredIDsForCategory` / `registeredIDsForTransport` are O(N).
**Severity:** *perf* (small N today; grows with A10)

### E5. Stream serialisation versioning.
**Severity:** *gap* (informational)

Wire bytes inside `AncPacket::data()` are versioned by the
underlying SMPTE / RFC spec, not by `DataStream`. A recording with
future-spec ANC packets deserialises the envelope but the bytes
inside may not parse on an older codec — that is correct
behaviour, just worth documenting.

### E6. `AncPayload` does not honour `AncDesc::acceptsFormat`.
**Severity:** *policy*

`addPacket(pkt)` adds unconditionally even when the desc filter is
restrictive. The descriptor's filter is a capability declaration,
not a runtime gate. Probably intentional but worth a
`addPacketChecked()` variant or a debug-build assert.

### E7. ~~Doc gap: there is no `docs/anc.md`.~~ **Resolved (F9.3, 2026-05-19).**
**Severity:** *doc* (already flagged in `ancdata.md` Phase 6)

# Phase F — Fix plan (DRAFT — needs sign-off)

Reordered to absorb the new RFC 8331 / ST 2110-40 / ST 334-2 / ST
2016-3 findings. High-correctness items first (no API risk), then
the RFC 8331 / ST 2110-40 compliance bugs, then API widening,
then performance / docs.

## F1 — Comment / doc / reserved-input tightening (low risk, no API break) — **LANDED 2026-05-19**

1. **A11** ✅ — checksum-bit-layout comment in `st291packet.h`
   now distinguishes CS (9-bit value + bit 9 = NOT bit 8, no
   independent parity) from DID/SDID/DBN/DC/UDW (8-bit data + 2
   parity bits).
2. **D1a** ✅ — `anccodec_atc.cpp` file header now correctly says
   ATC is a Type-2 ST 291 packet (was wrongly "Type-1").
3. **A2** ✅ — `buildWireBytes()` rejects any UDW whose upper
   2 bits are `11` (the §9.1 protected-code range plus
   parity-violation values 0x300–0x3FB). Per Q1 decision the
   build fails hard (returns invalid `St291Packet`) rather than
   silently mask-and-recompute. Added `Error::ProtectedAncCode`
   to `error.h` / `error.cpp` for use by future Result-returning
   build APIs.
4. **A6** ✅ — `buildWireBytes()` rejects `udw.size() > 255`
   (ST 291-1 §6.5). Verified: the 255-UDW case still succeeds.
5. **B3** ✅ — `buildRaw()` rejects DID=0x00 (ST 291-1 §6.1).
   Other reserved DID ranges (0x01-0x03, 0x20-0x3F, 0x81-0x83,
   etc.) deferred to F6 when the build API widens to surface
   specific error codes.
6. **C3** — **withdrawn** (re-verified during F1; the original
   audit misread the planner's increment order — max is 255, in
   spec). No code change.
7. **A3** ✅ — `AncPacket::data()` doxygen now spells out the
   "wire bytes start at DID, ADF is not stored" contract, cites
   ST 291-1 §5.2 and RFC 8331 §1, and tells SDI-touching backends
   to strip/prepend ADF themselves.
8. **D2b** ✅ — `anccodec_afd.cpp` file header now includes the
   full 8-UDW ST 2016-3 Table 1 layout (was 4 rows, partial), calls
   out bar-data round-trip as a known F4 deferral, and notes the
   `BarData`-vs-Pan-Scan mis-registration as the structural fix
   in F4.

Tests added (`tests/unit/st291packet.cpp`):
- Reject DID=0x00 → invalid `St291Packet`.
- Reject oversize UDW list (256 entries); 255-entry max still
  succeeds with valid checksum.
- Reject six representative protected / parity-violation values
  (0x300, 0x3AB, 0x3FC..0x3FF).
- Accept correctly-encoded pass-through values (parity-correct
  bytes 0x143 and 0x255).

Verification: full `unittest-promeki` (6963 cases, 140843
assertions) passes; the pre-existing `cscpipeline.cpp` `-Wsign-
compare` warnings remain untouched (unrelated work).

## F2 — CEA-708 CDP wire-format correctness (CRITICAL — interop bug) — **LANDED 2026-05-19**

The library previously emitted invalid CDPs against any conforming
third-party CEA-708 decoder.  Single commit fixed all five issues.

1. **D3a** ✅ — `Cea708Cdp::toBuffer()` / `fromBuffer()` now emit
   and consume the time-code section in spec H/M/S/F order
   (ST 334-2:2015 §5.3 Table 4).  Previously the bytes were written
   in F/S/M/H order, so a `01:02:03:04` source emitted as
   `04:03:02:01` on the wire.
2. **D3b** ✅ — hours byte upper bits stamp the reserved `'11'`
   pattern (bit-pattern `0xC0`); minutes byte upper bit stamps the
   reserved `'1'` (`0x80`).  Frame byte bit 6 stays `'0'` (already
   correct).
3. **D3c** ✅ — `drop_frame_flag` (byte 4 bit 7) is now sourced
   from `Timecode::isDropFrame()` on build and parsed back into
   the resolved `Timecode::Mode` on receive.
4. **D3e** ✅ — `fromBuffer()` resolves `Timecode::Mode` from the
   header's `frameRateCode` (ST 334-2 §5.3 Table 3, codes 1..8 →
   23.976/24/25/29.97/30/50/59.94/60) combined with the wire
   `drop_frame_flag`.  Unknown / reserved frame-rate codes yield
   an invalid `Timecode::Mode` rather than the previous hardcoded
   NDF30.  DF flag is silently ignored on rates without a DF
   sister format (24/25/50/60) — matches libvtc's family model.
5. **D3d** ✅ — added an explicit `Cea708Cdp::tcFieldFlag` field
   (default `false`).  Callers with interlaced / HFR context drive
   the bit; default progressive HD case keeps the bit at 0.  The
   flag round-trips through wire / JSON / equality.

Tests added to `tests/unit/cea708cdp.cpp`:
- H/M/S/F byte position verification with the canonical
  `01:02:03:04` digit set; expected wire bytes `0xD2 0xB4 0x56
  0x07` (= reserved bits + BCD digits).
- Reserved-bit isolation test with all-zero digits to confirm the
  reserved `'11'` / `'1'` bits are stamped regardless of digit
  content, and that frame byte bit 6 stays `'0'`.
- `drop_frame_flag` round-trip for both DF30 and NDF30 source
  timecodes.
- `tc_field_flag` round-trip plus default-false sanity.
- `Timecode::Mode` resolution across all eight Table 3 frame-rate
  codes, with both DF=0 and DF=1 cases for the 29.97 / 30 / 59.94
  families.
- Mode invalidation for reserved code 0.
- Full struct equality round-trip with all flags set.

Verification: `unittest-promeki` (6972 cases, 141176 assertions)
passes; no new warnings.

## F3 — RFC 8331 / ST 2110-40 correctness — **LANDED 2026-05-19**

1. **C1** ✅ — `FieldIndication::Invalid = 0x1` added.
   `RtpPayloadAnc::unpackAncPackets` skips any RTP datagram whose
   F-bit is `0b01` (RFC 8331 §2.1 SHOULD-ignore) and continues
   processing the rest of the list.
2. **C2** ✅ — `validate()` now accepts `ANC_Count=0` + `Length=0`
   keep-alive payloads as end-of-frame markers; rejects malformed
   non-zero-Length keep-alive shapes.  `unpackAncPackets` likewise
   absorbs keep-alives without appending records.  `packAncFrame`
   emits a single ST 2110-40 §5.5 keep-alive RTP packet
   (`ANC_Count=0`, `Length=0`, `Marker=1`, F per session) whenever
   the planner produces no records — empty input list, list of
   non-St291 packets only, or list where every entry was filtered
   under C10.  Added `setKeepAliveField` / `keepAliveField` so
   interlaced sessions can stamp the F-bit appropriately;
   `FieldIndication::Invalid` is rejected.  `RtpAncPacketizerThread`
   no longer early-exits on empty AncPayload / missing-streamIdx —
   it always pushes a keep-alive batch so downstream consumers see
   end-of-frame for every video frame.
3. **C4** ✅ — keep-alive emit deliberately stops at the 8-byte
   §2.1 header (no records, no word_align trailing pad per §2.2).
4. **C5** ✅ — `unpackAncPackets` now `promekiWarn`s on a record
   whose padded extent overruns the declared `Length`.  Body bytes
   still decode (un-padded record is still valid wire); the
   warning surfaces the malformed-payload condition under default
   policy and can become a hard reject when `StrictValidate` lands
   in F7.
5. **C7** ✅ — `St291Packet` exposes the full sentinel set as
   named constants: `LineNoSpecific` (0x7FF), `LineSwitchingDefault`
   (0x7FE) aliased as `UnspecifiedLine`, `LineLargerThan11Bits`
   (0x7FD); `UnspecifiedHOffset` (0xFFF), `HOffsetInHanc` (0xFFE),
   `HOffsetInActiveVideo` (0xFFD), `HOffsetLargerThan12Bits`
   (0xFFC).  `AncMeta::St291::Line` VariantSpec default is now
   `0x7FE` (was `0x0000`).  `AncTranslateConfig::St291BuildLine`
   default is `0x7FE` (was `0`) and every codec's caller-side
   fallback (CEA-708 / AFD / ATC / HDR-static / HDR-dynamic 2094-40)
   now uses `St291Packet::UnspecifiedLine` so a freshly built
   packet with no cfg override lands on 0x7FE rather than 0.
6. **C8** ✅ — `AncDesc::toSdp` emits `m=video` (was
   `m=application`); `fromSdp` accepts only `m=video` smpte291
   sections per RFC 8331 §3.1 / ST 2110-40 §7.
7. **A7** ✅ — `AncFormat::Data` gained `st291SdidRange` (concrete
   SDID list for wildcard families).  `Smpte2020Audio` registers
   `0x01..0x09` as its concrete range.  New accessor
   `AncFormat::st291ConcreteSdids()` returns either the explicit
   range for wildcards or `{st291Sdid}` for ordinary formats.
   `AncDesc::toSdp` walks this list when emitting `DID_SDID` fmtp
   entries, so wildcard families now produce one entry per
   concrete SDID instead of the previous (DID, 0x00) collision
   with RFC 8331's Type-1 sentinel.  `fromSdp` dedupes back into
   the family ID via wildcard lookup.
8. **C11** ✅ — `packAncFrame` stable-sorts St291 packets by
   ascending `(Line_Number, Horizontal_Offset)` via a new
   `orderedSt291Indices` helper before planning RTP groupings.
   Sentinel locations (default 0x7FE / 0xFFF) sort to the tail
   because their numeric value exceeds any real coordinate;
   stable sort preserves their relative input order.  The
   `RtpPlan` shape changed from raw-AncPacket indices to indices
   into the sorted index list.
9. **C9** ✅ — `AncDesc::toSdp` now emits the full ST 2110-40 §7
   mandatory fmtp parameter set: `SSN` (always
   `ST2110-40:2018`), `TM` (always `CTM` — Compatible
   Transmission Model), `exactframerate` (emitted from the
   descriptor's `FrameRate` when valid, formatted as the bare
   integer for integer rates and as `<num>/<den>` for rational
   rates), and the optional `TROFF` (uint32, 90 kHz ticks) /
   `VPID_Code` (ST 352 byte 1) parameters when non-zero.
   `AncDesc::Impl` gained `troff` + `vpidCode` fields with the
   matching `troff()` / `setTroff()` / `vpidCode()` /
   `setVpidCode()` accessors.  `fromSdp` parses both back into
   the descriptor.  DataStream wire format bumped to v2 with a
   conditional v2-trailer read so older v1 streams still parse
   (TROFF and VPID_Code default to 0).
10. **C10** ✅ — `orderedSt291Indices` skips
    `AncCategory::AudioMetadata` packets with a `promekiWarn`,
    matching ST 2110-40 §5.2.1 ("EDH packets should not be
    transported using this mechanism" + "embedded audio packets
    should not be transmitted using this method").  EDH packets
    are not yet a first-class `AncFormat` (DID 0xF4 / 0xF8 in
    ST 291-1 §11.3); the filter is documented so the future EDH
    registry entry plugs in cleanly.

Tests added:
- `tests/unit/network/rtppayloadanc.cpp` — F3 SUBCASEs for C1
  (F=0b01 record skip), C2 (keep-alive emit, receive, F-bit setter
  + Invalid rejection), C11 (sort by Line / HOffset), C10
  (AudioMetadata drop), and C7 (spec-default verification + named
  sentinel constants), plus an end-to-end "CEA-708 codec defaults
  Line to UnspecifiedLine" test confirming the codec adopts the
  0x7FE fallback.
- `tests/unit/ancdesc.cpp` — F3 SUBCASEs for C8 (`m=video` on
  emit, accept), C9 (SSN / TM / exactframerate emission, integer
  rate formatting), A7 (wildcard SDID expansion, fromSdp dedupe,
  `st291ConcreteSdids` per format type).
- Existing tests updated: ANC packetizer thread tests now expect
  keep-alives for empty input / missing streamIdx instead of empty
  batches; `validate` test accepts `ANC_Count=0` keep-alives;
  `fromSdp rejects wrong media type` switched to `m=audio` since
  `m=video` is now the correct ANC media type.

Verification: `unittest-promeki` (6979 cases, 141073 assertions)
passes; no new warnings.

## F4 — ST 2016-3 AFD/Pan-Scan correction — **LANDED 2026-05-19**

1. **D2a** ✅ — `AncFormat::BarData` is gone.  `AncFormat::PanScan`
   is registered in its place at DID 0x41 / SDID 0x06 with the
   correct description "SMPTE 2016-4 Pan-Scan Information" (per
   ST 2016-3 Annex C road map).  The legacy `"BarData"` name no
   longer resolves through `AncFormat::fromName` per audit Q7
   (rename without compat shim).  The `Afd` registration's
   description now reads "SMPTE 2016-3 Active Format Description
   and Bar Data" — both fields ride in the single combined
   ST 2016-3 §4 packet, so the registry text now matches the
   wire reality.
2. **D2b** ✅ — added `AncAfd` value type
   (`include/promeki/ancafd.h` / `src/proav/ancafd.cpp`) carrying
   the AFD code, AR flag, the four bar-data edge flags
   (Top / Bottom / Left / Right per ST 2016-1 §6 + ST 2016-3
   Table 1 UDW 4), and the two 16-bit bar-data values
   (UDWs 5-6 / 7-8).  Registered `DataTypeAncAfd = 0x6B`; full
   PROMEKI_DATATYPE wiring with `toString`, `toJson`, equality,
   and v1 DataStream serialization.  The AFD codec
   (`src/proav/anccodec_afd.cpp`) is migrated to round-trip
   through `AncAfd` — every bar-data flag bit and both 16-bit
   values land back byte-exact on parse.  The build path retains
   a legacy compatibility shim that accepts a bare
   `Variant<uint8_t>` (the pre-F4 packed code+AR byte) and
   promotes it to a default-bar AncAfd, so call sites that have
   not yet been migrated continue to emit valid packets.  The
   parse path always returns `Variant<AncAfd>` — there is no
   uint8_t fallback on the receive side.  Existing AFD UDW 1
   bit layout (a3..a0 in bits 6..3, AR in bit 2 per ST 2016-3
   Table 1) is preserved.

Tests added / updated (`tests/unit/anccodec_afd.cpp`):
- Every AFD code (0..15) at AR=1 round-trips through Variant<AncAfd>.
- AR=0 path round-trip.
- Letterbox bar-data round-trip with explicit wire-byte position
  checks on UDW 4 (`0xC0` = Top|Bottom) and UDWs 5-8 (MSB-first
  16-bit values).
- Pillarbox bar-data round-trip with explicit wire-byte position
  checks on UDW 4 (`0x30` = Left|Right).
- Empty-bar-data case zeros UDWs 2..8.
- Legacy uint8_t Variant input continues to build valid packets
  and parses back as AncAfd with zero bar data.
- Capability queries (parser/builder registration) preserved.
- Line / FieldB threading via AncTranslateConfig preserved.
- Sync-policy tests (Play/Drop/Repeat) migrated to AncAfd.
- AncAfd value-type tests: default construction, setBarFlags
  masks the reserved low nibble, per-flag accessors round-trip
  independently, equality/inequality, Variant round-trip,
  DataStream round-trip.
- PanScan registry verification: DID 0x41 / SDID 0x06,
  "SMPTE 2016-4 Pan-Scan Information" description; `fromName`
  resolves "PanScan" and rejects the legacy "BarData" name.

Other touched call sites:
- `tests/unit/ancpacket.cpp`, `tests/unit/ancformat.cpp`,
  `tests/unit/st291packet.cpp`: `AncFormat::BarData` →
  `AncFormat::PanScan` substitution.

Verification: `unittest-promeki` (7005 cases, 141491 assertions)
passes; `unittest-tui` (196 cases, 1878 assertions) and
`unittest-sdl` (15 cases, 18162 assertions) pass.  No new
warnings.

## F5 — ATC value type + HFR (ST 12-3) — **LANDED 2026-05-19**

1. **D1c / D1d / D1e** ✅ — added `AncAtc` value type
   (`include/promeki/ancatc.h` / `src/proav/ancatc.cpp`) wrapping
   the wall-clock `Timecode` plus the SMPTE ST 12-1/-2 fields it
   does not model: eight 4-bit binary-group nibbles
   (`UserBits = Array<uint8_t, 8>`), the ColorFrame / Polarity /
   BGF0 / BGF1 / BGF2 flag byte, and the DBB2 status byte
   (VITC line-select / duplicate / validity / process).  Registered
   `DataTypeAncAtc = 0x6A`; full PROMEKI_DATATYPE wiring with
   `toString`, `toJson`, equality, and v1 DataStream
   serialization.  The ATC codec
   (`src/proav/anccodec_atc.cpp`) is migrated to round-trip
   through `AncAtc` — every binary-group nibble, every flag bit,
   and DBB2 land back byte-exact on parse.  The build path still
   accepts a bare `Variant<Timecode>` as a compatibility shim
   (promotes to a default-flag `AncAtc`), but the canonical
   round-trip type is now `AncAtc`.
2. **D1b** ✅ — added `AncMeta::Atc::Rate` (uint32 fps) for the
   per-packet rate sidecar.  Parser precedence per audit Q4:
   meta key first, then `AncTranslateConfig::AtcParseRateHint`,
   then hard fail with the new `Error::InsufficientContext`
   (added to `Error::Code` + table).  The pre-F5 silent-30-fps
   default is gone — quiet wrong answers were the worst option.
   `modeForRate` honours non-standard explicit hints via
   `vtc_format_find_or_create`, so an unusual rate (e.g. 99 fps)
   no longer silently demotes to NDF30.
3. **D1f** ✅ — added `AncAtc::atcVitcFormatForFrame(fps,
   frameIdx)` and `ancAtcIsHfrRate(fps)` helpers that encode the
   ST 12-3:2016 §6 rule: at progressive >30 fps the
   ATC_VITC1 (DBB1=0x01, field-mark=0) and ATC_VITC2 (DBB1=0x02,
   field-mark=1) carriages alternate per physical frame to label
   first-vs-second of each frame-pair.  Callers route each frame
   through the helper to pick the right `AncFormat::ID` for the
   build call; the codec emits whatever the caller asks for
   (DBB1 per ID), no auto-magic.  Documenting the rule in a
   first-class helper closes the audit's "library hardcodes
   DBB1=0x01" complaint.

Tests added (`tests/unit/anccodec_atc.cpp`):
- All pre-F5 `parsed.get<Timecode>()` call sites migrated to
  `atcTimecode(parsed)` (extracts the Timecode from the
  AncAtc-carrying Variant).
- 32-combination flag-bit sweep (every subset of the five flag
  bits round-trips independently — catches any cross-wired UDW
  b6/b7 assignment).
- Binary-group nibble round-trip over the low-nibble (0..7) and
  high-nibble (8..F) ranges.
- DBB2 bit-sweep verifying each bit lands in the correct UDW
  9..16 b3 slot (LSB-first).
- Timecode-only Variant build path produces a default
  zero-flags / zero-userBits / zero-DBB2 AncAtc on parse.
- D1b: no-hint parse → `Error::InsufficientContext`; meta key
  outweighs cfg hint; non-standard hint (99 fps) yields a custom
  Timecode mode.
- D1f: `atcVitcFormatForFrame` returns AtcVitc1 at ≤30 fps for
  every frame; alternates VITC1 / VITC2 at HFR rates (50, 60,
  120) with the expected even/odd parity.
- DataStream Variant<AncAtc> round-trip verifying every field
  survives a tag-framed serialization.

Verification: `unittest-promeki` (6993 cases, 138892 assertions)
passes; no new warnings.

## F6 — Type-1 packet support + reserved validation — **LANDED 2026-05-19**

1. **A1 + B5** — widen `St291Packet` for Type-1 (DBN word); key
   second-word interpretation off DID's high bit. **DONE.**
   - Added `dbn()` accessor that returns the second-word data byte
     for Type-1 packets (DID ≥ 0x80) and zero for Type-2.
   - `sdid()` now reports the RFC 8331 §3.1 sentinel `0x00` for
     Type-1 packets (it was silently returning the DBN byte
     mis-labelled as SDID).
   - New `buildRawType1(did, dbn, udw, ...)` factory makes the
     Type-1 build explicit at the call site; it rejects DIDs with
     the high bit clear so callers using SDID-based packets are
     forced to `buildRaw()`.
   - `setUdw` was preserving SDID across re-packs; now reads the
     existing second-word as DBN for Type-1 and SDID for Type-2 so
     the DBN cycle survives a UDW mutation.
   - `ntv2anc.cpp` AJA emission updated: the SID byte fed to
     `dst.SetSID()` is now `sp.isType1() ? sp.dbn() : sp.sdid()`
     so Type-1 DBN round-trips through the AJA backend.
   - Header doxygen on `isType1()` rewritten (the prior text was
     factually wrong on both directions: it claimed Type-1 packets
     "carry their own length" — they do not, they carry a DBN —
     and that "almost every modern ANC format is Type-1" when
     actually almost every modern format is Type-2).
2. **A5** — register Packet-Marked-for-Deletion (DID 0x80) as a
   first-class no-op format and use it as the canonical Type-1
   round-trip test fixture. **DONE.**
   - New `AncFormat::PacketForDeletion` enumerator + registry
     entry: DID `0x80`, wildcard SDID (so the `(0x80, anyDBN)`
     lookup falls back here regardless of DBN value), concrete
     SDID range `{0x00}` for SDP fmtp emission per RFC 8331 §3.1.
     Category is `Unknown` until F8 adds a control-packet
     category.
   - New unit-test fixture exercises the build → from() → AJA
     round-trip end-to-end through `PacketForDeletion`.
3. **B1** — tighten `St291Packet::from()` length validation.
   **DONE.**
   - `from()` now unpacks the DC byte and requires
     `data.size() >= ceil((4 + DC) * 10 / 8)` so a truncated
     capture is rejected at promotion rather than letting later
     accessors walk off the end of the buffer.
4. **B4** — add `udwRaw()`. **DONE.**
   - New `udwRaw()` returns the UDW list with the parity-bits
     preserved verbatim, for byte-exact replay verification.
   - `udw()` now strips parity bits (matches every in-tree
     consumer's `& 0xFF` mask and matches the audit's premise);
     the existing test masks survive unchanged.

Test additions (8 new cases on top of the prior 7012):

- buildRawType1 builds a Type-1 packet; word 1 is DBN, sdid() is 0.
- buildRawType1 rejects Type-2 DIDs (sanity-checks the high-bit guard).
- dbn() reports 0 on Type-2 packets.
- PacketForDeletion is the canonical Type-1 round-trip (build +
  from() + registry lookup at multiple DBN values + concrete-SDID
  enumeration).
- setUdw on a Type-1 packet preserves DBN.
- udwRaw() preserves parity bits while udw() strips them.
- from() rejects buffers shorter than the declared DC implies.
- Existing buildRaw test switched to a Type-2 DID so sdid()
  read-back semantics make sense (Type-1 read-back is covered by
  the new tests above).

Verification: `unittest-promeki` (7020 cases, 141581 assertions)
passes; no new warnings.

## F7 — Checksum policy wiring — **LANDED 2026-05-19**

1. **B2 / E2** — wire `AncChecksumPolicy` into
   `St291Packet::from()` and `RtpPayloadAnc::unpackAncPackets`.
   Default `PreserveOrRecompute`. **DONE.**
   - `St291Packet::from()` gained an
     `AncChecksumPolicy policy = PreserveOrRecompute` parameter;
     under `StrictValidate` the promotion fails with the new
     `Error::InvalidChecksum` when the stored §6.4 Checksum_Word
     does not match the value recomputed over
     (DID, SDID, DataCount, UDW). `PreserveOrRecompute` and
     `AlwaysRecompute` accept the packet as-is on the parse path
     (per Q6 — the byte-exact replay contract); the distinction
     between those two only matters on emission.
   - `RtpPayloadAnc::unpackAncPackets` gained the same
     `policy` parameter (default `PreserveOrRecompute`) and
     forwards it to `St291Packet::from` at the per-record
     promotion boundary. A `StrictValidate` mismatch surfaces as
     `Error::InvalidChecksum` from `unpackAncPackets`; records
     decoded before the failing one remain in the output list.
2. Wire RFC 8331 §7 Data_Count and Checksum validation hooks
   under `StrictValidate`. **DONE.**
   - The §7 Data_Count check (declared DC vs available payload
     bytes) was already structural in `St291Packet::from()` and
     `unpackAncPackets` — both reject truncated buffers via
     `Error::InvalidArgument` / `Error::OutOfRange` regardless of
     policy, which is what §7's SHOULD-check calls for. Under
     `StrictValidate` the additional check is the Checksum_Word
     (see item 1).
3. New error code. **DONE.**
   - Added `Error::InvalidChecksum` with a registry entry
     describing the StrictValidate path. The doxygen on
     `AncChecksumPolicy::StrictValidate` already referenced this
     code; it now exists.

Test additions (7 new cases on top of the prior 7012):

- `from()` defaults to `PreserveOrRecompute` and tolerates a
  deliberately-corrupted checksum.
- `from()` with `AlwaysRecompute` is also tolerant on parse.
- `from()` with `StrictValidate` rejects a mismatched checksum
  with `Error::InvalidChecksum`.
- `from()` with `StrictValidate` accepts a clean packet.
- `unpackAncPackets` default policy preserves a captured
  bad-checksum record verbatim.
- `unpackAncPackets` with `StrictValidate` returns
  `Error::InvalidChecksum` and leaves the output list empty for
  a single-record payload.
- `unpackAncPackets` with `StrictValidate` accepts a clean
  record.

Verification: `unittest-promeki` (7019 cases, 141471 assertions)
passes; no new warnings.

## F8 — Registry / category housekeeping — **LANDED 2026-05-19**

1. **A9** — add `Subtitles` / `Klv` / `Sei` / `Vbi` to
   `AncCategory`. Landed: four new enumerators registered in
   `enums.h`; `AncCategory(name)` round-trips for each. No
   existing well-known format was reassigned (`Klv0601` stays
   in `Geolocation` per the doc — `Klv` is reserved for the
   generic ST 336 / non-geolocation bucket added in F8 for use
   by future codecs).
2. **A10** — register the new well-known formats:
   - `Op47Sdp` — DID 0x43 / SDID 0x02, category `Subtitles`.
   - `Op47Multipack` — DID 0x43 / SDID 0x01, category
     `Subtitles`.
   - `CcfSt2106` — DID 0x41 / SDID 0x14, category `Captions`
     (the CCF is a companion-of-captions signal, not its own
     family).
   - `VbiSt2031` — DID 0x60 / SDID 0x01, category `Vbi`. The
     ATC SDIDs at (0x60, 0x60..0x62) are unaffected — the
     wildcard match is still exact-first.
   - `HdrDynamic2094_10` — name-only registration with
     `st291Did == 0`. ST 2108-1 multiplexes Frame Type 1
     (`HdrStatic2086`) and Frame Type 2 (this entry) inside
     the same DID 0x41 / SDID 0x0C, so the (DID,SDID) lookup
     stays anchored to `HdrStatic2086` and the ST 2108-1
     codec is responsible for promoting the format ID after
     it inspects the Frame Type byte. Pan-Scan (F4) was the
     "registry rename" item and shipped in that phase.
3. Unit tests added in `tests/unit/ancformat.cpp` cover each
   new format's name / category / canonical transport /
   (DID, SDID) lookup; a dedicated test verifies
   `HdrDynamic2094_10` is name-addressable but does not collide
   with `HdrStatic2086` on the wire lookup.

## F9 — Performance + docs

1. **A8 / E3** — ✅ **Partially landed (2026-05-19).** The five
   hot-path ST 291 framing fields (Line, HOffset, FieldB, CBit,
   StreamNum) are now direct accessors / setters on `AncPacket`
   itself, eliminating the per-packet `Metadata` hash lookups on
   the RTP pack / unpack path.  The `AncMeta::St291` namespace
   (which previously declared these as Metadata keys) is removed.
   Remaining item: profile the net hot-path gain on a 4K60 ANC
   stream.
2. **E4** — ✅ **Landed (2026-05-19, in the same F6–F9 commit
   `8f35a9a`).** `AncFormatRegistry` now maintains two
   `Map<int, AncFormat::IDList>` indexes (`byCategory`,
   `byTransport`) populated incrementally inside `add()` so the
   `registeredIDsForCategory` / `registeredIDsForTransport`
   lookups are O(1) map probes instead of full registry scans.
   `registerData` routes through `add()` so runtime registrations
   stay consistent with the index.  The transport index honours
   `canonicalTransport` plus any non-zero per-transport key byte,
   so multi-transport formats (e.g. `HdrStatic2086` on canonical
   `HdmiInfoFrame` with secondary `St291` carriage) appear under
   every applicable transport — preserving the pre-F9.2 semantics
   of the scan-based path.  Existing
   `tests/unit/ancformat.cpp` cases for `registeredIDsForCategory`
   (Captions, Hdr, Subtitles, Vbi) and `registeredIDsForTransport`
   (St291, HdmiInfoFrame, MpegTsPrivate) all continue to pass
   under the indexed implementation; per-test wire-byte inclusion
   / exclusion semantics are unchanged.
3. **E7** — ✅ **Landed (2026-05-19).** `docs/anc.md` authored with
   architecture overview, wire-layer contracts (ADF stripping, Type-1
   vs Type-2, protected codes, checksum policy), transport mapping,
   hot-path notes, and `ancaudit.md` cross-reference.

## F10 — Codec deep audits (Phase G)

After F2–F5 ship, do byte-position audits against:

1. **ST 2108-1 §5.3.2 / §5.3.3** for the HDR-static SEI-style
   codec (D4c) — ✅ **LANDED 2026-05-20.** Frame Type 0 and Frame
   Type 1 (Mastering Display + Content Light Level) verified
   byte-for-byte; +7 reference-vector tests added. Findings
   logged under D4 above. §5.3.4 / §5.3.5 (Dynamic Metadata Types
   1 and 5) deferred to the codecs that will own those frame
   types (`HdrDynamic2094_10` and a future SL-HDR1 codec).
2. **ST 2108-2 §5.3 / §5.4** for the HDR-dynamic KLV codec (D5c)
   — ✅ **LANDED 2026-05-20.** App 4 Set Key (ST 2094-2 Table 10),
   4-byte BER long-form Set Length (ST 2094-2 §6.1), 16-bit BE
   Message Length (excludes self), 1-based Packet Count, all 24
   Local Tags (ST 2094-2 Tables 3 + 11), all 8 Rational
   denominators verified byte-for-byte. +10 byte-level tests
   added. Findings logged under D5 above. The other three Frame
   Keys (MD Color Volume §5.4.2.2, Max Light Level §5.4.2.3,
   DMCVT App 1 §5.4.2.4) remain out of scope for this codec —
   future codecs would land their own audits.
3. **RDD 8** for OP-47 SDP — ✅ **LANDED 2026-05-20.** New
   `AncOp47Sdp` value type (`DataTypeAncOp47Sdp = 0x6C`) + codec
   (`src/proav/anccodec_op47sdp.cpp`) round-trips up to 5 VBI
   packets per SDP. Byte-position audit against RDD 8 §5.1
   verified for: IDENTIFIER 1/2 (`0x51 0x15`), LENGTH (full UDW
   byte count), FORMAT CODE (`0x02` WST teletext), Structure A
   descriptor bit layout (b0-b4 line, b5-b6 reserved, b7 field
   one per §5.4.1), Structure B 45-byte payload (run-in /
   framing / MRAG / subtitling — §5.5.2), FOOTER ID (`0x74`),
   16-bit big-endian FSC (§5.2), SDP CHECKSUM (§5.3 — sum mod
   256 = 0). Codec rejects: wrong identifiers, wrong format
   code, LENGTH/DC mismatch, wrong footer ID, corrupted SDP
   checksum, non-zero descriptor following zero (violation of
   §5.4.2 prefix rule), and build requests with >5 packets.
   Per §3 (cites SMPTE 344M) emits in the Y stream
   (`cBit=false`). Frame-sync policy: only `Play` passes
   through — `Drop` and `Repeat` both drop, because repeating
   would re-issue an identical FSC and confuse downstream loss
   detection. 20 codec cases + 5 value-type cases / 121
   assertions added. Multipacket (RDD 8 §6, `AncFormat::Op47Multipack`)
   is registered but not implemented at this layer — that's a
   future sibling codec.
4. **ST 2020 family** (parts 1–3) for Dolby audio metadata —
   ✅ **LANDED 2026-05-20** (ST 2020-2 Method A). New
   `AncSt2020Audio` value type (`DataTypeSt2020Audio = 0x6D`) +
   codec (`src/proav/anccodec_st2020audio.cpp`) round-trips the
   ST 2020-2 §5 wire format at DID 0x45 with SDIDs 0x01..0x09
   (channel-pair association per ST 2020-1 §7.1 Table 1).
   Byte-position audit against ST 2020-1 / ST 2020-2 verified for:
   DID 0x45 fixed (ST 2020-1 §7), SDID-to-channel-pair Table 1
   mapping (`NoAssociation`=0x01, `ChannelPair1_2`=0x02, …,
   `ChannelPair15_16`=0x09), Payload Descriptor bit layout
   (ST 2020-2 §5.4 Table 1: bit 7 COMPATIBILITY=0, bits 6/5
   Reserved=0, bits 4..3 VERSION=01b, bit 2 DOUBLE_PKT, bit 1
   SECOND_PKT, bit 0 DUPLICATE_PKT), DC1 = MDF + 1 (§5.3 single
   packet), 2-packet split for 254 < MDF ≤ 508 (§5.3
   `DC1 = n+1, DC2 = MDF - DC1 + 2`), Y-stream emission (§8 +
   ST 2020-1 §8: "ANC packets shall be carried in the Y stream"
   → `cBit=false`).  Multi-parser (`PROMEKI_REGISTER_ANC_MULTI_PARSER`)
   reassembles split frames, validates DOUBLE/SECOND bit sequence
   per §5.4.3, rejects: mismatched SDIDs across the pair, three+
   packet groups (§5.3 only defines two), or (Double=0) shape on
   what was claimed as a split. Single-packet parse returns
   `Error::InsufficientContext` on a (Double=1) packet so the
   dispatcher routes through `parseGroup`. Builder rejects:
   MDF > 508 bytes, SDID=0x00 (ST 2020-1 §7.1 reserved + RFC 8331
   §3.1 Type-1 collision). Frame-sync policy: Play/Repeat pass
   through (§5.4.4 expects same metadata on the second physical
   frame of a 50/60 Hz pair); Drop discards. 19 codec cases + 5
   value-type cases / 116 assertions added. **ST 2020-3 Method B**
   (Dolby E specific) and **deeper metadata-frame parsing** per
   ST 2020-1 §5 (sync segments / data segments / end-of-frame
   sync) remain out of scope at this codec layer — the library
   treats the metadata-frame bytes as opaque.
5. **ST 352:2013** VPID — ✅ **LANDED 2026-05-20.** Thin AncCodec
   wrapper (`src/proav/anccodec_vpid.cpp`) bridges the existing
   `SdiVpid` value type (`DataTypeSdiVpid = 0x69`) into the
   `AncTranslator` dispatch framework. Parser / builder delegate
   to `SdiVpid::fromSt291Packet` / `SdiVpid::toSt291Packet` so
   the wire-level enforcement (DID 0x41 / SDID 0x01 / DC=4, byte
   order, checksum) is shared with direct callers of those
   helpers — no duplicate wire logic. Builder honours
   `AncTranslateConfig::St291BuildLine` (default
   `UnspecifiedLine` = 0x7FE) and `St291FieldB`, rejects
   non-`SdiVpid` Variants with `Error::InvalidArgument`.
   Frame-sync policy: VPID is a steady-state link descriptor with
   no per-frame sequence state, so Play and Repeat pass the
   packet through and Drop discards (same shape as the AFD
   policy). 11 codec cases / 33 assertions added. ST 352:2013
   §6.2 recommended VANC line lookup is exposed via
   `SdiVpid::recommendedAncLine(fmt, field)`; callers wanting
   that placement pre-fill `St291BuildLine` from it.

# Decisions

The open questions from the first audit pass are answered below.
These are the working positions the fix-plan phases assume; they
are open for revisiting if the implementation surfaces something
unexpected.

### Q1. A2 — protected codes: **hard-fail.**

`makeWord()`'s pass-through path will reject any caller-supplied
10-bit word whose data byte lies in the protected ranges (the
upper-2-bit pattern `00` or `11` from a caller-supplied parity
that landed on a protected code), returning a new
`Error::ProtectedAncCode`.

Rationale: ST 291-1 §9.1 is a SHALL NOT. The pass-through path
exists so that callers can preserve their own parity bits across
a round-trip, *not* so they can paper over invalid wire bytes.
Silently masking-and-recomputing would convert a real caller bug
(or hostile input) into a wire packet that's bit-different from
what the caller asked for, which defeats the whole "byte-exact
replay" promise. Loud failure preserves the contract.

Project conventions cited: "Prefer specific error codes over
generic ones." Add `Error::ProtectedAncCode`.

### Q2. A4 — composite SDI: **decline by design.**

We do not implement composite ANC packets (ADF = 3FCh, ST 291-1
§5.3 / §7.2). Rationale: no current or planned backend ingests
composite-domain SDI; modern equipment (NTV2, NDI, RTMP,
ST 2110-40) all consume component or post-component data.
Implementing composite would add a code path with no test
coverage source and no production consumer.

Action: `docs/anc.md` (Phase F9) calls out the decline
explicitly. If a future composite-SDI requirement materialises,
the framework's existing ADF-strip-on-ingest abstraction can be
extended without breaking changes.

### Q3. C2 — keep-alive emission: **emit per ST 2110-40 §5.5.**

The packer emits a single keep-alive RTP packet (ANC_Count=0,
Length=0, Marker=1, F per session) for every video frame / field
with no ANC payload. The depacker accepts ANC_Count=0 packets as
end-of-frame markers, not droppable noise.

Rationale: ST 2110-40 §5.5 is a SHALL. RFC 8331 §2.1 explicitly
documents the keep-alive shape and its purpose. Any receiver
that breaks on a conforming keep-alive is non-conforming; that's
their bug to fix. The cost of *not* sending keep-alives is real
(downstream consumers can't distinguish "no ANC this frame"
from "ANC packet dropped in transit").

Migration risk: low. Receivers that ignore unknown-shape
payloads will silently ignore the keep-alive (no harm).
Receivers that crash on a 0-count payload are already broken
under any conforming sender.

### Q4. D1b — ATC rate hint: **sideband via `AncMeta::Atc::Rate` with cfg fallback.**

Hierarchy on parse:

1. **`AncMeta::Atc::Rate`** on the packet — set by the capture
   path when the paired video desc is known. Takes precedence.
2. **`AncTranslateConfig::AtcParseRateHint`** — application-level
   override for sources where the capture didn't stamp the rate
   (raw RTP receive, SDP-only context, file replay).
3. **Fail with `Error::InsufficientContext`** when neither is
   present. Do not silently default to 30 fps.

Rationale: The rate is a property of the *capture context*, not
of the ATC bytes themselves. Stamping it on the packet's meta
sidecar is semantically correct and mirrors how the library
already handles `AncMeta::St291::Line / HOffset / FieldB`. The
cfg fallback covers sources that genuinely don't know the rate
upfront. Silent 30-fps default is the worst option — produces
quiet wrong answers and is what we have today.

Add `Error::InsufficientContext` (specific error code per
project convention).

### Q5. D1c — ATC value type: **introduce `AncAtc`.**

Parse / build round-trip through a new value type
`AncAtc { Timecode tc; Array<uint8_t, 8> userBits; uint8_t
flags; uint8_t dbb2; }` (or equivalent). `Timecode` stays clean.

Rationale: `Timecode` is used in many places (libvtc-backed
across the entire pipeline); binary groups / color-frame /
BGF / DBB2 are *only* meaningful in ATC carriage. Per the
project's pattern (`FrameNumber` extracted as its own class
rather than bloating shared types), specialised data deserves a
specialised value type. Callers that only want the wall-clock
ignore the rest via `ancAtc.timecode()`.

Naming: `AncAtc` follows the existing `Cea708Cdp` / `HdrStatic2086`
codec-value-type convention. Lives in `include/promeki/ancatc.h`.

### Q6. B2 — default checksum policy: **`PreserveOrRecompute`.**

Default for both `St291Packet::from()` and `RtpPayloadAnc::unpackAncPackets`
is `PreserveOrRecompute`. Configurable per `AncTranslator` session;
production-grade ingest paths opt into `StrictValidate`; the unit
test harness defaults to `StrictValidate`.

Rationale: The whole `ancdata.md` "native wire form" architecture
depends on byte-exact replay of captured packets. A captured
packet with a stored-valid checksum must round-trip with that
exact checksum; a synthesised packet (no stored checksum) gets
one computed. `StrictValidate` is the right default for
production ingest but the wrong default for a general-purpose
library — too many real SDI captures contain occasional bit
errors that downstream codecs gracefully tolerate, and a hard
reject at the carriage layer makes the library overly fragile.

### Q7. D2a — `BarData` rename: **rename without compat shim.**

`AncFormat::BarData` is removed. `AncFormat::PanScan` is added
with DID=0x41, SDID=0x06, description "SMPTE 2016-4 Pan-Scan
Information". Bar data folds into the AFD codec (per ST 2016-3
§4 the AFD and Bar Data ride in the same packet).

Recordings that serialised the literal name `"BarData"` will
deserialise to `AncFormat::Invalid`. That's the correct
behaviour — the name was misleading, the registration was
spec-wrong, and the project policy on "no backwards-compat
shims" applies. Any value preserved under the old name was
already wrong (it was claiming to be ST 2016-3 Bar Data but
carrying a Pan-Scan SDID).

Migration: any in-repo test fixtures or persisted recordings
that use `"BarData"` need to be regenerated as part of the F4
landing.

# Acceptance criteria for "audit complete"

- All findings have a final disposition (fix / decline / defer).
- All F1–F8 items landed with tests (the wire-format bugs). ✅
- The remaining F9 item 2 (E4) + F10 work has a tracking entry here.
- `docs/anc.md` exists and references this audit. ✅ (2026-05-19)
- The fundamental layer (Phase A + B + C) has a property-based
  round-trip test that captures the byte-exact replay contract.
