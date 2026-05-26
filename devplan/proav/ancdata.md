# Ancillary Data (ANC) Stack

**Library:** `promeki` (proav + network)
**Standards:** All work follows `CODING_STANDARDS.md`. Every new
class requires complete doctest coverage. See `devplan/README.md`
for the full requirements.

The goal is a generalized side-channel/metadata stack: capture
metadata from any supported wire format (SDI ST 291, ST 2110-40
RTP, NDI XML, RTMP AMF script tags, HDMI InfoFrames, MPEG-TS
private sections, HLS SEI), carry it through `MediaPipeline` in
its **native wire form** so it can be re-emitted bit-exact when
the destination matches the source, translate between wire
forms only when the destination differs, and expose typed
parsers/builders on top so application code (closed-caption
overlay, SCTE-104 driven splicers, AFD-aware scalers, ATC
round-trip, HDR pass-through) does not have to touch raw bytes.

## Status at a glance (2026-05-24)

| Phase | What | Status |
|------:|------|--------|
| 0     | Foundation types (AncFormat, AncPacket, AncDesc, AncPayload, AncMeta, St291Packet, HdmiInfoFrame, MediaDesc/Frame plumbing) | **Landed** |
| 1     | RTP ST 2110-40 / RFC 8331 (RtpPayloadAnc + packetizer/depacketizer threads + RtpMediaIO stream wiring) | **Landed** |
| 1.5   | ESN reorder handling | **Deferred** (packer writes 0, depacketizer ignores) |
| 2     | `AncTranslateConfig` + `AncTranslator` + 3 registries + macros + initial ATC and AFD ← → St291 codecs | **Landed** |
| 2b    | CEA-708 ← → St291 + `Cea708Cdp` + TPG caption injection + Inspector AncData JSONL dump | **Landed** |
| P2    | Second-pass conformance audit (Phases A/B/C wire bugs, per-codec deep audits, registry / docs) — `devplan/proav/ancaudit.md` | **Complete — all F1–F10 findings landed; audit file retired 2026-05-20** |
| 3     | Remaining typed parsers (full AFD value type, Atc helpers, Scte104, HdrStatic2086 St291, HDR dynamic, KLV) | **Partial** — HdrStatic2086 HdmiInfoFrame + St291 (ST 2108-1) codecs landed 2026-05-15; HdrDynamic2094_40 value type + HdmiInfoFrame + St291 (ST 2108-2 KLV, multi-packet) codecs landed 2026-05-15; AncOp47Sdp value type + OP-47 SDP codec (RDD 8, DID 0x43/SDID 0x02) landed 2026-05-20; AncSt2020Audio value type + ST 2020-2 Method A codec (DID 0x45, SDIDs 0x01–0x09) landed 2026-05-20; VPID codec (SdiVpid ← → AncTranslator, DID 0x41/SDID 0x01) landed 2026-05-20; Scte104 codec still pending |
| 3.5   | Subtitle file I/O + CEA-608 codec (Subtitle/SubtitleList/SubRip, Scc, Cea608Encoder/Decoder all three modes, TPG injection, round-trip func test) | **Landed** |
| P3    | CEA-608 conformance audit — ANSI/CTA-608-E S-2019 (60+ findings across XDS, wire/charset, decoder, encoder) | **Complete — all findings landed 2026-05-23; 3 post-audit follow-ons landed 2026-05-24** |
| 4     | MediaIO backend integration (codec API Frame-shaped refactor + ANC pairing, Cea708 ← / → HlsSei + NVENC SEI injection) | **Partial** — YouTube delivery path landed 2026-05-12; NdiMediaIO ANC + RtmpMediaIO ANC + AncMetadataStamper pending |
| 4.5   | Frame-sync ANC policy (FrameSyncDisposition + AncSyncPolicy registry + AncFrameSync class with stash + per-codec Play/Drop/Repeat policies for ATC, Cea708, Afd, Hdr*) | **Mostly landed** — registry + AncFrameSync (stash included) + per-codec policies for ATC/Cea708/Afd/Hdr static/Hdr dynamic landed; VPID (Play+Repeat pass-through, Drop discards) + Op47Sdp (Play only, Repeat+Drop both drop — FSC collision avoidance) + St2020Audio (Play+Repeat pass-through, Drop discards) sync policies landed 2026-05-20; SCTE-104 policy still waits on the Scte104 codec; functional 23.976→60 3:2-pulldown test pending |
| 5     | AJA NTV2 SDI ingest contract (documentation only) + build scaffolding (`thirdparty/libajantv2`, `PROMEKI_ENABLE_NTV2`) | **Scaffolding landed 2026-05-16; MediaIO backend pending** |
| 6     | Inspector ANC JSONL, caption renderer, CEA-708 DTVCC stack, TPG 708 emission, captions.md, `captions.cea708.subrip_roundtrip` | **Mostly landed** — remaining functional matrix + `docs/proav/anc.dox` pending |

The Phase 2b end-to-end slice proves the architecture: caption text
goes in via `MediaConfig::TpgAncCaptionsFile` (a SubRip `.srt` path),
rides through TPG → MediaIOPortConnection → Inspector, and lands in
a JSONL file with the original bytes recoverable from the cc_data
triples. The same SRT path also drives a *visual* render via
`SubtitleBurnMediaIO`, and an alternative SCC bypass path
(`MediaConfig::TpgAncCaptionsScc`) feeds real-broadcast-captioner
output directly into cc_data. The ATSC A/53 `HlsSei` codec +
NVENC SEI injection closes the YouTube / Twitch / RTMP+H.264 caption
delivery path end-to-end.

## Decisions

These are settled scope answers that the rest of this plan
assumes — captured here so reviewers don't re-litigate them
mid-implementation. They will migrate to `docs/proav/anc.dox`
when that lands (Phase 6).

- **Generic-packet shape.** One `AncPayload` per `Frame`,
  holding an `AncPacket::List`. `AncPacket` is the *single*
  carrier type for every transport: `(AncFormat, AncTransport,
  Buffer data, Metadata meta)`. The `data` field holds the
  packet's wire-form bytes; the `meta` field holds
  transport-specific sidecar (line number for ST 291,
  InfoFrame type for HDMI, script-tag name for RTMP, etc.).
  Symmetric with one `VideoPayload` carrying one frame of
  pixels.
- **Native-wire model — no normalization on ingress.** Backends
  store packets in the wire form they arrived on. Translation
  only happens when the emit-side transport differs. A pipeline
  that goes SDI → ST 2110-40 (both `AncTransport::St291`) does
  zero translation and is byte-exact end to end. A pipeline
  that goes NDI → RTMP translates only on the RTMP sink.
- **`AncPacket` is internally CoW.** Holds a `SharedPtr<Impl>`,
  copy = refcount bump, mutators do copy-on-write. No `::Ptr`
  alias (per the post-2026-05-07 convention). `::List` is
  `List<AncPacket>` — a vector of one-pointer values. Cheap to
  pass through pipelines, cheap to compose into transport
  helpers.
- **Transport-specific helpers by composition, not inheritance.**
  `St291Packet`, `HdmiInfoFrame`, etc. each hold an
  `AncPacket _pkt` by value and expose typed accessors over
  `(data, meta)`. Implicit decay to `const AncPacket &` for
  storage; explicit `St291Packet::from(const AncPacket &)`
  promotion for typed reads.
