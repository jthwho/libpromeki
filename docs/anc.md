# Ancillary Data Framework {#anc}

End-to-end ancillary-data carriage: SDI VANC, RFC 8331 RTP /
ST 2110-40, NDI XML, HDMI InfoFrame, RTMP AMF, MPEG-TS private,
HLS SEI.

The ancillary-data (ANC) framework is the portion of libpromeki
that carries non-picture, non-audio per-frame metadata across every
transport the library supports.  It is the home of CEA-708 closed
captions, ATC timecode, AFD / Pan-Scan signalling, HDR static and
dynamic metadata, SCTE-104 splice signals, ST 2020 Dolby audio
metadata, OP-47 subtitles, ST 2106 caption flags, ST 2031 VBI
carriage, VPID, KLV, SEI, and any user-registered format.

## Layering and core types {#anc_layering}

The framework has three layers.  Wire formats live at the bottom;
the carrier object @ref promeki::AncPacket sits in the middle as a
transport-agnostic envelope; typed parsers / builders live at the
top.

- **Wire layer.**  Transport-specific bit layouts: ST 291-1 §6
  packed 10-bit words for SDI VANC; RFC 8331 §2.2 per-packet
  records inside an RTP payload for ST 2110-40; HDMI CEA-861
  InfoFrames; RTMP AMF0 script tags; NDI XML metadata elements;
  MPEG-TS private sections; H.264 / HEVC user-data SEI messages.
  @ref promeki::St291Packet is the typed view onto the SDI 10-bit
  packing; the other transports have smaller wire wrappers next
  to their backend (see @ref promeki::HdmiInfoFrame for the HDMI
  side).
- **Carrier layer.**  @ref promeki::AncPacket is the single
  canonical handle.  It pairs the wire bytes with an
  @ref promeki::AncFormat (logical "what kind of data") and an
  @ref promeki::AncTransport (the current wire carrier) and
  exposes the framing fields each transport needs.  Multiple
  AncPackets aggregate into an @ref promeki::AncPayload riding on
  a @ref promeki::Frame; an @ref promeki::AncDesc on the Frame's
  MediaDesc declares the per-source "what's allowed and where to
  land it" contract.
- **Typed layer.**  Codecs parse / build typed Variants from a
  carrier.  @ref promeki::Cea708Cdp, @ref promeki::AncAtc,
  @ref promeki::AncAfd, @ref promeki::HdrStatic2086,
  @ref promeki::HdrDynamic2094_40, and the like.
  @ref promeki::AncTranslator dispatches across the registered
  codec set so callers do not have to map (format, transport)
  tuples to codec function pointers by hand.

## AncPacket — the canonical envelope {#anc_packet}

Every transit point inside the library — capture, parse, translate,
emit — passes through @ref promeki::AncPacket.  It is the only
type that can move ANC data across thread boundaries cheaply.

| Field                  | Provenance                                                                            |
|------------------------|---------------------------------------------------------------------------------------|
| `format()`             | Logical @ref promeki::AncFormat (Cea708, Afd, AtcLtc, ...).                           |
| `transport()`          | Wire @ref promeki::AncTransport (St291, RtpAncSt2110_40, ...).                        |
| `data()`               | Wire bytes — canonical post-ADF layout for ST 291; transport-specific shape otherwise.|
| `meta()`               | Rare / multi-byte transport keys (HDMI type, RTMP script name, ...).                  |
| `st291Line()` / etc.   | Hot-path ST 291 framing — direct fields on the `Impl` (not in meta).                  |

The handle is a CoW value-type backed by `SharedPtr<Impl>`: copies
are a refcount bump and mutators (`setFormat`, `setData`,
`setMeta`, `setSt291Line`, ...) detach into a fresh `Impl` when the
refcount is greater than one.  Pass AncPackets by value through a
pipeline; never by reference.

### Canonical data layout {#anc_packet_data}

For @ref promeki::AncTransport::St291 packets the buffer holds the
canonical **post-ADF** payload — DID, SDID (or DBN for Type-1),
DataCount, UDW..., Checksum — packed as 10-bit words MSB-first.
The Ancillary Data Flag (ADF, ST 291-1 §5.2: `000h 3FFh 3FFh` for
component or `3FCh` for composite) is **not** stored.  RFC 8331 §1
makes the same choice for the RTP carriage; the library's storage
shape matches the wire shape RFC 8331 carries on the network.

