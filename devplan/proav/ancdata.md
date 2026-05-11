# Ancillary Data (ANC) Stack

**Library:** `promeki` (proav + network)
**Standards:** All work follows `CODING_STANDARDS.md`. Every new
class requires complete doctest coverage. See `devplan/README.md`
for the full requirements.

The goal is end-to-end ancillary-data plumbing: capture ST 291 ANC
packets (eventually from AJA NTV2 SDI, today from RTP ST 2110-40
and NDI), carry them through `MediaPipeline` losslessly so they
can be re-emitted bit-exact, translate them into the equivalent
side channel on every other supported wire format, and expose
typed parsers/builders on top so application code (closed-caption
overlay, scte-104 driven splicers, AFD-aware scalers, ATC
round-trip) does not have to touch raw 10-bit words.

The plan lands in dependency order: foundation types first
(payload, descriptor, ANC packet value type, well-known type
registry), then the ST 2110-40 wire format, then the translator
registry, then typed parsers for the four first-class ANC kinds,
then MediaIO backend integration, then application surfaces.
None of this requires the AJA NTV2 backend — when that lands it
slots into the contract defined here.

## Decisions

These are settled scope answers that the rest of this plan
assumes — captured here so reviewers don't re-litigate them
mid-implementation.

- **Payload shape:** one `AncillaryPayload` per `Frame`, holding
  a list of ST 291 ANC packets. Symmetric with one
  `VideoPayload` carrying one frame of pixels. Per-frame RTP
  marker semantics map cleanly, and `MediaDesc::ancList` stays
  one-entry-per-stream the way `imageList` / `audioList` are.
- **First-class ANC types (with typed parsers/builders):**
  - SMPTE 334-1/2 CEA-708 & CEA-608 closed captions
    (DID 0x61, SDID 0x01/0x02)
  - SMPTE 2016 AFD + Bar Data (DID 0x41, SDID 0x05/0x06)
  - SMPTE 12M-2 ATC LTC / VITC timecode
    (DID 0x60, SDID 0x60/0x61)
  - SCTE-104 ad/program markers (DID 0x41, SDID 0x07)
- **ANC format identity:** the logical "what kind of ANC is
  this" (CEA-708, AFD, ATC, SCTE-104, …) is a TypeRegistry-
  style `AncFormat` class, parallel to `PixelFormat` and
  `AudioFormat`: lightweight wrapper around an immutable
  `Data` record, integer `ID` enum for the well-known set,
  `UserDefined` extension point for runtime registration.
  Translators, typed parsers, and SDP fmtp emission all
  resolve through this single identity.
- **Cross-format translation:** a registry keyed by
  `(AncFormat::ID, AncTransport src, AncTransport dst)`. The
  `AncTransport` enum names the carrier (`AncPacket`,
  `Internal`, `NdiMetadata`, `RtmpScript`, `MxfKlv`,
  `HlsSei`). Each translator is independently unit-testable;
  backends look up by key and translate at the seam — no ANC
  parsing lives inside `NdiMediaIO` / `RtmpMediaIO` proper.
- **SDI capture device:** out of scope for this devplan. AJA
  NTV2 is the planned future SDI source; the ANC stack lands
  with a documented contract (Phase 5) so AJA NTV2 ingest is
  a drop-in producer when it arrives.

---

## Phase 0 — Foundation types

The minimum substrate the rest of the plan depends on. No wire
formats yet; no backend integration yet.

### `AncFormat` (TypeRegistry-style format registry)

A lightweight inline wrapper around an immutable `Data`
record identified by an integer `ID`, parallel in every
respect to `PixelFormat` and `AudioFormat`. Well-known IDs
ship with the library; user code can register additional
formats at runtime via `registerType()` + `registerData()`.

**Files:**
- [ ] `include/promeki/ancformat.h`
- [ ] `src/proav/ancformat.cpp`
- [ ] `tests/unit/proav/ancformat.cpp`

**Built-in IDs (initial set; grows as parsers land):**
- [ ] `Invalid = 0`
- [ ] `Cea708 = 1` — SMPTE 334-2 CEA-708 captions
  (DID 0x61, SDID 0x01)
- [ ] `Cea608 = 2` — SMPTE 334-1 CEA-608 captions
  (DID 0x61, SDID 0x02)
- [ ] `Afd = 3` — SMPTE 2016-3 AFD (DID 0x41, SDID 0x05)
- [ ] `BarData = 4` — SMPTE 2016-3 Bar Data
  (DID 0x41, SDID 0x06)
- [ ] `Scte104 = 5` — SCTE-104 ad/program markers
  (DID 0x41, SDID 0x07)
- [ ] `AtcLtc = 6` — SMPTE 12M-2 ATC LTC
  (DID 0x60, SDID 0x60)
- [ ] `AtcVitc1 = 7` — SMPTE 12M-2 ATC VITC1
  (DID 0x60, SDID 0x61)
- [ ] `AtcVitc2 = 8` — SMPTE 12M-2 ATC VITC2
  (DID 0x60, SDID 0x62)
- [ ] `Smpte2020Audio = 9` — Dolby audio metadata
  (DID 0x45, SDID 0x01–0x09 — registered as separate
  IDs as needs arise, or as a single ID with a sub-type
  field in `Data`)