- **One stateful worker class — `AncTranslator` — and three
  kinds of registered handlers.** Public API is a single
  value-type session that holds config and dispatches; under
  it sit three independent registries.
  - **Parsers** — `(AncFormat, AncTransport src) →
    Result<Variant>`. Decode a packet's wire bytes into a
    typed Variant (e.g. `Variant(Cea708Cdp{...})`).
  - **Builders** — `(AncFormat, AncTransport dst) →
    Result<AncPacket>`. Encode a typed Variant into a packet
    on the requested transport.
  - **Translators (direct)** — `(AncFormat, AncTransport src,
    AncTransport dst) → Result<AncPacket>`. Optional fast-path
    wire-to-wire conversion that skips the Variant round-trip.
  - **`AncTranslator` class** — constructed
    `AncTranslator(AncTranslateConfig)`. Holds config. Methods:
    `parse(AncPacket) → Result<Variant>`,
    `build(Variant, AncFormat, AncTransport target) →
    Result<AncPacket>`,
    `translate(AncPacket, AncTransport target) →
    Result<AncPacket>`. `translate` prefers a registered
    direct translator and falls back to `parse(pkt)` →
    `build(variant, fmt, target)` when none exists.
  - **Third-party extension story.** A new transport `XYZ` is
    plumbed into the stack by registering parsers and builders
    for the formats it carries (`Cea708 ← XYZ`, `Cea708 → XYZ`,
    …). Direct translators are an optional optimization layer
    on top.
- **Held config via `AncTranslateConfig`.** Universal knobs
  (`Fidelity`, `Checksum`, `OnUnsupported`, `AllowLossy`)
  plus per-transport build-time keys (`St291BuildLine`,
  `St291FieldB`, `NdiXmlNamespace`, `HdmiInfoFrameOui`,
  `RtmpAmfObjectName`). Mirrors the `MediaConfig` pattern: a
  `VariantDatabase<"AncTranslateConfig">` subclass with typed-key
  statics on the class. The config is passed at `AncTranslator`
  construction time and threaded internally to every
  parser/builder/translator handler. C++ identifiers flatten
  namespacing (`St291BuildLine`); wire-names keep the dot
  (`St291.BuildLine`).
- **First-class ANC formats (typed parsers/builders).**
  Landed: CEA-708 CDP (Phase 2b), ATC LTC / VITC1 / VITC2
  (Phase 2), minimal AFD (Phase 2 — full `Afd` value type with
  Bar Data pending), CEA-608 via `Cea608Encoder` / `Cea608Decoder`
  (Phase 3.5), CEA-708 DTVCC (`Cea708Service` /
  `Cea708DtvccPacket` / `Cea708WindowState` /
  `Cea708Encoder` / `Cea708Decoder`, Phase 6),
  `HdrStaticMetadata` (Phase 3, HdmiInfoFrame codec only).
  Pending: SCTE-104, HdrStatic2086 St291 codec, HDR dynamic
  (HDR10+ / ST 2094-40), KLV (MISB ST 0601), Scte35 over
  MpegTsPrivate.
- **`AncFormat` carries a category.** Every format is tagged
  with an `AncCategory` (`Captions`, `Timecode`, `Splice`,
  `Aspect`, `Hdr`, `AudioMetadata`, `Display`, `Geolocation`,
  `UserDefined`). Lets sinks declare "I carry Captions+Timecode
  only" by category rather than enumerating every ID.
- **SDI capture device:** out of scope for this devplan. AJA
  NTV2 is the planned future SDI source; the ANC stack lands
  with a documented contract (Phase 5) so AJA NTV2 ingest is
  a drop-in producer when it arrives.
- **HDMI and MPEG-TS transports:** enum values and format IDs
  ship now; backend wiring (DeckLink HDMI capture, MPEG-TS
  demuxer, SRT) is out of scope here and lands when those
  MediaIO backends do.
- **Naming.** General-interface types use the `Anc` prefix
  (`AncPacket`, `AncPayload`, `AncDesc`, `AncFormat`,
  `AncTransport`, `AncTranslator`). Transport helpers are
  named for their transport (`St291Packet`, `HdmiInfoFrame`).
  Typed parsers are named for their format (`Cea708Cdp`,
  `Afd`, `Atc`, `Scte104`, `HdrStaticMetadata`,
  `HdrDynamic2094_40`, `Klv0601`). No type spells out
  "Ancillary" in full.

## Architecture overview

```
┌─────────────────────────────────────────────────────────────┐
│ Layer 4 — Typed parsers / builders                          │
│   Cea708Cdp, Cea708Service, Cea708DtvccPacket,              │
│   Cea708WindowState, Cea708Encoder, Cea708Decoder,          │
│   Cea608Encoder, Cea608Decoder, HdrStaticMetadata,          │
│   HdrDynamic2094_40                              [landed]   │
│   Afd value type, Atc helpers, Scte104, Klv0601,            │
│   AncMetadataStamper                             [pending]  │
└──────────────────────────────┬──────────────────────────────┘
                               │ uses
┌──────────────────────────────▼──────────────────────────────┐
│ Layer 3 — AncTranslator + handler registries [landed]       │
│   AncTranslator      — stateful session, holds              │
│                        AncTranslateConfig.                  │
│                        parse() / build() / translate().     │
│   ParserRegistry     — (format, srcT)        → fn           │
│   BuilderRegistry    — (format, dstT)        → fn           │
│   TranslatorRegistry — (format, srcT, dstT)  → fn (opt)     │
│   Codecs landed: Cea708 (St291 + HlsSei),                   │
│                  AtcLtc/Vitc1/Vitc2, Afd, HdrStatic2086     │
│                  (HdmiInfoFrame + St291),                   │
│                  HdrDynamic2094_40 (HdmiInfoFrame +         │
│                                     St291 multi-packet).    │
└──────────────────────────────┬──────────────────────────────┘
                               │ operates on
┌──────────────────────────────▼──────────────────────────────┐
│ Layer 2 — Transport helpers [landed]                        │
│   St291Packet, HdmiInfoFrame                                │
│   Typed accessors over (data, meta) when transport matches. │
└──────────────────────────────┬──────────────────────────────┘
                               │ holds
┌──────────────────────────────▼──────────────────────────────┐
│ Layer 1 — Generic carrier + identity types [landed]         │
│   AncPacket, AncPayload, AncDesc, AncFormat, AncTransport,  │
│   AncCategory, AncMeta                                      │
└─────────────────────────────────────────────────────────────┘
```

**Lifecycle of one ANC packet**, end-to-end:

1. **Ingress.** Backend receives wire bytes, wraps them in an
   `AncPacket` with the matching `AncTransport` and the
   matching `AncFormat` (looked up via the registry's
   per-transport key — `fromSt291DidSdid`,
   `fromHdmiInfoFrameType`, etc.). Per-packet framing
   metadata goes into `meta`.
2. **Pipeline.** The `AncPacket` rides through `MediaPipeline`
   inside the frame's `AncPayload`. Copies are refcount
   bumps; no decode, no translation.
3. **Inspection (optional).** Application code or
   `AncMetadataStamper` calls `AncTranslator::parse(pkt)` —
   works whether the packet is St291, NdiXml, or any
   transport with a registered parser.
4. **Egress.** Sink backend holds an `AncTranslator` and
   walks the payload. For each packet: if
   `pkt.transport() == sink's native transport`, emit
   `pkt.data()` verbatim. Otherwise call
   `translator.translate(pkt, sinkT)` — the translator
   prefers a registered direct `(fmt, src, dst)` handler and
   falls back to `parse(pkt) → build(variant, fmt, dst)`
   when none exists. Packets with no parser or builder for
   the needed pair are skipped and logged once per format.

---

## Phase 1.5 — ESN reorder handling (deferred)

- [ ] Plumb a 32-bit logical sequence number through the reorder
  buffer (today only sees 16-bit RTP seq) so `RtpPayloadAnc`
  can write ESN on send and use it on receive. Orthogonal to
  wire-format correctness; lands alongside any cross-stream
  sequence-aware reorder work.
- [ ] F-bit InterlacedField1 (F=10) on send — requires plumbing
  `AncDesc::scanMode` through the packetizer thread so
  `FieldB=false + scanMode=Interlaced` promotes to F=10
  (today F=00 covers both progressive and field-1).

---

## Phase P2 — Second-pass conformance audit follow-ups (landed 2026-05-20)

All audit findings from the second-pass review (tracked in the now-retired
`devplan/proav/ancaudit.md`) have landed.  Key items:

- [x] **P2-1 / P2-ATC** — ATC DID/SDID collapse: ST 12-2:2014 §5 assigns
  DID=0x60 / SDID=0x60 to every ATC flavour; `AncFormat::AtcLtc/Vitc1/Vitc2`
  all key to that pair, with DBB1 as the discriminator.  `AncAtc::PayloadType`
  enum (`Ltc`, `Vitc1`, `Vitc2`) added to `ancatc.h`; codec stamps it from
  the wire DBB1 on parse and from the caller's format choice on build.
  `AncAtc::payloadType()` / `setPayloadType()` accessors added.
- [x] **P2-9** — `AncDesc::toSdp` no longer emits `TM=CTM` alongside
  `SSN=ST2110-40:2018`; ST 2110-40:2023 §7 couples TM to :2023 SSN only.
  CTM is implicit when TM is absent.
- [x] **P2-17** — `St291Packet::buildRaw` / `buildRawType1` now hard-reject
  reserved DID ranges (ST 291-1 §6.1 Figure 4a/4b) and SDID=0x00 for Type-2.
- [x] **P2-21** — `AncCategory::Control{15}` added to `enums_anc.h` for
  in-band control packets (PacketForDeletion, EDH, RP 165 status).
- [x] **P2-22** — `AncFormat::PacketForDeletion` wildcard match: `fromSt291DidSdid(0x80, anyDBN)` resolves via the Type-1 wildcard path.
- [x] **P2-23** — `AncTranslator::parseGroup` + `hasParser` tested; confirmed
  multi-parser takes precedence on `parse(single packet)` and that
  `InsufficientContext` is returned when a multi-packet codec receives a lone
  packet.
- [x] **P2-24** — `AncFormat::fromHdmiInfoFrame(type, oui)` added; OUI
  0x00D046 → `HdrDynamic2094_40`, 0x00903E → `DvRpu`, others fall through
  to `fromHdmiInfoFrameType`.
- [x] **P2-26** — HDR dynamic §9.4 (AppVer=1 SHALL NOT): codec strips
  `ColorSaturationWeight` and clamps `numWindows > 1` to 1 on emission;
  §9.3 (AppVer=0 SHOULD NOT): codec warns but emits for backward
  compatibility with pre-2020 senders.
- [x] **P2-31** — `AncTranslator` registry idempotency: re-registering the
  same function pointer is a no-op; DEBUG builds hard-fail on collision with a
  different pointer.
- [x] **New `AncTranslateConfig` keys** — `St291BuildCBit` (C-bit for built
  ST 291 packets), `St291KeepAliveField` (F-bit for §5.5 keep-alives),
  `HdrDynamicImageWidth` / `HdrDynamicImageHeight` (§9.2 Window 0 sentinel
  when zero). Convenience accessors `checksumPolicy()` and
  `keepAliveFieldByte()` added.
- [x] **`RxAncFrame::keepAlive`** — bool field identifying ST 2110-40 §5.5
  ANC_Count=0 keep-alive frames at the depacketizer output.
- [x] **`RtpAncPacketizerContext::keepAliveField`** — F-bit config for
  keep-alive RTP packets; packetizer plumbs it into `RtpPayloadAnc`.
- [x] **`AncFormat::Op47Multipack`** (DID 0x43/SDID 0x03) and
  **`AncFormat::VbiSt2031`** (DID 0x41/SDID 0x08) registered.
- [x] **`AncSt2020Audio::PayloadDescriptorCompatibilityBit`** constant added
  (§5.4.1 bit 7; shall be 0 on Method-A packets).