Backends that ingest raw SDI VANC (e.g. NTV2) MUST strip the ADF
before constructing the @ref promeki::AncPacket; backends that
emit to SDI MUST prepend the ADF themselves.  Transports that have
no ADF concept (RTP, NDI, RTMP, HDMI InfoFrame, MPEG-TS) see the
same canonical post-ADF bytes via @ref promeki::AncPacket::data.

### ST 291 framing fields {#anc_packet_st291}

Five hot-path framing fields live as direct accessors on
@ref promeki::AncPacket rather than as @ref promeki::Metadata keys
(the audit's F9.1 conversion):

| Accessor             | Default  | Meaning                                                      |
|----------------------|----------|--------------------------------------------------------------|
| `st291Line()`        | `0x7FE`  | VANC line; RFC 8331 §2.2 / RP 168 "switching-point default". |
| `st291HOffset()`     | `0xFFF`  | Horizontal offset within the line; "unspecified".            |
| `st291FieldB()`      | `false`  | F-bit: `true` on field 2 of an interlaced source.            |
| `st291CBit()`        | `false`  | C-bit: `true` on the chrominance data stream.                |
| `st291StreamNum()`   | `0`      | RFC 8331 StreamNum; non-zero on multi-link / 12G SDI.        |

The defaults match the RFC 8331 §2.2 sentinels for "unspecified"
so a freshly built packet round-trips cleanly through the carriage
layer without the codec stamping anything.  The RTP pack / unpack
hot path reads these once per packet (~9 k lookups/sec on HD-60);
direct fields keep that out of the @ref promeki::Metadata hash.

### Per-transport meta sidecar {#anc_packet_meta}

Other transports keep their framing in @ref promeki::AncPacket::meta
because the fields are either rare on the hot path or longer than
five bytes total:

| Namespace                  | Keys                              | When stamped                                                                                                          |
|----------------------------|-----------------------------------|-----------------------------------------------------------------------------------------------------------------------|
| `AncMeta::Atc`             | `Rate`                            | Capture stamps the source frame rate so the ATC parser can resolve a Timecode::Mode without `AtcParseRateHint`.       |
| `AncMeta::HdmiInfoFrame`   | `Type`, `Version`, `Length`       | HDMI ingest stamps the InfoFrame header bytes.                                                                        |
| `AncMeta::RtmpAmf`         | `ScriptName`                      | RTMP ingest preserves the AMF0 script-tag name.                                                                       |
| `AncMeta::NdiXml`          | `ElementName`                     | NDI ingest preserves the top-level XML element.                                                                       |
| `AncMeta::MpegTsPrivate`   | `Pid`, `TableId`                  | TS demux stamps the PID + table_id.                                                                                   |
| `AncMeta::HlsSei`          | `PayloadType`, `Uuid`             | HLS demux stamps the SEI payload type + unregistered-SEI UUID.                                                        |

## AncFormat — logical identity {#anc_format}

@ref promeki::AncFormat is the typereg-pattern handle for the
"what kind of data" question.  Each registered format carries:

- A short `name` (e.g. `"Cea708"`, `"AtcLtc"`).
- A human-readable `desc`.
- An @ref promeki::AncCategory (broad classification: Captions,
  Timecode, Splice, Aspect, Hdr, AudioMetadata, Display,
  Geolocation, PayloadId, UserDefined, Subtitles, Klv, Sei, Vbi).
- A `canonicalTransport` — the primary transport the data
  originates on.
- Per-transport identity bytes: `st291Did + st291Sdid`,
  `hdmiInfoFrameType`, `mpegTsTableId`.

### Lookup helpers {#anc_format_lookup}

| Helper                                                                  | Purpose                                                                |
|-------------------------------------------------------------------------|------------------------------------------------------------------------|
| @ref promeki::AncFormat::fromSt291DidSdid                               | Resolve a captured (DID, SDID) byte pair to an `AncFormat`.            |
| @ref promeki::AncFormat::fromHdmiInfoFrameType                          | Resolve an HDMI InfoFrame type byte (0x82 = AVI, 0x84 = Audio, ...).   |
| @ref promeki::AncFormat::fromMpegTsTableId                              | Resolve an MPEG-TS private-section table_id.                           |
| @ref promeki::AncFormat::fromName / @ref promeki::AncFormat::fromString | Resolve by registered name (the DataStream wire form).                 |
| @ref promeki::AncFormat::registeredIDsForCategory                       | Enumerate every format in a given category (indexed: O(1)).            |
| @ref promeki::AncFormat::registeredIDsForTransport                      | Enumerate every format that rides on a given transport (indexed: O(1)).|

The category / transport views are indexed at registration time so
they cost a single map lookup rather than rescanning the full
table.  Runtime registrations via
@ref promeki::AncFormat::registerData feed the indices the same
way.

### Wildcard SDIDs {#anc_format_wildcard}

Some formats span an SDID range under a single DID.  SMPTE 2020
Dolby metadata uses DID 0x45 with SDIDs 0x01–0x09 to carry
different sub-flavours, but the library represents them as one
@ref promeki::AncFormat::Smpte2020Audio entry.  These register
with `st291Sdid == 0` plus a `st291SdidRange` enumeration of the
concrete SDIDs; lookup matches exact (DID, SDID) first and falls
back to (DID, 0) on miss.  SDP fmtp emission expands the wildcard
into one `DID_SDID=` entry per concrete SDID — emitting
`SDID=0x00` verbatim would collide with the RFC 8331 §3.1 Type-1
sentinel and is therefore forbidden as a real per-packet SDID.

## AncTransport — wire carriers {#anc_transport}

Each registered transport names a wire carrier.  The current set:

| Transport                | Notes                                                                |
|--------------------------|----------------------------------------------------------------------|
| `St291`                  | SDI VANC, ST 291-1 / RFC 8331 / ST 2110-40 carriage (the post-ADF wire bytes are identical for SDI and RTP, so RFC 8331 RTP payloads ride this transport too — see [RFC 8331 carriage](#anc_rfc8331)). |
| `HdmiInfoFrame`          | CEA-861 InfoFrames over HDMI.                                        |
| `NdiXml`                 | NDI metadata frames (XML element).                                   |
| `RtmpAmf`                | RTMP AMF0 script tags (`onCaptionInfo`, `onCuePoint`, ...).          |
| `MpegTsPrivate`          | MPEG-TS private sections (PSI / PSIP / SCTE-35).                     |
| `HlsSei`                 | H.264 / HEVC user-data SEI carried in HLS.                           |

A given @ref promeki::AncFormat may have more than one valid
transport — CEA-708 captions for example ride natively on ST 291
SDI (CDP) and also surface on HDMI (via the SPD InfoFrame on some
sources), HLS (via user-data SEI), RTMP (via `onCaptionInfo` script
tags), and NDI (via XML metadata).  The format identity is
preserved across translation; only the carrier changes.

## RFC 8331 / ST 2110-40 carriage {#anc_carriage}

@ref promeki::RtpPayloadAnc implements the RFC 8331 payload format
for ST 2110-40 ANC carriage.  Highlights of the contract:

- **Payload header** (RFC 8331 §2.1).  Eight bytes:
  ESN (2) | Length (2) | ANC_Count (1) | F (2 bits) | reserved (14
  bits).  The library writes F per session
  (@ref promeki::RtpPayloadAnc::setKeepAliveField) and inherits
  the consensus F-bit from the carried records on regular frames.
- **Per-packet record** (§2.2).  Four-byte header carrying
  Line_Number (11 bits), Horizontal_Offset (12 bits), C-bit,
  S-bit, StreamNum, followed by the canonical post-ADF DID / SDID
  / DataCount / UDW... / Checksum bytes (packed 10-bit MSB-first).
  Trailing word_align bytes zero-pad to the next 32-bit boundary.
- **Sender ordering** (ST 2110-40 §5.2.2).  Sender emits records
  in ascending (Line, HOffset).  Sentinel-location records sort
  to the tail (their numeric value exceeds any real coordinate)
  with relative input order preserved via stable sort.
- **Audio-metadata filtering** (§5.2.1).  AudioMetadata-category
  packets are dropped with a warning on egress; this transport
  explicitly forbids them.
- **Keep-alive** (§5.5).  When a frame has no eligible ANC records
  the packer emits a single keep-alive RTP packet: ANC_Count=0,
  Length=0, Marker=1, F-bit per session.  The depacker accepts
  ANC_Count=0 with Length=0 as end-of-frame; ANC_Count=0 with
  non-zero Length is malformed and rejected.
- **F-bit value 0b01** (§2.1 reserved).  Depacker skips the entire
  RTP payload's records when it sees this value, per the §2.1
  receiver guidance.
- **Trailing pad over-run** (§2.2).  A record whose padded extent
  runs past the declared Length is warned but the record body
  itself is still parsed.

### SDP signalling {#anc_carriage_sdp}

@ref promeki::AncDesc::toSdp emits the ST 2110-40 §7 / RFC 8331
§3.1 fmtp parameter list.  Mandatory keys always present:

```
m=video <port> RTP/AVP 100
a=rtpmap:100 smpte291/90000
a=fmtp:100 SSN=ST2110-40:2018;TM=CTM;exactframerate=60000/1001;
           DID_SDID={0x61,0x01};DID_SDID={0x60,0x60};...
```

Optional keys emitted when non-zero:

| Parameter   | Source                          | Notes                                              |
|-------------|---------------------------------|----------------------------------------------------|
| `TROFF`     | @ref promeki::AncDesc::troff    | RTP timestamp offset in 90 kHz ticks; omitted at 0.|
| `VPID_Code` | @ref promeki::AncDesc::vpidCode | ST 352 VPID byte-1 of the paired video stream.     |

Wildcard-SDID formats expand into one `DID_SDID` entry per
concrete SDID (see @ref anc_format_wildcard).

## ST 291 packet contract {#anc_st291}

@ref promeki::St291Packet is the typed wrapper around an
@ref promeki::AncPacket with `transport() == St291`.  It owns the
10-bit packing / unpacking, parity, and checksum logic.

### Type-1 vs Type-2 packets {#anc_st291_type1}

ST 291-1 §5.1 splits ancillary packets into two types based on the
DID's high bit:

- **Type-2** (DID < 0x80).  Word 1 carries SDID — this is the
  common shape (CEA-708 captions, AFD, ATC, HDR, ...).
- **Type-1** (DID ≥ 0x80).  Word 1 carries DBN (Data Block
  Number), not SDID — used by ST 291-1 §6.3
  Packet-Marked-for-Deletion (DID=0x80) and a small set of
  reserved control packets.

The library exposes both forms cleanly:

```cpp
// Type-2 build (SDID carried in word 1):
St291Packet pkt = St291Packet::buildRaw(0x61, 0x01, udw, line);
assert(!pkt.isType1());
assert(pkt.sdid() == 0x01);

// Type-1 build (DBN carried in word 1; sdid() reports 0):
St291Packet del = St291Packet::buildRawType1(0x80, dbn, udw, line);
assert(del.isType1());
assert(del.sdid() == 0);
assert(del.dbn() == dbn);
```

RFC 8331 §3.1 mandates that the SDP `DID_SDID` parameter for a
Type-1 packet uses `0x00` as the SDID slot.  The
@ref promeki::AncFormat registry registers Type-1 formats
(@ref promeki::AncFormat::PacketForDeletion) with `st291Sdid == 0`
plus a `st291SdidRange` of `{0x00}` so the SDP emission produces
that sentinel correctly.

### Protected codes (ST 291-1 §9.1) {#anc_st291_protected}

The library enforces a hard-fail on protected codes.  ST 291-1
§9.1 SHALL NOT use 10-bit words whose data byte lies in the
protected ranges — the upper-2-bit pattern `00` or `11` from a
caller-supplied parity that lands on a protected code.

@ref promeki::St291Packet::buildRaw's pass-through path rejects
any caller-supplied 10-bit word that violates this rule and
returns @ref promeki::Error::ProtectedAncCode.  This preserves the
byte-exact-replay contract — silently masking would convert a
real caller bug (or hostile input) into a wire packet that differs
from what the caller asked for.

The pass-through path exists so callers can preserve their own
parity bits across a round trip, not so they can paper over
invalid wire bytes.  Synthesised parity (computed by the library
from an 8-bit data byte) never lands on a protected code, so
typical builders never trigger this error.

### Checksum policy {#anc_st291_checksum}

@ref promeki::AncChecksumPolicy governs how the ST 291-1 §6.4
Checksum_Word is treated on promotion:

- `PreserveOrRecompute` (default).  Accept the packet regardless
  of the stored checksum.  Preserves byte-exact replay for
  captured packets that may contain occasional bit errors.
- `AlwaysRecompute`.  Same behaviour as `PreserveOrRecompute` on
  the parse path; only differs at emit time.
- `StrictValidate`.  RFC 8331 §7 SHOULD-check: validate that the
  stored Checksum_Word equals the value recomputed over
  (DID, SDID, DataCount, UDW) per ST 291-1 §6.4.  On mismatch the
  promotion fails with @ref promeki::Error::InvalidChecksum.

@ref promeki::AncTranslator sessions opt into `StrictValidate`
via the `AncTranslateConfig::Checksum` key; the unit-test harness
defaults to `StrictValidate` so accidental regressions surface
loudly there.  Production-grade ingest also opts in; the
library's default at the generic entry point stays tolerant
because too many real SDI captures contain occasional bit errors
that downstream codecs gracefully tolerate.

## Codec catalogue {#anc_codecs}

Codec is the term for "the thing that parses an
@ref promeki::AncPacket into a typed Variant or builds an
@ref promeki::AncPacket from one."  Each codec registers with
@ref promeki::AncTranslator under a (format, transport) tuple.

| Format                              | Codec source                                  | Notes                                                                          |
|-------------------------------------|-----------------------------------------------|--------------------------------------------------------------------------------|
| `Cea708`                            | @ref promeki::Cea708Cdp + anccodec_cea708.cpp | SMPTE 334-2 CDP, ST 291 transport.                                             |
| `Afd`                               | @ref promeki::AncAfd + anccodec_afd.cpp       | ST 2016-3 AFD + Bar Data, ST 291.                                              |
| `AtcLtc` / `AtcVitc1` / `AtcVitc2`  | @ref promeki::AncAtc + anccodec_atc.cpp       | SMPTE 12M-2 timecode; rate sideband via `AncMeta::Atc::Rate`.  See @ref anc_atc_carriage. |
| `AtcHfrtc`                          | @ref promeki::AncAtc + anccodec_atc_hfrtc.cpp | SMPTE ST 12-3 HFR timecode (DID=0x60, SDID=0x61); the only conformant carriage at ≥72 fps. |
| `HdrStatic2086`                     | anccodec_hdrstatic_st291.cpp                  | ST 2108-1 Frame Type 1 mastering display metadata.                             |
| `HdrDynamic2094_40`                 | anccodec_hdrdynamic2094_40_st291.cpp          | ST 2094-40 dynamic HDR (HDR10+) KLV.                                           |
| `Vpid`                              | @ref promeki::SdiVpid                         | ST 352 Video Payload Identifier.                                               |
| `PacketForDeletion`                 | (none yet)                                    | Type-1 sentinel; the framework supports the carriage.                          |

Codec-level byte-position conformance is verified for the
HDR-static SEI-style Frame Types (ST 2108-1 §5.3.2–§5.3.5), the
HDR-dynamic KLV multi-packet concatenation (ST 2108-2 §5.3 /
§5.4), RDD 8 OP-47 SDP, ST 2020 Dolby Method A, and ST 352 VPID.
Pending future demand: ST 2020-3 Method B (Dolby E specific) and
the RDD 8 §6 OP-47 Multipacket variant.

## AncPayload and AncDesc {#anc_payload}

@ref promeki::AncPayload aggregates the @ref promeki::AncPacket
list for one moment in time (typically per @ref promeki::Frame).
It is a payload like @ref promeki::VideoPayload /
@ref promeki::AudioPayload and lives on the Frame's payload list.

@ref promeki::AncDesc is the descriptor that goes on the
@ref promeki::MediaDesc for a source / sink.  It declares:

- `sourceRaster`, `scanMode`, `frameRate` — context the receiver
  needs to interpret the packets (line numbers, F-bit,
  frame-rate-dependent codecs like ATC).
- `allowedFormats` — explicit per-format allow-list, or empty to
  accept any registered format.
- `allowedCategories` — broader category-level allow-list.
- `pairedVideoStreamIndex` / `pairedAudioStreamIndex` — when the
  ANC stream is bound to a specific video / audio stream on the
  enclosing Frame.
- `troff`, `vpidCode` — ST 2110-40 §7 SDP carriers.

## Translation across transports {#anc_translation}

The same @ref promeki::AncFormat can ride on multiple
@ref promeki::AncTransport carriers.  Translation is the act of
moving an @ref promeki::AncPacket from one transport to another
without re-encoding the payload semantics.

The model: each codec exposes both a parser (transport bytes →
typed Variant) and a builder (typed Variant → transport bytes).
The translator chains the two when the source transport's parser
and the destination transport's builder are both registered for
the same logical format:

```cpp
AncTranslator t;
AncPacket inbound = ...; // CEA-708 on ST 291 from an SDI capture
Result<AncPacket::List> outbound =
    t.translate(inbound, AncTransport::RtpAncSt2110_40);
// outbound holds the same Cea708 payload on RFC 8331 RTP.
```

When a (format, transport) tuple has no registered codec the
@ref promeki::AncTranslator falls back to a wire-bytes-only
translation: it preserves `data()` and re-stamps the transport.
This is enough when the destination transport's wire format
matches the source's (the common case for ST 291 ↔ RFC 8331,
which share the post-ADF 10-bit packing).

## ATC carriage — ST 12-2 vs ST 12-3 {#anc_atc_carriage}

ATC (Ancillary Timecode) covers two related-but-distinct ST 291
carriages.  Both share DID=0x60 and the 16-UDW data layout, but
they target different frame-rate regimes:

- **ST 12-2:2014 + Am1:2013** — the classical carriage.  SDID=0x60
  for all three flavours (LTC / VITC1 / VITC2).  Discriminator is
  the DBB1 payload-type byte (0x00 / 0x01 / 0x02).  Covers base
  rates (≤30 fps) and the four "pair-rate" HFR cases (48 / 50 /
  60 / 59.94) via the §9.2 Am1 frame-mark mechanism.
- **ST 12-3:2016** — the high-frame-rate carriage.  SDID=0x61.
  DBB1 = 0x80..0x8F where the low nibble is the bitstream number
  (multiple parallel HFR timecode streams can coexist per video
  frame).  Covers the ≥72 fps rates (72 / 96 / 100 / 120 /
  119.88) where the single-bit field-mark of ST 12-2 Am1 can't
  disambiguate N>2 sub-frames per super-frame.

### Carriage decision tree {#anc_atc_carriage_decision}

| Wall-clock rate   | Carriage                          | DID  | SDID | DBB1 range  |
|-------------------|-----------------------------------|------|------|-------------|
| ≤30 fps           | ST 12-2 (LTC / VITC1 / VITC2)     | 0x60 | 0x60 | 0x00 / 0x01 / 0x02 |
| 48, 50, 59.94, 60 | ST 12-2 pair-rate (VITC1 + VITC2) | 0x60 | 0x60 | 0x01 / 0x02 |
| 72, 96, 100       | ST 12-3 ATC_HFRTC                 | 0x60 | 0x61 | 0x80..0x8F  |
| 119.88, 120       | ST 12-3 ATC_HFRTC                 | 0x60 | 0x61 | 0x80..0x8F  |

The library uses @ref promeki::ancAtcIsPairHfrRate and @ref
promeki::ancAtcIsHfrtcRate to express the split at the codec
layer.  The @ref promeki::AncAtc::atcVitcFormatForFrame helper
returns the right VITC1/VITC2 alternation index for pair-rate
emission; at HFRTC rates it returns the same alternation but the
result is informational only (ATC_HFRTC is the conformant path
at those rates).

### DBB1 reference {#anc_atc_dbb1}

DBB1 is byte 1 of the ATC packet (carried as bit 3 of UDWs 1..8,
LSB-first across the eight UDWs).

| DBB1 value     | Meaning                              | Spec reference          |
|----------------|--------------------------------------|-------------------------|
| `0x00`         | ATC_LTC (LTC time address)           | ST 12-2 §6.2.1, Table 2 |
| `0x01`         | ATC_VITC1 (first VITC field)         | ST 12-2 §6.2.1, Table 2 |
| `0x02`         | ATC_VITC2 (second VITC field)        | ST 12-2 §6.2.1, Table 2 |
| `0x03..0x05`   | User-defined range                   | ST 12-2 Table 2         |
| `0x06`         | Film-data reader                     | ST 12-2 Table 2         |
| `0x07`         | Production-data reader               | ST 12-2 Table 2         |
| `0x08..0x7C`   | Locally-generated range              | ST 12-2 Table 2         |
| `0x7D`         | Video-tape data (local)              | ST 12-2 Table 2         |
| `0x7E`         | Film data (local)                    | ST 12-2 Table 2         |
| `0x7F`         | Production data (local)              | ST 12-2 Table 2         |
| `0x80..0x8F`   | ATC_HFRTC, bitstream 0..15           | ST 12-3 §10.1           |

The libpromeki enum @ref promeki::AncAtc::PayloadType names the
five entries the codecs handle: `Ltc`, `Vitc1`, `Vitc2`, and
`HfrtcBase` (= 0x80, the first of the 16-element HFRTC range).
@ref promeki::AncAtc::isHfrtcPayload and @ref
promeki::AncAtc::hfrtcBitstream split the HFRTC range out for
convenience.

### DBB2 reference {#anc_atc_dbb2}

DBB2 is byte 2 of the ATC packet (UDWs 9..16 bit 3, LSB-first).
Its bit semantics depend on which carriage applies — the same
byte means different things under ST 12-2 vs ST 12-3.

**ST 12-2 (LTC / VITC1 / VITC2) — Table 3:**

| Bits | Field                  | Helper                                         |
|------|------------------------|------------------------------------------------|
| 0..4 | VITC line-select (HDTV: 0 = "don't care") | @ref promeki::AncAtc::dbb2DecodeVitc, @ref promeki::AncAtc::dbb2EncodeVitc |
| 5    | Line-duplication flag  | (ditto)                                        |
| 6    | 0 = valid, 1 = interpolated | (ditto)                                   |
| 7    | 0 = processed, 1 = retransmitted | (ditto)                              |

**ST 12-3 (ATC_HFRTC) — §9.2.2:**

| Bits | Field                                      | Helper                                         |
|------|--------------------------------------------|------------------------------------------------|
| 0..4 | N (3, 4, or 5 — ST 12-3 super-frame group size) | @ref promeki::AncAtc::dbb2DecodeHfrtc, @ref promeki::AncAtc::dbb2EncodeHfrtc |
| 5..6 | Super-frame rate (00 = 24, 01 = 25, 10 = 30) | (ditto)                                      |
| 7    | Reserved (0)                               | (ditto)                                        |

The `(super-frame count, N)` tuple uniquely identifies every
standard ST 12-3 format — receivers don't need a separate rate
hint to resolve the libvtc format from a captured HFRTC packet.
The single ambiguity (NDF120 vs DF120 — both have super-frame
count = 30 and N = 4) is broken by the codeword's drop-frame bit
at codeword position 10.

### Worked example — 60p pair-rate {#anc_atc_60p_pair_example}

At 60p one wall-clock second contains 60 physical frames numbered
0..59.  ST 12-1 §12 / ST 12-2 §9.2 Am1 packs them as 30 pairs
sharing the same BCD super-frame digits (= pair-index in the
0..29 range), distinguished by a single field-mark bit in the
codeword.

Mapping for physical frame 9 (the example used in the round-trip
test suite):

- `pair_index = 9 / 2 = 4` → wire frame_tens = 0, frame_units = 4.
- `field_mark = 9 % 2 = 1` → UDW 7 b7 = 1 (the "polarity slot"
  that ST 12-2 Am1 reinterprets as the field-mark at pair-rate).
- Format ID = `AtcVitc2` (per `atcVitcFormatForFrame(60, 9)` —
  even frames use VITC1, odd use VITC2).

The receiver reads:

- frame digits (0, 4) → `pair_index = 4`.
- UDW 7 b7 → `field_mark = 1`.
- `physical_frame = 4 × 2 + 1 = 9`.

If `AncTranslateConfig::AtcVitcLegacyFieldMark` is set, the
encoder clears the field-mark bit unconditionally (ST 12-2 Am1
grandfathers this as compliant for pre-Am1 receivers).  Round-trip
through such a stream loses one bit of sub-frame phase: pair 9
collapses to pair 8 on decode.

### Worked example — 120p HFRTC {#anc_atc_120p_hfrtc_example}

At 120p (NDF120 = 30×4) one second contains 120 physical frames
numbered 0..119, grouped into 30 super-frames of 4 sub-frames each.

Mapping for physical frame 47:

- `super_frame = 47 / 4 = 11` → wire frame_tens = 1, frame_units = 1.
- `sub_frame_index = 47 % 4 = 3` → libvtc packs it as
  `(sf_1, sf_2) = (1, 1)` per ST 12-3 Table 3 for the 30×N family.
- DBB1 = 0x80 + bitstream_number (default bitstream 0 → 0x80).
- DBB2 = 0x44 (super-frame count = 30 → 0b10 << 5; N = 4 → 0b00100).

The sub-frame identifier bits land at codeword bit positions per
ST 12-3 Table 3:

| Family       | bit 11 | bit 27 | bit 43 | bit 58 | bit 59 |
|--------------|--------|--------|--------|--------|--------|
| 30×N (120p)  | sf_2   | sf_1   | 0      | 0      | 0      |
| 24×N (72/96) | sf_2   | sf_1   | 0      | 0      | 0      |
| 25×N (100p)  | sf_2   | 0      | 0      | 0      | sf_1   |
| 24×5 (120p)  | sf_2   | sf_1   | sf_3   | 0      | 0      |

For physical frame 47 at NDF120 → bit 11 = 1, bit 27 = 1, bit 43
= 0, bit 58 = 0, bit 59 = 0.

The 25×N swap (sf_1 at bit 59 instead of bit 27) is the
load-bearing wire-position difference between the 25×N and
24×N / 30×N families.  Receivers MUST consult DBB2 to recover the
format before walking sub-frame bits.

### What HFRTC strips on the wire {#anc_atc_hfrtc_strip}

ST 12-3 §6.2 reassigns the bit positions ST 12-1 used for the
color-frame flag (bit 11) and the BGF triple (bits 43 / 58 / 59)
to the sub-frame identifier bits.  Consequently:

- `Timecode::colorFrame()` does NOT survive an HFRTC round-trip
  (the bit slot is reused for sf_2).
- `Timecode::userbits().mode()` (the BGF triple) does NOT survive
  an HFRTC round-trip.
- The eight user-bit nibbles DO survive — they live at separate
  bit positions (4-7, 12-15, 20-23, 28-31, 36-39, 44-47, 52-55,
  60-63) that ST 12-3 leaves alone.

Callers that need per-physical-frame BGF semantics at HFR rates
must use an out-of-band channel — the ATC codeword cannot carry
them.

## Design decisions {#anc_decisions}

The library's ANC framework was reviewed against the relevant
SMPTE and IETF standards (ST 291-1, RP 291-2, RFC 8331,
ST 2110-40, ST 12-1 / -2 / -3, ST 334-1 / -2, ST 352, ST 2016-3 /
-4, ST 2020-1 / -2, ST 2086, ST 2094-*, ST 2106, ST 2108-1 / -2,
RDD 8).  Codec-level byte-position conformance has been verified
for the HDR-static SEI-style Frame Types (ST 2108-1 §5.3.2 /
§5.3.3), the HDR-dynamic KLV multi-packet path (ST 2108-2 §5.3 /
§5.4 + ST 2094-2 Tables 10-11), RDD 8 OP-47 SDP, ST 2020 Dolby
Method A, and ST 352 VPID.

Notable design choices the library makes:

- Protected codes (ST 291-1 §9.1) hard-fail with
  @ref promeki::Error::ProtectedAncCode rather than silently
  masking — preserves the byte-exact replay contract.
- Composite SDI (ADF = `3FC`) is declined by design — no current
  or planned backend ingests composite-domain SDI.
- ST 2110-40 §5.5 keep-alives are emitted for every video frame
  / field with no ANC payload.
- ATC rate hint: sideband via `AncMeta::Atc::Rate`, fallback to
  `AncTranslateConfig::AtcParseRateHint`, then
  @ref promeki::Error::InsufficientContext rather than a silent
  30-fps default.
- ATC carries its non-Timecode fields in a dedicated
  @ref promeki::AncAtc value type rather than overloading
  @ref promeki::Timecode (whose fields cover the time-address
  word, including the BGF mode triple and color-frame flag — see
  @ref timecode_physical_frame).
- ATC supports both ST 12-2 (≤30 + pair-rate HFR) and ST 12-3
  (≥72 fps) carriages.  The codec picks per source rate; receivers
  parse both.  See @ref anc_atc_carriage.
- Default checksum policy is `PreserveOrRecompute` (see
  @ref anc_st291_checksum).
- `AncFormat::PanScan` (DID 0x41 / SDID 0x06) carries the
  ST 2016-4 Pan-Scan registration; Bar Data folds into the AFD
  codec because ST 2016-3 §4 carries both in one packet.

@see promeki::AncPacket, promeki::AncFormat, promeki::AncTransport,
     promeki::AncCategory, promeki::AncPayload, promeki::AncDesc,
     promeki::AncTranslator, promeki::St291Packet,
     promeki::RtpPayloadAnc, promeki::Cea708Cdp, promeki::AncAtc,
     promeki::AncAfd, promeki::SdiVpid, @ref captions,
     @ref timecode