- [ ] `UserDefined = 1024` — first ID available to user
  registrations.

**`AncFormat::Data` (immutable record):**
- [ ] `ID id`
- [ ] `String name` — short canonical name
  (e.g. `"Cea708"`); resolves through `fromName`.
- [ ] `String desc` — human-readable description.
- [ ] `uint8_t did`, `uint8_t sdid` — canonical DID/SDID
  pair the registry uses for `fromDidSdid` lookup.
- [ ] `AncTransport canonicalTransport` — almost always
  `AncTransport::AncPacket`; nominal "where this format
  lives natively" hint. Lets translators pick a default
  source when the user passes only a format identifier.
- [ ] (Optional, late-phase) function pointers for fast-path
  `AncPacket ↔ Internal` decode/encode. Initial
  implementation keeps these in translator classes only;
  the `Data` field is reserved.

**Class surface:**
- [ ] `AncFormat()` — invalid format.
- [ ] `AncFormat(ID id)` — resolves to the registered `Data`
  for `id`; invalid if not registered.
- [ ] `explicit AncFormat(const String &name)` — name
  lookup, mirroring `PixelFormat`.
- [ ] `static ID registerType()` — allocates a new ID at
  `UserDefined` or above.
- [ ] `static void registerData(Data &&)` — installs an
  immutable record.
- [ ] `static IDList registeredIDs()` — every registered
  ID, well-known and user. Required for the `using IDList`
  TypeRegistry convention.
- [ ] `static AncFormat fromDidSdid(uint8_t did, uint8_t sdid)`
  — primary "what is this raw ANC packet" lookup; returns
  an invalid `AncFormat` when the pair has no registered
  mapping.
- [ ] `static AncFormat fromName(const String &)` and
  `static AncFormat::ID idFromName(const String &)`.
- [ ] `ID id() const`, `const String &name() const`,
  `const String &desc() const`, `uint8_t did() const`,
  `uint8_t sdid() const`, `bool isValid() const`.
- [ ] `bool operator==(AncFormat) const`, `operator!=`.
- [ ] `DataStream` operators (writes the integer ID; reads
  back to a registered ID; mirrors `PixelFormat`).
- [ ] Variant type integration: `TypeAncFormat` Variant type
  so `Metadata`/`VariantDatabase` can carry an `AncFormat`
  natively without string conversion.

**Doctests:**
- [ ] Construction by ID, by name, default invalid.
- [ ] `registerType` returns unique IDs that don't collide
  with the well-known range.
- [ ] `registerData` installs and is retrievable.
- [ ] `fromDidSdid` for every well-known pair plus an
  unregistered pair (returns invalid).
- [ ] `registeredIDs` enumerates the full set.
- [ ] DataStream + Variant round-trip.

### `AncPacket` (value type)

ST 291 ANC packet, post-decoded from the wire. Plain value
class — small enough to copy, big enough to need its own header.

**Files:**
- [ ] `include/promeki/ancpacket.h`
- [ ] `src/proav/ancpacket.cpp`
- [ ] `tests/unit/proav/ancpacket.cpp`

**Fields:**
- [ ] `uint8_t did`, `uint8_t sdid` — raw 8-bit values (the
  parity-stripped 10-bit DID/SDID; the logical `AncFormat`
  is derived on demand via `AncFormat::fromDidSdid`).
- [ ] `uint16_t lineNumber` — ST 291 line number (11 bits);
  `0` when unknown / from a wire format that does not carry
  one (e.g. NDI captions translated in).
- [ ] `uint16_t horizontalOffset` — 0x000 if unspecified,
  0xFFF = unknown per RFC 8331.
- [ ] `bool fieldB` (a.k.a. F bit) — interlaced field
  identification.
- [ ] `bool cBit` — RFC 8331 color-difference channel flag
  (set for ANC carried on the C channel of SD/HD-SDI).
- [ ] `uint8_t streamNum` — RFC 8331 stream identifier
  (multi-link SDI / 12G sub-image). Zero for single-link.
- [ ] `List<uint16_t> userDataWords` — the 10-bit UDW
  payload, stripped of parity bits but otherwise verbatim.
- [ ] `uint16_t checksum` — recomputable from UDW + DID/SDID,
  but stored so a captured-then-replayed packet round-trips
  bit-exact even when the source had a checksum we don't
  understand.

**Methods:**
- [ ] `AncFormat format() const` — lookup via
  `AncFormat::fromDidSdid`; returns an invalid `AncFormat`
  when no well-known mapping exists (raw DID/SDID still
  works for forwarding).
- [ ] `bool isType1() const` — ST 291 type-1 packet
  (high bit of DID set).
- [ ] `uint16_t computedChecksum() const` — recompute per
  ST 291 §6.4 for validation tests.
- [ ] `bool checksumValid() const`.
- [ ] `Error encodeUdwBytes(Buffer &out, MemDomain domain
  = MemDomain::Host) const` — pack 10-bit UDW into the
  RFC 8331 word-aligned wire form (caller chooses padding
  rules; helpers in `ancwire.h` below build on this).
- [ ] `static Error decodeUdwBytes(const void *p, size_t bits,
  List<uint16_t> &out)` — inverse.
- [ ] `DataStream` operators `<<` / `>>` for pipeline
  serialization (round-trips every field including raw
  checksum).