- [x] **Property-based round-trip tests** — `tests/unit/anc_roundtrip_property.cpp`
  (Phase A/B/C): 200-iteration deterministic random walks over
  `St291Packet` build/from (Type-1 + Type-2), sentinel line/hOffset values,
  `RtpPayloadAnc` single-packet, multi-packet, Type-1 mixed, and MTU-split
  frames.  Also covers `AncPacket` identity contract (wire-buffer = identity,
  meta-only mutations don't alter wire bytes).
- [x] **RFC 8331 hot-path micro-opt** — `RtpPacket::List` factory allocates
  with 16-byte alignment instead of the default page-rounded allocation, eliminating
  4 KB waste per small ANC/audio packet.
- [x] **`utils/promeki-bench/cases/ancrtp.cpp`** — `ancrtp` bench suite:
  `pack_hd60`, `unpack_hd60`, `roundtrip_hd60` cases measuring RFC 8331
  TX/RX hot-path throughput at a representative ~20 ANC-packets/frame load.

---

## Phase P3 — CEA-608 conformance audit (landed 2026-05-23)

Full ANSI/CTA-608-E S-2019 pass: 60+ findings across four layers.
All items closed in a single changeset.

### XDS (`Cea608XdsExtractor` / `Cea608XdsInjector`)

New files: `include/promeki/cea608xds.h`, `include/promeki/cea608xdsinjector.h`,
`src/proav/cea608xds.cpp`, `src/proav/cea608xdsinjector.cpp`.

- [x] **`isEndOfProgramSentinel`** — moved to `Cea608XdsPacket` (correct home);
  former placement on the extractor was a misplaced helper.
- [x] **Composite-1 byte cap** — enforced the §9 32-byte informational-byte cap
  on the Composite Packet-1 payload.
- [x] **Reserved-bit validation** — `cgmsA` / `aspectRatio` / `channelMapPointer`
  accessors now reject packets with non-zero reserved bits per §9.1.
- [x] **§8.6.7 caption-interrupt suspension** — a caption or text control pair
  arriving mid-XDS-packet suspends the in-flight slot; the next Continue with
  matching (class, type) resumes rather than creating a second slot.
- [x] **§E.10 caption-yield** — `nextPair(hasCaptionPair)` holds the in-flight
  cursor when the caption encoder has claimed the F2 slot for the current frame.
- [x] **Overdue-tier escalation** — same-priority tie-breaking now uses longest-
  overdue source first so peer sources rotate fairly under bursty load.
- [x] **7 new round-trip tests** — `Cea608XdsPacket::encode` + `Cea608XdsExtractor`
  round-trips for Network Name, MPAA Content Advisory, Impulse Capture ID,
  Channel Map Pointer (reserved-bit rejection), Program Description Row,
  NWS Message, and the Future-class shadow of Program Name.

### Wire / charset (`Cea608` / `Cea608Ext`)

- [x] **`applyChannel` / `applyChannelInPlace`** — new helpers consolidating the
  §8.4 F2 misc-control remap (`0x14 → 0x15`, `0x1C → 0x1D`) and the
  intra-field channel-bit OR (`0x08`) into a single, tested function.  The
  encoder now uses a single post-pass channel-shift instead of duplicated
  inline remap logic.
- [x] **`isXdsControl` / `isXdsTerminator`** — named predicates replacing magic
  literal comparisons throughout the decoder.
- [x] **`TabOffsetB1` / `FgAttrB1` consolidated to `Cc1ExtAttrB1`** — both
  extended-attribute families share the same CC1 first byte (`0x17`); the
  duplicate constants were removed and callers updated.
- [x] **`kExtFrench` table rename** — array and accessor functions renamed
  `kExtPortugueseGerman` / `decodeExtPortugueseGerman` / etc. to match the
  actual content (§A.6 Spanish/Miscellaneous and Portuguese/German tables,
  not French).
- [x] **16 new wire-level TEST_CASEs** — `applyChannel` × 4 channels,
  `applyChannelInPlace`, `isXdsControl` / `isXdsTerminator` boundary probes.

### Decoder (`Cea608Decoder` / `Cea608Ext`)

- [x] **D1: §C.11 EOC-during-roll-up** — an EOC while roll-up is active now
  ends the live cue and blanks the screen instead of silently promoting it
  to displayed memory.
- [x] **D2: §C.10 RUx erases prior displayed** — switching INTO roll-up from
  pop-on / paint-on erases both memories per the spec; the prior pop-on cue
  is emitted with its end at the RUx timestamp.
- [x] **D3: §C.10 / §B.8.3 RCL no-clear** — RCL no longer erases
  non-displayed memory (ENM is the explicit clear); back-to-back RCLs
  preserve the loading slate.
- [x] **D4: §C.13 BS at column 32** — a BS received while the cursor is at
  column 32 (internal col 31) erases the character at col 31 in place rather
  than wrapping or ignoring.
- [x] **D5: §C.9 auto-erase for all modes** — the 16-second no-refresh
  auto-erase applies to pop-on, paint-on, and roll-up alike; formerly only
  paint-on was guarded.
- [x] **D6: §C.21 enable-side hysteresis** — after an auto-erase the decoder
  suppresses output until 15 consecutive parity-valid frames confirm signal
  restoration.
- [x] **D7: §C.10 paint-on overlay** — `displayedText()` / `displayedCue()`
  now overlay the live loading grid on top of any retained pop-on cue using
  `overlayGrids(under, over)` so "any displayed captioning shall be
  unaffected" when transitioning into paint-on.
- [x] **D9: §6.4.2 extended-char cursor decrement** — when an extended
  character arrives with no preceding placeholder, the decoder now decrements
  the cursor by one (if not at column 0) before writing, erasing whatever was
  there, per the spec fallback path.
- [x] **D12: §6.2 FON styled-space cell** — FON inserts a styled-space cell
  before switching to Flash opacity so the flash attribute attaches to the
  correct cell (mirrors the FA / FAU "automatic BS" contract).
- [x] **F2 misc-control remap via `applyChannel`** — decoder now uses the
  shared helper rather than per-site `0x14 → 0x15` inline remaps.

### Encoder (`Cea608Encoder`)

- [x] **E1: §B.8.3 ENM-after-RCL in pop-on** — a doubled ENM pair is emitted
  immediately after the doubled RCL pair in the pop-on pre-roll, ensuring the
  non-displayed memory is wiped clean before each new cue is loaded.  This
  is now load-bearing: the corresponding decoder D3 fix relies on the ENM
  being present.
- [x] **E4: §B.4 PAC-indent + MR split for coloured / italic rows** — rows
  that need both a non-zero indent and a colour / italic attribute now emit a
  PAC at the indent column followed by a Mid-Row code for the colour, instead
  of collapsing to flush-left with white.
- [x] **E11: §8.4 F2 misc-control remap scope** — the remap is now correctly
  scoped to the misc-control family (`b2 0x20..0x2F`) and is NOT applied to
  other control families (Special / Extended chars, PAC, Mid-Row); a
  regression that incorrectly remapped special-character first bytes on CC3
  is fixed.
- [x] **§B.11.5 0x01–0x0F guard assert** — an assertion blocks emission of
  raw bytes in the XDS-reserved range; these would corrupt F2 XDS framing if
  accidentally passed through the encoder.
- [x] **§6.2 mid-row preceded-space** — the encoder inserts a literal space
  before every Mid-Row code, honouring the spec requirement that the space be
  present on the wire even when the caller's text already starts at the
  transition boundary.
- [x] **Channel-shift post-pass via `applyChannel`** — the encoder now builds
  every control pair CC1-shaped and shifts to the target channel in a single
  post-pass instead of duplicating the inline remap logic.

### Phase P3 follow-ons (landed 2026-05-24)

Three bugs found during integration testing after P3 closed.

- [x] **Decoder: symmetric-gap anchor recovery** — `rowToAnchor` previously used
  a fixed first-column threshold (`col < 4 → Left`, `col < 24 → Center`) which
  silently collapsed any centered cue wider than ~24 chars to Left (e.g. a
  28-char centered cue lands at `firstCol = 2`, below the old threshold).
  Replaced with a symmetric-gap rule: `leftGap = firstCol`, `rightGap = 31 −
  lastCol`; Center when `|leftGap − rightGap| ≤ 1`, Left when `leftGap <
  rightGap`, Right when `leftGap > rightGap`.  The cell-grid's last-occupied
  column substitutes for the cue width that is absent from the PAC byte.
  2 new TEST_CASEs: wide centered cue round-trip × 3 anchor rows.

- [x] **Encoder: centered pre-roll flush-left fallback** — centered (and
  right-aligned) rows emit Tab Offset pairs in the pre-roll, adding frames
  before the cue's wire stream begins.  Cues starting close to media t=0 (or
  hot on the heels of a prior cue's EDM) could push `firstFrame` below zero or
  into the overlap boundary, causing the cue to be silently dropped.  The encoder
  now attempts a flush-left re-layout when either condition is detected: if the
  re-laid-out body fits the window the cue is kept at BottomLeft / etc. and a
  `promekiWarn` is emitted; only if even flush-left doesn't fit is the cue
  dropped.  1 new TEST_CASE: 28-char BottomCenter cue at frame 20 (centered
  pre-roll = 22 frames, flush-left = 20 frames) — asserts kept, anchor = BottomLeft.

- [x] **SubRip: BottomCenter default for un-marked cues** — `parseAnchorPrefix`
  previously set `outAnchor = SubtitleAnchor::Default` when no `{\anN}` prefix
  was found.  `Cea608Encoder` has no "no-hint" wire representation and treated
  Default as BottomLeft col 0, so SRT cues without explicit positioning were
  encoded flush-left and decoded back as BottomLeft instead of the broadcast
  convention (centered at the bottom).  `parseAnchorPrefix` now defaults to
  `BottomCenter`; `addAnchorPrefix` skips the `{\an2}` emission for BottomCenter
  to preserve the SRT round-trip for un-marked cues.

---

## Phase 2 follow-ups (deferred from Phase 2)

- [ ] `MediaConfig::AncTranslateConfig` key — set on a MediaIO
  to use this config for every translate / codec call on that
  backend. Per-call overrides are still possible by
  constructing a fresh config from
  `MediaConfig::AncTranslateConfig` and `merge`-ing per-format
  adjustments. Lands when the first MediaIO backend (NDI,
  RTMP) plumbs it.
- [ ] `TypeAncTranslateConfig` / `TypeAncPacket` Variant
  X-macro integration — currently deferred because the
  config / packet types transitively depend on Variant
  itself (circular include). DataStream + JSON round-trips
  work without Variant integration; revisit if a use case
  demands Variant carriage.
- [ ] 10-bit pack/unpack helper consolidation — split between
  `st291packet.cpp` (full pack/unpack) and `rtppayloadanc.cpp`
  (small `readWord10`). Once the typed Phase 3 parsers walk
  10-bit streams the pack/unpack should consolidate into
  `include/promeki/st291bits.h`.

---

## Phase 3 — Remaining typed parsers and builders

### AFD + Bar Data (full value type)

Phase 2 ships a minimal `uint8_t` Variant carrying AFD code +
AR bit. The full `Afd` value type with Bar Data is still
pending.

- [ ] `include/promeki/afd.h` + `src/proav/afd.cpp` —
  `Afd::Code` enum mirroring SMPTE 2016-1 (4-bit AFD codes),
  `Afd` value type `{Code code; BarData bars;}`,
  `static Result<Afd> parse(const AncPacket &, const AncTranslateConfig &)`,
  `static AncPacket build(const Afd &, AncTransport, const AncTranslateConfig &)`.
- [ ] `tests/unit/afd.cpp`.

### ATC LTC / VITC timecode helpers

Phase 2 ships the codec for `AtcLtc` / `AtcVitc1` / `AtcVitc2`
that round-trips through `Timecode`. A typed wrapper helper
class would tidy callsites.

- [ ] `include/promeki/atc.h` + `src/proav/atc.cpp` —
  `Atc::Kind` enum `{Ltc, Vitc1, Vitc2}`, bidirectional
  `formatFor(Kind)` / `kindOf(AncFormat)` mapping,
  `static Result<Timecode> parse(const AncPacket &, ...)`
  (kind inferred from `packet.format()`),
  `static AncPacket build(const Timecode &, Kind, AncTransport, ...)`.
- [ ] `tests/unit/atc.cpp`.

### SCTE-104

- [ ] `include/promeki/scte104.h` + `src/proav/scte104.cpp` —
  `Scte104Message` value type modelling the operation list +
  protocol version + splice request data, Variant integration
  `TypeScte104Message`, `parse` / `build` against St291.
- [ ] Initial sub-types: `SpliceRequest`, `TimeSignal`,
  `SegmentationDescriptor`. Other ops parse to an opaque
  blob.
- [ ] `tests/unit/scte104.cpp`.

### HDR static metadata over St291

The `HdrStaticMetadata` value type + `← / → HdmiInfoFrame`
codec landed 2026-05-15. The St291 carriage (SMPTE ST 2108-1)
also landed 2026-05-15.

- [x] `HdrStatic2086 ← / → St291` codec
  (`src/proav/anccodec_hdrstatic_st291.cpp`) — SMPTE ST 2108-1
  with DID=`0x41` / SDID=`0x0C`. AncFormat registration
  extended to include the ST 2108-1 DID/SDID so
  `AncFormat::fromSt291DidSdid` resolves capture packets to
  `HdrStatic2086`. UDW carries one or more Metadata Frames
  (`{Type, Length, Data Bytes}`). Builder emits a Type-0
  (Mastering Display, 26-byte HEVC payloadType=137 SEI body)
  frame when `MasteringDisplay::isValid()` and a Type-1 (Content
  Light Level, 6-byte HEVC payloadType=144 SEI body) frame when
  `ContentLightLevel::isValid()`; both frames go in one ANC
  packet per spec §5.3.2 / §5.3.3. Parser walks every frame in
  the UDW, accepts ST 2108-1 Type 0 + Type 1, and skips reserved /
  unknown frame types for forward compatibility (Types 2 / 6
  Dynamic Metadata land as separate codecs when needed). Chromaticities
  encoded big-endian as `value * 50000` per HEVC; luminances as
  `u(32)` in 0.0001 cd/m² units (HEVC convention, different from
  the CTA-861-G DRM InfoFrame's `u(16)` integer-cd
  encoding). ST 2108-1 does not carry the transfer
  characteristic, so the parsed `HdrStaticMetadata::eotf` is
  always `Unspecified` — application code recovers the EOTF
  from the SMPTE ST 352 Payload Identifier instead.
- [x] `tests/unit/anccodec_hdrstatic_st291.cpp` — registry
  capability, DID/SDID lookup, both-frames emission, MD+CLL
  round-trip with quantisation-aware tolerance, CLL-only build,
  unknown-frame-type skip, truncated-frame rejection,
  empty-descriptor rejection.

### HDR dynamic metadata (HDR10+ / ST 2094-40)

The `HdrDynamic2094_40` value type + `← / → HdmiInfoFrame` codec
landed 2026-05-15. The value type models the full SMPTE ST 2094-40
metadata syntax (application_version, num_windows + extra-window
geometry, targeted-system-display max + actual-peak grid,
per-window MaxSCL / AverageMaxRGB / distribution percentiles /
fraction-bright-pixels / Bezier tone-mapping curve /
colour-saturation weight, mastering-display actual-peak grid)
and round-trips through the canonical MSB-first bitstream via
`toBuffer()` / `fromBuffer()`. The HDMI codec wraps that
bitstream in a CEA-861 Vendor-Specific InfoFrame body
(`type=0x81`, `version=1`) prefixed by the 3-byte HDR10+ OUI
`0x90-84-8B` (LSB-first wire order); `AncTranslateConfig::HdmiInfoFrameOui`
overrides the OUI for third-party vendors. The St291 carriage is
the remaining gap.

- [x] `include/promeki/hdrdynamic2094_40.h` +
  `src/proav/hdrdynamic2094_40.cpp` — full structural value type.
  Variant payload type `TypeHdrDynamic2094_40` (DataStream tag
  `0x63`). DataStream + JSON + `toString` round-trips. Landed
  2026-05-15.
- [x] `HdrDynamic2094_40 ← / → HdmiInfoFrame` codec
  (`src/proav/anccodec_hdrdynamic2094_40.cpp`) — Vendor-Specific
  InfoFrame with HDR10+ OUI. Landed 2026-05-15.
- [x] `tests/unit/hdrdynamic2094_40.cpp` +
  `tests/unit/anccodec_hdrdynamic2094_40.cpp` — value-type
  round-trip, multi-window geometry, optional sub-structures,
  Variant + DataStream, HDMI codec parser/builder registration,
  custom OUI override.
- [x] `HdrDynamic2094_40 ← / → St291` codec (SMPTE ST 2108-2)
  landed 2026-05-15
  (`src/proav/anccodec_hdrdynamic2094_40_st291.cpp`).
  DID=`0x41` / SDID=`0x0D`. UDW = Packet-Count byte +
  HDR/WCG Metadata Message (u16 Length + sequence of KLV
  frames). One DMCVT App 4 Set Frame per processing window;
  Frame Key UL = `06.0E.2B.34.02.53.01.01.05.31.02.04.00.00.00.00`,
  Length is BER long-form (4 bytes: `0x83` + 3 length bytes per
  ST 2094-2 §6.1), Value is a sequence of {LocalTag (u16 BE),
  LocalLength (u16 BE), Value} items per ST 2094-2 Annex A
  Table A.2.  The wire-bitstream u(N) widths of
  @ref HdrDynamic2094_40 map 1:1 to ST 2094-2 Rational
  numerators for every field except
  `TargetedSystemDisplayMaximumLuminance` (wire u(27) in
  0.0001 cd/m² steps, Rational Den 100, so /100 on emit and
  ×100 on parse).
- [x] **Multi-packet split** (ST 2108-2 §5.3 / §5.4.1) — a full
  single-window HDR10+ Message (~290 bytes) exceeds the
  255-byte ST 291 UDW cap, so the builder splits the Message
  across multiple ANC packets with Packet Count incrementing
  from `0x01`.  The parser is registered as an
  `AncTranslator::MultiParserFn` so the framework hands it the
  full set of related packets at once; the codec sorts by
  Packet Count, validates the `[1, 2, ..., N]` sequence,
  concatenates the Message-bytes, and walks the KLV frames
  inside.  Limits: max 255 packets per Message (Packet Count
  is u8); out-of-order delivery handled via defensive sort;
  gaps in the sequence rejected with `CorruptData`.
- [x] `tests/unit/anccodec_hdrdynamic2094_40_st291.cpp` —
  15 cases covering single-packet fast path, multi-packet
  split + reassembly, multi-window descriptors, actual-peak
  grids, out-of-order packet handling, gap rejection, empty
  list rejection, single-packet parse on a multi-packet
  Message (truncated → error), §9.4 SHALL NOT strip at
  AppVer=1 (ColorSaturationWeight + numWindows>1 clamp),
  §9.3 warn-but-emit preservation at AppVer=0 (P2-26,
  2026-05-20).
- [x] **AncTranslator framework extensions** that landed
  alongside the codec (2026-05-20):
  - `BuilderFn` and `TranslatorFn` return type changed from
    `Result<AncPacket>` to `Result<List<AncPacket>>` so any
    codec can emit N wire packets per one logical message.
    All existing builders (Afd, Atc×3, Cea708×2, HdrStatic×2,
    HdrDynamic HDMI) updated to wrap their single result in a
    list.  Callers (`TpgMediaIO`, `NvencVideoEncoder`,
    `SubtitleBurnMediaIO` test) updated to iterate the list.
  - New `MultiParserFn` signature
    `Result<Variant>(*)(const List<AncPacket>&, const AncTranslateConfig&)`
    and `registerMultiParser` registry slot.  Multi-parsers
    take precedence on `parse(single packet)` (the framework
    wraps the packet in a 1-elem list) and on the new
    `parseGroup(packet list)` entry point.
  - `PROMEKI_REGISTER_ANC_MULTI_PARSER` macro for static-init
    registration.
  - `AncTranslator::parseGroup(AncPacket::List)` tested via
    P2-23 (multi-parser dispatch, `hasParser` reporting, single-
    packet `InsufficientContext` path) and P2-31 (idempotent
    re-registration / DEBUG collision rejection).

### KLV (MISB ST 0601)

- [ ] `include/promeki/klv0601.h` + `src/proav/klv0601.cpp` —
  `Klv0601` value type for the geolocation tags promeki cares
  about (precision timestamp, platform heading, sensor LLH,
  frame center, footprint).
- [ ] `parse` / `build` against `St291` (broadcast carriage)
  and `MpegTsPrivate` (transport-stream carriage).
- [ ] `tests/unit/klv0601.cpp`.

---

## Phase 4 — Remaining MediaIO backend integration

The framework (Phase 2), the first-class CEA-708 codec
(Phase 2b), the `Cea708 ← / → HlsSei` codec, the codec-base
Frame-shaped refactor, the ANC pairing fields on `AncDesc`,
and the NVENC SEI injection body all landed 2026-05-12. TPG
ingest (Phase 2b extended in Phase 3.5c) and Inspector egress
(Phase 2b) are also landed. The remaining pieces are the NDI
and RTMP ANC seams and the `AncMetadataStamper` pipeline
stage.

### NdiMediaIO

NDI metadata frames carry the closed-caption / AFD / timecode
equivalents as XML.

- [ ] **Source side:** for each incoming NDI metadata frame,
  identify the format via the top-level XML element. Build an
  `AncPacket` with `transport = NdiXml`, `data = <XML bytes>`,
  `meta = {NdiXml::ElementName = "..."}`. Attach to the
  outbound `Frame`'s `AncPayload`. Unknown XML elements kept
  as-is (`format = Invalid` with a user-registered fallback
  if one exists).
- [ ] **Sink side:** walk `frame.ancPayloads()`; for each
  packet, if `transport == NdiXml`, emit bytes verbatim;
  otherwise call `translator.translate(packet, NdiXml)`. Skip
  + log-once for packets with no translator.
- [ ] `Cea708 ← / → NdiXml` codec (needs XML carriage
  agreement).
- [ ] `Afd ← / → NdiXml`.
- [ ] Tests: NDI ANC round-trip (NDI → NDI byte-exact),
  cross-format (RTP-40 St291 captions → NDI XML captions).

### RtmpMediaIO

RTMP metadata carries via AMF0 script tags (`onMetaData`,
`onCaptionInfo`, `onCuePoint`). **For YouTube, Twitch, and
most modern CDN ingest the SEI path is what services actually
read** (already landed via `Cea708 ← / → HlsSei` + NVENC).
The AMF path is still useful for Facebook Live and older
OBS-derived endpoints.

- [ ] **Source side:** parse incoming `onCaptionInfo`,
  `onMetaData`, `onCuePoint` script tags into `AncPacket`
  with `transport = RtmpAmf`, `data = <AMF0 bytes>`,
  `meta = {RtmpAmf::ScriptName = "..."}`. Resolve format via
  the script-name table (`onCaptionInfo → Cea708`,
  `onCuePoint → Scte104`, …).
- [ ] **Sink side:** mirror — `RtmpAmf` packets ride through
  unchanged; others go through `translator.translate(...,
  RtmpAmf)`.
- [ ] `Cea708 ← / → RtmpAmf` codec — AMF0 `onCaptionInfo`
  decode.
- [ ] Tests: RTMP round-trip via the existing `RtmpClient`
  loopback fixture.

### AncMetadataStamper pipeline stage

Some ANC contents need to surface on the *frame's* `Metadata`,
not just inside the ANC payload (e.g. AFD code →
`Metadata::AspectRatio` on the paired VideoPayload).
Translators and codecs stay pure (AncPacket in, AncPacket out);
a separate pipeline stage walks the assembled Frame, runs
registered typed parsers, and stamps the matching `Metadata`
keys.

- [ ] `include/promeki/ancmetadatastamper.h` +
  `src/proav/ancmetadatastamper.cpp`.
- [ ] Each `Stamp` is a
  `(AncFormat::ID, function<void(const AncPacket &, Frame &)>)`
  pair, registered at static-init via
  `PROMEKI_REGISTER_ANC_STAMP`.
- [ ] Stage configuration: `List<AncFormat::ID>` filter
  (empty = run all).
- [ ] Default registered stamps:
  - `Afd` → `Metadata::AspectRatio` on the matched VideoPayload.
  - `AtcLtc` / `AtcVitc1` / `AtcVitc2` → `Metadata::Timecode`
    on the Frame.
  - `HdrStatic2086` → explodes the `HdrStaticMetadata`
    façade onto `Metadata::MasteringDisplay`,
    `Metadata::ContentLightLevel`, and
    `Metadata::VideoTransferCharacteristics` on the paired
    VideoPayload.
  - `Cea708` → `Metadata::HasCaptions = true`.
- [ ] `tests/unit/ancmetadatastamper.cpp`.

### NVENC SEI injection follow-ups

- [ ] Generalise `VideoSeiCaptionsEnabled` to a list-typed key
  (`VideoSeiCaptionFormats`) when the second SEI-bearing
  format lands (HDR dynamic metadata, KLV). `AncFormat` is a
  `TypeRegistry` not a `TypedEnum`, so the format set is
  currently hard-coded to `{AncFormat::Cea708}`.
- [ ] NVENC AV1 caption-OBU path — NVENC has no native
  support; today AV1 warns-once and emits no caption
  metadata.

### Future hooks

- [ ] **MXF container:** MXF carries ANC as KLV essence
  (SMPTE 436M). Lands when the MXF MediaIO does. New
  transport `MxfKlv` if needed at that time — or absorb
  into `MpegTsPrivate` if KLV semantics are close enough.

---

## Phase 4.5 — Frame-sync ANC policy

A pipeline that performs frame-rate conversion or absorbs
genlock drift decides, per output frame slot, whether to **play**
the next input frame, **drop** it, or **repeat** the previous
one. The video side of that decision is straightforward (drop
the whole frame / repeat the whole frame). The ANC side is not
— a repeated frame must not re-fire CEA-708 caption commands,
must not re-emit a SCTE-104 splice request, and must keep ATC
timecode incrementing rather than freeze it; a dropped frame
must not lose a queued splice cue. The policy is **per-format**,
parallel to how parsers / builders / translators are per-format.

### Disposition algebra

`FrameSyncDisposition` is a small value type with three variants:

```cpp
class FrameSyncDisposition {
public:
        enum Kind { Play, Drop, Repeat };
        static FrameSyncDisposition play();
        static FrameSyncDisposition drop();
        static FrameSyncDisposition repeat(uint8_t count = 1);

        Kind    kind() const;
        uint8_t repeatCount() const;   // valid when kind == Repeat
};
```

`Repeat` carries a count so a single decision can say "hold this
frame for N output slots" — important for codecs whose policy
depends on the repeat index (CEA-708 CDP `sequence_counter`, ATC
timecode increment, especially across drop-frame boundaries).
Stretch / interpolate dispositions are deferred; revisit when a
varispeed-capable backend lands.

### `AncSyncPolicy` — fourth ANC registry

Sits next to `ParserRegistry` / `BuilderRegistry` /
`TranslatorRegistry` inside `AncTranslator`, with the same
config-threading and registration-macro pattern.

```cpp
using SyncPolicyFn = Result<List<AncPacket>>(*)(
        const AncPacket             &pkt,
        FrameSyncDisposition         disposition,
        uint8_t                      repeatIndex,   // 0..count-1 within run
        const AncTranslateConfig    &cfg);
```

Return-value conventions:

- **Empty list** = drop this packet from the output frame.
- **One packet** = the policy's transformed packet (e.g. ATC with
  incremented timecode, CEA-708 CDP with framing-only payload).
- **>1 packet** = split / promote — used by the SCTE-104 stash
  promotion path so the next surviving frame emits both the
  stashed cue and any new packets that arrived.

Default fallback when no policy is registered for a format: on
`Repeat`, copy through; on `Drop`, drop. Logged once per format
per `AncTranslator` instance.

Per-codec defaults registered alongside each codec:

| Format               | On Repeat                                                                            | On Drop                              |
|----------------------|--------------------------------------------------------------------------------------|--------------------------------------|
| `AtcLtc/Vitc1/Vitc2` | Increment timecode by `repeatIndex` (idx=0 unchanged; idx=1 advances by one frame, …) | Drop                                 |
| `Cea708`             | `sequence_counter` advances by `repeatIndex`, every cc_construct triple `cc_valid=0` (idx=0 unchanged) | Drop (caption-decoder glitch ok)     |
| `Afd`                | Copy through (idempotent)                                                            | Drop                                 |
| `HdrStatic2086`      | Copy through (sticky)                                                                | Drop                                 |
| `HdrDynamic2094_40`  | Copy through (hold last sample)                                                      | Drop                                 |
| `Scte104`            | Drop (do **not** re-fire splice request)                                             | Stash → emit on next surviving frame |
| `Klv0601`            | Copy through (hold last sample)                                                      | Drop                                 |