- [ ] `bool operator==` / `operator!=`.
- [ ] String-style `toString(verbose=false)` for diagnostics.

**Doctests:** packet construction, checksum compute/validate,
type lookup, UDW 10-bit pack/unpack at every byte alignment
edge, `DataStream` round-trip, equality, type predicates.

### `AncDesc` (descriptor)

Parallel to `ImageDesc` / `AudioDesc`. Describes a single ANC
stream's shape — *not* the per-frame ANC packets themselves.
The descriptor is what `MediaDesc::ancList` carries and what
SDP `m=` sections round-trip.

**Files:**
- [ ] `include/promeki/ancdesc.h`
- [ ] `src/proav/ancdesc.cpp`
- [ ] `tests/unit/proav/ancdesc.cpp`

**Fields:**
- [ ] `Size2Du32 sourceRaster` — the video raster the ANC was
  associated with (e.g. 1920×1080). `(0,0)` if unbound. Lets a
  consumer interpret line numbers without consulting the
  paired `ImageDesc`.
- [ ] `VideoScanMode scanMode` — interlaced/progressive,
  required for correct line interpretation.
- [ ] `FrameRate frameRate` — same convention as `ImageDesc`;
  drives ST 2110-40 packet timing.
- [ ] `AncFormat::IDList allowedFormats` — empty = no
  restriction; non-empty = whitelist used by sinks that only
  carry a subset (e.g. an NDI metadata channel that only
  forwards captions). `IDList` not `List<AncFormat>` so the
  descriptor stays trivially serializable through the same
  registry resolution path PixelFormat/AudioFormat use.
- [ ] `Metadata metadata` — per-stream metadata container,
  matching ImageDesc / AudioDesc.
- [ ] Standard `PROMEKI_SHARED_FINAL` layout: `::Ptr`, `::List`,
  `::PtrList`. (See `dataobjects.dox` for the category.)

**Methods:**
- [ ] `bool isValid() const` — must have a valid scan mode and
  a positive raster *or* an unbound `(0,0)` raster paired with
  an explicit `allowedFormats`.
- [ ] `bool formatEquals(const AncDesc &) const` — ignores
  metadata, mirrors `ImageDesc::formatEquals`.
- [ ] SDP round-trip: `static AncDesc fromSdp(const
  SdpMediaDescription &)`, `SdpMediaDescription toSdp() const`
  using RFC 8331 `smpte291` rtpmap and the
  `VPID_Code=` / `DID_SDID=` fmtp grammar.
- [ ] `DataStream` operators.
- [ ] `operator==` / `!=`.

### `AncillaryPayload` (MediaPayload subclass)

The concrete `MediaPayload` that flows through the pipeline.
Inherits directly from `MediaPayload` (no abstract
"AncPayload" intermediate — there is no compressed/uncompressed
split to bridge, unlike video/audio).

**Files:**
- [ ] `include/promeki/ancillarypayload.h`
- [ ] `src/proav/ancillarypayload.cpp`
- [ ] `tests/unit/proav/ancillarypayload.cpp`

**Layout:**
- [ ] Inherits `MediaPayload`; uses `PROMEKI_SHARED_FINAL`-style
  CoW (`PROMEKI_REGISTER_MEDIAPAYLOAD` fourcc `"ANCp"`).
- [ ] Member `AncDesc _desc` (owns the stream descriptor).
- [ ] Member `List<AncPacket> _packets` (ordered per emission
  order — for SDI this is "as encountered scanning down VANC").
- [ ] No backing `BufferView` — the ANC packets are the data.
  `MediaPayload::data()` returns an empty `BufferView`; the
  serializer hooks (`serialisePayload`/`deserialisePayload`)
  write/read the packet list directly.

**Overrides:**
- [ ] `kind() const → MediaPayloadKind::AncillaryData`
- [ ] `isCompressed() const → false`
- [ ] `metadata()` forwards to `_desc.metadata()` (matches
  VideoPayload pattern).
- [ ] `hasDuration() const → true`; `duration()` returns the
  stamped frame duration (set by producer / MediaIO fill site,
  same convention as VideoPayload).
- [ ] `subclassFourCC() const → 'ANCp'`.
- [ ] `_promeki_clone() const` — deep clone (packet list copy).
- [ ] `serialisePayload(DataStream &)` writes desc + packets.
- [ ] `deserialisePayload(DataStream &)` inverse.
- [ ] `variantLookup*` via `PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH`;
  scalar keys expose `packetCount`, `hasCaptions`, `hasTimecode`,
  `hasAfd`, etc. as lookup-driven booleans for `VariantQuery`
  expressions.

**Methods:**
- [ ] `const AncDesc &desc() const`, `AncDesc &desc()`,
  `void setDesc(const AncDesc &)`.
- [ ] `const List<AncPacket> &packets() const`,
  `List<AncPacket> &packets()`, `void addPacket(AncPacket)`,
  `void clearPackets()`.
- [ ] `AncPacket::List packetsOfFormat(AncFormat) const` —
  filter helper (returns a value list, may be empty).
- [ ] `bool hasFormat(AncFormat) const`.
- [ ] `bool isExclusiveExtras() const` — `true` (no shared
  Buffer fields).

### `MediaDesc` + `Frame` plumbing

Wire `AncDesc` into `MediaDesc`; add an ANC convenience
accessor to `Frame`.