Work items:

- [x] `AncTranslator::registerSyncPolicy(format, fn)` +
  `PROMEKI_REGISTER_ANC_SYNC_POLICY` macro mirroring the other
  three registry macros.
- [x] `AncTranslator::applySyncPolicy(packet, disposition,
  repeatIndex)` entry point.
- [x] Per-codec policies registered in `anccodec_atc.cpp`,
  `anccodec_cea708.cpp`, `anccodec_afd.cpp`,
  `anccodec_hdrstatic_st291.cpp`,
  `anccodec_hdrdynamic2094_40_st291.cpp`. SCTE-104 + KLV
  policies land with the Phase 3 codec for each.
- [x] `tests/unit/anctranslator_syncpolicy.cpp` — registry
  capability, default fallback, per-codec policy fan-out.

### `AncFrameSync` — pipeline stage

(Renamed from `FrameSync` to avoid collision with the existing
clock-paced `FrameSync` class in `include/promeki/framesync.h`.)

```cpp
class AncFrameSync {
public:
        explicit AncFrameSync(AncTranslateConfig cfg);

        // Apply the disposition to one input frame, producing
        // 0..N output frames (0 = Drop, 1 = Play / Repeat[1],
        // N = Repeat[N]). Each output frame's AncPayload is
        // walked through AncSyncPolicy with the appropriate
        // repeatIndex (0-based within the Repeat run).
        Result<List<Frame>> apply(Frame in, FrameSyncDisposition d);
};
```

The class keeps an internal **stash** buffer for codecs whose
Drop policy returns one or more packets that have to ride
forward (today: SCTE-104 splice cues; possibly KLV
precision-time later). The stash drains on the next `Play`.
Bounded at one packet per format per stash-eligible category — a
second drop while a cue is already stashed for that format
**replaces** the stashed packet (latest-wins) and logs once.
Open question: SCTE-104 cues with a future `splice_time` could
plausibly want a deeper queue; revisit if a real workflow shows
up.

A MediaIO backend that drives a hardware frame sync (AJA
Genlock, NDI internal sync) instantiates `FrameSync` the same
way: it consumes the input `Frame` plus the hardware's announced
disposition, then hands the resulting per-frame
`AncPacket::List` to the card to inject on output frame N.
Software frame syncs emit the resulting `Frame` to the next
pipeline strand. Same class body, different consumer at the
back.

Work items:

- [x] `include/promeki/framesyncdisposition.h` (new — value type
  split from the pipeline-class header).
- [x] `include/promeki/ancframesync.h` + `src/proav/ancframesync.cpp`
  — `AncFrameSync`, stash buffer, `apply()` driver loop.
- [x] `tests/unit/framesyncdisposition.cpp` — factories,
  default-construction, value semantics, equality, constexpr
  usability.
- [x] `tests/unit/ancframesync.cpp` — disposition matrix
  (Play / Drop / Repeat[1] / Repeat[3]) per registered format,
  default-fallback for an unregistered format, video-payload
  pointer shared across Repeat output frames, stash on Drop
  emits on next Play / Repeat[idx=0], latest-wins replacement
  on double-stash, drains create a fresh AncPayload when the
  next surviving frame has none.
- [x] `tests/unit/anccodec_atc.cpp` — `Repeat[3]` increments
  TC by `repeatIndex`; `Repeat` across the DF30 minute-rollover
  boundary (`00:00:59:29 → 00:01:00:02 → :03 → :04`); preserves
  the source packet's line / fieldB across re-encode.
- [x] `tests/unit/anccodec_cea708.cpp` — sequence_counter
  advances by `repeatIndex`; cc_construct triples invalidated;
  envelope (cc_count, frameRateCode) preserved.
- [x] `tests/unit/anccodec_afd.cpp`,
  `tests/unit/anccodec_hdrstatic_st291.cpp`,
  `tests/unit/anccodec_hdrdynamic2094_40_st291.cpp` — copy-through
  Play / Drop / Repeat assertions per codec.
- [ ] `tests/func/anc-framesync-23976-to-60/` — TPG @ 23.976
  with CEA-708 + ATC LTC, `AncFrameSync` driving 3:2 pulldown
  to 60p, verify cadence matches the disposition pattern,
  decoded captions byte-equal to source, ATC LTC monotonic
  across the output run.

### Open questions (revisit during build)

- **Null-payload CEA-708 CDP on Repeat.** A CDP with all
  cc_construct triples set to `cc_valid=0` is structurally valid
  per SMPTE 334-2 but no shipping encoder I've seen emits one.
  Verify downstream decoders don't choke; if any do, fall back
  to "emit no CDP at all on Repeat" and accept the timing glitch.
- **Hardware-sync disposition source.** AJA NTV2 surfaces
  drop/repeat events through its SDK; NDI internal sync hides
  them. A backend whose hardware doesn't report the disposition
  has to derive it from output-frame-count drift against the
  input clock. Bake the derivation into `FrameSync` itself or
  push it onto the backend? Probably the backend (FrameSync
  stays a pure transformer) but worth confirming when the AJA
  backend lands.

---

## Phase 5 — AJA NTV2 SDI capture contract

**Build scaffolding landed 2026-05-16.** `thirdparty/libajantv2` is
now a git submodule (AJA ntv2_18_0_0 / commit `4add452`).
`PROMEKI_ENABLE_NTV2` (default ON, requires PROAV) wires `ajantv2`
into `promeki` as a PRIVATE link target. The MediaIO backend itself
lands in a follow-up changeset.

The ANC stack ships with the contract the future SDI MediaIO must
satisfy so that backend is a drop-in producer. The contract is
captured here until `docs/proav/anc.dox` lands (Phase 6).

- [ ] An SDI capture MediaIO that produces ANC must:
  - For every captured frame, scan VANC (and HANC when
    requested) line ranges, build one `AncPayload` listing
    every ST 291 packet found. Each packet uses
    `transport = St291`, `data` set to the RFC 8331
    per-packet layout (DID, SDID, DataCount, packed UDW,
    checksum, padding), and `meta` populated with Line,
    HOffset, FieldB, CBit, StreamNum.
  - Resolve `AncFormat` via
    `AncFormat::fromSt291DidSdid(did, sdid)`. Unknown
    DID/SDID pairs use a fallback `AncFormat::Invalid`
    packet (still carried — wire fidelity preserved).
  - Set `AncDesc::sourceRaster` and `scanMode` to match the
    paired `ImageDesc` so line numbers are interpretable
    even when the ANC payload is consumed without the
    video.
  - Stamp `payload.duration` to one frame period of the
    session frame rate.