**Files:**
- [ ] `include/promeki/mediadesc.h` — add `AncDesc::List
  _ancList` member; `ancList()` const + mutable accessors;
  update `isValid` to accept ANC-only descriptors (a
  metadata-only stream is meaningful, e.g. an ST 2110-40
  receiver with no paired -20 video); update `formatEquals`;
  update equality; update DataStream `<<` / `>>`; update
  `fromSdp` / `toSdp` to round-trip the ANC `m=` section.
- [ ] `include/promeki/frame.h` + `frame.cpp` — add
  `AncillaryPayload::PtrList ancPayloads() const` mirroring
  `videoPayloads` / `audioPayloads`. Update `mediaDesc()` so
  the assembled descriptor includes ANC entries.
- [ ] Update `tests/unit/proav/mediadesc.cpp` and
  `tests/unit/proav/frame.cpp` for the new surface.

**Out of scope here:**
- No pipeline-planner consequences — ANC streams ride along
  the same `MediaIOPortGroup` as the producing backend until
  a future use case demands a separate route.

### `AncSourceProvenance` enum (typed enum)

Stamped on each `AncillaryPayload` so downstream knows where
the packets originated. Drives the translator registry's
"do we need to translate, or is this packet already in the
right form?" decision and helps test instrumentation.

**Values:** `Unknown`, `Sdi`, `Rtp291` (ST 2110-40),
`NdiMetadata`, `RtmpScript`, `SynthesizedInternal`,
`FileContainer` (e.g. MXF-carried ANC).

**File:** entry in `include/promeki/enums.h`, stamped under
`AncDesc::metadata()` key `AncProvenance` so it round-trips
through DataStream/JSON without growing the descriptor proper.

---

## Phase 1 — RTP ST 2110-40 / RFC 8331 wire format

ST 2110-40 / RFC 8331 packetization. This is the first wire
format the ANC stack supports end-to-end and the one the rest
of the plan validates against (translators are tested by
round-tripping through an RTP-40 pair).

### `RtpPayloadAnc` (RtpPayload subclass)

Implements RFC 8331 pack/unpack for `AncillaryPayload`. Lives
next to the other `RtpPayload*` classes.

**Files:**
- [ ] `include/promeki/rtppayloadanc.h` (separate header — the
  ST 2110-40 wire format is large enough that bundling into
  `rtppayload.h` would clutter it)
- [ ] `src/network/rtppayloadanc.cpp`
- [ ] `tests/unit/network/rtppayloadanc.cpp`

**Interface:**
- [ ] Derives `RtpPayload`. `payloadType()` defaults to 100
  (dynamic, configurable); `clockRate()` = 90 000 (RFC 8331
  §3.2).
- [ ] `pack` deviates from the byte-stream `pack(const void *,
  size_t)` shape used by video/audio payloads. ANC carries
  structured packets, not opaque bytes. Add a typed overload
  `RtpPacket::List packAncFrame(const List<AncPacket> &packets,
  uint32_t rtpTimestamp, bool fieldB)`; the byte-stream `pack`
  asserts unreachable (the packetizer thread calls the typed
  form directly).
- [ ] `unpack` returns a `Buffer` of serialized packet records
  (an internal compact form) plus a typed
  `unpackAncPackets(RtpPacket::List, List<AncPacket> &out)`
  helper used by the depacketizer.
- [ ] `validate(Buffer)` returns `Accept` for any well-formed
  ANC frame; `DropSilently` for zero ANC_Count payloads.
- [ ] `maxPayloadSize()` honors the standard MTU-safe default.

**Wire format (per RFC 8331 §2.1):**
- [ ] 16-bit Extended Sequence Number (already carried in the
  generic RTP header — RFC 8331 does not add another).
- [ ] Payload: 2-byte ANC_Count + 2-byte Reserved + for each
  ANC packet: 2-byte (C / Line_Number) + 2-byte (Horizontal_Offset
  / S / StreamNum) + 2-byte (DID / SDID) + 1-byte Data_Count +
  N × 10-bit UDW + 10-bit Checksum + word-align padding.
- [ ] Honor the marker bit on the last packet of a frame
  (single-RTP-packet ANC frames have marker=1 outright).

**Doctests:**
- [ ] Round-trip every well-known ANC type (captions, AFD,
  ATC, SCTE-104) through pack/unpack.
- [ ] Multi-packet ANC frames where the wire layout splits at
  byte boundaries between ANC packets.
- [ ] Field-B / progressive line numbering.
- [ ] Word-alignment padding at every UDW count modulo.
- [ ] Truncated / under-length input handling (`DropSilently`).

### RtpMediaIO ANC stream wiring

`RtpMediaIO` already carries a "data" m=section via
`RtpPayloadJson`. Add a parallel **ANC stream** rather than
overloading the existing data stream — RFC 8331 has its own
SDP signature (`smpte291` rtpmap), its own clock rate
expectation, and a different `validate` policy.

**MediaConfig keys (add):**
- [ ] `AncRtpDestination` — mirrors `VideoRtpDestination`
  shape.
- [ ] `AncRtpPayloadType` (default 100).
- [ ] `AncRtpClockRate` (default 90 000; expose so capture
  cards that drift the ANC clock can match exactly).
- [ ] `MetadataRtpFormat::St2110_40` flips from "rejected by
  backend" to "wired up"; chosen via existing
  `MetadataRtpFormat` config. When `St2110_40` is selected the
  data m=section is implicitly the ANC stream; when
  `JsonMetadata` is selected the legacy JSON path runs as
  today. (Strict separation of "data" vs "anc" via two
  parallel streams is overkill until a use case shows up.)

**Per-stream worker threads:**
- [ ] `RtpAncPacketizerThread` — converts per-frame
  `AncillaryPayload` into RFC 8331 RTP packets. Mirrors the
  shape of `RtpAudioPacketizerThread` (typed context struct,
  bounded payload queue input, RtpPacketBatch output, EOS
  drain).
- [ ] `RtpAncDepacketizerThread` — mirrors
  `RtpDataDepacketizerThread`: marker-bit / timestamp flush,
  payload->unpack, push `RxAncFrame` bundles onto the
  per-stream queue. Add `RxAncFrame` struct alongside the
  existing `RxDataMessage` / `RxAudioChunk` / `RxVideoFrame`.
- [ ] `RtpAncTxThread` is *not* needed as a separate class —
  ANC packet rates are low enough that the existing
  `RtpTxThread` (used by video) handles ANC packets without
  modification once the packetizer hands it a `RtpPacketBatch`.

**RtpAggregatorThread integration:**
- [ ] Add ANC drain to the existing aggregator alongside
  video/audio/data, emitting `Frame` instances with only an
  `AncillaryPayload` populated when the receiver is ANC-only.
- [ ] When a paired video stream is active, the aggregator
  must associate ANC frames with the right video frame by RTP
  timestamp (same SR-anchored math the existing aggregator
  uses for audio).

**SDP round-trip:**
- [ ] `MediaDesc::toSdp` / `fromSdp` extended to emit/consume
  the `m=application <port> RTP/AVP <pt>` section with
  `a=rtpmap:<pt> smpte291/90000` and
  `a=fmtp:<pt> DID_SDID={(DID,SDID),…}` where the fmtp value
  is computed from `AncDesc::allowedFormats` (each
  `AncFormat::ID` resolves to its canonical
  `(did(), sdid())` pair) when the list is non-empty.
- [ ] `RtpMediaIO::buildSdp` / `applySdp` learn the ANC
  section symmetric to video/audio.

**Doctests / integration:**
- [ ] `tests/unit/network/rtpancpacketizerthread.cpp` —
  packetizer covers RTP-TS continuity, marker-bit emission,
  bounded queue cancel-on-close, EOS drain.
- [ ] `tests/unit/network/rtpancdepacketizerthread.cpp` —
  depacketizer covers reassembly across timestamp changes,
  SSRC reset handling, drop-on-invalid.
- [ ] `tests/func/rtp-anc-roundtrip/` (promeki-test) — full
  RTP-40 round-trip: source emits a Frame carrying a known
  ANC packet list, sink receives, asserts byte-exact equality.

---

## Phase 2 — `AncTransport` + translator registry

The seam between "ANC packets are the canonical in-memory
representation" and "every backend has its own way of
shipping the same information." Each translator is keyed by
the triple `(AncFormat::ID, AncTransport src, AncTransport
dst)` — one logical format identity per the Phase 0
registry, two carriers. Backends look up at the seam and
never parse ANC themselves.

### `AncTransport` (typed enum)

Names the carrier a translator works with. Distinct from
`AncFormat` (the logical "what kind of data is this"):
`AncFormat::Cea708` is closed-caption content regardless of
whether it's currently riding inside an `AncPacket`, an NDI
XML metadata frame, or an AMF0 script tag.

**File:** entry in `include/promeki/enums.h`
(`TypedEnum<AncTransport>` per the `feedback_typedenum_enums_h`
rule).

**Values:**
- [ ] `AncPacket` — canonical in-memory `AncPacket` value.
  This is what flows through `AncillaryPayload`; it is the
  primary hub format that other transports always translate
  through.
- [ ] `Internal` — a typed parsed payload as a `Variant`
  (e.g. a `Cea708Cdp` struct for captions, a `Timecode` for
  ATC, an `AfdCode + BarData` pair for AFD). Used by
  pipeline stages that want the decoded form without
  reaching into ANC bytes.
- [ ] `NdiMetadata` — NDI XML metadata frame body.
- [ ] `RtmpScript` — RTMP AMF0 script tag (onCaptionInfo /
  onCuePoint / onMetaData payload body).
- [ ] `MxfKlv` — SMPTE 436M MXF ancillary essence KLV.
- [ ] `HlsSei` — CEA-608/708 carried as H.264 / HEVC SEI
  user-data registered messages (CEA-708 NAL fragments
  consumed by the future HLS muxer).
- [ ] `RtpSt2110_40` — reserved. Byte-level RFC 8331 framing
  is handled by `RtpPayloadAnc` directly (it works in lists
  of `AncPacket`, not single packets), so no translator
  registers under this transport in v1. Listed so the enum
  is forward-compatible if a use case shows up.

### `AncTranslator` interface + registry

**Files:**
- [ ] `include/promeki/anctranslator.h`
- [ ] `src/proav/anctranslator.cpp` (registry + base helpers)
- [ ] `tests/unit/proav/anctranslator.cpp`