- [ ] **Output side** (SDI sink emitting ANC): the inverse —
  inject the listed ANC packets at the requested line
  numbers, recompute checksums on emit if a translator-built
  packet lacks one, and warn (don't error) when the requested
  line is outside the VBI/VANC region the current raster
  supports.
- [ ] **HDMI capture (future):** same pattern applies with
  `transport = HdmiInfoFrame` and
  `meta = {HdmiInfoFrame::Type, Version, Length}`.

---

## Phase 6 — Remaining inspection, tooling, demos

Most of Phase 6 landed: Inspector ANC JSONL dump (Phase 2b),
SubtitleRenderer + SubtitleBurnMediaIO + Cea608Anc / Cea708Anc
sources (2026-05-11/12), full CEA-708 DTVCC stack
(`Cea708Service`, `Cea708DtvccPacket`, `Cea708WindowState`,
`Cea708Encoder`, `Cea708Decoder` — 2026-05-12), full G2/G3
extended-character tables (2026-05-13), TPG 708 emission +
`CaptionCodec` typed enum (2026-05-12), `docs/captions.md`
(2026-05-14), `captions.cea608.subrip_roundtrip` +
`captions.cea708.subrip_roundtrip` promeki-test cases.

### TPG ANC emission generalisation

Phase 2b shipped CEA-708 as separate `TpgAncCaptions*` keys.
Generalisation to a unified emission list waits for the second
format to land.

- [ ] `MediaConfig::AncEmission` EnumList key (replaces the
  per-format `TpgAnc*` keys) once 2+ formats are wired up.
- [ ] Stepped ATC LTC timecode that follows the running frame
  counter — codec already registered; needs TPG-side glue
  mirroring the CEA-708 path.
- [ ] Cycling AFD code.
- [ ] Fixed-value HDR static metadata (now that the codec is
  landed).
- [ ] Periodic SCTE-104 splice signal (needs Phase 3 codec).

### Caption renderer follow-ups

- [ ] `SubtitleRenderer` edge style + true alpha blending for
  `Translucent` / `Flash` opacities. Today honours fg colour,
  per-span / per-cue bg rectangles (with `Transparent`
  suppression), italic / bold / underline, and the
  `Transparent` shortcut for fg / bg. Edge effects need a
  FreeType glyph-outline manipulation pass; partial opacity
  needs a paint-engine alpha-blend primitive.
- [ ] Inline-tag escape in the `SubRip` emitter — a span's
  text containing a literal `<X>` could collide with markup.
  Real-world cue text essentially never carries `<` so this
  is more hygiene than correctness.
- [ ] `VideoSubtitleBurnBgOpacity` (0..1 alpha) threading
  through `Color::setAlpha` — today's background draw is
  opaque-only.
- [ ] CEA-708 streaming roll-up — a real broadcast roll-up
  captioner persists a single defined+displayed window
  across adjacent RollUp cues and only emits CR + new line
  chars on transitions. Current per-cue DefineWindow approach
  is functionally correct (round-trips cleanly through
  `Cea708Decoder`) but emits more wire bytes than a streaming
  flow would.

### promeki-test functional matrix

- [ ] `tests/func/anc-rtp40-roundtrip/` — TPG (with CEA-708
  captions) → RTP-40 → receiver → byte-exact ANC compare.
  All the moving parts are landed; this is a wiring task.
- [ ] `tests/func/anc-ndi-roundtrip/` — needs the Phase 4 NDI
  ANC codec.
- [ ] `tests/func/anc-rtmp-captions/` — needs the Phase 4
  RTMP AMF CEA-708 codec.
- [ ] `tests/func/anc-youtube-sei-roundtrip/` — TPG with a
  SubRip caption file → NVENC encode → RTMP out to a local
  loopback server → ffmpeg-decode of the resulting FLV → SEI
  extraction → `Cea708 ← HlsSei` parser → `Cea608Decoder` →
  SubRip → diff against fixture. Proves the full
  YouTube-style delivery path.
- [ ] `tests/func/anc-mediapipeline-passthrough/` — ANC enters
  via one MediaIO, leaves via another with the same wire
  format, verifies byte-exact passthrough (no translation).
  Doable today on the TPG-emit → Inspector-receive leg; a
  passthrough sink (writing the same wire form back out)
  needs a small MediaIO addition.
- [ ] `tests/func/anc-cross-transport/` — ANC enters as
  St291, leaves as NdiXml or RtmpAmf, verifies semantic
  equivalence (typed parse on both ends matches). Needs
  Phase 4 NDI / RTMP ANC codecs.

### Documentation

- [ ] `docs/proav/anc.dox` — top-level chapter covering the
  generic packet, the descriptor, the two registries, the
  transport helpers, and the SDI / HDMI / MPEG-TS capture
  contracts. Migrates the **Decisions** and **Architecture
  overview** sections out of this devplan when it lands.
  Worked example showing how to receive ANC via RTP-40 and
  re-emit it on NDI.
- [ ] Update `docs/proav/mediaio.dox` to reference ANC
  alongside video/audio.

---

## Open questions / things to revisit during build

- **Multi-packet CDP (CEA-708 > 255 bytes).**  SMPTE 334-2 §6.3
  splits oversize CDPs across multiple ST 291 packets sharing a
  sequence counter. The current codec returns
  `Error::OutOfRange` for CDPs > 255 bytes. Wire when a real
  source emits a multi-packet CDP, or when caption-data density
  rises above the per-frame cap.
- **Per-line-number injection on sinks:** SDI emit ordering
  matters (some receivers care about VANC line monotonicity).
  Spec it in the SDI contract section but don't bake
  enforcement into `AncPayload` itself — leave it to the
  sink's emission stage.
- **Multi-link / 12G SDI stream numbering:** the `StreamNum`
  meta key on St291 packets is enough for capture/playback
  today; full 12G sub-image mapping (per ST 2082 / ST 2110-40
  sub-streams) waits on the AJA NTV2 backend confirming what
  it actually exposes.
- **HDMI capture timing:** HDMI InfoFrames arrive per video
  field/frame from the cable. The "AncPayload per Frame"
  model fits, but capture drivers that surface InfoFrames
  out-of-band (e.g. on EDID/HPD events) need a side channel.
  Probably a `MediaIO`-level metadata key rather than an
  AncPayload; revisit when the first HDMI MediaIO lands.
- **MPEG-TS carriage for SRT/HLS-in pipelines:** SCTE-35 and
  KLV in transport-stream private sections need a TS demuxer
  to surface to the AncPayload. The transport enum and format
  IDs ship now; the demuxer is a separate project's
  responsibility.
- **`TpgAncCaptionsEnabled` with no video.** A TPG configured
  with only ANC captions and no video / audio / timecode
  currently still passes open() and emits Frames with just an
  `AncPayload`. Whether downstream pipelines (planner, port
  connection, sinks) cope with a video-less Frame is not yet
  exercised end-to-end — the Inspector test pipelines always
  have video. Probably fine but worth a smoke test before
  promoting to a documented use case.
- **TPG frame-rate code mapping is closed-form.** Anything
  not in `{23.976, 24, 25, 29.97, 30, 50, 59.94, 60}` emits
  `frameRateCode = 0` (unknown). Round-trip is still
  structural; widen the table when a non-standard rate ships.
- **Per-codec checksum-policy doctest matrix.** The three-way
  `AncChecksumPolicy` matrix is wired but the per-codec
  doctest matrix is still pending (no codec yet emits a
  packet with a caller-provided checksum that differs from
  the recomputed one).
- **`HlsSei` naming.** Existing `AncTransport::HlsSei` enum
  value is a historical misnomer — the SEI mechanism is
  H.264-specific, not HLS-specific. `H264Sei` describes it
  better; renaming is a follow-on cleanup, not blocking.

## Tiny library follow-ups surfaced during build

- **`File::remove(path)` missing.** Inspector AncData test
  falls back to `std::remove(path.cstr())`. A static
  `File::remove(const String &)` would be a clean addition.
- **`Variant::get<T>()` for non-Variant T fails at link
  time.** Calling `Variant::get<JsonObject>()` produces a
  long mangled link error instead of a clear compile-time
  message. A
  `static_assert(detail::is_variant_type_v<T>, "T is not a registered Variant payload type")`
  inside `VariantImpl::get` would fail loudly at the call site.
- **`TypedEnum` not a Variant payload type — only base
  `Enum` is.** Calling `cfg.getAs<AncFidelity>` fails to
  link; the established pattern is
  `cfg.get(key).asEnum(AncFidelity::Type).value() == AncFidelity::Full.value()`.
  Consider whether `TypedEnum<Derived>` should grow a Variant
  payload specialisation that round-trips through the base
  `Enum` type.
- **Inspector AncData hex fallback migration.**
  `InspectorMediaIO::runAncDataCheck` still uses a per-site
  hex-dump loop; should migrate to `Buffer::toHex()` for
  consistency now that the helper exists.