**Interface:**
- [ ] `virtual ~AncTranslator() = default`.
- [ ] `virtual AncFormat::ID format() const = 0`.
- [ ] `virtual AncTransport sourceTransport() const = 0`.
- [ ] `virtual AncTransport targetTransport() const = 0`.
- [ ] `virtual Error translate(const Variant &in,
  Variant &out) const = 0` — convert from `sourceTransport`
  to `targetTransport`. The `Variant` shape for each
  transport is fixed:
  - `AncPacket` — `Variant` wrapping an `AncPacket` value.
  - `Internal` — format-specific typed payload (registered
    as a Variant type per format, e.g. `TypeCea708Cdp`).
  - `NdiMetadata` — `String` (UTF-8 XML element).
  - `RtmpScript` — Variant carrying an AMF0 object (the
    existing `AmfValue` type the RTMP code already uses).
  - `MxfKlv` — `Buffer` (raw KLV bytes).
  - `HlsSei` — `Buffer` (raw SEI payload).
- [ ] Inputs that cannot be represented in the target return
  `Error::NotSupported`; backends use that as the "skip" /
  "leave as native carrier metadata" signal.

**Per-format translator enumeration:**
Backends and tooling routinely need to ask *"what targets can
I translate this format into?"* — so the registry exposes a
per-format lookup in addition to the triple lookup.

- [ ] `static const AncTranslator *find(AncFormat::ID,
  AncTransport src, AncTransport dst)` — primary lookup.
- [ ] `static List<const AncTranslator *> findForFormat(
  AncFormat::ID)` — every translator registered for one
  format, both directions. Drives `mediaio --dump-anc`'s
  "what targets does this format support?" diagnostic and
  the NDI/RTMP sink's "translate everything I can, leave the
  rest" walk.
- [ ] `static List<const AncTranslator *> findForTransport(
  AncTransport src, AncTransport dst)` — every translator
  for one (src, dst) pair, across all formats. Drives the
  sink emit loop where the destination transport is known
  but the source format varies frame-to-frame.
- [ ] `static void registerTranslator(UniquePtr<AncTranslator>)`
  — called at static-init time per `PROMEKI_REGISTER_ANC_TRANSLATOR`
  macro, mirrors `PROMEKI_REGISTER_MEDIAPAYLOAD`.
- [ ] Static-init thread-safety: registry uses
  `Mutex`-guarded `Map<Tuple3, UniquePtr>` storage; lookup
  reads are lock-free after registration completes (no
  late-bound `registerTranslator` after `main`).
- [ ] Doctests: registration, triple lookup, per-format
  enumeration, per-transport enumeration, double-register
  rejection, look up an unregistered triple returns null.

### Initial translator set

Each translator gets its own `.cpp` file in
`src/proav/anctranslator_*.cpp` and its own doctest in
`tests/unit/proav/anctranslator_*.cpp`. None of these depend
on a real backend — they're pure ANC-to-target conversions.

- [ ] `AncTranslator_Cea708_AncPacket_Internal` /
  `_Internal_AncPacket` — pair that maps an ANC packet to
  the typed `Cea708Cdp` Variant and back. Phase 3's typed
  parser is the implementation. Once these land, every other
  CEA-708 translator can route through `Internal` as the hub
  if it doesn't want to touch raw UDW words.
- [ ] `AncTranslator_Cea708_AncPacket_NdiMetadata` /
  `_NdiMetadata_AncPacket` — wraps the ST 334 CDP payload
  into the NDI metadata XML schema NDI's caption decoder
  consumes.
- [ ] `AncTranslator_Cea708_AncPacket_RtmpScript` /
  `_RtmpScript_AncPacket` — produces the AMF0 onCaptionInfo
  data tag YouTube / FB Live consume (the same shape
  `RtmpClient` already publishes for the test stream).
- [ ] `AncTranslator_Afd_AncPacket_NdiMetadata` /
  `_NdiMetadata_AncPacket` — NDI AFD frame description
  (drives downstream scalers).
- [ ] `AncTranslator_Afd_AncPacket_Internal` — translates
  AFD into an `AfdCode + BarData` typed Variant; pipeline
  stages stamp the matching `Metadata::AspectRatio` key on
  the paired `VideoPayload` from this typed form.
- [ ] `AncTranslator_AtcLtc_AncPacket_Internal` — ANC ATC →
  `Timecode` (via libvtc-backed `Timecode`); stamped onto
  the frame's `Metadata::Timecode` key by the consuming
  pipeline stage.
- [ ] `AncTranslator_Scte104_AncPacket_Internal` — parses
  SCTE-104 into the typed `Scte104Message` Variant. RTMP
  (`onCuePoint`) and HLS (`SCTE-35 in SEI`) targets follow
  in Phase 4 when the consuming backends are ready.

**Naming convention:** `AncTranslator_<Format>_<Src>_<Dst>`.
Reads top-to-bottom as "translator that takes Format from
Src transport to Dst transport." Pair classes (forward +
inverse) live in the same `.cpp` file when their logic
shares an internal helper.

---

## Phase 3 — Typed parsers and builders

Application-facing accessors. Each typed parser is a thin
wrapper around `AncPacket`'s raw UDW list — never an
independent data type — so an `AncillaryPayload` can be
introspected without losing wire fidelity.

### CEA-708 / CEA-608 captions

**Files:**
- [ ] `include/promeki/cea708cdp.h` + `src/proav/cea708cdp.cpp`
- [ ] `tests/unit/proav/cea708cdp.cpp`

**Surface:**
- [ ] `Cea708Cdp::parse(const AncPacket &)` — extracts the
  CDP packet per SMPTE 334-2 / CEA-708-D Annex.
- [ ] `Cea708Cdp::build(const Cea708Cdp &, AncPacket &out,
  uint16_t lineNumber)` — inverse, produces a 334-compliant
  ANC packet.
- [ ] Helpers for the CDP framing fields (cc_data, cdp
  frame rate, time_code_present, service_info_present).
- [ ] CEA-608 line-21 byte-pair access for legacy 608-only
  consumers.

### AFD + Bar Data

**Files:**
- [ ] `include/promeki/afdancpacket.h` + `src/proav/afdancpacket.cpp`
- [ ] `tests/unit/proav/afdancpacket.cpp`

**Surface:**
- [ ] `AfdAncPacket::AfdCode` enum mirroring SMPTE 2016-1
  table (4-bit AFD codes).
- [ ] `AfdAncPacket::parse(const AncPacket &)` returns
  `AfdCode` + optional bar values.
- [ ] `AfdAncPacket::build(AfdCode, BarData, AncPacket &out)`.
- [ ] `AfdAncPacket::toMetadata(Metadata &)` — sets the
  paired video's aspect/bar metadata.

### ATC LTC / VITC timecode

**Files:**
- [ ] `include/promeki/atcancpacket.h` + `src/proav/atcancpacket.cpp`
- [ ] `tests/unit/proav/atcancpacket.cpp`

**Surface:**
- [ ] `AtcAncPacket::parse(const AncPacket &) → Result<Timecode>`
  — reuses the existing `Timecode` (libvtc-backed) class.
- [ ] `AtcAncPacket::build(const Timecode &, AncPacket &out,
  AtcKind kind)` where `AtcKind` selects LTC / VITC-1 /
  VITC-2.

### SCTE-104

The most structurally complex of the four. Borrow the SCTE-104
specification's "single_operation_message" / "multiple_operation_message"
split.

**Files:**
- [ ] `include/promeki/scte104ancpacket.h`
- [ ] `src/proav/scte104ancpacket.cpp`
- [ ] `tests/unit/proav/scte104ancpacket.cpp`

**Surface:**
- [ ] `Scte104Message` value type modelling the operation
  list + protocol version + splice request data.
- [ ] `Scte104AncPacket::parse(const AncPacket &) →
  Result<Scte104Message>`.
- [ ] `Scte104AncPacket::build(const Scte104Message &,
  AncPacket &out)`.
- [ ] Sub-types for the operations promeki cares about
  initially: `SpliceRequest`, `TimeSignal`,
  `SegmentationDescriptor`. Other ops parse to an opaque
  `Variant`-encoded blob — application code that needs them
  can extend the parser later.

---

## Phase 4 — MediaIO backend integration

ANC packets entering and leaving the existing backends. By
this phase the ANC stack and translator registry are mature
enough that each backend's ANC seam is small (look up
translator, hand off, done).

### NdiMediaIO

NDI metadata frames carry the closed-caption / AFD / timecode
equivalents as XML.

- [ ] **Source side:** when an NDI source receives a metadata
  frame, parse the well-known XML elements (captions, AFD,
  ATC) via the matching `(format, NdiMetadata, AncPacket)`
  translator, build `AncPacket` instances, attach an
  `AncillaryPayload` to the emitted `Frame`. Unknown XML
  elements are kept as-is on the frame metadata (no
  information loss); they just don't become ANC packets.
- [ ] **Sink side:** before sending each frame, walk
  `frame.ancPayloads()`, look up each packet's
  `(packet.format().id(), AncPacket, NdiMetadata)`
  translator, produce the NDI XML, and emit the metadata
  frames inline with the video. Packets with no registered
  translator are skipped (logged once per format-id).
- [ ] Tests: ndimediaio.cpp gets ANC round-trip cases (captions,
  AFD, ATC) using NDI Tools' bundled test sender/receiver
  schemas as fixtures.

### RtmpMediaIO

RTMP metadata carries via AMF0 onCaptionInfo / onMetadata
script tags.

- [ ] **Source side:** parse incoming `onCaptionInfo`,
  `onMetaData`, `onCuePoint` script tags through the matching
  `(format, RtmpScript, AncPacket)` translators into
  `AncPacket` instances.
- [ ] **Sink side:** convert outgoing ANC packets into AMF0
  script tags via `(format, AncPacket, RtmpScript)`
  translators. CEA-708 → `onCaptionInfo`, AFD → metadata
  key, SCTE-104 → cue points / `onCuePoint`.
- [ ] Tests: round-trip via the existing `RtmpClient`
  loopback test fixture.

### Future hooks

These are documented but not implemented in this phase —
they're listed so the contract is visible when the backends
land.

- [ ] **AAC + HLS encoder:** HLS pipelines that carry CEA-708
  in NAL SEI rather than as a side data track. Translator
  target is `HlsCea708`; consumer is a future HLS muxer
  MediaIO.
- [ ] **MXF container:** MXF carries ANC as KLV essence
  (SMPTE 436M). Translator target is `MxfKlv`. Lands when
  the MXF MediaIO does.

---

## Phase 5 — AJA NTV2 SDI capture contract (forward-looking)

No code lands in this devplan for AJA NTV2 — but the ANC
stack ships with the contract the future capture MediaIO must
satisfy, so that backend is a drop-in producer.

### Documented contract

- [ ] An SDI capture MediaIO that produces ANC must:
  - For every captured frame, scan VANC (and HANC when
    requested) line ranges, build one `AncillaryPayload`
    listing every ST 291 packet found (DID, SDID, line
    number, h-offset, F-bit, UDW list, checksum).
  - Stamp `AncSourceProvenance::Sdi` into the descriptor
    metadata so translators downstream know the packets are
    canonical.
  - Set `AncDesc::sourceRaster` and `scanMode` to match the
    paired `ImageDesc` so line numbers are interpretable
    even when the ANC payload is consumed without the video.
  - Stamp `payload.duration` to one frame period of the
    session frame rate (matches the VideoPayload fill
    behavior MediaIO already enforces).
- [ ] **Output side** (SDI sink emitting ANC): the inverse —
  inject the listed ANC packets at the requested line
  numbers, recompute checksums on emit if a translator-built
  packet lacks one, and warn (don't error) when the requested
  line is outside the VBI/VANC region the current raster
  supports.
- [ ] No special AJA-NTV2 file is needed here; the contract
  is added as a section in `docs/proav/anc.dox` (created as
  part of this phase) and pointed to from this devplan.

---

## Phase 6 — Inspection, tooling, demos

User-facing surfaces that exercise the stack end-to-end.

### `mediaio --dump-anc`

- [ ] Add a `--dump-anc` flag to `utils/mediaio/` that for
  every read frame prints one line per ANC packet with
  `AncFormat` name, line, DID/SDID, byte count, and (when a
  `(format, AncPacket, Internal)` translator is registered)
  a one-line decoded summary (e.g. "CEA-708 CDP rate 59.94,
  1 service block, 12 cc_data words"). Lists the registered
  target transports per format via
  `AncTranslatorRegistry::findForFormat`.

### ANC test pattern source MediaIO

- [ ] `AncTpgMediaIO` (or option on the existing `TpgMediaIO`)
  that emits frames carrying a synthesizable mix of ANC types
  on configurable lines: stepped ATC timecode that follows
  the running frame counter, fixed-text CEA-708 captions, a
  cycling AFD code, and an occasional SCTE-104 splice signal.
  Drives every functional test below.

### Caption renderer (optional follow-on)

- [ ] A `Cea708Overlay` `MediaIO` (transform stage) that
  consumes `ancPayloads()`, decodes CDP service blocks, and
  paints captions onto the paired `UncompressedVideoPayload`
  using the existing `paintengine.h` infrastructure. Listed
  as optional: the typed parser landing in Phase 3 already
  unlocks third-party renderers; an in-tree overlay is just
  the most visible demo.

### promeki-test functional matrix

- [ ] `tests/func/anc-rtp40-roundtrip/` — TPG → RTP-40 →
  receiver → byte-exact ANC compare.
- [ ] `tests/func/anc-ndi-roundtrip/` — TPG → NDI metadata →
  NDI receiver → translator → ANC compare (for the subset of
  types NDI carries).
- [ ] `tests/func/anc-rtmp-captions/` — TPG with CEA-708 →
  RTMP → consumer reads `onCaptionInfo` → captions match.
- [ ] `tests/func/anc-mediapipeline-passthrough/` — ANC enters
  via one MediaIO, leaves via another with the same wire
  format, verifies the pipeline did not mangle anything.

### Documentation

- [ ] `docs/proav/anc.dox` — top-level chapter covering the
  payload, the descriptor, the translator registry, and the
  AJA NTV2 contract. Worked example showing how to receive
  ANC via RTP-40 and re-emit it on NDI.
- [ ] Update `docs/proav/mediaio.dox` to reference ANC
  alongside video/audio.

---

## Open questions / things to revisit during build

- **Should `RtpMediaIO` carry data + anc on the same m=section
  or on two?** Two adds wiring complexity; one prevents
  simultaneous JSON metadata + RFC 8331 (which is rarely a
  real use case). Starting with one (selected by
  `MetadataRtpFormat`) keeps the change footprint small and
  is reversible if a deployment needs both.
- **Per-line-number injection on sinks:** SDI emit ordering
  matters (some receivers care about VANC line monotonicity).
  Spec it in the AJA NTV2 contract section but don't bake
  enforcement into `AncillaryPayload` itself — leave that to
  the sink's emission stage.
- **Checksum recompute on translate:** translators that build
  an `AncPacket` from a non-ANC source (e.g. NDI XML → ANC)
  must always recompute the checksum. Captured packets that
  arrived from SDI/RTP-40 keep their as-received checksum so
  re-emission is byte-exact. The `AncTranslator` interface
  doesn't enforce this — flag it in the per-translator
  unit-test checklist instead.
- **Multi-link / 12G SDI stream numbering:** the `streamNum`
  field on `AncPacket` is enough for capture/playback today;
  full 12G sub-image mapping (per ST 2082 / ST 2110-40
  sub-streams) waits on the AJA NTV2 backend confirming what
  it actually exposes.
